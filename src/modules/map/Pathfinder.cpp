#include "map/Pathfinder.h"

#include "map/TileOrientation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace eve::map {
namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kSqrt2 = 1.41421356237f;

enum class Topology { Ortho4, Ortho8, Hex };

using NeighborFn = std::function<void(int nx, int ny, float moveCost)>;

Topology parseTopology(const std::string &name, Topology fallback) {
    if (name == "ortho4" || name == "orthogonal4" || name == "4") return Topology::Ortho4;
    if (name == "ortho8" || name == "orthogonal8" || name == "8") return Topology::Ortho8;
    if (name == "hex" || name == "hexagonal" || name == "staggered") return Topology::Hex;
    if (name == "auto") return fallback;
    return fallback;
}

const char *topologyName(Topology t) {
    switch (t) {
    case Topology::Ortho4:
        return "ortho4";
    case Topology::Ortho8:
        return "ortho8";
    case Topology::Hex:
        return "hex";
    }
    return "ortho4";
}

Topology topologyFromOrientation(MapOrientation orientation, bool preferDiagonal) {
    switch (orientation) {
    case MapOrientation::Staggered:
    case MapOrientation::Hexagonal:
        return Topology::Hex;
    case MapOrientation::Orthogonal:
    case MapOrientation::Isometric:
    default:
        return preferDiagonal ? Topology::Ortho8 : Topology::Ortho4;
    }
}

void forEachNeighbor(Topology topology, int x, int y, bool staggerAxisY, bool staggerOdd,
                     const NeighborFn &fn) {
    if (!fn) return;
    switch (topology) {
    case Topology::Ortho4: {
        static const int dx[4] = {1, -1, 0, 0};
        static const int dy[4] = {0, 0, 1, -1};
        for (int i = 0; i < 4; ++i) fn(x + dx[i], y + dy[i], 1.f);
        break;
    }
    case Topology::Ortho8: {
        static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        for (int i = 0; i < 8; ++i) {
            const float c = (i < 4) ? 1.f : kSqrt2;
            fn(x + dx[i], y + dy[i], c);
        }
        break;
    }
    case Topology::Hex: {
        if (staggerAxisY) {
            const bool rowOdd = ((y & 1) != 0);
            const bool shifted = staggerOdd ? rowOdd : !rowOdd;
            if (shifted) {
                fn(x + 1, y, 1.f);
                fn(x - 1, y, 1.f);
                fn(x, y - 1, 1.f);
                fn(x + 1, y - 1, 1.f);
                fn(x, y + 1, 1.f);
                fn(x + 1, y + 1, 1.f);
            } else {
                fn(x + 1, y, 1.f);
                fn(x - 1, y, 1.f);
                fn(x - 1, y - 1, 1.f);
                fn(x, y - 1, 1.f);
                fn(x - 1, y + 1, 1.f);
                fn(x, y + 1, 1.f);
            }
        } else {
            const bool colOdd = ((x & 1) != 0);
            const bool shifted = staggerOdd ? colOdd : !colOdd;
            if (shifted) {
                fn(x, y + 1, 1.f);
                fn(x, y - 1, 1.f);
                fn(x - 1, y, 1.f);
                fn(x - 1, y + 1, 1.f);
                fn(x + 1, y, 1.f);
                fn(x + 1, y + 1, 1.f);
            } else {
                fn(x, y + 1, 1.f);
                fn(x, y - 1, 1.f);
                fn(x - 1, y - 1, 1.f);
                fn(x - 1, y, 1.f);
                fn(x + 1, y - 1, 1.f);
                fn(x + 1, y, 1.f);
            }
        }
        break;
    }
    }
}

float heuristic(Topology topology, int x0, int y0, int x1, int y1) {
    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);
    switch (topology) {
    case Topology::Ortho4:
        return float(dx + dy);
    case Topology::Ortho8:
        return float(std::max(dx, dy)) + (kSqrt2 - 1.f) * float(std::min(dx, dy));
    case Topology::Hex: {
        auto toCube = [](int x, int y) {
            const int q = x - (y - (y & 1)) / 2;
            const int r = y;
            const int s = -q - r;
            return std::tuple<int, int, int>{q, r, s};
        };
        auto [q0, r0, s0] = toCube(x0, y0);
        auto [q1, r1, s1] = toCube(x1, y1);
        return float(std::max({std::abs(q0 - q1), std::abs(r0 - r1), std::abs(s0 - s1)}));
    }
    }
    return float(dx + dy);
}

/** Navigable grid — internal linkage to keep Windows export table small. */
struct Grid {
    int width = 0;
    int height = 0;
    TileLayer *layer = nullptr;
    uint64_t layerRevision = 0;
    Topology topology = Topology::Ortho4;
    bool topologyManual = false;
    bool diagonal = false;
    bool blockEmpty = true;
    bool staggerAxisY = true;
    bool staggerOdd = true;
    bool dirty = true;
    std::vector<float> cost;  // ≤0 => blocked
    std::unordered_set<uint32_t> blockedGids;

    int index(int x, int y) const { return y * width + x; }
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }

    void resize(int w, int h) {
        width = w > 0 ? w : 0;
        height = h > 0 ? h : 0;
        cost.assign(size_t(width * height), 1.f);
        dirty = true;
    }

    void applyAutoTopologyFromLayer() {
        if (!layer) return;
        topology = topologyFromOrientation(layer->config()->orientation, diagonal);
    }

    void bindLayer(TileLayer *l) {
        layer = l;
        if (!layer) return;
        auto cfg = layer->config();
        resize(cfg->mapW, cfg->mapH);
        staggerAxisY = cfg->staggerAxis == StaggerAxis::Y;
        staggerOdd = cfg->staggerIndex == StaggerIndex::Odd;
        if (!topologyManual) applyAutoTopologyFromLayer();
        syncFromLayer();
    }

    void clearLayer() { layer = nullptr; }

    void syncFromLayer() {
        if (!layer) return;
        auto cfg = layer->config();
        auto tiles = layer->tiles();
        if (cfg->mapW != width || cfg->mapH != height) resize(cfg->mapW, cfg->mapH);
        staggerAxisY = cfg->staggerAxis == StaggerAxis::Y;
        staggerOdd = cfg->staggerIndex == StaggerIndex::Odd;
        if (!topologyManual) applyAutoTopologyFromLayer();

        const int n = width * height;
        for (int i = 0; i < n; ++i) {
            const uint32_t gid = (i < int(tiles->gids.size())) ? tileGid(tiles->gids[size_t(i)]) : 0u;
            bool blocked = false;
            if (blockEmpty && gid == 0u) blocked = true;
            if (blockedGids.count(gid)) blocked = true;
            cost[size_t(i)] = blocked ? 0.f : 1.f;
        }
        layerRevision = tiles->revision;
        dirty = true;
    }

    void setTopology(const std::string &name) {
        if (name == "auto") {
            topologyManual = false;
            if (layer) applyAutoTopologyFromLayer();
            else topology = diagonal ? Topology::Ortho8 : Topology::Ortho4;
        } else {
            topologyManual = true;
            topology = parseTopology(name, topology);
        }
        dirty = true;
    }

    void setDiagonal(bool enable) {
        diagonal = enable;
        if (!topologyManual && layer) applyAutoTopologyFromLayer();
        else if (!topologyManual) topology = diagonal ? Topology::Ortho8 : Topology::Ortho4;
        dirty = true;
    }

    void blockGid(int gid) {
        if (gid < 0) return;
        blockedGids.insert(uint32_t(gid));
        dirty = true;
        if (layer) syncFromLayer();
    }

    void unblockGid(int gid) {
        if (gid < 0) return;
        blockedGids.erase(uint32_t(gid));
        dirty = true;
        if (layer) syncFromLayer();
    }

    void clearBlockedGids() {
        blockedGids.clear();
        dirty = true;
        if (layer) syncFromLayer();
    }

    void setBlockEmpty(bool enable) {
        blockEmpty = enable;
        dirty = true;
        if (layer) syncFromLayer();
    }

    bool isWalkable(int x, int y) const {
        if (!inBounds(x, y)) return false;
        return cost[size_t(index(x, y))] > 0.f;
    }

    void setBlocked(int x, int y, bool blocked) {
        if (!inBounds(x, y)) return;
        cost[size_t(index(x, y))] = blocked ? 0.f : 1.f;
        dirty = true;
    }

    void setCellCost(int x, int y, float c) {
        if (!inBounds(x, y)) return;
        cost[size_t(index(x, y))] = c;
        dirty = true;
    }

    float getCellCost(int x, int y) const {
        if (!inBounds(x, y)) return 0.f;
        return cost[size_t(index(x, y))];
    }

    void forEachWalkableNeighbor(int x, int y, const NeighborFn &fn) const {
        if (!fn || !inBounds(x, y)) return;
        forEachNeighbor(topology, x, y, staggerAxisY, staggerOdd, [&](int nx, int ny, float moveCost) {
            if (!isWalkable(nx, ny)) return;
            if (topology == Topology::Ortho8) {
                const int dx = nx - x;
                const int dy = ny - y;
                if (dx != 0 && dy != 0) {
                    if (!isWalkable(x + dx, y) || !isWalkable(x, y + dy)) return;
                }
            }
            fn(nx, ny, moveCost * getCellCost(nx, ny));
        });
    }
};

struct HeapNode {
    float f = 0.f;
    int idx = 0;
    bool operator>(const HeapNode &o) const { return f > o.f; }
};

}  // namespace

struct Pathfinder::Impl {
    Grid grid;
    bool hasCachedField = false;
    int cachedGoalX = 0;
    int cachedGoalY = 0;
    FlowField cachedField;
};

Pathfinder::Pathfinder() : impl_(std::make_unique<Impl>()) {}

Pathfinder::Pathfinder(TileLayer *layer) : Pathfinder() { bindLayer(layer); }

Pathfinder::Pathfinder(int width, int height) : Pathfinder() { setSize(width, height); }

Pathfinder::~Pathfinder() = default;
Pathfinder::Pathfinder(Pathfinder &&) noexcept = default;
Pathfinder &Pathfinder::operator=(Pathfinder &&) noexcept = default;

void Pathfinder::bindLayer(TileLayer *layer) {
    impl_->grid.bindLayer(layer);
    invalidateCache();
}

void Pathfinder::setSize(int width, int height) {
    impl_->grid.clearLayer();
    impl_->grid.resize(width, height);
    invalidateCache();
}

void Pathfinder::setTopology(const std::string &name) {
    impl_->grid.setTopology(name);
    invalidateCache();
}

std::string Pathfinder::getTopology() const { return topologyName(impl_->grid.topology); }

void Pathfinder::setDiagonal(bool enable) {
    impl_->grid.setDiagonal(enable);
    invalidateCache();
}

bool Pathfinder::getDiagonal() const { return impl_->grid.diagonal; }

void Pathfinder::blockGid(int gid) {
    impl_->grid.blockGid(gid);
    invalidateCache();
}

void Pathfinder::unblockGid(int gid) {
    impl_->grid.unblockGid(gid);
    invalidateCache();
}

void Pathfinder::clearBlockedGids() {
    impl_->grid.clearBlockedGids();
    invalidateCache();
}

void Pathfinder::setBlockEmpty(bool enable) {
    impl_->grid.setBlockEmpty(enable);
    invalidateCache();
}

bool Pathfinder::getBlockEmpty() const { return impl_->grid.blockEmpty; }

void Pathfinder::setBlocked(int x, int y, bool blocked) {
    impl_->grid.setBlocked(x, y, blocked);
    invalidateCache();
}

bool Pathfinder::isWalkable(int x, int y) const { return impl_->grid.isWalkable(x, y); }

void Pathfinder::setCellCost(int x, int y, float cost) {
    impl_->grid.setCellCost(x, y, cost);
    invalidateCache();
}

float Pathfinder::getCellCost(int x, int y) const { return impl_->grid.getCellCost(x, y); }

void Pathfinder::syncFromLayer() {
    impl_->grid.syncFromLayer();
    invalidateCache();
}

void Pathfinder::invalidateCache() {
    impl_->hasCachedField = false;
    impl_->cachedField.clear();
}

namespace {

bool ensureSynced(Grid &grid) {
    if (grid.layer && (grid.dirty || grid.layerRevision != grid.layer->tiles()->revision))
        grid.syncFromLayer();
    return grid.width > 0 && grid.height > 0;
}

FlowField *buildFlowFieldUncached(Grid &grid, int gx, int gy) {
    auto *field = new FlowField();
    if (!ensureSynced(grid) || !grid.isWalkable(gx, gy)) {
        field->resize(grid.width, grid.height);
        field->setGoal(gx, gy);
        return field;
    }

    const int w = grid.width;
    const int h = grid.height;
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
        const float enterCur = grid.getCellCost(cx, cy);
        forEachNeighbor(grid.topology, cx, cy, grid.staggerAxisY, grid.staggerOdd,
                        [&](int nx, int ny, float moveCost) {
                            if (!grid.isWalkable(nx, ny)) return;
                            if (grid.topology == Topology::Ortho8) {
                                const int dx = nx - cx;
                                const int dy = ny - cy;
                                if (dx != 0 && dy != 0) {
                                    if (!grid.isWalkable(cx + dx, cy) || !grid.isWalkable(cx, cy + dy))
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

}  // namespace

Path *Pathfinder::findPath(int sx, int sy, int gx, int gy) {
    auto *path = new Path();
    Grid &grid = impl_->grid;
    if (!ensureSynced(grid)) return path;
    if (!grid.isWalkable(sx, sy) || !grid.isWalkable(gx, gy)) return path;
    if (sx == gx && sy == gy) {
        path->add(sx, sy);
        path->setTotalCost(0.f);
        return path;
    }

    const int w = grid.width;
    const int n = w * grid.height;
    const auto idx = [w](int x, int y) { return y * w + x; };

    std::vector<float> gScore(size_t(n), kInf);
    std::vector<int> parent(size_t(n), -1);
    std::vector<uint8_t> closed(size_t(n), 0);

    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> open;
    const int start = idx(sx, sy);
    const int goal = idx(gx, gy);
    gScore[size_t(start)] = 0.f;
    open.push(HeapNode{heuristic(grid.topology, sx, sy, gx, gy), start});

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
        grid.forEachWalkableNeighbor(cx, cy, [&](int nx, int ny, float edgeCost) {
            const int ni = idx(nx, ny);
            if (closed[size_t(ni)]) return;
            const float tentative = gScore[size_t(cur.idx)] + edgeCost;
            if (tentative >= gScore[size_t(ni)]) return;
            gScore[size_t(ni)] = tentative;
            parent[size_t(ni)] = cur.idx;
            open.push(HeapNode{tentative + heuristic(grid.topology, nx, ny, gx, gy), ni});
        });
    }

    if (!found) return path;

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

FlowField *Pathfinder::buildFlowField(int gx, int gy) {
    Grid &grid = impl_->grid;
    if (!ensureSynced(grid)) {
        auto *empty = new FlowField();
        empty->resize(0, 0);
        empty->setGoal(gx, gy);
        return empty;
    }

    if (impl_->hasCachedField && !grid.dirty && impl_->cachedGoalX == gx && impl_->cachedGoalY == gy &&
        impl_->cachedField.getWidth() == grid.width &&
        impl_->cachedField.getHeight() == grid.height) {
        auto *copy = new FlowField();
        *copy = impl_->cachedField;
        return copy;
    }

    FlowField *field = buildFlowFieldUncached(grid, gx, gy);
    impl_->cachedField = *field;
    impl_->cachedGoalX = gx;
    impl_->cachedGoalY = gy;
    impl_->hasCachedField = true;
    grid.dirty = false;
    return field;
}

Path *Pathfinder::followFlow(FlowField *field, int sx, int sy) {
    auto *path = new Path();
    Grid &grid = impl_->grid;
    if (!field || !ensureSynced(grid)) return path;
    if (!field->isReachable(sx, sy)) return path;

    const int gx = field->getGoalX();
    const int gy = field->getGoalY();
    const int maxSteps = grid.width * grid.height + 2;
    int x = sx;
    int y = sy;
    path->add(x, y);
    float total = 0.f;
    for (int step = 0; step < maxSteps; ++step) {
        if (x == gx && y == gy) break;
        const int nx = field->nextX(x, y);
        const int ny = field->nextY(x, y);
        if (nx == x && ny == y) break;
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
    Grid &grid = impl_->grid;
    if (!ensureSynced(grid)) return new Path();
    if (!impl_->hasCachedField || grid.dirty || impl_->cachedGoalX != gx || impl_->cachedGoalY != gy ||
        impl_->cachedField.getWidth() != grid.width ||
        impl_->cachedField.getHeight() != grid.height) {
        FlowField *tmp = buildFlowFieldUncached(grid, gx, gy);
        impl_->cachedField = *tmp;
        delete tmp;
        impl_->cachedGoalX = gx;
        impl_->cachedGoalY = gy;
        impl_->hasCachedField = true;
        grid.dirty = false;
    }
    return followFlow(&impl_->cachedField, sx, sy);
}

// --- Path / FlowField (same TU; keep export surface small on Windows) ---

void Path::clear() {
    cells_.clear();
    totalCost_ = 0.f;
}

void Path::add(int x, int y) { cells_.push_back(Cell{x, y}); }

void Path::reverse() {
    for (size_t i = 0, j = cells_.size(); i + 1 < j; ++i, --j) std::swap(cells_[i], cells_[j - 1]);
}

int Path::getLength() const { return int(cells_.size()); }

int Path::getX(int index) const {
    if (index < 0 || index >= int(cells_.size())) return 0;
    return cells_[size_t(index)].x;
}

int Path::getY(int index) const {
    if (index < 0 || index >= int(cells_.size())) return 0;
    return cells_[size_t(index)].y;
}

float Path::getTotalCost() const { return totalCost_; }

void Path::setTotalCost(float cost) { totalCost_ = cost; }

void FlowField::clear() {
    width_ = height_ = 0;
    goalX_ = goalY_ = 0;
    cost_.clear();
    nextX_.clear();
    nextY_.clear();
}

void FlowField::resize(int width, int height) {
    width_ = width > 0 ? width : 0;
    height_ = height > 0 ? height : 0;
    const int n = width_ * height_;
    cost_.assign(size_t(n), kUnreachable);
    nextX_.assign(size_t(n), 0);
    nextY_.assign(size_t(n), 0);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const int i = index(x, y);
            nextX_[size_t(i)] = x;
            nextY_[size_t(i)] = y;
        }
    }
}

void FlowField::setGoal(int x, int y) {
    goalX_ = x;
    goalY_ = y;
}

bool FlowField::inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

float FlowField::costAt(int x, int y) const {
    if (!inBounds(x, y)) return kUnreachable;
    return cost_[size_t(index(x, y))];
}

void FlowField::setCost(int x, int y, float cost) {
    if (!inBounds(x, y)) return;
    cost_[size_t(index(x, y))] = cost;
}

int FlowField::nextX(int x, int y) const {
    if (!inBounds(x, y)) return x;
    return nextX_[size_t(index(x, y))];
}

int FlowField::nextY(int x, int y) const {
    if (!inBounds(x, y)) return y;
    return nextY_[size_t(index(x, y))];
}

void FlowField::setNext(int x, int y, int nx, int ny) {
    if (!inBounds(x, y)) return;
    const int i = index(x, y);
    nextX_[size_t(i)] = nx;
    nextY_[size_t(i)] = ny;
}

bool FlowField::isReachable(int x, int y) const {
    const float c = costAt(x, y);
    return c < kUnreachable && c >= 0.f;
}

}  // namespace eve::map
