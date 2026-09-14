#pragma once

#include <algorithm>
#include <cmath>

namespace eve::graphics {

/**
 * @brief CPU reference for simple offset parallax (matches the scale / height convention
 * of parallax_map.glsl POM; used by unit tests, not the GPU path).
 *
 * Height: white (1) = raised toward viewer. Depth = 1 - height.
 * viewDirTS: tangent-space view direction (toward camera), typically normalized.
 */
struct ParallaxParams {
    float scale = 0.f;      // 0 = off
    float minLayers = 8.f;  // adaptive POM min steps
    float maxLayers = 32.f; // adaptive POM max steps
    bool silhouette = false; // SilPOM: discard when displaced UV leaves mesh UV bounds
};

/** @brief Single-sample offset mapping (cheap approximation of the first POM step). */
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

/**
 * @brief Silhouette POM coverage in raw mesh UV space [0,1].
 *
 * Returns 1 when the displaced UV stays inside the mesh chart (plus optional padding),
 * else 0. GPU path discards fragments with coverage below the alpha cutoff.
 *
 * @param displacedU Displaced U after POM (raw mesh UV space, not tiled).
 * @param displacedV Displaced V after POM.
 * @param padding Soft border in UV units (0 = hard clip at 0/1).
 */
inline float silPomCoverage(float displacedU, float displacedV, float padding = 0.f) {
    const float pad = std::max(0.f, padding);
    const float minU = displacedU - pad;
    const float minV = displacedV - pad;
    const float maxU = (1.f + pad) - displacedU;
    const float maxV = (1.f + pad) - displacedV;
    return (minU >= 0.f && minV >= 0.f && maxU >= 0.f && maxV >= 0.f) ? 1.f : 0.f;
}

/**
 * @brief Screen-space displacement offset (educational SSDM CPU reference).
 *
 * Classic SSDM warps already-rendered pixels along the screen-space projection of the
 * surface relief. For a planar card this reduces to a height-weighted offset along the
 * projected view / normal direction in NDC pixels.
 *
 * @param height01 Height sample in [0,1] (1 = raised toward viewer).
 * @param viewNdcX View direction X in NDC-ish screen space (toward camera projection).
 * @param viewNdcY View direction Y in NDC-ish screen space.
 * @param scale Displacement strength in screen UV units (typical 0.01..0.08).
 * @param outX Output screen-UV delta X.
 * @param outY Output screen-UV delta Y.
 */
inline void ssdmScreenOffset(float height01, float viewNdcX, float viewNdcY, float scale,
                             float &outX, float &outY) {
    if (scale < 1e-5f) {
        outX = 0.f;
        outY = 0.f;
        return;
    }
    // Raised texels push toward the viewer in screen space (opposite to POM's UV dig).
    const float h = std::clamp(height01, 0.f, 1.f);
    const float len = std::sqrt(viewNdcX * viewNdcX + viewNdcY * viewNdcY);
    if (len < 1e-5f) {
        outX = 0.f;
        outY = 0.f;
        return;
    }
    outX = (viewNdcX / len) * h * scale;
    outY = (viewNdcY / len) * h * scale;
}

/**
 * @brief Whether an SSDM-displaced screen sample still covers the surface footprint.
 *
 * Used by the comparison demo to clip rays that leave the card after view-space march.
 */
inline float ssdmCoverage(float baseU, float baseV, float offsetU, float offsetV,
                          float padding = 0.f) {
    return silPomCoverage(baseU + offsetU, baseV + offsetV, padding);
}

inline void clampParallaxParams(ParallaxParams &p) {
    if (p.scale < 0.f) p.scale = 0.f;
    if (p.scale > 0.25f) p.scale = 0.25f;
    if (p.minLayers < 1.f) p.minLayers = 1.f;
    if (p.maxLayers < p.minLayers) p.maxLayers = p.minLayers;
    if (p.maxLayers > 64.f) p.maxLayers = 64.f;
}

}  // namespace eve::graphics
