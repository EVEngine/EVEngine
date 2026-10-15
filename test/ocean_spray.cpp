#include <array>
#include <limits>
#include "fluids/OceanBoat.h"
#include "fluids/OceanSpray.h"
#include "fluids/OceanSprayRenderer.h"
#include "zeroerr/unittest.h"

using namespace eve::fluids;
namespace {
OceanSpectrumFrame sprayWater(double time) {
    OceanSpectrumBuilder b;
    b.settings.significantWaveHeight = 0;
    b.settings.bands                 = {{8, 32.0, 8.0, 32.0}};
    auto sea                         = b.build();
    REQUIRE(sea.ok());
    auto frame = sea.value().frameBuilder(time).build();
    REQUIRE(frame.ok());
    return std::move(frame).takeValue();
}
}  // namespace
TEST_CASE("fluids.ocean.sprayDeterminismAndSourceOrder") {
    OceanSpraySettings settings;
    settings.capacity = 128;
    auto initial      = OceanSprayFrame::create(settings);
    REQUIRE(initial.ok());
    OceanBoatReceipt a, b;
    a.contacts.push_back({{0, 0, 0}, {4, -2, 0}, 0.5});
    b.contacts.push_back({{3, 0, 0}, {0, -1, 5}, 0.5});
    const std::array<OceanSpraySource, 2> ab{{{1, a}, {2, b}}}, ba{{{2, b}, {1, a}}};
    auto                                  frame = sprayWater(0);
    OceanWaterView                        water(frame);
    auto                                  x = OceanSprayStepBuilder(initial.value(), water, ab, 1.0 / 30.0).build();
    auto                                  y = OceanSprayStepBuilder(initial.value(), water, ba, 1.0 / 30.0).build();
    REQUIRE(x.ok());
    REQUIRE(y.ok());
    CHECK(initial.value().particles().empty());
    CHECK(initial.value().time() == 0);
    REQUIRE(x.value().particles().size() > 0);
    REQUIRE(x.value().particles().size() == y.value().particles().size());
    for (unsigned i = 0; i < x.value().particles().size(); ++i) {
        const auto& p = x.value().particles()[i];
        const auto& q = y.value().particles()[i];
        CHECK(p.id == q.id);
        CHECK(glm::length(p.position - q.position) == 0);
        CHECK(glm::length(p.velocity - q.velocity) == 0);
        CHECK(p.position.y > 0);
    }
    settings.seed = 2;
    auto other    = OceanSprayFrame::create(settings);
    REQUIRE(other.ok());
    auto z = OceanSprayStepBuilder(other.value(), water, ab, 1.0 / 30.0).build();
    REQUIRE(z.ok());
    CHECK(glm::length(z.value().particles()[0].velocity - x.value().particles()[0].velocity) > 1e-4);
    auto           nextWater = sprayWater(x.value().time() + 1e-10);
    OceanWaterView nextView(nextWater);
    auto           next = OceanSprayStepBuilder(x.value(), nextView, {}, 1.0 / 30.0).build();
    REQUIRE(next.ok());
    CHECK(next.value().time() == nextWater.time() + 1.0 / 30.0);
    REQUIRE(next.value().particles().size() == x.value().particles().size());
    const auto& p = x.value().particles()[0];
    const auto& q = next.value().particles()[0];
    CHECK(glm::length(q.position - (p.position + p.velocity / 30.0 + glm::dvec3(0, -0.5 * 9.81 / 900, 0))) < 1e-12);
    CHECK(std::abs(q.velocity.y - (p.velocity.y - 9.81 / 30.0)) < 1e-12);
}
TEST_CASE("fluids.ocean.sprayBudgetsLifetimeAndReentry") {
    OceanSpraySettings settings;
    settings.capacity               = 2;
    settings.lifetime               = 0.1;
    settings.emissionPerVolumeSpeed = 1000;
    auto initial                    = OceanSprayFrame::create(settings);
    REQUIRE(initial.ok());
    OceanBoatReceipt boat;
    boat.contacts.push_back({{0, 0, 0}, {4, 0, 0}, 1});
    const std::array<OceanSpraySource, 1> sources{{{1, boat}}};
    auto                                  frame = sprayWater(0);
    OceanWaterView                        water(frame);
    auto emitted = OceanSprayStepBuilder(initial.value(), water, sources, 1.0 / 30.0).build();
    REQUIRE(emitted.ok());
    CHECK(emitted.value().report().emitted == 2);
    CHECK(emitted.value().report().dropped == 98);
    auto     current = std::move(emitted).takeValue();
    unsigned expired = 0;
    for (int step = 0; step < 8; ++step) {
        auto           f = sprayWater(current.time());
        OceanWaterView v(f);
        auto           next = OceanSprayStepBuilder(current, v, {}, 1.0 / 30.0).build();
        REQUIRE(next.ok());
        expired += next.value().report().expired;
        current = std::move(next).takeValue();
    }
    CHECK(current.particles().empty());
    CHECK(expired == 2);
    settings.lifetime = 10;
    auto longLife     = OceanSprayFrame::create(settings);
    REQUIRE(longLife.ok());
    auto shot = OceanSprayStepBuilder(longLife.value(), water, sources, 1.0 / 30.0).build();
    REQUIRE(shot.ok());
    current           = std::move(shot).takeValue();
    unsigned returned = 0;
    for (int step = 0; step < 90; ++step) {
        auto           f = sprayWater(current.time());
        OceanWaterView v(f);
        auto           next = OceanSprayStepBuilder(current, v, {}, 1.0 / 30.0).build();
        REQUIRE(next.ok());
        returned += next.value().report().returnedToWater;
        current = std::move(next).takeValue();
    }
    CHECK(returned == 2);
    CHECK(current.particles().empty());
}
TEST_CASE("fluids.ocean.sprayRejectsWholeInvalidBatch") {
    auto initial = OceanSprayFrame::create({});
    REQUIRE(initial.ok());
    OceanBoatReceipt good, bad;
    good.contacts.push_back({{0, 0, 0}, {5, 0, 0}, 1});
    bad                        = good;
    bad.contacts[0].position.x = std::numeric_limits<double>::quiet_NaN();
    const std::array<OceanSpraySource, 2> sources{{{1, good}, {2, bad}}}, duplicate{{{1, good}, {1, good}}};
    auto                                  frame = sprayWater(0);
    OceanWaterView                        water(frame);
    CHECK(!OceanSprayStepBuilder(initial.value(), water, sources, 1.0 / 30.0).build().ok());
    CHECK(!OceanSprayStepBuilder(initial.value(), water, duplicate, 1.0 / 30.0).build().ok());
    auto           stale = sprayWater(1);
    OceanWaterView oldWater(stale);
    CHECK(!OceanSprayStepBuilder(initial.value(), oldWater, {}, 1.0 / 30.0).build().ok());
    CHECK(initial.value().time() == 0);
    CHECK(initial.value().particles().empty());
    OceanSpraySettings badSettings;
    badSettings.capacity = 0;
    CHECK(!OceanSprayFrame::create(badSettings).ok());
}

TEST_CASE("fluids.ocean.sprayMeshBasisAndFloatingOrigin") {
    auto make = [&](glm::dvec3 origin) {
        auto initial = OceanSprayFrame::create({});
        REQUIRE(initial.ok());
        OceanBoatReceipt receipt;
        receipt.contacts.push_back({origin, {4, 0, 0}, 1});
        const std::array<OceanSpraySource, 1> sources{{{1, receipt}}};
        auto                                  waterFrame = sprayWater(0);
        OceanWaterView                        water(waterFrame);
        auto spray = OceanSprayStepBuilder(initial.value(), water, sources, 1.0 / 30.0).build();
        REQUIRE(spray.ok());
        OceanSprayMeshBuilder b(spray.value());
        b.view.eye    = origin + glm::dvec3(0, 0, 3);
        b.view.origin = origin;
        auto mesh     = b.build();
        REQUIRE(mesh.ok());
        b.view.up = b.view.forward;
        CHECK(!b.build().ok());
        return std::move(mesh).takeValue();
    };
    auto a = make({0, 0, 0}), b = make({1e8, 0, -1e8});
    REQUIRE(!a.positions.empty());
    REQUIRE(a.positions.size() == b.positions.size());
    CHECK(a.indices.size() == a.positions.size() / 12 * 6);
    for (unsigned i = 0; i < a.positions.size(); ++i) CHECK(std::abs(a.positions[i] - b.positions[i]) < 1e-6F);
    for (auto i : a.indices) CHECK(i < a.positions.size() / 3);
    CHECK(a.positions[0] < a.positions[3]);
    CHECK(a.positions[1] < a.positions[7]);
}
