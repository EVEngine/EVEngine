#include <algorithm>
#include <functional>
#include "rts/RTSCrowdFlowNavigationInternal.h"

namespace eve::rts {
namespace {
bool handleLess(const ecs::EntityHandle& left, const ecs::EntityHandle& right) {
    if (left.table != right.table) return std::less<ecs::Table*>{}(left.table, right.table);
    if (left.type != right.type) return left.type < right.type;
    if (left.id != right.id) return left.id < right.id;
    return left.generation < right.generation;
}
template <class T>
Result<T> groupFailure(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "crowdFlowNavigation.group"));
}
}  // namespace

Result<void> CrowdFlowNavigation::replaceGroups(crowd::Crowd& crowd, std::vector<GroupInput> groups) try {
    if (groups.size() > 64)
        return groupFailure<void>(DiagnosticCode::InvalidArgument, "Expected at most 64 shared groups");
    const auto                source = crowd.getFieldStatus();
    std::vector<GroupBinding> staged;
    for (const auto& group : groups) {
        if (group.members.empty() || group.members.size() > 1024)
            return groupFailure<void>(DiagnosticCode::InvalidArgument, "Expected 1..1024 group members");
        auto normalized = goalSet(crowd, group.targets);
        if (!normalized) return Result<void>::failure(normalized.status());
        std::shared_ptr<const std::vector<crowd::FlowGoal>> goals;
        const auto                                          priorGroup = std::lower_bound(
            groups_.begin(), groups_.end(), group.members.front().unit,
            [](const auto& candidate, const auto& handle) { return handleLess(candidate.member.unit, handle); });
        if (priorGroup != groups_.end() && !handleLess(group.members.front().unit, priorGroup->member.unit) &&
            priorGroup->owner == source.owner && priorGroup->input == source.inputRevision &&
            *priorGroup->goals == normalized.value())
            goals = priorGroup->goals;
        else
            goals = std::make_shared<const std::vector<crowd::FlowGoal>>(std::move(normalized).takeValue());
        for (const auto& member : group.members) {
            if (member.unit.table == nullptr || member.unit.generation == 0 || member.orderId.empty())
                return groupFailure<void>(DiagnosticCode::InvalidArgument, "Invalid group member binding");
            GroupBinding binding{member, goals, false, source.owner, source.inputRevision};
            const auto   previous = std::lower_bound(
                groups_.begin(), groups_.end(), member.unit,
                [](const auto& candidate, const auto& handle) { return handleLess(candidate.member.unit, handle); });
            if (previous != groups_.end() && !handleLess(member.unit, previous->member.unit) &&
                previous->member.orderId == member.orderId && previous->owner == source.owner &&
                previous->input == source.inputRevision && previous->goals == goals)
                binding.reform = previous->reform;
            staged.push_back(std::move(binding));
        }
    }
    std::sort(staged.begin(), staged.end(),
              [](const auto& left, const auto& right) { return handleLess(left.member.unit, right.member.unit); });
    if (std::adjacent_find(staged.begin(), staged.end(), [](const auto& left, const auto& right) {
            return !handleLess(left.member.unit, right.member.unit);
        }) != staged.end())
        return groupFailure<void>(DiagnosticCode::Conflict, "Unit belongs to multiple shared groups");
    groups_.swap(staged);
    return Result<void>::success();
} catch (const std::exception& error) {
    return groupFailure<void>(DiagnosticCode::Failed, error.what());
}

Result<CrowdFlowNavigation::Route> CrowdFlowNavigation::requestRoute(crowd::Crowd& crowd, ecs::EntityHandle unit,
                                                                     std::string_view orderId, WorldPosition position,
                                                                     WorldPosition target, SimulationTick tick) {
    auto exact = goalCell(crowd, target);
    if (!exact) return Result<Route>::failure(exact.status());
    auto cell = goalCell(crowd, position);
    if (!cell) return Result<Route>::failure(cell.status());
    const auto source = crowd.getFieldStatus();
    Route      route;
    route.exactGoal_ = exact.value();
    route.target_    = target;
    route.owner_     = source.owner;
    route.input_     = source.inputRevision;
    auto member = std::lower_bound(groups_.begin(), groups_.end(), unit, [](const auto& binding, const auto& handle) {
        return handleLess(binding.member.unit, handle);
    });
    bool reform = false;
    const bool matched = member != groups_.end() && !handleLess(unit, member->member.unit) &&
                         member->member.orderId == orderId && member->owner == source.owner &&
                         member->input == source.inputRevision &&
                         std::binary_search(member->goals->begin(), member->goals->end(), exact.value());
    if (matched) {
        // Once a member enters any slot cell, it follows its own terminal route until membership changes.
        // Keeping this derived phase across group refresh prevents oscillation back to the nearest wrong slot.
        reform = member->reform || std::binary_search(member->goals->begin(), member->goals->end(), cell.value());
        if (!reform) route.sharedGoals_ = member->goals;
    }
    const auto goals     = route.sharedGoals_ ? std::span<const crowd::FlowGoal>(*route.sharedGoals_)
                                              : std::span<const crowd::FlowGoal>(&route.exactGoal_, 1);
    auto       requested = requestGoals(goals, tick);
    if (!requested) return Result<Route>::failure(requested.status());
    if (matched) member->reform = reform;
    return Result<Route>::success(std::move(route));
}

Result<CrowdFlowNavigation::DriveState> CrowdFlowNavigation::steerRoute(crowd::Crowd& crowd, WorldPosition position,
                                                                        const Route& route, float radius,
                                                                        crowd::MovementBinding movement,
                                                                        crowd::AgentUpdate&    update) {
    const auto source = crowd.getFieldStatus();
    if (source.owner != route.owner_ || source.inputRevision != route.input_)
        return groupFailure<DriveState>(DiagnosticCode::StaleHandle, "Route belongs to a stale navigation epoch");
    const auto goals = route.sharedGoals_ ? std::span<const crowd::FlowGoal>(*route.sharedGoals_)
                                          : std::span<const crowd::FlowGoal>(&route.exactGoal_, 1);
    return steerGoals(crowd, position, goals, route.target_, radius, movement, update);
}
}  // namespace eve::rts
