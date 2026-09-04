#pragma once

#include "stylize/TrailEffect.h"

#include <glm/vec3.hpp>

#include <cstdint>

namespace eve::stylize {

/** @brief Shape and deterministic noise controls for one 3D beam or lightning arc. */
struct BeamEffectConfig {
    std::uint32_t segments = 12;
    float widthStart = 0.12f;
    float widthEnd = 0.04f;
    float alphaStart = 1.f;
    float alphaEnd = 0.f;
    float noiseAmplitude = 0.f;
    float noiseCycles = 3.f;
    std::uint32_t randomSeed = 1;
    std::uint32_t branchCount = 0;
    std::uint32_t branchSegments = 4;
    float branchLength = 0.35f;
    float branchWidthScale = 0.5f;
};

/** @brief Observable terminal state of deterministic beam mesh generation. */
enum class BeamBuildStatus : std::uint8_t { Built, DegenerateInput, InvalidConfig };

/** @brief Owning beam generation result suitable for MeshEffectRenderer::submitTrail. */
struct BeamBuildResult {
    BeamBuildStatus status = BeamBuildStatus::InvalidConfig;
    TrailMeshSnapshot mesh;
};

/**
 * @brief Generate a camera-facing beam or branched lightning ribbon.
 * @param start World-space beam origin.
 * @param end World-space beam destination.
 * @param cameraForward Normalized direction from camera into the scene; normalized internally.
 * @param config Segment, taper, noise and branch controls copied for this build.
 * @return Structured status and an owning triangle mesh; failures contain an empty mesh.
 * @thread Thread-safe; performs deterministic CPU work only.
 * @reentrancy Does not invoke callbacks or retain arguments.
 */
[[nodiscard]] BeamBuildResult buildBeamEffect(const glm::vec3& start, const glm::vec3& end,
                                              const glm::vec3& cameraForward,
                                              const BeamEffectConfig& config = {});

}  // namespace eve::stylize
