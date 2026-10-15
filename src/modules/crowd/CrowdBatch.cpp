#include "crowd/CrowdInternal.h"

#include <new>

namespace eve::crowd {
namespace {
Result<AgentBatchReceipt> failed(DiagnosticCode code, const char* message) {
    return Result<AgentBatchReceipt>::failure(Diagnostic::error(code, message, "agentBatch"));
}
bool nonNegative(const std::optional<float>& value) { return !value || (std::isfinite(*value) && *value >= 0.f); }
bool validUpdate(const AgentUpdate& update) {
    if (update.movement && ((update.movement->movement.value() == 0) != (update.movement->revision == 0))) return false;
    if (!nonNegative(update.maxSpeed) || !nonNegative(update.maxAcceleration) || !nonNegative(update.headingTurnRate) ||
        !nonNegative(update.radius))
        return false;
    if ((update.interaction && update.holdPosition) || (update.policy && update.holdPosition) ||
        (update.interaction && update.policy))
        return false;
    if (update.interaction) {
        const auto& value = *update.interaction;
        if (!std::isfinite(value.pushability) || value.pushability < 0.f || value.pushability > 1.f ||
            value.layer < 0 || value.mask < 0)
            return false;
    }
    if (update.policy && !validInteraction(*update.policy)) return false;
    if (update.intent) {
        const auto& value = *update.intent;
        if (value.action < AgentAction::Idle || value.action > AgentAction::Boids || !std::isfinite(value.targetX) ||
            !std::isfinite(value.targetY))
            return false;
    }
    return true;
}
bool duplicates(std::vector<std::uint64_t>& identities) {
    std::sort(identities.begin(), identities.end());
    return std::adjacent_find(identities.begin(), identities.end()) != identities.end();
}
}  // namespace

bool validAgentUpdate(const AgentUpdate& update) { return validUpdate(update); }
Result<AgentBatchReceipt> Crowd::updateAgents(const AgentUpdateBatch& batch) {
    auto prepared = prepareAgentUpdates(batch);
    if (!prepared) return Result<AgentBatchReceipt>::failure(prepared.status());
    return commitAgentUpdates(std::move(prepared).takeValue(), {});
}
void Crowd::Impl::applyAgentUpdate(const AgentUpdate& update, std::size_t slot) {
    auto& d = *this;
        if (update.maxSpeed) d.maxSpeeds[slot] = *update.maxSpeed;
        if (update.maxAcceleration) d.maxAccels[slot] = *update.maxAcceleration;
        if (update.headingTurnRate) d.turnRates[slot] = *update.headingTurnRate;
        if (update.radius) d.radii[slot] = *update.radius;
        if (update.flow) d.flows[slot] = *update.flow;
        if (update.movement) d.movements[slot] = *update.movement;
        if (update.avoidancePriority) d.avoidancePriorities[slot] = *update.avoidancePriority;
        if (update.data) d.datas[slot] = *update.data;
        if (update.interaction) d.interactions[slot] = canonicalInteraction(*update.interaction);
        if (update.policy) d.interactions[slot] = *update.policy;
        if (update.holdPosition) d.interactions[slot].holdPosition = *update.holdPosition;
        if (d.interactions[slot].holdPosition) d.vxs[slot] = d.vys[slot] = d.speeds[slot] = 0.f;
        if (update.intent) {
            d.actions[slot]    = static_cast<std::int32_t>(update.intent->action);
            d.hasTargets[slot] = static_cast<std::uint8_t>(update.intent->hasTarget);
            d.targetXs[slot]   = update.intent->targetX;
            d.targetYs[slot]   = update.intent->targetY;
        }

}

Result<AgentBatchReceipt> Crowd::removeAgents(const AgentRemovalBatch& batch) {
    std::vector<std::uint64_t> identities;
    try {
        identities.reserve(batch.agents.size());
        for (const auto& reference : batch.agents) {
            if (!isAgentAlive(reference)) return failed(DiagnosticCode::NotFound, "Stale or wrong-owner removal");
            identities.push_back(reference.handle.packed());
        }
    } catch (const std::bad_alloc&) {
        return failed(DiagnosticCode::Failed, "Cannot allocate removal validation workspace");
    }
    if (duplicates(identities)) return failed(DiagnosticCode::Conflict, "Duplicate agent removal");
    // All allocation and reference validation completed. Re-resolve after every swap-pop.
    for (const auto& reference : batch.agents) {
        const bool removed = removeAgent(impl_->resolveAgent(reference));
        EV_ASSERT(removed, "Validated Crowd removal must succeed without reentry");
    }
    return Result<AgentBatchReceipt>::success({batch.agents.size()});
}

Result<AgentBatchReceipt> Crowd::copyAgentFrame(AgentFrame& destination) const {
    try {
        destination.agents.reserve(impl_->xs.size());
    } catch (const std::bad_alloc&) {
        return failed(DiagnosticCode::Failed, "Cannot reserve output frame storage");
    }
    destination.agents.resize(impl_->xs.size());
    for (std::size_t slot = 0; slot < impl_->xs.size(); ++slot) {
        const auto registryIndex = impl_->denseToRegistry[slot];
        destination.agents[slot] = {
            {AgentHandle(registryIndex, impl_->registry[registryIndex].generation), impl_->owner},
            getAgentState(static_cast<int>(slot))};
    }
    destination.simulationSeconds = impl_->elapsed.seconds();
    destination.tick              = impl_->tick;
    return Result<AgentBatchReceipt>::success({destination.agents.size()});
}

Result<AgentBatchReceipt> Crowd::copyAgentStates(std::span<const AgentRef> references, AgentFrame& destination) const {
    for (const auto reference : references)
        if (!isAgentAlive(reference)) return failed(DiagnosticCode::NotFound, "Stale or wrong-owner frame reference");
    try {
        destination.agents.reserve(references.size());
    } catch (const std::bad_alloc&) {
        return failed(DiagnosticCode::Failed, "Cannot reserve selected output frame storage");
    }
    destination.agents.resize(references.size());
    for (std::size_t index = 0; index < references.size(); ++index)
        destination.agents[index] = {references[index], getAgentState(impl_->resolveAgent(references[index]))};
    destination.simulationSeconds = impl_->elapsed.seconds();
    destination.tick              = impl_->tick;
    return Result<AgentBatchReceipt>::success({references.size()});
}

}  // namespace eve::crowd
