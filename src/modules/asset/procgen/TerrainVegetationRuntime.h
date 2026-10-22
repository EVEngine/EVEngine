#pragma once

/** @file TerrainVegetationRuntime.h @brief Runtime realization of terrain vegetation assets. */

#include "asset/procgen/EvpackInstanceSetLoader.h"
#include "asset/procgen/EvpackPointGraphLoader.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::asset_procgen {

struct LoadedTerrainMaterial;
struct TerrainMaterialAtlases;

/** @brief Hard limits for one deterministic terrain vegetation realization. */
struct TerrainVegetationLimits {
    std::uint32_t maximumGeneratedInstances = 1'000'000;
    std::uint32_t maximumTotalInstances     = 2'000'000;
    std::uint32_t maximumGraphNodes         = 4096;
};

/** @brief One stable terrain vegetation transform and its source attributes. */
struct TerrainVegetationInstance {
    std::uint64_t        id = 0;
    std::string          prototype;
    std::string          layer;
    std::array<float, 3> position{};
    std::array<float, 4> rotation{0.f, 0.f, 0.f, 1.f};
    std::array<float, 3> scale{1.f, 1.f, 1.f};
    std::array<float, 3> normal{0.f, 1.f, 0.f};
    std::uint32_t        seed = 0;
};

/** @brief Contiguous prototype range suitable for one instanced render submission. */
struct TerrainVegetationBucket {
    std::string   prototype;
    std::uint32_t firstInstance = 0;
    std::uint32_t instanceCount = 0;
};

/** @brief Owning, prototype-sorted result plus scalability evidence. */
struct TerrainVegetationRealization {
    float                                  wavingGrassAmount = 0.f;
    float                                  wavingGrassSpeed = 0.f;
    float                                  wavingGrassStrength = 0.f;
    std::array<float, 4>                   wavingGrassTint{1.f, 1.f, 1.f, 1.f};
    std::vector<RuntimeInstancePrototype>  prototypes;
    std::vector<TerrainVegetationInstance> instances;
    std::vector<TerrainVegetationBucket>   buckets;
    std::uint32_t                          generatedCount      = 0;
    std::uint32_t                          explicitCount       = 0;
    std::uint32_t                          weightCulledCount   = 0;
    std::uint32_t                          graphNodesEvaluated = 0;
    double                                 graphMilliseconds   = 0.0;
};

/** @brief Explicit world-to-control-map projection for terrain layer filtering. */
struct TerrainVegetationWeightDomain {
    float minimumX = 0.f;
    float minimumZ = 0.f;
    float maximumX = 0.f;
    float maximumZ = 0.f;
    /** @brief Reverse V for source maps whose rows oppose the canonical terrain Z axis. */
    bool flipV = false;
};

/**
 * @brief Execute an optional terrain PCG graph and merge an optional baked instance set.
 * @param graph Mutable owning graph because execution updates its cache and metrics; caller
 * serializes access for the duration of this call. Null omits procedural vegetation.
 * @param explicitInstances Borrowed immutable baked instances. Null omits baked vegetation.
 * @param limits Allocation and execution limits checked before publishing the detached result.
 * @return Prototype-sorted instances and buckets, or a structured failure with no partial result.
 * @thread Worker-safe across distinct graph objects. Does not perform GPU calls or callbacks.
 * @reentrancy Not reentrant for the same graph object.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<TerrainVegetationRealization> realizeTerrainVegetation(
    LoadedPointGraph* graph, const LoadedInstanceSet* explicitInstances, const TerrainVegetationLimits& limits = {});

/**
 * @brief Apply terrain RGBA layer weights to procedural instances using stable stochastic thinning.
 * @param realization Borrowed realization; only instances with a non-empty layer are filtered.
 * @param material Borrowed layer table whose names resolve instance layer attributes.
 * @param atlases Borrowed linear RGBA8 control maps, one per four layers.
 * @param domain Explicit world XZ projection and source V orientation.
 * @return Detached, re-bucketed realization; invalid maps or unknown layers fail atomically.
 * @thread Worker-safe while all inputs remain immutable.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<TerrainVegetationRealization> filterTerrainVegetationByLayerWeights(
    const TerrainVegetationRealization& realization, const LoadedTerrainMaterial& material,
    const TerrainMaterialAtlases& atlases, const TerrainVegetationWeightDomain& domain);

}  // namespace eve::asset_procgen
