#pragma once

#include "common/Result.h"
#include "stylize/BeamEffect.h"
#include "stylize/MeshEffect.h"
#include "stylize/MeshEffectRenderer.h"

#include <glm/vec3.hpp>

#include <span>
#include <vector>

namespace eve::stylize {

/** @brief Production-oriented beam behavior preset. */
enum class BeamSkillPreset : std::uint8_t { Laser, Lightning, ChainLightning };

/**
 * @brief Gameplay-facing recipe for laser and lightning mesh effects.
 *
 * The recipe owns playback/style/path state but no Graphics resources. Path
 * points are copied on assignment. Rendering is synchronous and render-thread
 * affine through MeshEffectRenderer; gameplay callbacks are never retained.
 */
class BeamSkillEffect {
public:
    /** @brief Construct one recipe and apply preset geometry/style defaults. */
    explicit BeamSkillEffect(BeamSkillPreset preset);

    /** @brief Replace the owning world-space path; at least two points are required to build. */
    void setPath(std::span<const glm::vec3> points);

    /** @brief Convenience two-point path assignment. */
    void setEndpoints(const glm::vec3& start, const glm::vec3& end);

    /** @brief Restart the effect playback envelope. */
    void play() noexcept { effect_.play(); }

    /** @brief Stop immediately or fade using the requested duration. */
    void stop(float fadeOutSeconds = 0.f) { effect_.stop(fadeOutSeconds); }

    /** @brief Advance the owned effect envelope with caller-provided simulation time. */
    void update(float deltaSeconds) { effect_.update(deltaSeconds); }

    /** @brief Build and merge every path segment into one owning ribbon snapshot. */
    [[nodiscard]] BeamBuildResult build(const glm::vec3& cameraForward) const;

    /**
     * @brief Build and submit the current path through the shared Trail renderer.
     * @param renderer Borrowed render-thread adapter; never retained.
     * @param cameraForward Camera direction used to face ribbon vertices.
     * @param tint Per-draw tint multiplied by recipe playback intensity.
     * @return Draw status or a structured invalid-path/build failure.
     */
    [[nodiscard]] eve::Result<MeshEffectSubmitStatus> submit(
        MeshEffectRenderer& renderer, const glm::vec3& cameraForward,
        const graphics::Color& tint = graphics::Color(1.f));

    /** @brief Access mutable geometry controls for per-skill overrides. */
    [[nodiscard]] BeamEffectConfig& geometry() noexcept { return geometry_; }

    /** @brief Access the owned style/playback instance for exposed parameters. */
    [[nodiscard]] MeshEffectInstance& effect() noexcept { return effect_; }

    /** @brief Return the selected immutable behavior preset. */
    [[nodiscard]] BeamSkillPreset preset() const noexcept { return preset_; }

private:
    BeamSkillPreset preset_;
    BeamEffectConfig geometry_;
    MeshEffectInstance effect_{"slash"};
    std::vector<glm::vec3> path_;
};

}  // namespace eve::stylize
