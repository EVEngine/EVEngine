#include "rts/RTSSystemMovementInternal.h"

#include <algorithm>
#include <cmath>

namespace eve::rts {
using systems_internal::distanceSquared;
using systems_internal::entityPosition;
using systems_internal::isMovementOrder;
using systems_internal::readCurrent;

Result<std::size_t> MotionSystem::step(const SimulationStep& step) {
    if (step.delta.nanoseconds() < 0)
        return Result<std::size_t>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS motion step delta must be non-negative", "step.delta"));
    const double deltaSeconds = step.delta.seconds();
    std::size_t  processed    = 0;
    auto         view = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Navigation, Unit::Orders, Unit::Combat,
                                  Unit::Containment, Unit::Supply, Unit::Morale, Unit::Tactics, Unit::Command, Unit::Effects>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, navigation, orders, combat, containment, supply, morale, tactics, command, effects] =
            *it;
        Unit* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        if (unit->crowd()->link.isBound() || containment->container.isBound()) continue;
        if (!std::isfinite(motion->x) || !std::isfinite(motion->y) || !std::isfinite(motion->speed) ||
            !std::isfinite(motion->arrivalRadius) || motion->speed < 0.0f || motion->arrivalRadius < 0.0f)
            return Result<std::size_t>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "RTS motion state must contain finite non-negative values", "unit.motion"));

        auto current = readCurrent(orders->values);
        if (!current) return Result<std::size_t>::failure(current.status());
        auto record = std::move(current).takeValue();
        if (!record) continue;
        if (!isMovementOrder(record->kind)) {
            motion->arrived = true;
            ++processed;
            continue;
        }
        if ((navigation->trafficWaiting && !navigation->trafficRecoveryTarget) || supply->convoyWaiting ||
            (morale->retreating && tactics->retreatCovering)) {
            motion->arrived = false;
            ++processed;
            continue;
        }
        if (record->kind == OrderKind::AttackMove && combat->engagementRange > 0.0f &&
            !navigation->trafficRecoveryTarget) {
            auto engaged = entityPosition(combat->target);
            if (engaged && distanceSquared(motion->x, motion->y, engaged->x, engaged->y) <=
                               combat->engagementRange * combat->engagementRange) {
                motion->arrived = false;
                ++processed;
                continue;
            }
        }

        WorldPosition target = record->target;
        if (record->kind == OrderKind::Attack || record->kind == OrderKind::Resupply ||
            record->kind == OrderKind::SupplyRelay) {
            auto liveTarget = entityPosition(record->targetEntity);
            if (liveTarget) target = *liveTarget;
            if (record->kind == OrderKind::SupplyRelay && supply->rendezvousActive) target = supply->rendezvousPoint;
        } else if (record->kind == OrderKind::Escort && tactics->guardSet) {
            target = {tactics->guardX, tactics->guardY};
        } else if (record->kind == OrderKind::Patrol && navigation->patrolInitialized &&
                   !navigation->patrolTowardTarget) {
            target = navigation->patrolOrigin;
        }
        if (navigation->plannedOrderId == record->id && !navigation->unreachable &&
            navigation->waypointIndex < navigation->waypoints.size())
            target = navigation->waypoints[navigation->waypointIndex];
        const bool movingSlot = record->kind == OrderKind::Move && navigation->formationTarget.has_value();
        if (movingSlot) target = *navigation->formationTarget;
        if (navigation->trafficRecoveryTarget) target = *navigation->trafficRecoveryTarget;
        const float dx       = target.x - motion->x;
        const float dy       = target.y - motion->y;
        const float distance = std::hypot(dx, dy);
        if (!std::isfinite(distance))
            return Result<std::size_t>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "RTS motion target distance is non-finite", "order.target"));
        float arrivalRadius = motion->arrivalRadius;
        if (record->kind == OrderKind::Attack)
            arrivalRadius = std::max(arrivalRadius, combat->engagementRange);
        else if (record->kind == OrderKind::Resupply || record->kind == OrderKind::SupplyRelay)
            arrivalRadius = std::max(arrivalRadius, supply->range * 0.8f);
        if (distance <= arrivalRadius) {
            if (record->kind != OrderKind::Attack) {
                motion->x = target.x;
                motion->y = target.y;
            }
            motion->arrived = true;
        } else if (deltaSeconds > 0.0 && motion->speed > 0.0f) {
            const float moraleFactor = morale->active ? std::clamp(morale->suppressedSpeedFactor, 0.0f, 1.0f) : 1.0f;
            const float commandFactor =
                command->requiresCommand && !command->inCommand ? command->outOfCommandSpeedFactor : 1.0f;
            const float  effectFactor = static_cast<float>(effects->values.multiplier("speedMultiplier"));
            const float  speedFactor  = moraleFactor * commandFactor * effectFactor * navigation->formationSpeedFactor;
            const double travel       = static_cast<double>(motion->speed * speedFactor) * deltaSeconds;
            if (!std::isfinite(travel))
                return Result<std::size_t>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "RTS motion travel distance is non-finite", "unit.motion.speed"));
            const float amount = static_cast<float>(std::min<double>(travel, distance));
            motion->x += dx / distance * amount;
            motion->y += dy / distance * amount;
            motion->arrived = static_cast<double>(amount) >= distance - arrivalRadius;
            if (motion->arrived) {
                if (record->kind != OrderKind::Attack) {
                    motion->x = target.x;
                    motion->y = target.y;
                }
            }
        } else {
            motion->arrived = false;
        }
        if (movingSlot)
            motion->arrived = distanceSquared(motion->x, motion->y, record->target.x, record->target.y) <=
                              motion->arrivalRadius * motion->arrivalRadius;
        if (navigation->trafficRecoveryTarget) motion->arrived = false;
        ++processed;
    }
    return Result<std::size_t>::success(processed, Status::success(StatusCode::Applied));
}

}  // namespace eve::rts
