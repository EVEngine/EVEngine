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

float hash21(int x, int y) {
    uint32_t n = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u;
    n          = (n ^ (n >> 13u)) * 1274126177u;
    return float(n & 0x00ffffffu) / float(0x00ffffffu);
}

float valueNoise(float x, float y) {
    const int   x0  = int(std::floor(x));
    const int   y0  = int(std::floor(y));
    const float fx  = x - float(x0);
    const float fy  = y - float(y0);
    const float ux  = fx * fx * (3.f - 2.f * fx);
    const float uy  = fy * fy * (3.f - 2.f * fy);
    const float n00 = hash21(x0, y0);
    const float n10 = hash21(x0 + 1, y0);
    const float n01 = hash21(x0, y0 + 1);
    const float n11 = hash21(x0 + 1, y0 + 1);
    const float nx0 = n00 + (n10 - n00) * ux;
    const float nx1 = n01 + (n11 - n01) * ux;
    return nx0 + (nx1 - nx0) * uy;
}

float fbm(float x, float y) {
    float sum  = 0.f;
    float amp  = 0.5f;
    float freq = 1.f;
    for (int i = 0; i < 5; ++i) {
        sum += amp * valueNoise(x * freq, y * freq);
        freq *= 2.03f;
        amp *= 0.5f;
    }
    return sum;
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

  let cloudA = textureSample(mainTex, mainSampler, scrollUV(input.uv, tileA, speedA, time, 1.0)).rgb;
  let cloudB = textureSample(mainTex, mainSampler, scrollUV(input.uv, tileB, speedB, time, -1.0)).rgb;
  let cloud = mix(cloudA, cloudB, cloudMix);
  let noise = luma(cloud);
  let maskUv = input.uv + (noise - 0.5) * distort + fix;
  let maskSample = textureSample(maskTex, maskSampler, maskUv);
  let unlocked = maskSample.r;
  let selected = maskSample.g;
  let dissolve = maskSample.b;
  var fogKeep = 1.0 - smoothstep(0.5 - edgeSoft, 0.5 + edgeSoft, unlocked);
  let dissolveNoise = luma(textureSample(mainTex, mainSampler,
      input.uv * dissolveScale + vec2<f32>(time * 0.02, -time * 0.015)).rgb);
  fogKeep = fogKeep * step(dissolve, dissolveNoise + 1e-4);

  if (passMode < 0.5) {
    let sUv = maskUv + shadowOff;
    let sMask = textureSample(maskTex, maskSampler, sUv);
    var sFog = 1.0 - smoothstep(0.5 - edgeSoft, 0.5 + edgeSoft, sMask.r);
    let sDissolve = luma(textureSample(mainTex, mainSampler, (input.uv + shadowOff) * dissolveScale).rgb);
    sFog = sFog * step(sMask.b, sDissolve + 1e-4);
    let a = sFog * shadowStrength * fogAlpha * input.color.a;
    return vec4<f32>(0.0, 0.0, 0.0, a);
  }

  var col = fogRgb * mix(vec3<f32>(0.55), cloud, 0.85);
  let blink = selected * selectStrength * selectPulse;
  col = mix(col, col * 1.35 + vec3<f32>(0.18, 0.22, 0.28), clamp(blink, 0.0, 1.0));
  let a = fogKeep * fogAlpha * input.color.a;
  return vec4<f32>(col, a);
})";
        shader_                           = graphics_->newShaderFromWgsl({}, fragment);
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

Texture *MapFog::makeCloudTexture(int size) {
    const int            n = std::clamp(size, 16, 512);
    std::vector<uint8_t> rgba(size_t(n * n * 4));
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            // Seamless-ish by sampling in toroidal fashion at low frequency.
            const float   u  = float(x) / float(n);
            const float   v  = float(y) / float(n);
            const float   n0 = fbm(u * 4.f, v * 4.f);
            const float   n1 = fbm(u * 8.f + 17.1f, v * 8.f + 9.3f);
            const float   c  = std::clamp(n0 * 0.65f + n1 * 0.35f, 0.f, 1.f);
            const uint8_t b  = uint8_t(c * 255.f + 0.5f);
            const size_t  i  = size_t((y * n + x) * 4);
            rgba[i + 0]      = b;
            rgba[i + 1]      = b;
            rgba[i + 2]      = b;
            rgba[i + 3]      = 255;
        }
    }
    return graphics_->newTexture(n, n, rgba.data(), true, true);
}

void MapFog::ensureCloudTexture() {
    if (cloud_) return;
    if (!ownedCloud_) ownedCloud_ = makeCloudTexture(128);
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
}

void MapFog::draw(float x, float y, float width, float height) {
    if (!mask_ || width <= 0.f || height <= 0.f) return;
    ensureCloudTexture();
    Texture *cloud = cloud_ ? cloud_ : ownedCloud_;
    if (!cloud) return;

    // Article: shadow pass must run before the main cloud pass.
    if (shadowEnabled_ && shadowStrength_ > 0.f) {
        syncUniforms(0.f);
        graphics_->drawTexturedRectShaderDepth(cloud, mask_, shader_, x, y, width, height, Color(1.f, 1.f, 1.f, 1.f));
    }
    syncUniforms(1.f);
    graphics_->drawTexturedRectShaderDepth(cloud, mask_, shader_, x, y, width, height, Color(1.f, 1.f, 1.f, 1.f));
}

}  // namespace eve::graphics
