#pragma once

#include <algorithm>
#include <cmath>

namespace eve::graphics {

/**
 * CPU reference for simple offset parallax (matches the scale / height convention
 * of parallax_map.glsl POM; used by unit tests, not the GPU path).
 *
 * Height: white (1) = raised toward viewer. Depth = 1 - height.
 * viewDirTS: tangent-space view direction (toward camera), typically normalized.
 */
struct ParallaxParams {
    float scale = 0.f;      // 0 = off
    float minLayers = 8.f;  // adaptive POM min steps
    float maxLayers = 32.f; // adaptive POM max steps
};

/** Single-sample offset mapping (cheap approximation of the first POM step). */
inline void parallaxOffsetUV(float u, float v, float height01, float viewTSx, float viewTSy,
                             float viewTSz, float scale, float &ou, float &ov) {
    if (scale < 1e-5f) {
        ou = u;
        ov = v;
        return;
    }
    const float depth = 1.f - height01;
    const float vz = std::max(std::fabs(viewTSz), 0.08f);
    ou = u - (viewTSx / vz) * depth * scale;
    ov = v - (viewTSy / vz) * depth * scale;
}

inline void clampParallaxParams(ParallaxParams &p) {
    if (p.scale < 0.f) p.scale = 0.f;
    if (p.scale > 0.25f) p.scale = 0.25f;
    if (p.minLayers < 1.f) p.minLayers = 1.f;
    if (p.maxLayers < p.minLayers) p.maxLayers = p.minLayers;
    if (p.maxLayers > 64.f) p.maxLayers = 64.f;
}

}  // namespace eve::graphics
