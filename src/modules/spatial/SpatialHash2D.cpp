#include "spatial/SpatialHash2D.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>

namespace eve::spatial {

SpatialHash2D::SpatialHash2D(float cellSize) { setCellSize(cellSize); }

void SpatialHash2D::setCellSize(float cellSize) {
    if (cellSize <= 0.f) {
        throw Exception("SpatialHash2D: cellSize must be > 0");
    }
    if (!items_.empty() && cellSize != cellSize_) {
        // Rebuild with new size.
        auto snapshot = items_;
        clear();
        cellSize_ = cellSize;
        for (const auto &kv : snapshot) {
            insert(kv.first, kv.second.minX, kv.second.minY, kv.second.maxX, kv.second.maxY);
        }
        return;
    }
    cellSize_ = cellSize;
}

void SpatialHash2D::clear() {
    items_.clear();
    cells_.clear();
    results_.clear();
}

bool SpatialHash2D::contains(int id) const { return items_.find(id) != items_.end(); }

void SpatialHash2D::cellRange(const AABB2 &b, int &minCX, int &minCY, int &maxCX,
                              int &maxCY) const {
    minCX = static_cast<int>(std::floor(b.minX / cellSize_));
    minCY = static_cast<int>(std::floor(b.minY / cellSize_));
    maxCX = static_cast<int>(std::floor(b.maxX / cellSize_));
    maxCY = static_cast<int>(std::floor(b.maxY / cellSize_));
}

void SpatialHash2D::insertCells(int id, const AABB2 &b) {
    int minCX, minCY, maxCX, maxCY;
    cellRange(b, minCX, minCY, maxCX, maxCY);
    for (int cy = minCY; cy <= maxCY; ++cy) {
        for (int cx = minCX; cx <= maxCX; ++cx) {
            cells_[cellKey2(cx, cy)].push_back(id);
        }
    }
}

void SpatialHash2D::eraseCells(int id, const AABB2 &b) {
    int minCX, minCY, maxCX, maxCY;
    cellRange(b, minCX, minCY, maxCX, maxCY);
    for (int cy = minCY; cy <= maxCY; ++cy) {
        for (int cx = minCX; cx <= maxCX; ++cx) {
            auto it = cells_.find(cellKey2(cx, cy));
            if (it == cells_.end()) continue;
            auto &vec = it->second;
            vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
            if (vec.empty()) cells_.erase(it);
        }
    }
}

bool SpatialHash2D::insert(int id, float minX, float minY, float maxX, float maxY) {
    AABB2 b = makeAABB2(minX, minY, maxX, maxY);
    if (!b.valid()) return false;
    if (contains(id)) remove(id);
    items_[id] = b;
    insertCells(id, b);
    return true;
}

bool SpatialHash2D::remove(int id) {
    auto it = items_.find(id);
    if (it == items_.end()) return false;
    eraseCells(id, it->second);
    items_.erase(it);
    return true;
}

bool SpatialHash2D::update(int id, float minX, float minY, float maxX, float maxY) {
    if (!contains(id)) return false;
    return insert(id, minX, minY, maxX, maxY);
}

void SpatialHash2D::queryCells(int minCX, int minCY, int maxCX, int maxCY, const AABB2 *rect,
                               float cx, float cy, float radius, bool useCircle, bool usePoint) {
    results_.clear();
    std::unordered_set<int> seen;
    for (int y = minCY; y <= maxCY; ++y) {
        for (int x = minCX; x <= maxCX; ++x) {
            auto it = cells_.find(cellKey2(x, y));
            if (it == cells_.end()) continue;
            for (int id : it->second) {
                if (!seen.insert(id).second) continue;
                auto item = items_.find(id);
                if (item == items_.end()) continue;
                const AABB2 &b = item->second;
                bool hit       = false;
                if (rect) {
                    hit = b.intersectsAABB(*rect);
                } else if (useCircle) {
                    hit = b.intersectsCircle(cx, cy, radius);
                } else if (usePoint) {
                    hit = b.containsPoint(cx, cy);
                }
                if (hit) results_.addUnchecked(id);
            }
        }
    }
}

int SpatialHash2D::queryPoint(float x, float y) {
    const int cx = static_cast<int>(std::floor(x / cellSize_));
    const int cy = static_cast<int>(std::floor(y / cellSize_));
    queryCells(cx, cy, cx, cy, nullptr, x, y, 0.f, false, true);
    return results_.getCount();
}

int SpatialHash2D::queryRect(float minX, float minY, float maxX, float maxY) {
    AABB2 rect = makeAABB2(minX, minY, maxX, maxY);
    int minCX, minCY, maxCX, maxCY;
    cellRange(rect, minCX, minCY, maxCX, maxCY);
    queryCells(minCX, minCY, maxCX, maxCY, &rect, 0.f, 0.f, 0.f, false, false);
    return results_.getCount();
}

int SpatialHash2D::queryCircle(float cx, float cy, float radius) {
    if (radius < 0.f) radius = 0.f;
    AABB2 rect = makeAABB2(cx - radius, cy - radius, cx + radius, cy + radius);
    int minCX, minCY, maxCX, maxCY;
    cellRange(rect, minCX, minCY, maxCX, maxCY);
    queryCells(minCX, minCY, maxCX, maxCY, nullptr, cx, cy, radius, true, false);
    return results_.getCount();
}

}  // namespace eve::spatial
