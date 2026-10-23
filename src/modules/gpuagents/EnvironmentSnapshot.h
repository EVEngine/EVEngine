#pragma once
#include "common/Export.h"

#include "gpuagents/ObstacleField.h"
#include "gpuagents/SurfaceField.h"

#include <glm/glm.hpp>

#include <vector>

namespace eve::gpuagents {

/** @brief Point attractor / repulsor / danger marker in world space. */
struct EnvironmentMarker {
    glm::vec3 position{0.f};
    float     radius   = 2.f;
    float     strength = 1.f;
};

/**
 * @brief Read-only environment semantics prepared by GpuAgentWorld each tick.
 *
 * Solvers must not mutate World-owned fields; they may write into a Simulation-local
 * SurfaceField copy when the effect owns life-field evolution.
 */
struct EVENGINE_API_DOMAINS EnvironmentSnapshot {
    glm::vec3 waterCurrent{0.f};
    glm::vec3 windVelocity{0.f};
    float     groundY   = 0.f;
    float     ceilingY  = 40.f;

    std::vector<EnvironmentMarker> goals;
    std::vector<EnvironmentMarker> dangers;
    std::vector<EnvironmentMarker> nutrients;

    const ObstacleField* obstacles = nullptr;
    SurfaceField*        surface   = nullptr;  ///< optional; Life Network writes trails here
};

}  // namespace eve::gpuagents
