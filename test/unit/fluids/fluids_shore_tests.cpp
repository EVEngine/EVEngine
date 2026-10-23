#include "fluids/ShallowWaterField.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace {
using namespace eve::fluids;
std::unique_ptr<ShallowWaterField> shoreField() {
    ShallowWaterSettings s;
    s.resolutionX = 64;
    s.resolutionZ = 8;
    s.cellSize    = 0.1f;
    s.preferGpu   = false;
    auto made     = ShallowWaterField::create(s);
    REQUIRE(made.ok());
    auto               field = std::move(made).takeValue();
    std::vector<float> bed(512);
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 64; ++x) bed[size_t(z * 64 + x)] = std::max(0.f, ((float(x) + 0.5f) * 0.1f - 2.f) * 0.3f);
    REQUIRE(field->setBed(bed).ok());
    REQUIRE(field->fillSurface(0.4f).ok());
    return field;
}
}  // namespace

TEST_CASE("fluids.shore.levelLakeStaysAtRestOverWetDrySlope") {
    auto                     field = shoreField();
    const std::vector<float> before(field->depths().begin(), field->depths().end());
    for (int i = 0; i < 240; ++i) REQUIRE(field->step(1.f / 60.f).ok());
    for (size_t i = 0; i < before.size(); ++i) {
        REQUIRE(std::abs(field->depths()[i] - before[i]) < 0.00001f);
        REQUIRE(std::abs(field->momentumX()[i]) < 0.00001f);
        REQUIRE(std::abs(field->momentumZ()[i]) < 0.00001f);
    }
}

TEST_CASE("fluids.shore.invalidInitializationIsAtomic") {
    auto                     field = shoreField();
    const std::vector<float> before(field->depths().begin(), field->depths().end());
    REQUIRE(!field->fillSurface(std::numeric_limits<float>::quiet_NaN()).ok());
    REQUIRE(!field->fillSurface(20.f).ok());
    auto beds = std::vector<float>(field->beds().begin(), field->beds().end());
    beds[20]  = std::numeric_limits<float>::infinity();
    REQUIRE(!field->setBed(beds).ok());
    REQUIRE(std::equal(before.begin(), before.end(), field->depths().begin()));
    REQUIRE(std::isfinite(field->beds()[20]));
}

TEST_CASE("fluids.shore.waveRunsUpAndReturnsWithoutNegativeDepth") {
    auto field = shoreField();
    REQUIRE(field->disturbAt(2.9f, 0.4f, 0.4f, 0.12f, 0.4f).ok());
    double mass0 = std::accumulate(field->depths().begin(), field->depths().end(), 0.0);
    float  runup = 0.f, returnVelocity = 0.f, peakDepth = 0.f;
    for (int tick = 0; tick < 480; ++tick) {
        REQUIRE(field->step(1.f / 120.f).ok());
        // First initially dry cell: bed=0.405, 5 mm above the rest surface.
        runup     = std::max(runup, field->depthAt(3.35f, 0.4f));
        peakDepth = std::max(peakDepth, field->depthAt(3.05f, 0.4f));
        if (tick > 30) returnVelocity = std::min(returnVelocity, field->velocityAt(3.05f, 0.4f).x);
        for (float h : field->depths()) {
            REQUIRE(std::isfinite(h));
            REQUIRE(h >= 0.f);
        }
    }
    const double mass1 = std::accumulate(field->depths().begin(), field->depths().end(), 0.0);
    REQUIRE(runup > 0.001f);
    REQUIRE(returnVelocity < -0.005f);
    REQUIRE(field->depthAt(3.05f, 0.4f) < peakDepth);
    REQUIRE(std::abs(mass1 - mass0) < mass0 * 0.02);
}

TEST_CASE("fluids.shore.visualGainPreservesLevelRestSurface") {
    auto                  field = shoreField();
    std::vector<float>    positions, normals, uvs;
    std::vector<uint32_t> indices;
    REQUIRE(field->buildSurfaceMesh(positions, normals, uvs, indices, 2.5f).ok());
    for (size_t i = 0; i < field->depths().size(); ++i) {
        const float expected = std::max(0.4f, field->beds()[i]);
        REQUIRE(std::abs(positions[i * 3 + 1] - expected) < 0.00001f);
    }
}

TEST_CASE("fluids.shore.airAndDryLandCollidersDoNotCreateWater") {
    auto                              field = shoreField();
    eve::fluids::ShallowWaterCollider air;
    air.label    = 1;
    air.center   = {1.f, 3.f, 0.4f};
    air.velocity = {1.f, -1.f, 0.f};
    eve::fluids::ShallowWaterCollider land;
    land.label    = 2;
    land.center   = {5.f, 0.95f, 0.4f};
    land.velocity = {1.f, -1.f, 0.f};
    const std::vector<eve::fluids::ShallowWaterCollider> colliders{air, land};
    REQUIRE(field->setColliders(colliders).ok());
    const std::vector<float> before(field->depths().begin(), field->depths().end());
    for (int i = 0; i < 60; ++i) REQUIRE(field->step(1.f / 60.f).ok());
    for (size_t i = 0; i < before.size(); ++i) REQUIRE(std::abs(before[i] - field->depths()[i]) < 0.00001f);
}
