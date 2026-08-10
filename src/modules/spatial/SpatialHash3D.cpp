#include "spatial/SpatialHash3D.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>

namespace eve::spatial {

SpatialHash3D::SpatialHash3D(float cellSize) { setCellSize(cellSize); }

void SpatialHash3D::setCellSize(float cellSize) {
    if (cellSize <= 0.f) {
        throw Exception("SpatialHash3D: cellSize must be > 0");
    }
    if (!items_.empty() && cellSize != cellSize_) {
        auto snapshot = items_;
        clear();
        cellSize_ = cellSize;
        for (const auto &kv : snapshot) {
            insert(kv.first, kv.second.minX, kv.second.minY, kv.second.minZ, kv.second.maxX,
                   kv.second.maxY, kv.second.maxZ);
        }
        return;
    }
    cellSize_ = cellSize;
}

void SpatialHash3D::clear() {
    items_.clear();
    cells_.clear();
    results_.clear();
}

bool SpatialHash3D::contains(int id) const { return items_.find(id) != items_.end(); }

void SpatialHash3D::cellRange(const AABB3 &b, int &minCX, int &minCY, int &minCZ, int &maxCX,
                              int &maxCY, int &maxCZ) const {
    minCX = static_cast<int>(std::floor(b.minX / cellSize_));
    minCY = static_cast<int>(std::floor(b.minY / cellSize_));
    minCZ = static_cast<int>(std::floor(b.minZ / cellSize_));
    maxCX = static_cast<int>(std::floor(b.maxX / cellSize_));
    maxCY = static_cast<int>(std::floor(b.maxY / cellSize_));
    maxCZ = static_cast<int>(std::floor(b.maxZ / cellSize_));
}

void SpatialHash3D::insertCells(int id, const AABB3 &b) {
    int minCX, minCY, minCZ, maxCX, maxCY, maxCZ;
    cellRange(b, minCX, minCY, minCZ, maxCX, maxCY, maxCZ);
    for (int cz = minCZ; cz <= maxCZ; ++cz) {
        for (int cy = minCY; cy <= maxCY; ++cy) {
            for (int cx = minCX; cx <= maxCX; ++cx) {
                cells_[cellKey3(cx, cy, cz)].push_back(id);
            }
        }
    }
}

void SpatialHash3D::eraseCells(int id, const AABB3 &b) {
    int minCX, minCY, minCZ, maxCX, maxCY, maxCZ;
    cellRange(b, minCX, minCY, minCZ, maxCX, maxCY, maxCZ);
    for (int cz = minCZ; cz <= maxCZ; ++cz) {
        for (int cy = minCY; cy <= maxCY; ++cy) {
            for (int cx = minCX; cx <= maxCX; ++cx) {
                auto it = cells_.find(cellKey3(cx, cy, cz));
                if (it == cells_.end()) continue;
                auto &vec = it->second;
                vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
                if (vec.empty()) cells_.erase(it);
            }
        }
    }
}

bool SpatialHash3D::insert(int id, float minX, float minY, float minZ, float maxX, float maxY,
                           float maxZ) {
    AABB3 b = makeAABB3(minX, minY, minZ, maxX, maxY, maxZ);
    if (!b.valid()) return false;
    if (contains(id)) remove(id);
    items_[id] = b;
    insertCells(id, b);
    return true;
}

bool SpatialHash3D::remove(int id) {
    auto it = items_.find(id);
    if (it == items_.end()) return false;
    eraseCells(id, it->second);
    items_.erase(it);
    return true;
}

bool SpatialHash3D::update(int id, float minX, float minY, float minZ, float maxX, float maxY,
                           float maxZ) {
    if (!contains(id)) return false;
    return insert(id, minX, minY, minZ, maxX, maxY, maxZ);
}

void SpatialHash3D::queryCells(int minCX, int minCY, int minCZ, int maxCX, int maxCY, int maxCZ,
                               const AABB3 *box, float cx, float cy, float cz, float radius,
                               bool useSphere, bool usePoint) {
    results_.clear();
    std::unordered_set<int> seen;
    for (int z = minCZ; z <= maxCZ; ++z) {
        for (int y = minCY; y <= maxCY; ++y) {
            for (int x = minCX; x <= maxCX; ++x) {
                auto it = cells_.find(cellKey3(x, y, z));
                if (it == cells_.end()) continue;
                for (int id : it->second) {
                    if (!seen.insert(id).second) continue;
                    auto item = items_.find(id);
                    if (item == items_.end()) continue;
                    const AABB3 &b = item->second;
                    bool hit       = false;
                    if (box) {
                        hit = b.intersectsAABB(*box);
                    } else if (useSphere) {
                        hit = b.intersectsSphere(cx, cy, cz, radius);
                    } else if (usePoint) {
                        hit = b.containsPoint(cx, cy, cz);
                    }
                    if (hit) results_.addUnchecked(id);
                }
            }
        }
    }
}

int SpatialHash3D::queryPoint(float x, float y, float z) {
    const int cx = static_cast<int>(std::floor(x / cellSize_));
    const int cy = static_cast<int>(std::floor(y / cellSize_));
    const int cz = static_cast<int>(std::floor(z / cellSize_));
    queryCells(cx, cy, cz, cx, cy, cz, nullptr, x, y, z, 0.f, false, true);
    return results_.getCount();
}

int SpatialHash3D::queryAABB(float minX, float minY, float minZ, float maxX, float maxY,
                             float maxZ) {
    AABB3 box = makeAABB3(minX, minY, minZ, maxX, maxY, maxZ);
    int minCX, minCY, minCZ, maxCX, maxCY, maxCZ;
    cellRange(box, minCX, minCY, minCZ, maxCX, maxCY, maxCZ);
    queryCells(minCX, minCY, minCZ, maxCX, maxCY, maxCZ, &box, 0.f, 0.f, 0.f, 0.f, false, false);
    return results_.getCount();
}

int SpatialHash3D::querySphere(float cx, float cy, float cz, float radius) {
    if (radius < 0.f) radius = 0.f;
    AABB3 box = makeAABB3(cx - radius, cy - radius, cz - radius, cx + radius, cy + radius,
                          cz + radius);
    int minCX, minCY, minCZ, maxCX, maxCY, maxCZ;
    cellRange(box, minCX, minCY, minCZ, maxCX, maxCY, maxCZ);
    queryCells(minCX, minCY, minCZ, maxCX, maxCY, maxCZ, nullptr, cx, cy, cz, radius, true, false);
    return results_.getCount();
}

}  // namespace eve::spatial
