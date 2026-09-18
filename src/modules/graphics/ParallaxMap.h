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
    // padding expands the keep region to [-pad, 1+pad].
    const float pad = std::max(0.f, padding);
    return (displacedU >= -pad && displacedV >= -pad && displacedU <= 1.f + pad &&
            displacedV <= 1.f + pad)
               ? 1.f
               : 0.f;
}

/**
 * @brief Soft chart coverage in [0,1] — feathers near the UV border (full SilPOM).
 *
 * @param displacedU Displaced U after POM.
 * @param displacedV Displaced V after POM.
 * @param feather Soft edge width in UV units (clamped to a tiny epsilon).
 */
inline float silPomCoverageSoft(float displacedU, float displacedV, float feather) {
    const float f = std::max(feather, 1e-4f);
    auto edge = [f](float t) {
        if (t <= 0.f) return 0.f;
        if (t >= f) return 1.f;
        const float x = t / f;
        return x * x * (3.f - 2.f * x); // smoothstep
    };
    const float cx = std::min(edge(displacedU), edge(1.f - displacedU));
    const float cy = std::min(edge(displacedV), edge(1.f - displacedV));
    return cx * cy;
}

/**
 * @brief View-dependent horizon trim (SPOM-style) for grazing SilPOM limbs.
 *
 * Near-grazing fragments whose height is below a view-dependent threshold are
 * clipped so low relief does not smear past the geometric silhouette.
 *
 * @return 1 keep, 0 clip.
 */
inline float silPomHorizonTrim(float height01, float ndotv, float strength,
                               float bias = 0.02f) {
    const float t = std::clamp(1.f - std::fabs(ndotv) / 0.35f, 0.f, 1.f);
    const float threshold = std::clamp(std::pow(t, 1.5f) * strength, 0.f, 1.f);
    return (height01 - bias >= threshold) ? 1.f : 0.f;
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
