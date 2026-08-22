#include "decal/Decal.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::decal {

Module_IMPL(Decal, new Decal());

Decal::Decal() {
    registerDecalCapabilities();
    graphics::RenderSystem3D::addDecalExtraDrawer(
        [](graphics::Graphics &gfx, const graphics::Camera3D::Data &cam,
           const glm::mat4 &viewProj, float aspect) {
            DecalManager::inst().drawAll(gfx, cam, viewProj, aspect);
        });
}

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
    cls.addFunc("remove", &Decal::remove);
    cls.addFunc("clearAll", &Decal::clearAll);
    cls.addFunc("count", &Decal::count);
    cls.addFunc("setLimit", &Decal::setLimit);
    cls.addFunc("update", &Decal::update);
    cls.addFunc("setEnabled", &Decal::setEnabled);
}

}  // namespace eve::decal
