#include "common/Profile.h"
#include "crowd/Crowd.h"
#include "rts/RTSCrowdFlowNavigationInternal.h"
#include "rts/RTSSystemMovementInternal.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <optional>
#include <vector>

namespace eve::rts {
using systems_internal::distanceSquared;
using systems_internal::entityPosition;
using systems_internal::isMovementOrder;
using systems_internal::readCurrent;

Result<std::size_t> CrowdMotionSystem::step(const SimulationStep& step, crowd::Crowd& crowd) {
    return stepInternal(step, crowd, nullptr);
}
Result<std::size_t> CrowdMotionSystem::stepSharedFlow(const SimulationStep& step, crowd::Crowd& crowd,
                                                      CrowdFlowNavigation& navigation) {
    return stepInternal(step, crowd, &navigation);
}
Result<std::size_t> CrowdMotionSystem::stepInternal(const SimulationStep& step, crowd::Crowd& crowd,
                                                    CrowdFlowNavigation* flowNavigation) {
    EV_PROFILE_MODULE("rts", "CrowdMotionSystem::step");
    const crowd::CrowdStep crowdStep{step.tick, step.delta, {}};
    auto                   admitted = crowd.checkAdvanceClock(crowdStep);
    if (!admitted) return Result<std::size_t>::failure(admitted.status());
    if (step.delta.nanoseconds() < 0)
        return Result<std::size_t>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS crowd step delta must be non-negative", "step.delta"));
    auto change = CrowdFlowNavigation::InputChange::Unchanged;
    if (flowNavigation) {
        auto begun = flowNavigation->begin(crowd, step);
        if (!begun) return Result<std::size_t>::failure(begun.status());
        change = begun.value();
    }
    struct LinkedUnit {
        Unit*                      unit;
        Unit::Motion*              motion;
        Unit::Navigation*          navigation;
        Unit::Crowd*               settings;
        Unit::Combat*              combat;
        Unit::Supply*              supply;
        Unit::Morale*              morale;
        Unit::Tactics*             tactics;
        Unit::Command*             command;
        Unit::Effects*             effects;
        OrderComponent*            orders;
        bool                       sharedFlowReady = true;
        std::optional<OrderRecord> record{};
    };
    std::vector<LinkedUnit>   linked;
    std::vector<Unit::Crowd*> detached;
    bool                      needsBinding = false;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Navigation, Unit::Orders, Unit::Crowd, Unit::Combat,
                          Unit::Containment, Unit::Supply, Unit::Morale, Unit::Tactics, Unit::Command, Unit::Effects>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, navigation, orders, settings, combat, containment, supply, morale, tactics, command,
              effects] = *it;
        auto* unit     = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        if (!settings->link.isBound() || containment->container.isBound()) {
            detached.push_back(settings);
            continue;
        }
        needsBinding = needsBinding || settings->runtimeBindingKey != settings->link.key() ||
                       !crowd.isAgentAlive({settings->runtimeAgent, settings->runtimeOwner});
        linked.push_back(
            {unit, motion, navigation, settings, combat, supply, morale, tactics, command, effects, &orders->values});
    }
    // Logical-name admission happens only when a binding changes. Ordinary ticks use numeric references.
    if (needsBinding) {
        std::vector<const std::string*> keys;
        keys.reserve(linked.size());
        for (const auto& entry : linked) keys.push_back(&entry.settings->link.key());
        std::sort(keys.begin(), keys.end(), [](const auto* a, const auto* b) { return *a < *b; });
        if (std::adjacent_find(keys.begin(), keys.end(), [](const auto* a, const auto* b) { return *a == *b; }) !=
            keys.end())
            return Result<std::size_t>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "RTS crowd agent keys must be unique", "unit.crowd.link"));
    }
    crowd::AgentRemovalBatch removals;
    for (const auto* settings : detached) {
        const crowd::AgentRef ref{settings->runtimeAgent, settings->runtimeOwner};
        if (crowd.isAgentAlive(ref)) removals.agents.push_back(ref);
    }
    for (const auto& entry : linked) {
        const crowd::AgentRef ref{entry.settings->runtimeAgent, entry.settings->runtimeOwner};
        if (entry.settings->runtimeBindingKey != entry.settings->link.key() && crowd.isAgentAlive(ref))
            removals.agents.push_back(ref);
    }
    if (!removals.agents.empty()) {
        auto removed = crowd.removeAgents(removals);
        if (!removed) return Result<std::size_t>::failure(removed.status());
    }
    for (auto* settings : detached) {
        settings->runtimeAgent = {};
        settings->runtimeOwner = 0;
        settings->runtimeBindingKey.clear();
    }
    if (linked.empty()) {
        auto advanced = crowd.advance(crowdStep);
        if (!advanced) return Result<std::size_t>::failure(advanced.status());
        return Result<std::size_t>::success(0, Status::success(StatusCode::NoOp));
    }

    const auto isWaiting = [](const LinkedUnit& entry) {
        return (entry.navigation->trafficWaiting && !entry.navigation->trafficRecoveryTarget) ||
               entry.supply->convoyWaiting || (entry.morale->retreating && entry.tactics->retreatCovering);
    };
    struct FlowAssignment {
        size_t                     index;
        CrowdFlowNavigation::Route route;
    };
    std::vector<FlowAssignment>  flowAssignments;
    crowd::AgentUpdateBatch      updates;
    std::vector<crowd::AgentRef> references;
    updates.agents.reserve(linked.size());
    references.reserve(linked.size());
    for (auto& entry : linked) {
        const std::string& key = entry.settings->link.key();
        crowd::AgentRef    reference{entry.settings->runtimeAgent, entry.settings->runtimeOwner};
        if (entry.settings->runtimeBindingKey != key || !crowd.isAgentAlive(reference)) {
            auto resolved = crowd.resolveNamedAgent(key);
            if (!resolved) {
                // Legacy binding admits existing authoritative ECS geometry, including preexisting overlap.
                // Production creation goes through the atomic placement batch instead.
                const int agent = crowd.addNamedAgent(key, entry.motion->x, entry.motion->y, entry.settings->heading,
                                                      entry.settings->radius);
                if (agent < 0)
                    return Result<std::size_t>::failure(Diagnostic::error(
                        DiagnosticCode::Failed, "canonical Crowd rejected an RTS binding", "unit.crowd.link"));
                resolved = crowd.agentRef(agent);
            }
            if (!resolved) return Result<std::size_t>::failure(resolved.status());
            reference                         = resolved.value();
            entry.settings->runtimeAgent      = reference.handle;
            entry.settings->runtimeOwner      = reference.owner;
            entry.settings->runtimeBindingKey = key;
        }
        crowd::AgentUpdate update;
        update.reference = reference;
        if (flowNavigation) update.flow = crowd::FlowLease{};
        update.radius            = entry.settings->radius;
        update.avoidancePriority = entry.navigation->movementPriority;
        const float speedFactor =
            entry.morale->active ? std::clamp(entry.morale->suppressedSpeedFactor, 0.0f, 1.0f) : 1.0f;
        const float commandFactor =
            entry.command->requiresCommand && !entry.command->inCommand ? entry.command->outOfCommandSpeedFactor : 1.0f;
        const float effectFactor = static_cast<float>(entry.effects->values.multiplier("speedMultiplier"));
        update.maxSpeed =
            entry.motion->speed * speedFactor * commandFactor * effectFactor * entry.navigation->formationSpeedFactor;
        auto current = readCurrent(*entry.orders);
        if (!current) return Result<std::size_t>::failure(current.status());
        auto record            = std::move(current).takeValue();
        update.holdPosition    = record && record->kind == OrderKind::HoldPosition;
        bool attackMoveEngaged = false;
        if (record && record->kind == OrderKind::AttackMove && entry.combat->engagementRange > 0.0f &&
            !entry.navigation->trafficRecoveryTarget && !entry.navigation->combatSlotTarget) {
            auto engaged      = entityPosition(entry.combat->target);
            attackMoveEngaged = engaged && distanceSquared(entry.motion->x, entry.motion->y, engaged->x, engaged->y) <=
                                               entry.combat->engagementRange * entry.combat->engagementRange;
        }
        if (record && isMovementOrder(record->kind) && !isWaiting(entry) && !attackMoveEngaged) {
            WorldPosition target        = systems_internal::movementGoal(*entry.unit, *record);
            const auto    requestedGoal = target;
            if (entry.navigation->plannedOrderId == record->id && !entry.navigation->unreachable &&
                entry.navigation->waypointIndex < entry.navigation->waypoints.size())
                target = entry.navigation->waypoints[entry.navigation->waypointIndex];
            if (record->kind == OrderKind::Move && entry.navigation->formationTarget)
                target = *entry.navigation->formationTarget;
            if (entry.navigation->combatSlotTarget) target = *entry.navigation->combatSlotTarget;
            if (entry.navigation->trafficRecoveryTarget) target = *entry.navigation->trafficRecoveryTarget;
            if (flowNavigation && systems_internal::isSharedFlowOrder(*entry.unit, *record)) {
                auto requested =
                    flowNavigation->requestRoute(crowd, entry.unit->identity()->self, record->id,
                                                 {entry.motion->x, entry.motion->y}, requestedGoal, step.tick);
                if (!requested) return Result<std::size_t>::failure(requested.status());
                flowAssignments.push_back({updates.agents.size(), std::move(requested).takeValue()});
            }
            update.intent = {crowd::AgentAction::Seek, true, target.x, target.y};
        } else {
            update.intent = {crowd::AgentAction::Idle, false, 0.f, 0.f};
        }
        entry.record = std::move(record);
        updates.agents.push_back(update);
        references.push_back(reference);
    }
    if (flowNavigation) {
        if (change == CrowdFlowNavigation::InputChange::Changed) {
            crowd::AgentUpdateBatch releases;
            releases.agents.reserve(references.size());
            for (const auto ref : references) {
                crowd::AgentUpdate release;
                release.reference = ref;
                release.flow      = crowd::FlowLease{};
                releases.agents.push_back(std::move(release));
            }
            auto cleared = crowd.updateAgents(releases);
            if (!cleared) return Result<std::size_t>::failure(cleared.status());
        }
        auto pumped = flowNavigation->pump(crowd, step);
        if (!pumped) return Result<std::size_t>::failure(pumped.status());
        for (const auto& assignment : flowAssignments) {
            auto& entry  = linked[assignment.index];
            auto& update = updates.agents[assignment.index];
            auto  before = crowd.readAgent(references[assignment.index]);
            if (!before) return Result<std::size_t>::failure(before.status());
            auto steered = flowNavigation->steerRoute(crowd, {before.value().x, before.value().y}, assignment.route,
                                                      entry.settings->radius, before.value().movement, update);
            if (!steered) return Result<std::size_t>::failure(steered.status());
            entry.sharedFlowReady         = steered.value() == CrowdFlowNavigation::DriveState::Ready;
            entry.navigation->unreachable = steered.value() == CrowdFlowNavigation::DriveState::Unreachable ||
                                            steered.value() == CrowdFlowNavigation::DriveState::Unavailable;
            if (!entry.navigation->unreachable) entry.navigation->unreachableReported = false;
            if (entry.navigation->unreachable && !entry.navigation->unreachableReported) {
                if (entry.record) flowNavigation->queueNotice(entry.unit->identity()->self, *entry.record);
            }
        }
    }
    auto updated = crowd.updateAgents(updates);
    if (!updated) return Result<std::size_t>::failure(updated.status());
    crowd::AgentFrame         frame;
    std::vector<std::uint8_t> arrivalSelection;
    try {
        frame.agents.reserve(references.size());
        arrivalSelection.assign(references.size(), 0);
    } catch (const std::bad_alloc&) {
        return Result<std::size_t>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Cannot prepare RTS Crowd output frame", "rts.crowd.frame"));
    }
    const bool                                   zeroDelta = step.delta.nanoseconds() == 0;
    std::optional<crowd::Crowd::AgentUpdatePlan> arrivalPlan;
    if (!zeroDelta) {
        updates.agents.clear();
        for (const auto reference : references) {
            crowd::AgentUpdate arrived;
            arrived.reference = reference;
            arrived.intent    = {crowd::AgentAction::Idle, false, 0.f, 0.f};
            updates.agents.push_back(arrived);
        }
        auto prepared = crowd.prepareAgentUpdates(updates);
        if (!prepared) return Result<std::size_t>::failure(prepared.status());
        arrivalPlan.emplace(std::move(prepared).takeValue());
    }
    auto advanced = crowd.advance(crowdStep);
    if (!advanced) return Result<std::size_t>::failure(advanced.status());
    auto copied = crowd.copyAgentStates(references, frame);
    if (!copied) return Result<std::size_t>::failure(copied.status());
    updates.agents.clear();
    for (std::size_t index = 0; index < linked.size(); ++index) {
        const auto& entry       = linked[index];
        const auto& state       = frame.agents[index].state;
        entry.motion->x         = state.x;
        entry.motion->y         = state.y;
        entry.settings->heading = state.heading;
    }
    // Publish every pose before evaluating goals that observe another unit's new pose.
    for (std::size_t index = 0; index < linked.size(); ++index) {
        const auto& entry  = linked[index];
        const auto& state  = frame.agents[index].state;
        const auto& record = entry.record;
        if (!record || !isMovementOrder(record->kind)) {
            entry.motion->arrived = true;
            continue;
        }
        if (!entry.sharedFlowReady || isWaiting(entry) || entry.navigation->trafficRecoveryTarget ||
            entry.navigation->combatSlotTarget) {
            entry.motion->arrived = false;
            continue;
        }
        const auto  target        = systems_internal::movementGoal(*entry.unit, *record);
        const float remaining     = std::sqrt(distanceSquared(state.x, state.y, target.x, target.y));
        float       arrivalRadius = entry.motion->arrivalRadius;
        if (record->kind == OrderKind::Attack)
            arrivalRadius = std::max(arrivalRadius, entry.combat->engagementRange);
        else if (record->kind == OrderKind::Resupply || record->kind == OrderKind::SupplyRelay)
            arrivalRadius = std::max(arrivalRadius, entry.supply->range * 0.8f);
        entry.motion->arrived = remaining <= arrivalRadius;
        if (entry.motion->arrived) {
            // Arrival is a gameplay tolerance, not permission to undo collision resolution.
            arrivalSelection[index] = 1;
        }
    }
    if (zeroDelta) {
        // The no-time pass cannot move agents. The copied frame already validates every
        // reference, so only units that reached a goal need an idle-intent update.
        crowd::AgentUpdateBatch arrived;
        for (std::size_t index = 0; index < references.size(); ++index) {
            if (arrivalSelection[index] == 0) continue;
            crowd::AgentUpdate update;
            update.reference = references[index];
            update.intent    = {crowd::AgentAction::Idle, false, 0.f, 0.f};
            arrived.agents.push_back(update);
        }
        if (!arrived.agents.empty()) {
            auto arrivals = crowd.updateAgents(arrived);
            if (!arrivals) return Result<std::size_t>::failure(arrivals.status());
        }
    } else {
        auto arrivals = crowd.commitAgentUpdates(std::move(*arrivalPlan), arrivalSelection);
        if (!arrivals) return Result<std::size_t>::failure(arrivals.status());
    }
    return Result<std::size_t>::success(linked.size(), Status::success(StatusCode::Applied));
}

}  // namespace eve::rts
