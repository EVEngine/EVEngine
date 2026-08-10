#pragma once

#include "map/FlowField.h"
#include "map/Path.h"
#include "map/PathGrid.h"
#include "map/TileLayer.h"

#include <string>

namespace eve::map {

/**
 * Pathfinding facade over a PathGrid.
 * Single-agent: A*. Group (same goal): Flow Field + follow.
 */
class Pathfinder {
public:
    Pathfinder() = default;
    explicit Pathfinder(TileLayer *layer);
    explicit Pathfinder(int width, int height);

    void bindLayer(TileLayer *layer);
    void setSize(int width, int height);

    PathGrid &grid() { return grid_; }
    const PathGrid &grid() const { return grid_; }

    // --- Grid config (script-friendly forwards) ---
    void setTopology(const std::string &name);
    std::string getTopology() const;
    void setDiagonal(bool enable);
    bool getDiagonal() const;
    void blockGid(int gid);
    void unblockGid(int gid);
    void clearBlockedGids();
    void setBlockEmpty(bool enable);
    bool getBlockEmpty() const;
    void setBlocked(int x, int y, bool blocked);
    bool isWalkable(int x, int y) const;
    void setCellCost(int x, int y, float cost);
    float getCellCost(int x, int y) const;
    void syncFromLayer();

    /**
     * A* from (sx,sy) to (gx,gy). Returns owned Path* (may be empty if unreachable).
     * Never returns nullptr — always a Path object for simpler script null checks via length.
     */
    Path *findPath(int sx, int sy, int gx, int gy);

    /** Build / reuse cached flow field toward goal. Owned by caller. */
    FlowField *buildFlowField(int gx, int gy);

    /** Trace path along a flow field from start. Owned Path*. */
    Path *followFlow(FlowField *field, int sx, int sy);

    /**
     * Group helper: ensure field for goal, then follow from start.
     * Equivalent to buildFlowField + followFlow but reuses internal cache when possible.
     */
    Path *findGroupPath(int sx, int sy, int gx, int gy);

    /** Invalidate cached flow field (also called when grid dirties). */
    void invalidateCache();

private:
    bool ensureSynced();
    FlowField *buildFlowFieldUncached(int gx, int gy);

    PathGrid grid_;
    // Cache for group pathfinding
    bool hasCachedField_ = false;
    int cachedGoalX_ = 0;
    int cachedGoalY_ = 0;
    FlowField cachedField_;
};

}  // namespace eve::map
