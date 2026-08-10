#pragma once

#include "spatial/Bounds.h"
#include "spatial/QueryIds.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace eve::spatial {

/**
 * Region octree for 3D AABB broad-phase / scene culling.
 * Same storage rules as QuadTree (smallest fully-containing node).
 */
class Octree {
public:
    Octree(float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
           int maxDepth = 8, int maxPerNode = 8);
    ~Octree() = default;

    Octree(const Octree &)            = delete;
    Octree &operator=(const Octree &) = delete;

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
        AABB3            bounds;
        int              depth = 0;
        std::vector<int> itemIds;
        std::unique_ptr<Node> children[8];
        bool             isLeaf() const { return children[0] == nullptr; }
    };

    void  rebuild();
    bool  insertInto(Node &node, int id, const AABB3 &bounds);
    void  split(Node &node);
    void  collect(Node &node, const AABB3 *box, float cx, float cy, float cz, float radius,
                  bool useSphere);
    Node *ensureRoot();

    AABB3                              rootBounds_;
    int                                maxDepth_   = 8;
    int                                maxPerNode_ = 8;
    std::unique_ptr<Node>              root_;
    std::unordered_map<int, AABB3>     items_;
    QueryIds                           results_;
};

}  // namespace eve::spatial
