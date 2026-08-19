#pragma once

#include "procgen/Grid2D.h"
#include "procgen/Params.h"
#include "procgen/Semantic.h"

// Prefer specific DTL headers over the umbrella <DTL.hpp>. The umbrella pulls in
// RandomVoronoi / SimpleVoronoiIsland, which call missing drawOperator* members and
// fail under Apple Clang. Compat shims for DrawJagged* are on the include path.
#include <DTL/Base/RogueLike.hpp>
#include <DTL/Random/RandomEngine.hpp>
#include <DTL/Shape/CellularAutomatonIsland.hpp>
#include <DTL/Shape/MazeDig.hpp>
#include <DTL/Shape/PerlinIsland.hpp>
#include <DTL/Shape/SimpleRogueLike.hpp>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace eve::procgen::dtlutil {

inline void seedEngine(uint32_t seed) {
    DTL_RANDOM_ENGINE.seed(seed);
    DTL_RANDOM_ENGINE.clear();
}

inline std::vector<std::vector<std::uint_fast8_t>> makeMatrix(int width, int height,
                                                              std::uint_fast8_t fill = 0) {
    return std::vector<std::vector<std::uint_fast8_t>>(
        size_t(height), std::vector<std::uint_fast8_t>(size_t(width), fill));
}

inline void copyMatrixToGrid(const std::vector<std::vector<std::uint_fast8_t>> &matrix,
                             Grid2D &grid,
                             const std::function<uint32_t(std::uint_fast8_t)> &mapValue) {
    const int h = int(matrix.size());
    const int w = h > 0 ? int(matrix[0].size()) : 0;
    grid.resize(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            grid.setCell(x, y, mapValue(matrix[size_t(y)][size_t(x)]));
        }
    }
}

/** @brief Place spawn + stairs on walkable cells (floor/corridor/grass/dirt/sand). */
inline void placeSpawnAndStairs(Grid2D &grid, uint32_t seed) {
    std::vector<std::pair<int, int>> walkable;
    walkable.reserve(size_t(grid.getWidth() * grid.getHeight() / 4));
    for (int y = 0; y < grid.getHeight(); ++y) {
        for (int x = 0; x < grid.getWidth(); ++x) {
            const uint32_t c = uint32_t(grid.getCell(x, y));
            if (c == Semantic::Floor || c == Semantic::Corridor || c == Semantic::Grass ||
                c == Semantic::Dirt || c == Semantic::Sand) {
                walkable.emplace_back(x, y);
            }
        }
    }
    if (walkable.empty()) return;

    auto pick = [&](uint32_t salt) -> std::pair<int, int> {
        const uint32_t idx = (seed * 1664525u + salt * 1013904223u) % uint32_t(walkable.size());
        return walkable[idx];
    };
    auto a = pick(1);
    auto b = pick(7);
    if (a == b && walkable.size() > 1) b = walkable[(size_t(seed) + 1) % walkable.size()];

    grid.clearObjects();
    grid.addObjectAt("spawn", "spawn", float(a.first), float(a.second));
    grid.addObjectAt("stairs", "stairs", float(b.first), float(b.second));
}

}  // namespace eve::procgen::dtlutil
