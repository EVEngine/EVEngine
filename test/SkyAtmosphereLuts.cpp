#include "graphics/sky/SkyAtmosphereLuts.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "graphics/sky/SkyAtmospherePass.h"
#include "graphics/sky/SkyOpticalCycle.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.sky atmosphere vacuum preserves transmission without creating scattered energy") {
    eve::graphics::SkyAtmosphereParameters parameters;
    parameters.rayleigh = parameters.mieScattering = parameters.mieAbsorption = parameters.ozoneAbsorption =
        parameters.groundAlbedo                                               = {0, 0, 0};
    auto baked = eve::graphics::detail::bakeSkyAtmosphereLuts(parameters);
    REQUIRE(baked.ok());
    const auto& tables = baked.value();
    REQUIRE(tables.transmittance.size() == 256 * 64 * 4);
    REQUIRE(tables.multiScattering.size() == 32 * 32 * 4);
    for (size_t i = 0; i < tables.transmittance.size(); ++i) REQUIRE(tables.transmittance[i] == 1);
    for (size_t i = 0; i < tables.multiScattering.size(); ++i)
        REQUIRE(tables.multiScattering[i] == (i % 4 == 3 ? 1.f : 0.f));
    auto ambient = eve::graphics::detail::bakeDistantSkyAmbient(parameters, tables, {0, 1, 0});
    REQUIRE(ambient.ok());
    for (float channel : ambient.value()) REQUIRE(channel == 0);
}

TEST_CASE("graphics.sky multiple scattering adds finite energy independently of transmission") {
    eve::graphics::SkyAtmosphereParameters parameters;
    auto                                   enabled = eve::graphics::detail::bakeSkyAtmosphereLuts(parameters);
    REQUIRE(enabled.ok());
    parameters.multiScatteringFactor = 0;
    auto disabled                    = eve::graphics::detail::bakeSkyAtmosphereLuts(parameters);
    REQUIRE(disabled.ok());
    REQUIRE(enabled.value().transmittance == disabled.value().transmittance);
    double total = 0;
    for (size_t i = 0; i < enabled.value().multiScattering.size(); ++i) {
        if (i % 4 == 3) continue;
        const auto value = enabled.value().multiScattering[i];
        REQUIRE(std::isfinite(value));
        REQUIRE(value >= 0);
        REQUIRE(disabled.value().multiScattering[i] == 0);
        total += value;
    }
    REQUIRE(total > 0);
    for (auto transmission : enabled.value().transmittance) {
        REQUIRE(std::isfinite(transmission));
        REQUIRE(transmission >= 0);
        REQUIRE(transmission <= 1);
    }
    parameters.groundAlbedo[0] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!eve::graphics::detail::bakeSkyAtmosphereLuts(parameters).ok());
}

TEST_CASE("graphics.sky distant ambient responds to solar elevation and rejects invalid preparation") {
    eve::graphics::SkyAtmosphereParameters parameters;
    auto                                   tables = eve::graphics::detail::bakeSkyAtmosphereLuts(parameters);
    REQUIRE(tables.ok());
    auto day    = eve::graphics::detail::bakeDistantSkyAmbient(parameters, tables.value(), {0, 1, 0});
    auto night  = eve::graphics::detail::bakeDistantSkyAmbient(parameters, tables.value(), {0, -1, 0});
    auto scaled = eve::graphics::detail::bakeDistantSkyAmbient(parameters, tables.value(), {0, 2, 0});
    REQUIRE(day.ok());
    REQUIRE(night.ok());
    REQUIRE(scaled.ok());
    for (size_t axis = 0; axis < 3; ++axis) {
        REQUIRE(std::isfinite(day.value()[axis]));
        REQUIRE(day.value()[axis] > night.value()[axis]);
        REQUIRE(night.value()[axis] >= 0);
        REQUIRE(day.value()[axis] == scaled.value()[axis]);
    }
    REQUIRE(!eve::graphics::detail::bakeDistantSkyAmbient(parameters, tables.value(), {0, 0, 0}).ok());
    auto corrupted = tables.value();
    corrupted.multiScattering.pop_back();
    REQUIRE(!eve::graphics::detail::bakeDistantSkyAmbient(parameters, corrupted, {0, 1, 0}).ok());
    corrupted                  = tables.value();
    corrupted.transmittance[0] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!eve::graphics::detail::bakeDistantSkyAmbient(parameters, corrupted, {0, 1, 0}).ok());
    parameters.atmosphereHeightKm = 5;
    REQUIRE(!eve::graphics::detail::bakeDistantSkyAmbient(parameters, tables.value(), {0, 1, 0}).ok());
}

TEST_CASE("graphics.sky optical cycle matches source component values and bounded LUT interpolation") {
    using namespace eve::graphics;
    SkyDaylightLayer        d;
    SkyAtmosphereParameters p;
    auto                    noon = detail::opticalSample(p, d, 1), midnight = detail::opticalSample(p, d, -1),
         dawn = detail::opticalSample(p, d, 0);
    REQUIRE(std::abs(noon.rayleigh[0] - .00674508f) < 1e-7f);
    REQUIRE(std::abs(midnight.rayleigh[2] - .024375f) < 1e-7f);
    REQUIRE(std::abs(dawn.rayleigh[2] - .03366377f) < 1e-7f);
    REQUIRE(std::abs(noon.ozoneAbsorption[0] - .001794476f) < 1e-8f);
    REQUIRE(std::abs(midnight.ozoneAbsorption[1] - .003396696f) < 1e-8f);
    REQUIRE(std::abs(dawn.ozoneAbsorption[1] - .002927907f) < 1e-8f);
    for (float y : {-.0625f, -.0275f, .0025f, .0575f}) {
        auto a     = detail::bakeSkyAtmosphereLuts(detail::opticalSample(p, d, y - .0025f));
        auto b     = detail::bakeSkyAtmosphereLuts(detail::opticalSample(p, d, y + .0025f));
        auto exact = detail::bakeSkyAtmosphereLuts(detail::opticalSample(p, d, y));
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        REQUIRE(exact.ok());
        // The original .002 bound covers optical interpolation. Each packed
        // endpoint and the independently baked center add half a storage ULP;
        // budget these separately rather than hiding them in a global epsilon.
        const auto checkPackedInterpolation = [&](const auto& low, const auto& high, const auto& center) {
            for (size_t i = 0; i < center.size(); ++i) {
                if (i % 4 == 3) continue;
                const int  mantissaBits = i % 4 == 2 ? 5 : 6;
                const auto halfUlp      = [mantissaBits](float value) {
                    int exponent = 0;
                    const float fraction = std::frexp(value, &exponent);
                    if (fraction == 0) exponent = -13;
                    return std::ldexp(1.f, std::max(-14, exponent - 1) - mantissaBits - 1);
                };
                const float storageError = .5f * (halfUlp(low[i]) + halfUlp(high[i])) + halfUlp(center[i]);
                REQUIRE(std::abs((low[i] + high[i]) * .5f - center[i]) < .002f + storageError);
            }
        };
        checkPackedInterpolation(a.value().transmittance, b.value().transmittance, exact.value().transmittance);
        checkPackedInterpolation(a.value().multiScattering, b.value().multiScattering, exact.value().multiScattering);
    }
}
