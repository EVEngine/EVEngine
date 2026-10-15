#include "fluids/OceanGpuSpectrum.h"
#include "fluids/OceanSeaTransition.h"
#include "gpgpu/Gpgpu.h"
#include "graphics/Graphics.h"
#include "window/Window.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

TEST_CASE("fluids.ocean.gpuSpectrumMatchesCpuAndRejectsStaleFrames") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    auto*                       compute  = eve::gpgpu::Gpgpu::create();
    eve::window::WindowSettings settings;
    settings.width  = 64;
    settings.height = 64;
    REQUIRE(window->setWindowSettings(settings));
    eve::fluids::OceanSpectrumBuilder builder;
    builder.settings.current  = {0.7, -0.3};
    builder.settings.seaLevel = 3.0;
    auto spectrum             = builder.build();
    REQUIRE(spectrum.ok());
    eve::fluids::OceanGpuSpectrumBuilder gpuBuilder(spectrum.value(), *graphics, *compute);
    auto                                 gpu = gpuBuilder.build();
    if (graphics->getBackendName() != "vulkan") {
        REQUIRE(!gpu.ok());
        window->close();
        return;
    }
    REQUIRE(gpu.ok());
    eve::fluids::OceanGpuFrameToken previous;
    for (double time : {0.0, 3.125, 16.125, 100000000.125, -0.5}) {
        auto expected = spectrum.value().frameBuilder(time).build();
        REQUIRE(expected.ok());
        auto submitted = gpu.value()->frameBuilder(time).submit();
        REQUIRE(submitted.ok());
        CHECK(!gpu.value()->band(previous, 0).ok());
        CHECK(!gpu.value()->readbackBuilder(previous).build().ok());
        previous      = submitted.value();
        auto resident = gpu.value()->band(previous, 0);
        REQUIRE(resident.ok());
        CHECK(resident.value().buffer.backend == eve::GpuResidentBackend::Vulkan);
        CHECK(resident.value().buffer.nativeHandle != 0);
        CHECK(resident.value().buffer.sizeBytes >= 64 * 64 * 6 * sizeof(float));
        auto actual = gpu.value()->readbackBuilder(previous).build();
        REQUIRE(actual.ok());
        REQUIRE(actual.value().grids().size() == expected.value().grids().size());
        double displacementError = 0.0, velocityError = 0.0;
        for (std::size_t b = 0; b < actual.value().grids().size(); ++b) {
            const auto& a = actual.value().grids()[b];
            const auto& e = expected.value().grids()[b];
            REQUIRE(a.resolution == e.resolution);
            for (std::size_t i = 0; i < a.displacement.size(); ++i) {
                displacementError = std::max(displacementError, glm::length(a.displacement[i] - e.displacement[i]));
                velocityError     = std::max(velocityError, glm::length(a.velocity[i] - e.velocity[i]));
            }
        }
        std::printf("Ocean GPU parity t=%.9g displacement=%.9g velocity=%.9g\n", time, displacementError,
                    velocityError);
        CHECK(displacementError < 0.005);
        CHECK(velocityError < 0.05);
        auto cpuSurface = expected.value().sample({8.23, -3.47});
        auto gpuSurface = actual.value().sample({8.23, -3.47});
        REQUIRE(cpuSurface.ok());
        REQUIRE(gpuSurface.ok());
        CHECK(std::abs(cpuSurface.value().height - gpuSurface.value().height) < 0.005);
        CHECK(glm::length(cpuSurface.value().normal - gpuSurface.value().normal) < 0.005);
    }
    auto invalid = gpu.value()->frameBuilder(std::numeric_limits<double>::quiet_NaN()).submit();
    CHECK(!invalid.ok());
    CHECK(gpu.value()->band(previous, 0).ok());
    CHECK(!gpu.value()->band(previous, 99).ok());
    auto other = gpuBuilder.build();
    REQUIRE(other.ok());
    auto otherFrame = other.value()->frameBuilder(0.0).submit();
    REQUIRE(otherFrame.ok());
    CHECK(!gpu.value()->band(otherFrame.value(), 0).ok());
    other.value().reset();
    gpu.value().reset();
    window->close();
}

TEST_CASE("fluids.ocean.gpuSpectrumReportsUnavailableProvider") {
    auto*                             graphics = eve::graphics::Graphics::create();
    auto*                             compute  = eve::gpgpu::Gpgpu::create();
    eve::fluids::OceanSpectrumBuilder builder;
    auto                              spectrum = builder.build();
    REQUIRE(spectrum.ok());
    eve::fluids::OceanGpuSpectrumBuilder gpuBuilder(spectrum.value(), *graphics, *compute);
    auto                                 result = gpuBuilder.build();
    REQUIRE(!result.ok());
}

TEST_CASE("fluids.ocean.gpuSpectrumLargeBandParity") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    auto*                       compute  = eve::gpgpu::Gpgpu::create();
    eve::window::WindowSettings settings;
    settings.width  = 64;
    settings.height = 64;
    REQUIRE(window->setWindowSettings(settings));
    for (unsigned resolution : {128U, 256U}) {
        eve::fluids::OceanSpectrumBuilder builder;
        builder.settings.bands   = {{resolution, 512.0, 8.0, 512.0}};
        builder.settings.current = {-2.0, 1.0};
        auto spectrum            = builder.build();
        REQUIRE(spectrum.ok());
        auto expected = spectrum.value().frameBuilder(9.75).build();
        REQUIRE(expected.ok());
        eve::fluids::OceanGpuSpectrumBuilder gpuBuilder(spectrum.value(), *graphics, *compute);
        auto                                 gpu = gpuBuilder.build();
        if (graphics->getBackendName() != "vulkan") {
            REQUIRE(!gpu.ok());
            break;
        }
        REQUIRE(gpu.ok());
        auto token = gpu.value()->frameBuilder(9.75).submit();
        REQUIRE(token.ok());
        auto actual = gpu.value()->readbackBuilder(token.value()).build();
        REQUIRE(actual.ok());
        double      error = 0.0;
        const auto& a     = actual.value().grids().front();
        const auto& e     = expected.value().grids().front();
        for (std::size_t i = 0; i < a.displacement.size(); ++i)
            error = std::max(error, glm::length(a.displacement[i] - e.displacement[i]));
        std::printf("Ocean GPU parity resolution=%u displacement=%.9g\n", resolution, error);
        CHECK(error < 0.005);
    }
    window->close();
}

TEST_CASE("fluids.ocean.gpuSeaTransitionMatchesCpuFieldsAndLevel") {
    using namespace eve::fluids;
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    auto*                       compute  = eve::gpgpu::Gpgpu::create();
    eve::window::WindowSettings settings;
    settings.width  = 64;
    settings.height = 64;
    REQUIRE(window->setWindowSettings(settings));
    OceanSpectrumBuilder builder;
    builder.settings.bands                 = {{16, 64, 8, 32}, {32, 256, 32, 256}};
    builder.settings.current               = {0.4, -0.2};
    builder.settings.significantWaveHeight = 0.3;
    builder.settings.choppiness            = 0.2;
    auto from                              = builder.build();
    REQUIRE(from.ok());
    builder.settings.significantWaveHeight = 1.0;
    builder.settings.choppiness            = 0.7;
    builder.settings.seaLevel              = 1.5;
    builder.settings.seed                  = 47;
    builder.settings.windDirection         = {-0.4, 0.8};
    auto to                                = builder.build();
    REQUIRE(to.ok());
    OceanSeaTransitionBuilder plan(from.value(), to.value());
    plan.start      = 15;
    plan.duration   = 2;
    auto transition = plan.build();
    REQUIRE(transition.ok());
    auto gpu = OceanGpuSpectrumBuilder(transition.value(), *graphics, *compute).build();
    if (graphics->getBackendName() != "vulkan") {
        REQUIRE(!gpu.ok());
        window->close();
        return;
    }
    REQUIRE(gpu.ok());
    OceanGpuFrameToken previous;
    for (double time : {14.0, 15.0, 15.25, 16.0, 16.75, 17.0, 18.0, 100000000.125}) {
        auto expected = transition.value().frameBuilder(time).build();
        REQUIRE(expected.ok());
        auto token = gpu.value()->frameBuilder(time).submit();
        REQUIRE(token.ok());
        CHECK(!gpu.value()->atlas(previous).ok());
        previous    = token.value();
        auto actual = gpu.value()->readbackBuilder(token.value()).build();
        REQUIRE(actual.ok());
        auto atlas = gpu.value()->atlas(token.value());
        REQUIRE(atlas.ok());
        const double u = std::clamp((time - 15) / 2, 0.0, 1.0), w = u * u * u * (10 + u * (-15 + 6 * u));
        CHECK(std::abs(atlas.value().seaLevel - 1.5 * w) < 1e-12);
        double dError = 0, vError = 0;
        REQUIRE(actual.value().grids().size() == expected.value().grids().size());
        for (std::size_t b = 0; b < actual.value().grids().size(); ++b) {
            const auto& a = actual.value().grids()[b];
            const auto& e = expected.value().grids()[b];
            for (std::size_t i = 0; i < a.displacement.size(); ++i) {
                dError = std::max(dError, glm::length(a.displacement[i] - e.displacement[i]));
                vError = std::max(vError, glm::length(a.velocity[i] - e.velocity[i]));
            }
        }
        auto pa = actual.value().position({3.25, 5.75}), pe = expected.value().position({3.25, 5.75});
        REQUIRE(pa.ok());
        REQUIRE(pe.ok());
        CHECK(glm::length(pa.value() - pe.value()) < 5e-5);
        std::printf("Transition GPU t=%.9g displacement=%.9g velocity=%.9g\n", time, dError, vError);
        CHECK(dError < 5e-5);
        CHECK(vError < 1e-4);
    }
    // GPU preparation owns the endpoint leases, not the temporary transition object.
    auto prepare = [&]() {
        auto temporary = plan.build();
        REQUIRE(temporary.ok());
        return OceanGpuSpectrumBuilder(temporary.value(), *graphics, *compute);
    };
    auto retained = prepare().build();
    REQUIRE(retained.ok());
    REQUIRE(retained.value()->frameBuilder(16).submit().ok());
    retained.value().reset();
    gpu.value().reset();
    window->close();
}
