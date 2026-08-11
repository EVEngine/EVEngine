#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/DtlHelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace eve::procgen {
namespace {

bool genDungeonBsp(const Params &params, Grid2D &out, std::string &error) {
    const int w = params.getWidth();
    const int h = params.getHeight();
    if (w < 8 || h < 8) {
        error = "dungeon.bsp: size must be at least 8x8";
        return false;
    }

    const int divisionMin     = params.getInt("divisionMin", 3);
    const int divisionRandMax = params.getInt("divisionRandMax", 4);
    const int roomMinX        = params.getInt("roomMinX", params.getInt("roomMin", 5));
    const int roomRandMaxX    = params.getInt("roomRandMaxX", params.getInt("roomRand", 2));
    const int roomMinY        = params.getInt("roomMinY", params.getInt("roomMin", 5));
    const int roomRandMaxY    = params.getInt("roomRandMaxY", params.getInt("roomRand", 2));

    auto matrix = dtlutil::makeMatrix(w, h, 0);
    // room=1, road=2; wall remains 0
    dtl::shape::SimpleRogueLike<std::uint_fast8_t> shape(
        1, 2, size_t(std::max(1, divisionMin)), size_t(std::max(0, divisionRandMax)),
        size_t(std::max(1, roomMinX)), size_t(std::max(0, roomRandMaxX)),
        size_t(std::max(1, roomMinY)), size_t(std::max(0, roomRandMaxY)));

    if (!shape.drawSEED(matrix, params.getSeed())) {
        error = "dungeon.bsp: DTL SimpleRogueLike draw failed";
        return false;
    }

    dtlutil::copyMatrixToGrid(matrix, out, [](std::uint_fast8_t v) -> uint32_t {
        if (v == 1) return Semantic::Floor;
        if (v == 2) return Semantic::Corridor;
        return Semantic::Wall;
    });
    out.setMeta("algorithm", "dungeon.bsp");
    dtlutil::placeSpawnAndStairs(out, params.getSeed());
    return true;
}

bool genCaveCellular(const Params &params, Grid2D &out, std::string &error) {
    const int w = params.getWidth();
    const int h = params.getHeight();
    if (w < 4 || h < 4) {
        error = "cave.cellular: size must be at least 4x4";
        return false;
    }
    const int    loops = params.getInt("loops", 5);
    const double fill  = double(params.getFloat("fill", 0.45f));

    auto matrix = dtlutil::makeMatrix(w, h, 0);
    // land=1, border/wall=0
    dtl::shape::CellularAutomatonIsland<std::uint_fast8_t> shape(1, 0, size_t(std::max(1, loops)),
                                                                 fill);
    dtlutil::seedEngine(params.getSeed());
    if (!shape.draw(matrix)) {
        error = "cave.cellular: DTL CellularAutomatonIsland draw failed";
        return false;
    }

    dtlutil::copyMatrixToGrid(matrix, out, [](std::uint_fast8_t v) -> uint32_t {
        return v == 1 ? Semantic::Floor : Semantic::Wall;
    });
    out.setMeta("algorithm", "cave.cellular");
    dtlutil::placeSpawnAndStairs(out, params.getSeed());
    return true;
}

bool genMazeBacktrack(const Params &params, Grid2D &out, std::string &error) {
    const int w = params.getWidth();
    const int h = params.getHeight();
    // MazeDig works best on odd dimensions.
    const int mw = w < 5 ? 5 : (w % 2 == 0 ? w - 1 : w);
    const int mh = h < 5 ? 5 : (h % 2 == 0 ? h - 1 : h);

    auto matrix = dtlutil::makeMatrix(mw, mh, 0);
    // empty(floor)=1, wall=0
    dtl::shape::MazeDig<std::uint_fast8_t> shape(1, 0);
    if (!shape.drawSEED(matrix, params.getSeed())) {
        error = "maze.backtrack: DTL MazeDig draw failed";
        return false;
    }

    out.resize(w, h);
    out.fill(Semantic::Wall);
    for (int y = 0; y < mh && y < h; ++y) {
        for (int x = 0; x < mw && x < w; ++x) {
            out.setCell(x, y, matrix[size_t(y)][size_t(x)] == 1 ? Semantic::Floor : Semantic::Wall);
        }
    }
    out.setMeta("algorithm", "maze.backtrack");
    dtlutil::placeSpawnAndStairs(out, params.getSeed());
    return true;
}

bool genNoiseTerrain(const Params &params, Grid2D &out, std::string &error) {
    const int w = params.getWidth();
    const int h = params.getHeight();
    if (w < 2 || h < 2) {
        error = "noise.terrain: size must be at least 2x2";
        return false;
    }
    const float frequency = params.getFloat("frequency", 6.f);
    const int   octaves   = params.getInt("octaves", 4);
    // Height bands in [0, maxHeight]
    const int maxHeight = params.getInt("maxHeight", 9);

    auto matrix = dtlutil::makeMatrix(w, h, 0);
    dtl::shape::PerlinIsland<std::uint_fast8_t> shape(double(std::max(0.1f, frequency)),
                                                      size_t(std::max(1, octaves)),
                                                      std::uint_fast8_t(std::max(1, maxHeight)), 0);
    if (!shape.drawSEED(matrix, params.getSeed())) {
        error = "noise.terrain: DTL PerlinIsland draw failed";
        return false;
    }

    const float waterMax = params.getFloat("waterMax", 0.25f);
    const float sandMax  = params.getFloat("sandMax", 0.35f);
    const float grassMax = params.getFloat("grassMax", 0.65f);
    const float dirtMax  = params.getFloat("dirtMax", 0.80f);
    const float stoneMax = params.getFloat("stoneMax", 0.92f);

    dtlutil::copyMatrixToGrid(matrix, out, [&](std::uint_fast8_t v) -> uint32_t {
        const float t = maxHeight <= 0 ? 0.f : float(v) / float(maxHeight);
        if (t < waterMax) return Semantic::Water;
        if (t < sandMax) return Semantic::Sand;
        if (t < grassMax) return Semantic::Grass;
        if (t < dirtMax) return Semantic::Dirt;
        if (t < stoneMax) return Semantic::Stone;
        return Semantic::Snow;
    });
    out.setMeta("algorithm", "noise.terrain");
    dtlutil::placeSpawnAndStairs(out, params.getSeed());
    return true;
}

/** Drunkard's walk — not in DTL; local RNG from seed. */
bool genCaveDrunkard(const Params &params, Grid2D &out, std::string &error) {
    const int w = params.getWidth();
    const int h = params.getHeight();
    if (w < 4 || h < 4) {
        error = "cave.drunkard: size must be at least 4x4";
        return false;
    }
    const float floorPct = std::clamp(params.getFloat("floorPct", 0.45f), 0.05f, 0.95f);
    const int   target   = int(float(w * h) * floorPct);

    out.resize(w, h);
    out.fill(Semantic::Wall);

    std::mt19937 rng(params.getSeed());
    std::uniform_int_distribution<int> dirDist(0, 3);
    int x = w / 2;
    int y = h / 2;
    int carved = 0;
    int guard  = w * h * 20;
    while (carved < target && guard-- > 0) {
        if (out.getCell(x, y) != Semantic::Floor) {
            out.setCell(x, y, Semantic::Floor);
            ++carved;
        }
        switch (dirDist(rng)) {
        case 0: if (x > 1) --x; break;
        case 1: if (x < w - 2) ++x; break;
        case 2: if (y > 1) --y; break;
        default: if (y < h - 2) ++y; break;
        }
    }
    // Outer wall frame
    for (int i = 0; i < w; ++i) {
        out.setCell(i, 0, Semantic::Wall);
        out.setCell(i, h - 1, Semantic::Wall);
    }
    for (int j = 0; j < h; ++j) {
        out.setCell(0, j, Semantic::Wall);
        out.setCell(w - 1, j, Semantic::Wall);
    }
    out.setMeta("algorithm", "cave.drunkard");
    dtlutil::placeSpawnAndStairs(out, params.getSeed());
    return true;
}

}  // namespace

// Defined in WfcSimple.cpp
void registerWfcSimple(GeneratorRegistry &registry);

void GeneratorRegistry::registerBuiltins() {
    if (builtinsRegistered_) return;
    registerAlgorithm("dungeon.bsp", genDungeonBsp);
    registerAlgorithm("cave.cellular", genCaveCellular);
    registerAlgorithm("cave.drunkard", genCaveDrunkard);
    registerAlgorithm("maze.backtrack", genMazeBacktrack);
    registerAlgorithm("noise.terrain", genNoiseTerrain);
    registerWfcSimple(*this);
    builtinsRegistered_ = true;
}

}  // namespace eve::procgen
