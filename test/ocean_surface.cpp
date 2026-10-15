#include "fluids/OceanSurface.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

using namespace eve::fluids;

TEST_CASE("fluids.ocean.displacedWorldQueryMatchesGeometry") {
    OceanSurfaceSettings settings;
    settings.waves   = {{{1.0, 0.3}, 0.8, 18.0, 0.4, 0.7}, {{-0.4, 1.0}, 0.2, 7.0, 1.2, 0.4}};
    settings.current = {0.7, -0.3};
    auto ocean       = OceanSurface::create(settings);
    REQUIRE(ocean.ok());
    for (double time : {0.0, 1.5, 17.0}) {
        for (double x : {-40.0, 0.0, 13.0, 100000.0}) {
            const glm::dvec2 q{x, 2.7};
            auto             point = ocean.value().position(q, time);
            REQUIRE(point.ok());
            auto sample = ocean.value().sample({point.value().x, point.value().z}, time);
            REQUIRE(sample.ok());
            CHECK(std::abs(sample.value().height - point.value().y) < 1e-6);
            CHECK(glm::length(sample.value().parameterPosition - q) < 1e-6);
            CHECK(sample.value().normal.y > 0.0);
            CHECK(sample.value().horizontalJacobian > 0.0);
            const double eps = 1e-4;
            auto         px  = ocean.value().position(q + glm::dvec2(eps, 0.0), time);
            auto         pz  = ocean.value().position(q + glm::dvec2(0.0, eps), time);
            REQUIRE(px.ok());
            REQUIRE(pz.ok());
            auto normal = glm::normalize(glm::cross(pz.value() - point.value(), px.value() - point.value()));
            CHECK(glm::length(normal - sample.value().normal) < 1e-4);
            auto next = ocean.value().position(q + settings.current * eps, time + eps);
            REQUIRE(next.ok());
            CHECK(glm::length((next.value() - point.value()) / eps - sample.value().velocity) < 1e-3);
        }
    }
}

TEST_CASE("fluids.ocean.rejectsFoldedAndNonfiniteInputs") {
    OceanSurfaceSettings settings;
    settings.waves = {{{1.0, 0.0}, 1.0, 1.0, 0.0, 1.0}};
    auto folded    = OceanSurface::create(settings);
    CHECK(!folded.ok());
    settings.waves.clear();
    auto ocean = OceanSurface::create(settings);
    REQUIRE(ocean.ok());
    auto bad = ocean.value().sample({0.0, 0.0}, std::numeric_limits<double>::quiet_NaN());
    CHECK(!bad.ok());
    auto still = ocean.value().sample({123.0, -44.0}, 900.0);
    REQUIRE(still.ok());
    CHECK(still.value().height == 0.0);
    CHECK(still.value().normal.y == 1.0);
}

TEST_CASE("fluids.ocean.buoyancyVolumeAndDrag") {
    OceanBuoyancyCell cell;
    cell.radius = 1.0;
    cell.volume = 2.0;
    OceanSurfaceSample water;
    auto               half = evaluateOceanCellForce(cell, water, 1000.0, 9.81);
    REQUIRE(half.ok());
    CHECK(std::abs(half.value().submergedVolume - 1.0) < 1e-12);
    CHECK(std::abs(half.value().force.y - 9810.0) < 1e-8);
    cell.center.y = 2.0;
    auto dry      = evaluateOceanCellForce(cell, water, 1000.0, 9.81);
    REQUIRE(dry.ok());
    CHECK(glm::length(dry.value().force) == 0.0);
    cell.center.y        = -2.0;
    cell.pointVelocity.x = 3.0;
    water.velocity.x     = 1.0;
    auto submerged       = evaluateOceanCellForce(cell, water, 1000.0, 9.81);
    REQUIRE(submerged.ok());
    CHECK(submerged.value().submergedVolume == 2.0);
    CHECK(submerged.value().force.x < 0.0);
    CHECK(std::abs(submerged.value().force.y - 19620.0) < 1e-8);
    cell.pointVelocity = water.velocity;
    auto drifting      = evaluateOceanCellForce(cell, water, 1000.0, 9.81);
    REQUIRE(drifting.ok());
    CHECK(drifting.value().force.x == 0.0);
    cell.radius  = 0.0;
    auto invalid = evaluateOceanCellForce(cell, water, 1000.0, 9.81);
    CHECK(!invalid.ok());
}
