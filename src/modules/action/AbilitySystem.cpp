#include "action/AbilitySystem.h"

#include <algorithm>
#include <utility>

namespace eve::action {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<void> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

bool terminal(ActionPhase phase) {
    return phase == ActionPhase::Completed || phase == ActionPhase::Cancelled || phase == ActionPhase::Failed;
}

template <typename Id>
Result<Id> takeNext(Id& next, std::string_view domain) {
    const Id   value       = next;
    const auto incremented = next.incremented();
    if (!incremented) return failure<Id>(DiagnosticCode::Conflict, std::string(domain) + " identity space exhausted");
    next = *incremented;
    return Result<Id>::success(value);
}

}  // namespace

Result<void> AbilityDefinition::validate() const {
    if (!id.isValid()) return failure(DiagnosticCode::InvalidArgument, "Ability id is invalid", "id");
    if (cooldown < Duration::zero())
        return failure(DiagnosticCode::InvalidArgument, "Ability cooldown must be non-negative", "cooldown");
    auto actionValid = action.validate();
    if (!actionValid) return Result<void>::failure(actionValid.status());
    for (std::size_t i = 0; i < triggers.size(); ++i) {
        if (!tags::isValidGameplayTagName(triggers[i].gameplayTag))
            return failure(DiagnosticCode::InvalidArgument, "Ability trigger tag is invalid",
                           "triggers[" + std::to_string(i) + "].gameplayTag");
    }
    return Result<void>::success();
}

Result<void> AbilityRuntime::registerDefinition(AbilityDefinition definition) {
    auto valid = definition.validate();
    if (!valid) return valid;
    const std::string key = definition.id.format();
    if (definitions_.contains(key))
        return failure(DiagnosticCode::AlreadyExists, "Ability definition is already registered", key);
    definitions_.emplace(key, std::move(definition));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<AbilityGrantId> AbilityRuntime::grant(std::string ownerId, const LogicalId& definitionId) {
    if (ownerId.empty())
        return failure<AbilityGrantId>(DiagnosticCode::InvalidArgument, "Ability owner is empty", "ownerId");
    const auto definition = definitions_.find(definitionId.format());
    if (definition == definitions_.end())
        return failure<AbilityGrantId>(DiagnosticCode::NotFound, "Ability definition is not registered",
                                       definitionId.format());
    auto grantId = nextGrantId();
    if (!grantId) return grantId;
    AbilityInstanceId ownerInstance;
    if (definition->second.instancing == AbilityInstancingPolicy::PerOwner) {
        auto instance = nextInstanceId();
        if (!instance) return Result<AbilityGrantId>::failure(instance.status());
        ownerInstance = instance.value();
    }
    AbilityGrantState state{grantId.value(), std::move(ownerId), definitionId, Duration::zero()};
    grants_.emplace(grantId.value(), GrantRecord{std::move(state), ownerInstance});
    return Result<AbilityGrantId>::success(grantId.value(), Status::success(StatusCode::Applied));
}

Result<void> AbilityRuntime::revoke(AbilityGrantId grantId) {
    const auto found = grants_.find(grantId);
    if (found == grants_.end()) return failure(DiagnosticCode::NotFound, "Ability grant was not found", "grantId");
    if (grantHasActiveActivation(grantId))
        return failure(DiagnosticCode::Conflict, "Active ability grant cannot be revoked", "grantId");
    grants_.erase(found);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<AbilityGrantState> AbilityRuntime::findGrant(AbilityGrantId grantId) const {
    const auto found = grants_.find(grantId);
    if (found == grants_.end())
        return failure<AbilityGrantState>(DiagnosticCode::NotFound, "Ability grant was not found", "grantId");
    return Result<AbilityGrantState>::success(found->second.state);
}

Result<AbilityActivation> AbilityRuntime::activate(AbilityGrantId grantId, ActionRequest request, SimulationTick tick) {
    auto grant = grants_.find(grantId);
    if (grant == grants_.end())
        return failure<AbilityActivation>(DiagnosticCode::NotFound, "Ability grant was not found", "grantId");
    const auto definition = definitions_.find(grant->second.state.definitionId.format());
    if (definition == definitions_.end())
        return failure<AbilityActivation>(DiagnosticCode::NotFound, "Granted ability definition was removed",
                                          "definitionId");
    if (grant->second.state.cooldownRemaining > Duration::zero())
        return failure<AbilityActivation>(DiagnosticCode::PreconditionViolation, "Ability is on cooldown", "cooldown");
    if (request.actionId != definition->second.action.id)
        return failure<AbilityActivation>(DiagnosticCode::InvalidArgument, "Ability request action id does not match",
                                          "actionId");
    if (definition->second.instancing != AbilityInstancingPolicy::PerExecution && grantHasActiveActivation(grantId))
        return failure<AbilityActivation>(DiagnosticCode::Conflict, "Ability instance is already active", "grantId");

    std::vector<AbilityActivationId> replaceable;
    if (definition->second.activationGroup != AbilityActivationGroup::Independent) {
        for (const auto& [id, active] : activations_) {
            if (active.ownerId != grant->second.state.ownerId || active.group == AbilityActivationGroup::Independent)
                continue;
            const auto* execution = runtime_.find(active.executionId);
            if (!execution || terminal(execution->phase())) continue;
            if (active.group == AbilityActivationGroup::ExclusiveBlocking)
                return failure<AbilityActivation>(DiagnosticCode::Conflict, "An exclusive-blocking ability is active",
                                                  "activationGroup");
            replaceable.push_back(id);
        }
    }

    auto execution = runtime_.submit(definition->second.action, std::move(request));
    if (!execution) return Result<AbilityActivation>::failure(execution.status());
    for (const auto replacedId : replaceable) {
        auto cancelled = runtime_.cancel(activations_.at(replacedId).executionId, tick);
        if (!cancelled) {
            auto rollback = runtime_.cancel(execution.value(), tick);
            if (!rollback) return Result<AbilityActivation>::failure(rollback.status());
            return failure<AbilityActivation>(DiagnosticCode::Conflict,
                                              "Could not replace the active exclusive ability", "activationGroup");
        }
        activations_.erase(replacedId);
    }

    auto activationId = nextActivationId();
    if (!activationId) {
        auto rollback = runtime_.cancel(execution.value(), tick);
        if (!rollback) return Result<AbilityActivation>::failure(rollback.status());
        return Result<AbilityActivation>::failure(activationId.status());
    }
    AbilityInstanceId instanceId;
    if (definition->second.instancing == AbilityInstancingPolicy::PerOwner) {
        instanceId = grant->second.ownerInstanceId;
    } else if (definition->second.instancing == AbilityInstancingPolicy::PerExecution) {
        auto instance = nextInstanceId();
        if (!instance) {
            auto rollback = runtime_.cancel(execution.value(), tick);
            if (!rollback) return Result<AbilityActivation>::failure(rollback.status());
            return Result<AbilityActivation>::failure(instance.status());
        }
        instanceId = instance.value();
    }
    AbilityActivation activation{activationId.value(),
                                 grantId,
                                 instanceId,
                                 execution.value(),
                                 grant->second.state.ownerId,
                                 definition->second.activationGroup};
    activations_.emplace(activation.id, activation);
    grant->second.state.cooldownRemaining = definition->second.cooldown;
    return Result<AbilityActivation>::success(std::move(activation), Status::success(StatusCode::Applied));
}

std::vector<AbilityGrantId> AbilityRuntime::matchingGrants(std::string_view ownerId,
                                                           std::string_view gameplayEventTag) const {
    std::vector<AbilityGrantId> result;
    for (const auto& [id, grant] : grants_) {
        if (grant.state.ownerId != ownerId) continue;
        const auto definition = definitions_.find(grant.state.definitionId.format());
        if (definition == definitions_.end()) continue;
        const bool matches = std::any_of(
            definition->second.triggers.begin(), definition->second.triggers.end(), [&](const auto& trigger) {
                return tags::gameplayTagMatches(gameplayEventTag, trigger.gameplayTag, trigger.match);
            });
        if (matches) result.push_back(id);
    }
    return result;
}

Result<void> AbilityRuntime::advanceCooldowns(Duration delta) {
    if (delta < Duration::zero())
        return failure(DiagnosticCode::InvalidArgument, "Ability cooldown delta must be non-negative", "delta");
    for (auto& [id, grant] : grants_) {
        const auto remaining = grant.state.cooldownRemaining.nanoseconds();
        grant.state.cooldownRemaining =
            Duration::fromNanoseconds(delta.nanoseconds() >= remaining ? 0 : remaining - delta.nanoseconds());
    }
    return Result<void>::success(Status::success(delta.isZero() ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> AbilityRuntime::synchronize() {
    const std::size_t before = activations_.size();
    std::erase_if(activations_, [&](const auto& item) {
        const auto* execution = runtime_.find(item.second.executionId);
        return !execution || terminal(execution->phase());
    });
    const std::size_t removed = before - activations_.size();
    return Result<std::size_t>::success(removed,
                                        Status::success(removed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

std::vector<AbilityActivation> AbilityRuntime::activeActivations() const {
    std::vector<AbilityActivation> result;
    result.reserve(activations_.size());
    for (const auto& [id, activation] : activations_) result.push_back(activation);
    return result;
}

Result<AbilityGrantId>      AbilityRuntime::nextGrantId() { return takeNext(nextGrant_, "ability grant"); }
Result<AbilityActivationId> AbilityRuntime::nextActivationId() {
    return takeNext(nextActivation_, "ability activation");
}
Result<AbilityInstanceId> AbilityRuntime::nextInstanceId() { return takeNext(nextInstance_, "ability instance"); }

bool AbilityRuntime::grantHasActiveActivation(AbilityGrantId grantId) const {
    return std::any_of(activations_.begin(), activations_.end(), [&](const auto& item) {
        if (item.second.grantId != grantId) return false;
        const auto* execution = runtime_.find(item.second.executionId);
        return execution && !terminal(execution->phase());
    });
}

}  // namespace eve::action
