#pragma once

/** @file TacticsPath.h @brief Deterministic fixed-cost tactics path queries. */

#include "tactics/TacticsTypes.h"

#include <map>
#include <vector>

namespace eve::tactics {

/** @brief Deterministic logical distance used by cellsInRange. */
enum class CellRangeMetric : std::uint8_t { Manhattan, Chebyshev, Hex };

/** @brief One reachable cell and its minimum fixed-point movement cost. */
struct ReachableCell {
    Cell cell;
    int  cost = 0;
};

/**
 * @brief Owning result of one deterministic reachability query.
 *
 * Results are ordered by Cell coordinates. They remain valid independently of
 * subsequent board mutations, but represent only the board revision observed
 * during the query and must be recomputed before a state-changing commit.
 */
class Reachability {
public:
    /** @brief Return reachable cells in deterministic coordinate order. */
    [[nodiscard]] const std::vector<ReachableCell>& cells() const noexcept { return cells_; }
    /** @brief Return whether a cell is reachable within the queried budget. */
    [[nodiscard]] bool contains(Cell cell) const noexcept;
    /** @brief Return the minimum cost for a reachable cell, or NotFound. */
    [[nodiscard]] Result<int> cost(Cell cell) const;
    /** @brief Reconstruct an origin-to-target path, or NotFound. */
    [[nodiscard]] Result<std::vector<Cell>> pathTo(Cell target) const;

private:
    friend class PathQuery;
    Cell                 origin_;
    std::vector<ReachableCell> cells_;
    std::map<Cell, Cell> predecessor_;
};

/**
 * @brief Pure deterministic board path algorithms.
 *
 * Queries are thread-safe when no caller mutates the borrowed BoardState.
 * They retain no references and invoke no callbacks. Equal-cost frontier ties
 * are ordered by cost then Cell coordinates, yielding bit-exact results.
 */
class PathQuery final {
public:
    /**
     * @brief Compute all unoccupied reachable cells for a placed subject.
     * @param board Borrowed immutable board valid for the synchronous call.
     * @param subject Placed moving subject; its origin remains traversable.
     * @param budget Non-negative fixed-point movement budget.
     * @return Owning reachability snapshot or a structured failure.
     */
    [[nodiscard]] static Result<Reachability> reachable(const BoardState& board, SubjectRef subject, int budget);

    /**
     * @brief Compute a minimum-cost origin-to-target path within budget.
     * @return Owning path including origin and target, or a structured failure.
     */
    [[nodiscard]] static Result<std::vector<Cell>> path(const BoardState& board, SubjectRef subject, Cell target,
                                                        int budget);

    /**
     * @brief Enumerate existing cells whose logical distance is within an inclusive range.
     * @param board Borrowed immutable board valid for the synchronous call.
     * @param origin Logical range origin; it need not exist on the board.
     * @param minimum Inclusive non-negative minimum distance.
     * @param maximum Inclusive maximum distance, not smaller than minimum.
     * @param metric Manhattan, Chebyshev, or axial-hex distance.
     * @return Cells in deterministic coordinate order.
     */
    [[nodiscard]] static Result<std::vector<Cell>> cellsInRange(const BoardState& board, Cell origin, int minimum,
                                                                int maximum, CellRangeMetric metric);
};

}  // namespace eve::tactics
