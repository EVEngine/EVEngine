/**
 * @file HexVisibility.cpp
 * @brief Reference-counted cell visibility and the explored latch it drives.
 */

#include "hexmap/HexVisibility.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace eve::hexmap {
namespace {

[[nodiscard]] Result<void> visibilityInvalidArgument(std::string message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "hexmap"));
}

/**
 * @brief Checks the two arguments `increase`/`decrease` share.
 * @return Success, or the InvalidArgument failure both entry points raise.
 */
[[nodiscard]] Result<void> validateView(const HexMap& map, const HexVisibility& visibility, HexCoordinates from) {
    if (!map.contains(from)) return visibilityInvalidArgument("view origin is outside the hex map");
    if (visibility.cellCount() != map.cellCount())
        return visibilityInvalidArgument("visibility counters must be reset to map.cellCount() before a view update");
    return Result<void>::success();
}

}  // namespace

// --- state ------------------------------------------------------------------

void HexVisibility::reset(std::int32_t cellCount) {
    counts_.assign(cellCount > 0 ? static_cast<std::size_t>(cellCount) : 0u, 0);
    touched_.clear();
}

bool HexVisibility::isVisible(std::int32_t cellIndex) const noexcept {
    if (cellIndex < 0 || cellIndex >= cellCount()) return false;
    return counts_[static_cast<std::size_t>(cellIndex)] > 0;
}

std::int32_t HexVisibility::visibility(std::int32_t cellIndex) const noexcept {
    if (cellIndex < 0 || cellIndex >= cellCount()) return 0;
    return counts_[static_cast<std::size_t>(cellIndex)];
}

std::int32_t HexVisibility::visibleCellCount() const noexcept { return static_cast<std::int32_t>(touched_.size()); }

// --- viewers ----------------------------------------------------------------

Result<void> HexVisibility::increase(HexMap& map, HexSearchContext& scratch, HexCoordinates from,
                                     std::int32_t range) {
    Result<void> valid = validateView(map, *this, from);
    if (!valid.ok()) return valid;

    std::vector<std::int32_t> cells;
    Result<void> collected = collectVisibleCells(map, scratch, from, range, cells);
    if (!collected.ok()) return collected;

    for (const std::int32_t index : cells) {
        // `collectVisibleCells` only reports indices of this map, so the counter is in range.
        if (++counts_[static_cast<std::size_t>(index)] != 1) continue;
        // First viewer of this cell: latch it explored and remember the counter so
        // `clear` can zero the map without a full scan.
        touched_.push_back(index);
        map.setExplored(map.coordinatesAt(index), true)
            .ignore("the cell index comes from this map, so the explored latch cannot be rejected");
        // The fog overlay of this cell changed shade, so its chunk must be rebuilt.
        map.markChunkDirtyAndNeighbors(map.chunkIndexOf(map.coordinatesAt(index)));
    }
    return Result<void>::success();
}

Result<void> HexVisibility::decrease(HexMap& map, HexSearchContext& scratch, HexCoordinates from,
                                     std::int32_t range) {
    Result<void> valid = validateView(map, *this, from);
    if (!valid.ok()) return valid;

    std::vector<std::int32_t> cells;
    Result<void> collected = collectVisibleCells(map, scratch, from, range, cells);
    if (!collected.ok()) return collected;

    for (const std::int32_t index : cells) {
        std::int32_t& counter = counts_[static_cast<std::size_t>(index)];
        if (counter == 0) continue;
        if (--counter != 0) continue;
        // The cell lost its last viewer. `touched_` order is unspecified, so a
        // swap-and-pop removal is fine.
        const auto entry = std::find(touched_.begin(), touched_.end(), index);
        if (entry != touched_.end()) {
            *entry = touched_.back();
            touched_.pop_back();
        }
        // The cell became invisible again: its fog overlay shade changed.
        map.markChunkDirtyAndNeighbors(map.chunkIndexOf(map.coordinatesAt(index)));
    }
    return Result<void>::success();
}

void HexVisibility::clear(HexMap& map) noexcept {
    // Every cell that loses its last viewer changes the fog overlay's shade, so each
    // touched cell's chunk has to be rebuilt. `increase`/`decrease` already do this;
    // leaving it out here meant dropping the viewers left stale fog geometry on screen
    // until an unrelated edit happened to dirty the same chunk.
    for (const std::int32_t index : touched_) {
        counts_[static_cast<std::size_t>(index)] = 0;
        map.markChunkDirtyAndNeighbors(map.chunkIndexOf(map.coordinatesAt(index)));
    }
    // The map's explored flags are a one-way latch and stay untouched.
    touched_.clear();
}

}  // namespace eve::hexmap
