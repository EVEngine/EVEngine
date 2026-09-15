#pragma once
#include <memory>
#include <span>
#include "common/Result.h"

namespace eve::physics {
class Body3D;
class World3D;
}  // namespace eve::physics
namespace eve::fluids {
class VolumeFluid;
struct VolumeFluidCollider;
struct VolumeFluidThermalRule;

/** @brief Process-local generation-checked bridge between fluid samples and rigid bodies.
 * @ownership Owns local collider descriptions and typed physics links, never world/body/solver pointers.
 * @thread Simulation-thread affine; no callbacks, concurrent mutation or reentrancy.
 * @details Step exclusively supplies the solver's collider set. Destroying this bridge
 * leaves both worlds alive; destroying a body/world makes the stored link stale.
 * After restore/hot reload, reconstruct links from application persistent identity.
 * This bridge has no serialized runtime handles and invokes no world.step callbacks.
 */
class VolumeFluidCoupling final {
public:
    VolumeFluidCoupling();
    ~VolumeFluidCoupling();
    VolumeFluidCoupling(const VolumeFluidCoupling&)            = delete;
    VolumeFluidCoupling& operator=(const VolumeFluidCoupling&) = delete;
    /** @brief Registers a live body and local-space shape; labels must be unique, maximum 1024. */
    [[nodiscard]] Result<void> attach(physics::Body3D& body, const VolumeFluidCollider& localShape);
    /** @brief Removes a label; absent labels are a successful no-op. */
    void detach(unsigned label);
    /** @brief Sets per-step rigid velocity-change limits used after impulse aggregation.
     * @param maximumLinearDelta Maximum center-of-mass speed change in [0.01,1000] m/s.
     * @param maximumAngularDelta Maximum angular-speed change in [0.01,10000] rad/s.
     * @details Simulation-thread only. Defaults are 20 m/s and 100 rad/s. The
     * complete fluid step remains authoritative; only the outgoing partitioned
     * rigid impulse is scaled. No callback or external object is retained.
     */
    [[nodiscard]] Result<void> setImpulseLimits(float maximumLinearDelta, float maximumAngularDelta);
    /** @brief Returns how many linked bodies had a linear or angular impulse scaled in the last successful step. */
    [[nodiscard]] unsigned lastClampedBodyCount() const;
    /** @brief Resolves all links, samples poses, advances fluid and applies opposite impulses once.
     * @return Number of contact and attachment-reaction contributions applied. Stale links fail before either domain
     * mutates.
     * @details All linked bodies must belong to the supplied world. Forces are
     * aggregated about each body's center of mass, preserving the contact torque.
     * Prescribed attached solids return their linear inertia and gravity load; this
     * includes ellipsoid angular inertia and is separate from collider-contact
     * impulses; each contribution is applied once.
     * Rigid integration is caller-owned: call world.step after this operation.
     * This is partitioned coupling, not a shared iterative rigid/fluid solve.
     * Optional thermal rules use sampled collider labels in the same fluid step.
     * Rules are borrowed only for this call; invalid rules leave both domains unchanged.
     * Aggregated impulses are bounded by setImpulseLimits to prevent a light rigid
     * body from destabilizing the next partitioned fluid sample.
     */
    [[nodiscard]] Result<unsigned> step(physics::World3D& world, VolumeFluid& fluid, float dt, unsigned substeps = 4,
                                        std::span<const VolumeFluidThermalRule> rules = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::fluids
