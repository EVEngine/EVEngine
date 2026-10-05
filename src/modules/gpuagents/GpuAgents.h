#pragma once
#include "common/Export.h"

#include "common/Module.h"
#include "common/Result.h"
#include "gpuagents/EffectBackend.h"
#include "gpuagents/EffectProfile.h"
#include "gpuagents/GpuAgentWorld.h"

#include <memory>

namespace eve::gpuagents {

/**
 * @brief GPU Agents module — extensible fixed-step agent FX simulation.
 *
 * Script: `gpuAgents <- eve.GpuAgents();`
 *
 * Four-layer architecture: EffectProfile → EffectBackend → Solver → Renderer,
 * orchestrated by GpuAgentWorld for environment registration and by
 * GpuAgentSimulation for fixed-step double buffers.
 */
class EVENGINE_API_DOMAINS GpuAgents : public Module {
public:
    Module_REG(GpuAgents);

    /**
     * @brief Create an empty World.
     * @ownership Caller / script VM owns the returned world; delete or let the VM free it.
     * @lifetime Valid until the owner destroys it; not retained by the module.
     * @nullable No on success; factory failure is an invariant.
     * @thread Main/composition thread only.
     */
    [[nodiscard("world ownership must be retained")]] GpuAgentWorld* newWorld();

    /**
     * @brief Create a configured EffectBackend.
     * @param kind 0=Fish, 1=LifeNetwork, 2=Bird, 3=Petal.
     * @param maxAgents Capacity.
     * @ownership Caller / script VM owns the returned backend.
     * @lifetime Valid until the owner destroys it; register with World only as borrowed.
     * @nullable Yes when configure fails; caller must check for nullptr.
     * @thread Main/composition thread only.
     */
    [[nodiscard("backend ownership must be retained")]] EffectBackend* newBackend(int kind, int maxAgents);

    /**
     * @brief Build a default EffectProfile value for script helpers.
     * @param kind Effect kind integer.
     * @param maxAgents Capacity.
     */
    [[nodiscard]] EffectProfile makeProfile(int kind, int maxAgents) const;

    /**
     * @brief Spawn a simple school / flock / petal cloud into a backend.
     * @param backend Target backend; borrowed, must already be configured.
     * @param count Alive agent count.
     * @param centerX Spawn center X.
     * @param centerY Spawn center Y.
     * @param centerZ Spawn center Z.
     * @param spread Spawn radius.
     * @ownership Borrowed backend; this call does not take ownership.
     * @lifetime `backend` must remain valid for the duration of the call.
     */
    [[nodiscard("check spawn")]] Result<void> spawnCloud(EffectBackend* backend, int count, float centerX,
                                                         float centerY, float centerZ, float spread) const;
};

}  // namespace eve::gpuagents
