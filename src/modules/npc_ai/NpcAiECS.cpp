#include "npc_ai/NpcAiECS.h"

#include <array>

namespace eve::npc_ai {

Result<NpcAgentState> NpcAgentState::create(NpcAiWorld& world, std::string_view behaviorId) {
    auto created = world.createAgent(behaviorId);
    if (!created.ok()) return Result<NpcAgentState>::failure(created.status());
    return Result<NpcAgentState>::success({created.value()});
}

Result<void> NpcAgentState::releaseAgent(NpcAiWorld& world) {
    if (!agent.isValid()) return Result<void>::success(Status::success(StatusCode::NoOp));
    if (world.isStale(agent)) {
        agent = AgentHandle::invalid();
        return Result<void>::success(Status::success(StatusCode::NoOp));
    }
    auto released = world.destroyAgent(agent);
    if (!released.ok()) return Result<void>::failure(released.status());
    agent = AgentHandle::invalid();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

std::span<const NpcAiSystemContract> npcAiSystemContracts() noexcept {
    static constexpr std::array<NpcAiSystemContract, 1> contracts{{
        {"npc_ai.decision_projection", "caller-selected gameplay short root",
         "View<EntityRoot, NpcAgentState, NpcAiProjection>", "NpcAgentState; injected TickContext",
         "NpcAiProjection; module-owned NpcAiWorld", "none", "AI trace/signals inside NpcAiWorld",
         "NpcAiWorld and owned task providers", "GameplayDecision",
         "stable handle order; injected tick/dt; provider-defined tolerance"},
    }};
    return contracts;
}

}  // namespace eve::npc_ai
