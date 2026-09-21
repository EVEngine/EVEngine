#pragma once
#include "common/Export.h"

/**
 * @file TerrainImporter.h
 * @brief Shared canonical terrain inputs used by Unreal and Unity adapters.
 */

#include "asset/import/AssetImporter.h"

#include <array>

namespace eve::asset_import {

/** @brief One canonical terrain material layer after source-engine mapping. */
struct CanonicalTerrainLayer {
    std::string          name;
    std::string          diffuseSource;
    std::string          normalSource;
    std::string          weightSource;
    std::string          maskSource;
    std::string          diffuseAsset, normalAsset, weightAsset, maskAsset;
    std::string          normalConvention = "opengl";
    float                tileSizeMeters   = 1.0f;
    std::array<float, 2> tileScaleMeters{1.f, 1.f};
    std::array<float, 2> tileOffsetMeters{};
    std::array<float, 4> maskRemapMinimum{};
    std::array<float, 4> maskRemapMaximum{1.f, 1.f, 1.f, 1.f};
    std::array<float, 4> specular{};
    float                metallic    = 0.f;
    float                normalScale = 1.f;
    float                smoothness  = 0.f;
};

/** @brief One deterministic terrain scatter rule, independent of source engine nodes. */
struct CanonicalScatterRule {
    std::string   id;
    std::string   prototype;
    std::string   layer;
    float         densityPerSquareMeter = 0.0f;
    float         minimumSlopeRadians   = 0.0f;
    float         maximumSlopeRadians   = 1.57079632679f;
    std::uint64_t seed                  = 0;
};

/** @brief One baked instance in canonical right-handed metre coordinates. */
struct CanonicalTerrainInstance {
    std::string prototype;
    float       position[3] = {};
    float       rotation[4] = {0, 0, 0, 1};
    float       scale[3]    = {1, 1, 1};
};

/** @brief One source vegetation prototype shared by baked terrain instances. */
struct CanonicalTerrainDetailPrototype {
    std::string  prototype;
    std::string  renderMode;
    bool         usePrototypeMesh = false;
    bool         useInstancing    = false;
    float        minWidth         = 1.f;
    float        maxWidth         = 1.f;
    float        minHeight        = 1.f;
    float        maxHeight        = 1.f;
    std::int64_t noiseSeed        = 0;
    float        noiseSpread      = .1f;
    float        density          = 1.f;
    float        alignToGround    = 0.f;
    float        positionJitter   = 0.f;
    std::string  resourceAsset;
    std::array<float, 4> healthyColor{1.f, 1.f, 1.f, 1.f};
    std::array<float, 4> dryColor{1.f, 1.f, 1.f, 1.f};
    float                bendFactor       = 0.f;
    float                holeEdgePadding  = 0.f;
    bool                 useDensityScaling = true;
};

/** @brief Fully parsed source terrain ready for canonical archive construction. */
struct CanonicalTerrainInput {
    std::uint32_t                                width    = 0;
    std::uint32_t                                height   = 0;
    float                                        spacingX = 1.0f;
    float                                        spacingZ = 1.0f;
    std::vector<float>                           heightsMeters;
    std::vector<CanonicalTerrainLayer>           layers;
    std::string                                  holesSource;
    std::string                                  holesAsset;
    std::array<std::string, 4>                   controlSources;
    std::array<std::string, 4>                   controlAssets;
    float                                        boundsMultiplier = 1.f;
    bool                                         hasWavingGrass = false;
    float                                        wavingGrassAmount = 0.f;
    float                                        wavingGrassSpeed = 0.f;
    float                                        wavingGrassStrength = 0.f;
    std::array<float, 4>                         wavingGrassTint{1.f, 1.f, 1.f, 1.f};
    std::vector<CanonicalScatterRule>            scatterRules;
    std::vector<CanonicalTerrainDetailPrototype> detailPrototypes;
    std::vector<CanonicalTerrainInstance>        instances;
    Value::Object                                sourceTransform;
};

/**
 * @brief Build `eve.terrain`, terrain material, PCG and instance assets from canonical input.
 * @param package Stable package identity and provenance.
 * @param terrain Parsed canonical terrain values.
 * @param importer Stable adapter identifier recorded in provenance.
 * @param limits Allocation limits shared with source parsers.
 * @return Owning `.eva` candidate with typed binary height/instance blobs.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<PreparedAssetImport> prepareCanonicalTerrainImport(
    const ImportPackageIdentity& package, const CanonicalTerrainInput& terrain, std::string_view importer,
    const AssetImportLimits& limits = {});

}  // namespace eve::asset_import
