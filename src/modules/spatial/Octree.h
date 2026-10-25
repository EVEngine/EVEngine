#pragma once
#include "common/Export.h"


#include "spatial/Bounds.h"
#include "spatial/QueryIds.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace eve::spatial {

/**
 * @brief Region octree for 3D AABB broad-phase / scene culling.
 * Same storage rules as QuadTree (smallest fully-containing node).
 */
class EVENGINE_API_FOUNDATION Octree {
public:
    /** @brief Creates an octree covering the given 3D bounds. */
    Octree(float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
           int maxDepth = 8, int maxPerNode = 8);
    /** @brief Releases tree nodes. */
    ~Octree() = default;

    Octree(const Octree &)            = delete;
    Octree &operator=(const Octree &) = delete;

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
