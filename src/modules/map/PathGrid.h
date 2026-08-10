#pragma once

#include "map/PathTopology.h"
#include "map/TileLayer.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace eve::map {

/**
 * Navigable grid: walkability + traversal cost, independent of rendering.
 * Optional TileLayer binding refreshes walkability from GIDs.
 */
class PathGrid {
public:
    void resize(int width, int height);
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    void bindLayer(TileLayer *layer);
    TileLayer *getLayer() const { return layer_; }
    void clearLayer();

    /** Refresh walkable/cost from bound layer GIDs. No-op if unbound. */
    void syncFromLayer();

    void setTopology(const std::string &name);
    void setTopologyEnum(PathTopology t);
    std::string getTopology() const;
    PathTopology topology() const { return topology_; }

    /** When topology is auto-derived from layer, prefer 8-dir for ortho/iso. */
    void setDiagonal(bool enable);
    bool getDiagonal() const { return diagonal_; }

    void blockGid(int gid);
    void unblockGid(int gid);
    void clearBlockedGids();
    bool isGidBlocked(int gid) const;

    /** If true (default), GID 0 is not walkable when syncing from a layer. */
    void setBlockEmpty(bool enable);
    bool getBlockEmpty() const { return blockEmpty_; }

    void setBlocked(int x, int y, bool blocked);
    bool isBlocked(int x, int y) const;
    bool isWalkable(int x, int y) const;

    /** Movement multiplier into this cell (default 1). ≤0 marks blocked. */
    void setCellCost(int x, int y, float cost);
    float getCellCost(int x, int y) const;

    bool inBounds(int x, int y) const;

    bool staggerAxisY() const { return staggerAxisY_; }
    bool staggerOdd() const { return staggerOdd_; }

    void markDirty();
    bool isDirty() const { return dirty_; }
    void clearDirty();

    /**
     * Invoke fn(nx,ny,edgeCost) for each walkable neighbor.
     * edgeCost = moveCost * destination cell cost.
     * Ortho8 skips diagonal when either orthogonal side is blocked (no corner cut).
     */
    void forEachWalkableNeighbor(int x, int y, const NeighborFn &fn) const;

private:
    int index(int x, int y) const { return y * width_ + x; }
    void applyAutoTopologyFromLayer();

    int width_ = 0;
    int height_ = 0;
    TileLayer *layer_ = nullptr;
    PathTopology topology_ = PathTopology::Ortho4;
    bool topologyManual_ = false;
    bool diagonal_ = false;
    bool blockEmpty_ = true;
    bool staggerAxisY_ = true;
    bool staggerOdd_ = true;
    bool dirty_ = true;
    std::vector<float> cost_;  // ≤0 => blocked
    std::unordered_set<uint32_t> blockedGids_;
};

}  // namespace eve::map
