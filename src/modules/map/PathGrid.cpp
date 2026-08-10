#include "map/PathGrid.h"

#include "map/TileOrientation.h"

namespace eve::map {

void PathGrid::resize(int width, int height) {
    width_ = width > 0 ? width : 0;
    height_ = height > 0 ? height : 0;
    cost_.assign(size_t(width_ * height_), 1.f);
    dirty_ = true;
}

void PathGrid::bindLayer(TileLayer *layer) {
    layer_ = layer;
    if (!layer_) return;
    auto *cfg = layer_->config();
    if (!cfg) return;
    resize(cfg->mapW, cfg->mapH);
    staggerAxisY_ = cfg->staggerAxis == StaggerAxis::Y;
    staggerOdd_ = cfg->staggerIndex == StaggerIndex::Odd;
    if (!topologyManual_) applyAutoTopologyFromLayer();
    syncFromLayer();
}

void PathGrid::clearLayer() { layer_ = nullptr; }

void PathGrid::applyAutoTopologyFromLayer() {
    if (!layer_ || !layer_->config()) return;
    topology_ = topologyFromOrientation(layer_->config()->orientation, diagonal_);
}

void PathGrid::syncFromLayer() {
    if (!layer_) return;
    auto *cfg = layer_->config();
    auto *tiles = layer_->tiles();
    if (!cfg || !tiles) return;
    if (cfg->mapW != width_ || cfg->mapH != height_) resize(cfg->mapW, cfg->mapH);
    staggerAxisY_ = cfg->staggerAxis == StaggerAxis::Y;
    staggerOdd_ = cfg->staggerIndex == StaggerIndex::Odd;
    if (!topologyManual_) applyAutoTopologyFromLayer();

    const int n = width_ * height_;
    for (int i = 0; i < n; ++i) {
        const uint32_t gid = (i < int(tiles->gids.size())) ? tileGid(tiles->gids[size_t(i)]) : 0u;
        bool blocked = false;
        if (blockEmpty_ && gid == 0u) blocked = true;
        if (blockedGids_.count(gid)) blocked = true;
        cost_[size_t(i)] = blocked ? 0.f : 1.f;
    }
    dirty_ = true;
}

void PathGrid::setTopology(const std::string &name) {
    if (name == "auto") {
        topologyManual_ = false;
        if (layer_) applyAutoTopologyFromLayer();
        else topology_ = diagonal_ ? PathTopology::Ortho8 : PathTopology::Ortho4;
    } else {
        topologyManual_ = true;
        topology_ = parsePathTopology(name, topology_);
    }
    dirty_ = true;
}

void PathGrid::setTopologyEnum(PathTopology t) {
    topologyManual_ = true;
    topology_ = t;
    dirty_ = true;
}

std::string PathGrid::getTopology() const { return pathTopologyName(topology_); }

void PathGrid::setDiagonal(bool enable) {
    diagonal_ = enable;
    if (!topologyManual_ && layer_) applyAutoTopologyFromLayer();
    else if (!topologyManual_) topology_ = diagonal_ ? PathTopology::Ortho8 : PathTopology::Ortho4;
    dirty_ = true;
}

void PathGrid::blockGid(int gid) {
    if (gid < 0) return;
    blockedGids_.insert(uint32_t(gid));
    dirty_ = true;
    if (layer_) syncFromLayer();
}

void PathGrid::unblockGid(int gid) {
    if (gid < 0) return;
    blockedGids_.erase(uint32_t(gid));
    dirty_ = true;
    if (layer_) syncFromLayer();
}

void PathGrid::clearBlockedGids() {
    blockedGids_.clear();
    dirty_ = true;
    if (layer_) syncFromLayer();
}

bool PathGrid::isGidBlocked(int gid) const {
    if (gid < 0) return false;
    return blockedGids_.count(uint32_t(gid)) > 0;
}

void PathGrid::setBlockEmpty(bool enable) {
    blockEmpty_ = enable;
    dirty_ = true;
    if (layer_) syncFromLayer();
}

void PathGrid::setBlocked(int x, int y, bool blocked) {
    if (!inBounds(x, y)) return;
    cost_[size_t(index(x, y))] = blocked ? 0.f : 1.f;
    dirty_ = true;
}

bool PathGrid::isBlocked(int x, int y) const { return !isWalkable(x, y); }

bool PathGrid::isWalkable(int x, int y) const {
    if (!inBounds(x, y)) return false;
    return cost_[size_t(index(x, y))] > 0.f;
}

void PathGrid::setCellCost(int x, int y, float cost) {
    if (!inBounds(x, y)) return;
    cost_[size_t(index(x, y))] = cost;
    dirty_ = true;
}

float PathGrid::getCellCost(int x, int y) const {
    if (!inBounds(x, y)) return 0.f;
    return cost_[size_t(index(x, y))];
}

bool PathGrid::inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

void PathGrid::markDirty() { dirty_ = true; }
void PathGrid::clearDirty() { dirty_ = false; }

void PathGrid::forEachWalkableNeighbor(int x, int y, const NeighborFn &fn) const {
    if (!fn || !inBounds(x, y)) return;

    forEachNeighbor(topology_, x, y, staggerAxisY_, staggerOdd_,
                    [&](int nx, int ny, float moveCost) {
                        if (!isWalkable(nx, ny)) return;
                        if (topology_ == PathTopology::Ortho8) {
                            const int dx = nx - x;
                            const int dy = ny - y;
                            if (dx != 0 && dy != 0) {
                                // No corner cutting: both orthogonal sides must be free.
                                if (!isWalkable(x + dx, y) || !isWalkable(x, y + dy)) return;
                            }
                        }
                        const float cellCost = getCellCost(nx, ny);
                        fn(nx, ny, moveCost * cellCost);
                    });
}

}  // namespace eve::map
