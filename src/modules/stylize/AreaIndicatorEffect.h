#pragma once

#include "stylize/TrailEffect.h"

#include <cstdint>

namespace eve::stylize {

/** @brief Primitive footprint used by a world-space skill area indicator. */
enum class AreaIndicatorShape : std::uint8_t { Circle, Ring, Sector, Rectangle };

/** @brief Geometry and edge-fade controls for one local-XZ area indicator. */
struct AreaIndicatorConfig {
    AreaIndicatorShape shape = AreaIndicatorShape::Circle;
    float radius = 1.f;
    float innerRadius = 0.75f;
    float sectorAngleRadians = 1.57079632679f;
    float width = 1.f;
    float length = 2.f;
    std::uint32_t segments = 32;
    float innerAlpha = 0.35f;
    float outerAlpha = 1.f;
};

/** @brief Observable terminal state of area-indicator mesh generation. */
enum class AreaIndicatorBuildStatus : std::uint8_t { Built, InvalidConfig };

/** @brief Owning local-space mesh and terminal build status. */
struct AreaIndicatorBuildResult {
    AreaIndicatorBuildStatus status = AreaIndicatorBuildStatus::InvalidConfig;
    TrailMeshSnapshot mesh;
};

/**
 * @brief Generate a local-XZ area indicator compatible with dynamic mesh submission.
 * @param config Shape dimensions, tessellation and alpha controls copied for this build.
 * @return Structured status and owning triangle mesh; invalid input yields an empty mesh.
 * @thread Thread-safe CPU-only operation.
 * @reentrancy Does not invoke callbacks or retain config.
 */
[[nodiscard]] AreaIndicatorBuildResult buildAreaIndicator(
    const AreaIndicatorConfig& config = {});

}  // namespace eve::stylize
