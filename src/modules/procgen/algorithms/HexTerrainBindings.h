#pragma once

/** @file HexTerrainBindings.h @brief Optional hexmap terrain bake bindings for Procgen. */

#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"
#include "procgen/Params.h"

namespace ssq {
class Class;
}

namespace eve::procgen {
class GeneratorRegistry;

/**
 * @brief Register `hex.terrain` / `hex.sphere` schemas when hexmap is present.
 *
 * The schemas exist so `applyAlgorithmDefaults` can fill generator tunables.
 * They are not Grid2D recipes: `generate` / `generateTo` refuse them and point
 * at `generateHexTerrain` / `generateHexSphere`.
 */
void registerHexTerrainAlgorithms(GeneratorRegistry& registry);

/**
 * @brief Bake a planar hex map into the script Value hexmap applies.
 * @param params Seed, `width`/`height` (multiples of the 5×5 chunk size) and
 *               `HexMapGeneratorSettings` keys.
 * @return Object with `kind="hex.terrain"`, dimensions, seed and packed cells.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<eve::Value> generateHexTerrain(const Params& params);

/**
 * @brief Bake a spherical hex map into the script Value hexmap applies.
 * @param params Seed plus `subdivision`, `radius` and sphere generator keys.
 * @return Object with `kind="hex.sphere"`, topology, seed and packed cells.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<eve::Value> generateHexSphere(const Params& params);

/** @brief Expose `generateHexTerrain` / `generateHexSphere` on the Procgen class. */
void exposeHexTerrain(ssq::Class& cls);
}  // namespace eve::procgen
