#include "graphics/DepthOfField.h"

#include <algorithm>
#include <string>
#include <vector>

#include "common/Exception.h"
#include "graphics/BlendMode.h"
#include "graphics/Canvas.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/PostProcessWgsl.h"
#include "graphics/shaders/dof_gaussian_frag_spv.inc"

namespace eve::graphics {
namespace {

std::vector<uint32_t> copySpv(const uint32_t *data, size_t count) {
    return std::vector<uint32_t>(data, data + count);
}

Shader *newDofShader(Graphics *gfx) {
    Shader *shader =
        gfx->getBackendName() == "webgpu"
            ? gfx->newShaderFromWgsl({}, std::string(shaders::kPostCommon) + shaders::kDofGaussian)
            : gfx->newShaderFromSpv({},
                                    copySpv(dof_gaussian_frag_spv, dof_gaussian_frag_spv_count));
    if (!shader || !shader->gpuHandle) throw eve::Exception("DepthOfField: failed to create shader");
    shader->declareFloat("texelW");
    shader->declareFloat("texelH");
    shader->declareFloat("directionX");
    shader->declareFloat("directionY");
    shader->declareFloat("focusDistance");
    shader->declareFloat("focusRange");
    shader->declareFloat("maxBlurPx");
    shader->declareFloat("nearZ");
    shader->declareFloat("farZ");
    return shader;
}

}  // namespace

DepthOfField::DepthOfField(Graphics *gfx) : gfx_(gfx) {
    if (!gfx_) throw eve::Exception("DepthOfField: null graphics");
    shader_ = newDofShader(gfx_);
}

void DepthOfField::ensureTargets(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (temp_ && width_ == width && height_ == height) return;
    width_ = width;
    height_ = height;
    temp_ = gfx_->newHDRCanvas(width, height);
    output_ = gfx_->newHDRCanvas(width, height);
    if (!temp_ || !output_) throw eve::Exception("DepthOfField: failed to allocate HDR targets");
}

void DepthOfField::configure(float dirX, float dirY, float focusDistance, float maxBlurPx,
                             float focusRange, float nearZ, float farZ, int width, int height) {
    shader_->sendFloat("texelW", 1.f / float(std::max(width, 1)));
    shader_->sendFloat("texelH", 1.f / float(std::max(height, 1)));
    shader_->sendFloat("directionX", dirX);
    shader_->sendFloat("directionY", dirY);
    shader_->sendFloat("focusDistance", std::max(focusDistance, 0.f));
    shader_->sendFloat("focusRange", std::max(focusRange, 1e-3f));
    shader_->sendFloat("maxBlurPx", std::max(maxBlurPx, 0.f));
    const float nearClip = nearZ > 1e-4f ? nearZ : 0.1f;
    const float farClip = farZ > nearClip ? farZ : nearClip + 1.f;
    shader_->sendFloat("nearZ", nearClip);
    shader_->sendFloat("farZ", farClip);
}

Texture *DepthOfField::apply(Texture *source, Texture *hwDepth, float focusDistance,
                             float maxBlurPx, float focusRange, float nearZ, float farZ) {
    if (!source) throw eve::Exception("DepthOfField.apply: null source");
    if (!hwDepth || maxBlurPx <= 0.f) return source;

    ensureTargets(source->getWidth(), source->getHeight());
    Canvas *previous = gfx_->getCanvas();

    temp_->clear(Color(0.f, 0.f, 0.f, 0.f), {}, {});
    gfx_->setCanvas(temp_);
    configure(1.f, 0.f, focusDistance, maxBlurPx, focusRange, nearZ, farZ, width_, height_);
    gfx_->drawTexturedRectShaderDepth(source, hwDepth, shader_, 0.f, 0.f, float(width_),
                                      float(height_), Color(1.f, 1.f, 1.f, 1.f));

    output_->clear(Color(0.f, 0.f, 0.f, 0.f), {}, {});
    gfx_->setCanvas(output_);
    configure(0.f, 1.f, focusDistance, maxBlurPx, focusRange, nearZ, farZ, width_, height_);
    gfx_->drawTexturedRectShaderDepth(temp_->getTexture(), hwDepth, shader_, 0.f, 0.f, float(width_),
                                      float(height_), Color(1.f, 1.f, 1.f, 1.f));

    gfx_->setCanvas(previous);
    Texture *result = output_->getTexture();
    if (!result) throw eve::Exception("DepthOfField: output texture missing");
    return result;
}

}  // namespace eve::graphics
