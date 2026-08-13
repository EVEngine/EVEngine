#include "graphics/AntiAliasing.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/aa_fxaa_frag_spv.inc"
#include "graphics/shaders/aa_nfaa_frag_spv.inc"
#include "graphics/shaders/aa_smaa_frag_spv.inc"
#include "graphics/shaders/aa_ssaa_frag_spv.inc"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/vec4.hpp>

namespace eve::graphics {
namespace {

std::vector<uint32_t> copySpv(const uint32_t *data, size_t count) {
    return std::vector<uint32_t>(data, data + count);
}

Shader *createFxaaShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AntiAliasing: null graphics");
    auto frag = copySpv(aa_fxaa_frag_spv, aa_fxaa_frag_spv_count);
    Shader *sh = gfx->newShaderFromSpv({}, frag);
    if (!sh || !sh->gpuHandle) throw eve::Exception("AntiAliasing: failed to create FXAA shader");
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("edgeThreshold");
    sh->declareFloat("edgeThresholdMin");
    sh->declareFloat("subpix");
    sh->declareFloat("quality");
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    sh->sendFloat("edgeThreshold", 0.166f);
    sh->sendFloat("edgeThresholdMin", 0.0833f);
    sh->sendFloat("subpix", 0.75f);
    sh->sendFloat("quality", 1.f);
    return sh;
}

Shader *createSmaaShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AntiAliasing: null graphics");
    auto frag = copySpv(aa_smaa_frag_spv, aa_smaa_frag_spv_count);
    Shader *sh = gfx->newShaderFromSpv({}, frag);
    if (!sh || !sh->gpuHandle) throw eve::Exception("AntiAliasing: failed to create SMAA shader");
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("threshold");
    sh->declareFloat("localContrast");
    sh->declareFloat("maxSearch");
    sh->declareFloat("cornerRounding");
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    sh->sendFloat("threshold", 0.1f);
    sh->sendFloat("localContrast", 0.5f);
    sh->sendFloat("maxSearch", 8.f);
    sh->sendFloat("cornerRounding", 0.25f);
    return sh;
}

Shader *createSsaaShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AntiAliasing: null graphics");
    auto frag = copySpv(aa_ssaa_frag_spv, aa_ssaa_frag_spv_count);
    Shader *sh = gfx->newShaderFromSpv({}, frag);
    if (!sh || !sh->gpuHandle) throw eve::Exception("AntiAliasing: failed to create SSAA shader");
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("scale");
    sh->declareFloat("kernel");
    sh->declareFloat("sampleRadius");
    sh->sendFloat("texelW", 1.f / 512.f);
    sh->sendFloat("texelH", 1.f / 512.f);
    sh->sendFloat("scale", 2.f);
    sh->sendFloat("kernel", 1.f);  // tent
    sh->sendFloat("sampleRadius", 1.f);
    return sh;
}

Shader *createNfaaShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AntiAliasing: null graphics");
    auto frag = copySpv(aa_nfaa_frag_spv, aa_nfaa_frag_spv_count);
    Shader *sh = gfx->newShaderFromSpv({}, frag);
    if (!sh || !sh->gpuHandle) throw eve::Exception("AntiAliasing: failed to create NFAA shader");
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("strength");
    sh->declareFloat("power");
    sh->declareFloat("blurScale");
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    sh->sendFloat("strength", 1.f);
    sh->sendFloat("power", 1.f);
    sh->sendFloat("blurScale", 1.f);
    return sh;
}

}  // namespace

AntiAliasing::AntiAliasing(Graphics *gfx) : gfx_(gfx) {
    fxaa_ = createFxaaShader(gfx);
    smaa_ = createSmaaShader(gfx);
    ssaa_ = createSsaaShader(gfx);
    nfaa_ = createNfaaShader(gfx);
    applyQualityDefaults();
}

AntiAliasing::~AntiAliasing() = default;

void AntiAliasing::applyQualityDefaults() {
    if (quality_ == "low") {
        fxaa_->sendFloat("edgeThreshold", 0.25f);
        fxaa_->sendFloat("edgeThresholdMin", 0.0833f);
        fxaa_->sendFloat("subpix", 0.5f);
        fxaa_->sendFloat("quality", 0.f);
        smaa_->sendFloat("threshold", 0.15f);
        smaa_->sendFloat("localContrast", 0.55f);
        smaa_->sendFloat("maxSearch", 4.f);
        smaa_->sendFloat("cornerRounding", 0.1f);
        ssaa_->sendFloat("scale", 2.f);
        ssaa_->sendFloat("kernel", 0.f);  // box
        ssaa_->sendFloat("sampleRadius", 0.75f);
        nfaa_->sendFloat("strength", 0.7f);
        nfaa_->sendFloat("power", 1.2f);
        nfaa_->sendFloat("blurScale", 0.85f);
        return;
    }
    if (quality_ == "high") {
        fxaa_->sendFloat("edgeThreshold", 0.125f);
        fxaa_->sendFloat("edgeThresholdMin", 0.0312f);
        fxaa_->sendFloat("subpix", 0.85f);
        fxaa_->sendFloat("quality", 2.f);
        smaa_->sendFloat("threshold", 0.05f);
        smaa_->sendFloat("localContrast", 0.4f);
        smaa_->sendFloat("maxSearch", 16.f);
        smaa_->sendFloat("cornerRounding", 0.4f);
        ssaa_->sendFloat("scale", 4.f);
        ssaa_->sendFloat("kernel", 2.f);  // gaussian
        ssaa_->sendFloat("sampleRadius", 2.f);
        nfaa_->sendFloat("strength", 1.25f);
        nfaa_->sendFloat("power", 0.85f);
        nfaa_->sendFloat("blurScale", 1.35f);
        return;
    }
    // medium (default / unknown)
    quality_ = "medium";
    fxaa_->sendFloat("edgeThreshold", 0.166f);
    fxaa_->sendFloat("edgeThresholdMin", 0.0833f);
    fxaa_->sendFloat("subpix", 0.75f);
    fxaa_->sendFloat("quality", 1.f);
    smaa_->sendFloat("threshold", 0.1f);
    smaa_->sendFloat("localContrast", 0.5f);
    smaa_->sendFloat("maxSearch", 8.f);
    smaa_->sendFloat("cornerRounding", 0.25f);
    ssaa_->sendFloat("scale", 2.f);
    ssaa_->sendFloat("kernel", 1.f);  // tent
    ssaa_->sendFloat("sampleRadius", 1.25f);
    nfaa_->sendFloat("strength", 1.f);
    nfaa_->sendFloat("power", 1.f);
    nfaa_->sendFloat("blurScale", 1.f);
}

void AntiAliasing::setQuality(const std::string &quality) {
    if (quality == "low" || quality == "medium" || quality == "high")
        quality_ = quality;
    else
        quality_ = "medium";
    applyQualityDefaults();
}

void AntiAliasing::setMode(const std::string &mode) {
    if (mode == "fxaa" || mode == "smaa" || mode == "ssaa" || mode == "nfaa")
        mode_ = mode;
    else
        mode_ = "fxaa";
}

Shader *AntiAliasing::shaderForMode() const {
    if (mode_ == "smaa") return smaa_;
    if (mode_ == "ssaa") return ssaa_;
    if (mode_ == "nfaa") return nfaa_;
    return fxaa_;
}

Shader *AntiAliasing::getShader() const { return shaderForMode(); }

bool AntiAliasing::hasParam(const std::string &name) const {
    Shader *sh = shaderForMode();
    return sh && sh->hasUniform(name);
}

void AntiAliasing::setFloat(const std::string &name, float value) {
    Shader *sh = shaderForMode();
    if (!sh) throw eve::Exception("AntiAliasing.setFloat: null shader");
    sh->sendFloat(name, value);
}

float AntiAliasing::getFloat(const std::string &name) const {
    Shader *sh = shaderForMode();
    if (!sh) throw eve::Exception("AntiAliasing.getFloat: null shader");
    float v = 0.f;
    if (sh->getFromVar(name, &v, sizeof(v)) != int(sizeof(v)))
        throw eve::Exception("AntiAliasing.getFloat: missing param '%s'", name.c_str());
    return v;
}

float AntiAliasing::suggestScale() const {
    if (mode_ != "ssaa") return 1.f;
    if (quality_ == "high") return 4.f;
    return 2.f;
}

int AntiAliasing::resolutionFor(int destSize) const {
    if (destSize < 1) destSize = 1;
    const float s = suggestScale();
    return std::max(1, int(std::floor(float(destSize) * s)));
}

void AntiAliasing::uploadScreenUniforms(Texture *source) {
    if (!source) throw eve::Exception("AntiAliasing: null source texture");
    const int w = std::max(1, source->getWidth());
    const int h = std::max(1, source->getHeight());
    const float tw = 1.f / float(w);
    const float th = 1.f / float(h);
    Shader *sh = shaderForMode();
    if (sh->hasUniform("texelW")) sh->sendFloat("texelW", tw);
    if (sh->hasUniform("texelH")) sh->sendFloat("texelH", th);
    if (mode_ == "ssaa" && sh->hasUniform("scale")) {
        // Keep user override if they set scale explicitly after setQuality;
        // still ensure scale reflects suggestScale when it matches the quality default.
        // Prefer the quality-driven suggestScale unless caller already changed it away
        // from {2,4}. Here we always sync scale to suggestScale for predictability.
        sh->sendFloat("scale", suggestScale());
    }
}

void AntiAliasing::drawFullscreen(Graphics *gfx, Texture *source, Shader *shader) {
    if (!gfx) throw eve::Exception("AntiAliasing: null graphics");
    if (!source) throw eve::Exception("AntiAliasing: null source texture");
    if (!shader) throw eve::Exception("AntiAliasing: null shader");
    uploadScreenUniforms(source);
    const float dw = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float dh =
        gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    gfx->drawTexturedRectShader(source, shader, 0.f, 0.f, dw, dh, glm::vec4(1.f, 1.f, 1.f, 1.f));
}

void AntiAliasing::prepareSource(Texture *source) { uploadScreenUniforms(source); }

void AntiAliasing::apply(Graphics *gfx, Texture *source) {
    drawFullscreen(gfx, source, shaderForMode());
}

void AntiAliasing::applyTo(Graphics *gfx, Texture *source, Canvas *dest) {
    if (!gfx) throw eve::Exception("AntiAliasing.applyTo: null graphics");
    if (!dest) throw eve::Exception("AntiAliasing.applyTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    apply(gfx, source);
    gfx->setCanvas(prev);
}

void AntiAliasing::applyCanvas(Graphics *gfx, Canvas *source) {
    if (!source) throw eve::Exception("AntiAliasing.applyCanvas: null source");
    Texture *tex = source->getTexture();
    if (!tex) throw eve::Exception("AntiAliasing.applyCanvas: source has no sampleable texture");
    apply(gfx, tex);
}

void AntiAliasing::applyCanvasTo(Graphics *gfx, Canvas *source, Canvas *dest) {
    if (!source) throw eve::Exception("AntiAliasing.applyCanvasTo: null source");
    Texture *tex = source->getTexture();
    if (!tex) throw eve::Exception("AntiAliasing.applyCanvasTo: source has no sampleable texture");
    applyTo(gfx, tex, dest);
}

}  // namespace eve::graphics
