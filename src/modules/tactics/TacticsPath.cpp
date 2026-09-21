#include "tactics/TacticsPath.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>
#include <queue>
#include <utility>

namespace eve::tactics {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

struct FrontierNode {
    int  cost = 0;
    Cell cell;
};

struct FrontierLater {
    bool operator()(const FrontierNode& left, const FrontierNode& right) const noexcept {
        if (left.cost != right.cost) return left.cost > right.cost;
        return left.cell > right.cell;
    }
};

/** @brief Deterministic expansion result shared by the reachability and path queries. */
struct SearchOutcome {
    std::map<Cell, int>  best;
    std::map<Cell, Cell> predecessor;
};

/**
 * @brief Validate a query subject and return its placed origin cell.
 *
 * Both queries share this so their diagnostics cannot drift apart, and a caller
 * cannot observe a different refusal for the same malformed input.
 */
Result<Cell> resolveQueryOrigin(const BoardState& board, SubjectRef subject, int budget) {
    if (!subject.isValid())
        return failure<Cell>(DiagnosticCode::InvalidArgument,
                             "tactics reachability requires a valid subject", "subject");
    if (budget < 0)
        return failure<Cell>(DiagnosticCode::InvalidArgument,
                             "tactics reachability budget must be non-negative", "budget");
    const auto origin = board.position(subject);
    if (!origin)
        return failure<Cell>(DiagnosticCode::NotFound, "tactics reachability subject is not placed", "subject");
    return Result<Cell>::success(*origin);
}

/**
 * @brief Uniform-cost expansion from a placed origin.
 *
 * The frontier is a binary heap ordered by `(cost, cell)`, which reproduces the
 * documented tie-break exactly: equal-cost cells are expanded in coordinate
 * order, so a caller observes the same predecessor chain on every run.
 *
 * @param stopAt When set, the expansion stops as soon as that cell is finalised.
 *        Because the heap pops in non-decreasing `(cost, cell)` order, a cell's
 *        cost and predecessor are already final at its first pop, so stopping
 *        there yields exactly the values a full expansion would have left in it.
 *        Only valid for queries that need one target, never for the reachable set.
 */
Result<SearchOutcome> expand(const BoardState& board, SubjectRef subject, Cell origin, int budget,
                             const std::optional<Cell>& stopAt) {
    SearchOutcome outcome;
    std::priority_queue<FrontierNode, std::vector<FrontierNode>, FrontierLater> frontier;
    outcome.best.emplace(origin, 0);
    frontier.push({0, origin});

    while (!frontier.empty()) {
        const FrontierNode current = frontier.top();
        frontier.pop();
        const auto known = outcome.best.find(current.cell);
        if (known == outcome.best.end() || known->second != current.cost) continue;
        if (stopAt && current.cell == *stopAt) break;
        for (const Cell next : board.neighbours(current.cell)) {
            const auto occupied = board.occupant(next);
            // The moving subject keeps its own origin traversable.
            if (occupied && *occupied != subject) continue;
            auto state = board.cell(next);
            if (!state) return Result<SearchOutcome>::failure(state.status());
            const CellState cellState = std::move(state).takeValue();
            if (!cellState.passable) continue;
            // A declared directed edge refines this direction only; an undeclared
            // direction keeps the pure cell-cost behaviour.
            const std::optional<EdgeState> directed = board.tryEdge(current.cell, next);
            if (directed && !directed->passable) continue;
            const int extra = directed ? directed->extraCost : 0;
            const long long step = static_cast<long long>(cellState.moveCost) + static_cast<long long>(extra);
            if (step > std::numeric_limits<int>::max()) continue;
            if (current.cost > std::numeric_limits<int>::max() - static_cast<int>(step)) continue;
            const int candidate = current.cost + static_cast<int>(step);
            if (candidate > budget) continue;
            const auto old = outcome.best.find(next);
            if (old != outcome.best.end() && candidate >= old->second) continue;
            outcome.best[next]        = candidate;
            outcome.predecessor[next] = current.cell;
            frontier.push({candidate, next});
        }
    }
    return Result<SearchOutcome>::success(std::move(outcome));
}

}  // namespace

bool Reachability::contains(Cell cellValue) const noexcept {
    const auto found = std::lower_bound(cells_.begin(), cells_.end(), cellValue,
                                        [](const ReachableCell& entry, Cell value) { return entry.cell < value; });
    return found != cells_.end() && found->cell == cellValue;
}

Result<int> Reachability::cost(Cell cellValue) const {
    const auto found = std::lower_bound(cells_.begin(), cells_.end(), cellValue,
                                        [](const ReachableCell& entry, Cell value) { return entry.cell < value; });
    if (found == cells_.end() || found->cell != cellValue)
        return failure<int>(DiagnosticCode::NotFound, "tactics cell is not reachable", "cell");
    return Result<int>::success(found->cost);
}

Result<std::vector<Cell>> Reachability::pathTo(Cell target) const {
    if (!contains(target))
        return failure<std::vector<Cell>>(DiagnosticCode::NotFound, "tactics target is not reachable", "target");
    std::vector<Cell> result{target};
    while (result.back() != origin_) {
        const auto found = predecessor_.find(result.back());
        if (found == predecessor_.end())
            return failure<std::vector<Cell>>(DiagnosticCode::InvariantViolation,
                                              "tactics reachability predecessor chain is incomplete", "path");
        result.push_back(found->second);
    }
    std::reverse(result.begin(), result.end());
    return Result<std::vector<Cell>>::success(std::move(result));
}

Result<Reachability> PathQuery::reachable(const BoardState& board, SubjectRef subject, int budget) {
    auto origin = resolveQueryOrigin(board, subject, budget);
    if (!origin) return Result<Reachability>::failure(origin.status());

    // A reachability query needs every cell, so it never stops early.
    auto outcome = expand(board, subject, origin.value(), budget, std::nullopt);
    if (!outcome) return Result<Reachability>::failure(outcome.status());

    Reachability result;
    result.origin_      = origin.value();
    result.predecessor_ = std::move(outcome.value().predecessor);
    result.cells_.reserve(outcome.value().best.size());
    for (const auto& [cellValue, cost] : outcome.value().best) result.cells_.push_back({cellValue, cost});
    return Result<Reachability>::success(std::move(result));
}

Result<std::vector<Cell>> PathQuery::path(const BoardState& board, SubjectRef subject, Cell target, int budget) {
    auto origin = resolveQueryOrigin(board, subject, budget);
    if (!origin) return Result<std::vector<Cell>>::failure(origin.status());

    // Single-target query: stop expanding once the target is finalised instead of
    // building the whole reachable set.
    auto outcome = expand(board, subject, origin.value(), budget, target);
    if (!outcome) return Result<std::vector<Cell>>::failure(outcome.status());
    if (!outcome.value().best.contains(target))
        return failure<std::vector<Cell>>(DiagnosticCode::NotFound, "tactics target is not reachable", "target");

    std::vector<Cell> result{target};
    while (result.back() != origin.value()) {
        const auto found = outcome.value().predecessor.find(result.back());
        if (found == outcome.value().predecessor.end())
            return failure<std::vector<Cell>>(DiagnosticCode::InvariantViolation,
                                              "tactics reachability predecessor chain is incomplete", "path");
        result.push_back(found->second);
    }
    std::reverse(result.begin(), result.end());
    return Result<std::vector<Cell>>::success(std::move(result));
}

Result<std::vector<Cell>> PathQuery::cellsInRange(const BoardState& board, Cell origin, int minimum, int maximum,
                                                  CellRangeMetric metric) {
    if (minimum < 0 || maximum < minimum)
        return failure<std::vector<Cell>>(DiagnosticCode::InvalidArgument,
                                          "tactics range bounds must be ordered and non-negative", "range");
    std::vector<Cell> result;
    for (const Cell cell : board.cells()) {
        const std::int64_t deltaX = static_cast<std::int64_t>(cell.x) - origin.x;
        const std::int64_t deltaY = static_cast<std::int64_t>(cell.y) - origin.y;
        const std::int64_t dx = std::abs(deltaX);
        const std::int64_t dy = std::abs(deltaY);
        const std::int64_t dz =
            std::abs(static_cast<std::int64_t>(cell.layer) - origin.layer);
        std::int64_t distance = 0;
        switch (metric) {
            case CellRangeMetric::Manhattan: distance = dx + dy + dz; break;
            case CellRangeMetric::Chebyshev: distance = std::max({dx, dy, dz}); break;
            case CellRangeMetric::Hex:
                if (cell.layer != origin.layer) continue;
                distance = std::max({dx, dy, std::abs(deltaX + deltaY)});
                break;
        }
        if (distance >= minimum && distance <= maximum) result.push_back(cell);
    }
    return Result<std::vector<Cell>>::success(std::move(result));
}

}  // namespace eve::tactics
