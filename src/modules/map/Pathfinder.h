#pragma once

#include "map/FlowField.h"
#include "map/Path.h"
#include "map/TileLayer.h"

#include <memory>
#include <functional>
#include <string>

namespace eve::map {

/**
 * @brief Pathfinding facade.
 * Single-agent: A*. Group (same goal): Flow Field + follow.
 * Grid/topology internals stay out of the public ABI (Windows export limit).
 */
class Pathfinder {
public:
    Pathfinder();
    explicit Pathfinder(TileLayer *layer);
    explicit Pathfinder(int width, int height);
    ~Pathfinder();

    Pathfinder(Pathfinder &&) noexcept;
    Pathfinder &operator=(Pathfinder &&) noexcept;
    Pathfinder(const Pathfinder &) = delete;
    Pathfinder &operator=(const Pathfinder &) = delete;

    void bindLayer(TileLayer *layer);
    void setSize(int width, int height);

    // --- Grid config (script-friendly) ---
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
     * @brief A* from (sx,sy) to (gx,gy). Returns owned Path* (may be empty if unreachable).
     * Never returns nullptr — always a Path object for simpler script null checks via length.
     */
    Path *findPath(int sx, int sy, int gx, int gy);

    /**
     * @brief A* with a caller-owned non-negative dynamic entry penalty.
     * @param sx Start cell x.
     * @param sy Start cell y.
     * @param gx Goal cell x.
     * @param gy Goal cell y.
     * @param entryPenalty Additional cost for entering a walkable cell. Negative
     *        values are clamped to zero; non-finite values reject that cell.
     * @return Caller-owned path, empty when no route satisfies the overlay.
     * @ownership Ownership transfers to the caller, which must release the Path after its final synchronous use.
     * @lifetime The returned Path remains valid until the caller releases it and does not borrow Pathfinder state.
     */
    Path *findPath(int sx, int sy, int gx, int gy,
                   const std::function<float(int, int)> &entryPenalty);

    /** @brief Build / reuse cached flow field toward goal. Owned by caller. */
    FlowField *buildFlowField(int gx, int gy);

    /** Trace path along a flow field from start. Owned Path*. */
    Path *followFlow(FlowField *field, int sx, int sy);

    /**
     * @brief Group helper: ensure field for goal, then follow from start.
     * Equivalent to buildFlowField + followFlow but reuses internal cache when possible.
     */
    Path *findGroupPath(int sx, int sy, int gx, int gy);

    /** @brief Invalidate cached flow field (also called when grid dirties). */
    void invalidateCache();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::map
