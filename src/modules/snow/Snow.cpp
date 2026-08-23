#include "snow/Snow.h"

#include "graphics/Graphics.h"
#include "procgen/heightmap/Heightmap.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace eve::snow {

Module_IMPL(Snow, new Snow());

Snow::Snow() = default;

void applySnowToHeightmap(const SnowField &field, const procgen::Heightmap &terrain,
                          procgen::Heightmap &out, float heightScale) {
    const int w = terrain.getWidth();
    const int h = terrain.getHeight();
    out.resize(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float s = field.inBounds(x, y) ? field.height(x, y) : 0.f;
            out.setHeight(x, y, terrain.height(x, y) + s * heightScale);
        }
    }
}

SnowField *Snow::newField(int width, int height) {
    return new SnowField(width, height);
}

bool Snow::applyToHeightmap(SnowField *field, procgen::Heightmap *terrain,
                            procgen::Heightmap *out, float heightScale) {
    if (!field || !terrain || !out) return false;
    applySnowToHeightmap(*field, *terrain, *out, heightScale);
    return true;
}

namespace {

bool snowPixels(const SnowField &field, const std::string &kind, std::vector<uint8_t> &rgba) {
    if (kind == "height") {
        rgba = field.toHeightRGBA();
        return true;
    }
    if (kind == "albedo") {
        rgba = field.toAlbedoRGBA();
        return true;
    }
    if (kind == "normal") {
        rgba = field.toNormalRGBA();
        return true;
    }
    return false;
}

}  // namespace

graphics::Texture *Snow::uploadTexture(SnowField *field, graphics::Graphics *gfx,
                                       const std::string &kind) {
    if (!field || !gfx) return nullptr;
    std::vector<uint8_t> rgba;
    if (!snowPixels(*field, kind, rgba) || rgba.empty()) return nullptr;
    return gfx->newTexture(field->getWidth(), field->getHeight(), rgba.data(), false, false);
}

bool Snow::updateTexture(SnowField *field, graphics::Texture *texture,
                         graphics::Graphics *gfx, const std::string &kind) {
    if (!field || !texture || !gfx) return false;
    std::vector<uint8_t> rgba;
    if (!snowPixels(*field, kind, rgba) || rgba.empty()) return false;
    return gfx->updateTexture(texture, field->getWidth(), field->getHeight(), rgba.data());
}

// ---------------------------------------------------------------------------
// Script binding
// ---------------------------------------------------------------------------

void Snow::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Snow::create, false);
    expose(cls);

    auto field = table.addClass<SnowField>(
        "SnowField", std::function<SnowField *()>([]() -> SnowField * { return nullptr; }),
        true);
    field.addFunc("resize", &SnowField::resize);
    field.addFunc("getWidth", &SnowField::getWidth);
    field.addFunc("getHeight", &SnowField::getHeight);
    field.addFunc("fill", &SnowField::fill);
    field.addFunc("setHeight", &SnowField::setHeight);
    field.addFunc("height", &SnowField::height);
    field.addFunc("stampFootprint", &SnowField::stampFootprint);
    field.addFunc("stampImpact", &SnowField::stampImpact);
    field.addFunc("addSnowfall", &SnowField::addSnowfall);
    field.addFunc("isDirty", &SnowField::isDirty);
    field.addFunc("clearDirty", &SnowField::clearDirty);
}

void Snow::expose(ssq::Class &cls) {
    cls.addFunc("newField", &Snow::newField);
    cls.addFunc("applyToHeightmap", &Snow::applyToHeightmap);
    cls.addFunc("uploadTexture", &Snow::uploadTexture);
    cls.addFunc("updateTexture", &Snow::updateTexture);
}

}  // namespace eve::snow
