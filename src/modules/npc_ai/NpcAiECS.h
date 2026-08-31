#pragma once

#include "common/ECS.h"
#include "npc_ai/NpcAi.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eve::npc_ai {

/**
 * @brief Authoritative gameplay-entity link to one module-owned NPC agent.
 * @remarks The gameplay entity creates and releases the agent. Deferred ECS copies
 * only copy the generation handle; release invalidates every copy. If the AI agent
 * is destroyed first, release observes stale state and clears the link as NoOp.
 * Runtime handles are transient and must be rebuilt from logical behavior ids after restore.
 */
struct NpcAgentState {
    AgentHandle agent;

    /** @brief Creates the canonical module-owned agent referenced by this component. */
    [[nodiscard]] static Result<NpcAgentState> create(NpcAiWorld& world, std::string_view behaviorId);
    /** @brief Releases the owned agent once and clears this component's handle. */
    [[nodiscard]] Result<void> releaseAgent(NpcAiWorld& world);
};

/** @brief Cold derived, local-only projection for rendering, editor and gameplay inspection. */
struct NpcAiProjection {
    std::string              activeState;
    std::vector<std::string> activePath;
    std::uint64_t            lastTick = 0;

    /** @brief Atomically replaces derived fields from an owning runtime snapshot. */
    void replace(const AgentSnapshot& snapshot) {
        activeState = snapshot.activeState;
        activePath  = snapshot.activePath;
        lastTick    = snapshot.lastTick;
    }
};

struct NpcAiEcsStepReport {
    std::size_t linkedEntities = 0;
    TickReport  runtime;
};

struct NpcAiSystemContract {
    std::string_view name;
    std::string_view entityScope;
    std::string_view view;
    std::string_view readSet;
    std::string_view writeSet;
    std::string_view structuralChanges;
    std::string_view events;
    std::string_view services;
    std::string_view phase;
    std::string_view determinism;
};

/** @brief Returns immutable process-lifetime metadata for the NPC AI ECS phase. */
[[nodiscard]] std::span<const NpcAiSystemContract> npcAiSystemContracts() noexcept;

/** @brief Gameplay-decision phase adapter over a caller-selected existing entity root. */
class NpcAiEcsSystem {
public:
    /**
     * @brief Preflights every link, advances the AI world once, then publishes projections.
     * @tparam EntityRoot Existing project/domain short root; no NPC entity base is introduced.
     * @param world Synchronously borrowed AI world; never retained.
     * @param context Injected deterministic time and budgets.
     * @return Entity and runtime work report, or the first structured stale/provider failure.
     * @thread Owning ECS simulation thread.
     * @reentrancy Provider callbacks run only after the preflight View scope closes.
     * @remarks Reads NpcAgentState, writes NpcAiProjection, performs no ECS structural mutation,
     * and publishes no callbacks while iterating a View.
     */
    template <class EntityRoot>
    [[nodiscard]] static Result<NpcAiEcsStepReport> step(NpcAiWorld& world, const TickContext& context) {
        std::size_t linked = 0;
        {
            auto view = ecs::View<EntityRoot, NpcAgentState, NpcAiProjection>();
            for (auto it = view.begin(); it != view.end(); ++it) {
                auto [state, projection] = *it;
                (void)projection;
                auto current = world.snapshot(state->agent);
                if (!current.ok()) return Result<NpcAiEcsStepReport>::failure(current.status());
                ++linked;
            }
        }

        auto advanced = world.tick(context);
        if (!advanced.ok()) return Result<NpcAiEcsStepReport>::failure(advanced.status());

        {
            auto view = ecs::View<EntityRoot, NpcAgentState, NpcAiProjection>();
            for (auto it = view.begin(); it != view.end(); ++it) {
                auto [state, projection] = *it;
                auto current             = world.snapshot(state->agent);
                if (!current.ok()) return Result<NpcAiEcsStepReport>::failure(current.status());
                projection->replace(current.value());
            }
        }
        return Result<NpcAiEcsStepReport>::success({linked, advanced.value()});
    }
};

}  // namespace eve::npc_ai
