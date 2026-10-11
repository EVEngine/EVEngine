#pragma once

/** @file HexMapMesh.h @brief Per-chunk mesh generation for every hex map surface. */

#include "hexmap/HexMap.h"
#include "hexmap/HexMeshData.h"
#include "hexmap/HexVisibility.h"

#include <cstdint>

namespace eve::hexmap {

/**
 * @brief Encodes the three contributing cells of a terrain vertex.
 *
 * The vertex stream only carries two texture coordinates, so the terrain
 * shader's inputs are packed into them:
 *
 *   `u = terrainA + terrainB * 8 + terrainC * 64`
 *   `v = weightB + weightC * 16`
 *
 * Terrain indices are in `[0, 4]` and weights in `[0, 1]`, so both values stay
 * well inside float32's exact-integer range and the shader recovers the inputs
 * with `mod`/`fract`. This is a deliberate compatibility bridge for the current
 * two-coordinate vertex layout and must not become the terrain data format.
 */
struct HexTerrainVertexCode {
    /** @brief First texture coordinate, carrying the three terrain indices. */
    [[nodiscard]] static float encodeIndices(std::int32_t a, std::int32_t b, std::int32_t c) noexcept {
        return static_cast<float>(a) + static_cast<float>(b) * 8.f + static_cast<float>(c) * 64.f;
    }
    /** @brief Second texture coordinate, carrying the secondary/tertiary weights. */
    [[nodiscard]] static float encodeWeights(float weightB, float weightC) noexcept { return weightB + weightC * 16.f; }
};

/** @brief Terrain-layer weights of a vertex; the three entries sum to 1. */
struct HexTerrainWeights {
    float a = 1.f;
    float b = 0.f;
    float c = 0.f;

    /** @brief Weights of the first cell alone. */
    [[nodiscard]] static HexTerrainWeights primary() noexcept { return HexTerrainWeights{1.f, 0.f, 0.f}; }
    /** @brief Weights of two cells, `t` towards the second. */
    [[nodiscard]] static HexTerrainWeights blend(float t) noexcept { return HexTerrainWeights{1.f - t, t, 0.f}; }
    /** @brief Weights of three cells, `t2`/`t3` towards the second and third. */
    [[nodiscard]] static HexTerrainWeights blend3(float t2, float t3) noexcept {
        return HexTerrainWeights{1.f - t2 - t3, t2, t3};
    }
    /** @brief Interpolates two weight sets. */
    [[nodiscard]] static HexTerrainWeights lerp(const HexTerrainWeights& a, const HexTerrainWeights& b,
                                                float t) noexcept {
        return HexTerrainWeights{a.a + (b.a - a.a) * t, a.b + (b.b - a.b) * t, a.c + (b.c - a.c) * t};
    }
};

/**
 * @brief Builds the ground, terrace and cliff surface of one chunk.
 *
 * The result covers the chunk's own cells plus the blend strips it owns towards
 * its NE, E and SE neighbours, matching the reference project's chunk-border
 * ownership rule.
 *
 * @param map Source map; must contain `chunkIndex`.
 * @param chunkIndex Chunk to build.
 * @param out Destination mesh; cleared and finalized by this call.
 */
void buildTerrainMesh(const HexMap& map, std::int32_t chunkIndex, HexMeshData& out);

/**
 * @brief Builds the water surface and shore band of one chunk.
 *
 * Only cells that are underwater contribute. `u` carries the shore parameter
 * (`0` for open water, `1` at the shoreline) and `v` is reserved.
 */
void buildWaterMesh(const HexMap& map, std::int32_t chunkIndex, HexMeshData& out);

/**
 * @brief Builds the river channel surface of one chunk.
 *
 * `u` runs along the channel and `v` across it, so the shader can animate flow.
 */
void buildRiverMesh(const HexMap& map, std::int32_t chunkIndex, HexMeshData& out);

/**
 * @brief Builds the road surface of one chunk.
 *
 * `u` runs along the road and `v` across it.
 */
void buildRoadMesh(const HexMap& map, std::int32_t chunkIndex, HexMeshData& out);

/**
 * @brief Builds one chunk surface by dispatching on `surface`.
 *
 * @param map Source map; must contain `chunkIndex`.
 * @param chunkIndex Chunk to build.
 * @param surface Which stream to generate; `HexSurface::Fog` is not supported here
 *                because it also needs the visibility state, use `buildFogMesh`.
 * @param out Destination mesh; cleared and finalized by this call.
 */
void buildChunkSurfaceMesh(const HexMap& map, std::int32_t chunkIndex, HexSurface surface, HexMeshData& out);

/**
 * @brief Builds the fog-of-war overlay of one chunk.
 *
 * Only cells that are *not* fully visible contribute: an unexplored cell is
 * covered by an opaque dark column, an explored cell that no viewer currently
 * sees is covered by a dimmer one. The column is a hexagonal prism rather than a
 * flat cap so it also covers the cliff faces along a fog boundary.
 *
 * The single texture coordinate carries the fog shade (`0` explored-but-unseen,
 * `1` unexplored); the second coordinate is reserved.
 *
 * @param map Source map; must contain `chunkIndex`.
 * @param visibility Visibility counters of the same map.
 * @param chunkIndex Chunk to build.
 * @param out Destination mesh; cleared and finalized by this call.
 */
void buildFogMesh(const HexMap& map, const HexVisibility& visibility, std::int32_t chunkIndex, HexMeshData& out);

}  // namespace eve::hexmap
