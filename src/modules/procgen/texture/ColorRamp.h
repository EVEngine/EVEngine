#pragma once

#include <cstdint>
#include <vector>

namespace eve::procgen {

struct Rgba8 {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

/** @brief Piecewise-linear color ramp in t∈[0,1], with optional hard banding. */
struct ColorRamp {
    struct Stop {
        float t = 0.f;
        Rgba8 c;
    };

    std::vector<Stop> stops;

    void add(float t, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    Rgba8 sample(float t) const;
    /** @brief Quantize continuous t into `bands` steps then sample (pixel look). */
    Rgba8 sampleBanded(float t, int bands) const;
};

}  // namespace eve::procgen
