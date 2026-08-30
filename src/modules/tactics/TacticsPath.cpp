#include "tactics/TacticsPath.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
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
    if (!subject.isValid())
        return failure<Reachability>(DiagnosticCode::InvalidArgument,
                                     "tactics reachability requires a valid subject", "subject");
    if (budget < 0)
        return failure<Reachability>(DiagnosticCode::InvalidArgument,
                                     "tactics reachability budget must be non-negative", "budget");
    const auto origin = board.position(subject);
    if (!origin)
        return failure<Reachability>(DiagnosticCode::NotFound, "tactics reachability subject is not placed",
                                     "subject");

    std::map<Cell, int> best;
    std::map<Cell, Cell> predecessor;
    std::priority_queue<FrontierNode, std::vector<FrontierNode>, FrontierLater> frontier;
    best.emplace(*origin, 0);
    frontier.push({0, *origin});

    while (!frontier.empty()) {
        const FrontierNode current = frontier.top();
        frontier.pop();
        const auto known = best.find(current.cell);
        if (known == best.end() || known->second != current.cost) continue;
        for (const Cell next : board.neighbours(current.cell)) {
            const auto occupied = board.occupant(next);
            if (occupied && *occupied != subject) continue;
            auto state = board.cell(next);
            if (!state) return Result<Reachability>::failure(state.status());
            const CellState cellState = std::move(state).takeValue();
            if (!cellState.passable || current.cost > std::numeric_limits<int>::max() - cellState.moveCost) continue;
            const int candidate = current.cost + cellState.moveCost;
            if (candidate > budget) continue;
            const auto old = best.find(next);
            if (old != best.end() && candidate >= old->second) continue;
            best[next]        = candidate;
            predecessor[next] = current.cell;
            frontier.push({candidate, next});
        }
    }

    Reachability result;
    result.origin_      = *origin;
    result.predecessor_ = std::move(predecessor);
    result.cells_.reserve(best.size());
    for (const auto& [cellValue, cost] : best) result.cells_.push_back({cellValue, cost});
    return Result<Reachability>::success(std::move(result));
}

Result<std::vector<Cell>> PathQuery::path(const BoardState& board, SubjectRef subject, Cell target, int budget) {
    auto result = reachable(board, subject, budget);
    if (!result) return Result<std::vector<Cell>>::failure(result.status());
    return result.value().pathTo(target);
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
