#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "gpuagents/AgentState.h"
#include "gpuagents/EffectProfile.h"
#include "gpuagents/EnvironmentSnapshot.h"
#include "gpuagents/IAgentSolver.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace eve::gpuagents {

/**
 * @brief Fixed-step, double-buffered agent simulation owner.
 *
 * Owns agent capacity, ping/pong state buffers, accumulator, and the active solver.
 * Does not own World environment data — callers pass an EnvironmentSnapshot each step.
 */
class EVENGINE_API_DOMAINS GpuAgentSimulation {
public:
    GpuAgentSimulation() = default;

    /**
     * @brief Configure capacity and bind a solver for the profile kind.
     * @param profile Effect parameters (copied).
     * @return Ok or InvalidArgument when capacity / kind is illegal.
     */
    [[nodiscard("check simulation configure")]] Result<void> configure(const EffectProfile& profile);

    /**
     * @brief Replace the solver instance (tests / custom kernels).
     * @ownership Simulation takes ownership.
     */
    [[nodiscard("check solver bind")]] Result<void> setSolver(std::unique_ptr<IAgentSolver> solver);

    /**
     * @brief Spawn `count` alive agents using `spawnFn(index) -> AgentState`.
     * @param count Number of agents to activate (≤ capacity).
     * @param spawnFn Per-index initializer; age/alive filled if left zero.
     */
    [[nodiscard("check initialize")]] Result<void> initialize(int count,
                                                              const std::function<AgentState(int)>& spawnFn);

    /** @brief Clear all agents and the time accumulator. */
    void reset();

    /**
     * @brief Advance wall-clock `dt` via fixed substeps.
     * @param dt Non-negative frame delta seconds.
     * @param env Environment snapshot for this tick.
     * @return Ok, or FailedPrecondition when unconfigured.
     */
    [[nodiscard("check simulation step")]] Result<void> step(float dt, const EnvironmentSnapshot& env);

    /** @brief Active agent capacity. */
    int capacity() const { return capacity_; }
    /** @brief Number of alive agents. */
    int aliveCount() const;
    /** @brief Current front-buffer states (read-only). */
    const std::vector<AgentState>& states() const { return buffers_[front_]; }
    /** @brief Mutable front buffer (editor preview / tests). */
    std::vector<AgentState>& states() { return buffers_[front_]; }
    /** @brief Bound profile. */
    const EffectProfile& profile() const { return profile_; }
    /** @brief True after successful configure. */
    bool isConfigured() const { return configured_; }
    /** @brief Accumulated simulation time in seconds. */
    float simTime() const { return simTime_; }
    /** @brief Completed fixed substeps. */
    std::uint64_t stepCount() const { return stepCount_; }

private:
    EffectProfile                   profile_{};
    std::unique_ptr<IAgentSolver>   solver_;
    std::vector<AgentState>         buffers_[2]{};
    int                             front_      = 0;
    int                             capacity_   = 0;
    float                           accumulator_ = 0.f;
    float                           simTime_     = 0.f;
    std::uint64_t                   stepCount_   = 0;
    bool                            configured_  = false;

    [[nodiscard]] Result<void> makeDefaultSolver();
};

}  // namespace eve::gpuagents
