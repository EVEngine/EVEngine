/**
 * @file HexSearch.cpp
 * @brief Cell-graph search: the bucket-list frontier, pathfinding and visibility.
 */

#include "hexmap/HexSearch.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <string>

namespace eve::hexmap {
namespace {

[[nodiscard]] Result<HexPath> pathInvalidArgument(const std::string& message) {
    return Result<HexPath>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "hexmap"));
}

[[nodiscard]] Result<void> voidInvalidArgument(const std::string& message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "hexmap"));
}

[[nodiscard]] Result<HexPath> unreachable(const std::string& message) {
    return Result<HexPath>::failure(Diagnostic::error(DiagnosticCode::NotFound, message, "hexmap"));
}

/**
 * @brief Turn index a cell reached at `distance` is entered on.
 *
 * Mirrors `(distance - 1) / speed` of the reference algorithm, clamped so the
 * origin (distance 0) reports turn 0 instead of a negative value.
 */
[[nodiscard]] std::int32_t turnOf(std::int32_t distance, std::int32_t speed) noexcept {
    if (distance <= 0) return 0;
    return (distance - 1) / speed;
}

}  // namespace

// --- HexSearchContext -------------------------------------------------------

void HexSearchContext::resize(std::int32_t cellCount) {
    if (cellCount <= 0) {
        data_.clear();
    } else {
        data_.assign(static_cast<std::size_t>(cellCount), HexSearchData{});
    }
    buckets_.clear();
    minimum_ = 0;
}

HexSearchData& HexSearchContext::data(std::int32_t cellIndex) noexcept {
    if (cellIndex < 0 || cellIndex >= cellCount()) return sink_;
    return data_[static_cast<std::size_t>(cellIndex)];
}

const HexSearchData& HexSearchContext::data(std::int32_t cellIndex) const noexcept {
    if (cellIndex < 0 || cellIndex >= cellCount()) return sink_;
    return data_[static_cast<std::size_t>(cellIndex)];
}

std::int32_t HexSearchContext::beginPhase() noexcept {
    phase_ += 2;
    // Clearing the frontier is only this: the bucket array holds no owned state,
    // and the per-cell records are invalidated lazily by the phase check, so a
    // new search never pays for resetting the grid.
    buckets_.clear();
    minimum_ = 0;
    return phase_;
}

void HexSearchContext::enqueue(std::int32_t cellIndex) noexcept {
    if (data_.empty() || cellIndex < 0 || cellIndex >= cellCount()) return;
    std::int32_t priority = data_[cellIndex].priority();
    if (priority < 0) priority = 0;
    if (priority < minimum_) minimum_ = priority;
    if (static_cast<std::size_t>(priority) >= buckets_.size())
        buckets_.resize(static_cast<std::size_t>(priority) + 1, -1);
    // Link the cell at the head of its bucket: equal-priority cells form an
    // intrusive singly linked list through `nextWithSamePriority`.
    data_[cellIndex].nextWithSamePriority = buckets_[static_cast<std::size_t>(priority)];
    buckets_[static_cast<std::size_t>(priority)] = cellIndex;
}

HexSearchPop HexSearchContext::dequeue(std::int32_t& outCellIndex) noexcept {
    const std::int32_t bucketCount = static_cast<std::int32_t>(buckets_.size());
    for (; minimum_ < bucketCount; ++minimum_) {
        const std::int32_t head = buckets_[static_cast<std::size_t>(minimum_)];
        if (head < 0) continue;
        buckets_[static_cast<std::size_t>(minimum_)] = data(head).nextWithSamePriority;
        outCellIndex = head;
        return HexSearchPop::Cell;
    }
    return HexSearchPop::Empty;
}

void HexSearchContext::unlink(std::int32_t cellIndex, std::int32_t bucket) noexcept {
    if (bucket < 0 || static_cast<std::size_t>(bucket) >= buckets_.size()) return;
    std::int32_t current = buckets_[static_cast<std::size_t>(bucket)];
    if (current < 0) return;
    if (current == cellIndex) {
        buckets_[static_cast<std::size_t>(bucket)] = data(current).nextWithSamePriority;
        return;
    }
    while (true) {
        const std::int32_t next = data(current).nextWithSamePriority;
        if (next < 0) return;
        if (next == cellIndex) {
            data(current).nextWithSamePriority = data(next).nextWithSamePriority;
            return;
        }
        current = next;
    }
}

void HexSearchContext::change(std::int32_t cellIndex, std::int32_t oldPriority) noexcept {
    unlink(cellIndex, oldPriority);
    enqueue(cellIndex);
}

// --- movement rules ---------------------------------------------------------

bool isValidDestination(const HexMap& map, HexCoordinates coordinates, const HexOccupancyQuery& occupied) {
    if (!map.contains(coordinates)) return false;
    // `canHoldUnit` is the shared "explored, explorable, above water" terrain rule;
    // occupancy by another actor is the caller's extra clause.
    const HexCellData* cell = map.cell(coordinates);
    if (cell == nullptr || !canHoldUnit(*cell)) return false;
    if (occupied && occupied(coordinates)) return false;
    return true;
}

std::int32_t moveCost(const HexMap& map, HexCoordinates from, HexCoordinates to, HexDirection direction,
                      const HexOccupancyQuery& occupied) {
    if (!isValidDestination(map, to, occupied)) return -1;

    const HexEdgeType type = edgeType(map.elevation(from), map.elevation(to));
    if (type == HexEdgeType::Cliff) return -1;

    if (map.flags(from).hasRoad(direction)) return 1;
    if (map.isWalled(from) != map.isWalled(to)) return -1;

    const std::int32_t base = type == HexEdgeType::Flat ? 5 : 10;
    return base + map.urbanLevel(to) + map.farmLevel(to) + map.plantLevel(to);
}

// --- pathfinding ------------------------------------------------------------

Result<HexPath> findPath(const HexMap& map, HexSearchContext& scratch, HexCoordinates from, HexCoordinates to,
                         const HexMoveRules& rules, const HexOccupancyQuery& occupied) {
    if (map.empty()) return pathInvalidArgument("cannot search an empty hex map");
    if (!map.contains(from)) return pathInvalidArgument("path origin is outside the hex map");
    if (!map.contains(to)) return pathInvalidArgument("path goal is outside the hex map");
    if (scratch.cellCount() != map.cellCount())
        return pathInvalidArgument("search scratch must be resized to map.cellCount() before findPath");
    if (!isValidDestination(map, to, occupied)) return unreachable("path goal is not a valid destination");

    const std::int32_t speed       = std::max<std::int32_t>(1, rules.speed);
    const std::int32_t start       = map.indexOf(from);
    const std::int32_t goal        = map.indexOf(to);
    const HexCoordinates goalCoord = to;
    const std::int32_t phase       = scratch.beginPhase();

    // The origin has to be written completely, not just tagged with the new phase.
    // `beginPhase` only bumps the phase and clears the buckets, so a reused context
    // (the module owns exactly one `scratch_`) still holds the previous search's
    // `distance`, `heuristic` and `pathFrom` here. `enqueue` derives the bucket from
    // `priority()`, and the first relaxation reads `data(start).distance`, so a stale
    // origin shifted every turn number and could pick a different route. The start
    // cell is never relaxed as a neighbour, so nothing else can fix it up.
    HexSearchData& startData = scratch.data(start);
    startData.searchPhase    = phase;
    startData.distance       = 0;
    startData.pathFrom       = -1;
    startData.heuristic      = from.distanceTo(goalCoord);
    scratch.enqueue(start);

    bool found = false;
    while (true) {
        std::int32_t current = -1;
        if (scratch.dequeue(current) != HexSearchPop::Cell) break;

        const std::int32_t currentDistance = scratch.data(current).distance;
        // Marking the record with `phase + 1` is how the reference says "expanded":
        // an untouched record still carries the older phase, so the distinction
        // needs no clearing pass.
        scratch.data(current).searchPhase = phase + 1;

        if (current == goal) {
            found = true;
            break;
        }

        const std::int32_t currentTurn = turnOf(currentDistance, speed);
        const HexCoordinates currentCoord = map.coordinatesAt(current);

        for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
            const HexDirection direction = static_cast<HexDirection>(i);
            HexCoordinates     neighbour{};
            if (!map.getNeighbor(currentCoord, direction, neighbour)) continue;

            const std::int32_t neighbourIndex = map.indexOf(neighbour);
            if (neighbourIndex < 0) continue;
            // Already expanded: its bucket entry was consumed and it must not be
            // relaxed again within this phase.
            if (scratch.data(neighbourIndex).searchPhase == phase + 1) continue;

            const std::int32_t step = moveCost(map, currentCoord, neighbour, direction, occupied);
            if (step < 0) continue;

            std::int32_t distance = currentDistance + step;
            const std::int32_t turn = turnOf(distance, speed);
            if (turn > currentTurn) distance = turn * speed + step;

            HexSearchData& neighbourData = scratch.data(neighbourIndex);
            if (neighbourData.searchPhase != phase) {
                // A fresh record is fully overwritten; it carries no bucket link yet.
                neighbourData.searchPhase = phase;
                neighbourData.distance    = distance;
                neighbourData.pathFrom    = current;
                neighbourData.heuristic   = neighbour.distanceTo(goalCoord);
                scratch.enqueue(neighbourIndex);
            } else if (distance < neighbourData.distance) {
                // The priority must be read before the record is updated: `change`
                // unlinks the cell from the bucket it is still sitting in.
                const std::int32_t oldPriority = neighbourData.priority();
                neighbourData.distance         = distance;
                neighbourData.pathFrom         = current;
                scratch.change(neighbourIndex, oldPriority);
            }
        }
    }

    if (!found) return unreachable("no path exists between the two cells");

    HexPath path;
    for (std::int32_t index = goal; index != start; index = scratch.data(index).pathFrom) {
        if (index < 0) return unreachable("path reconstruction lost the chain to the origin");
        const std::int32_t distance = scratch.data(index).distance;
        path.cells.push_back(index);
        path.turns.push_back(turnOf(distance, speed));
    }
    path.cells.push_back(start);
    path.turns.push_back(turnOf(scratch.data(start).distance, speed));
    std::reverse(path.cells.begin(), path.cells.end());
    std::reverse(path.turns.begin(), path.turns.end());
    return Result<HexPath>::success(std::move(path));
}

// --- visibility -------------------------------------------------------------

Result<void> collectVisibleCells(const HexMap& map, HexSearchContext& scratch, HexCoordinates from,
                                 std::int32_t range, std::vector<std::int32_t>& out) {
    out.clear();
    if (map.empty()) return voidInvalidArgument("cannot collect visibility from an empty hex map");
    if (!map.contains(from)) return voidInvalidArgument("view origin is outside the hex map");
    if (scratch.cellCount() != map.cellCount())
        return voidInvalidArgument("search scratch must be resized to map.cellCount() before collectVisibleCells");

    const std::int32_t searchRange = range + map.values(from).viewElevation();
    const std::int32_t start       = map.indexOf(from);
    const std::int32_t phase       = scratch.beginPhase();

    scratch.data(start).searchPhase = phase;
    scratch.data(start).distance    = 0;
    scratch.enqueue(start);

    while (true) {
        std::int32_t current = -1;
        if (scratch.dequeue(current) != HexSearchPop::Cell) break;

        // Nearest first: the frontier always pops the cheapest step count, so the
        // produced order is stable for a given map.
        scratch.data(current).searchPhase = phase + 1;
        out.push_back(current);

        const std::int32_t   currentDistance = scratch.data(current).distance;
        const HexCoordinates currentCoord    = map.coordinatesAt(current);

        for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
            const HexDirection direction = static_cast<HexDirection>(i);
            HexCoordinates     neighbour{};
            if (!map.getNeighbor(currentCoord, direction, neighbour)) continue;
            if (!map.isExplorable(neighbour)) continue;

            const std::int32_t neighbourIndex = map.indexOf(neighbour);
            if (neighbourIndex < 0) continue;
            if (scratch.data(neighbourIndex).searchPhase == phase + 1) continue;

            const std::int32_t distance = currentDistance + 1;
            if (distance + map.values(neighbour).viewElevation() > searchRange) continue;
            if (distance > from.distanceTo(neighbour)) continue;

            HexSearchData& neighbourData = scratch.data(neighbourIndex);
            if (neighbourData.searchPhase != phase) {
                neighbourData.searchPhase = phase;
                neighbourData.distance    = distance;
                neighbourData.pathFrom    = current;
                neighbourData.heuristic   = 0;
                scratch.enqueue(neighbourIndex);
            } else if (distance < neighbourData.distance) {
                const std::int32_t oldPriority = neighbourData.priority();
                neighbourData.distance         = distance;
                neighbourData.pathFrom         = current;
                scratch.change(neighbourIndex, oldPriority);
            }
        }
    }
    return Result<void>::success();
}

}  // namespace eve::hexmap
