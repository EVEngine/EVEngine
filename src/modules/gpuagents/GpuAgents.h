#pragma once
#include "common/Export.h"

#include "common/Module.h"
#include "common/Result.h"
#include "gpuagents/EffectBackend.h"
#include "gpuagents/EffectProfile.h"
#include "gpuagents/GpuAgentWorld.h"
#include "gpuagents/LifeFieldMaterialBinding.h"
#include "gpuagents/SurfaceCapture.h"
#include "gpuagents/SurfaceField.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

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

    /**
     * @brief Orthographic triangle bake into a world's surface field.
     * @param world Borrowed world whose surface is rewritten.
     * @param positions Interleaved xyz floats.
     * @param indices Triangle indices.
     * @param originX Domain origin X.
     * @param originY Domain origin Y (also base height).
     * @param originZ Domain origin Z.
     * @param worldSize Domain side length.
     * @param resolution Field resolution.
     * @ownership Borrowed world; positions/indices copied for the call only.
     * @lifetime world must outlive the call.
     */
    [[nodiscard("check surface capture")]] Result<void> captureSurface(GpuAgentWorld*                    world,
                                                                       const std::vector<float>&         positions,
                                                                       const std::vector<std::uint32_t>& indices,
                                                                       float originX, float originY, float originZ,
                                                                       float worldSize, int resolution) const;

    /**
     * @brief Pack a world's surface into CPU LifeField / SurfaceData RGBA buffers.
     * @param world Borrowed world with an initialized surface.
     * @param binding Destination binding (caller-owned).
     * @ownership Borrowed; does not take ownership of world or binding.
     * @lifetime Both arguments must outlive the call.
     */
    [[nodiscard("check life-field sync")]] Result<void> syncLifeFieldBinding(GpuAgentWorld*            world,
                                                                             LifeFieldMaterialBinding* binding) const;
};

}  // namespace eve::gpuagents
