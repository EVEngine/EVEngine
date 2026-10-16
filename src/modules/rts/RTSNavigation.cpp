#include "common/Profile.h"
#include <algorithm>
#include <memory>
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "rts/RTSSystemMovementInternal.h"
#include "weapon/WeaponSystem.h"
namespace eve::rts::systems_internal {
float visibleHostileThreatAt(Faction& viewer, WorldPosition point) {
    const auto visible = [&](SubjectRef subject) {
        if (!viewer.intel()->enabled) return true;
        const auto found = std::find_if(viewer.intel()->contacts.begin(), viewer.intel()->contacts.end(),
                                        [&](const auto& contact) { return contact.subject == subject; });
        return found != viewer.intel()->contacts.end() && found->visible && found->detected;
    };
    const auto contribution = [&](const weapon::WeaponDefinition& definition, WorldPosition source) {
        if (definition.range <= 0.0f) return 0.0f;
        const float distance = std::hypot(source.x - point.x, source.y - point.y);
        if (distance > definition.range) return 0.0f;
        return std::max(0.0f, definition.damage) / std::max(0.05f, definition.cooldown) *
               (0.1f + 1.0f - distance / definition.range);
    };
    float threat = 0.0f;
    auto  units  = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Faction, Unit::Weapon, Unit::Durability,
                             Unit::Containment>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, motion, faction, weaponLink, durability, containment] = *it;
        if (!durability->alive || containment->container.isBound() ||
            FactionRelationSystem::isAllied(dynamic_cast<Faction*>(faction->link.resolve()), &viewer) ||
            !visible(identity->subject))
            continue;
        auto*       weaponEntity = dynamic_cast<weapon::WeaponEntity*>(weaponLink->link.resolve());
        const auto* definition   = weaponEntity == nullptr ? nullptr : weaponEntity->definition()->def;
        if (definition != nullptr) threat += contribution(*definition, {motion->x, motion->y});
    }
    auto buildings = ecs::View<Building, Building::Identity, Building::Placement, Building::Faction, Building::Weapon,
                               Building::Integrity, Building::Construction>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, placement, faction, weaponLink, integrity, construction] = *it;
        if (!integrity->alive || construction->progress < 1.0f ||
            FactionRelationSystem::isAllied(dynamic_cast<Faction*>(faction->link.resolve()), &viewer) ||
            !visible(identity->subject))
            continue;
        auto*       weaponEntity = dynamic_cast<weapon::WeaponEntity*>(weaponLink->link.resolve());
        const auto* definition   = weaponEntity == nullptr ? nullptr : weaponEntity->definition()->def;
        if (definition != nullptr) threat += contribution(*definition, {placement->worldX, placement->worldY});
    }
    return threat;
}

}  // namespace eve::rts::systems_internal
namespace eve::rts {
using systems_internal::distanceSquared;
using systems_internal::entityPosition;
using systems_internal::isMovementOrder;
using systems_internal::readCurrent;
using systems_internal::visibleHostileThreatAt;
Result<std::size_t> NavigationSystem::step(map::Pathfinder& pathfinder, const NavigationGrid& grid,
                                           const NavigationEvent& unreachable) {
    return stepInternal(pathfinder, grid, unreachable, false);
}
Result<std::size_t> NavigationSystem::stepSharedFlow(map::Pathfinder& pathfinder, const NavigationGrid& grid,
                                                     const NavigationEvent& unreachable) {
    return stepInternal(pathfinder, grid, unreachable, true);
}
Result<std::size_t> NavigationSystem::stepInternal(map::Pathfinder& pathfinder, const NavigationGrid& grid,
                                                   const NavigationEvent& unreachable, bool sharedFlows) {
    EV_PROFILE_MODULE("rts", "NavigationSystem::step");
    if (!std::isfinite(grid.cellSize) || grid.cellSize <= 0.0f || !std::isfinite(grid.originX) ||
        !std::isfinite(grid.originY))
        return Result<std::size_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "RTS navigation grid requires finite origin and positive cell size", "navigation.grid"));
    const auto worldToCell = [&](float value, float origin) {
        return static_cast<int>(std::lround((value - origin) / grid.cellSize));
    };
    const auto cellToWorld = [&](int value, float origin) {
        return origin + static_cast<float>(value) * grid.cellSize;
    };
    std::size_t processed = 0;
    auto        view = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Navigation, Unit::Orders, Unit::Containment,
                                 Unit::Tactics, Unit::Supply, Unit::Faction>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, navigation, orders, containment, tactics, supply, faction] = *it;
        auto* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        if (containment->container.isBound()) continue;
        auto current = readCurrent(orders->values);
        if (!current) return Result<std::size_t>::failure(current.status());
        auto record = std::move(current).takeValue();
        if (!record || !isMovementOrder(record->kind)) {
            navigation->waypoints.clear();
            navigation->waypointIndex = 0;
            navigation->plannedOrderId.clear();
            navigation->unreachable         = false;
            navigation->unreachableReported = false;
            navigation->trafficWaiting      = false;
            navigation->patrolInitialized   = false;
            navigation->patrolTowardTarget  = true;
            supply->routeThreat             = 0.0f;
            supply->routeAvoidedThreat      = false;
            continue;
        }
        const auto goal = systems_internal::movementGoal(*unit, *record);
        if (sharedFlows && systems_internal::isSharedFlowNavigationOrder(*unit, *record)) {
            if (navigation->plannedOrderId != record->id) {
                navigation->unreachable         = false;
                navigation->unreachableReported = false;
            }
            navigation->waypoints.clear();
            navigation->waypointIndex  = 0;
            navigation->plannedOrderId = record->id;
            navigation->plannedGoal    = goal;
            ++processed;
            continue;
        }
        const bool supplyMission      = record->kind == OrderKind::Resupply || record->kind == OrderKind::SupplyRelay;
        auto*      viewer             = dynamic_cast<Faction*>(faction->link.resolve());
        float      currentRouteThreat = 0.0f;
        if (supplyMission && viewer != nullptr) {
            WorldPosition previous{motion->x, motion->y};
            for (std::size_t index = navigation->waypointIndex; index < navigation->waypoints.size(); ++index) {
                const WorldPosition point = navigation->waypoints[index];
                currentRouteThreat +=
                    visibleHostileThreatAt(*viewer, point) * std::hypot(point.x - previous.x, point.y - previous.y);
                previous = point;
            }
        }
        const bool threatChanged = supplyMission && navigation->plannedOrderId == record->id &&
                                   currentRouteThreat > supply->routeThreat + 1e-4f;
        const bool changed = navigation->plannedOrderId != record->id || threatChanged ||
                             distanceSquared(goal.x, goal.y, navigation->plannedGoal.x, navigation->plannedGoal.y) >
                                 grid.cellSize * grid.cellSize * 0.25f;
        if (changed) {
            navigation->waypoints.clear();
            navigation->waypointIndex         = 0;
            navigation->plannedOrderId        = record->id;
            navigation->plannedGoal           = goal;
            navigation->unreachable           = false;
            navigation->unreachableReported   = false;
            const int                  startX = worldToCell(motion->x, grid.originX);
            const int                  startY = worldToCell(motion->y, grid.originY);
            const int                  goalX  = worldToCell(goal.x, grid.originX);
            const int                  goalY  = worldToCell(goal.y, grid.originY);
            std::unique_ptr<map::Path> baseline;
            std::unique_ptr<map::Path> path;
            if (supplyMission) {
                baseline.reset(pathfinder.findPath(startX, startY, goalX, goalY));
                path.reset(pathfinder.findPath(startX, startY, goalX, goalY, [&](int x, int y) {
                    if (viewer == nullptr) return 0.0f;
                    const WorldPosition point{cellToWorld(x, grid.originX), cellToWorld(y, grid.originY)};
                    return visibleHostileThreatAt(*viewer, point) * 0.5f;
                }));
                const auto exposure = [&](const map::Path* candidate) {
                    if (candidate == nullptr || candidate->empty() || viewer == nullptr) return 0.0f;
                    float         total = 0.0f;
                    WorldPosition previous{motion->x, motion->y};
                    for (int index = 1; index < candidate->getLength(); ++index) {
                        const WorldPosition point{cellToWorld(candidate->getX(index), grid.originX),
                                                  cellToWorld(candidate->getY(index), grid.originY)};
                        total += visibleHostileThreatAt(*viewer, point) *
                                 std::hypot(point.x - previous.x, point.y - previous.y);
                        previous = point;
                    }
                    return total;
                };
                supply->routeThreat = exposure(path.get());
                supply->routeAvoidedThreat =
                    baseline != nullptr && !baseline->empty() && supply->routeThreat + 1e-4f < exposure(baseline.get());
            } else {
                path.reset(pathfinder.findPath(startX, startY, goalX, goalY));
                supply->routeThreat        = 0.0f;
                supply->routeAvoidedThreat = false;
            }
            if (path == nullptr || path->empty()) {
                navigation->unreachable = true;
            } else {
                navigation->waypoints.reserve(static_cast<std::size_t>(path->getLength()));
                for (int index = 1; index < path->getLength(); ++index)
                    navigation->waypoints.push_back(
                        {cellToWorld(path->getX(index), grid.originX), cellToWorld(path->getY(index), grid.originY)});
            }
        } else if (supplyMission) {
            supply->routeThreat = currentRouteThreat;
        }
        while (navigation->waypointIndex < navigation->waypoints.size()) {
            const auto  waypoint = navigation->waypoints[navigation->waypointIndex];
            const float radius   = std::max(motion->arrivalRadius, grid.cellSize * 0.1f);
            if (distanceSquared(motion->x, motion->y, waypoint.x, waypoint.y) > radius * radius) break;
            ++navigation->waypointIndex;
        }
        if (navigation->unreachable && !navigation->unreachableReported) {
            navigation->unreachableReported = true;
            if (unreachable) unreachable(*unit, *record);
        }
        ++processed;
    }
    return Result<std::size_t>::success(processed,
                                        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::rts
