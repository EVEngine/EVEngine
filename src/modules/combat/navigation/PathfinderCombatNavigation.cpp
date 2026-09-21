#include "combat/navigation/PathfinderCombatNavigation.h"

#include "map/Path.h"
#include "map/Pathfinder.h"

#include <cmath>
#include <limits>
#include <utility>

namespace eve::combat::navigation {
namespace {

bool finite(CombatVector2 value) { return std::isfinite(value.x) && std::isfinite(value.z); }

double length(CombatVector2 value) { return std::hypot(value.x, value.z); }

CombatVector2 normalized(CombatVector2 value) {
    const double magnitude = length(value);
    return magnitude > 0.0 ? CombatVector2{value.x / magnitude, value.z / magnitude} : CombatVector2{};
}

Result<int> worldToCell(double coordinate, double origin, double cellSize, std::string path) {
    const double projected = std::floor((coordinate - origin) / cellSize);
    if (!std::isfinite(projected) || projected < static_cast<double>(std::numeric_limits<int>::min()) ||
        projected > static_cast<double>(std::numeric_limits<int>::max()))
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "navigation coordinate is outside grid range", std::move(path)));
    return Result<int>::success(static_cast<int>(projected));
}

}  // namespace

Result<void> PathfinderCombatNavigationConfig::validate() const {
    if (!finite(origin) || !std::isfinite(cellSize) || cellSize <= 0.0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "combat navigation grid projection is invalid", "config"));
    return Result<void>::success();
}

PathfinderCombatNavigationProvider::PathfinderCombatNavigationProvider(
    map::Pathfinder& pathfinder, PathfinderCombatNavigationConfig config) noexcept
    : pathfinder_(pathfinder), config_(config) {}

Result<std::unique_ptr<PathfinderCombatNavigationProvider>> PathfinderCombatNavigationProvider::create(
    map::Pathfinder& pathfinder, PathfinderCombatNavigationConfig config) {
    auto valid = config.validate();
    if (!valid)
        return Result<std::unique_ptr<PathfinderCombatNavigationProvider>>::failure(valid.status());
    return Result<std::unique_ptr<PathfinderCombatNavigationProvider>>::success(
        std::unique_ptr<PathfinderCombatNavigationProvider>(
            new PathfinderCombatNavigationProvider(pathfinder, config)));
}

Result<CombatNavigationSteering> PathfinderCombatNavigationProvider::steer(
    const CombatLocomotionState& state, const CombatNavigationGoal& goal, SimulationTick tick) {
    (void)tick;
    if (!finite(state.position) || !finite(goal.position) || !std::isfinite(goal.acceptanceRadius) ||
        goal.acceptanceRadius < 0.0)
        return Result<CombatNavigationSteering>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "combat navigation request is invalid", "navigation"));

    const CombatVector2 goalDelta{goal.position.x - state.position.x,
                                  goal.position.z - state.position.z};
    if (length(goalDelta) <= goal.acceptanceRadius)
        return Result<CombatNavigationSteering>::success(
            {CombatNavigationPhase::Arrived, {}, 0.0});

    auto startX = worldToCell(state.position.x, config_.origin.x, config_.cellSize, "state.position.x");
    if (!startX) return Result<CombatNavigationSteering>::failure(startX.status());
    auto startY = worldToCell(state.position.z, config_.origin.z, config_.cellSize, "state.position.z");
    if (!startY) return Result<CombatNavigationSteering>::failure(startY.status());
    auto goalX = worldToCell(goal.position.x, config_.origin.x, config_.cellSize, "goal.position.x");
    if (!goalX) return Result<CombatNavigationSteering>::failure(goalX.status());
    auto goalY = worldToCell(goal.position.z, config_.origin.z, config_.cellSize, "goal.position.z");
    if (!goalY) return Result<CombatNavigationSteering>::failure(goalY.status());

    std::unique_ptr<map::Path> path(pathfinder_.get().findPath(startX.value(), startY.value(),
                                                               goalX.value(), goalY.value()));
    if (!path || path->empty())
        return Result<CombatNavigationSteering>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "combat navigation goal is unreachable", state.subject.format()));

    CombatVector2 waypoint = goal.position;
    if (path->getLength() > 1) {
        waypoint.x = config_.origin.x + (static_cast<double>(path->getX(1)) + 0.5) * config_.cellSize;
        waypoint.z = config_.origin.z + (static_cast<double>(path->getY(1)) + 0.5) * config_.cellSize;
    }
    return Result<CombatNavigationSteering>::success(
        {CombatNavigationPhase::Moving,
         normalized({waypoint.x - state.position.x, waypoint.z - state.position.z}), 1.0});
}

}  // namespace eve::combat::navigation
