#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace eve::ui {

/** @brief Direction of a linear image fill; geometry stays inside the original layout box. */
enum class FillDirection : uint8_t { Right = 0, Left, Down, Up };

/** @brief Pure linear fill geometry, normalized to the original image rectangle. */
struct LinearFillRect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
};

/** @brief Clamps finite fractions; invalid presentation data produces an empty fill. */
inline float hudFraction(float value) { return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f; }

/** @brief Computes cropped geometry/UVs; stretch preserves the complete source UV rectangle. */
inline LinearFillRect linearFillRect(float fraction, FillDirection direction, bool stretch) {
    const float    f = hudFraction(fraction);
    LinearFillRect r{0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f};
    switch (direction) {
        case FillDirection::Right:
            r.w = f;
            if (!stretch) r.u1 = f;
            break;
        case FillDirection::Left:
            r.x = 1.f - f;
            r.w = f;
            if (!stretch) r.u0 = r.x;
            break;
        case FillDirection::Down:
            r.h = f;
            if (!stretch) r.v1 = f;
            break;
        case FillDirection::Up:
            r.y = 1.f - f;
            r.h = f;
            if (!stretch) r.v0 = r.y;
            break;
    }
    return r;
}

/** @brief One nine-patch axis, separating destination distances from source UVs. */
struct NinePatchAxis {
    float nearSize, farSize, nearUv, farUv;
};

/** @brief Scales border geometry while retaining source texel coverage; scale/textureSize must be positive. */
inline NinePatchAxis ninePatchAxis(float size, float nearBorder, float farBorder, float uv0, float uv1,
                                   float textureSize, float scale) {
    const float nearSize   = std::min(nearBorder * scale, size * .5f);
    const float farSize    = std::min(farBorder * scale, size * .5f);
    const float uvPerPixel = (uv1 - uv0) / textureSize;
    return {nearSize, farSize, uv0 + nearSize / scale * uvPerPixel, uv1 - farSize / scale * uvPerPixel};
}

/** @brief One normalized quad of a radial ring; inner points coincide for a solid sector. */
struct RadialFillQuad {
    float outerX0, outerY0, outerX1, outerY1;
    float innerX0, innerY0, innerX1, innerY1;
};

/** @brief Allocation-free radial mesh with at most 64 segments per complete turn. */
struct RadialFillMesh {
    std::array<RadialFillQuad, 64> quads{};
    int                            count = 0;
};

/** @brief Builds an elliptical sector/ring in [0,1] coordinates; zero degrees points right. */
inline RadialFillMesh radialFillMesh(float fraction, float startDegrees, bool clockwise, float innerRadius) {
    RadialFillMesh mesh;
    const float    f = hudFraction(fraction);
    if (f <= 0.f || !std::isfinite(startDegrees)) return mesh;
    const float inner = hudFraction(innerRadius);
    if (inner >= 1.f) return mesh;
    constexpr float pi    = 3.14159265358979323846f;
    const float     start = std::fmod(startDegrees, 360.f) * pi / 180.f;
    const float     sweep = f * 2.f * pi * (clockwise ? 1.f : -1.f);
    mesh.count            = std::max(1, int(std::ceil(f * 64.f)));
    for (int i = 0; i < mesh.count; ++i) {
        const float a  = start + sweep * float(i) / float(mesh.count);
        const float b  = start + sweep * float(i + 1) / float(mesh.count);
        const float ax = std::cos(a) * .5f, ay = std::sin(a) * .5f;
        const float bx = std::cos(b) * .5f, by = std::sin(b) * .5f;
        mesh.quads[std::size_t(i)] = {.5f + ax,         .5f + ay,         .5f + bx,         .5f + by,
                                      .5f + ax * inner, .5f + ay * inner, .5f + bx * inner, .5f + by * inner};
    }
    return mesh;
}

}  // namespace eve::ui
