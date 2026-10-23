#pragma once
#include <algorithm>
#include "graphics/sky/SkyAtmospherePass.h"
#include "graphics/sky/SkyWispsLayer.h"
namespace eve::graphics::detail {
/** @brief Pure clear/manual optical projection. Inputs are validated authored values and sun Y in [-1,1]. */
inline SkyAtmosphereParameters opticalSample(SkyAtmosphereParameters p, const SkyDaylightLayer& d, float sunY) {
    const float          day   = std::clamp((sunY + .03604f) / (.03604f + .06319f), 0.f, 1.f);
    const float          night = (1 - day) * (1 - day), dusk = std::clamp(1 - sunY / .3f, 0.f, 1.f);
    const float          twilight = std::clamp((.025f - sunY) / .05f, 0.f, 1.f);
    std::array<float, 3> absorption{};
    for (size_t i = 0; i < 3; ++i) {
        p.rayleigh[i] =
            ((d.rayDay[i] * (1 - dusk) + d.rayDusk[i] * dusk) * (1 - night) + d.rayNight[i] * night) * d.rayDay[3];
        absorption[i] = d.absorptionDay[i] * (1 - twilight) + d.absorptionNight[i] * twilight;
    }
    const float extremes = *std::min_element(absorption.begin(), absorption.end()) +
                           *std::max_element(absorption.begin(), absorption.end());
    const float scale = d.absorptionDay[3] * (1 - twilight) + d.absorptionNight[3] * twilight;
    // Hue +180 degrees in HSV, preserving saturation/value, equals max+min-RGB.
    for (size_t i = 0; i < 3; ++i) p.ozoneAbsorption[i] = (extremes - absorption[i]) * scale;
    return p;
}
inline constexpr unsigned OpticalLayers  = 81;
inline constexpr float    OpticalMinimum = -.08f, OpticalMaximum = .32f;
}  // namespace eve::graphics::detail
