#include <limits>
#include "fluids/OceanSeaTransition.h"
#include "zeroerr/unittest.h"

using namespace eve::fluids;
TEST_CASE("fluids.ocean.seaTransitionVelocityAndEndpoints") {
    OceanSpectrumBuilder source, target;
    source.settings.bands                 = {{16, 64, 8, 64}};
    source.settings.significantWaveHeight = 0.3;
    source.settings.current               = {0.0, 0.0};
    target.settings                       = source.settings;
    target.settings.significantWaveHeight = 0.8;
    target.settings.peakPeriod            = 4.0;
    target.settings.windDirection         = {0.2, 0.9};
    target.settings.seaLevel              = 0.7;
    target.settings.seed                  = 12;
    target.settings.choppiness            = 0.3;
    auto a = source.build(), b = target.build();
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    OceanSeaTransitionBuilder builder(a.value(), b.value());
    builder.start    = 10;
    builder.duration = 8;
    auto transition  = builder.build();
    REQUIRE(transition.ok());
    const glm::dvec2 q(3.25, 5.75);
    for (double time : {9.0, 10.0, 10.001, 12.0, 14.0, 17.999, 18.0, 19.0}) {
        const double dt    = 1e-4;
        auto         frame = transition.value().frameBuilder(time).build();
        REQUIRE(frame.ok());
        auto before = transition.value().frameBuilder(time - dt).build();
        REQUIRE(before.ok());
        auto after = transition.value().frameBuilder(time + dt).build();
        REQUIRE(after.ok());
        auto point = frame.value().position(q);
        REQUIRE(point.ok());
        auto sample = frame.value().sample({point.value().x, point.value().z});
        REQUIRE(sample.ok());
        auto p0 = before.value().position(q - source.settings.current * dt);
        REQUIRE(p0.ok());
        auto p1 = after.value().position(q + source.settings.current * dt);
        REQUIRE(p1.ok());
        CHECK(glm::length((p1.value() - p0.value()) / (2 * dt) - sample.value().velocity) < 2e-5);
        if (time <= 10 || time >= 18) {
            auto endpoint = (time <= 10 ? a.value() : b.value()).frameBuilder(time).build();
            REQUIRE(endpoint.ok());
            auto expected = endpoint.value().position(q);
            REQUIRE(expected.ok());
            CHECK(glm::length(point.value() - expected.value()) == 0);
        }
    }
    // At the midpoint smoothstep is 1/2, including the sea-level component.
    auto middle = transition.value().frameBuilder(14).build();
    auto fa = a.value().frameBuilder(14).build(), fb = b.value().frameBuilder(14).build();
    REQUIRE(middle.ok());
    REQUIRE(fa.ok());
    REQUIRE(fb.ok());
    auto pm = middle.value().position(q), pa = fa.value().position(q), pb = fb.value().position(q);
    REQUIRE(pm.ok());
    REQUIRE(pa.ok());
    REQUIRE(pb.ok());
    CHECK(glm::length(pm.value() - (pa.value() + pb.value()) * 0.5) < 1e-12);
}
TEST_CASE("fluids.ocean.seaTransitionValidationAndOwningBuilder") {
    auto make = [&] {
        OceanSpectrumBuilder s;
        s.settings.bands = {{8, 32, 8, 32}};
        auto a           = s.build();
        REQUIRE(a.ok());
        s.settings.significantWaveHeight = 0;
        auto b                           = s.build();
        REQUIRE(b.ok());
        auto transition = OceanSeaTransitionBuilder(a.value(), b.value()).build();
        REQUIRE(transition.ok());
        return transition.value().frameBuilder(5);
    };
    auto owned = make().build();
    REQUIRE(owned.ok());
    OceanSpectrumBuilder s;
    s.settings.bands = {{8, 32, 8, 32}};
    auto a           = s.build();
    REQUIRE(a.ok());
    OceanSeaTransitionBuilder transition(a.value(), a.value());
    transition.duration = 0;
    CHECK(!transition.build().ok());
    transition.duration = 1;
    transition.start    = 1e9;
    CHECK(!transition.build().ok());
    transition.start = 0;
    auto valid       = transition.build();
    REQUIRE(valid.ok());
    CHECK(!valid.value().frameBuilder(std::numeric_limits<double>::quiet_NaN()).build().ok());
    s.settings.current.x = 1;
    auto current         = s.build();
    REQUIRE(current.ok());
    CHECK(!OceanSeaTransitionBuilder(a.value(), current.value()).build().ok());
    s.settings.current.x           = 0;
    s.settings.bands[0].resolution = 16;
    auto layout                    = s.build();
    REQUIRE(layout.ok());
    CHECK(!OceanSeaTransitionBuilder(a.value(), layout.value()).build().ok());
}
TEST_CASE("fluids.ocean.seaTransitionCurrentPreservesEndpointVelocityContract") {
    OceanSpectrumBuilder builder;
    builder.settings.bands                 = {{16, 64, 8, 64}};
    builder.settings.current               = {0.4, -0.2};
    builder.settings.significantWaveHeight = 0.3;
    auto a                                 = builder.build();
    REQUIRE(a.ok());
    builder.settings.significantWaveHeight = 0.8;
    builder.settings.windDirection         = {0.3, 0.8};
    auto b                                 = builder.build();
    REQUIRE(b.ok());
    OceanSeaTransitionBuilder transitionBuilder(a.value(), b.value());
    transitionBuilder.start    = 2;
    transitionBuilder.duration = 6;
    auto transition            = transitionBuilder.build();
    REQUIRE(transition.ok());
    const double time = 4, dt = 1e-4;
    auto         weight = [](double t) {
        double u = (t - 2) / 6;
        return u * u * u * (10 + u * (-15 + 6 * u));
    };
    const double w = weight(time), rate = (weight(time + dt) - weight(time - dt)) / (2 * dt);
    auto         fa = a.value().frameBuilder(time).build(), fb = b.value().frameBuilder(time).build();
    auto         actual = transition.value().frameBuilder(time).build();
    REQUIRE(fa.ok());
    REQUIRE(fb.ok());
    REQUIRE(actual.ok());
    const auto& ga     = fa.value().grids()[0];
    const auto& gb     = fb.value().grids()[0];
    const auto& result = actual.value().grids()[0];
    for (std::size_t i = 0; i < result.velocity.size(); ++i) {
        auto expected =
            (1 - w) * ga.velocity[i] + w * gb.velocity[i] + rate * (gb.displacement[i] - ga.displacement[i]);
        CHECK(glm::length(result.velocity[i] - expected) < 1e-8);
    }
}
