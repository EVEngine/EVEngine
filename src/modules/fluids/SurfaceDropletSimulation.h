#pragma once
#include "common/Export.h"


#include "fluids/FluidSurfaceBinding.h"
#include "fluids/SurfaceWetnessField.h"

#include <glm/glm.hpp>

#include <cstdint>
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
    uint64_t        id = 0;
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
class EVENGINE_API_DOMAINS SurfaceDropletSimulation {
public:
    /** @param binding dynamic surface; it must outlive this solver. */
    /** @brief Surface droplet simulation. */
    explicit SurfaceDropletSimulation(FluidSurfaceBinding* binding,
                                      const SurfaceDropletParams& params = {},
                                      SurfaceWetnessField* wetness = nullptr);

    /** @brief Add one bound droplet. Invalid locations are rejected. */
    bool addDroplet(const SurfaceLocation& location, float volume = 1.f,
                    const glm::vec3& relativeVelocity = glm::vec3(0.f));

    /** @brief Advance all bound droplets and rebuild the detached event list. */
    void step(float dt);

    /** @return currently attached droplets. */
    /** @brief Droplets. */
    const std::vector<SurfaceDroplet>& droplets() const { return droplets_; }

    /** @return droplets detached during the most recent step. */
    /** @brief Detached droplets. */
    const std::vector<DetachedDroplet>& detachedDroplets() const { return detached_; }

    /** @return droplets currently travelling through world space. */
    /** @brief Airborne droplets. */
    const std::vector<AirborneDroplet>& airborneDroplets() const { return airborne_; }

    /** @return spherical-cap base radius derived from volume and contact angle. */
    /** @brief Droplet radius. */
    float dropletRadius(float volume) const;

    /** @return mutable solver parameters. */
    /** @brief Params. */
    SurfaceDropletParams& params() { return params_; }

    /** @return solver parameters. */
    /** @brief Params. */
    const SurfaceDropletParams& params() const { return params_; }

    /** @brief Remove attached droplets and pending detach events. */
    void clear();

private:
    void stepSubstep(float dt, float poseDt);

    FluidSurfaceBinding*          binding_ = nullptr;
    SurfaceDropletParams          params_;
    SurfaceWetnessField*          wetness_ = nullptr;
    std::vector<SurfaceDroplet>   droplets_;
    std::vector<DetachedDroplet>  detached_;
    std::vector<AirborneDroplet>  airborne_;
    uint64_t                      nextDropletId_ = 1;
};

}  // namespace eve::fluids
