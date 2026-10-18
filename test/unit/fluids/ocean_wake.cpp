#include "fluids/OceanBoat.h"
#include "fluids/OceanWake.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>

using namespace eve::fluids;
namespace {
eve::Result<void> advanceWake(OceanWake& wake, double dt, double endTime, const OceanSpectrumFrame& frame) {
    auto prepared = wake.prepareStep(dt, endTime, frame);
    if (!prepared) return eve::Result<void>::failure(prepared.status());
    return wake.commitStep(*prepared.value());
}
}  // namespace

namespace {
OceanSpectrumFrame stationaryFrame(double time) {
    OceanSpectrumBuilder builder;
    builder.settings.significantWaveHeight = 0.0;
    builder.settings.bands                 = {{8, 32.0, 8.0, 32.0}};
    auto sea                               = builder.build();
    REQUIRE(sea.ok());
    auto frame = sea.value().frameBuilder(time).build();
    REQUIRE(frame.ok());
    return std::move(frame).takeValue();
}
}  // namespace

TEST_CASE("fluids.ocean.wakePropagatesAndRejectsAtomically") {
    OceanWakeSettings settings;
    settings.resolution      = 32;
    settings.length          = 64.0;
    settings.absorptionWidth = 8.0;
    auto wake                = OceanWake::create(settings);
    REQUIRE(wake.ok());
    std::array<OceanWakeImpulse, 2> bad{
        {{{0.0, 0.0}, 2.0, 1.0, {0.0, 0.0}, 0.5}, {{0.0, 0.0}, 0.0, 1.0, {0.0, 0.0}, 0.5}}};
    auto invalid = wake.value()->inject(bad);
    CHECK(!invalid.ok());
    auto untouched = wake.value()->sample({0.0, 0.0});
    REQUIRE(untouched.ok());
    CHECK(untouched.value().velocity == 0.0);
    auto injected = wake.value()->inject(std::span<const OceanWakeImpulse>(bad.data(), 1));
    REQUIRE(injected.ok());
    auto initial = wake.value()->sample({0.0, 0.0});
    REQUIRE(initial.ok());
    CHECK(initial.value().velocity == 1.0);
    CHECK(initial.value().foam == 0.5);
    double propagated = 0.0;
    for (int step = 1; step <= 120; ++step) {
        auto advanced = advanceWake(*wake.value(), 1.0 / 60.0, step / 60.0, stationaryFrame(wake.value()->time()));
        REQUIRE(advanced.ok());
        auto neighbor = wake.value()->sample({6.0, 0.0});
        REQUIRE(neighbor.ok());
        propagated = std::max(propagated, std::abs(neighbor.value().height));
    }
    CHECK(propagated > 0.01);
    auto later = wake.value()->sample({0.0, 0.0});
    REQUIRE(later.ok());
    CHECK(later.value().foam < 0.2);
    const double before  = later.value().height;
    auto         badStep = advanceWake(*wake.value(), -1.0, 3.0, stationaryFrame(wake.value()->time()));
    CHECK(!badStep.ok());
    auto unchanged = wake.value()->sample({0.0, 0.0});
    REQUIRE(unchanged.ok());
    CHECK(unchanged.value().height == before);
    auto edge = wake.value()->sample({-32.0, 0.0});
    REQUIRE(edge.ok());
    CHECK(edge.value().height == 0.0);
    CHECK(glm::length(edge.value().gradient) == 0.0);

    // A wave without injected foam must create white water on its steep crest;
    // a resting field must remain clear. The solver still owns propagation.
    auto breaking = OceanWake::create(settings), calm = OceanWake::create(settings);
    REQUIRE(breaking.ok());
    REQUIRE(calm.ok());
    const OceanWakeImpulse crest{{0.0, 0.0}, 2.0, 5.0, {0.0, 0.0}, 0.0};
    REQUIRE(breaking.value()->inject(std::span(&crest, 1)).ok());
    double whiteWater = 0.0;
    for (int step = 1; step <= 60; ++step) {
        auto frame = stationaryFrame((step - 1) / 60.0);
        REQUIRE(advanceWake(*breaking.value(), 1.0 / 60.0, step / 60.0, frame).ok());
        REQUIRE(advanceWake(*calm.value(), 1.0 / 60.0, step / 60.0, frame).ok());
        for (double f : breaking.value()->gridView().foam) {
            CHECK((f >= 0.0 && f <= 1.0));
            whiteWater = std::max(whiteWater, f);
        }
    }
    CHECK(whiteWater > 0.1);
    for (double f : calm.value()->gridView().foam) CHECK(f == 0.0);
}

TEST_CASE("fluids.ocean.wakeRecenterAndComposition") {
    OceanWakeSettings settings;
    settings.resolution      = 32;
    settings.length          = 64.0;
    settings.absorptionWidth = 8.0;
    auto wake                = OceanWake::create(settings);
    REQUIRE(wake.ok());
    OceanBoatReceipt receipt;
    receipt.contacts.push_back({{0.0, 0.0, 0.0}, {4.0, -1.0, 0.0}, 2.0});
    auto injected = injectOceanBoatWake(*wake.value(), receipt, 1.0 / 60.0);
    REQUIRE(injected.ok());
    auto advanced = advanceWake(*wake.value(), 1.0 / 60.0, 1.0 / 60.0, stationaryFrame(0.0));
    REQUIRE(advanced.ok());
    auto original = wake.value()->sample({2.0, 0.0});
    REQUIRE(original.ok());
    CHECK(std::abs(original.value().height) > 1e-5);
    auto moved = wake.value()->recenter({4.0, -2.0});
    REQUIRE(moved.ok());
    auto same = wake.value()->sample({2.0, 0.0});
    REQUIRE(same.ok());
    CHECK(std::abs(original.value().height - same.value().height) < 1e-12);
    OceanSpectrumBuilder builder;
    builder.settings.bands = {{16, 64.0, 8.0, 64.0}};
    auto sea               = builder.build();
    REQUIRE(sea.ok());
    auto frame = sea.value().frameBuilder(wake.value()->time()).build();
    REQUIRE(frame.ok());
    OceanWaterView water(frame.value(), *wake.value());
    auto           point = water.position({2.31, 0.77});
    REQUIRE(point.ok());
    auto sample = water.sample({point.value().x, point.value().z});
    REQUIRE(sample.ok());
    CHECK(std::abs(point.value().y - sample.value().height) < 1e-5);
    auto px = water.position({2.31001, 0.77});
    auto pz = water.position({2.31, 0.77001});
    REQUIRE(px.ok());
    REQUIRE(pz.ok());
    const auto normal = glm::normalize(glm::cross(pz.value() - point.value(), px.value() - point.value()));
    CHECK(glm::length(normal - sample.value().normal) < 1e-4);
    auto geometry = water.geometry({2.31, 0.77});
    REQUIRE(geometry.ok());
    CHECK(glm::length(geometry.value().position - point.value()) < 1e-12);
    CHECK(glm::length(geometry.value().normal() - normal) < 1e-4);
    CHECK(glm::length(geometry.value().normal() - sample.value().normal) < 1e-5);
    auto staleReceipt = injectOceanBoatWake(*wake.value(), receipt, 1.0 / 60.0);
    CHECK(!staleReceipt.ok());
    auto next = advanceWake(*wake.value(), 1.0 / 60.0, 2.0 / 60.0, frame.value());
    REQUIRE(next.ok());
    auto staleView = water.sample({0.0, 0.0});
    CHECK(!staleView.ok());
    CHECK(!water.geometry({0.0, 0.0}).ok());
    auto far = wake.value()->recenter({1000.0, 1000.0});
    REQUIRE(far.ok());
    auto cleared = wake.value()->sample({1000.0, 1000.0});
    REQUIRE(cleared.ok());
    CHECK(cleared.value().height == 0.0);

    // A subdivided large hull shares one forcing budget, rather than injecting
    // sixteen complete wakes and hitting the field's velocity/foam clamps.
    auto largeHullWake = OceanWake::create(settings);
    REQUIRE(largeHullWake.ok());
    OceanBoatReceipt largeHull;
    for (int cell = 0; cell < 16; ++cell) largeHull.contacts.push_back({{0.0, 0.0, 0.0}, {0.0, 0.0, 12.0}, 500.0});
    REQUIRE(injectOceanBoatWake(*largeHullWake.value(), largeHull, 1.0 / 60.0).ok());
    double maxVelocity = 0.0, maxFoam = 0.0;
    for (int z = -24; z <= 24; z += 2) {
        auto point = largeHullWake.value()->sample({0.0, double(z)});
        REQUIRE(point.ok());
        maxVelocity = std::max(maxVelocity, std::abs(point.value().velocity));
        maxFoam     = std::max(maxFoam, point.value().foam);
    }
    CHECK(maxVelocity > 0.1);
    CHECK(maxVelocity < 0.25);
    CHECK(maxFoam <= 0.020001);
    largeHull.contacts.back().position.x = std::numeric_limits<double>::quiet_NaN();
    auto beforeInvalid                   = largeHullWake.value()->sample({0.0, 8.0});
    REQUIRE(beforeInvalid.ok());
    CHECK(!injectOceanBoatWake(*largeHullWake.value(), largeHull, 1.0 / 60.0).ok());
    auto afterInvalid = largeHullWake.value()->sample({0.0, 8.0});
    REQUIRE(afterInvalid.ok());
    CHECK(afterInvalid.value().velocity == beforeInvalid.value().velocity);
    CHECK(afterInvalid.value().foam == beforeInvalid.value().foam);

    for (double direction : {1.0, -1.0}) {
        auto bowWake = OceanWake::create(settings);
        REQUIRE(bowWake.ok());
        OceanBoatReceipt hull;
        for (double along : {-12.0, -4.0, 4.0, 12.0})
            for (double side : {-2.0, 2.0})
                hull.contacts.push_back({{side, 0.0, along}, {0.0, 0.0, 12.0 * direction}, 8.0});
        REQUIRE(injectOceanBoatWake(*bowWake.value(), hull, 1.0 / 60.0).ok());
        auto leading  = bowWake.value()->sample({0.0, 14.0 * direction});
        auto trailing = bowWake.value()->sample({0.0, -10.0 * direction});
        REQUIRE(leading.ok());
        REQUIRE(trailing.ok());
        CHECK(leading.value().velocity > 0.05);
        CHECK(leading.value().velocity > 10.0 * std::abs(trailing.value().velocity));
    }
}

TEST_CASE("fluids.ocean.wakeDispersionMatchesIndependentDft") {
    OceanWakeSettings settings;
    settings.resolution      = 16;
    settings.length          = 32.0;
    settings.absorptionWidth = 4.0;
    settings.depth           = 3.0;
    settings.damping         = 0.0;
    auto wake                = OceanWake::create(settings);
    REQUIRE(wake.ok());
    const std::array<OceanWakeImpulse, 1> impulses{{{{0.0, 0.0}, 2.0, 1.0, {0.0, 0.0}, 0.0}}};
    auto                                  injected = wake.value()->inject(impulses);
    REQUIRE(injected.ok());
    constexpr unsigned        n   = 16;
    constexpr double          tau = 2.0 * std::numbers::pi;
    std::array<double, n * n> velocity{};
    for (unsigned z = 0; z < n; ++z)
        for (unsigned x = 0; x < n; ++x) {
            auto v = wake.value()->sample({double(x) * 2.0 - 16.0, double(z) * 2.0 - 16.0});
            REQUIRE(v.ok());
            velocity[z * n + x] = v.value().velocity;
        }
    std::complex<double> expected{};
    constexpr double     dt = 1.0 / 60.0;
    for (unsigned kz = 0; kz < n; ++kz) {
        for (unsigned kx = 0; kx < n; ++kx) {
            const int    ix = kx < n / 2 ? int(kx) : int(kx) - int(n);
            const int    iz = kz < n / 2 ? int(kz) : int(kz) - int(n);
            const double k  = std::hypot(double(ix), double(iz)) * tau / 32.0;
            if (k == 0.0) continue;
            std::complex<double> coefficient{};
            for (unsigned z = 0; z < n; ++z)
                for (unsigned x = 0; x < n; ++x)
                    coefficient += velocity[z * n + x] * std::polar(1.0, -tau * (kx * x + kz * z) / n) / double(n * n);
            const double omega = std::sqrt(9.81 * k * std::tanh(k * settings.depth));
            expected += coefficient * (std::sin(omega * dt) / omega) * std::polar(1.0, tau * (kx + kz) * 0.5);
        }
    }
    auto advanced = advanceWake(*wake.value(), dt, dt, stationaryFrame(0.0));
    REQUIRE(advanced.ok());
    auto actual = wake.value()->sample({0.0, 0.0});
    REQUIRE(actual.ok());
    CHECK(std::abs(expected.imag()) < 1e-10);
    CHECK(std::abs(actual.value().height - expected.real()) < 1e-10);
}

TEST_CASE("fluids.ocean.foamTransportMatchesCurrentAndDecay") {
    OceanWakeSettings settings;
    settings.resolution      = 32;
    settings.length          = 32.0;
    settings.absorptionWidth = 4.0;
    settings.foamDecay       = 0.7;
    auto wake                = OceanWake::create(settings);
    REQUIRE(wake.ok());
    const std::array<OceanWakeImpulse, 1> source{{{{-3.0, 2.0}, 1.0, 0.0, {0.0, 0.0}, 0.8}}};
    REQUIRE(wake.value()->inject(source).ok());
    const auto                grid = wake.value()->gridView();
    const std::vector<double> old(grid.foam.begin(), grid.foam.end());
    OceanSpectrumBuilder      builder;
    builder.settings.significantWaveHeight = 0.0;
    builder.settings.current               = {30.0, -30.0};
    builder.settings.bands                 = {{8, 32.0, 8.0, 32.0}};
    auto sea                               = builder.build();
    REQUIRE(sea.ok());
    auto frame = sea.value().frameBuilder(0.0).build();
    REQUIRE(frame.ok());
    constexpr double dt = 1.0 / 30.0;
    REQUIRE(advanceWake(*wake.value(), dt, dt, frame.value()).ok());
    const auto moved = wake.value()->gridView();
    for (unsigned z = 0; z < 32; ++z)
        for (unsigned x = 0; x < 32; ++x) {
            const double edge   = double(std::min({x, z, 31 - x, 31 - z}));
            const double sponge = std::clamp(1.0 - edge / 4.0, 0.0, 1.0);
            const double expected =
                x > 0 && z < 31 ? old[(z + 1) * 32 + x - 1] * std::exp(-(0.7 + 8.0 * sponge * sponge) * dt) : 0.0;
            CHECK(std::abs(moved.foam[z * 32 + x] - expected) < 1e-12);
        }
    auto peak = wake.value()->sample({-2.0, 1.0});
    REQUIRE(peak.ok());
    CHECK(std::abs(peak.value().foam - 0.8 * std::exp(-0.7 * dt)) < 1e-12);
    // Outgoing history leaves the tile; no periodic wrap injects foam upstream.
    for (int step = 2; step <= 24; ++step) {
        auto start = sea.value().frameBuilder((step - 1) * dt).build();
        REQUIRE(start.ok());
        REQUIRE(advanceWake(*wake.value(), dt, step * dt, start.value()).ok());
    }
    for (double f : wake.value()->gridView().foam) CHECK((f >= 0.0 && f < 1e-8));
}

TEST_CASE("fluids.ocean.foamTransportFailurePreservesWholeStep") {
    OceanWakeSettings settings;
    settings.resolution      = 32;
    settings.length          = 32.0;
    settings.absorptionWidth = 4.0;
    settings.center          = {1e9 - 16.0, 0.0};
    auto wake                = OceanWake::create(settings);
    REQUIRE(wake.ok());
    const std::array<OceanWakeImpulse, 1> source{{{settings.center, 1.0, 1.0, {0.0, 0.0}, 0.8}}};
    REQUIRE(wake.value()->inject(source).ok());
    const auto                grid = wake.value()->gridView();
    const std::vector<double> foam(grid.foam.begin(), grid.foam.end());
    const std::vector<double> height(grid.height.begin(), grid.height.end());
    OceanSpectrumBuilder      builder;
    builder.settings.significantWaveHeight = 0.0;
    builder.settings.current               = {-100.0, 0.0};
    builder.settings.bands                 = {{8, 32.0, 8.0, 32.0}};
    auto sea                               = builder.build();
    REQUIRE(sea.ok());
    auto stale = sea.value().frameBuilder(1.0).build();
    REQUIRE(stale.ok());
    CHECK(!advanceWake(*wake.value(), 1.0 / 30.0, 1.0 / 30.0, stale.value()).ok());
    auto frame = sea.value().frameBuilder(0.0).build();
    REQUIRE(frame.ok());
    // A later cell's midpoint leaves the supported world domain, after earlier candidates were sampled.
    CHECK(!advanceWake(*wake.value(), 1.0 / 30.0, 1.0 / 30.0, frame.value()).ok());
    CHECK(wake.value()->time() == 0.0);
    const auto after = wake.value()->gridView();
    CHECK(std::equal(foam.begin(), foam.end(), after.foam.begin()));
    CHECK(std::equal(height.begin(), height.end(), after.height.begin()));
    auto sample = wake.value()->sample(settings.center);
    REQUIRE(sample.ok());
    CHECK(sample.value().velocity == 1.0);
}

TEST_CASE("fluids.ocean.wakePreparedStepIsOwningAndRevisionChecked") {
    OceanWakeSettings settings;
    settings.resolution      = 16;
    settings.length          = 32.0;
    settings.absorptionWidth = 4.0;
    auto wake                = OceanWake::create(settings);
    auto foreign             = OceanWake::create(settings);
    REQUIRE(wake.ok());
    REQUIRE(foreign.ok());
    auto step = wake.value()->prepareStep(1.0 / 60.0, 1.0 / 60.0, stationaryFrame(0.0));
    REQUIRE(step.ok());
    CHECK(wake.value()->time() == 0.0);
    CHECK(!foreign.value()->commitStep(*step.value()).ok());
    REQUIRE(wake.value()->commitStep(*step.value()).ok());
    CHECK(wake.value()->time() == 1.0 / 60.0);
    CHECK(!wake.value()->commitStep(*step.value()).ok());
    auto a = wake.value()->prepareStep(1.0 / 60.0, 2.0 / 60.0, stationaryFrame(1.0 / 60.0));
    auto b = wake.value()->prepareStep(1.0 / 60.0, 2.0 / 60.0, stationaryFrame(1.0 / 60.0));
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(wake.value()->commitStep(*a.value()).ok());
    CHECK(!wake.value()->commitStep(*b.value()).ok());
    auto moved = wake.value()->prepareStep(1.0 / 60.0, 3.0 / 60.0, stationaryFrame(2.0 / 60.0));
    REQUIRE(moved.ok());
    REQUIRE(wake.value()->recenter({2.0, 0.0}).ok());
    CHECK(!wake.value()->commitStep(*moved.value()).ok());
    auto injected = wake.value()->prepareStep(1.0 / 60.0, 3.0 / 60.0, stationaryFrame(2.0 / 60.0));
    REQUIRE(injected.ok());
    const std::array<OceanWakeImpulse, 1> source{{{{0.0, 0.0}, 2.0, 1.0, {0.0, 0.0}, 0.5}}};
    REQUIRE(wake.value()->inject(source).ok());
    CHECK(!wake.value()->commitStep(*injected.value()).ok());
    CHECK(wake.value()->time() == 2.0 / 60.0);
    auto orphan = wake.value()->prepareStep(1.0 / 60.0, 3.0 / 60.0, stationaryFrame(2.0 / 60.0));
    REQUIRE(orphan.ok());
    wake.value().reset();
    CHECK(!foreign.value()->commitStep(*orphan.value()).ok());
}
