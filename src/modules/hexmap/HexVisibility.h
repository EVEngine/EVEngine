#pragma once
#include "common/Export.h"


/** @file HexVisibility.h @brief Reference-counted cell visibility and explored state. */

#include "common/Result.h"
#include "hexmap/HexSearch.h"

#include <cstdint>
#include <vector>

namespace eve::hexmap {

/**
 * @brief Fog-of-war bookkeeping: how many viewers currently see each cell.
 *
 * Visibility is a *counter*, not a flag, because several actors overlap: a cell
 * stops being visible only when the last viewer that covered it stops looking.
 * The first time a cell becomes visible it is also marked *explored*, which is a
 * one-way latch: explored cells stay explored after the viewer leaves and only
 * the visibility counter drops.
 *
 * Invariant: `counts_[i] == 0` for every cell that no viewer covers, and
 * `counts_[i] > 0` exactly for the cells `isVisible(i)` reports.
 *
 * Ownership and lifetime: owns one counter per cell; `reset` re-sizes it and
 * `clear` zeroes it. It does not own the map it annotates.
 *
 * Thread affinity: main thread only; no locking.
 */
class EVENGINE_API_WORLD HexVisibility {
public:
    HexVisibility() = default;

    /**
     * @brief Sizes the counter array for `cellCount` cells and zeroes it.
     * @param cellCount Number of cells of the annotated map.
     * @note This does not clear the map's explored flags; callers that rebuild a
     *       map get a fresh map whose flags are already clear.
     */
    void reset(std::int32_t cellCount);

    /** @brief Number of tracked cells. */
    [[nodiscard]] std::int32_t cellCount() const noexcept { return static_cast<std::int32_t>(counts_.size()); }

    /** @brief Whether any viewer currently sees the cell. */
    [[nodiscard]] bool isVisible(std::int32_t cellIndex) const noexcept;
    /** @brief Number of viewers currently covering the cell. */
    [[nodiscard]] std::int32_t visibility(std::int32_t cellIndex) const noexcept;
    /** @brief Number of cells with at least one viewer. */
    [[nodiscard]] std::int32_t visibleCellCount() const noexcept;

    /**
     * @brief Adds one viewer at `from`.
     *
     * @param map Map whose explored flags are latched; must contain `from`.
     * @param scratch Search scratch, resized to `map.cellCount()`.
     * @param from Viewing cell.
     * @param range Vision radius in cells.
     * @return Success, or InvalidArgument when `from` is outside the grid.
     * @cost Proportional to the visible region.
     */
    [[nodiscard]] Result<void> increase(HexMap& map, HexSearchContext& scratch, HexCoordinates from,
                                        std::int32_t range);

    /**
     * @brief Removes one viewer at `from`.
     *
     * Counters are clamped at zero, so a mismatched decrease cannot make a cell
     * permanently invisible by going negative.
     *
     * @param map Map being annotated.
     * @param scratch Search scratch, resized to `map.cellCount()`.
     * @param from Viewing cell.
     * @param range Vision radius in cells.
     * @return Success, or InvalidArgument when `from` is outside the grid.
     * @cost Proportional to the visible region.
     */
    [[nodiscard]] Result<void> decrease(HexMap& map, HexSearchContext& scratch, HexCoordinates from,
                                        std::int32_t range);

    /**
     * @brief Drops every viewer, leaving the map's explored flags untouched.
     *
     * Cells that lose their last viewer change the fog overlay, so their chunks are
     * marked dirty in `map`, exactly as `decrease` does. Leaving that to the caller
     * meant dropping the viewers could leave stale fog geometry on screen until an
     * unrelated edit happened to dirty the same chunk.
     *
     * @param map The annotated map whose fog geometry is dirtied.
     * @cost Proportional to the number of visible cells, not to the whole map.
     */
    void clear(HexMap& map) noexcept;

private:
    std::vector<std::int32_t> counts_;
    /** @brief Linear indices with a non-zero counter, used by `clear` to avoid a full scan. */
    std::vector<std::int32_t> touched_;
};

}  // namespace eve::hexmap
