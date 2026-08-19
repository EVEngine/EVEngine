#pragma once

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
class SpatialHash2D {
public:
    explicit SpatialHash2D(float cellSize = 64.f);
    ~SpatialHash2D() = default;

    SpatialHash2D(const SpatialHash2D &)            = delete;
    SpatialHash2D &operator=(const SpatialHash2D &) = delete;

    void  clear();
    void  setCellSize(float cellSize);
    float getCellSize() const { return cellSize_; }

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
