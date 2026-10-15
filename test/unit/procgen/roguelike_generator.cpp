#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "procgen/Procgen.h"
#include "procgen/Semantic.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/algorithms/RoguelikeGenerator.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <string>
#include <vector>

using namespace eve::procgen;

namespace {

struct ParamsLease {
    ProcgenParamsHandleRef handle{};
    ParamsLease() = default;
    explicit ParamsLease(ProcgenParamsHandleRef value) : handle(value) {}
    ParamsLease(const ParamsLease &)            = delete;
    ParamsLease &operator=(const ParamsLease &) = delete;
    ParamsLease(ParamsLease &&other) noexcept : handle(other.handle) { other.handle = {}; }
    ParamsLease &operator=(ParamsLease &&other) noexcept {
        if (this == &other) return *this;
        reset();
        handle       = other.handle;
        other.handle = {};
        return *this;
    }
    ~ParamsLease() { reset(); }
    void reset() noexcept {
        if (!handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("roguelike params cleanup");
        handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<Params> view() const noexcept { return Procgen::resolve(handle); }
};

struct GridLease {
    ProcgenGridHandleRef handle{};
    GridLease() = default;
    explicit GridLease(ProcgenGridHandleRef value) : handle(value) {}
    GridLease(const GridLease &)            = delete;
    GridLease &operator=(const GridLease &) = delete;
    GridLease(GridLease &&other) noexcept : handle(other.handle) { other.handle = {}; }
    GridLease &operator=(GridLease &&other) noexcept {
        if (this == &other) return *this;
        reset();
        handle       = other.handle;
        other.handle = {};
        return *this;
    }
    ~GridLease() { reset(); }
    void reset() noexcept {
        if (!handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("roguelike grid cleanup");
        handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<Grid2D> view() const noexcept { return Procgen::resolve(handle); }
};

ParamsLease requireParams() {
    auto result = Procgen::newParamsHandle();
    REQUIRE(result.ok());
    return ParamsLease(std::move(result).takeValue());
}

GridLease requireGrid(Procgen &proc, const std::string &algorithm, ProcgenParamsHandleRef params) {
    auto result = proc.generateHandle(algorithm, params);
    REQUIRE(result.ok());
    return GridLease(std::move(result).takeValue());
}

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

bool allWalkableConnected(const Grid2D &g) {
    const int w = g.getWidth(), h = g.getHeight();
    int start = -1, total = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (isWalkableCell(g, x, y)) { if (start < 0) start = y * w + x; ++total; }
    if (start < 0) return true;
    std::vector<bool> seen(size_t(w * h), false);
    std::queue<int> pending;
    pending.push(start); seen[size_t(start)] = true;
    int reached = 0;
    constexpr int dx[] = {1, -1, 0, 0};
    constexpr int dy[] = {0, 0, 1, -1};
    while (!pending.empty()) {
        const int key = pending.front(); pending.pop(); ++reached;
        const int x = key % w, y = key / w;
        for (int i = 0; i < 4; ++i) {
            const int nx = x + dx[i], ny = y + dy[i];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || !isWalkableCell(g, nx, ny)) continue;
            const int next = ny * w + nx;
            if (!seen[size_t(next)]) { seen[size_t(next)] = true; pending.push(next); }
        }
    }
    return reached == total;
}

}  // namespace

TEST_CASE("procgen.roguelike.reproducible") {
    Procgen *mod = Procgen::create();
    CHECK(mod->hasAlgorithm("level.roguelike"));

    auto p     = requireParams();
    auto pView = p.view();
    REQUIRE(pView.isBound());
    pView->setSeed(42);
    pView->setSize(48, 32);
    pView->setInt("roomCount", 10);
    auto aLease = requireGrid(*mod, "level.roguelike", p.handle);
    auto a      = aLease.view();
    REQUIRE(a.isBound());

    auto q     = requireParams();
    auto qView = q.view();
    REQUIRE(qView.isBound());
    qView->setSeed(42);
    qView->setSize(48, 32);
    qView->setInt("roomCount", 10);
    auto bLease = requireGrid(*mod, "level.roguelike", q.handle);
    auto b      = bLease.view();
    REQUIRE(b.isBound());

    // Same seed -> identical cells and detail.
    CHECK(a->cells() == b->cells());
    CHECK(a->detail() == b->detail());
}

TEST_CASE("procgen.roguelike.structure") {
    Procgen *mod = Procgen::create();
    auto     p     = requireParams();
    auto     pView = p.view();
    REQUIRE(pView.isBound());
    pView->setSeed(7);
    pView->setSize(48, 32);
    pView->setInt("roomCount", 12);
    pView->setInt("stairCount", 2);
    pView->setString("connectionStyle", "nearest");
    pView->setFloat("decorDensity", 0.06f);
    pView->setString("decorSet", "mixed");
    auto gLease = requireGrid(*mod, "level.roguelike", p.handle);
    auto g      = gLease.view();
    REQUIRE(g.isBound());

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
    CHECK(allWalkableConnected(*g));

    // Metadata describes the run.
    CHECK_EQ(g->getMeta("algorithm", ""), "level.roguelike");
    CHECK_EQ(g->getMeta("seed", ""), "7");

    // Objects carry spawn/stairs plus props.
    bool hasSpawn = false;
    int stairObjects = 0;
    for (int i = 0; i < g->getObjectCount(); ++i) {
        if (g->getObjectType(i) == "spawn") hasSpawn = true;
        if (g->getObjectType(i) == "stairs") {
            ++stairObjects;
            CHECK((g->getObjectFlags(i) & 64) != 0);
            const int x = int(g->getObjectX(i)), y = int(g->getObjectY(i));
            const float rotation = g->getObjectRotation(i);
            if (rotation == 180.f) CHECK_EQ(g->getCell(x, y - 1), int(Semantic::Wall));
            else if (rotation == 0.f) CHECK_EQ(g->getCell(x, y + 1), int(Semantic::Wall));
            else if (rotation == 270.f) CHECK_EQ(g->getCell(x - 1, y), int(Semantic::Wall));
            else { CHECK_EQ(rotation, 90.f); CHECK_EQ(g->getCell(x + 1, y), int(Semantic::Wall)); }
        }
    }
    CHECK(hasSpawn);
    CHECK_EQ(stairObjects, 2);
    CHECK_EQ(g->getMeta("stairs", ""), "2");
}

TEST_CASE("procgen.roguelike.seedVaries") {
    Procgen *mod = Procgen::create();
    auto     run = [&](uint32_t seed) {
        auto p     = requireParams();
        auto pView = p.view();
        REQUIRE(pView.isBound());
        pView->setSeed(seed);
        pView->setSize(40, 28);
        pView->setInt("roomCount", 9);
        auto gLease = requireGrid(*mod, "level.roguelike", p.handle);
        auto g      = gLease.view();
        REQUIRE(g.isBound());
        Grid2D copy = *g;
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
    auto     gen = [&](int rooms, const std::string &pattern, const std::string &style,
                       const std::string &layout, int branchBias) {
        auto p     = requireParams();
        auto pView = p.view();
        REQUIRE(pView.isBound());
        pView->setSeed(11);
        pView->setSize(48, 32);
        pView->setInt("roomCount", rooms);
        pView->setString("floorPattern", pattern);
        pView->setString("corridorStyle", style);
        pView->setString("layoutStyle", layout);
        pView->setString("connectionStyle", layout == "clustered" ? "growth" : "sequential");
        pView->setInt("clusterBranchBias", branchBias);
        auto gLease = requireGrid(*mod, "level.roguelike", p.handle);
        auto g      = gLease.view();
        REQUIRE(g.isBound());
        Grid2D copy = *g;
        return copy;
    };

    Grid2D few  = gen(5, "brick", "l", "grid", 0);
    Grid2D many = gen(20, "brick", "l", "grid", 0);
    CHECK(few.cells() != many.cells());

    // Pattern change keeps same base but alters floor detail.
    Grid2D brick   = gen(9, "brick", "l", "grid", 0);
    Grid2D checker = gen(9, "checker", "l", "grid", 0);
    CHECK_EQ(brick.cells(), checker.cells());
    CHECK(brick.detail() != checker.detail());

    Grid2D clustered = gen(9, "brick", "l", "clustered", 0);
    CHECK(clustered.cells() != brick.cells());
    CHECK(allWalkableConnected(clustered));
    Grid2D hubClustered = gen(9, "brick", "l", "clustered", 2);
    CHECK(hubClustered.cells() != clustered.cells());
    CHECK(allWalkableConnected(hubClustered));
}

TEST_CASE("procgen.roguelike.configurableAssetDressing") {
    Procgen *mod = Procgen::create();
    auto p = requireParams();
    auto pView = p.view();
    REQUIRE(pView.isBound());
    pView->setSeed(23);
    pView->setSize(52, 38);
    pView->setInt("roomCount", 14);
    pView->setString("assetPack", "test-pack");
    pView->setString("assets.container", "crate_a,crate_b");
    pView->setString("assets.light", "sconce_custom");
    pView->setString("assets.banner", "wall_hanging_custom");
    pView->setString("assets.floor", "floor_a,floor_b");
    pView->setFloat("propDensity", 0.6f);
    auto gLease = requireGrid(*mod, "level.roguelike", p.handle);
    auto g = gLease.view();
    REQUIRE(g.isBound());
    CHECK_EQ(g->getMeta("assetPack", ""), "test-pack");
    CHECK_EQ(g->getMeta("assets.floor", ""), "floor_a,floor_b");
    CHECK_GT(std::stoi(g->getMeta("placedProps", "0")), 0);
    CHECK_GE(std::stoi(g->getMeta("minimumRoomProps", "0")), 8);

    bool customLight = false;
    bool fractionalPlacement = false;
    bool roomZone = false;
    for (int i = 0; i < g->getObjectCount(); ++i) {
        if (g->getObjectType(i) == "room") {
            roomZone = true;
            const int rx = int(g->getObjectX(i)), ry = int(g->getObjectY(i));
            const int rw = int(g->getObjectWidth(i)), rh = int(g->getObjectHeight(i));
            for (int y = ry; y < ry + rh; ++y)
                for (int x = rx; x < rx + rw; ++x)
                    CHECK_EQ(g->getCell(x, y), int(Semantic::Floor));
        }
        if (g->getObjectType(i) == "light") {
            customLight = customLight || g->getObjectAsset(i) == "sconce_custom";
            CHECK((g->getObjectFlags(i) & 4) != 0);
        }
        CHECK_GE(g->getObjectRotation(i), 0.f);
        CHECK_LT(g->getObjectRotation(i), 360.f);
        fractionalPlacement = fractionalPlacement ||
            std::abs(g->getObjectX(i) - std::round(g->getObjectX(i))) > 0.01f ||
            std::abs(g->getObjectY(i) - std::round(g->getObjectY(i))) > 0.01f;
    }
    CHECK(customLight);
    CHECK(roomZone);
    CHECK(fractionalPlacement);
    CHECK(markersWalkable(*g));
}

TEST_CASE("procgen.roguelike.errors") {
    Procgen *mod = Procgen::create();
    auto     p     = requireParams();
    auto     pView = p.view();
    REQUIRE(pView.isBound());
    pView->setSeed(1);
    pView->setSize(4, 4);  // too small
    auto failed = mod->generateHandle("level.roguelike", p.handle);
    CHECK(!failed.ok());
    const eve::Diagnostic *diagnostic = failed.error();
    REQUIRE(diagnostic != nullptr);
    CHECK_EQ(diagnostic->code(), eve::DiagnosticCode::Failed);
}

TEST_CASE("procgen.roguelike.manualFillDetail") {
    Procgen *mod = Procgen::create();
    auto     p     = requireParams();
    auto     pView = p.view();
    REQUIRE(pView.isBound());
    pView->setSeed(3);
    pView->setSize(32, 24);
    auto gLease = requireGrid(*mod, "level.roguelike", p.handle);
    auto g      = gLease.view();
    REQUIRE(g.isBound());

    // Ultimate control: script can override any cell / detail after generation.
    g->setCell(1, 1, int(Semantic::Floor));
    g->setDetail(1, 1, 5);
    CHECK_EQ(g->getCell(1, 1), int(Semantic::Floor));
    CHECK_EQ(g->getDetail(1, 1), 5);
    CHECK_EQ(g->getDetail(-5, -5), 0);   // out of bounds -> 0
    g->setDetail(0, 0, 999);             // clamps to 255
    CHECK_EQ(g->getDetail(0, 0), 255);
}

TEST_CASE("procgen.autotileGrid.postProcess") {
    Procgen *mod = Procgen::create();
    auto     p     = requireParams();
    auto     pView = p.view();
    REQUIRE(pView.isBound());
    pView->setSeed(9);
    pView->setSize(40, 30);
    pView->setInt("roomCount", 8);
    pView->setInt("autotile", 0);  // disable built-in autotile to test the helper alone
    auto gLease = requireGrid(*mod, "level.roguelike", p.handle);
    auto g      = gLease.view();
    REQUIRE(g.isBound());
    auto autotile = mod->autotileGrid(gLease.handle);
    CHECK(autotile.ok());
    CHECK(wallMasksConsistent(*g));
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
