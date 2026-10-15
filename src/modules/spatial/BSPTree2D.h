#pragma once
#include "common/Export.h"


#include "spatial/Bounds.h"
#include "spatial/QueryIds.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace eve::spatial {

/**
 * @brief Binary space partition tree (kd-style AABB splits) for 2D culling.
 * Alternating X/Y splits at node midplanes; spanning items stay on the node.
 */
class EVENGINE_API_FOUNDATION BSPTree2D {
public:
    /** @brief Creates a 2D BSP/kd tree covering the given bounds. */
    BSPTree2D(float minX, float minY, float maxX, float maxY, int maxDepth = 12,
              int maxPerNode = 8);
    /** @brief Releases tree nodes. */
    ~BSPTree2D() = default;

    BSPTree2D(const BSPTree2D &)            = delete;
    BSPTree2D &operator=(const BSPTree2D &) = delete;

    /** @brief Removes all stored entries. */
    void clear();
    /** @brief Inserts an item AABB; false if out of bounds or id already present. */
    bool insert(int id, float minX, float minY, float maxX, float maxY);
    /** @brief Removes an item by id; false if unknown. */
    bool remove(int id);
    /** @brief Moves an existing item to a new AABB; false if unknown or out of bounds. */
    bool update(int id, float minX, float minY, float maxX, float maxY);
    /** @brief True if the id is currently stored. */
    bool contains(int id) const;
    /** @brief Number of stored ids. */
    int  getCount() const { return static_cast<int>(items_.size()); }

    /** @brief Finds items overlapping a point; fills the result buffer. @return Hit count. */
    int queryPoint(float x, float y);
    /** @brief Finds items overlapping an AABB; fills the result buffer. @return Hit count. */
    int queryRect(float minX, float minY, float maxX, float maxY);
    /** @brief Finds items overlapping a circle; fills the result buffer. @return Hit count. */
    int queryCircle(float cx, float cy, float radius);

    /** @brief Number of hits from the last query*. */
    int getResultCount() const { return results_.getCount(); }
    /** @brief Hit id at dense index from the last query*, or -1. */
    int getResultId(int index) const { return results_.getId(index); }

    /** @brief Root/world minimum X. */
    float getMinX() const { return rootBounds_.minX; }
    /** @brief Root/world minimum Y. */
    float getMinY() const { return rootBounds_.minY; }
    /** @brief Root/world maximum X. */
    float getMaxX() const { return rootBounds_.maxX; }
    /** @brief Root/world maximum Y. */
    float getMaxY() const { return rootBounds_.maxY; }
    /** @brief Maximum subdivision depth. */
    int   getMaxDepth() const { return maxDepth_; }
    /** @brief Item capacity before a node splits. */
    int   getMaxPerNode() const { return maxPerNode_; }

private:
    struct Node {
        AABB2                 bounds;
        int                   depth = 0;
        int                   axis  = 0;  // 0=X, 1=Y
        float                 split = 0.f;
        std::vector<int>      itemIds;
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
        bool                  isLeaf() const { return left == nullptr; }
    };

    void  rebuild();
    bool  insertInto(Node &node, int id, const AABB2 &bounds);
    void  split(Node &node);
    void  collect(Node &node, const AABB2 *rect, float cx, float cy, float radius, bool useCircle);
    Node *ensureRoot();

    AABB2                          rootBounds_;
    int                            maxDepth_   = 12;
    int                            maxPerNode_ = 8;
    std::unique_ptr<Node>          root_;
    std::unordered_map<int, AABB2> items_;
    QueryIds                       results_;
};

}  // namespace eve::spatial
