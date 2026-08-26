#include "procgen/BiomeScript.h"

#include "procgen/Biome.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>

namespace eve::procgen {

void exposeBiomeRules(ssq::Table& table) {
    auto biome = table.addClass<BiomeRules>(
        "ProcgenBiomeRules", std::function<BiomeRules*()>([]() -> BiomeRules* { return nullptr; }),
        true);
    biome.addFunc("clear", &BiomeRules::clear);
    biome.addFunc("addLayer", &BiomeRules::addLayer);
    biome.addFunc("removeLayer", &BiomeRules::removeLayer);
    biome.addFunc("hasLayer", &BiomeRules::hasLayer);
    biome.addFunc("getLayerCount", &BiomeRules::getLayerCount);
    biome.addFunc("getLayerName", &BiomeRules::getLayerName);
    biome.addFunc("getLayerPriority", &BiomeRules::getLayerPriority);
    biome.addFunc("getLayerDensity", &BiomeRules::getLayerDensity);
    biome.addFunc("addAsset", &BiomeRules::addAsset);
    biome.addFunc("getAssetCount", &BiomeRules::getAssetCount);
    biome.addFunc("getAssetName", &BiomeRules::getAssetName);
    biome.addFunc("addExclusion", &BiomeRules::addExclusion);
    biome.addFunc("getExclusionCount", &BiomeRules::getExclusionCount);
    biome.addFunc("generate", &BiomeRules::generate);
    biome.addFunc("getError", &BiomeRules::getError);
    biome.addFunc("debugReport", &BiomeRules::debugReport);
}

}  // namespace eve::procgen
