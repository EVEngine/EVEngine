#include "crowd/Crowd.h"
#include "rts/RTSSystemMovementInternal.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace eve::rts {
using systems_internal::distanceSquared;
using systems_internal::entityPosition;
using systems_internal::isMovementOrder;
using systems_internal::readCurrent;

Result<std::size_t> CrowdMotionSystem::step(const SimulationStep& step, crowd::Crowd& crowd) {
    if (step.delta.nanoseconds() < 0)
        return Result<std::size_t>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS crowd step delta must be non-negative", "step.delta"));
    struct LinkedUnit {
        Unit*             unit;
        Unit::Motion*     motion;
        Unit::Navigation* navigation;
        Unit::Crowd*      settings;
        Unit::Combat*     combat;
        Unit::Supply*     supply;
        Unit::Morale*     morale;
        Unit::Tactics*    tactics;
        Unit::Command*    command;
        Unit::Effects*    effects;
        OrderComponent*   orders;
    };
    std::vector<LinkedUnit>  linked;
    std::vector<std::string> keys;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Navigation, Unit::Orders, Unit::Crowd, Unit::Combat,
                          Unit::Containment, Unit::Supply, Unit::Morale, Unit::Tactics, Unit::Command, Unit::Effects>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, navigation, orders, settings, combat, containment, supply, morale, tactics, command,
              effects] = *it;
        auto* unit     = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        if (!settings->link.isBound()) continue;
        if (containment->container.isBound()) {
            // Contained units have no world-space footprint. Recreate the
            // projection from their authoritative motion when they disembark.
            crowd.removeNamedAgent(settings->link.key());
            continue;
        }
        const std::string& key = settings->link.key();
        if (std::find(keys.begin(), keys.end(), key) != keys.end())
            return Result<std::size_t>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "RTS crowd agent keys must be unique", "unit.crowd.link"));
        keys.push_back(key);
        linked.push_back(
            {unit, motion, navigation, settings, combat, supply, morale, tactics, command, effects, &orders->values});
    }
    if (linked.empty()) return Result<std::size_t>::success(0, Status::success(StatusCode::NoOp));

    const auto isWaiting = [](const LinkedUnit& entry) {
        return (entry.navigation->trafficWaiting && !entry.navigation->trafficRecoveryTarget) ||
               entry.supply->convoyWaiting || (entry.morale->retreating && entry.tactics->retreatCovering);
    };
    for (const auto& entry : linked) {
        const std::string& key   = entry.settings->link.key();
        int                agent = crowd.getNamedAgentIndex(key);
        if (agent < 0)
            agent = crowd.addNamedAgent(key, entry.motion->x, entry.motion->y, entry.settings->heading,
                                        entry.settings->radius);
        if (agent < 0)
            return Result<std::size_t>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "canonical Crowd rejected an RTS agent", "unit.crowd.link"));
        crowd.setAgentPosition(agent, entry.motion->x, entry.motion->y);
        crowd.setAgentRadius(agent, entry.settings->radius);
        auto priority = crowd.setAgentAvoidancePriority(agent, entry.navigation->movementPriority);
        if (!priority) return Result<std::size_t>::failure(priority.status());
        const float speedFactor =
            entry.morale->active ? std::clamp(entry.morale->suppressedSpeedFactor, 0.0f, 1.0f) : 1.0f;
        const float commandFactor =
            entry.command->requiresCommand && !entry.command->inCommand ? entry.command->outOfCommandSpeedFactor : 1.0f;
        const float effectFactor = static_cast<float>(entry.effects->values.multiplier("speedMultiplier"));
        crowd.setAgentSpeed(agent, entry.motion->speed * speedFactor * commandFactor * effectFactor *
                                       entry.navigation->formationSpeedFactor);
        auto current = readCurrent(*entry.orders);
        if (!current) return Result<std::size_t>::failure(current.status());
        auto record      = std::move(current).takeValue();
        auto interaction = crowd.getAgentInteraction(agent);
        if (!interaction) return Result<std::size_t>::failure(interaction.status());
        auto policy         = interaction.value();
        policy.holdPosition = record && record->kind == OrderKind::HoldPosition;
        auto configured     = crowd.setAgentInteraction(agent, policy);
        if (!configured) return Result<std::size_t>::failure(configured.status());
        bool attackMoveEngaged = false;
        if (record && record->kind == OrderKind::AttackMove && entry.combat->engagementRange > 0.0f &&
            !entry.navigation->trafficRecoveryTarget) {
            auto engaged      = entityPosition(entry.combat->target);
            attackMoveEngaged = engaged && distanceSquared(entry.motion->x, entry.motion->y, engaged->x, engaged->y) <=
                                               entry.combat->engagementRange * entry.combat->engagementRange;
        }
        if (record && isMovementOrder(record->kind) && !isWaiting(entry) && !attackMoveEngaged) {
            WorldPosition target = record->target;
            if (record->kind == OrderKind::Attack || record->kind == OrderKind::Resupply ||
                record->kind == OrderKind::SupplyRelay) {
                auto liveTarget = entityPosition(record->targetEntity);
                if (liveTarget) target = *liveTarget;
            } else if (record->kind == OrderKind::Escort && entry.tactics->guardSet) {
                target = {entry.tactics->guardX, entry.tactics->guardY};
            } else if (record->kind == OrderKind::Patrol && entry.navigation->patrolInitialized &&
                       !entry.navigation->patrolTowardTarget) {
                target = entry.navigation->patrolOrigin;
            }
            if (entry.navigation->plannedOrderId == record->id && !entry.navigation->unreachable &&
                entry.navigation->waypointIndex < entry.navigation->waypoints.size())
                target = entry.navigation->waypoints[entry.navigation->waypointIndex];
            if (record->kind == OrderKind::Move && entry.navigation->formationTarget)
                target = *entry.navigation->formationTarget;
            if (entry.navigation->trafficRecoveryTarget) target = *entry.navigation->trafficRecoveryTarget;
            crowd.setAgentTarget(agent, target.x, target.y);
            crowd.setAgentAction(agent, "seek");
        } else {
            crowd.clearAgentTarget(agent);
            crowd.setAgentAction(agent, "idle");
        }
    }
    auto advanced = crowd.advance(static_cast<float>(step.delta.seconds()));
    if (!advanced) return Result<std::size_t>::failure(advanced.status());
    for (const auto& entry : linked) {
        const int  agent = crowd.getNamedAgentIndex(entry.settings->link.key());
        const auto state = crowd.getAgentState(agent);
        if (state.action < 0)
            return Result<std::size_t>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "canonical Crowd lost an RTS agent", "unit.crowd.link"));
        entry.motion->x         = state.x;
        entry.motion->y         = state.y;
        entry.settings->heading = state.heading;
        auto current            = readCurrent(*entry.orders);
        if (!current) return Result<std::size_t>::failure(current.status());
        auto record = std::move(current).takeValue();
        if (!record || !isMovementOrder(record->kind)) {
            entry.motion->arrived = true;
            continue;
        }
        if (isWaiting(entry) || entry.navigation->trafficRecoveryTarget) {
            entry.motion->arrived = false;
            continue;
        }
        WorldPosition target = record->target;
        if (record->kind == OrderKind::Attack || record->kind == OrderKind::Resupply ||
            record->kind == OrderKind::SupplyRelay) {
            auto liveTarget = entityPosition(record->targetEntity);
            if (liveTarget) target = *liveTarget;
        } else if (record->kind == OrderKind::Escort && entry.tactics->guardSet) {
            target = {entry.tactics->guardX, entry.tactics->guardY};
        }
        const float remaining     = std::sqrt(distanceSquared(state.x, state.y, target.x, target.y));
        float       arrivalRadius = entry.motion->arrivalRadius;
        if (record->kind == OrderKind::Attack)
            arrivalRadius = std::max(arrivalRadius, entry.combat->engagementRange);
        else if (record->kind == OrderKind::Resupply || record->kind == OrderKind::SupplyRelay)
            arrivalRadius = std::max(arrivalRadius, entry.supply->range * 0.8f);
        entry.motion->arrived = remaining <= arrivalRadius;
        if (entry.motion->arrived) {
            // Arrival is a gameplay tolerance, not permission to undo collision resolution.
            crowd.setAgentAction(agent, "idle");
        }
    }
    return Result<std::size_t>::success(linked.size(), Status::success(StatusCode::Applied));
}

}  // namespace eve::rts
