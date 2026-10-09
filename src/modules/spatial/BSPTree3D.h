#pragma once
#include "common/Export.h"


#include "spatial/Bounds.h"
#include "spatial/QueryIds.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace eve::spatial {

/**
 * @brief Binary space partition tree (kd-style AABB splits) for 3D culling.
 * Alternating X/Y/Z splits at node midplanes.
 */
class EVENGINE_API_FOUNDATION BSPTree3D {
public:
    /** @brief Creates a 3D BSP/kd tree covering the given bounds. */
    BSPTree3D(float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
              int maxDepth = 12, int maxPerNode = 8);
    /** @brief Releases tree nodes. */
    ~BSPTree3D() = default;

    BSPTree3D(const BSPTree3D &)            = delete;
    BSPTree3D &operator=(const BSPTree3D &) = delete;

    /** @brief Removes all stored entries. */
    void clear();
    /** @brief Inserts an item AABB; false if out of bounds or id already present. */
    bool insert(int id, float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    /** @brief Removes an item by id; false if unknown. */
    bool remove(int id);
    /** @brief Moves an existing item to a new AABB; false if unknown or out of bounds. */
    bool update(int id, float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    /** @brief True if the id is currently stored. */
    bool contains(int id) const;
    /** @brief Number of stored ids. */
    int  getCount() const { return static_cast<int>(items_.size()); }

    /** @brief Finds items overlapping a point; fills the result buffer. @return Hit count. */
    int queryPoint(float x, float y, float z);
    /** @brief Finds items overlapping an AABB; fills the result buffer. @return Hit count. */
    int queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    /** @brief Finds items overlapping a sphere; fills the result buffer. @return Hit count. */
    int querySphere(float cx, float cy, float cz, float radius);

    /** @brief Number of hits from the last query*. */
    int getResultCount() const { return results_.getCount(); }
    /** @brief Hit id at dense index from the last query*, or -1. */
    int getResultId(int index) const { return results_.getId(index); }

    /** @brief Root/world minimum X. */
    float getMinX() const { return rootBounds_.minX; }
    /** @brief Root/world minimum Y. */
    float getMinY() const { return rootBounds_.minY; }
    /** @brief Root/world minimum Z. */
    float getMinZ() const { return rootBounds_.minZ; }
    /** @brief Root/world maximum X. */
    float getMaxX() const { return rootBounds_.maxX; }
    /** @brief Root/world maximum Y. */
    float getMaxY() const { return rootBounds_.maxY; }
    /** @brief Root/world maximum Z. */
    float getMaxZ() const { return rootBounds_.maxZ; }
    /** @brief Maximum subdivision depth. */
    int   getMaxDepth() const { return maxDepth_; }
    /** @brief Item capacity before a node splits. */
    int   getMaxPerNode() const { return maxPerNode_; }

private:
    struct Node {
        AABB3                 bounds;
        int                   depth = 0;
        int                   axis  = 0;  // 0=X, 1=Y, 2=Z
        float                 split = 0.f;
        std::vector<int>      itemIds;
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
        bool                  isLeaf() const { return left == nullptr; }
    };

    void  rebuild();
    bool  insertInto(Node &node, int id, const AABB3 &bounds);
    void  split(Node &node);
    void  collect(Node &node, const AABB3 *box, float cx, float cy, float cz, float radius,
                  bool useSphere);
    Node *ensureRoot();

    AABB3                          rootBounds_;
    int                            maxDepth_   = 12;
    int                            maxPerNode_ = 8;
    std::unique_ptr<Node>          root_;
    std::unordered_map<int, AABB3> items_;
    QueryIds                       results_;
};

}  // namespace eve::spatial
