/**
 * @file LineOfSight.cpp
 * @brief Built-in grid line-of-sight and cover policies.
 */

#include "tactics/LineOfSight.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace eve::tactics {
namespace {

/** @brief Whether a cell blocks sight: tagged as a blocker, or not on the board at all. */
bool blocksSight(const BoardState& board, Cell cell) {
    const auto state = board.cell(cell);
    if (!state) return true;
    return std::find(state.value().tags.begin(), state.value().tags.end(), kSightBlockerTag) !=
           state.value().tags.end();
}

/** @brief Axial hex coordinate, used for the cube-space hex trace. */
struct Hex {
    int q = 0;
    int r = 0;
};

Hex toHex(Cell cell) { return Hex{cell.x, cell.y}; }

/** @brief Round a fractional cube coordinate back to the nearest hex. */
Cell fromCube(double x, double y, double z) {
    std::int64_t rx = static_cast<std::int64_t>(x < 0 ? x - 0.5 : x + 0.5);
    std::int64_t ry = static_cast<std::int64_t>(y < 0 ? y - 0.5 : y + 0.5);
    std::int64_t rz = static_cast<std::int64_t>(z < 0 ? z - 0.5 : z + 0.5);
    const std::int64_t dx = std::llabs(rx - static_cast<std::int64_t>(x));
    const std::int64_t dy = std::llabs(ry - static_cast<std::int64_t>(y));
    const std::int64_t dz = std::llabs(rz - static_cast<std::int64_t>(z));
    if (dx > dy && dx > dz) {
        rx = -ry - rz;
    } else if (dy > dz) {
        ry = -rx - rz;
    } else {
        rz = -rx - ry;
    }
    return Cell{static_cast<int>(rx), static_cast<int>(ry), 0};
}

/**
 * @brief The cells strictly between two cells, in canonical orientation.
 *
 * @param reverse Set to true when the trace was computed from `to` towards `from`, so the
 *        caller can reverse the result. Computing from the canonical order is what makes
 *        sight symmetric by construction.
 */
std::vector<Cell> traceInterior(const BoardState& board, Cell from, Cell to, bool& reverse) {
    std::vector<Cell> interior;
    if (from.layer != to.layer) return interior;
    const int layer = from.layer;

    Cell start = from;
    Cell end   = to;
    reverse    = (to < from);
    if (reverse) std::swap(start, end);

    if (board.topology() == BoardTopology::HexAxial) {
        // Cube-space interpolation: the standard hex line, deduplicated so a trace that clips
        // two hexes in one step does not repeat them.
        const Hex a = toHex(start);
        const Hex b = toHex(end);
        const int dx = b.q - a.q;
        const int dy = b.r - a.r;
        const int dz = -dx - dy;
        const int steps = std::max({std::abs(dx), std::abs(dy), std::abs(dz)});
        Cell previous = start;
        for (int step = 1; step < steps; ++step) {
            const double t = static_cast<double>(step) / static_cast<double>(steps);
            Cell cell = fromCube(static_cast<double>(a.q) + static_cast<double>(dx) * t,
                                 static_cast<double>(a.r) + static_cast<double>(dy) * t,
                                 static_cast<double>(dz) * t);
            cell.layer = layer;
            if (cell == previous) continue;
            interior.push_back(cell);
            previous = cell;
        }
        return interior;
    }

    // Square topologies: a supercover walk. When both error conditions hold in one step the
    // trace takes a corner, so a diagonal that squeezes between two cells is blocked if
    // either of them blocks: a shot does not pass through a keyhole.
    const int dx = end.x - start.x;
    const int dy = end.y - start.y;
    const int stepX = (dx > 0) - (dx < 0);
    const int stepY = (dy > 0) - (dy < 0);
    const int absX = std::abs(dx);
    const int absY = std::abs(dy);
    int x = start.x;
    int y = start.y;
    int error = absX - absY;
    while (x != end.x || y != end.y) {
        const int doubled = 2 * error;
        if (doubled > -absY) {
            error -= absY;
            x += stepX;
        }
        if (doubled < absX) {
            error += absX;
            y += stepY;
        }
        if (x == end.x && y == end.y) break;
        Cell cell{x, y, layer};
        interior.push_back(cell);
    }
    return interior;
}

}  // namespace

std::string_view coverLevelName(CoverLevel level) noexcept {
    switch (level) {
        case CoverLevel::None: return "none";
        case CoverLevel::Half: return "half";
        case CoverLevel::Full: return "full";
    }
    return "none";
}

Result<bool> GridLineOfSightPolicy::visible(const BoardState& board, Cell from, Cell to) const {
    if (from.layer != to.layer)
        return Result<bool>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "line of sight does not cross layers", "from.layer"));
    if (!board.contains(from))
        return Result<bool>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "line of sight origin is not on the board", "from"));
    if (!board.contains(to))
        return Result<bool>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "line of sight target is not on the board", "to"));
    if (from == to) return Result<bool>::success(true);

    bool reverse = false;
    auto interior = traceInterior(board, from, to, reverse);
    for (const Cell cell : interior) {
        if (blocksSight(board, cell)) return Result<bool>::success(false);
    }
    return Result<bool>::success(true);
}

Result<CoverLevel> GridCoverPolicy::cover(const BoardState& board, Cell attacker, Cell target) const {
    if (attacker.layer != target.layer)
        return Result<CoverLevel>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "cover does not cross layers", "attacker.layer"));
    if (!board.contains(target))
        return Result<CoverLevel>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "cover target is not on the board", "target"));
    if (!board.contains(attacker))
        return Result<CoverLevel>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "cover attacker is not on the board", "attacker"));

    // Full cover first: something sits on the incoming line.
    bool reverse = false;
    for (const Cell cell : traceInterior(board, attacker, target, reverse)) {
        if (blocksSight(board, cell)) return Result<CoverLevel>::success(CoverLevel::Full);
    }
    // Half cover: a blocker next to the target, but not between attacker and target.
    for (const Cell neighbour : board.neighbours(target)) {
        // A neighbour the board does not contain is not cover a target can use.
        if (board.contains(neighbour) && blocksSight(board, neighbour))
            return Result<CoverLevel>::success(CoverLevel::Half);
    }
    return Result<CoverLevel>::success(CoverLevel::None);
}

std::shared_ptr<ILineOfSightPolicy> gridLineOfSightPolicy() {
    static const std::shared_ptr<GridLineOfSightPolicy> policy = std::make_shared<GridLineOfSightPolicy>();
    return policy;
}

std::shared_ptr<ICoverPolicy> gridCoverPolicy() {
    static const std::shared_ptr<GridCoverPolicy> policy = std::make_shared<GridCoverPolicy>();
    return policy;
}

Result<std::vector<Cell>> visibleCellsInRange(const BoardState& board, const ILineOfSightPolicy& policy,
                                             Cell origin, int minimum, int maximum, CellRangeMetric metric) {
    auto inRange = PathQuery::cellsInRange(board, origin, minimum, maximum, metric);
    if (!inRange) return Result<std::vector<Cell>>::failure(inRange.status());
    const std::vector<Cell> candidates = std::move(inRange).takeValue();

    std::vector<Cell> visible;
    visible.reserve(candidates.size());
    for (const Cell cell : candidates) {
        auto sight = policy.visible(board, origin, cell);
        if (!sight) return Result<std::vector<Cell>>::failure(sight.status());
        if (sight.value()) visible.push_back(cell);
    }
    return Result<std::vector<Cell>>::success(std::move(visible));
}

}  // namespace eve::tactics
