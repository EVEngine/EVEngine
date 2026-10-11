#include "graphics/MapFog.h"

#include "common/Exception.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"

#include "graphics/shaders/map_fog_frag_spv.inc"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace eve::graphics {
namespace {

float hashWrap(int x, int y, int period, uint32_t seed) {
    x = ((x % period) + period) % period;
    y = ((y % period) + period) % period;
    uint32_t n = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + seed;
    n          = (n ^ (n >> 13u)) * 1274126177u;
    return float(n & 0x00ffffffu) / float(0x00ffffffu);
}

float valueNoiseWrap(float x, float y, int period, uint32_t seed) {
    const int   x0 = int(std::floor(x));
    const int   y0 = int(std::floor(y));
    const float fx = x - float(x0);
    const float fy = y - float(y0);
    // Quintic fade — softer ridges than the classic cubic hermite.
    const float ux = fx * fx * fx * (fx * (fx * 6.f - 15.f) + 10.f);
    const float uy = fy * fy * fy * (fy * (fy * 6.f - 15.f) + 10.f);
    const float n00 = hashWrap(x0, y0, period, seed);
    const float n10 = hashWrap(x0 + 1, y0, period, seed);
    const float n01 = hashWrap(x0, y0 + 1, period, seed);
    const float n11 = hashWrap(x0 + 1, y0 + 1, period, seed);
    const float nx0 = n00 + (n10 - n00) * ux;
    const float nx1 = n01 + (n11 - n01) * ux;
    return nx0 + (nx1 - nx0) * uy;
}

// True tileable fBm: each octave period is an integer multiple of the base so
// u=0 and u=1 hit identical lattice corners (no fmod phase-shift seams).
float fbmSeamless(float u, float v, int basePeriod, uint32_t seed, int octaves) {
    float sum    = 0.f;
    float amp    = 0.55f;
    float norm   = 0.f;
    int   period = std::max(basePeriod, 2);
    for (int i = 0; i < octaves; ++i) {
        sum += amp * valueNoiseWrap(u * float(period), v * float(period), period,
                                    seed + uint32_t(i) * 97u);
        norm += amp;
        period *= 2;
        amp *= 0.48f;
    }
    return (norm > 1e-6f) ? (sum / norm) : 0.f;
}

float billow(float n) { return 1.f - std::fabs(n * 2.f - 1.f); }

float ridged(float n) {
    const float r = 1.f - std::fabs(n * 2.f - 1.f);
    return r * r;
}

}  // namespace

MapFog::MapFog(Graphics *graphics) : graphics_(graphics) {
    if (!graphics_) throw eve::Exception("MapFog: null graphics");
    if (graphics_->getBackendName() == "webgpu") {
        static const std::string fragment = R"(
struct Externals { data: array<f32, 32>, };
@group(0) @binding(0) var mainTex: texture_2d<f32>;
@group(0) @binding(1) var maskTex: texture_2d<f32>;
@group(0) @binding(2) var mainSampler: sampler;
@group(0) @binding(3) var maskSampler: sampler;
@group(0) @binding(4) var<uniform> u: Externals;
struct In { @location(0) color: vec4<f32>, @location(1) uv: vec2<f32>, };

fn scrollUV(uv: vec2<f32>, tile: f32, speed: f32, time: f32, dir: f32) -> vec2<f32> {
  return uv * max(tile, 1e-4) + vec2<f32>(dir * speed * time, dir * speed * time * 0.73);
}
fn luma(c: vec3<f32>) -> f32 {
  return dot(c, vec3<f32>(0.299, 0.587, 0.114));
}
fn hash22(p: vec2<f32>) -> vec2<f32> {
  return fract(sin(vec2<f32>(dot(p, vec2<f32>(127.1, 311.7)),
                              dot(p, vec2<f32>(269.5, 183.3)))) * 43758.5453);
}
fn softenEdge(v: f32, soft: f32) -> f32 {
  let lo = clamp(0.5 - soft, 0.0, 1.0);
  let hi = clamp(0.5 + soft, 0.0, 1.0);
  let t = smoothstep(lo, hi, v);
  return pow(t, mix(1.0, 0.50, clamp(soft * 3.0, 0.0, 1.0)));
}
fn sampleUnlockSoft(uv: vec2<f32>, softRadius: f32) -> f32 {
  return (textureSample(maskTex, maskSampler, uv).r
    + textureSample(maskTex, maskSampler, uv + vec2<f32>(softRadius, 0.0)).r
    + textureSample(maskTex, maskSampler, uv - vec2<f32>(softRadius, 0.0)).r
    + textureSample(maskTex, maskSampler, uv + vec2<f32>(0.0, softRadius)).r
    + textureSample(maskTex, maskSampler, uv - vec2<f32>(0.0, softRadius)).r
    + textureSample(maskTex, maskSampler, uv + vec2<f32>(softRadius, softRadius) * 0.707).r
    + textureSample(maskTex, maskSampler, uv + vec2<f32>(-softRadius, softRadius) * 0.707).r
    + textureSample(maskTex, maskSampler, uv + vec2<f32>(softRadius, -softRadius) * 0.707).r
    + textureSample(maskTex, maskSampler, uv + vec2<f32>(-softRadius, -softRadius) * 0.707).r) * (1.0 / 9.0);
}

@fragment fn fs_main(input: In) -> @location(0) vec4<f32> {
  let time = u.data[0];
  let tileA = u.data[1];
  let tileB = u.data[2];
  let speedA = u.data[3];
  let speedB = u.data[4];
  let distort = u.data[5];
  let fix = vec2<f32>(u.data[6], u.data[7]);
  let fogRgb = vec3<f32>(u.data[8], u.data[9], u.data[10]);
  let fogAlpha = clamp(u.data[11], 0.0, 1.0);
  let shadowOff = vec2<f32>(u.data[12], u.data[13]);
  let shadowStrength = clamp(u.data[14], 0.0, 1.0);
  let passMode = u.data[15];
  let edgeSoft = max(u.data[16], 1e-4);
  let selectStrength = max(u.data[17], 0.0);
  let selectPulse = clamp(u.data[18], 0.0, 1.0);
  let dissolveScale = max(u.data[19], 0.25);
  let cloudMix = clamp(u.data[20], 0.0, 1.0);
  let densityContrast = max(u.data[21], 0.05);
  let densityBias = clamp(u.data[22], 0.0, 0.9);
  let aspect = max(u.data[23], 1e-4);

  let cloudUv = vec2<f32>(input.uv.x * aspect, input.uv.y);
  let sampleA = textureSample(mainTex, mainSampler, scrollUV(cloudUv, tileA, speedA, time, 1.0));
  let sampleB = textureSample(mainTex, mainSampler, scrollUV(cloudUv, tileB, speedB, time, -1.0));
  let cloudBase = mix(sampleA.rgb, sampleB.rgb, cloudMix);
  let cloudMul = clamp(sampleA.rgb * sampleB.rgb * 1.12, vec3<f32>(0.0), vec3<f32>(1.0));
  let cloud = mix(cloudBase, cloudMul, 0.28);
  let noise = luma(cloud);
  var cover = max(sampleA.a, sampleB.a) + sampleA.a * sampleB.a * 0.45;
  cover = clamp(cover, 0.0, 1.0);

  let softRadius = max(edgeSoft * 0.35, 0.008);
  let baseUv = input.uv + fix;
  // Hard unwarped clear core — warp must not drag fog into the unlock hole.
  let clearCore = smoothstep(0.90, 0.985,
      sampleUnlockSoft(baseUv, max(softRadius * 2.6, 0.018)));
  let warpScale = 1.0 - clearCore;

  let warpNoise = luma(mix(
    textureSample(mainTex, mainSampler, scrollUV(cloudUv, max(tileA * 0.20, 0.12), speedA * 0.28, time, 1.0)).rgb,
    textureSample(mainTex, mainSampler, scrollUV(cloudUv, max(tileB * 0.20, 0.12), speedB * 0.28, time, -1.0)).rgb,
    0.5));
  let warpFine = luma(mix(
    textureSample(mainTex, mainSampler, scrollUV(cloudUv, max(tileA * 0.55, 0.25), speedA * 0.55, time, 1.0)).rgb,
    textureSample(mainTex, mainSampler, scrollUV(cloudUv, max(tileB * 0.55, 0.25), speedB * 0.55, time, -1.0)).rgb,
    0.5));
  let maskUv = input.uv +
    ((warpNoise - 0.5) * distort + (warpFine - 0.5) * (distort * 0.35)) * warpScale + fix;
  let maskSample = textureSample(maskTex, maskSampler, maskUv);
  let selected = maskSample.g;
  let dissolve = maskSample.b;

  // Build one implicit surface from map distance + cloud height. The mask
  // moves the cloud boundary; it never clips individual lobes.
  let unlockedAmt = softenEdge(sampleUnlockSoft(maskUv, softRadius), edgeSoft);
  let wideUnlock = softenEdge(
      sampleUnlockSoft(maskUv, max(softRadius * 2.4, 0.040)),
      min(edgeSoft * 1.35, 0.48));
  let frontier = smoothstep(0.02, 0.20, wideUnlock) *
                 (1.0 - smoothstep(0.985, 1.0, wideUnlock));
  let puffGrid = 9.0;
  let drift = vec2<f32>(time * 0.035, time * 0.021);
  let puffPos = floor((cloudUv * puffGrid + drift) * 28.0) / 28.0;
  let puffCell = floor(puffPos);
  var puffField = 0.0;
  var puffShadow = 0.0;
  for (var py: i32 = -1; py <= 1; py = py + 1) {
    for (var px: i32 = -1; px <= 1; px = px + 1) {
      let cell = puffCell + vec2<f32>(f32(px), f32(py));
      let rnd = hash22(cell);
      let centre = cell + vec2<f32>(0.18) + rnd * 0.64;
      let radius = 0.62 + hash22(cell + vec2<f32>(19.7)).x * 0.18;
      let centreCloud = (centre - drift) / puffGrid;
      let centreUv = vec2<f32>(centreCloud.x / aspect, centreCloud.y) + fix;
      let centreMask = textureSample(maskTex, maskSampler, centreUv);
      // Keep the cluster alive while its Gaussian core contracts. The
      // reveal channel only releases it at the very end of the motion.
      let anchored = 1.0 - smoothstep(0.90, 0.995, centreMask.r);
      let dissolveAtCentre = clamp(centreMask.b, 0.0, 1.0);
      // Raising this Gaussian isosurface removes the low-density rim
      // first, so the cloud contracts organically toward its core.
      let contraction = smoothstep(0.0, 0.82, dissolveAtCentre);
      let cloudThreshold = mix(0.18, 0.92, contraction);
      let clusterScale = mix(1.0, 0.18, contraction);
      let axis = normalize(vec2<f32>(rnd.x - 0.5, rnd.y - 0.5) + vec2<f32>(0.17, 0.08));
      let side = vec2<f32>(-axis.y, axis.x);
      let centres = array<vec2<f32>, 4>(centre,
        centre + axis * radius * 0.62,
        centre - axis * radius * 0.55 + side * radius * 0.24,
        centre - side * radius * 0.58);
      let radii = array<f32, 4>(radius, radius * 0.72, radius * 0.66, radius * 0.58);
      var cluster = 0.0;
      var shadowCluster = 0.0;
      for (var ci: i32 = 0; ci < 4; ci = ci + 1) {
        let lobeCentre = centre + (centres[ci] - centre) * clusterScale;
        let sphereXY = (puffPos - lobeCentre) / max(radii[ci], 0.05);
        let gaussian = exp(-2.2 * dot(sphereXY, sphereXY));
        let lobe = smoothstep(cloudThreshold,
                              min(cloudThreshold + 0.12, 0.98), gaussian);
        cluster = max(cluster, lobe);
        let shadowXY = (puffPos - vec2<f32>(0.0, 0.12) - lobeCentre) /
                       max(radii[ci], 0.05);
        let shadowGaussian = exp(-2.2 * dot(shadowXY, shadowXY));
        shadowCluster = max(shadowCluster,
          smoothstep(cloudThreshold, min(cloudThreshold + 0.12, 0.98),
                     shadowGaussian));
      }
      // Fade throughout the contraction, not only in its last third.
      // Multiplying the complete cluster field preserves its silhouette:
      // lobes become smaller and paler together instead of being clipped.
      let earlyFade = 1.0 - 0.42 * contraction;
      let finalFade = 1.0 - smoothstep(0.88, 1.0, dissolveAtCentre);
      let alive = earlyFade * finalFade * anchored;
      puffField = max(puffField, cluster * alive);
      puffShadow = max(puffShadow, shadowCluster * alive);
    }
  }
  let bottomEdge = max(puffShadow - puffField, 0.0);
  // Coverage comes only from complete cloud clusters. A continuous backing
  // sheet reads as a white mask beneath the clouds and makes reveals abrupt.
  var fogKeep = max(puffField, bottomEdge);

  let density = smoothstep(densityBias, min(densityBias + densityContrast, 0.98),
                           mix(noise, cover, 0.82));
  let body = mix(0.96, 1.0, density);

  if (passMode < 0.5) {
    // Drop-shadow of the puff silhouette onto unlocked ground only:
    // sample cloud cover at uv - shadowOff so the cast matches the fog lobes.
    let groundVisible = 1.0 - fogKeep;
    let unlocked = smoothstep(0.38, 0.80, textureSample(maskTex, maskSampler, baseUv).r);
    let sUnlock = textureSample(maskTex, maskSampler, baseUv + shadowOff).r;
    let overhang = 1.0 - smoothstep(0.48, 0.84, sUnlock);
    let castUv = cloudUv - vec2<f32>(shadowOff.x * aspect, shadowOff.y);
    let castA = textureSample(mainTex, mainSampler, scrollUV(castUv, tileA, speedA, time, 1.0));
    let castB = textureSample(mainTex, mainSampler, scrollUV(castUv, tileB, speedB, time, -1.0));
    let castCover = clamp(max(castA.a, castB.a) + castA.a * castB.a * 0.45, 0.0, 1.0);
    let puffCast = smoothstep(0.12, 0.52, castCover);
    let a = groundVisible * unlocked * overhang * puffCast *
            shadowStrength * fogAlpha * input.color.a;
    return vec4<f32>(0.24, 0.29, 0.38, a * 0.42);
  }

  let cloudTop = vec3<f32>(0.945, 0.965, 0.965);
  let cloudBottom = vec3<f32>(0.737, 0.784, 0.800);
  let sculpted = cloudTop;
  let frontierPuffs = puffField * smoothstep(0.04, 0.46, wideUnlock);
  let sculptAmount = mix(0.04, 1.0, max(frontier, frontierPuffs));
  var col = mix(fogRgb, sculpted, sculptAmount);
  col = mix(col, cloudBottom, clamp(bottomEdge * 0.88, 0.0, 1.0));
  let blink = selected * selectStrength * selectPulse;
  col = mix(col, col * 1.08 + vec3<f32>(0.06, 0.10, 0.14), clamp(blink, 0.0, 1.0));
  let a = fogKeep * body * fogAlpha * input.color.a;
  return vec4<f32>(col, a);
}
)";
        shader_ = graphics_->newShaderFromWgsl({}, fragment);
    } else {
        std::vector<uint32_t> fragment(map_fog_frag_spv, map_fog_frag_spv + map_fog_frag_spv_count);
        shader_ = graphics_->newShaderFromSpv({}, fragment);
    }
    if (!shader_) throw eve::Exception("MapFog: shader creation failed");

    shader_->declareFloat("time");
    shader_->declareFloat("tileA");
    shader_->declareFloat("tileB");
    shader_->declareFloat("speedA");
    shader_->declareFloat("speedB");
    shader_->declareFloat("distort");
    shader_->declareFloat("fixX");
    shader_->declareFloat("fixY");
    shader_->declareFloat("fogR");
    shader_->declareFloat("fogG");
    shader_->declareFloat("fogB");
    shader_->declareFloat("fogAlpha");
    shader_->declareFloat("shadowOffX");
    shader_->declareFloat("shadowOffY");
    shader_->declareFloat("shadowStrength");
    shader_->declareFloat("passMode");
    shader_->declareFloat("edgeSoft");
    shader_->declareFloat("selectStrength");
    shader_->declareFloat("selectPulse");
    shader_->declareFloat("dissolveScale");
    shader_->declareFloat("cloudMix");
    shader_->declareFloat("densityContrast");
    shader_->declareFloat("densityBias");
    shader_->declareFloat("aspect");
}

MapFog::~MapFog() = default;

void MapFog::update(float dt) {
    if (dt > 0.f) time_ += dt;
}

void MapFog::setTime(float time) { time_ = time; }

void MapFog::setCloudTexture(Texture *cloud) { cloud_ = cloud; }

Texture *MapFog::getCloudTexture() {
    ensureCloudTexture();
    return cloud_ ? cloud_ : ownedCloud_;
}

void MapFog::setMaskTexture(Texture *mask) { mask_ = mask; }

void MapFog::setCloudTiling(float tileA, float tileB) {
    tileA_ = std::max(tileA, 0.01f);
    tileB_ = std::max(tileB, 0.01f);
}

void MapFog::setCloudSpeed(float speedA, float speedB) {
    speedA_ = speedA;
    speedB_ = speedB;
}

void MapFog::setDistort(float amount) { distort_ = std::max(amount, 0.f); }

void MapFog::setDistortFix(float x, float y) {
    fixX_ = x;
    fixY_ = y;
}

void MapFog::setFogColor(float r, float g, float b) {
    fogR_ = std::clamp(r, 0.f, 1.f);
    fogG_ = std::clamp(g, 0.f, 1.f);
    fogB_ = std::clamp(b, 0.f, 1.f);
}

void MapFog::setFogAlpha(float alpha) { fogAlpha_ = std::clamp(alpha, 0.f, 1.f); }

void MapFog::setEdgeSoftness(float softness) { edgeSoft_ = std::clamp(softness, 0.001f, 0.5f); }

void MapFog::setShadowEnabled(bool enabled) { shadowEnabled_ = enabled; }

void MapFog::setShadow(float offsetX, float offsetY, float strength) {
    shadowOffX_     = offsetX;
    shadowOffY_     = offsetY;
    shadowStrength_ = std::clamp(strength, 0.f, 1.f);
}

void MapFog::setSelectStrength(float strength) { selectStrength_ = std::max(strength, 0.f); }

void MapFog::setDissolveScale(float scale) { dissolveScale_ = std::max(scale, 0.25f); }

void MapFog::setCloudMix(float mix) { cloudMix_ = std::clamp(mix, 0.f, 1.f); }

void MapFog::setCloudDensity(float contrast, float bias) {
    densityContrast_ = std::max(contrast, 0.05f);
    densityBias_     = std::clamp(bias, 0.f, 0.9f);
}

Texture *MapFog::makeCloudTexture(int size) {
    const int            n = std::clamp(size, 16, 512);
    std::vector<float>   height(size_t(n * n), 0.f);
    std::vector<uint8_t> rgba(size_t(n * n * 4));
    // Reference FoW look: overlapping circular "cotton" puffs with per-blob
    // lighting (bright top-left / gray bottom-right), not continuous fBm wallpaper.
    // Toroidal stamps keep the map seamless for dual reverse-scroll.

    struct Puff {
        float u = 0.f;
        float v = 0.f;
        float r = 0.f;
    };
    std::vector<Puff> puffs;
    puffs.reserve(96);

    auto addLayer = [&](int gridX, int gridY, float radiusMin, float radiusMax, uint32_t seed) {
        for (int j = 0; j < gridY; ++j) {
            for (int i = 0; i < gridX; ++i) {
                const float ju = hashWrap(i, j, 1024, seed);
                const float jv = hashWrap(i, j, 1024, seed + 17u);
                const float jr = hashWrap(i, j, 1024, seed + 91u);
                Puff p;
                p.u = (float(i) + 0.20f + 0.60f * ju) / float(gridX);
                p.v = (float(j) + 0.20f + 0.60f * jv) / float(gridY);
                p.r = radiusMin + (radiusMax - radiusMin) * jr;
                puffs.push_back(p);
            }
        }
    };
    // Separated lobe families.  The previous 4x4 layer used radii up to 0.30,
    // covering almost every texel several times and collapsing the alpha into
    // a featureless sheet.  Deep fog is filled by the shader; this texture is
    // deliberately porous so the frontier can expose individual cotton puffs.
    addLayer(3, 3, 0.16f, 0.23f, 0xA11CE001u);
    addLayer(5, 5, 0.070f, 0.115f, 0xBEEF42u);
    addLayer(7, 7, 0.030f, 0.052f, 0xC0FFEEu);

    auto wrapDelta = [](float d) {
        d -= std::floor(d + 0.5f);
        return d;
    };

    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float u = (float(x) + 0.5f) / float(n);
            const float v = (float(y) + 0.5f) / float(n);
            float h = 0.f;
            for (const Puff &p : puffs) {
                const float dx = wrapDelta(u - p.u);
                const float dy = wrapDelta(v - p.v);
                const float d  = std::sqrt(dx * dx + dy * dy) / std::max(p.r, 1e-4f);
                if (d >= 1.f) continue;
                // Hard-core cotton lobe (reference FoW): flat top, short soft rim.
                float b = 1.f - d;
                b       = b * b * (3.f - 2.f * b);  // smoothstep
                h += b;
            }
            // Soft-max plateau: merged sheet with deeper valleys (terrain peeks).
            h = std::clamp(h * 0.90f, 0.f, 1.f);
            h = h * h * (3.f - 2.f * h);
            height[size_t(y * n + x)] = h;
        }
    }

    auto sampleH = [&](int x, int y) {
        x = ((x % n) + n) % n;
        y = ((y % n) + n) % n;
        return height[size_t(y * n + x)];
    };

    // Key light from upper-left — matches reference puff self-shadowing.
    const float lx     = 0.62f, ly = 0.72f, lz = 0.35f;
    const float invLen = 1.f / std::sqrt(lx * lx + ly * ly + lz * lz);
    const float Lx = lx * invLen, Ly = ly * invLen, Lz = lz * invLen;

    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float h  = sampleH(x, y);
            // A one-texel derivative is nearly flat on a 512px texture whose
            // major lobes span 70-100px.  A wider footprint restores the
            // directional light/dark roll visible on the reference clouds.
            const float hx = sampleH(x + 5, y) - sampleH(x - 5, y);
            const float hy = sampleH(x, y + 5) - sampleH(x, y - 5);

            // Directional lobe shading — milky top-left, cool gray underside.
            float nx = -hx * 4.8f, ny = -hy * 4.8f, nz = 0.42f;
            const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= nlen;
            ny /= nlen;
            nz /= nlen;
            const float ndotl = std::max(nx * Lx + ny * Ly + nz * Lz, 0.f);
            const float lit = 0.42f + 0.58f * ndotl;
            float shade     = std::clamp(0.28f + h * lit * 0.72f, 0.f, 1.f);
            shade           = std::pow(shade, 0.78f);

            const float coolR = 0.68f, coolG = 0.74f, coolB = 0.86f;
            const float warmR = 1.00f, warmG = 1.00f, warmB = 0.995f;
            const float t     = std::clamp(shade, 0.f, 1.f);
            const float r =
                std::clamp(coolR * (1.f - t) + warmR * t + ndotl * 0.04f * h, 0.f, 1.f);
            const float g =
                std::clamp(coolG * (1.f - t) + warmG * t + ndotl * 0.025f * h, 0.f, 1.f);
            const float b = std::clamp(coolB * (1.f - t) + warmB * t, 0.f, 1.f);

            // Opaque cores, clearer valleys — terrain peeks only in gaps.
            float a = std::clamp((h - 0.07f) / 0.25f, 0.f, 1.f);
            a       = a * a * (3.f - 2.f * a);  // smoothstep

            const size_t i = size_t((y * n + x) * 4);
            rgba[i + 0]    = uint8_t(r * 255.f + 0.5f);
            rgba[i + 1]    = uint8_t(g * 255.f + 0.5f);
            rgba[i + 2]    = uint8_t(b * 255.f + 0.5f);
            rgba[i + 3]    = uint8_t(a * 255.f + 0.5f);
        }
    }
    return graphics_->newTexture(n, n, rgba.data(), true, true);
}

void MapFog::ensureCloudTexture() {
    if (cloud_) return;
    if (!ownedCloud_) ownedCloud_ = makeCloudTexture(256);
    cloud_ = ownedCloud_;
}

void MapFog::syncUniforms(float passMode) {
    const float pulse = 0.55f + 0.45f * std::sin(time_ * 4.2f);
    shader_->sendFloat("time", time_);
    shader_->sendFloat("tileA", tileA_);
    shader_->sendFloat("tileB", tileB_);
    shader_->sendFloat("speedA", speedA_);
    shader_->sendFloat("speedB", speedB_);
    shader_->sendFloat("distort", distort_);
    shader_->sendFloat("fixX", fixX_);
    shader_->sendFloat("fixY", fixY_);
    shader_->sendFloat("fogR", fogR_);
    shader_->sendFloat("fogG", fogG_);
    shader_->sendFloat("fogB", fogB_);
    shader_->sendFloat("fogAlpha", fogAlpha_);
    shader_->sendFloat("shadowOffX", shadowOffX_);
    shader_->sendFloat("shadowOffY", shadowOffY_);
    shader_->sendFloat("shadowStrength", shadowStrength_);
    shader_->sendFloat("passMode", passMode);
    shader_->sendFloat("edgeSoft", edgeSoft_);
    shader_->sendFloat("selectStrength", selectStrength_);
    shader_->sendFloat("selectPulse", pulse);
    shader_->sendFloat("dissolveScale", dissolveScale_);
    shader_->sendFloat("cloudMix", cloudMix_);
    shader_->sendFloat("densityContrast", densityContrast_);
    shader_->sendFloat("densityBias", densityBias_);
    shader_->sendFloat("aspect", drawAspect_);
}

void MapFog::draw(float x, float y, float width, float height) {
    if (!mask_ || width <= 0.f || height <= 0.f) return;
    ensureCloudTexture();
    Texture *cloud = cloud_ ? cloud_ : ownedCloud_;
    if (!cloud) return;

    drawAspect_ = width / std::max(height, 1e-4f);

    // Article: shadow pass must run before the main cloud pass.
    if (shadowEnabled_ && shadowStrength_ > 0.f) {
        syncUniforms(0.f);
        graphics_->drawTexturedRectShaderDepth(cloud, mask_, shader_, x, y, width, height,
                                               Color(1.f, 1.f, 1.f, 1.f));
    }
    syncUniforms(1.f);
    graphics_->drawTexturedRectShaderDepth(cloud, mask_, shader_, x, y, width, height,
                                           Color(1.f, 1.f, 1.f, 1.f));
}

}  // namespace eve::graphics
