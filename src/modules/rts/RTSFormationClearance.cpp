#include "map/Pathfinder.h"
#include "rts/RTSSystemMovementInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::rts::systems_internal {
bool isFormationSegmentClear(const map::Pathfinder& pathfinder, const NavigationGrid& grid, WorldPosition from,
                             WorldPosition to, float radius) {
    if (!isFinitePosition(from) || !isFinitePosition(to) || !std::isfinite(radius) || radius <= 0.f ||
        !std::isfinite(grid.cellSize) || grid.cellSize <= 0.f || !std::isfinite(grid.originX) ||
        !std::isfinite(grid.originY))
        return false;
    const double     cell     = grid.cellSize;
    const double     ax       = (static_cast<double>(from.x) - grid.originX) / cell;
    const double     ay       = (static_cast<double>(from.y) - grid.originY) / cell;
    const double     bx       = (static_cast<double>(to.x) - grid.originX) / cell;
    const double     by       = (static_cast<double>(to.y) - grid.originY) / cell;
    const double     reach    = static_cast<double>(radius) / cell + 0.5;
    const double     minX     = std::ceil(std::min(ax, bx) - reach);
    const double     maxX     = std::floor(std::max(ax, bx) + reach);
    const double     minY     = std::ceil(std::min(ay, by) - reach);
    const double     maxY     = std::floor(std::max(ay, by) + reach);
    constexpr double minIndex = static_cast<double>(std::numeric_limits<int>::min()) + 1.0;
    constexpr double maxIndex = static_cast<double>(std::numeric_limits<int>::max()) - 1.0;
    if (minX < minIndex || maxX > maxIndex || minY < minIndex || maxY > maxIndex ||
        (maxX - minX + 1.0) * (maxY - minY + 1.0) > 4096.0)
        return false;
    for (int y = static_cast<int>(minY); y <= static_cast<int>(maxY); ++y) {
        for (int x = static_cast<int>(minX); x <= static_cast<int>(maxX); ++x) {
            if (pathfinder.isWalkable(x, y)) continue;
            double     enter = 0.0, leave = 1.0;
            const auto intersectsSlab = [&](double start, double delta, double low, double high) {
                if (delta == 0.0) return start >= low && start <= high;
                double first = (low - start) / delta;
                double last  = (high - start) / delta;
                if (first > last) std::swap(first, last);
                enter = std::max(enter, first);
                leave = std::min(leave, last);
                return enter <= leave;
            };
            if (intersectsSlab(ax, bx - ax, x - reach, x + reach) && intersectsSlab(ay, by - ay, y - reach, y + reach))
                return false;
        }
    }
    return true;
}
}  // namespace eve::rts::systems_internal
