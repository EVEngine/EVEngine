#pragma once

#include "virtualgeometry/VirtualGeometryAsset.h"

#include <cstdint>

namespace eve::virtualgeometry {

/**
 * @brief CPU preprocessor that builds a Nanite-style hierarchical cluster DAG from a
 * triangle mesh:
 *
 *   1. Meshletization (LOD0): greedily partition the mesh into connected
 *      clusters of ~maxTrianglesPerCluster triangles (cache-friendly).
 *   2. Hierarchical LOD: iteratively merge clusters into parents and simplify
 *      each parent with a Quadric Error Metric (QEM) edge-collapse, building a
 *      cluster DAG with per-cluster bounding spheres + geometric error.
 *
 * Pure CPU, dependency-free, unit-testable.
 */
class VirtualGeometryBuilder {
public:
    struct MeshInput {
        int vertexCount = 0;
        const float *positions = nullptr;  // xyz packed
        const float *normals = nullptr;    // optional xyz packed
        const std::uint32_t *indices = nullptr;  // triangle list (count % 3 == 0)
        int indexCount = 0;
    };

    struct Options {
        int maxVerticesPerCluster = 64;   // ~Nanite 128 tris / 64-96 verts
        int maxTrianglesPerCluster = 124;
        int minLodLevels = 4;             // build at least this many levels
        int mergeFactor = 4;              // clusters merged into one parent
        float lodTargetRatio = 0.5f;      // parent keeps this fraction of triangles
    };

    /** @brief Returns false on empty/invalid input. */
    bool build(const MeshInput &in, const Options &opt, VirtualGeometryAsset &out);
};

}  // namespace eve::virtualgeometry
