#pragma once

#include "fluids/FluidSurfaceBinding.h"
#include "fluids/SurfaceWetnessField.h"

#include <glm/glm.hpp>

#include <vector>

namespace eve::fluids {

/** @brief Material and integration parameters for surface-bound droplets. */
struct SurfaceDropletParams {
    /** @brief World-space gravity in units per second squared. */
    glm::vec3 gravity{0.f, -9.8f, 0.f};
    /** @brief Exponential damping of velocity relative to the surface. */
    float friction = 1.5f;
    /** @brief Maximum relative droplet speed. */
    float maxSpeed = 12.f;
    /** @brief Maximum outward acceleration retained by adhesion. */
    float adhesionAcceleration = 12.f;
    /** @brief Maximum number of triangle edges crossed by one step. */
    int maxCrossings = 16;
    /** @brief Water contact angle in degrees, used to derive a visible cap radius. */
    float contactAngleDegrees = 72.f;
    /** @brief Multiplier for the sum of cap radii used by droplet merging. */
    float mergeRadiusScale = 0.72f;
    /** @brief Wet-film amount deposited per world-space unit travelled. */
    float trailDeposition = 0.22f;
    /** @brief Air drag applied after a droplet leaves the surface. */
    float airDrag = 0.08f;
    /** @brief Maximum distance at which an airborne droplet can reattach. */
    float reattachDistance = 0.035f;
};

/** @brief One droplet addressed in material space on a dynamic triangle surface. */
struct SurfaceDroplet {
    SurfaceLocation location;
    glm::vec3       relativeVelocity{0.f};
    glm::vec3       previousSurfaceVelocity{0.f};
    float           volume = 1.f;
    bool            hasPreviousSurfaceVelocity = false;
};

/** @brief Droplet converted from a surface-bound state to a free world-space state. */
struct DetachedDroplet {
    glm::vec3 position{0.f};
    glm::vec3 velocity{0.f};
    float     volume = 1.f;
};

/** @brief Persistent world-space droplet after it has detached from a surface. */
struct AirborneDroplet {
    glm::vec3 position{0.f};
    glm::vec3 velocity{0.f};
    float     volume = 1.f;
    float     age = 0.f;
};

/**
 * @brief CPU reference solver for droplets moving over static, rigid or deforming surfaces.
 *
 * The binding owns topology and poses; this solver owns only material-space droplet
 * addresses. It applies gravity relative to surface acceleration, transports droplets
 * across triangle adjacency, and emits world-space states at open edges or when the
 * outward acceleration exceeds adhesion.
 */
class SurfaceDropletSimulation {
public:
    /** @param binding dynamic surface; it must outlive this solver. */
    explicit SurfaceDropletSimulation(FluidSurfaceBinding* binding,
                                      const SurfaceDropletParams& params = {},
                                      SurfaceWetnessField* wetness = nullptr);

    /** @brief Add one bound droplet. Invalid locations are rejected. */
    bool addDroplet(const SurfaceLocation& location, float volume = 1.f,
                    const glm::vec3& relativeVelocity = glm::vec3(0.f));

    /** @brief Advance all bound droplets and rebuild the detached event list. */
    void step(float dt);

    /** @return currently attached droplets. */
    const std::vector<SurfaceDroplet>& droplets() const { return droplets_; }

    /** @return droplets detached during the most recent step. */
    const std::vector<DetachedDroplet>& detachedDroplets() const { return detached_; }

    /** @return droplets currently travelling through world space. */
    const std::vector<AirborneDroplet>& airborneDroplets() const { return airborne_; }

    /** @return spherical-cap base radius derived from volume and contact angle. */
    float dropletRadius(float volume) const;

    /** @return mutable solver parameters. */
    SurfaceDropletParams& params() { return params_; }

    /** @brief Remove attached droplets and pending detach events. */
    void clear();

private:
    FluidSurfaceBinding*          binding_ = nullptr;
    SurfaceDropletParams          params_;
    SurfaceWetnessField*          wetness_ = nullptr;
    std::vector<SurfaceDroplet>   droplets_;
    std::vector<DetachedDroplet>  detached_;
    std::vector<AirborneDroplet>  airborne_;
};

}  // namespace eve::fluids
