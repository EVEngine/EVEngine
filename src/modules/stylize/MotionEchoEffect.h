#pragma once

#include "stylize/MeshParticleEmitter.h"
#include "stylize/TrailEffect.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eve::stylize {

/** @brief Sampling, retention and ribbon appearance for a projectile trail. */
struct ProjectileTrailConfig {
    float sampleIntervalSeconds = 1.f / 60.f;
    float sampleLifetimeSeconds = 0.35f;
    float minimumDistance = 0.01f;
    float widthHead = 0.12f;
    float widthTail = 0.f;
    std::size_t maximumSamples = 128;
};

/** @brief Per-call observable sample and expiration counters. */
struct MotionEchoAdvanceReport {
    std::uint32_t captured = 0;
    std::uint32_t expired = 0;
    std::uint32_t distanceRejected = 0;
    std::uint32_t alive = 0;
};

/**
 * @brief Time-resampled world-space trail for projectiles and dash effects.
 *
 * The trail owns positions and ages. update() interpolates missed samples for
 * high-speed movement and performs deferred expiration after aging all points.
 */
class ProjectileTrailEffect {
public:
    /** @brief Construct with sanitized positive timing/capacity limits. */
    explicit ProjectileTrailEffect(ProjectileTrailConfig config = {});

    /** @brief Clear samples and timing state. */
    void reset() noexcept;

    /**
     * @brief Advance time and resample movement up to the current world position.
     * @param deltaSeconds Non-negative caller-owned simulation delta.
     * @param currentPosition Current projectile world position.
     * @return Capture, rejection, expiration and live counters.
     */
    [[nodiscard]] MotionEchoAdvanceReport update(float deltaSeconds,
                                                 const glm::vec3& currentPosition);

    /** @brief Build a camera-facing ribbon ordered from oldest tail to newest head. */
    [[nodiscard]] TrailMeshSnapshot build(const glm::vec3& cameraForward) const;

    /** @brief Return retained sample count. */
    [[nodiscard]] std::size_t size() const noexcept { return samples_.size(); }

private:
    struct Sample {
        glm::vec3 position{0.f};
        float age = 0.f;
    };

    bool append(const glm::vec3& position, float initialAge,
                MotionEchoAdvanceReport& report);

    ProjectileTrailConfig config_;
    std::vector<Sample> samples_;
    glm::vec3 previousInput_{0.f};
    float sampleAccumulator_ = 0.f;
    bool hasPreviousInput_ = false;
};

/** @brief Fixed-rate model snapshot and fade controls for character afterimages. */
struct AfterimageEffectConfig {
    float sampleIntervalSeconds = 0.06f;
    float lifetimeSeconds = 0.3f;
    std::size_t maximumImages = 12;
    graphics::Color colorStart{0.4f, 0.8f, 1.f, 0.65f};
    graphics::Color colorEnd{0.1f, 0.2f, 1.f, 0.f};
};

/**
 * @brief Fixed-rate owning transform history emitted as mesh-particle instances.
 *
 * Stable IDs remain valid until each snapshot expires. The class owns no mesh,
 * shader or target handle and is therefore safe to update outside rendering.
 */
class AfterimageEffect {
public:
    /** @brief Construct with sanitized timing/capacity limits. */
    explicit AfterimageEffect(AfterimageEffectConfig config = {});

    /** @brief Clear images and reset stable identity/timing state. */
    void reset() noexcept;

    /**
     * @brief Advance and capture the current model at fixed intervals.
     * @param deltaSeconds Non-negative caller-owned simulation delta.
     * @param currentModel Current world model matrix copied into new snapshots.
     * @return Capture, expiration and live counters.
     */
    [[nodiscard]] MotionEchoAdvanceReport update(float deltaSeconds,
                                                 const glm::mat4& currentModel);

    /** @brief Return render instances in oldest-to-newest stable order. */
    [[nodiscard]] std::vector<MeshParticleInstance> snapshot() const;

    /** @brief Return retained image count. */
    [[nodiscard]] std::size_t size() const noexcept { return images_.size(); }

private:
    struct Image {
        std::uint64_t stableId = 0;
        glm::mat4 model{1.f};
        float age = 0.f;
    };

    AfterimageEffectConfig config_;
    std::vector<Image> images_;
    float sampleAccumulator_ = 0.f;
    std::uint64_t nextStableId_ = 1;
};

}  // namespace eve::stylize
