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
  let sampleA = textureSample(mainTex, mainSampler, scrollUV(cloudUv, tileA, speedA, time, 1.0)).rgb;
  let sampleB = textureSample(mainTex, mainSampler, scrollUV(cloudUv, tileB, speedB, time, -1.0)).rgb;
  let cloudMul = clamp(sampleA * sampleB * 1.35, vec3<f32>(0.0), vec3<f32>(1.0));
  let cloudAvg = mix(sampleA, sampleB, cloudMix);
  let cloud = mix(cloudAvg, cloudMul, 0.40);
  let noise = luma(cloud);

  let warpNoise = luma(mix(
    textureSample(mainTex, mainSampler, scrollUV(cloudUv, max(tileA * 0.22, 0.15), speedA * 0.30, time, 1.0)).rgb,
    textureSample(mainTex, mainSampler, scrollUV(cloudUv, max(tileB * 0.22, 0.15), speedB * 0.30, time, -1.0)).rgb,
    0.5));
  let maskUv = input.uv + (warpNoise - 0.5) * distort + fix;
  let maskSample = textureSample(maskTex, maskSampler, maskUv);
  let unlocked = maskSample.r;
  let selected = maskSample.g;
  let dissolve = maskSample.b;
  let softRadius = max(edgeSoft * 0.55, 0.012);
  let unlockedSoft = (unlocked
    + textureSample(maskTex, maskSampler, maskUv + vec2<f32>(softRadius, 0.0)).r
    + textureSample(maskTex, maskSampler, maskUv - vec2<f32>(softRadius, 0.0)).r
    + textureSample(maskTex, maskSampler, maskUv + vec2<f32>(0.0, softRadius)).r
    + textureSample(maskTex, maskSampler, maskUv - vec2<f32>(0.0, softRadius)).r
    + textureSample(maskTex, maskSampler, maskUv + vec2<f32>(softRadius, softRadius) * 0.707).r
    + textureSample(maskTex, maskSampler, maskUv + vec2<f32>(-softRadius, softRadius) * 0.707).r
    + textureSample(maskTex, maskSampler, maskUv + vec2<f32>(softRadius, -softRadius) * 0.707).r
    + textureSample(maskTex, maskSampler, maskUv + vec2<f32>(-softRadius, -softRadius) * 0.707).r) * (1.0 / 9.0);
  var fogKeep = 1.0 - smoothstep(0.5 - edgeSoft, 0.5 + edgeSoft, unlockedSoft);
  let dissolveNoise = luma(textureSample(mainTex, mainSampler,
      cloudUv * dissolveScale + vec2<f32>(time * 0.015, -time * 0.02)).rgb);
  fogKeep = fogKeep * (1.0 - smoothstep(dissolve - 0.10, dissolve + 0.10, dissolveNoise) * step(1e-4, dissolve));
  let density = smoothstep(densityBias, clamp(densityBias + densityContrast, 0.0, 1.0), noise);
  // Soft translucent cloud sheet — valleys thinner, peaks denser (not swiss cheese).
  let body = mix(0.52, 1.0, density);

  if (passMode < 0.5) {
    let sUv = maskUv + shadowOff;
    let sMask = textureSample(maskTex, maskSampler, sUv);
    let sUnlocked = (sMask.r
      + textureSample(maskTex, maskSampler, sUv + vec2<f32>(softRadius, 0.0)).r
      + textureSample(maskTex, maskSampler, sUv - vec2<f32>(softRadius, 0.0)).r
      + textureSample(maskTex, maskSampler, sUv + vec2<f32>(0.0, softRadius)).r
      + textureSample(maskTex, maskSampler, sUv - vec2<f32>(0.0, softRadius)).r) * 0.2;
    var sFog = 1.0 - smoothstep(0.5 - edgeSoft, 0.5 + edgeSoft, sUnlocked);
    let sDissolveNoise = luma(textureSample(mainTex, mainSampler,
        (cloudUv + shadowOff * vec2<f32>(aspect, 1.0)) * dissolveScale).rgb);
    sFog = sFog * (1.0 - smoothstep(sMask.b - 0.10, sMask.b + 0.10, sDissolveNoise) * step(1e-4, sMask.b));
    let a = sFog * body * shadowStrength * fogAlpha * input.color.a;
    return vec4<f32>(0.0, 0.0, 0.0, a);
  }

  var col = fogRgb * mix(vec3<f32>(0.62), cloud, 0.88);
  let blink = selected * selectStrength * selectPulse;
  col = mix(col, col * 1.22 + vec3<f32>(0.18, 0.22, 0.30), clamp(blink, 0.0, 1.0));
  let a = fogKeep * body * fogAlpha * input.color.a;
  return vec4<f32>(col, a);
})";
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
    std::vector<uint8_t> rgba(size_t(n * n * 4));
    // Integer-period wrap fBm is seamless by construction. Prefer LOW base
    // periods so one tile reads as a few large soft billows (article look),
    // not wallpaper static. Do NOT scale UV by non-integers here.
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float u = float(x) / float(n);
            const float v = float(y) / float(n);

            // Period 1/2/3 => a few huge soft masses per tile (not wallpaper micro-noise).
            const float large = fbmSeamless(u, v, 1, 0xA11CE001u, 5);
            const float mid   = fbmSeamless(u, v, 2, 0xBEEF42u, 4);
            const float fine  = fbmSeamless(u, v, 3, 0xC0FFEEu, 3);

            // Soft puffy volumes: billow dominates, fine ridge only for breakup.
            const float soft = billow(large) * 0.68f + billow(mid) * 0.24f + ridged(fine) * 0.08f;
            // Lift into a pale cloud range so dual-scroll multiply stays readable.
            const float c = std::clamp(0.38f + soft * 0.55f, 0.f, 1.f);

            const float cool = std::clamp(c * 0.96f + 0.03f, 0.f, 1.f);
            const float warm = std::clamp(c * 1.02f - 0.01f, 0.f, 1.f);
            const size_t i   = size_t((y * n + x) * 4);
            rgba[i + 0]      = uint8_t(warm * 255.f + 0.5f);
            rgba[i + 1]      = uint8_t(c * 255.f + 0.5f);
            rgba[i + 2]      = uint8_t(cool * 255.f + 0.5f);
            rgba[i + 3]      = 255;
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
