#pragma once

#include "common/Module.h"

namespace eve::spatial {

class QuadTree;
class Octree;
class SpatialHash2D;
class SpatialHash3D;
class BSPTree2D;
class BSPTree3D;

/**
 * Spatial index module — broad-phase / map culling structures.
 * Script: `spatial <- eve.Spatial();`
 *
 * Provides 2D/3D QuadTree, Octree, SpatialHash, and BSP (kd-style) factories.
 * No overloads: use distinct *2D / *3D type names.
 */
class Spatial : public Module {
public:
    Module_REG(Spatial);
    Spatial() = default;
    ~Spatial() override = default;

    QuadTree *newQuadTree(float minX, float minY, float maxX, float maxY, int maxDepth = 8,
                          int maxPerNode = 8);
    Octree   *newOctree(float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
                        int maxDepth = 8, int maxPerNode = 8);

    SpatialHash2D *newSpatialHash2D(float cellSize = 64.f);
    SpatialHash3D *newSpatialHash3D(float cellSize = 64.f);

    BSPTree2D *newBSPTree2D(float minX, float minY, float maxX, float maxY, int maxDepth = 12,
                            int maxPerNode = 8);
    BSPTree3D *newBSPTree3D(float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
                            int maxDepth = 12, int maxPerNode = 8);
};

}  // namespace eve::spatial
