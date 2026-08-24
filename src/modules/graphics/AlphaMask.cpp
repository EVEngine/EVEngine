#include "graphics/AlphaMask.h"

#include "common/Exception.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"

#ifndef EVENGINE_WEBGPU
#include "graphics/shaders/alpha_mask_frag_spv.inc"
#endif

#include <algorithm>
#include <vector>

namespace eve::graphics {

AlphaMask::AlphaMask(Graphics *graphics) : graphics_(graphics) {
    if (!graphics_) throw eve::Exception("AlphaMask: null graphics");
#ifdef EVENGINE_WEBGPU
    static const std::string fragment = R"(
struct Externals { data: array<f32, 32>, };
@group(0) @binding(0) var mainTex: texture_2d<f32>;
@group(0) @binding(1) var maskTex: texture_2d<f32>;
@group(0) @binding(2) var mainSampler: sampler;
@group(0) @binding(3) var maskSampler: sampler;
@group(0) @binding(4) var<uniform> ext: Externals;
struct In { @location(0) color: vec4<f32>, @location(1) uv: vec2<f32>, };
@fragment fn fs_main(input: In) -> @location(0) vec4<f32> {
  let color = textureSample(mainTex, mainSampler, input.uv) * input.color;
  let ms = textureSample(maskTex, maskSampler, input.uv);
  var m = ms.r;
  if (ext.data[2] > 0.5) { m = 1.0 - m; }
  let width = max(ext.data[1], 0.0001);
  let coverage = smoothstep(ext.data[0] - width, ext.data[0] + width, m);
  return vec4<f32>(color.rgb, color.a * coverage);
})";
    shader_ = graphics_->newShaderFromWgsl({}, fragment);
#else
    std::vector<uint32_t> fragment(alpha_mask_frag_spv,
                                   alpha_mask_frag_spv + alpha_mask_frag_spv_count);
    shader_ = graphics_->newShaderFromSpv({}, fragment);
#endif
    if (!shader_) throw eve::Exception("AlphaMask: shader creation failed");
    shader_->declareFloat("threshold");
    shader_->declareFloat("softness");
    shader_->declareFloat("inverted");
    syncUniforms();
}

AlphaMask::~AlphaMask() = default;

void AlphaMask::syncUniforms() {
    shader_->sendFloat("threshold", threshold_);
    shader_->sendFloat("softness", softness_);
    shader_->sendFloat("inverted", inverted_ ? 1.f : 0.f);
}

void AlphaMask::setThreshold(float threshold) {
    threshold_ = std::clamp(threshold, 0.f, 1.f);
    syncUniforms();
}

void AlphaMask::setSoftness(float softness) {
    softness_ = std::clamp(softness, 0.f, 0.5f);
    syncUniforms();
}

void AlphaMask::setInverted(bool inverted) {
    inverted_ = inverted;
    syncUniforms();
}

void AlphaMask::draw(Texture *color, Texture *mask, float x, float y, float width, float height,
                     float r, float g, float b, float a) {
    if (!color || !mask) return;
    graphics_->drawTexturedRectShaderDepth(color, mask, shader_, x, y, width, height,
                                           Color(r, g, b, a));
}

}  // namespace eve::graphics
