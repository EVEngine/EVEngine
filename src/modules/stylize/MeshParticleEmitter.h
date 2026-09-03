#pragma once

#include "graphics/Color.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace eve::stylize {

/** @brief Built-in volume used to place and orient newly spawned mesh particles. */
enum class MeshParticleShape : std::uint8_t { Point, Sphere, Box, Cone };

/** @brief One repeatable emission event on an emitter timeline. */
struct MeshParticleBurst {
    float timeSeconds = 0.f;
    std::uint32_t count = 0;
};

/** @brief One normalized-time scalar key used by mesh-particle lifetime curves. */
struct MeshParticleScalarKey {
    float time = 0.f;
    float value = 1.f;
};

/** @brief One normalized-time color key used by mesh-particle gradients. */
struct MeshParticleColorKey {
    float time = 0.f;
    graphics::Color value{1.f};
};

/** @brief Deterministic simulation and emission settings for a 3D mesh emitter. */
struct MeshParticleEmitterConfig {
    std::uint32_t capacity = 1024;
    float emissionRate = 0.f;
    float lifetimeMin = 1.f;
    float lifetimeMax = 1.f;
    float speedMin = 0.f;
    float speedMax = 0.f;
    glm::vec3 direction{0.f, 1.f, 0.f};
    MeshParticleShape shape = MeshParticleShape::Point;
    glm::vec3 boxExtents{0.5f};
    float sphereRadius = 0.5f;
    float coneAngleRadians = 0.5f;
    glm::vec3 gravity{0.f};
    float damping = 0.f;
    float rotationMinRadians = 0.f;
    float rotationMaxRadians = 0.f;
    float angularVelocityMin = 0.f;
    float angularVelocityMax = 0.f;
    float scaleStart = 1.f;
    float scaleEnd = 0.f;
    graphics::Color colorStart{1.f};
    graphics::Color colorEnd{1.f, 1.f, 1.f, 0.f};
    /** @brief Optional normalized-age scale multiplier; empty uses scaleStart/scaleEnd. */
    std::vector<MeshParticleScalarKey> scaleCurve;
    /** @brief Optional normalized-age velocity multiplier; empty evaluates to one. */
    std::vector<MeshParticleScalarKey> velocityCurve;
    /** @brief Optional normalized-age color gradient; empty uses colorStart/colorEnd. */
    std::vector<MeshParticleColorKey> colorGradient;
    float fixedStepSeconds = 1.f / 60.f;
    std::uint32_t maximumSubsteps = 8;
    std::uint32_t randomSeed = 1;
    float loopDurationSeconds = 0.f;
    bool looping = false;
    std::vector<MeshParticleBurst> bursts;
};

/** @brief Render-facing immutable state of one live mesh particle. */
struct MeshParticleInstance {
    std::uint64_t stableId = 0;
    glm::mat4 model{1.f};
    graphics::Color color{1.f};
    float normalizedAge = 0.f;
};

/** @brief Observable result of advancing one mesh-particle emitter. */
struct MeshParticleAdvanceReport {
    std::uint32_t simulatedSteps = 0;
    std::uint32_t spawned = 0;
    std::uint32_t expired = 0;
    std::uint32_t dropped = 0;
    std::uint32_t alive = 0;
    float discardedTimeSeconds = 0.f;
};

/**
 * @brief CPU-authoritative deterministic 3D mesh-particle emitter.
 *
 * The emitter owns particle state and retains no graphics resources. Given the
 * same config, seed, origin, command sequence and delta sequence, snapshots are
 * bitwise stable on the same floating-point implementation. Structural changes
 * are deferred until each simulation step has finished iterating live particles.
 */
class MeshParticleEmitter {
public:
    /** @brief Construct an emitter and sanitize invalid scalar ranges. */
    explicit MeshParticleEmitter(MeshParticleEmitterConfig config = {});

    /** @brief Destroy owned particle state after its private type is complete. */
    ~MeshParticleEmitter();

    /** @brief Start from timeline zero and arm every configured burst. */
    void start();

    /** @brief Stop automatic emission while allowing existing particles to expire. */
    void stop() noexcept { emitting_ = false; }

    /** @brief Remove every particle and reset timeline/random state. */
    void reset();

    /** @brief Set the world-space origin copied by subsequent simulation steps. */
    void setOrigin(const glm::vec3& origin) noexcept { origin_ = origin; }

    /**
     * @brief Emit an immediate burst at the current origin.
     * @param count Requested particle count.
     * @return Number accepted; capacity overflow is reflected by advance reports.
     */
    [[nodiscard]] std::uint32_t emit(std::uint32_t count);

    /**
     * @brief Advance deterministic fixed-step simulation.
     * @param deltaSeconds Non-negative frame delta; invalid values advance nothing.
     * @return Per-call work, lifecycle and overflow counters.
     */
    [[nodiscard]] MeshParticleAdvanceReport advance(float deltaSeconds);

    /** @brief Build render transforms and colors in stable particle-ID order. */
    [[nodiscard]] std::vector<MeshParticleInstance> snapshot() const;

    /** @brief Return immutable sanitized configuration. */
    [[nodiscard]] const MeshParticleEmitterConfig& config() const noexcept { return config_; }

    /** @brief Return the number of live particles. */
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Particle;

    void simulateStep(float dt, MeshParticleAdvanceReport& report);
    [[nodiscard]] std::uint32_t spawn(std::uint32_t count, MeshParticleAdvanceReport* report);
    [[nodiscard]] float randomUnit();
    [[nodiscard]] glm::vec3 randomDirection();
    [[nodiscard]] glm::vec3 spawnOffset();

    MeshParticleEmitterConfig config_;
    std::vector<Particle> particles_;
    glm::vec3 origin_{0.f};
    std::uint32_t randomState_ = 1;
    std::uint64_t nextStableId_ = 1;
    float accumulator_ = 0.f;
    float timeline_ = 0.f;
    float emissionAccumulator_ = 0.f;
    std::size_t nextBurst_ = 0;
    bool emitting_ = false;
};

}  // namespace eve::stylize
