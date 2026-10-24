#include <cmath>
#include <limits>
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMask.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;

TEST_CASE("procgen.traversal.worldSlopeAndCost") {
    Heightmap heights(5, 3), base(5, 3), out(5, 3);
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 5; ++x) {
            heights.setHeight(x, z, float(x));
            base.setHeight(x, z, 2);
        }
    TerrainTraversalSettings s;
    s.maxSlopeDegrees = 40;
    REQUIRE(generateTerrainTraversalCosts(out, heights, base, s).ok());
    CHECK(out.height(2, 1) == 0);
    s.spacingX  = 2;
    s.slopeCost = 2;
    REQUIRE(generateTerrainTraversalCosts(out, heights, base, s).ok());
    CHECK(out.height(2, 1) == 4);
    base.setHeight(2, 1, 0);
    REQUIRE(generateTerrainTraversalCosts(out, heights, base, s).ok());
    CHECK(out.height(2, 1) == 0);
}
TEST_CASE("procgen.traversal.ridgeAtomicAlias") {
    Heightmap heights(3, 1), base(3, 1), out(3, 1);
    heights.setHeight(1, 0, 10);
    for (int x = 0; x < 3; ++x) base.setHeight(x, 0, 1);
    TerrainTraversalSettings s;
    REQUIRE(generateTerrainTraversalCosts(out, heights, base, s).ok());
    CHECK(out.height(1, 0) == 0);
    const auto previous = base.data();
    s.spacingX          = 0;
    CHECK(!generateTerrainTraversalCosts(base, heights, base, s).ok());
    CHECK(base.data() == previous);
    s.spacingX        = 1;
    s.maxSlopeDegrees = 90;
    REQUIRE(generateTerrainTraversalCosts(base, heights, base, s).ok());
    CHECK(base.height(1, 0) == 1);
    base.setHeight(0, 0, 0.5);
    CHECK(!generateTerrainTraversalCosts(out, heights, base, s).ok());
    s.slopeCost = std::numeric_limits<float>::infinity();
    CHECK(!generateTerrainTraversalCosts(out, heights, base, s).ok());
}
TEST_CASE("procgen.traversal.kilometrePathDetour") {
    constexpr int n = 513;
    Heightmap     heights(n, n), base(n, n), out(n, n);
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const float ridge = std::max(0.f, 25.f - std::abs(float(x) - 256));
            heights.setHeight(x, z, z > 380 ? 0.f : ridge * 2);
            base.setHeight(x, z, 1);
        }
    TerrainTraversalSettings s;
    s.spacingX = s.spacingZ = 2;
    s.maxSlopeDegrees       = 35;
    REQUIRE(generateTerrainTraversalCosts(out, heights, base, s).ok());
    eve::map::Pathfinder low(n, n);
    low.setDiagonal(false);
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) low.setCellCost(x, z, out.height(x, z));
    auto* detour = low.findPath(40, 100, 472, 100);
    REQUIRE(detour != nullptr);
    CHECK(detour->getLength() > 700);
    s.maxSlopeDegrees = 75;
    REQUIRE(generateTerrainTraversalCosts(out, heights, base, s).ok());
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) low.setCellCost(x, z, out.height(x, z));
    auto* direct = low.findPath(40, 100, 472, 100);
    REQUIRE(direct != nullptr);
    CHECK(direct->getLength() == 433);
}
