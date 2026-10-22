#pragma once
#include "common/Export.h"


#include "map/FlowField.h"
#include "map/Path.h"
#include "map/TileLayer.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include "common/Result.h"

namespace eve::map {
/** @brief Logical adjacency of a captured map navigation grid. */
enum class NavigationTopology { Ortho4, Ortho8, Hex };
/** @brief Cheap invalidation hint, distinct from immutable capture content revisions.
 * Includes bound-layer tile edits; equal hints allow skipping capture on the map owner thread.
 */
struct NavigationChangeStamp {
    std::uint64_t configuration = 0, layer = 0;
    friend bool   operator==(const NavigationChangeStamp &, const NavigationChangeStamp &) = default;
};
/** @brief Immutable owning capture; independent of Pathfinder and TileLayer lifetimes. */
class EVENGINE_API_WORLD NavigationGridSnapshot {
    friend class Pathfinder;
    struct Data;
    std::shared_ptr<const Data> data_;

public:
    /** @brief Construct an absent snapshot. */
    NavigationGridSnapshot() = default;
    /** @brief Whether this value contains captured data. */
    [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(data_); }
    /** @brief Read column count. */
    [[nodiscard]] int width() const noexcept;
    /** @brief Read row count. */
    [[nodiscard]] int height() const noexcept;
    /** @brief Read content revision, advanced only on changed captured content. */
    [[nodiscard]] std::uint64_t revision() const noexcept;
    /** @brief Read captured adjacency. */
    [[nodiscard]] NavigationTopology topology() const noexcept;
    /** @brief Read hex stagger axis (normalized to Y for orthogonal captures). */
    [[nodiscard]] bool isStaggerAxisY() const noexcept;
    /** @brief Read hex stagger parity (normalized to odd for orthogonal captures). */
    [[nodiscard]] bool isStaggerOdd() const noexcept;
    /** @brief Borrow costs while at least one snapshot retains this capture; reassignment releases it. */
    [[nodiscard]] std::span<const float> costs() const noexcept;
    /** @brief Borrow directional enter masks with the same snapshot lifetime. */
    [[nodiscard]] std::span<const std::uint8_t> enterMasks() const noexcept;
    /** @brief Borrow directional exit masks with the same snapshot lifetime. */
    [[nodiscard]] std::span<const std::uint8_t> exitMasks() const noexcept;
};

/**
 * @brief Pathfinding facade.
 * Single-agent: A*. Group (same goal): Flow Field + follow.
 * Grid/topology internals stay out of the public ABI (Windows export limit).
 */
class EVENGINE_API_WORLD Pathfinder {
public:
    /** @brief Pathfinder. */
    Pathfinder();
    /** @brief Pathfinder. */
    explicit Pathfinder(TileLayer *layer);
    /** @brief Pathfinder. */
    explicit Pathfinder(int width, int height);
    /** @brief Pathfinder. */
    ~Pathfinder();
    /** @brief Capture canonical grid inputs into immutable owned storage, without mutating the live grid.
     * @return Opaque snapshot; unchanged content shares the prior immutable capture and revision.
     * @ownership Retains no provider/layer pointers; a bound layer must remain alive during this call.
     * @thread Map owner thread only. @reentrancy No callbacks.
     * @cost Copies and compares grid-sized inputs; amortize at explicit navigation projection boundaries.
     */
    [[nodiscard]] Result<NavigationGridSnapshot> captureNavigationGrid();
    /** @brief Read an O(1) invalidation hint without copying/synchronizing the grid.
     * @ownership Borrows the already attached layer during this call; it must remain alive.
     * @thread Map owner thread only. @reentrancy No callbacks.
     */
    [[nodiscard]] NavigationChangeStamp getNavigationChangeStamp() const noexcept;

    /** @brief Pathfinder. */
    Pathfinder(Pathfinder &&) noexcept;
    /** @brief Operator =. */
    Pathfinder &operator=(Pathfinder &&) noexcept;
    Pathfinder(const Pathfinder &) = delete;
    Pathfinder &operator=(const Pathfinder &) = delete;

    /** @brief Binds layer. */
    void bindLayer(TileLayer *layer);
    /** @brief Sets the size. */
    void setSize(int width, int height);

    // --- Grid config (script-friendly) ---
    /** @brief Sets the topology. */
    void setTopology(const std::string &name);
    /** @brief Returns the topology. */
    std::string getTopology() const;
    /** @brief Sets the diagonal. */
    void setDiagonal(bool enable);
    /** @brief Returns the diagonal. */
    bool getDiagonal() const;
    /** @brief Block gid. */
    void blockGid(int gid);
    /** @brief Unblock gid. */
    void unblockGid(int gid);
    /** @brief Clears blocked gids. */
    void clearBlockedGids();
    /** @brief Sets the block empty. */
    void setBlockEmpty(bool enable);
    /** @brief Returns the block empty. */
    bool getBlockEmpty() const;
    /** @brief Sets the blocked. */
    void setBlocked(int x, int y, bool blocked);
    /** @brief True when walkable. */
    bool isWalkable(int x, int y) const;
    /** @brief Sets the cell cost. */
    void setCellCost(int x, int y, float cost);
    /** @brief Returns the cell cost. */
    float getCellCost(int x, int y) const;
    /** @brief Synchronizes from layer. */
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
                   /** @brief Float. */
                   const std::function<float(int, int)> &entryPenalty);

    /** @brief Build / reuse cached flow field toward goal. Owned by caller. */
    FlowField *buildFlowField(int gx, int gy);

    /** Trace path along a flow field from start. Owned Path*. */
    /** @brief Follow flow. */
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
