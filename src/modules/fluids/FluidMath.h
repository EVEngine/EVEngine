#pragma once

/**
 * @brief Core math shared by the CPU reference solver and the GLSL kernels.
 *
 * FluidMath holds the SPH kernel functions (poly6 / spiky gradient /
 * viscosity Laplacian), the tuning parameters and the particle layout used by
 * both the reference implementation in FluidSimulation and the GPU kernels in
 * FluidGpuKernels. Keep the two in step: every formula here is mirrored 1:1 in
 * GLSL, and the push-constant layout is documented in FluidGpuKernels.h.
 */

#include <glm/glm.hpp>

#include <cmath>

namespace eve::fluids {

/**
 * @brief Tuning knobs of one fluid simulation.
 *
 * Units are normalized (mass = 1 per particle, rest density 1). The support
 * radius defaults to 4x the particle radius, the common PBF/SPH choice, so a
 * particle sees roughly a 2-cell neighborhood in the uniform grid.
 */
struct FluidParams {
    /** @brief Resting particle radius in world units. */
    float particleRadius = 0.05f;
    /** @brief SPH support radius h (kernel cutoff), typically 4x particleRadius. */
    float supportRadius = 0.20f;
    /** @brief Rest density; normalized to 1. */
    float restDensity = 1.0f;
    /** @brief Gravity vector in world units / s^2. */
    glm::vec3 gravity{0.f, -9.8f, 0.f};
    /** @brief XSPH viscosity strength (0 = inviscid). */
    float viscosity = 0.02f;
    /** @brief Bingham yield stress: below this shear rate particles "freeze". */
    float yieldStress = 0.0f;
    /** @brief Fluid-fluid cohesion strength (droplet formation). */
    float cohesion = 0.0f;
    /** @brief Fluid-surface adhesion strength (contact angle / sticking). */
    float adhesion = 0.0f;
    /** @brief Linear air damping applied each substep. */
    float damping = 0.0f;
    /** @brief Velocity clamp after integration. */
    float maxVelocity = 20.f;
    /** @brief Solver substeps per call to step(dt). */
    int iterations = 1;
    /** @brief PBF density-constraint relaxation passes per substep. */
    int pbfIterations = 2;
};

/** @brief One simulated particle (CPU reference layout). */
struct FluidParticle {
    glm::vec3 pos{0.f};
    glm::vec3 vel{0.f};
};

/**
 * @brief Poly6 kernel value W(r2, h).
 * @param r2 squared distance between two particles.
 * @param h support radius.
 * @return kernel value, 0 outside the support.
 */
inline float fluidPoly6(float r2, float h) {
    float h2 = h * h;
    if (r2 >= h2 || r2 <= 0.f) return 0.f;
    float q = h2 - r2;
    return 315.f / (64.f * 3.14159265358979f * std::pow(h, 9.f)) * q * q * q;
}

/**
 * @brief Spiky gradient of the kernel w.r.t. particle i position.
 * @param dx vector from neighbor j to particle i (p_i - p_j).
 * @param h support radius.
 * @return gradient vector, zero outside the support.
 */
inline glm::vec3 fluidSpikyGrad(const glm::vec3& dx, float h) {
    float r2 = glm::dot(dx, dx);
    if (r2 >= h * h || r2 <= 1e-12f) return glm::vec3(0.f);
    float r = std::sqrt(r2);
    float q = h - r;
    float k = -45.f / (3.14159265358979f * std::pow(h, 6.f));
    return dx * (k * q * q / r);
}

/**
 * @brief Viscosity Laplacian kernel value.
 * @param r distance between two particles.
 * @param h support radius.
 * @return laplacian value, 0 outside the support.
 */
inline float fluidViscLaplacian(float r, float h) {
    if (r >= h || r <= 1e-9f) return 0.f;
    return 45.f / (3.14159265358979f * std::pow(h, 6.f)) * (1.f - r / h);
}

/**
 * @brief Akinci-style cohesion kernel C(r) for droplet surface tension.
 * @param r distance between two particles.
 * @param h support radius.
 * @return kernel value, 0 outside the support.
 */
inline float fluidCohesionKernel(float r, float h) {
    if (r >= h || r <= 1e-9f) return 0.f;
    const float q = h - r;
    return 32.f / (3.14159265358979f * std::pow(h, 9.f)) * q * q * q * r * r * r;
}

/**
 * @brief Clamp a vector's magnitude to maxSpeed.
 */
inline glm::vec3 fluidClampSpeed(const glm::vec3& v, float maxSpeed) {
    float s = glm::length(v);
    if (s > maxSpeed && s > 1e-9f) return v * (maxSpeed / s);
    return v;
}

}  // namespace eve::fluids
