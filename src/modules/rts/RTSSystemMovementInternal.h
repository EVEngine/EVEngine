#pragma once

// Private shared movement helpers; no independent state or provider lookup.
#include <algorithm>
#include <cmath>
#include <optional>
#include "rts/RTSSystems.h"

namespace eve::rts::systems_internal {

/** @brief Observe visible hostile weapon threat at a world point using canonical faction intelligence. */
float visibleHostileThreatAt(Faction& viewer, WorldPosition point);

// Conservative swept-radius query against the canonical navigation grid.
// Oversized/invalid queries return false and retain ordinary path steering.
EVENGINE_API_DOMAINS bool isFormationSegmentClear(const map::Pathfinder& pathfinder, const NavigationGrid& grid,
                                                  WorldPosition from, WorldPosition to, float radius);

inline bool isFinitePosition(WorldPosition position) { return std::isfinite(position.x) && std::isfinite(position.y); }

inline bool isSameHandle(const ecs::EntityHandle& left, const ecs::EntityHandle& right) {
    return left.table == right.table && left.type == right.type && left.id == right.id &&
           left.generation == right.generation;
}


inline float distanceSquared(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

/** @brief Whether planning uses the shared policy, before the current group projection exists. */
inline bool isSharedFlowNavigationOrder(Unit& unit, const OrderRecord& order) {
    return unit.crowd()->link.isBound() &&
           (order.kind == OrderKind::Move || order.kind == OrderKind::AttackMove || order.kind == OrderKind::Attack ||
            order.kind == OrderKind::Escort || order.kind == OrderKind::Patrol) &&
           !unit.navigation()->trafficRecoveryTarget;
}

/** @brief Whether motion uses a shared field instead of a radius-clear moving slot. */
inline bool isSharedFlowOrder(Unit& unit, const OrderRecord& order) {
    return isSharedFlowNavigationOrder(unit, order) && !unit.navigation()->formationTarget &&
           !unit.navigation()->combatSlotTarget;
}

inline bool isMovementOrder(OrderKind kind) {
    switch (kind) {
        case OrderKind::Move:
        case OrderKind::Attack:
        case OrderKind::AttackMove:
        case OrderKind::Build:
        case OrderKind::Gather:
        case OrderKind::ReturnCargo:
        case OrderKind::Patrol:
        case OrderKind::Repair:
        case OrderKind::Garrison:
        case OrderKind::BoardTransport:
        case OrderKind::Capture:
        case OrderKind::Resupply:
        case OrderKind::Escort:
        case OrderKind::SupplyRelay: return true;
        default: return false;
    }
}

inline bool isInteractionOrder(OrderKind kind) {
    return kind == OrderKind::Gather || kind == OrderKind::ReturnCargo || kind == OrderKind::Build ||
           kind == OrderKind::Repair;
}

// Service contact is based on occupied geometry, not exact overlap of agent centers.
// The radius is derived from canonical node membership/building footprint and is
// recomputed synchronously; no second range state or persistent format is created.
inline float interactionArrivalRadius(Unit& unit, const OrderRecord& order) {
    float radius = unit.motion()->arrivalRadius;
    if (order.kind == OrderKind::Gather) {
        if (auto* node = dynamic_cast<ResourceNode*>(ecs::try_get(order.targetEntity))) {
            const float workers = static_cast<float>(std::max<std::size_t>(1, node->harvest()->workers.size()));
            radius              = std::max(radius, unit.crowd()->radius * 2.0f * std::sqrt(workers));
        }
    } else if (isInteractionOrder(order.kind)) {
        if (auto* building = dynamic_cast<Building*>(ecs::try_get(order.targetEntity)))
            radius = std::max(radius, building->dropoff()->radius + unit.crowd()->radius);
    }
    return radius;
}

inline std::optional<WorldPosition> entityPosition(const ecs::EntityHandle& handle) {
    if (auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle)))
        return WorldPosition{unit->motion()->x, unit->motion()->y};
    if (auto* building = dynamic_cast<Building*>(ecs::try_get(handle)))
        return WorldPosition{building->placement()->worldX, building->placement()->worldY};
    return std::nullopt;
}

/** @brief Resolve the current canonical movement goal without retaining entity pointers.
 * @ownership Returns a world-position value. @thread Owner thread. @reentrancy No callbacks.
 */
inline WorldPosition movementGoal(Unit& unit, const OrderRecord& order) {
    WorldPosition goal = order.target;
    if (order.kind == OrderKind::Attack || order.kind == OrderKind::Resupply || order.kind == OrderKind::SupplyRelay) {
        if (auto target = entityPosition(order.targetEntity)) goal = *target;
        if (order.kind == OrderKind::SupplyRelay && unit.supply()->rendezvousActive)
            goal = unit.supply()->rendezvousPoint;
    } else if (order.kind == OrderKind::Escort && unit.tactics()->guardSet) {
        goal = {unit.tactics()->guardX, unit.tactics()->guardY};
    } else if (order.kind == OrderKind::Patrol && unit.navigation()->patrolInitialized &&
               !unit.navigation()->patrolTowardTarget) {
        goal = unit.navigation()->patrolOrigin;
    }
    return goal;
}

inline Result<std::optional<OrderRecord>> readCurrent(OrderComponent& orders) {
    auto current = orders.current();
    if (current) return Result<std::optional<OrderRecord>>::success(std::move(current).takeValue());
    if (current.code() == StatusCode::NotFound) {
        current.ignore("RTS entity has no active order");
        return Result<std::optional<OrderRecord>>::success(std::nullopt, Status::success(StatusCode::NoOp));
    }
    return Result<std::optional<OrderRecord>>::failure(current.status());
}

}  // namespace eve::rts::systems_internal
