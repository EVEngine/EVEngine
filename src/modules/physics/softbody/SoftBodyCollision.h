#pragma once

namespace eve::physics::softbody {

/** @brief Backend-neutral contact returned to the volumetric solver. */
struct SoftBodyContact {
    bool  hit = false;
    float nx = 0.f, ny = 0.f, nz = 0.f;
    float depth       = 0.f;
    int   bodyId      = -1;
    bool  dynamicBody = false;
};

/** @brief Strong result of a particle contact probe. */
enum class SoftBodyContactState { None, Hit };

/**
 * @brief Narrow borrowed collision service consumed by the soft-body core.
 *
 * Implementations own all referenced rigid bodies. The solver retains only
 * this interface pointer; callers must clear it before provider destruction.
 */
class ISoftBodyCollisionWorld {
public:
    virtual ~ISoftBodyCollisionWorld() = default;
    /** @brief Whether the provider can currently answer contacts. */
    [[nodiscard]] virtual bool softBodyCollisionAvailable() const noexcept = 0;
    /** @brief Probe the deepest non-sensor contact for a spherical particle. */
    [[nodiscard]] virtual SoftBodyContactState probeSoftBodyParticle(float x, float y, float z, float radius,
                                                                     SoftBodyContact& contact) const = 0;
    /** @brief Apply a world-space impulse to a previously reported dynamic body id. */
    virtual void applySoftBodyImpulse(int bodyId, float x, float y, float z) = 0;
};

}  // namespace eve::physics::softbody
