#include "rts/RTS.h"
#include "rts/RTSSystemMovementInternal.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace eve::rts {
namespace {
Result<void> groupFailure(const char* message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "movementGroup"));
}

double remainingRoute(Unit& unit, const OrderRecord& order) {
    auto          motion     = unit.motion();
    auto          navigation = unit.navigation();
    WorldPosition previous{motion->x, motion->y};
    double        remaining = 0.0;
    const auto    add       = [&](WorldPosition next) {
        remaining += std::hypot(static_cast<double>(next.x) - previous.x, static_cast<double>(next.y) - previous.y);
        previous = next;
    };
    if (navigation->plannedOrderId == order.id && !navigation->unreachable) {
        for (std::size_t i = navigation->waypointIndex; i < navigation->waypoints.size(); ++i)
            add(navigation->waypoints[i]);
    }
    add(order.target);
    return remaining;
}
}  // namespace

Result<FanOutReceipt> RTS::submitMovementGroup(const MovementGroupBatch& batch) {
    if (batch.units.size() < 2 || !std::isfinite(batch.leadDistance) || batch.leadDistance <= 0.f)
        return Result<FanOutReceipt>::failure(
            groupFailure("Movement group requires two units and positive finite lead distance").status());
    MovementGroup group;
    group.state.leadDistance = batch.leadDistance;
    group.handles.reserve(batch.units.size());
    group.state.members.reserve(batch.units.size());
    for (auto subject : batch.units) {
        auto* unit = findUnit(subject);
        if (!unit || !unit->durability()->alive || unit->containment()->container.isBound())
            return Result<FanOutReceipt>::failure(
                groupFailure("Movement group member must be a live uncontained owned unit").status());
        group.handles.push_back(ecs::handle_of(unit));
        group.epochs.push_back(unit->orders()->values.membershipEpoch_);
        group.state.members.push_back({subject, {}});
    }
    movementGroups_.reserve(movementGroups_.size() + 1);
    CommandSpec command;
    command.kind   = OrderKind::Move;
    command.target = batch.target;
    auto admitted  = CommandFanOutSystem::fanOut(group.handles, command, batch.formation);
    if (!admitted) return admitted;
    for (std::size_t i = 0; i < group.state.members.size(); ++i)
        group.state.members[i].orderId = admitted.value().orderIds[i];
    movementGroups_.push_back(std::move(group));
    return admitted;
}

Result<std::size_t> RTS::stepMovementGroups() {
    // Derived factors are refreshed even when the last group dissolved. Native
    // and Crowd motion consume the same projection after navigation/convoy.
    {
        auto view = ecs::View<Unit, Unit::Navigation>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [navigation]                = *it;
            navigation->formationSpeedFactor = 1.f;
            navigation->formationTarget.reset();
        }
    }
    std::size_t processed = 0;
    for (auto& group : movementGroups_) {
        MovementGroup active;
        active.state.leadDistance = group.state.leadDistance;
        std::vector<double>        distances;
        std::vector<WorldPosition> targets;
        double                     offsetX = 0.0, offsetY = 0.0;
        bool                       openTravel = true;
        double                     furthest   = 0.0;
        for (std::size_t i = 0; i < group.handles.size(); ++i) {
            auto* unit = dynamic_cast<Unit*>(ecs::try_get(group.handles[i]));
            if (!unit || !unit->durability()->alive || unit->containment()->container.isBound() ||
                unit->orders()->values.membershipEpoch_ != group.epochs[i])
                continue;
            auto current = systems_internal::readCurrent(unit->orders()->values);
            if (!current) return Result<std::size_t>::failure(current.status());
            if (!current.value() || current.value()->id != group.state.members[i].orderId ||
                current.value()->kind != OrderKind::Move)
                continue;
            auto navigation = unit->navigation();
            if (pathfinder_ && navigation->plannedOrderId == current.value()->id && !navigation->unreachable &&
                navigation->waypointIndex + 1 < navigation->waypoints.size()) {
                const WorldPosition position{unit->motion()->x, unit->motion()->y};
                const auto          waypoint = navigation->waypoints[navigation->waypointIndex];
                const double        tolerance =
                    std::max(static_cast<double>(unit->motion()->arrivalRadius),
                             static_cast<double>(unit->crowd()->radius) + navigationGrid_.cellSize * 0.1);
                if (std::hypot(static_cast<double>(position.x) - waypoint.x,
                               static_cast<double>(position.y) - waypoint.y) <= tolerance &&
                    systems_internal::isFormationSegmentClear(*pathfinder_, navigationGrid_, position,
                                                              navigation->waypoints[navigation->waypointIndex + 1],
                                                              unit->crowd()->radius))
                    ++navigation->waypointIndex;
            }
            const double remaining = remainingRoute(*unit, *current.value());
            if (!std::isfinite(remaining))
                return Result<std::size_t>::failure(
                    groupFailure("Movement group route distance must be finite").status());
            // A traffic loser must not stop the winner from clearing its route.
            // Keep membership so ordinary pacing resumes after the wait ends.
            const bool yielding = unit->navigation()->trafficWaiting || unit->supply()->convoyWaiting ||
                                  (unit->morale()->retreating && unit->tactics()->retreatCovering);
            if (!yielding) furthest = std::max(furthest, remaining);
            openTravel = openTravel && !yielding;
            targets.push_back(current.value()->target);
            offsetX += static_cast<double>(unit->motion()->x) - current.value()->target.x;
            offsetY += static_cast<double>(unit->motion()->y) - current.value()->target.y;
            distances.push_back(remaining);
            active.handles.push_back(group.handles[i]);
            active.epochs.push_back(group.epochs[i]);
            active.state.members.push_back(group.state.members[i]);
        }
        group = std::move(active);
        if (group.handles.size() < 2) continue;
        offsetX /= static_cast<double>(group.handles.size());
        offsetY /= static_cast<double>(group.handles.size());
        const double offsetLength = std::hypot(offsetX, offsetY);
        const double residual = offsetLength > 0.0 ? std::max(0.0, 1.0 - group.state.leadDistance / offsetLength) : 0.0;
        std::vector<WorldPosition> movingTargets;
        movingTargets.reserve(targets.size());
        for (std::size_t i = 0; i < targets.size(); ++i) {
            const WorldPosition target{static_cast<float>(targets[i].x + offsetX * residual),
                                       static_cast<float>(targets[i].y + offsetY * residual)};
            if (!systems_internal::isFinitePosition(target))
                return Result<std::size_t>::failure(groupFailure("Moving formation target must be finite").status());
            movingTargets.push_back(target);
            if (pathfinder_ && openTravel) {
                auto*               unit = static_cast<Unit*>(ecs::try_get(group.handles[i]));
                const WorldPosition from{unit->motion()->x, unit->motion()->y};
                openTravel = systems_internal::isFormationSegmentClear(*pathfinder_, navigationGrid_, from, targets[i],
                                                                       unit->crowd()->radius) &&
                             systems_internal::isFormationSegmentClear(*pathfinder_, navigationGrid_, from, target,
                                                                       unit->crowd()->radius);
            }
        }
        for (std::size_t i = 0; i < group.handles.size(); ++i) {
            auto*        unit = static_cast<Unit*>(ecs::try_get(group.handles[i]));
            const double excess =
                pathfinder_ && !openTravel ? 0.0 : std::max(0.0, furthest - distances[i] - group.state.leadDistance);
            unit->navigation()->formationSpeedFactor =
                static_cast<float>(std::clamp(1.0 - excess / group.state.leadDistance, 0.0, 1.0));
            if (openTravel) {
                // Translate the assigned destination slots to a shared moving
                // anchor. The anchor is derived from current positions, so
                // restore and member removal cannot leave an orphan leader.
                unit->navigation()->formationTarget = movingTargets[i];
                // A radius-clear direct route supersedes intermediate grid
                // waypoints; retaining them would send the unit back afterward.
                if (pathfinder_) unit->navigation()->waypointIndex = unit->navigation()->waypoints.size();
            }
            ++processed;
        }
    }
    std::erase_if(movementGroups_, [](const auto& group) { return group.handles.size() < 2; });
    return Result<std::size_t>::success(processed);
}

Result<std::vector<RTSMovementGroupSnapshot>> RTS::captureMovementGroups() const {
    std::vector<RTSMovementGroupSnapshot> result;
    for (const auto& group : movementGroups_) {
        RTSMovementGroupSnapshot active;
        active.leadDistance = group.state.leadDistance;
        for (std::size_t i = 0; i < group.handles.size(); ++i) {
            auto* unit = dynamic_cast<Unit*>(ecs::try_get(group.handles[i]));
            if (!unit || !unit->durability()->alive || unit->containment()->container.isBound() ||
                unit->orders()->values.membershipEpoch_ != group.epochs[i])
                continue;
            auto current = systems_internal::readCurrent(unit->orders()->values);
            if (!current) return Result<std::vector<RTSMovementGroupSnapshot>>::failure(current.status());
            if (current.value() && current.value()->kind == OrderKind::Move &&
                current.value()->id == group.state.members[i].orderId)
                active.members.push_back(group.state.members[i]);
        }
        if (active.members.size() >= 2) {
            std::sort(active.members.begin(), active.members.end(), [](const auto& left, const auto& right) {
                return left.subject.format() < right.subject.format();
            });
            result.push_back(std::move(active));
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.members.front().subject.format() < right.members.front().subject.format();
    });
    return Result<std::vector<RTSMovementGroupSnapshot>>::success(std::move(result));
}

Result<std::vector<RTS::MovementGroup>> RTS::prepareMovementGroups(const RTSStateSnapshot& snapshot) const {
    using Output = Result<std::vector<MovementGroup>>;
    if (snapshot.version == 1 && !snapshot.movementGroups.empty())
        return Output::failure(groupFailure("Version 1 RTS snapshots cannot contain movement groups").status());
    std::vector<MovementGroup>     result;
    std::unordered_set<SubjectRef> claimed;
    for (const auto& state : snapshot.movementGroups) {
        if (state.members.size() < 2 || !std::isfinite(state.leadDistance) || state.leadDistance <= 0.f)
            return Output::failure(groupFailure("Invalid movement-group snapshot geometry").status());
        MovementGroup group;
        group.state = state;
        for (const auto& member : state.members) {
            auto*      unit  = findUnit(member.subject);
            const auto saved = std::find_if(snapshot.units.begin(), snapshot.units.end(),
                                            [&](const auto& candidate) { return candidate.subject == member.subject; });
            if (!unit || saved == snapshot.units.end() || !claimed.insert(member.subject).second ||
                !saved->durability.alive || saved->container.isValid())
                return Output::failure(groupFailure("Invalid or duplicate movement-group member").status());
            OrderComponent validator;
            auto           restored = validator.restoreState(saved->orders);
            if (!restored) return Output::failure(restored.status());
            auto current = validator.current();
            if (!current || current.value().id != member.orderId || current.value().kind != OrderKind::Move)
                return Output::failure(
                    groupFailure("Movement-group membership does not match its active order").status());
            group.handles.push_back(ecs::handle_of(unit));
            group.epochs.push_back(unit->orders()->values.membershipEpoch_);
        }
        result.push_back(std::move(group));
    }
    return Output::success(std::move(result));
}
}  // namespace eve::rts
