#pragma once
#include "common/Export.h"


#include "spatial/Bounds.h"
#include "spatial/QueryIds.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eve::spatial {

/**
 * @brief Uniform-grid spatial hash for 2D AABB queries (map / particle / entity culling).
 * Items are registered in every overlapped cell.
 */
class EVENGINE_API_FOUNDATION SpatialHash2D {
public:
    /** @brief Creates a 2D spatial hash with the given cell size. */
    explicit SpatialHash2D(float cellSize = 64.f);
    /** @brief Releases hash buckets. */
    ~SpatialHash2D() = default;

    SpatialHash2D(const SpatialHash2D &)            = delete;
    SpatialHash2D &operator=(const SpatialHash2D &) = delete;

    /** @brief Removes all stored entries. */
    void  clear();
    /** @brief Sets cell size and clears existing entries. */
    void  setCellSize(float cellSize);
    /** @brief Current uniform cell size. */
    float getCellSize() const { return cellSize_; }

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

private:
    void cellRange(const AABB2 &b, int &minCX, int &minCY, int &maxCX, int &maxCY) const;
    void insertCells(int id, const AABB2 &b);
    void eraseCells(int id, const AABB2 &b);
    void queryCells(int minCX, int minCY, int maxCX, int maxCY, const AABB2 *rect, float cx,
                    float cy, float radius, bool useCircle, bool usePoint);

    float                                    cellSize_ = 64.f;
    std::unordered_map<int, AABB2>           items_;
    std::unordered_map<uint64_t, std::vector<int>> cells_;
    QueryIds                                 results_;
};

}  // namespace eve::spatial
