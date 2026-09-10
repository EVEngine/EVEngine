#pragma once
#include <cmath>
#include "graphics/Color.h"

/** @brief Recover linear scene radiance from ACES/sRGB display pixels at exposure 1.
 * @note Intended for unsaturated test samples; quantized highlights cannot be inverted accurately.
 */
inline eve::graphics::Color testSceneLinearColor(const eve::graphics::Color& pixel) {
    const auto decode = [](float value) {
        const float y = value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
        const float a = 2.43f * y - 2.51f, b = .59f * y - .03f;
        return (-b - std::sqrt(b * b - 4.f * a * .14f * y)) / (2.f * a);
    };
    return {decode(pixel.r), decode(pixel.g), decode(pixel.b), pixel.a};
}
