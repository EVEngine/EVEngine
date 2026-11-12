#pragma once

#include "physics/field/GravityField.h"

namespace eve::physics::field {

/** @brief One independently integrated point body; index identity is stable for this simulation's lifetime. */
struct MotionState {
    Particle              particle;
    std::array<double, 3> velocity{};
};

/** @brief Owning state for publication; never contains transient scene or body handles. */
struct MotionSnapshot {
    FieldStamp               stamp;
    std::vector<MotionState> states;
};

/**
 * @brief Prepared velocity-Verlet step, owning the candidate state but not its simulation.
 * @ownership Single-use move-only candidate. Destroying it cancels publication without changing live state.
 * @thread Owner-thread prepare/commit; no callbacks. Owner destruction makes the candidate uncommittable.
 */
class EVENGINE_API_FOUNDATION PreparedGravityStep {
public:
    /** @brief Discard candidate state without committing. */
    ~PreparedGravityStep();
    /** @brief Transfer the publication right. */
    PreparedGravityStep(PreparedGravityStep&&) noexcept;
    /** @brief Discard the old candidate and transfer the publication right. */
    PreparedGravityStep& operator=(PreparedGravityStep&&) noexcept;
    PreparedGravityStep(const PreparedGravityStep&)            = delete;
    PreparedGravityStep& operator=(const PreparedGravityStep&) = delete;

private:
    friend class GravitySimulation;
    struct Impl;
    explicit PreparedGravityStep(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Explicitly owned point-body simulation; never registers itself with the existing physics loop.
 * @ownership Sole writer of its motion state. Rigid-backend bodies must not be independently advanced here.
 * @thread All operations are serialized by the owner; no locks, callbacks, wall clock or global RNG.
 * @remarks Numerically deterministic for the same build/inputs; cross-platform tolerance, not bit-exact.
 */
class EVENGINE_API_FOUNDATION GravitySimulation {
public:
    /** @brief Destroy state and revoke all outstanding candidates. */
    ~GravitySimulation();
    /** @brief Transfer state ownership and outstanding candidate identity. */
    GravitySimulation(GravitySimulation&&) noexcept;
    /** @brief Revoke this owner's candidates and transfer another simulation. */
    GravitySimulation& operator=(GravitySimulation&&) noexcept;
    GravitySimulation(const GravitySimulation&)            = delete;
    GravitySimulation& operator=(const GravitySimulation&) = delete;

    /**
     * @brief Validate/copy initial states and compute the first acceleration using the fixed policy.
     * @return Owning simulation, or a structured field/input/budget failure.
     * @cost One field build/evaluation plus O(N) copy and storage per creation; initial force is not free.
     * @lifetime Input borrowed only for this synchronous call; no references escape.
     */
    [[nodiscard]] static Result<GravitySimulation> create(std::span<const MotionState> initial, FieldStamp stamp,
                                                          GravityPolicy policy, Duration fixedStep);
    /**
     * @brief Compute a complete next-tick candidate without mutating current state.
     * @return Owning candidate; rejects skipped/repeated ticks, invalid dt, singularity and budget failure.
     * @cost One field build/evaluation plus O(N) drift/kick work and staging storage per step.
     * @remarks Only positive, explicit simulation durations are accepted; failure consumes no simulation time.
     */
    [[nodiscard]] Result<PreparedGravityStep> prepareStep(SimulationStep step) const;
    /**
     * @brief Consume a candidate and atomically publish if its owner and base stamp are still current.
     * @return Success or stale/foreign candidate rejection; failure never mutates published state.
     * @cost O(N) destruction of old buffers, constant-time ownership swaps; no rebuilding or callbacks.
     */
    [[nodiscard]] Result<void> commit(PreparedGravityStep step);
    /**
     * @brief Copy current published state, without triggering simulation or generation.
     * @return Owning snapshot, or moved-from/allocation failure.
     * @cost O(N) copy and storage per explicit snapshot request; amortize across consumers.
     */
    [[nodiscard]] Result<MotionSnapshot> snapshot() const;
    /** @brief Return the last successfully published field's owning work counters. */
    [[nodiscard]] GravityMetrics metrics() const noexcept;

private:
    struct Impl;
    explicit GravitySimulation(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::physics::field
