#include "stylize/StylePass.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"

#include <glm/vec4.hpp>

namespace eve::stylize {

StylePass::StylePass(const std::string &style, graphics::Shader *shader)
    : style_(style), shader_(shader) {
    if (!shader_) throw eve::Exception("StylePass: null shader for style '%s'", style.c_str());
}

bool StylePass::hasParam(const std::string &name) const {
    return shader_ && shader_->hasUniform(name);
}

void StylePass::setFloat(const std::string &name, float value) {
    if (!shader_) throw eve::Exception("StylePass.setFloat: null shader");
    shader_->sendFloat(name, value);
}

float StylePass::getFloat(const std::string &name) const {
    if (!shader_) throw eve::Exception("StylePass.getFloat: null shader");
    float v = 0.f;
    if (shader_->getFromVar(name, &v, sizeof(v)) != int(sizeof(v)))
        throw eve::Exception("StylePass.getFloat: missing param '%s'", name.c_str());
    return v;
}

void StylePass::setTime(float seconds) {
    if (hasParam("time")) setFloat("time", seconds);
}

float StylePass::getTime() const {
    if (!hasParam("time")) return 0.f;
    return getFloat("time");
}

void StylePass::uploadScreenUniforms(int width, int height) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (hasParam("texelW")) setFloat("texelW", 1.f / float(width));
    if (hasParam("texelH")) setFloat("texelH", 1.f / float(height));
    if (hasParam("screenW")) setFloat("screenW", float(width));
    if (hasParam("screenH")) setFloat("screenH", float(height));
}

void StylePass::apply(graphics::Graphics *gfx, graphics::Texture *source) {
    if (!gfx) throw eve::Exception("StylePass.apply: null graphics");
    if (!source) throw eve::Exception("StylePass.apply: null source texture");
    if (!shader_) throw eve::Exception("StylePass.apply: null shader");

    const int w = source->getWidth();
    const int h = source->getHeight();
    uploadScreenUniforms(w, h);

    // Prefer destination canvas size for pixel grid when available.
    if (auto *dst = gfx->getCanvas()) {
        if (hasParam("screenW")) setFloat("screenW", float(dst->getWidth()));
        if (hasParam("screenH")) setFloat("screenH", float(dst->getHeight()));
    } else {
        if (hasParam("screenW")) setFloat("screenW", float(gfx->getWidth()));
        if (hasParam("screenH")) setFloat("screenH", float(gfx->getHeight()));
    }

    const float dw = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float dh =
        gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());

    gfx->drawTexturedRectShader(source, shader_, 0.f, 0.f, dw, dh, glm::vec4(1.f, 1.f, 1.f, 1.f));
}

void StylePass::applyCanvas(graphics::Graphics *gfx, graphics::Canvas *source) {
    if (!source) throw eve::Exception("StylePass.applyCanvas: null canvas");
    graphics::Texture *tex = source->getTexture();
    if (!tex) throw eve::Exception("StylePass.applyCanvas: canvas has no sampleable texture");
    apply(gfx, tex);
}

}  // namespace eve::stylize
