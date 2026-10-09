#pragma once
#include "common/Export.h"


#include "spatial/Bounds.h"
#include "spatial/QueryIds.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace eve::spatial {

/**
 * @brief Region quadtree for 2D AABB broad-phase / map culling.
 * Items are stored in the smallest node that fully contains them; spanning
 * items stay at the parent. Scripts use insert/remove/query* + getResult*.
 */
class EVENGINE_API_FOUNDATION QuadTree {
public:
    /** @brief Creates a quadtree covering the given 2D bounds. */
    QuadTree(float minX, float minY, float maxX, float maxY, int maxDepth = 8,
             int maxPerNode = 8);
    /** @brief Releases tree nodes. */
    ~QuadTree() = default;

    QuadTree(const QuadTree &)            = delete;
    QuadTree &operator=(const QuadTree &) = delete;

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
        AABB2            bounds;
        int              depth = 0;
        std::vector<int> itemIds;
        std::unique_ptr<Node> children[4];
        bool             isLeaf() const { return children[0] == nullptr; }
    };

    void  rebuild();
    bool  insertInto(Node &node, int id, const AABB2 &bounds);
    void  split(Node &node);
    void  collect(Node &node, const AABB2 *rect, float cx, float cy, float radius,
                  bool useCircle);
    Node *ensureRoot();

    AABB2                              rootBounds_;
    int                                maxDepth_   = 8;
    int                                maxPerNode_ = 8;
    std::unique_ptr<Node>              root_;
    std::unordered_map<int, AABB2>     items_;
    QueryIds                           results_;
};

}  // namespace eve::spatial
