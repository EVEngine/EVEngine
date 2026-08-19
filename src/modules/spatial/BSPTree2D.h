#pragma once

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
class BSPTree2D {
public:
    BSPTree2D(float minX, float minY, float maxX, float maxY, int maxDepth = 12,
              int maxPerNode = 8);
    ~BSPTree2D() = default;

    BSPTree2D(const BSPTree2D &)            = delete;
    BSPTree2D &operator=(const BSPTree2D &) = delete;

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
