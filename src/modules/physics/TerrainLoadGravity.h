#pragma once

#include "common/Result.h"

namespace eve::physics {
class Body3D;

/** @brief Caller-owned state for Pcg-style rigid-body activation after terrain loading. */
class TerrainLoadGravityState {
public:
    /** @brief Whether a containing terrain was found and activation is being monitored. */
    bool isMonitoring() const { return monitoring_; }
    /** @brief Whether the post-load delay has been scheduled. */
    bool isActivationScheduled() const { return activationScheduled_; }
    /** @brief Whether gravity restoration and the activation notification completed. */
    bool isCompleted() const { return completed_; }
    /** @brief Elapsed seconds since the first loaded observation. */
    float getDelayElapsed() const { return delayElapsed_; }

private:
    friend Result<void> beginTerrainLoadGravity(TerrainLoadGravityState*, Body3D*, bool);
    friend Result<bool> advanceTerrainLoadGravity(TerrainLoadGravityState*, Body3D*, bool, float,
                                                   float);
    bool monitoring_ = false;
    bool activationScheduled_ = false;
    bool completed_ = false;
    float delayElapsed_ = 0.f;
    float gravityScale_ = 1.f;
};

/**
 * @brief Begin monitoring a body when a containing terrain scene exists.
 * @param state Required caller-owned state, initialized exactly once.
 * @param body Required caller-owned dynamic body.
 * @param terrainFound Whether the caller found a terrain scene containing the body.
 * @return Success, or InvalidArgument before mutation.
 * @ownership State and body remain caller-owned and are not retained.
 * @thread Physics simulation owner thread only.
 */
[[nodiscard]] Result<void> beginTerrainLoadGravity(TerrainLoadGravityState* state, Body3D* body,
                                                    bool terrainFound);

/**
 * @brief Advance terrain-load waiting and restore gravity after the configured delay.
 * @param state State initialized by beginTerrainLoadGravity.
 * @param body The same live caller-owned body supplied at begin.
 * @param terrainLoaded Current regular terrain load state.
 * @param deltaSeconds Injected finite non-negative frame duration.
 * @param activationDelay Finite non-negative delay after the first loaded observation.
 * @return True exactly once when gravity is restored and caller-owned components should activate.
 * @ownership State and body remain caller-owned and are not retained.
 * @thread Physics simulation owner thread only; no callbacks are invoked.
 */
[[nodiscard]] Result<bool> advanceTerrainLoadGravity(TerrainLoadGravityState* state, Body3D* body,
                                                      bool terrainLoaded, float deltaSeconds,
                                                      float activationDelay);
}
