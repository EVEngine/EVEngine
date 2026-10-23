#include "procgen/texture/PbrMaterialScript.h"
#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include "image/ImageData.h"
#include "procgen/texture/PbrMaterial.h"
namespace eve::procgen {
void exposePbrMaterial(ssq::Table& table) {
    auto pbr = table.addClass<PbrTextureSet>(
        "ProcgenPbrMaterial", std::function<PbrTextureSet*()>([]() -> PbrTextureSet* { return nullptr; }), true);
    pbr.addFunc("destroy", &PbrTextureSet::destroy);
    pbr.addFunc("getAlbedo", &PbrTextureSet::getAlbedo);
    pbr.addFunc("getNormal", &PbrTextureSet::getNormal);
    pbr.addFunc("getRoughness", &PbrTextureSet::getRoughness);
    pbr.addFunc("getMetallic", &PbrTextureSet::getMetallic);
    pbr.addFunc("getHeight", &PbrTextureSet::getHeight);
    pbr.addFunc("getAo", &PbrTextureSet::getAo);
    pbr.addFunc("getTransmission", &PbrTextureSet::getTransmission);
    pbr.addFunc("getIor", &PbrTextureSet::getIor);
    pbr.addFunc("getAlbedoWidth", [](const PbrTextureSet* s) { return s->albedo->getWidth(); });
    pbr.addFunc("getAlbedoHeight", [](const PbrTextureSet* s) { return s->albedo->getHeight(); });
    pbr.addFunc("getNormalWidth", [](const PbrTextureSet* s) { return s->normal->getWidth(); });
    pbr.addFunc("getRoughnessWidth", [](const PbrTextureSet* s) { return s->roughness->getWidth(); });
    pbr.addFunc("getMetallicWidth", [](const PbrTextureSet* s) { return s->metallic->getWidth(); });
    pbr.addFunc("getHeightWidth", [](const PbrTextureSet* s) { return s->height->getWidth(); });
    pbr.addFunc("getAoWidth", [](const PbrTextureSet* s) { return s->ao->getWidth(); });
    pbr.addFunc("hasAllMaps", [](const PbrTextureSet* s) {
        return s->albedo && s->normal && s->roughness && s->metallic && s->height && s->ao;
    });
}
}  // namespace eve::procgen
