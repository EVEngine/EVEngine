#include "map/Pathfinder.h"

#include "map/PathTopology.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <vector>

namespace eve::map {
namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

struct HeapNode {
    float f = 0.f;
    int idx = 0;
    bool operator>(const HeapNode &o) const { return f > o.f; }
};

}  // namespace

Pathfinder::Pathfinder(TileLayer *layer) { bindLayer(layer); }

Pathfinder::Pathfinder(int width, int height) { setSize(width, height); }

void Pathfinder::bindLayer(TileLayer *layer) {
    grid_.bindLayer(layer);
    invalidateCache();
}

void Pathfinder::setSize(int width, int height) {
    grid_.clearLayer();
    grid_.resize(width, height);
    invalidateCache();
}

void Pathfinder::setTopology(const std::string &name) {
    grid_.setTopology(name);
    invalidateCache();
}

std::string Pathfinder::getTopology() const { return grid_.getTopology(); }

void Pathfinder::setDiagonal(bool enable) {
    grid_.setDiagonal(enable);
    invalidateCache();
}

bool Pathfinder::getDiagonal() const { return grid_.getDiagonal(); }

void Pathfinder::blockGid(int gid) {
    grid_.blockGid(gid);
    invalidateCache();
}

void Pathfinder::unblockGid(int gid) {
    grid_.unblockGid(gid);
    invalidateCache();
}

void Pathfinder::clearBlockedGids() {
    grid_.clearBlockedGids();
    invalidateCache();
}

void Pathfinder::setBlockEmpty(bool enable) {
    grid_.setBlockEmpty(enable);
    invalidateCache();
}

bool Pathfinder::getBlockEmpty() const { return grid_.getBlockEmpty(); }

void Pathfinder::setBlocked(int x, int y, bool blocked) {
    grid_.setBlocked(x, y, blocked);
    invalidateCache();
}

bool Pathfinder::isWalkable(int x, int y) const { return grid_.isWalkable(x, y); }

void Pathfinder::setCellCost(int x, int y, float cost) {
    grid_.setCellCost(x, y, cost);
    invalidateCache();
}

float Pathfinder::getCellCost(int x, int y) const { return grid_.getCellCost(x, y); }

void Pathfinder::syncFromLayer() {
    grid_.syncFromLayer();
    invalidateCache();
}

void Pathfinder::invalidateCache() {
    hasCachedField_ = false;
    cachedField_.clear();
}

bool Pathfinder::ensureSynced() {
    if (grid_.getLayer() && grid_.isDirty()) {
        // Layer may have changed tiles without going through PathGrid setters.
        grid_.syncFromLayer();
    }
    return grid_.getWidth() > 0 && grid_.getHeight() > 0;
}

Path *Pathfinder::findPath(int sx, int sy, int gx, int gy) {
    auto *path = new Path();
    if (!ensureSynced()) return path;
    if (!grid_.isWalkable(sx, sy) || !grid_.isWalkable(gx, gy)) return path;
    if (sx == gx && sy == gy) {
        path->add(sx, sy);
        path->setTotalCost(0.f);
        return path;
    }

    const int w = grid_.getWidth();
    const int h = grid_.getHeight();
    const int n = w * h;
    const auto idx = [w](int x, int y) { return y * w + x; };

    std::vector<float> gScore(size_t(n), kInf);
    std::vector<int> parent(size_t(n), -1);
    std::vector<uint8_t> closed(size_t(n), 0);

    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> open;
    const int start = idx(sx, sy);
    const int goal = idx(gx, gy);
    gScore[size_t(start)] = 0.f;
    open.push(HeapNode{pathHeuristic(grid_.topology(), sx, sy, gx, gy), start});

    bool found = false;
    while (!open.empty()) {
        const HeapNode cur = open.top();
        open.pop();
        if (closed[size_t(cur.idx)]) continue;
        closed[size_t(cur.idx)] = 1;
        if (cur.idx == goal) {
            found = true;
            break;
        }
        const int cx = cur.idx % w;
        const int cy = cur.idx / w;
        grid_.forEachWalkableNeighbor(cx, cy, [&](int nx, int ny, float edgeCost) {
            const int ni = idx(nx, ny);
            if (closed[size_t(ni)]) return;
            const float tentative = gScore[size_t(cur.idx)] + edgeCost;
            if (tentative >= gScore[size_t(ni)]) return;
            gScore[size_t(ni)] = tentative;
            parent[size_t(ni)] = cur.idx;
            const float f = tentative + pathHeuristic(grid_.topology(), nx, ny, gx, gy);
            open.push(HeapNode{f, ni});
        });
    }

    if (!found) return path;

    // Reconstruct
    int cur = goal;
    while (cur != -1) {
        path->add(cur % w, cur / w);
        if (cur == start) break;
        cur = parent[size_t(cur)];
    }
    path->reverse();
    path->setTotalCost(gScore[size_t(goal)]);
    return path;
}

FlowField *Pathfinder::buildFlowFieldUncached(int gx, int gy) {
    auto *field = new FlowField();
    if (!ensureSynced() || !grid_.isWalkable(gx, gy)) {
        field->resize(grid_.getWidth(), grid_.getHeight());
        field->setGoal(gx, gy);
        return field;
    }

    const int w = grid_.getWidth();
    const int h = grid_.getHeight();
    field->resize(w, h);
    field->setGoal(gx, gy);

    const auto idx = [w](int x, int y) { return y * w + x; };
    std::vector<float> dist(size_t(w * h), kInf);
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> open;

    dist[size_t(idx(gx, gy))] = 0.f;
    field->setCost(gx, gy, 0.f);
    field->setNext(gx, gy, gx, gy);
    open.push(HeapNode{0.f, idx(gx, gy)});

    while (!open.empty()) {
        const HeapNode cur = open.top();
        open.pop();
        if (cur.f > dist[size_t(cur.idx)]) continue;
        const int cx = cur.idx % w;
        const int cy = cur.idx / w;
        // Expand outward from the goal. Agents later walk neighbor → current, so the step
        // cost must be "enter current" (= moveCost * cellCost(cx,cy)), matching A*.
        const float enterCur = grid_.getCellCost(cx, cy);
        forEachNeighbor(grid_.topology(), cx, cy, grid_.staggerAxisY(), grid_.staggerOdd(),
                        [&](int nx, int ny, float moveCost) {
                            if (!grid_.isWalkable(nx, ny)) return;
                            if (grid_.topology() == PathTopology::Ortho8) {
                                const int dx = nx - cx;
                                const int dy = ny - cy;
                                if (dx != 0 && dy != 0) {
                                    if (!grid_.isWalkable(cx + dx, cy) || !grid_.isWalkable(cx, cy + dy))
                                        return;
                                }
                            }
                            const int ni = idx(nx, ny);
                            const float tentative = dist[size_t(cur.idx)] + moveCost * enterCur;
                            if (tentative + 1e-6f >= dist[size_t(ni)]) return;
                            dist[size_t(ni)] = tentative;
                            field->setCost(nx, ny, tentative);
                            field->setNext(nx, ny, cx, cy);
                            open.push(HeapNode{tentative, ni});
                        });
    }
    return field;
}

FlowField *Pathfinder::buildFlowField(int gx, int gy) {
    if (!ensureSynced()) {
        auto *empty = new FlowField();
        empty->resize(0, 0);
        empty->setGoal(gx, gy);
        return empty;
    }

    if (hasCachedField_ && !grid_.isDirty() && cachedGoalX_ == gx && cachedGoalY_ == gy &&
        cachedField_.getWidth() == grid_.getWidth() &&
        cachedField_.getHeight() == grid_.getHeight()) {
        auto *copy = new FlowField();
        *copy = cachedField_;
        return copy;
    }

    FlowField *field = buildFlowFieldUncached(gx, gy);
    cachedField_ = *field;
    cachedGoalX_ = gx;
    cachedGoalY_ = gy;
    hasCachedField_ = true;
    grid_.clearDirty();
    return field;
}

Path *Pathfinder::followFlow(FlowField *field, int sx, int sy) {
    auto *path = new Path();
    if (!field || !ensureSynced()) return path;
    if (!field->isReachable(sx, sy)) return path;

    const int gx = field->getGoalX();
    const int gy = field->getGoalY();
    const int maxSteps = grid_.getWidth() * grid_.getHeight() + 2;
    int x = sx;
    int y = sy;
    path->add(x, y);
    float total = 0.f;
    for (int step = 0; step < maxSteps; ++step) {
        if (x == gx && y == gy) break;
        const int nx = field->nextX(x, y);
        const int ny = field->nextY(x, y);
        if (nx == x && ny == y) break;  // stuck
        // Approximate step cost from field integration delta
        const float c0 = field->costAt(x, y);
        const float c1 = field->costAt(nx, ny);
        if (c0 < kInf && c1 < kInf && c0 >= c1) total += (c0 - c1);
        x = nx;
        y = ny;
        path->add(x, y);
    }
    path->setTotalCost(total);
    if (path->getLength() > 0) {
        const int lx = path->getX(path->getLength() - 1);
        const int ly = path->getY(path->getLength() - 1);
        if (lx != gx || ly != gy) path->clear();
    }
    return path;
}

Path *Pathfinder::findGroupPath(int sx, int sy, int gx, int gy) {
    // Use cache: buildFlowField updates cache; follow without forcing a new alloc of field.
    if (!ensureSynced()) return new Path();
    if (!hasCachedField_ || grid_.isDirty() || cachedGoalX_ != gx || cachedGoalY_ != gy ||
        cachedField_.getWidth() != grid_.getWidth() ||
        cachedField_.getHeight() != grid_.getHeight()) {
        FlowField *tmp = buildFlowFieldUncached(gx, gy);
        cachedField_ = *tmp;
        delete tmp;
        cachedGoalX_ = gx;
        cachedGoalY_ = gy;
        hasCachedField_ = true;
        grid_.clearDirty();
    }
    return followFlow(&cachedField_, sx, sy);
}

}  // namespace eve::map
