#include "graphics/Volumetric.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Drawable.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/volumetric_post_frag_spv.inc"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace eve::graphics {
namespace {

Shader *createVolumetricShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("Volumetric: null graphics");
    std::vector<uint32_t> frag(volumetric_post_frag_spv,
                               volumetric_post_frag_spv + volumetric_post_frag_spv_count);
    std::vector<uint32_t> vert;  // empty → default textured.vert
    Shader *sh = gfx->newShaderFromSpv(vert, frag);
    sh->declareFloat("lightX");
    sh->declareFloat("lightY");
    sh->declareFloat("exposure");
    sh->declareFloat("decay");
    sh->declareFloat("density");
    sh->declareFloat("weight");
    sh->declareFloat("sampleCount");
    sh->declareFloat("dustAmount");
    sh->declareFloat("fogAmount");
    sh->declareFloat("fogR");
    sh->declareFloat("fogG");
    sh->declareFloat("fogB");
    sh->declareFloat("shaftR");
    sh->declareFloat("shaftG");
    sh->declareFloat("shaftB");
    sh->declareFloat("time");
    sh->declareFloat("compositeMode");
    sh->declareFloat("intensity");

    sh->sendFloat("lightX", 0.5f);
    sh->sendFloat("lightY", 0.35f);
    sh->sendFloat("exposure", 0.35f);
    sh->sendFloat("decay", 0.96f);
    sh->sendFloat("density", 0.85f);
    sh->sendFloat("weight", 0.55f);
    sh->sendFloat("sampleCount", 48.f);
    sh->sendFloat("dustAmount", 0.25f);
    sh->sendFloat("fogAmount", 0.18f);
    sh->sendFloat("fogR", 0.55f);
    sh->sendFloat("fogG", 0.62f);
    sh->sendFloat("fogB", 0.75f);
    sh->sendFloat("shaftR", 1.f);
    sh->sendFloat("shaftG", 0.95f);
    sh->sendFloat("shaftB", 0.85f);
    sh->sendFloat("time", 0.f);
    sh->sendFloat("compositeMode", 0.f);
    sh->sendFloat("intensity", 1.f);
    return sh;
}

}  // namespace

Volumetric::Volumetric(Graphics *gfx) : gfx_(gfx) {
    shader_ = createVolumetricShader(gfx);
    applyQualityDefaults();
}

Volumetric::~Volumetric() = default;

void Volumetric::applyQualityDefaults() {
    if (quality_ == "low") {
        downscale_ = 4.f;
        setFloat("sampleCount", 16.f);
        setFloat("dustAmount", 0.12f);
        setFloat("fogAmount", 0.10f);
        setFloat("exposure", 0.40f);
    } else if (quality_ == "high") {
        downscale_ = 1.f;
        setFloat("sampleCount", 96.f);
        setFloat("dustAmount", 0.35f);
        setFloat("fogAmount", 0.22f);
        setFloat("exposure", 0.32f);
    } else {
        quality_ = "medium";
        downscale_ = 2.f;
        setFloat("sampleCount", 48.f);
        setFloat("dustAmount", 0.25f);
        setFloat("fogAmount", 0.18f);
        setFloat("exposure", 0.35f);
    }
}

void Volumetric::setQuality(const std::string &quality) {
    quality_ = quality;
    applyQualityDefaults();
}

void Volumetric::setLightScreenUV(float u, float v) {
    setFloat("lightX", u);
    setFloat("lightY", v);
}

float Volumetric::getLightScreenU() const { return getFloat("lightX"); }
float Volumetric::getLightScreenV() const { return getFloat("lightY"); }

void Volumetric::setLightScreenPos(float x, float y, float width, float height) {
    if (width < 1.f) width = 1.f;
    if (height < 1.f) height = 1.f;
    setLightScreenUV(x / width, y / height);
}

void Volumetric::setShaftColor(float r, float g, float b) {
    setFloat("shaftR", r);
    setFloat("shaftG", g);
    setFloat("shaftB", b);
}

void Volumetric::setFogColor(float r, float g, float b) {
    setFloat("fogR", r);
    setFloat("fogG", g);
    setFloat("fogB", b);
}

void Volumetric::setIntensity(float intensity) { setFloat("intensity", intensity); }

void Volumetric::setTime(float seconds) { setFloat("time", seconds); }

bool Volumetric::hasParam(const std::string &name) const {
    return shader_ && shader_->hasUniform(name);
}

void Volumetric::setFloat(const std::string &name, float value) {
    if (!shader_) throw eve::Exception("Volumetric.setFloat: null shader");
    shader_->sendFloat(name, value);
}

float Volumetric::getFloat(const std::string &name) const {
    if (!shader_) throw eve::Exception("Volumetric.getFloat: null shader");
    float v = 0.f;
    if (shader_->getFromVar(name, &v, sizeof(v)) != int(sizeof(v)))
        throw eve::Exception("Volumetric.getFloat: missing param '%s'", name.c_str());
    return v;
}

int Volumetric::getSampleCount() const { return int(std::lround(getFloat("sampleCount"))); }

int Volumetric::resolutionFor(int fullSize) const {
    if (fullSize < 1) return 1;
    const float ds = std::max(downscale_, 1.f);
    return std::max(1, int(std::floor(float(fullSize) / ds)));
}

void Volumetric::uploadCommon(bool compositeFromScene) {
    setFloat("compositeMode", compositeFromScene ? 1.f : 0.f);
}

void Volumetric::drawFullscreen(Graphics *gfx, Texture *source) {
    if (!gfx) throw eve::Exception("Volumetric: null graphics");
    if (!source) throw eve::Exception("Volumetric: null source texture");
    if (!shader_) throw eve::Exception("Volumetric: null shader");

    const float dw = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float dh =
        gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    gfx->drawTexturedRectShader(source, shader_, 0.f, 0.f, dw, dh, Color(1.f, 1.f, 1.f, 1.f));
}

void Volumetric::beginOcclusionMap(Graphics *gfx, float lightPixelX, float lightPixelY,
                                   float lightRadiusPixels) {
    if (!gfx) throw eve::Exception("Volumetric.beginOcclusionMap: null graphics");
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    const float r = std::max(lightRadiusPixels, 1.f);
    // Approximate disc with a bright square (good enough for radial blur sources).
    gfx->drawSolidRect(lightPixelX - r, lightPixelY - r, r * 2.f, r * 2.f,
                       Color(1.f, 1.f, 1.f, 1.f));
    const float w = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float h = gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    setLightScreenPos(lightPixelX, lightPixelY, w, h);
}

void Volumetric::drawOccluder(Graphics *gfx, Drawable *drawable, const glm::mat4 &matrix) {
    if (!gfx || !drawable) return;
    gfx->drawOcclusion(drawable, matrix);
}

void Volumetric::drawOccluderSolid(Graphics *gfx, float x, float y, float w, float h) {
    if (!gfx) return;
    gfx->drawOcclusionSolid(x, y, w, h);
}

void Volumetric::drawOccluderTexture(Graphics *gfx, Texture *texture, float x, float y, float w,
                                     float h) {
    if (!gfx) return;
    gfx->drawOcclusionTexture(texture, x, y, w, h);
}

void Volumetric::drawOccluders2D(Graphics *gfx) {
    if (!gfx) return;
    if (ecs::current()->getManager<Renderable2D>() == nullptr) return;
    auto view = ecs::View<Renderable2D, Renderable2D::Transform2D, Renderable2D::Sprite>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [xf, sp] = *it;
        if (!sp->visible || !sp->castOcclusion) continue;
        const float w = sp->width * (xf->sx == 0.f ? 1.f : xf->sx);
        const float h = sp->height * (xf->sy == 0.f ? 1.f : xf->sy);
        if (sp->texture)
            gfx->drawOcclusionTexture(sp->texture, xf->x, xf->y, w, h);
        else
            gfx->drawOcclusionSolid(xf->x, xf->y, w, h);
    }
}

void Volumetric::scatter(Graphics *gfx, Texture *occlusion) {
    uploadCommon(false);
    drawFullscreen(gfx, occlusion);
}

void Volumetric::scatterTo(Graphics *gfx, Texture *occlusion, Canvas *dest) {
    if (!gfx) throw eve::Exception("Volumetric.scatterTo: null graphics");
    if (!dest) throw eve::Exception("Volumetric.scatterTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    scatter(gfx, occlusion);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

void Volumetric::applyFromScene(Graphics *gfx, Texture *scene) {
    uploadCommon(true);
    drawFullscreen(gfx, scene);
}

void Volumetric::applyFromSceneTo(Graphics *gfx, Texture *scene, Canvas *dest) {
    if (!gfx) throw eve::Exception("Volumetric.applyFromSceneTo: null graphics");
    if (!dest) throw eve::Exception("Volumetric.applyFromSceneTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    applyFromScene(gfx, scene);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

}  // namespace eve::graphics
