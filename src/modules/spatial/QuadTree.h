#pragma once

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
class QuadTree {
public:
    QuadTree(float minX, float minY, float maxX, float maxY, int maxDepth = 8,
             int maxPerNode = 8);
    ~QuadTree() = default;

    QuadTree(const QuadTree &)            = delete;
    QuadTree &operator=(const QuadTree &) = delete;

    void clear();
    bool insert(int id, float minX, float minY, float maxX, float maxY);
    bool remove(int id);
    bool update(int id, float minX, float minY, float maxX, float maxY);
    bool contains(int id) const;
    int  getCount() const { return static_cast<int>(items_.size()); }

    int queryPoint(float x, float y);
    int queryRect(float minX, float minY, float maxX, float maxY);
    int queryCircle(float cx, float cy, float radius);

    int getResultCount() const { return results_.getCount(); }
    int getResultId(int index) const { return results_.getId(index); }

    float getMinX() const { return rootBounds_.minX; }
    float getMinY() const { return rootBounds_.minY; }
    float getMaxX() const { return rootBounds_.maxX; }
    float getMaxY() const { return rootBounds_.maxY; }
    int   getMaxDepth() const { return maxDepth_; }
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
