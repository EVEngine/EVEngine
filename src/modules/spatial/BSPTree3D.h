#pragma once

#include "spatial/Bounds.h"
#include "spatial/QueryIds.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace eve::spatial {

/**
 * Binary space partition tree (kd-style AABB splits) for 3D culling.
 * Alternating X/Y/Z splits at node midplanes.
 */
class BSPTree3D {
public:
    BSPTree3D(float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
              int maxDepth = 12, int maxPerNode = 8);
    ~BSPTree3D() = default;

    BSPTree3D(const BSPTree3D &)            = delete;
    BSPTree3D &operator=(const BSPTree3D &) = delete;

    void clear();
    bool insert(int id, float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    bool remove(int id);
    bool update(int id, float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    bool contains(int id) const;
    int  getCount() const { return static_cast<int>(items_.size()); }

    int queryPoint(float x, float y, float z);
    int queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    int querySphere(float cx, float cy, float cz, float radius);

    int getResultCount() const { return results_.getCount(); }
    int getResultId(int index) const { return results_.getId(index); }

    float getMinX() const { return rootBounds_.minX; }
    float getMinY() const { return rootBounds_.minY; }
    float getMinZ() const { return rootBounds_.minZ; }
    float getMaxX() const { return rootBounds_.maxX; }
    float getMaxY() const { return rootBounds_.maxY; }
    float getMaxZ() const { return rootBounds_.maxZ; }
    int   getMaxDepth() const { return maxDepth_; }
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
