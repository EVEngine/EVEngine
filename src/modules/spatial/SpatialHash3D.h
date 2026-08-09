#pragma once

#include "spatial/Bounds.h"
#include "spatial/QueryIds.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eve::spatial {

/** Uniform-grid spatial hash for 3D AABB / sphere queries. */
class SpatialHash3D {
public:
    explicit SpatialHash3D(float cellSize = 64.f);
    ~SpatialHash3D() = default;

    SpatialHash3D(const SpatialHash3D &)            = delete;
    SpatialHash3D &operator=(const SpatialHash3D &) = delete;

    void  clear();
    void  setCellSize(float cellSize);
    float getCellSize() const { return cellSize_; }

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

private:
    void cellRange(const AABB3 &b, int &minCX, int &minCY, int &minCZ, int &maxCX, int &maxCY,
                   int &maxCZ) const;
    void insertCells(int id, const AABB3 &b);
    void eraseCells(int id, const AABB3 &b);
    void queryCells(int minCX, int minCY, int minCZ, int maxCX, int maxCY, int maxCZ,
                    const AABB3 *box, float cx, float cy, float cz, float radius, bool useSphere,
                    bool usePoint);

    float                                          cellSize_ = 64.f;
    std::unordered_map<int, AABB3>                 items_;
    std::unordered_map<uint64_t, std::vector<int>> cells_;
    QueryIds                                       results_;
};

}  // namespace eve::spatial
