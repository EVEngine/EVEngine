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
 * @brief Spatial index module — broad-phase / map culling structures.
 * Script: `spatial <- eve.Spatial();`
 *
 * Provides 2D/3D QuadTree, Octree, SpatialHash, and BSP (kd-style) factories.
 * No overloads: use distinct *2D / *3D type names.
 */
class EVENGINE_API_FOUNDATION Spatial : public Module {
public:
    Module_REG(Spatial);
    /** @brief Default-constructs the spatial module. */
    Spatial() = default;
    /** @brief No owned indexes; factories return caller-owned pointers. */
    ~Spatial() override = default;

    /** @brief Creates a QuadTree over the given 2D bounds. @ownership Caller deletes. */
    QuadTree *newQuadTree(float minX, float minY, float maxX, float maxY, int maxDepth = 8,
                          int maxPerNode = 8);
    /** @brief Creates an Octree over the given 3D bounds. @ownership Caller deletes. */
    Octree   *newOctree(float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
                        int maxDepth = 8, int maxPerNode = 8);

    /** @brief Creates a 2D spatial hash with the given cell size. @ownership Caller deletes. */
    SpatialHash2D *newSpatialHash2D(float cellSize = 64.f);
    /** @brief Creates a 3D spatial hash with the given cell size. @ownership Caller deletes. */
    SpatialHash3D *newSpatialHash3D(float cellSize = 64.f);

    /** @brief Creates a 2D BSP/kd tree over the given bounds. @ownership Caller deletes. */
    BSPTree2D *newBSPTree2D(float minX, float minY, float maxX, float maxY, int maxDepth = 12,
                            int maxPerNode = 8);
    /** @brief Creates a 3D BSP/kd tree over the given bounds. @ownership Caller deletes. */
    BSPTree3D *newBSPTree3D(float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
                            int maxDepth = 12, int maxPerNode = 8);
};

}  // namespace eve::spatial
