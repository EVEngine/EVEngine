#pragma once
// Private rts.roots v1 row ledger. Both directions consume the same typed fields;
// orders, effects, projectiles and production retain their canonical nested codecs.
#include <tuple>
#include <type_traits>
#include "rts/RTSSnapshot.h"
namespace eve::rts::root_state_codec {
template <class T>
auto fields(T& v) {
    using U = std::remove_cv_t<T>;
    if constexpr (std::is_same_v<U, WorldPosition>)
        return std::tie(v.x, v.y);
    else if constexpr (std::is_same_v<U, combat::CombatState>)
        return std::tie(v.subject, v.health, v.maxHealth, v.poise, v.maxPoise);
    else if constexpr (std::is_same_v<U, attributes::AttributeSnapshotBase>)
        return std::tie(v.attribute, v.base);
    else if constexpr (std::is_same_v<U, attributes::AttributeModifier>)
        return std::tie(v.id, v.attribute, v.source, v.operation, v.value, v.priority, v.sequence, v.policyId);
    else if constexpr (std::is_same_v<U, attributes::AttributeProjectionSnapshot>)
        return std::tie(v.owner, v.capturedRevision, v.bases, v.modifiers);
    else if constexpr (std::is_same_v<U, effects::EffectPolicy>)
        return std::tie(v.stackMode, v.stackCount, v.duration, v.magnitude, v.overflow, v.maxStacks);
    else if constexpr (std::is_same_v<U, RTSEffectDefinition>)
        return std::tie(v.id, v.source, v.duration, v.period, v.magnitude, v.speedMultiplier, v.damageMultiplier,
                        v.incomingDamageMultiplier, v.healingPerSecond, v.policy, v.kind, v.tags);
    else if constexpr (std::is_same_v<U, AbilitySpec>)
        return std::tie(v.id, v.casterDefinition, v.target, v.resourceType, v.damageType, v.range, v.radius, v.cooldown,
                        v.resourceCost, v.damage, v.healing, v.castTime, v.channelTickInterval, v.interruptOnDamage,
                        v.appliesEffect, v.effect);
    else if constexpr (std::is_same_v<U, CommandSpec>)
        return std::tie(v.kind, v.target, v.definitionId, v.priority, v.timeoutSeconds, v.targetEntity,
                        v.secondaryTarget, v.radius, v.append);
    else if constexpr (std::is_same_v<U, Unit::Motion>)
        return std::tie(v.x, v.y, v.speed, v.arrivalRadius, v.arrived, v.airborne);
    else if constexpr (std::is_same_v<U, Unit::Navigation>)
        return std::tie(v.waypoints, v.waypointIndex, v.plannedOrderId, v.plannedGoal, v.patrolOrigin,
                        v.movementPriority, v.patrolInitialized, v.patrolTowardTarget, v.trafficWaiting, v.unreachable,
                        v.unreachableReported);
    else if constexpr (std::is_same_v<U, Unit::Vision>)
        return std::tie(v.sightRange, v.detectionRange, v.detectionStrength, v.radarRange, v.radarResolution,
                        v.jammingRange, v.stealth, v.enabled, v.cloaked);
    else if constexpr (std::is_same_v<U, RTSWorkerSnapshot>)
        return std::tie(v.resourceType, v.resourceNode, v.dropoff, v.cargo, v.capacity, v.gatherRate, v.buildRate,
                        v.repairRate, v.autoAssign);
    else if constexpr (std::is_same_v<U, Unit::Combat>)
        return std::tie(v.target, v.stance, v.acquisitionRange, v.engagementRange, v.leashRange, v.guardX, v.guardY,
                        v.guardSet, v.holdPosition, v.attackMove, v.suppressionPerShot, v.upgradeDamageFactor,
                        v.turnRateDegrees, v.aimToleranceDegrees, v.firingHeight, v.targetHeight, v.shotSequence,
                        v.targetPriorities);
    else if constexpr (std::is_same_v<U, Unit::Durability>)
        return std::tie(v.state, v.alive);
    else if constexpr (std::is_same_v<U, Unit::Shield>)
        return std::tie(v.value, v.capacity, v.regenRate, v.regenDelay, v.cooldown);
    else if constexpr (std::is_same_v<U, Unit::Veterancy>)
        return std::tie(v.experience, v.level, v.veteranThreshold, v.eliteThreshold, v.veteranDamageFactor,
                        v.eliteDamageFactor, v.veteranHealthFactor, v.eliteHealthFactor);
    else if constexpr (std::is_same_v<U, Unit::Command>)
        return std::tie(v.range, v.capacity, v.load, v.cost, v.priority, v.jammingRange, v.outOfCommandSpeedFactor,
                        v.outOfCommandDamageFactor, v.requiresCommand, v.relayRequiresUplink, v.relayActive, v.jammed,
                        v.inCommand, v.source, v.uplink);
    else if constexpr (std::is_same_v<U, Unit::Abilities::Cooldown>)
        return std::tie(v.id, v.remaining);
    else if constexpr (std::is_same_v<U, Unit::Abilities::Channel>)
        return std::tie(v.spec, v.target, v.point, v.startingHealth, v.remaining, v.tickRemaining);
    else if constexpr (std::is_same_v<U, Unit::Abilities>)
        return std::tie(v.cooldowns, v.channel);
    else if constexpr (std::is_same_v<U, Unit::Capture>)
        return std::tie(v.rate);
    else if constexpr (std::is_same_v<U, Unit::Supply>)
        return std::tie(v.stock, v.capacity, v.range, v.transferRate, v.transferProgress, v.reservedStock,
                        v.autoThreshold, v.priority, v.autoDispatch, v.relayEnabled, v.returning, v.rendezvousActive,
                        v.convoyWaiting, v.convoyIndex, v.assignedTarget, v.convoyLeader, v.returnPoint,
                        v.rendezvousPoint, v.rendezvousThreat, v.rendezvousAvoidedThreat, v.routeThreat,
                        v.routeAvoidedThreat);
    else if constexpr (std::is_same_v<U, Unit::Morale>)
        return std::tie(v.suppression, v.capacity, v.recoveryRate, v.suppressedSpeedFactor, v.suppressedDamageFactor,
                        v.retreatThreshold, v.retreatDistance, v.auraRange, v.auraSuppressionFactor,
                        v.auraRecoveryBonus, v.active, v.retreatEnabled, v.retreating);
    else if constexpr (std::is_same_v<U, Unit::Artillery>)
        return std::tie(v.deployTime, v.deployRemaining, v.shootAndScootDistance, v.previousX, v.previousY,
                        v.relocationTarget, v.departedPosition, v.relocationThreat, v.relocationConflictCount,
                        v.positionInitialized, v.relocating, v.hasDepartedPosition, v.shotSequence,
                        v.suppressionShotsRemaining, v.fireSupportRequester, v.observedFireSpotter, v.usingObservedFire,
                        v.lastFireTick, v.lastFirePosition, v.autoCounterBattery, v.counterBatteryWindowTicks);
    else if constexpr (std::is_same_v<U, Unit::Tactics>)
        return std::tie(v.combatGroup, v.threatSector, v.coordinatedVolleyInterval, v.volleyReleaseRemaining,
                        v.fireControlEffectiveness, v.volleyHolding, v.escortTarget, v.escortOffsetX, v.escortOffsetY,
                        v.protectionRange, v.guardX, v.guardY, v.guardSet, v.escortScreenSector, v.escortSectorMatched,
                        v.escortReinforcing, v.escortReinforcementSector, v.escortRearGuard, v.escortInterceptTarget,
                        v.escortHandoffCount, v.retreatFireTeam, v.retreatCoverElapsed, v.retreatCovering);
    else if constexpr (std::is_same_v<U, Unit::Technology>)
        return std::tie(v.applied);
    else if constexpr (std::is_same_v<U, Building::Placement>)
        return std::tie(v.cellX, v.cellY, v.worldX, v.worldY, v.rotation, v.placed);
    else if constexpr (std::is_same_v<U, Building::Construction>)
        return std::tie(v.progress, v.buildTimeSeconds, v.paused, v.builders);
    else if constexpr (std::is_same_v<U, Building::Integrity>)
        return std::tie(v.state, v.alive, v.repairCostPerHealth, v.repairCostRemainder, v.repairResource);
    else if constexpr (std::is_same_v<U, Building::Shield>)
        return std::tie(v.value, v.capacity, v.regenRate, v.regenDelay, v.cooldown);
    else if constexpr (std::is_same_v<U, Building::Capture>)
        return std::tie(v.capturable, v.durationSeconds, v.progress, v.capturingFaction, v.blockedByGarrison);
    else if constexpr (std::is_same_v<U, Building::Dropoff>)
        return std::tie(v.acceptedResources, v.radius);
    else if constexpr (std::is_same_v<U, Building::Rally>)
        return std::tie(v.enabled, v.command, v.combatGroup, v.transport, v.minimumTransportLoad, v.transportActive,
                        v.productionSpawnBlocked, v.blockedProductionTask, v.settledProductionTasks, v.reinforcements,
                        v.reinforcementLimit, v.reinforcementTypeLimits, v.reinforcementTypePriorities,
                        v.reinforcementFallbacks, v.reinforcementCapped, v.reinforcementPolicyPausedTask,
                        v.reinforcementAutoCancelDelay, v.reinforcementCappedSeconds);
    else if constexpr (std::is_same_v<U, Building::Combat>)
        return std::tie(v.target, v.airDefenseNetworkRoot, v.acquisitionRange, v.engagementRange,
                        v.airDefenseNetworkRange, v.airDefenseNetworkSize, v.turnRateDegrees, v.aimToleranceDegrees,
                        v.firingHeight, v.targetHeight, v.shotSequence, v.targetPriorities);
    else if constexpr (std::is_same_v<U, Building::Garrison>)
        return std::tie(v.capacity, v.damageBonusPerOccupant, v.occupants);
    else if constexpr (std::is_same_v<U, Building::Supply>)
        return std::tie(v.stock, v.capacity, v.range, v.transferRate, v.transferProgress, v.productionResource,
                        v.productionCostPerRound, v.productionRate, v.productionProgress);
    else if constexpr (std::is_same_v<U, Building::Vision>)
        return std::tie(v.sightRange, v.detectionRange, v.detectionStrength, v.radarRange, v.radarResolution,
                        v.jammingRange, v.enabled);
    else if constexpr (std::is_same_v<U, Building::Technology>)
        return std::tie(v.applied);
    else if constexpr (std::is_same_v<U, Building::Infrastructure>)
        return std::tie(v.powerProduced, v.powerConsumed, v.powerPriority, v.powered, v.buildInfluenceRadius,
                        v.incomeResource, v.incomeRate, v.incomeProgress);
    else if constexpr (std::is_same_v<U, Building::Command>)
        return std::tie(v.range, v.capacity, v.load, v.jammingRange, v.jammed, v.active);
    else if constexpr (std::is_same_v<U, Building::IndirectFire>)
        return std::tie(v.lastFireTick, v.lastFirePosition);
    else if constexpr (std::is_same_v<U, ResourceNode::Position>)
        return std::tie(v.x, v.y, v.radius);
    else if constexpr (std::is_same_v<U, ResourceNode::Stock>)
        return std::tie(v.resourceType, v.remaining, v.maximum, v.infinite);
    else if constexpr (std::is_same_v<U, Faction::Strategy>)
        return std::tie(v.workerDefinition, v.armyDefinition, v.targetBuildingDefinition, v.desiredWorkers,
                        v.attackThreshold, v.thinkInterval, v.thinkAccumulator, v.formationSpacing, v.enabled);
    else if constexpr (std::is_same_v<U, Faction::Workforce>)
        return std::tie(v.autoConstruction, v.autoRepair, v.maxBuildersPerSite, v.maxRepairersPerBuilding,
                        v.reserveWorkers);
    else if constexpr (std::is_same_v<U, Faction::ProductionPolicy::Reserve>)
        return std::tie(v.amount, v.minimumPriority);
    else if constexpr (std::is_same_v<U, Faction::ProductionPolicy>)
        return std::tie(v.resourceReserves);
    else if constexpr (std::is_same_v<U, Faction::Intel::Contact>)
        return std::tie(v.subject, v.kind, v.position, v.ageSeconds, v.visible, v.detected);
    else if constexpr (std::is_same_v<U, Faction::Intel>)
        return std::tie(v.enabled, v.contacts);
    else if constexpr (std::is_same_v<U, Faction::Technology>)
        return std::tie(v.unlocked, v.consumedTasks, v.unlockedDefinitions);
    else if constexpr (std::is_same_v<U, Match::Rules>)
        return std::tie(v.rule, v.archetype, v.targetValue);
    else if constexpr (std::is_same_v<U, Match::State>)
        return std::tie(v.phase, v.winningTeam, v.updateSequence);
    else if constexpr (std::is_same_v<U, Match::Events::Event>)
        return std::tie(v.sequence, v.kind, v.faction, v.team, v.reason);
    else if constexpr (std::is_same_v<U, Match::Events>)
        return std::tie(v.values);
    else if constexpr (std::is_same_v<U, RTSMatchParticipantSnapshot>)
        return std::tie(v.faction, v.team, v.eliminated, v.surrendered, v.reason);
    else if constexpr (std::is_same_v<U, RTSMovementGroupSnapshot::Member>)
        return std::tie(v.subject, v.orderId);
    else if constexpr (std::is_same_v<U, RTSMovementGroupSnapshot>)
        return std::tie(v.members, v.leadDistance);
    else if constexpr (std::is_same_v<U, RTSResourceNodeSnapshot>)
        return std::tie(v.subject, v.displayName, v.position, v.stock, v.workerCapacity, v.workers);
    else if constexpr (std::is_same_v<U, RTSPlayerSnapshot>)
        return std::tie(v.subject, v.displayName, v.units, v.buildings, v.selectionRevision, v.selectionTick);
    else if constexpr (std::is_same_v<U, RTSFactionSnapshot>)
        return std::tie(v.subject, v.displayName, v.units, v.buildings, v.strategy, v.workforce, v.productionPolicy,
                        v.intel, v.technology);
    else if constexpr (std::is_same_v<U, RTSMatchSnapshot>)
        return std::tie(v.subject, v.rules, v.participants, v.state, v.events);
    else if constexpr (std::is_same_v<U, RTSUnitSnapshot>)
        return std::tie(v.subject, v.definition, v.displayName, v.attributes, v.tags, v.effects, v.faction, v.motion,
                        v.navigation, v.vision, v.worker, v.combat, v.combatTarget, v.durability, v.shield, v.veterancy,
                        v.command, v.commandSource, v.commandUplink, v.abilities, v.abilityTarget, v.capture,
                        v.containmentCapacity, v.container, v.occupants, v.supply, v.supplyTarget, v.convoyLeader,
                        v.morale, v.artillery, v.fireSupportRequester, v.observedFireSpotter, v.tactics, v.escortTarget,
                        v.technology);
    else if constexpr (std::is_same_v<U, RTSBuildingSnapshot>)
        return std::tie(v.subject, v.definition, v.displayName, v.tags, v.effects, v.faction, v.placement,
                        v.construction, v.builders, v.integrity, v.shield, v.capture, v.capturingFaction, v.dropoff,
                        v.rally, v.rallyCommandTarget, v.rallyTransport, v.reinforcements, v.combat, v.combatTarget,
                        v.airDefenseNetworkRoot, v.garrison, v.occupants, v.supply, v.vision, v.technology,
                        v.infrastructure, v.command, v.indirectFire);
    else
        static_assert(!sizeof(T), "Undeclared RTS root wire type");
}
}  // namespace eve::rts::root_state_codec
