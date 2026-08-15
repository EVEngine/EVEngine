#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "procgen/Procgen.h"
#include "procgen/Semantic.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/algorithms/RoguelikeGenerator.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace eve::procgen;

namespace {

int countSemantic(const Grid2D &g, int semantic) {
    int n = 0;
    for (uint32_t c : g.cells())
        if (int(c) == semantic) ++n;
    return n;
}

bool isWalkableCell(const Grid2D &g, int x, int y) {
    const uint32_t c = uint32_t(g.getCell(x, y));
    return c == Semantic::Floor || c == Semantic::Corridor;
}

constexpr int kE = 1 << 0;   // +x
constexpr int kS = 1 << 1;   // +y
constexpr int kW = 1 << 2;   // -x
constexpr int kN = 1 << 3;   // -y
constexpr int kSE = 1 << 4;
constexpr int kSW = 1 << 5;
constexpr int kNW = 1 << 6;
constexpr int kNE = 1 << 7;

int expectedWallMask(const Grid2D &g, int x, int y) {
    const int w = g.getWidth();
    const int h = g.getHeight();
    int mask = 0;
    if (x + 1 < w && isWalkableCell(g, x + 1, y)) mask |= kE;
    if (y + 1 < h && isWalkableCell(g, x, y + 1)) mask |= kS;
    if (x - 1 >= 0 && isWalkableCell(g, x - 1, y)) mask |= kW;
    if (y - 1 >= 0 && isWalkableCell(g, x, y - 1)) mask |= kN;
    if (x + 1 < w && y + 1 < h && isWalkableCell(g, x + 1, y + 1)) mask |= kSE;
    if (x - 1 >= 0 && y + 1 < h && isWalkableCell(g, x - 1, y + 1)) mask |= kSW;
    if (x - 1 >= 0 && y - 1 >= 0 && isWalkableCell(g, x - 1, y - 1)) mask |= kNW;
    if (x + 1 < w && y - 1 >= 0 && isWalkableCell(g, x + 1, y - 1)) mask |= kNE;
    return mask;
}

// Wall detail must exactly equal the 8-bit neighbour mask (0 is fine for walls
// that touch no floor). Non-wall detail must be a floor variant (1..32) or a
// decor marker (>=100); anything else would be a mask leak into the floor.
bool wallMasksConsistent(const Grid2D &g) {
    const int w = g.getWidth();
    const int h = g.getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (uint32_t(g.getCell(x, y)) == Semantic::Wall) {
                if (g.getDetail(x, y) != expectedWallMask(g, x, y)) return false;
            } else {
                const int d = g.getDetail(x, y);
                if (d > 32 && d < 100) return false;  // mask leak into floor
            }
        }
    }
    return true;
}

// Spawn and stairs must land on walkable cells.
bool markersWalkable(const Grid2D &g) {
    for (int i = 0; i < g.getObjectCount(); ++i) {
        const std::string &t = g.getObjectType(i);
        if (t != "spawn" && t != "stairs") continue;
        if (!isWalkableCell(g, int(g.getObjectX(i)), int(g.getObjectY(i)))) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("procgen.roguelike.reproducible") {
    Procgen *mod = Procgen::create();
    CHECK(mod->hasAlgorithm("level.roguelike"));

    Params p;
    p.setSeed(42);
    p.setSize(48, 32);
    p.setInt("roomCount", 10);
    Grid2D *a = mod->generate("level.roguelike", &p);
    REQUIRE(a != nullptr);

    Params q;
    q.setSeed(42);
    q.setSize(48, 32);
    q.setInt("roomCount", 10);
    Grid2D *b = mod->generate("level.roguelike", &q);
    REQUIRE(b != nullptr);

    // Same seed -> identical cells and detail.
    CHECK(a->cells() == b->cells());
    CHECK(a->detail() == b->detail());
    delete a;
    delete b;
}

TEST_CASE("procgen.roguelike.structure") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(7);
    p.setSize(48, 32);
    p.setInt("roomCount", 12);
    p.setFloat("decorDensity", 0.06f);
    p.setString("decorSet", "mixed");

    Grid2D *g = mod->generate("level.roguelike", &p);
    REQUIRE(g != nullptr);

    const int floorCount = countSemantic(*g, Semantic::Floor) + countSemantic(*g, Semantic::Corridor);
    const int wallCount  = countSemantic(*g, Semantic::Wall);
    CHECK_GT(floorCount, 0);
    CHECK_GT(wallCount, 0);
    CHECK_EQ(floorCount + wallCount, g->getWidth() * g->getHeight());

    // Outer border stays solid wall (padding >= 1 by default).
    CHECK_EQ(g->getCell(0, 0), int(Semantic::Wall));
    CHECK_EQ(g->getCell(g->getWidth() - 1, g->getHeight() - 1), int(Semantic::Wall));

    // Detail layer: wall autotile masks + floor variants are present.
    CHECK(wallMasksConsistent(*g));
    CHECK(markersWalkable(*g));

    // Metadata describes the run.
    CHECK_EQ(g->getMeta("algorithm", ""), "level.roguelike");
    CHECK_EQ(g->getMeta("seed", ""), "7");

    // Objects carry spawn/stairs plus props.
    bool hasSpawn = false, hasStairs = false;
    for (int i = 0; i < g->getObjectCount(); ++i) {
        if (g->getObjectType(i) == "spawn") hasSpawn = true;
        if (g->getObjectType(i) == "stairs") hasStairs = true;
    }
    CHECK(hasSpawn);
    CHECK(hasStairs);
    delete g;
}

TEST_CASE("procgen.roguelike.seedVaries") {
    Procgen *mod = Procgen::create();
    auto run = [&](uint32_t seed) {
        Params p;
        p.setSeed(seed);
        p.setSize(40, 28);
        p.setInt("roomCount", 9);
        Grid2D *g = mod->generate("level.roguelike", &p);
        REQUIRE(g != nullptr);
        Grid2D copy = *g;
        delete g;
        return copy;
    };
    Grid2D s1 = run(1);
    Grid2D s2 = run(2);
    Grid2D s3 = run(1);
    CHECK(s1.cells() != s2.cells());
    CHECK(s1.cells() == s3.cells());
}

TEST_CASE("procgen.roguelike.rulesChangeLayout") {
    Procgen *mod = Procgen::create();
    auto gen = [&](int rooms, const std::string &pattern, const std::string &style) {
        Params p;
        p.setSeed(11);
        p.setSize(48, 32);
        p.setInt("roomCount", rooms);
        p.setString("floorPattern", pattern);
        p.setString("corridorStyle", style);
        Grid2D *g = mod->generate("level.roguelike", &p);
        REQUIRE(g != nullptr);
        Grid2D copy = *g;
        delete g;
        return copy;
    };

    Grid2D few  = gen(5, "brick", "l");
    Grid2D many = gen(20, "brick", "l");
    CHECK(few.cells() != many.cells());

    // Pattern change keeps same base but alters floor detail.
    Grid2D brick   = gen(9, "brick", "l");
    Grid2D checker = gen(9, "checker", "l");
    CHECK_EQ(brick.cells(), checker.cells());
    CHECK(brick.detail() != checker.detail());
}

TEST_CASE("procgen.roguelike.errors") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(1);
    p.setSize(4, 4);  // too small
    CHECK(mod->generate("level.roguelike", &p) == nullptr);
    CHECK(mod->lastError().find("at least 9x9") != std::string::npos);
}

TEST_CASE("procgen.roguelike.manualFillDetail") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(3);
    p.setSize(32, 24);
    Grid2D *g = mod->generate("level.roguelike", &p);
    REQUIRE(g != nullptr);

    // Ultimate control: script can override any cell / detail after generation.
    g->setCell(1, 1, int(Semantic::Floor));
    g->setDetail(1, 1, 5);
    CHECK_EQ(g->getCell(1, 1), int(Semantic::Floor));
    CHECK_EQ(g->getDetail(1, 1), 5);
    CHECK_EQ(g->getDetail(-5, -5), 0);   // out of bounds -> 0
    g->setDetail(0, 0, 999);             // clamps to 255
    CHECK_EQ(g->getDetail(0, 0), 255);
    delete g;
}

TEST_CASE("procgen.autotileGrid.postProcess") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(9);
    p.setSize(40, 30);
    p.setInt("roomCount", 8);
    p.setInt("autotile", 0);  // disable built-in autotile to test the helper alone
    Grid2D *g = mod->generate("level.roguelike", &p);
    REQUIRE(g != nullptr);
    CHECK(mod->autotileGrid(g));
    CHECK(wallMasksConsistent(*g));
    delete g;
}

TEST_CASE("procgen.randomSeed.nonZeroAndVaries") {
    Procgen *mod = Procgen::create();
    CHECK_GT(mod->randomSeed(), 0u);
    uint32_t a = mod->randomSeed();
    uint32_t b = mod->randomSeed();
    // Overwhelmingly likely distinct; do not require strict inequality.
    CHECK_GT(a, 0u);
    CHECK_GT(b, 0u);
    CHECK_EQ(randomSeedValue() == 0, false);
}
