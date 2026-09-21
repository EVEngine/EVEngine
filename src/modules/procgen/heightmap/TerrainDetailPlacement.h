#pragma once
#include <cstdint>
#include <string>
#include "common/Result.h"
namespace eve::procgen {
class TerrainDetailLayer;
class Heightmap;
class PointSet;
/** @brief Explicit native placement domain and resource for a terrain detail layer. */
struct TerrainDetailPlacementSettings {
    float originX = 0, originZ = 0, width = 1, depth = 1, heightScale = 1;
    float minimumScale = 1, maximumScale = 1;
    /** @brief Independent horizontal and vertical multipliers, applied after the uniform scale. */
    float minimumWidth = 1, maximumWidth = 1, minimumHeight = 1, maximumHeight = 1;
    /** @brief Linear RGBA endpoints passed to generated points for renderer-side instance tint. */
    float healthyR = 1, healthyG = 1, healthyB = 1, healthyA = 1;
    float dryR = 1, dryG = 1, dryB = 1, dryA = 1;
    /** @brief Normalized frequency control for deterministic healthy/dry color patches. */
    float       noiseSpread = 0.3F;
    int32_t     noiseSeed   = 0;
    uint32_t    seed        = 1;
    uint64_t    namespaceId = 1;
    /** @brief Density resource layer to export; zero selects the compatibility/default layer. */
    uint64_t    densityNamespaceId = 0;
    int         maxPoints   = 1000000;
    std::string asset;
};
/**
 * @brief Expand integer detail counts into the existing attributed PointSet, atomically.
 * @param output Exclusively borrowed destination, replaced only on success.
 * @param layer Immutable initialized counts; each count emits that many instances.
 * @param heights Finite terrain grid spanning the placement rectangle at its endpoint samples;
 * resolution is independent of the detail grid. Singleton axes are constant.
 * @param settings Finite positive domain extents/scale range, finite height scale and origin,
 * nonzero namespace unique to this resource/tile, nonempty asset reference, normalized linear colors,
 * noiseSpread in
 * [0,1], and nonnegative point budget.
 * @return Emitted count or diagnostic; invalid input, exceeded budget or
 * allocation exception preserves output. Owned output carries asset and detailCell/detailOrdinal attributes plus
 * deterministic coherent
 * healthy/dry point colors. IDs derive from namespace,
 * row-major cell and ordinal; named
 * position/rotation/scale hash streams are independent of counts. Increasing another cell's density preserves existing
 * identities and transforms. Changing grid dimensions or namespace changes identity scope. No asset is loaded or
 * borrowed; resource resolution stays downstream. Pcg delegates color noise to Unity Terrain; this native value-noise
 * reconstruction preserves its exposed
 * endpoints, seed and increasing-frequency spread meaning, without claiming
 * Unity-internal numerical parity.
 * Native within-cell jitter and upright normals replace Unity-owned detail
 * instancing; no Unity position parity.
 * Caller serializes output and input access on its owner thread; no callbacks,
 * retained references or hidden time.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> exportTerrainDetailPoints(
    PointSet& output, const TerrainDetailLayer& layer, const Heightmap& heights,
    const TerrainDetailPlacementSettings& settings);
}  // namespace eve::procgen
