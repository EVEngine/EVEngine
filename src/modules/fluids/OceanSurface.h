#pragma once

#include "common/Result.h"

#include <glm/glm.hpp>
#include <span>
#include <vector>

namespace eve::fluids {

/** @brief One deep-water Gerstner component; metres, radians, seconds, +Y up. */
struct OceanWave {
    glm::dvec2 direction{1.0, 0.0};
    double     amplitude  = 0.25;
    double     wavelength = 12.0;
    double     phase      = 0.0;
    double     choppiness = 0.5;
};

/** @brief Immutable authored sea inputs copied into OceanSurface. */
struct OceanSurfaceSettings {
    double                 seaLevel = 0.0;
    double                 gravity  = 9.81;
    glm::dvec2             current{0.0};
    std::vector<OceanWave> waves;
};

/** @brief Owning sample of the displaced surface at a requested world XZ and time. */
struct OceanSurfaceSample {
    double     height = 0.0;
    glm::dvec3 normal{0.0, 1.0, 0.0};
    /** @brief Gerstner surface particle velocity plus uniform current; a water-velocity approximation. */
    glm::dvec3 velocity{0.0};
    glm::dvec2 parameterPosition{0.0};
    double     time               = 0.0;
    double     horizontalJacobian = 1.0;
};

/**
 * @brief Immutable analytic sea shared by geometry and buoyancy consumers.
 * Owns every wave; no scene, GPU or physics pointer is retained. Concurrent const
 * queries are safe. Time is supplied by the simulation owner, never read internally.
 * Identical inputs are repeatable on one backend; cross-platform parity is numerical,
 * not bitwise. The sum of horizontal steepness is bounded to keep inversion unique.
 */
class OceanSurface {
public:
    /**
     * @brief Validate and copy a wave set, normalizing direction vectors.
     * @param settings Borrowed only during this call; at most 64 finite components.
     * @return Owned immutable surface or InvalidArgument; no external state changes.
     */
    [[nodiscard]] static Result<OceanSurface> create(const OceanSurfaceSettings& settings);

    /**
     * @brief Invert horizontal displacement and sample the actual world-space surface.
     * @param worldXZ Finite world coordinates in metres, magnitude at most 1e9.
     * @param time Injected seconds, magnitude at most 1e9.
     * @return Height, normal and approximate particle velocity, or InvalidArgument.
     * @thread Const, reentrant; no callbacks and no retained input references.
     */
    [[nodiscard]] Result<OceanSurfaceSample> sample(glm::dvec2 worldXZ, double time) const;

    /**
     * @brief Displace a material-space mesh point using the same wave function as queries.
     * @return World position or InvalidArgument for invalid coordinates/time.
     * @thread Const and reentrant; no retained inputs or callbacks.
     */
    [[nodiscard]] Result<glm::dvec3> position(glm::dvec2 parameterXZ, double time) const;

private:
    explicit OceanSurface(OceanSurfaceSettings settings) : settings_(std::move(settings)) {}
    OceanSurfaceSettings settings_;
};

/** @brief Sphere with assigned displacement volume; several disjoint volumes approximate a hull. */
struct OceanBuoyancyCell {
    glm::dvec3 center{0.0};
    glm::dvec3 pointVelocity{0.0};
    double     radius          = 0.5;
    double     volume          = 0.5;
    double     dragCoefficient = 1.0;
};

/** @brief Owning force at a hull point; the rigid-body owner applies it once per fixed step. */
struct OceanCellForce {
    glm::dvec3 position{0.0};
    glm::dvec3 force{0.0};
    double     submergedVolume = 0.0;
};

/**
 * @brief Integrate spherical-cap buoyancy and isotropic quadratic water drag.
 * @param cell Borrowed world-space hull cell; volume is assigned, not multiplied by sampling density.
 * @param water Borrowed sample at the cell's XZ and the current physics time.
 * @param density Positive water density in kg/m3.
 * @param gravity Positive gravitational acceleration in m/s2.
 * @return Owning force or InvalidArgument; neither input nor a physics body is mutated.
 * @thread Pure synchronous operation; no callbacks or retained references.
 */
[[nodiscard]] Result<OceanCellForce> evaluateOceanCellForce(const OceanBuoyancyCell&  cell,
                                                            const OceanSurfaceSample& water, double density,
                                                            double gravity);

}  // namespace eve::fluids
