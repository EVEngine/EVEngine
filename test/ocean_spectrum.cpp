#include "fluids/OceanSpectrum.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <complex>
#include <limits>
#include <numbers>

using namespace eve::fluids;

TEST_CASE("fluids.ocean.spectrumEnergyPeriodicityAndSeed") {
    OceanSpectrumBuilder builder;
    builder.settings.bands      = {{16, 64.0, 8.0, 64.0}};
    builder.settings.choppiness = 0.2;
    auto spectrum               = builder.build();
    REQUIRE(spectrum.ok());
    CHECK(std::abs(spectrum.value().variance() - 0.25) < 1e-12);
    auto frame = spectrum.value().frameBuilder(1.25).build();
    REQUIRE(frame.ok());
    const auto grid = frame.value().grids()[0];
    double     mean = 0.0, energy = 0.0;
    for (const auto& displacement : grid.displacement) {
        mean += displacement.y;
        energy += displacement.y * displacement.y;
    }
    mean /= grid.displacement.size();
    energy /= grid.displacement.size();
    CHECK(std::abs(mean) < 1e-12);
    CHECK(std::abs(energy - spectrum.value().variance()) < 1e-10);
    auto repeatedSpectrum = builder.build();
    REQUIRE(repeatedSpectrum.ok());
    auto repeated = repeatedSpectrum.value().frameBuilder(1.25).build();
    REQUIRE(repeated.ok());
    CHECK(glm::length(grid.displacement[13] - repeated.value().grids()[0].displacement[13]) < 1e-12);
    builder.settings.seed = 9;
    auto otherSpectrum    = builder.build();
    REQUIRE(otherSpectrum.ok());
    auto other = otherSpectrum.value().frameBuilder(1.25).build();
    REQUIRE(other.ok());
    CHECK(glm::length(grid.displacement[13] - other.value().grids()[0].displacement[13]) > 1e-3);
    for (glm::dvec2 q : {glm::dvec2(1.3, -2.7), glm::dvec2(-1e-20, 0.0), glm::dvec2(100000.1, 30.0)}) {
        auto point = frame.value().position(q);
        REQUIRE(point.ok());
        auto periodic = frame.value().position(q + glm::dvec2(64.0, -64.0));
        REQUIRE(periodic.ok());
        CHECK(glm::length(periodic.value() - point.value() - glm::dvec3(64.0, 0.0, -64.0)) < 1e-9);
        auto sample = frame.value().sample({point.value().x, point.value().z});
        REQUIRE(sample.ok());
        CHECK(std::abs(sample.value().height - point.value().y) < 1e-5);
    }
}

TEST_CASE("fluids.ocean.spectrumFftMatchesIndependentDft") {
    OceanSpectrumBuilder builder;
    builder.settings.bands      = {{8, 32.0, 8.0, 32.0}};
    builder.settings.choppiness = 0.0;
    auto spectrum               = builder.build();
    REQUIRE(spectrum.ok());
    // Reconstruct spatial Fourier coefficients from t=0, then independently
    // advance each travelling mode. The wind half-plane fixes propagation sign.
    auto initial = spectrum.value().frameBuilder(0.0).build();
    auto later   = spectrum.value().frameBuilder(0.7).build();
    REQUIRE(initial.ok());
    REQUIRE(later.ok());
    constexpr unsigned                n   = 8;
    constexpr double                  tau = 2.0 * std::numbers::pi;
    std::vector<std::complex<double>> coefficients(n * n);
    const auto&                       start = initial.value().grids()[0];
    for (unsigned kz = 0; kz < n; ++kz)
        for (unsigned kx = 0; kx < n; ++kx)
            for (unsigned z = 0; z < n; ++z)
                for (unsigned x = 0; x < n; ++x)
                    coefficients[kz * n + kx] +=
                        start.displacement[z * n + x].y * std::polar(1.0, -tau * (kx * x + kz * z) / n) / double(n * n);
    for (unsigned z = 0; z < n; ++z) {
        for (unsigned x = 0; x < n; ++x) {
            std::complex<double> expected{};
            for (unsigned kz = 0; kz < n; ++kz) {
                for (unsigned kx = 0; kx < n; ++kx) {
                    const int    ix    = kx < n / 2 ? int(kx) : int(kx) - int(n);
                    const int    iz    = kz < n / 2 ? int(kz) : int(kz) - int(n);
                    const double k     = std::hypot(double(ix), double(iz)) * tau / 32.0;
                    const double omega = std::sqrt(9.81 * k);
                    const double sign  = ix >= 0 ? -1.0 : 1.0;
                    expected +=
                        coefficients[kz * n + kx] * std::polar(1.0, tau * (kx * x + kz * z) / n + sign * omega * 0.7);
                }
            }
            CHECK(std::abs(expected.imag()) < 1e-10);
            CHECK(std::abs(expected.real() - later.value().grids()[0].displacement[z * n + x].y) < 1e-10);
        }
    }
}

TEST_CASE("fluids.ocean.spectrumVelocityAndValidation") {
    OceanSpectrumBuilder builder;
    builder.settings.bands = {{16, 64.0, 8.0, 64.0}};
    auto spectrum          = builder.build();
    REQUIRE(spectrum.ok());
    auto before = spectrum.value().frameBuilder(1.0 - 1e-4).build();
    auto now    = spectrum.value().frameBuilder(1.0).build();
    auto after  = spectrum.value().frameBuilder(1.0 + 1e-4).build();
    REQUIRE(before.ok());
    REQUIRE(now.ok());
    REQUIRE(after.ok());
    for (unsigned i = 0; i < 256; ++i) {
        const auto derivative =
            (after.value().grids()[0].displacement[i] - before.value().grids()[0].displacement[i]) / 2e-4;
        CHECK(glm::length(derivative - now.value().grids()[0].velocity[i]) < 1e-6);
    }
    auto badTime = spectrum.value().frameBuilder(std::numeric_limits<double>::infinity()).build();
    CHECK(!badTime.ok());
    builder.settings.bands.push_back(builder.settings.bands.front());
    auto overlapping = builder.build();
    CHECK(!overlapping.ok());
    builder.settings.bands.resize(1);
    builder.settings.bands[0].resolution = 15;
    auto nonRadixTwo                     = builder.build();
    CHECK(!nonRadixTwo.ok());
}

TEST_CASE("fluids.ocean.spectrumMultibandCurrentAndBuilderLifetime") {
    OceanSpectrumBuilder builder;
    auto                 spectrum = builder.build();
    REQUIRE(spectrum.ok());
    auto frame = spectrum.value().frameBuilder(4.0).build();
    REQUIRE(frame.ok());
    CHECK(frame.value().grids().size() == 3);
    for (double x = -30.0; x < 30.0; x += 2.3) {
        auto p = frame.value().position({x, 7.31});
        REQUIRE(p.ok());
        auto sample = frame.value().sample({p.value().x, p.value().z});
        REQUIRE(sample.ok());
        CHECK(std::abs(sample.value().height - p.value().y) < 1e-5);
        const double eps = 1e-5;
        auto         px  = frame.value().position({x + eps, 7.31});
        auto         pz  = frame.value().position({x, 7.31 + eps});
        REQUIRE(px.ok());
        REQUIRE(pz.ok());
        auto normal = glm::normalize(glm::cross(pz.value() - p.value(), px.value() - p.value()));
        CHECK(glm::length(normal - sample.value().normal) < 1e-4);
    }
    // Builder retains an immutable lease after both source spectrum and its
    // authoring settings leave scope. No deferred borrowed reference survives.
    auto movingBuilder = [] {
        OceanSpectrumBuilder local;
        local.settings.bands   = {{16, 64.0, 8.0, 64.0}};
        local.settings.current = {4.0, 0.0};
        auto sea               = local.build().expect("valid current sea");
        return sea.frameBuilder(1.0);
    }();
    auto moving = movingBuilder.build();
    REQUIRE(moving.ok());
    builder.settings.bands = {{16, 64.0, 8.0, 64.0}};
    auto stillSea          = builder.build();
    REQUIRE(stillSea.ok());
    auto still = stillSea.value().frameBuilder(1.0).build();
    REQUIRE(still.ok());
    auto a = still.value().position({8.0, 12.0});
    auto b = moving.value().position({12.0, 12.0});
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(glm::length(b.value() - a.value() - glm::dvec3(4.0, 0.0, 0.0)) < 1e-10);
    builder.settings.significantWaveHeight = 0.0;
    auto calm                              = builder.build();
    REQUIRE(calm.ok());
    auto flat = calm.value().frameBuilder(3.0).build();
    REQUIRE(flat.ok());
    for (const auto& displacement : flat.value().grids()[0].displacement) CHECK(glm::length(displacement) == 0.0);
}
