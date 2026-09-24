#pragma once
#include "common/Export.h"


/** @file HexSearch.h @brief Cell-graph search used by pathfinding and visibility. */

#include "common/Result.h"
#include "hexmap/HexMap.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace eve::hexmap {

/**
 * @brief Per-cell scratch record of one search.
 *
 * The fields mirror the reference hex-map project's cell search data: a search
 * is described by a monotonically increasing *phase* instead of a visited
 * clearing pass, so a search never pays for resetting the whole grid.
 */
struct HexSearchData {
    /** @brief Shortest distance from the search origin found so far. */
    std::int32_t distance = 0;
    /** @brief Cell the path entered this cell from, or -1. */
    std::int32_t pathFrom = -1;
    /** @brief Remaining-distance estimate towards the search goal (0 for blind searches). */
    std::int32_t heuristic = 0;
    /** @brief Phase this record was written in; a record from an older phase is unvisited. */
    std::int32_t searchPhase = 0;
    /** @brief Next cell index sharing this record's priority bucket, or -1. */
    std::int32_t nextWithSamePriority = -1;

    /** @brief Ordering key of the priority queue (`distance + heuristic`). */
    [[nodiscard]] std::int32_t priority() const noexcept { return distance + heuristic; }
};

/**
 * @brief Outcome of one frontier pop.
 *
 * A named status rather than a bare `bool`, so a caller cannot read a
 * meaningless cell index out of an empty queue by accident.
 */
enum class HexSearchPop : std::int32_t {
    /** @brief A cell was removed; the out-parameter holds it. */
    Cell = 0,
    /** @brief The frontier is empty; the out-parameter is untouched. */
    Empty = 1,
};

/**
 * @brief Reusable search scratch: one record per cell plus a priority bucket queue.
 *
 * The queue is the reference project's bucket-list frontier: an array indexed by
 * priority, where equal-priority cells form an intrusive singly linked list
 * through `HexSearchData::nextWithSamePriority`. Priorities are small bounded
 * integers - move costs and step counts - so a bucket list beats a binary heap
 * here and needs no comparison function.
 *
 * Ownership and lifetime: owns plain host memory sized to `cellCount`. It is a
 * scratch buffer, not a result: callers may reuse one instance across searches
 * and across maps of the same size.
 *
 * Thread affinity: no thread safety; a context must not be shared between
 * concurrent searches.
 */
class EVENGINE_API_WORLD HexSearchContext {
public:
    HexSearchContext() = default;

    /**
     * @brief Sizes the scratch for `cellCount` cells, preserving nothing.
     * @param cellCount Number of cells of the map this context searches.
     */
    void resize(std::int32_t cellCount);

    /** @brief Number of cells this context can hold. */
    [[nodiscard]] std::int32_t cellCount() const noexcept { return static_cast<std::int32_t>(data_.size()); }

    /**
     * @brief Mutable record of one cell.
     * @param cellIndex Linear cell index in `[0, cellCount())`.
     * @return The record; out-of-range indices return a shared sink record.
     */
    [[nodiscard]] HexSearchData& data(std::int32_t cellIndex) noexcept;
    /** @brief Const overload of `data`. */
    [[nodiscard]] const HexSearchData& data(std::int32_t cellIndex) const noexcept;

    /**
     * @brief Starts a new search: clears the frontier and advances the phase.
     * @return The new phase; records whose `searchPhase` differs are unvisited.
     * @note Phases advance by two so a cell can be marked "in the frontier" and
     *       "expanded" by two distinct values, matching the reference algorithm.
     */
    [[nodiscard]] std::int32_t beginPhase() noexcept;

    /** @brief Current phase. */
    [[nodiscard]] std::int32_t phase() const noexcept { return phase_; }

    /** @brief Adds a cell at its current priority. */
    void enqueue(std::int32_t cellIndex) noexcept;

    /**
     * @brief Removes the lowest-priority cell.
     * @param outCellIndex Receives the cell index; untouched when the frontier is empty.
     * @return `Cell` when a cell was popped, `Empty` when the frontier ran dry.
     */
    [[nodiscard]] HexSearchPop dequeue(std::int32_t& outCellIndex) noexcept;

    /**
     * @brief Re-queues a cell whose priority changed after it was enqueued.
     * @param cellIndex Cell to move.
     * @param oldPriority Priority the cell was enqueued with.
     */
    void change(std::int32_t cellIndex, std::int32_t oldPriority) noexcept;

private:
    void unlink(std::int32_t cellIndex, std::int32_t bucket) noexcept;

    std::vector<HexSearchData> data_;
    std::vector<std::int32_t>  buckets_;
    std::int32_t               minimum_ = 0;
    std::int32_t               phase_   = 0;
    HexSearchData              sink_{};
};

/** @brief Movement tuning of one actor; cells per turn and vision radius in cells. */
struct HexMoveRules {
    /** @brief Movement points available per turn. */
    std::int32_t speed = 24;
    /** @brief Vision radius in cells, added to the cell's own view elevation. */
    std::int32_t visionRange = 3;
};

/**
 * @brief Predicate answering whether a cell is already occupied by another actor.
 *
 * An empty query means "the grid is empty": the search then only rejects cells
 * that the terrain itself forbids.
 */
using HexOccupancyQuery = std::function<bool(HexCoordinates)>;

/**
 * @brief Whether an actor may occupy a cell.
 *
 * A destination must be explored, explorable, not underwater and not occupied.
 * This mirrors `HexUnit.IsValidDestination` in the reference project; the
 * `occupied` query supplies the "no other unit" clause.
 *
 * @param map Source map.
 * @param coordinates Cell to test.
 * @param occupied Occupancy predicate; may be empty.
 * @return True when the cell can hold an actor.
 */
[[nodiscard]] EVENGINE_API_WORLD bool isValidDestination(const HexMap& map, HexCoordinates coordinates,
                                                         const HexOccupancyQuery& occupied = {});

/**
 * @brief Cost of moving between two adjacent cells.
 *
 * Cliffs and wall boundaries are impassable. A road on the source cell costs a
 * single point; otherwise flat ground costs 5, a slope 10, plus the target
 * cell's urban, farm and plant levels.
 *
 * @param map Source map.
 * @param from Source cell.
 * @param to Destination cell.
 * @param direction Direction of travel from `from` to `to`.
 * @param occupied Occupancy predicate; may be empty.
 * @return A positive cost, or a negative value when the move is blocked.
 */
[[nodiscard]] EVENGINE_API_WORLD std::int32_t moveCost(const HexMap& map, HexCoordinates from, HexCoordinates to,
                                                       HexDirection direction, const HexOccupancyQuery& occupied = {});

/** @brief One found path: the cells from origin to goal, with the turn each one is reached on. */
struct HexPath {
    /** @brief Linear cell indices, `cells.front()` is the origin and `cells.back()` the goal. */
    std::vector<std::int32_t> cells;
    /** @brief Turn index for each entry of `cells`; parallel to `cells`. */
    std::vector<std::int32_t> turns;

    /** @brief Whether the path holds no cell. */
    [[nodiscard]] bool empty() const noexcept { return cells.empty(); }
    /** @brief Number of cells on the path, inclusive of both ends. */
    [[nodiscard]] std::size_t size() const noexcept { return cells.size(); }
};

/**
 * @brief Finds the cheapest path between two cells.
 *
 * The result is a plan, not a commitment: it is computed against the current
 * cell state and is invalidated by any later map mutation, so callers re-run it
 * rather than caching it across edits.
 *
 * @param map Source map.
 * @param scratch Search scratch, resized by the caller to `map.cellCount()`.
 * @param from Origin cell; must be inside the grid.
 * @param to Goal cell; must be inside the grid and a valid destination.
 * @param rules Movement rules of the travelling actor.
 * @param occupied Occupancy predicate of *other* actors; may be empty.
 * @return The path, or NotFound when the goal is unreachable / InvalidArgument
 *         when an endpoint is outside the grid.
 * @cost Proportional to the number of cells expanded; bounded by `map.cellCount()`.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<HexPath> findPath(const HexMap& map, HexSearchContext& scratch,
                                                          HexCoordinates from, HexCoordinates to,
                                                          const HexMoveRules&      rules,
                                                          const HexOccupancyQuery& occupied = {});

/**
 * @brief Collects every cell visible from `from` within `range`.
 *
 * Visibility is line of sight on the hex grid: a cell is visible when the
 * shortest path distance to it, plus its own view elevation, stays within
 * `range + from.viewElevation`, and that distance is not longer than the direct
 * hex distance (which rejects cells hidden behind a ridge).
 *
 * @param map Source map.
 * @param scratch Search scratch, resized to `map.cellCount()`.
 * @param from Viewing cell; must be inside the grid.
 * @param range Vision radius in cells, excluding the viewer's own elevation bonus.
 * @param out Receives the visible cells, nearest first; cleared first.
 * @return Success, or InvalidArgument when `from` is outside the grid.
 * @cost Proportional to the visible region, not to the map.
 */
[[nodiscard]] Result<void> collectVisibleCells(const HexMap& map, HexSearchContext& scratch, HexCoordinates from,
                                               std::int32_t range, std::vector<std::int32_t>& out);

}  // namespace eve::hexmap
