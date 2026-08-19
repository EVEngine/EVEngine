#pragma once

#include <cstdint>
#include <vector>

namespace eve::virtualgeometry {

/**
 * @brief GPU-mirrored cluster node layout (std430, matches shaders/vg_*.comp).
 * Exactly 4 x uvec4 per cluster:
 *
 *   [0] bounds     = { f32x(cx), f32x(cy), f32x(cz), f32x(r) }
 *   [1] ranges     = { triStart, triCount, lodLevel, parent }
 *   [2] err        = { f32x(errorR), f32x(errorRScreen), childCount, 0 }
 *   [3] children   = { child0, child1, child2, child3 }
 */
struct VgGpuCluster {
    std::uint32_t u0[4];
    std::uint32_t u1[4];
    std::uint32_t u2[4];
    std::uint32_t u3[4];
};

/** @brief CPU-side processed cluster (before packing into VgGpuCluster). */
struct VgCluster {
    float cx = 0.f, cy = 0.f, cz = 0.f, r = 0.f;  // bounding sphere
    std::uint32_t triStart = 0, triCount = 0;     // global triangle stream range
    std::uint32_t vertStart = 0, vertCount = 0;   // global position range used
    std::uint32_t lodLevel = 0;
    float errorR = 0.f;        // world-space geometric error radius
    float errorRScreen = 0.f;  // screen-space error constant (precomputed)
    std::uint32_t parent = 0xFFFFFFFFu;  // 0xFFFFFFFF = root
    std::uint32_t children[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    std::uint32_t childCount = 0;
};

/**
 * @brief Fully processed virtual-geometry asset (CPU representation).
 * Produced by Builder from a Mesh; uploaded to GPU SSBOs by the backend.
 */
struct VirtualGeometryAsset {
    int vertexCount = 0;
    std::vector<float> positions;    // xyz packed, size = 3 * vertexCount
    std::vector<float> normals;      // optional xyz packed

    /** @brief Flat global triangle stream; each entry is an index into positions. */
    std::vector<std::uint32_t> triangles;

    std::vector<VgCluster> clusters;

    // Precomputed projection-dependent constant cache (recomputed on setCamera).
    float lastErrorScale = 1.f;

    int totalTriangles() const { return static_cast<int>(triangles.size() / 3); }
};

}  // namespace eve::virtualgeometry
