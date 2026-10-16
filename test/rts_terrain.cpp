#include "rts/RTSTerrainTraceInternal.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using eve::rts::terrain_internal::firstTerrainImpact;

TEST_CASE("rts.terrainNarrowRidgeBlocksShotsInBothDirections") {
    const auto ridge = [](int x, int y) { return x == 1 && y == 0 ? 3.0f : 0.0f; };
    // This ray crosses cell (1,0) for less than 0.02 world units.
    const auto forward = firstTerrainImpact(0.1f, 0.10f, 1.f, 2.9f, 2.88f, 1.f, 0.f, 0.f, 1.f, ridge);
    const auto reverse = firstTerrainImpact(2.9f, 2.88f, 1.f, 0.1f, 0.10f, 1.f, 0.f, 0.f, 1.f, ridge);
    REQUIRE(forward.has_value());
    REQUIRE(reverse.has_value());
    CHECK(*forward < 0.33f);
    CHECK(*reverse > 0.67f);
}

TEST_CASE("rts.terrainHighGroundCanShootOverLowerRidge") {
    const auto ridge = [](int x, int) { return x == 1 ? 2.f : 0.f; };
    CHECK(firstTerrainImpact(0.5f, 0.5f, 1.f, 3.5f, 0.5f, 1.f, 0.f, 0.f, 1.f, ridge).has_value());
    CHECK(!firstTerrainImpact(0.5f, 0.5f, 5.f, 3.5f, 0.5f, 1.f, 0.f, 0.f, 1.f, ridge).has_value());
}

TEST_CASE("rts.terrainDescendingProjectileHitsInsideCell") {
    const auto ground = [](int, int) { return 0.f; };
    const auto impact = firstTerrainImpact(0.1f, 0.5f, 1.f, 0.9f, 0.5f, -1.f, 0.f, 0.f, 1.f, ground);
    REQUIRE(impact.has_value());
    CHECK(std::abs(*impact - 0.5f) < 0.00001f);
    CHECK(!firstTerrainImpact(0.1f, 0.5f, 1.f, 0.9f, 0.5f, 0.f, 0.f, 0.f, 1.f, ground).has_value());
}

TEST_CASE("rts.terrainGridOriginScaleAndVerticalSegments") {
    const auto ridge  = [](int x, int y) { return x == 1 && y == 0 ? 3.f : 0.f; };
    const auto impact = firstTerrainImpact(-9.f, 5.f, 1.f, -3.f, 5.f, 1.f, -10.f, 4.f, 2.f, ridge);
    REQUIRE(impact.has_value());
    CHECK(std::abs(*impact - 1.f / 6.f) < 0.00001f);
    const auto ground = [](int, int) { return 0.f; };
    REQUIRE(firstTerrainImpact(1.f, 1.f, 1.f, 1.f, 1.f, -1.f, 0.f, 0.f, 1.f, ground).has_value());
    CHECK(!firstTerrainImpact(1.f, 1.f, 1.f, 1.f, 1.f, 2.f, 0.f, 0.f, 1.f, ground).has_value());
}
