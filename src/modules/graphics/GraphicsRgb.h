#pragma once

/**
 * @file GraphicsRgb.h
 * @brief POD RGB triple for Graphics tone-map controls without glm headers.
 */

namespace eve::graphics {

/** @brief Linear RGB triple used by scene color-grade getters/setters. */
struct RgbF {
    float r = 1.f;
    float g = 1.f;
    float b = 1.f;
};

}  // namespace eve::graphics
