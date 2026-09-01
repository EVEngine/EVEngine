#pragma once

/**
 * @file TerrainImporter.h
 * @brief Shared canonical terrain inputs used by Unreal and Unity adapters.
 */

#include "asset_import/AssetImporter.h"

namespace eve::asset_import {

/** @brief One canonical terrain material layer after source-engine mapping. */
struct CanonicalTerrainLayer {
    std::string name;
    std::string diffuseSource;
    std::string normalSource;
    std::string weightSource;
    std::string normalConvention = "opengl";
    float       tileSizeMeters = 1.0f;
};

/** @brief One deterministic terrain scatter rule, independent of source engine nodes. */
struct CanonicalScatterRule {
    std::string id;
    std::string prototype;
    std::string layer;
    float       densityPerSquareMeter = 0.0f;
    float       minimumSlopeRadians = 0.0f;
    float       maximumSlopeRadians = 1.57079632679f;
    std::uint64_t seed = 0;
};

/** @brief One baked instance in canonical right-handed metre coordinates. */
struct CanonicalTerrainInstance {
    std::string prototype;
    float position[3] = {};
    float rotation[4] = {0, 0, 0, 1};
    float scale[3] = {1, 1, 1};
};

/** @brief Fully parsed source terrain ready for canonical archive construction. */
struct CanonicalTerrainInput {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float spacingX = 1.0f;
    float spacingZ = 1.0f;
    std::vector<float> heightsMeters;
    std::vector<CanonicalTerrainLayer> layers;
    std::vector<CanonicalScatterRule> scatterRules;
    std::vector<CanonicalTerrainInstance> instances;
    Value::Object sourceTransform;
};

/**
 * @brief Build `eve.terrain`, terrain material, PCG and instance assets from canonical input.
 * @param package Stable package identity and provenance.
 * @param terrain Parsed canonical terrain values.
 * @param importer Stable adapter identifier recorded in provenance.
 * @param limits Allocation limits shared with source parsers.
 * @return Owning `.eva` candidate with typed binary height/instance blobs.
 */
[[nodiscard]] Result<PreparedAssetImport> prepareCanonicalTerrainImport(
    const ImportPackageIdentity& package, const CanonicalTerrainInput& terrain,
    std::string_view importer, const AssetImportLimits& limits = {});

}  // namespace eve::asset_import
