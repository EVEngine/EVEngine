#include "decal/Decal.h"

#include "common/Exception.h"
#include "decal/ProceduralDecal.h"
#include "graphics/Graphics.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::decal {

struct Decal::ProceduralBakeCache {
    std::string            source;
    std::uint32_t          seed       = 0;
    int                    resolution = 0;
    ProceduralDecalBake    bake;
};

namespace {
const std::vector<std::uint8_t> &bakeChannel(const ProceduralDecalBake &bake,
                                              const std::string &channel) {
    if (channel == "albedo") return bake.albedo;
    if (channel == "normal") return bake.normal;
    if (channel == "params") return bake.params;
    throw eve::Exception("Decal procedural channel must be albedo, normal, or params");
}

void requireBakeChannel(const std::string &channel) {
    if (channel != "albedo" && channel != "normal" && channel != "params")
        throw eve::Exception("Decal procedural channel must be albedo, normal, or params");
}
}  // namespace

Module_IMPL(Decal, new Decal());

Decal::Decal() : proceduralBakeCache_(std::make_unique<ProceduralBakeCache>()) {
    registerDecalCapabilities();
    graphics::RenderSystem3D::addDecalExtraDrawer(
        [](graphics::Graphics &gfx, const graphics::Camera3D::Data &cam,
           const glm::mat4 &viewProj, float aspect) {
            DecalManager::inst().drawAll(gfx, cam.eyeX, cam.eyeY, cam.eyeZ, viewProj, aspect);
        });
}

Decal::~Decal() = default;

int Decal::project(float x, float y, float z, float nx, float ny, float nz,
                   graphics::Texture *albedo, const std::string &kind, float size, float depth,
                   bool randomYaw, int seed, float fadeIn, float lifetime, float fadeOut) {
    if (!albedo) throw eve::Exception("Decal.project: null albedo texture");
    return DecalManager::inst().project(x, y, z, nx, ny, nz, albedo, kind, size, depth, randomYaw,
                                        seed, fadeIn, lifetime, fadeOut, 0.f, 0.f, 0.f, 0.f);
}

bool Decal::setStrength(int id, float normalStrength, float roughnessStrength, float metalStrength,
                        float emissiveStrength) {
    return DecalManager::inst().setStrength(id, normalStrength, roughnessStrength,
                                            metalStrength, emissiveStrength);
}

bool Decal::setUvRect(int id, float x, float y, float w, float h) {
    return DecalManager::inst().setUvRect(id, x, y, w, h);
}

bool Decal::setTextures(int id, graphics::Texture *normal, graphics::Texture *params) {
    return DecalManager::inst().setTextures(id, normal, params);
}

bool Decal::setBlend(int id, const std::string &mode) {
    return DecalManager::inst().setBlend(id, mode);
}

DecalProjectionStatus Decal::setProjection(int id, const std::string &mode, float blendSharpness) {
    return DecalManager::inst().setProjection(id, mode, blendSharpness);
}

DecalParallaxStatus Decal::setParallax(int id, float scale, float minLayers, float maxLayers) {
    return DecalManager::inst().setParallax(id, scale, minLayers, maxLayers);
}

DecalEdgeFadeStatus Decal::setEdgeFade(int id, float width) {
    return DecalManager::inst().setEdgeFade(id, width);
}

bool Decal::remove(int id) { return DecalManager::inst().remove(id); }

void Decal::clearAll() { DecalManager::inst().clearAll(); }

int Decal::count() { return DecalManager::inst().count(); }

void Decal::setLimit(const std::string &kind, int limit) {
    DecalManager::inst().setLimit(kind, limit);
}

void Decal::update(float dt) { DecalManager::inst().update(dt); }

void Decal::setEnabled(graphics::Graphics *gfx, bool enabled) {
    if (!gfx) throw eve::Exception("Decal.setEnabled: null gfx");
    if (auto *rc = gfx->getRenderControl()) {
        if (enabled)
            rc->enable("decal");
        else
            rc->disable("decal");
    }
}

std::string Decal::proceduralPresets() const {
    return "blood-wet,blood-dried,damage,dirt,rust,puddle,paint,moss,mold,lichen";
}

graphics::Texture *Decal::bakePresetTexture(graphics::Graphics *gfx, const std::string &preset,
                                            std::uint32_t seed, int resolution,
                                            const std::string &channel) {
    if (!gfx) throw eve::Exception("Decal.bakePresetTexture: null gfx");
    requireBakeChannel(channel);
    const std::string source = "preset:" + preset;
    if (proceduralBakeCache_->source != source || proceduralBakeCache_->seed != seed ||
        proceduralBakeCache_->resolution != resolution) {
        auto recipe = proceduralDecalPreset(preset, seed);
        if (!recipe.ok()) throw eve::Exception("%s", recipe.status().describe().c_str());
        recipe.value().width = resolution;
        recipe.value().height = resolution;
        auto baked = bakeProceduralDecal(recipe.value());
        if (!baked.ok()) throw eve::Exception("%s", baked.status().describe().c_str());
        proceduralBakeCache_->source = source;
        proceduralBakeCache_->seed = seed;
        proceduralBakeCache_->resolution = resolution;
        proceduralBakeCache_->bake = std::move(baked.value());
    }
    const auto &bytes = bakeChannel(proceduralBakeCache_->bake, channel);
    auto *texture = gfx->newTexture(resolution, resolution, bytes.data());
    if (!texture) throw eve::Exception("Decal.bakePresetTexture: graphics upload failed");
    return texture;
}

graphics::Texture *Decal::bakeSbsprsTexture(graphics::Graphics *gfx, const std::string &xml,
                                            int resolution, const std::string &channel) {
    if (!gfx) throw eve::Exception("Decal.bakeSbsprsTexture: null gfx");
    requireBakeChannel(channel);
    const std::string source = "sbsprs:" + xml;
    if (proceduralBakeCache_->source != source || proceduralBakeCache_->seed != 0 ||
        proceduralBakeCache_->resolution != resolution) {
        auto recipe = importProceduralDecalSbsprs(xml);
        if (!recipe.ok()) throw eve::Exception("%s", recipe.status().describe().c_str());
        recipe.value().width = resolution;
        recipe.value().height = resolution;
        auto baked = bakeProceduralDecal(recipe.value());
        if (!baked.ok()) throw eve::Exception("%s", baked.status().describe().c_str());
        proceduralBakeCache_->source = source;
        proceduralBakeCache_->seed = 0;
        proceduralBakeCache_->resolution = resolution;
        proceduralBakeCache_->bake = std::move(baked.value());
    }
    const auto &bytes = bakeChannel(proceduralBakeCache_->bake, channel);
    auto *texture = gfx->newTexture(resolution, resolution, bytes.data());
    if (!texture) throw eve::Exception("Decal.bakeSbsprsTexture: graphics upload failed");
    return texture;
}

void Decal::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Decal::create, false);
    expose(cls);
}

void Decal::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Decal::getName);
    cls.addFunc("project", &Decal::project);
    cls.addFunc("setStrength", &Decal::setStrength);
    cls.addFunc("setUvRect", &Decal::setUvRect);
    cls.addFunc("setTextures", &Decal::setTextures);
    cls.addFunc("setBlend", &Decal::setBlend);
    cls.addFunc("setProjection", &Decal::setProjection);
    cls.addFunc("setParallax", &Decal::setParallax);
    cls.addFunc("setEdgeFade", &Decal::setEdgeFade);
    cls.addFunc("remove", &Decal::remove);
    cls.addFunc("clearAll", &Decal::clearAll);
    cls.addFunc("count", &Decal::count);
    cls.addFunc("setLimit", &Decal::setLimit);
    cls.addFunc("update", &Decal::update);
    cls.addFunc("setEnabled", &Decal::setEnabled);
    cls.addFunc("proceduralPresets", &Decal::proceduralPresets);
    cls.addFunc("bakePresetTexture", &Decal::bakePresetTexture);
    cls.addFunc("bakeSbsprsTexture", &Decal::bakeSbsprsTexture);
}

}  // namespace eve::decal
