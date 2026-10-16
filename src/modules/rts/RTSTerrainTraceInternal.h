#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace eve::rts::terrain_internal {

// Visit every positive-length cell interval, including arbitrarily short corner
// crossings. The heightfield is piecewise constant, matching the map grid.
// Inputs come from the validated script world; no state or callback is retained.
template <class HeightAt>
std::optional<float> firstTerrainImpact(float fromX, float fromY, float fromHeight, float toX, float toY,
                                        float toHeight, float originX, float originY, float cellSize,
                                        const HeightAt& heightAt) {
    const double x           = (static_cast<double>(fromX) - originX) / cellSize;
    const double y           = (static_cast<double>(fromY) - originY) / cellSize;
    const double dx          = (static_cast<double>(toX) - fromX) / cellSize;
    const double dy          = (static_cast<double>(toY) - fromY) / cellSize;
    int          cellX       = static_cast<int>(std::floor(x));
    int          cellY       = static_cast<int>(std::floor(y));
    const int    stepX       = dx > 0.0 ? 1 : -1;
    const int    stepY       = dy > 0.0 ? 1 : -1;
    const double infinity    = std::numeric_limits<double>::infinity();
    const double deltaX      = dx == 0.0 ? infinity : 1.0 / std::abs(dx);
    const double deltaY      = dy == 0.0 ? infinity : 1.0 / std::abs(dy);
    double       nextX       = dx == 0.0 ? infinity : (cellX + (dx > 0.0 ? 1.0 : 0.0) - x) / dx;
    double       nextY       = dy == 0.0 ? infinity : (cellY + (dy > 0.0 ? 1.0 : 0.0) - y) / dy;
    const double heightDelta = static_cast<double>(toHeight) - fromHeight;
    double       enter       = 0.0;
    while (enter < 1.0) {
        const double leave = std::min({nextX, nextY, 1.0});
        if (leave > enter) {
            const double terrain     = heightAt(cellX, cellY);
            const double entryHeight = fromHeight + heightDelta * enter;
            if (terrain >= entryHeight) return static_cast<float>(enter);
            if (heightDelta < 0.0) {
                const double crossing = (terrain - fromHeight) / heightDelta;
                if (crossing >= enter && crossing < leave) return static_cast<float>(crossing);
            }
        }
        if (leave >= 1.0) break;
        // Advancing both axes at an exact corner avoids testing cells touched
        // only at a point. Grid cells use the same half-open convention as navigation.
        if (nextX <= leave) {
            cellX += stepX;
            nextX += deltaX;
        }
        if (nextY <= leave) {
            cellY += stepY;
            nextY += deltaY;
        }
        enter = leave;
    }
    return std::nullopt;
}

}  // namespace eve::rts::terrain_internal
