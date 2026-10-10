#include "rts/RTS.h"
#include "rts/RTSSnapshotInternal.h"

#include "common/Value.h"

#include <algorithm>
#include <unordered_set>

namespace eve::rts {
namespace {

using snapshot_internal::subjectOf;

template <typename T>
std::vector<SubjectRef> subjectsOf(const std::vector<T>& handles) {
    std::vector<SubjectRef> result;
    result.reserve(handles.size());
    for (const auto& handle : handles) {
        const auto subject = subjectOf(handle);
        if (subject.isValid()) result.push_back(subject);
    }
    return result;
}

void stabilizeOrders(OrderComponent::Snapshot& state, std::map<std::string, SubjectRef>& targets) {
    for (auto& [id, command] : state.extended) {
        const auto subject = subjectOf(command.targetEntity);
        if (subject.isValid()) targets[id] = subject;
        command.targetEntity = {};
    }
}

Value refsValue(const std::vector<SubjectRef>& values) {
    Value::Array result;
    for (SubjectRef value : values) result.emplace_back(value.format());
    return Value(std::move(result));
}

Value stringsValue(const std::vector<std::string>& values) {
    Value::Array result;
    for (const auto& value : values) result.emplace_back(value);
    return Value(std::move(result));
}

Value prioritiesValue(const std::map<std::string, float>& values) {
    Value::Object result;
    for (const auto& [key, value] : values) result.emplace(key, value);
    return Value(std::move(result));
}

template <typename T>
Value integerMapValue(const std::map<std::string, T>& values) {
    Value::Object result;
    for (const auto& [key, value] : values) result.emplace(key, static_cast<std::int64_t>(value));
    return Value(std::move(result));
}

Value stringMapValue(const std::map<std::string, std::string>& values) {
    Value::Object result;
    for (const auto& [key, value] : values) result.emplace(key, value);
    return Value(std::move(result));
}

Value positionValue(WorldPosition value) { return Value(Value::Object{{"x", value.x}, {"y", value.y}}); }

Value abilitySpecValue(const AbilitySpec& value) {
    return Value(Value::Object{{"appliesEffect", value.appliesEffect},
                               {"castTime", value.castTime},
                               {"caster", value.casterDefinition.format()},
                               {"channelTick", value.channelTickInterval},
                               {"cooldown", value.cooldown},
                               {"damage", value.damage},
                               {"damageType", value.damageType},
                               {"effectDuration", value.effect.duration},
                               {"effectId", value.effect.id},
                               {"effectKind", static_cast<std::int64_t>(value.effect.kind)},
                               {"effectMagnitude", value.effect.magnitude},
                               {"effectPeriod", value.effect.period},
                               {"effectMaxStacks", static_cast<std::int64_t>(value.effect.policy.maxStacks)},
                               {"effectStackMode", static_cast<std::int64_t>(value.effect.policy.stackMode)},
                               {"effectSource", value.effect.source},
                               {"effectTags", stringsValue(value.effect.tags)},
                               {"healing", value.healing},
                               {"id", value.id},
                               {"interruptOnDamage", value.interruptOnDamage},
                               {"radius", value.radius},
                               {"range", value.range},
                               {"resourceCost", value.resourceCost},
                               {"resourceType", value.resourceType},
                               {"target", static_cast<std::int64_t>(value.target)}});
}

Value attributesValue(const attributes::AttributeProjectionSnapshot& value) {
    Value::Array bases;
    for (const auto& base : value.bases)
        bases.emplace_back(Value::Object{{"attribute", base.attribute}, {"base", base.base}});
    Value::Array modifiers;
    for (const auto& modifier : value.modifiers)
        modifiers.emplace_back(Value::Object{{"attribute", modifier.attribute},
                                             {"id", modifier.id},
                                             {"operation", static_cast<std::int64_t>(modifier.operation)},
                                             {"policy", modifier.policyId},
                                             {"priority", static_cast<std::int64_t>(modifier.priority)},
                                             {"sequence", static_cast<std::int64_t>(modifier.sequence)},
                                             {"source", modifier.source},
                                             {"value", modifier.value}});
    return Value(Value::Object{{"bases", Value(std::move(bases))},
                               {"modifiers", Value(std::move(modifiers))},
                               {"revision", static_cast<std::int64_t>(value.capturedRevision.value())}});
}

Value effectsValue(const RTSEffectSnapshot& value) {
    Value::Array instances;
    for (int index = 0; index < value.effects.effectCount(); ++index) {
        const auto* effect = value.effects.effectAt(index);
        if (effect == nullptr) continue;
        instances.emplace_back(Value::Object{{"duration", effect->duration},
                                             {"id", effect->id},
                                             {"magnitude", effect->magnitude},
                                             {"payload", effect->payload.toJson()},
                                             {"period", effect->period},
                                             {"periodElapsed", effect->periodElapsed},
                                             {"priority", effect->priority},
                                             {"remaining", effect->remaining},
                                             {"source", effect->source},
                                             {"policyDuration", static_cast<std::int64_t>(effect->policy.duration)},
                                             {"policyMagnitude", static_cast<std::int64_t>(effect->policy.magnitude)},
                                             {"policyMaxStacks", static_cast<std::int64_t>(effect->policy.maxStacks)},
                                             {"policyOverflow", static_cast<std::int64_t>(effect->policy.overflow)},
                                             {"policyStackCount", static_cast<std::int64_t>(effect->policy.stackCount)},
                                             {"policyStackMode", static_cast<std::int64_t>(effect->policy.stackMode)},
                                             {"stackCount", static_cast<std::int64_t>(effect->stackCount)},
                                             {"stackKey", effect->stackKey},
                                             {"subject", effect->subject},
                                             {"tags", stringsValue(effect->tags)},
                                             {"type", effect->type}});
    }
    return Value(Value::Object{{"commandInterrupts", static_cast<std::int64_t>(value.target.commandInterrupts)},
                               {"effects", Value(std::move(instances))},
                               {"morale", value.target.morale},
                               {"productionLocked", value.target.productionLocked},
                               {"suppression", static_cast<std::int64_t>(value.target.suppression)}});
}

Value ordersValue(const OrderComponent::Snapshot& orders, const std::map<std::string, SubjectRef>& targets) {
    Value::Array extended;
    for (const auto& [id, command] : orders.extended) {
        auto target = targets.find(id);
        extended.emplace_back(
            Value::Object{{"append", command.append},
                          {"definition", command.definitionId},
                          {"id", id},
                          {"kind", static_cast<std::int64_t>(command.kind)},
                          {"priority", command.priority},
                          {"radius", command.radius},
                          {"secondary", positionValue(command.secondaryTarget)},
                          {"target", positionValue(command.target)},
                          {"targetEntity", target == targets.end() ? std::string{} : target->second.format()},
                          {"timeout", command.timeoutSeconds}});
    }
    return Value(Value::Object{{"extended", Value(std::move(extended))}, {"queue", orders.queueJson}});
}

}  // namespace

Result<RTSStateSnapshot> RTS::snapshotState() const {
    RTSStateSnapshot result;
    auto             groups = captureMovementGroups();
    if (!groups) return Result<RTSStateSnapshot>::failure(groups.status());
    result.movementGroups = std::move(groups).takeValue();
    auto* currentTable    = ecs::current();
    for (const auto& handle : units_) {
        if (handle.table != currentTable) continue;
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (unit == nullptr) continue;
        RTSUnitSnapshot value;
        value.subject     = unit->identity()->subject;
        value.definition  = unit->definition()->id;
        value.displayName = unit->identity()->displayName;
        auto attributes   = RTSUnitAttributeAdapter::snapshot(*unit);
        if (!attributes) return Result<RTSStateSnapshot>::failure(attributes.status());
        value.attributes                      = std::move(attributes).takeValue();
        value.attributes.owner                = {};
        value.tags                            = unit->tags()->values.values();
        value.effects                         = unit->effects()->values.snapshot();
        value.faction                         = subjectOf(unit->faction()->link.resolve());
        value.motion                          = *unit->motion();
        value.navigation                      = *unit->navigation();
        value.navigation.formationSpeedFactor = 1.f;
        value.navigation.formationTarget.reset();
        if (value.navigation.trafficRecoveryTarget) value.navigation.plannedOrderId.clear();
        value.navigation.trafficRecoveryTarget.reset();
        value.vision         = *unit->vision();
        value.worker         = {unit->worker()->resourceType,
                                subjectOf(unit->worker()->resourceNode.resolve()),
                                subjectOf(unit->worker()->dropoff.resolve()),
                                unit->worker()->cargo,
                                unit->worker()->capacity,
                                unit->worker()->gatherRate,
                                unit->worker()->buildRate,
                                unit->worker()->repairRate,
                                unit->worker()->autoAssign};
        value.combat         = *unit->combat();
        value.combatTarget   = subjectOf(value.combat.target);
        value.combat.target  = {};
        value.durability     = *unit->durability();
        value.shield         = *unit->shield();
        value.veterancy      = *unit->veterancy();
        value.command        = *unit->command();
        value.commandSource  = subjectOf(value.command.source);
        value.commandUplink  = subjectOf(value.command.uplink);
        value.command.source = {};
        value.command.uplink = {};
        value.abilities      = *unit->abilities();
        if (value.abilities.channel) {
            value.abilityTarget             = subjectOf(value.abilities.channel->target);
            value.abilities.channel->target = {};
        }
        value.capture                        = *unit->capture();
        value.containmentCapacity            = unit->containment()->capacity;
        value.container                      = subjectOf(unit->containment()->container.resolve());
        value.occupants                      = subjectsOf(unit->containment()->occupants);
        value.supply                         = *unit->supply();
        value.supplyTarget                   = subjectOf(value.supply.assignedTarget);
        value.convoyLeader                   = subjectOf(value.supply.convoyLeader);
        value.supply.assignedTarget          = {};
        value.supply.convoyLeader            = {};
        value.morale                         = *unit->morale();
        value.artillery                      = *unit->artillery();
        value.fireSupportRequester           = subjectOf(value.artillery.fireSupportRequester);
        value.observedFireSpotter            = subjectOf(value.artillery.observedFireSpotter);
        value.artillery.fireSupportRequester = {};
        value.artillery.observedFireSpotter  = {};
        value.tactics                        = *unit->tactics();
        value.escortTarget                   = subjectOf(value.tactics.escortTarget);
        value.tactics.escortTarget           = {};
        value.technology                     = *unit->technology();
        auto orders                          = unit->orders()->values.snapshotState();
        if (!orders)
            return Result<RTSStateSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "failed to snapshot RTS unit orders", "units.orders"));
        value.orders = std::move(orders).takeValue();
        stabilizeOrders(value.orders, value.orderTargets);
        result.units.push_back(std::move(value));
    }
    for (const auto& handle : buildings_) {
        if (handle.table != currentTable) continue;
        auto* building = dynamic_cast<Building*>(ecs::try_get(handle));
        if (building == nullptr) continue;
        RTSBuildingSnapshot value;
        value.subject      = building->identity()->subject;
        value.definition   = building->definition()->id;
        value.displayName  = building->identity()->displayName;
        value.tags         = building->tags()->values.values();
        value.effects      = building->effects()->values.snapshot();
        value.faction      = subjectOf(building->faction()->link.resolve());
        value.placement    = *building->placement();
        value.construction = *building->construction();
        value.builders     = subjectsOf(value.construction.builders);
        value.construction.builders.clear();
        value.integrity                  = *building->integrity();
        value.shield                     = *building->shield();
        value.capture                    = *building->capture();
        value.capturingFaction           = subjectOf(value.capture.capturingFaction);
        value.capture.capturingFaction   = {};
        value.dropoff                    = *building->dropoff();
        value.rally                      = *building->rally();
        value.rallyCommandTarget         = subjectOf(value.rally.command.targetEntity);
        value.rallyTransport             = subjectOf(value.rally.transport);
        value.reinforcements             = subjectsOf(value.rally.reinforcements);
        value.rally.transport            = {};
        value.rally.command.targetEntity = {};
        value.rally.reinforcements.clear();
        value.combat                       = *building->combat();
        value.combatTarget                 = subjectOf(value.combat.target);
        value.airDefenseNetworkRoot        = subjectOf(value.combat.airDefenseNetworkRoot);
        value.combat.target                = {};
        value.combat.airDefenseNetworkRoot = {};
        value.garrison                     = *building->garrison();
        value.occupants                    = subjectsOf(value.garrison.occupants);
        value.garrison.occupants.clear();
        value.supply         = *building->supply();
        value.vision         = *building->vision();
        value.technology     = *building->technology();
        value.infrastructure = *building->infrastructure();
        value.command        = *building->command();
        value.indirectFire   = *building->indirectFire();
        auto production      = building->production()->values.snapshot();
        if (!production)
            return Result<RTSStateSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "failed to snapshot RTS production", "buildings.production"));
        value.productionJson = std::move(production).takeValue();
        auto orders          = building->orders()->values.snapshotState();
        if (!orders)
            return Result<RTSStateSnapshot>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "failed to snapshot RTS building orders", "buildings.orders"));
        value.orders = std::move(orders).takeValue();
        stabilizeOrders(value.orders, value.orderTargets);
        result.buildings.push_back(std::move(value));
    }
    for (const auto& handle : resourceNodes_) {
        if (handle.table != currentTable) continue;
        auto* node = dynamic_cast<ResourceNode*>(ecs::try_get(handle));
        if (node == nullptr) continue;
        result.resourceNodes.push_back({node->identity()->subject, node->identity()->displayName, *node->position(),
                                        *node->stock(), node->harvest()->capacity,
                                        subjectsOf(node->harvest()->workers)});
    }
    for (const auto& handle : players_) {
        if (handle.table != currentTable) continue;
        auto* player = dynamic_cast<Player*>(ecs::try_get(handle));
        if (player == nullptr) continue;
        result.players.push_back({player->identity()->subject, player->identity()->displayName,
                                  subjectsOf(player->selection()->units), subjectsOf(player->selection()->buildings)});
    }
    for (const auto& handle : factions_) {
        if (handle.table != currentTable) continue;
        auto* faction = dynamic_cast<Faction*>(ecs::try_get(handle));
        if (faction == nullptr) continue;
        result.factions.push_back({faction->identity()->subject, faction->identity()->displayName,
                                   subjectsOf(faction->members()->units), subjectsOf(faction->members()->buildings),
                                   *faction->strategy(), *faction->workforce(), *faction->productionPolicy(),
                                   *faction->intel(), *faction->technology()});
    }
    for (const auto& handle : matches_) {
        if (handle.table != currentTable) continue;
        auto* match = dynamic_cast<Match*>(ecs::try_get(handle));
        if (match == nullptr) continue;
        RTSMatchSnapshot value;
        value.subject = match->identity()->subject;
        value.rules   = *match->rules();
        value.state   = *match->state();
        value.events  = *match->events();
        for (const auto& participant : match->participants()->entries)
            value.participants.push_back({subjectOf(participant.faction.resolve()), participant.team,
                                          participant.eliminated, participant.surrendered, participant.reason});
        result.matches.push_back(std::move(value));
    }
    result.projectiles = projectiles_.snapshot();
    return Result<RTSStateSnapshot>::success(std::move(result), Status::success(StatusCode::Applied));
}

Result<std::string> RTS::canonicalStateJson() const {
    auto captured = snapshotState();
    if (!captured) return Result<std::string>::failure(captured.status());
    auto state     = std::move(captured).takeValue();
    auto bySubject = [](const auto& left, const auto& right) { return left.subject.format() < right.subject.format(); };
    std::sort(state.units.begin(), state.units.end(), bySubject);
    std::sort(state.buildings.begin(), state.buildings.end(), bySubject);
    std::sort(state.resourceNodes.begin(), state.resourceNodes.end(), bySubject);
    std::sort(state.players.begin(), state.players.end(), bySubject);
    std::sort(state.factions.begin(), state.factions.end(), bySubject);
    std::sort(state.matches.begin(), state.matches.end(), bySubject);

    Value::Array projectileSlots;
    for (const auto& slot : state.projectiles.runtime.slots) {
        Value projectileState;
        if (slot.state) {
            const auto& projectile = *slot.state;
            projectileState =
                Value(Value::Object{{"age", static_cast<std::int64_t>(projectile.age.nanoseconds())},
                                    {"definition", projectile.definitionId.format()},
                                    {"generation", static_cast<std::int64_t>(projectile.handle.generation)},
                                    {"gravity", projectile.gravity},
                                    {"lifetime", static_cast<std::int64_t>(projectile.lifetime.nanoseconds())},
                                    {"mode", static_cast<std::int64_t>(projectile.mode)},
                                    {"positionX", projectile.position.x},
                                    {"positionY", projectile.position.y},
                                    {"positionZ", projectile.position.z},
                                    {"slot", static_cast<std::int64_t>(projectile.handle.slot)},
                                    {"turnRate", projectile.maxTurnRateDegrees},
                                    {"velocityX", projectile.velocity.x},
                                    {"velocityY", projectile.velocity.y},
                                    {"velocityZ", projectile.velocity.z}});
        }
        projectileSlots.emplace_back(
            Value::Object{{"generation", static_cast<std::int64_t>(slot.generation)}, {"state", projectileState}});
    }
    Value::Array projectilePayloads;
    for (const auto& payload : state.projectiles.payloads)
        projectilePayloads.emplace_back(Value::Object{{"blocked", payload.blockedByObstacles},
                                                      {"damage", payload.damage},
                                                      {"damageType", payload.damageType},
                                                      {"excludedTags", stringsValue(payload.excludedTargetTags)},
                                                      {"faction", payload.faction.format()},
                                                      {"friendlyFire", payload.friendlyFire},
                                                      {"key", static_cast<std::int64_t>(payload.key)},
                                                      {"radius", payload.radius},
                                                      {"requiredTags", stringsValue(payload.requiredTargetTags)},
                                                      {"source", payload.source.format()},
                                                      {"splashMinimum", payload.splashMinimumDamageFactor},
                                                      {"target", payload.target.format()},
                                                      {"targetPoint", positionValue(payload.targetPoint)},
                                                      {"targetHeight", payload.targetHeight},
                                                      {"targetsAir", payload.targetsAir},
                                                      {"targetsGround", payload.targetsGround}});

    Value::Array units;
    for (const auto& value : state.units) {
        Value::Array waypoints;
        for (auto point : value.navigation.waypoints) waypoints.push_back(positionValue(point));
        Value::Array cooldowns;
        for (const auto& cooldown : value.abilities.cooldowns)
            cooldowns.emplace_back(Value::Object{{"id", cooldown.id}, {"remaining", cooldown.remaining}});
        Value channel;
        if (value.abilities.channel) {
            const auto& active = *value.abilities.channel;
            channel            = Value(Value::Object{{"point", positionValue(active.point)},
                                                     {"spec", abilitySpecValue(active.spec)},
                                                     {"remaining", active.remaining},
                                                     {"startingHealth", active.startingHealth},
                                                     {"target", value.abilityTarget.format()},
                                                     {"tickRemaining", active.tickRemaining}});
        }
        units.emplace_back(Value::Object{
            {"abilities",
             Value(Value::Object{{"channel", std::move(channel)}, {"cooldowns", Value(std::move(cooldowns))}})},
            {"artillery",
             Value(Value::Object{
                 {"autoCounterBattery", value.artillery.autoCounterBattery},
                 {"counterBatteryWindow", static_cast<std::int64_t>(value.artillery.counterBatteryWindowTicks)},
                 {"deployRemaining", value.artillery.deployRemaining},
                 {"deployTime", value.artillery.deployTime},
                 {"lastFirePosition", positionValue(value.artillery.lastFirePosition)},
                 {"lastFireTick", static_cast<std::int64_t>(value.artillery.lastFireTick.value())},
                 {"departedPosition", positionValue(value.artillery.departedPosition)},
                 {"hasDepartedPosition", value.artillery.hasDepartedPosition},
                 {"relocating", value.artillery.relocating},
                 {"relocationConflicts", value.artillery.relocationConflictCount},
                 {"relocationTarget", positionValue(value.artillery.relocationTarget)},
                 {"relocationThreat", value.artillery.relocationThreat},
                 {"positionInitialized", value.artillery.positionInitialized},
                 {"previous", positionValue({value.artillery.previousX, value.artillery.previousY})},
                 {"observedFireSpotter", value.observedFireSpotter.format()},
                 {"requester", value.fireSupportRequester.format()},
                 {"shotSequence", static_cast<std::int64_t>(value.artillery.shotSequence)},
                 {"shotsRemaining", value.artillery.suppressionShotsRemaining},
                 {"shootAndScoot", value.artillery.shootAndScootDistance},
                 {"usingObservedFire", value.artillery.usingObservedFire}})},
            {"attributes", attributesValue(value.attributes)},
            {"combat", Value(Value::Object{{"acquisition", value.combat.acquisitionRange},
                                           {"aimTolerance", value.combat.aimToleranceDegrees},
                                           {"attackMove", value.combat.attackMove},
                                           {"engagement", value.combat.engagementRange},
                                           {"guard", positionValue({value.combat.guardX, value.combat.guardY})},
                                           {"guardSet", value.combat.guardSet},
                                           {"hold", value.combat.holdPosition},
                                           {"leash", value.combat.leashRange},
                                           {"firingHeight", value.combat.firingHeight},
                                           {"targetHeight", value.combat.targetHeight},
                                           {"suppressionPerShot", value.combat.suppressionPerShot},
                                           {"targetPriorities", prioritiesValue(value.combat.targetPriorities)},
                                           {"shotSequence", static_cast<std::int64_t>(value.combat.shotSequence)},
                                           {"stance", combatStanceName(value.combat.stance)},
                                           {"target", value.combatTarget.format()},
                                           {"turnRate", value.combat.turnRateDegrees},
                                           {"upgradeDamage", value.combat.upgradeDamageFactor}})},
            {"command", Value(Value::Object{{"capacity", value.command.capacity},
                                            {"cost", value.command.cost},
                                            {"inCommand", value.command.inCommand},
                                            {"jammed", value.command.jammed},
                                            {"load", value.command.load},
                                            {"priority", value.command.priority},
                                            {"range", value.command.range},
                                            {"damageFactor", value.command.outOfCommandDamageFactor},
                                            {"relayRequiresUplink", value.command.relayRequiresUplink},
                                            {"relayActive", value.command.relayActive},
                                            {"requires", value.command.requiresCommand},
                                            {"speedFactor", value.command.outOfCommandSpeedFactor},
                                            {"source", value.commandSource.format()},
                                            {"uplink", value.commandUplink.format()}})},
            {"container", value.container.format()},
            {"containmentCapacity", static_cast<std::int64_t>(value.containmentCapacity)},
            {"captureRate", value.capture.rate},
            {"definition", value.definition.format()},
            {"displayName", value.displayName},
            {"durability", Value(Value::Object{{"alive", value.durability.alive},
                                               {"health", value.durability.state.health},
                                               {"maxHealth", value.durability.state.maxHealth},
                                               {"maxPoise", value.durability.state.maxPoise},
                                               {"poise", value.durability.state.poise}})},
            {"effects", effectsValue(value.effects)},
            {"faction", value.faction.format()},
            {"morale", Value(Value::Object{{"active", value.morale.active},
                                           {"auraRange", value.morale.auraRange},
                                           {"auraRecovery", value.morale.auraRecoveryBonus},
                                           {"auraSuppression", value.morale.auraSuppressionFactor},
                                           {"capacity", value.morale.capacity},
                                           {"recovery", value.morale.recoveryRate},
                                           {"retreatDistance", value.morale.retreatDistance},
                                           {"retreatEnabled", value.morale.retreatEnabled},
                                           {"retreatThreshold", value.morale.retreatThreshold},
                                           {"retreating", value.morale.retreating},
                                           {"suppressedDamage", value.morale.suppressedDamageFactor},
                                           {"suppressedSpeed", value.morale.suppressedSpeedFactor},
                                           {"suppression", value.morale.suppression}})},
            {"motion", Value(Value::Object{{"arrivalRadius", value.motion.arrivalRadius},
                                           {"airborne", value.motion.airborne},
                                           {"arrived", value.motion.arrived},
                                           {"speed", value.motion.speed},
                                           {"x", value.motion.x},
                                           {"y", value.motion.y}})},
            {"navigation",
             Value(Value::Object{{"goal", positionValue(value.navigation.plannedGoal)},
                                 {"movementPriority", value.navigation.movementPriority},
                                 {"order", value.navigation.plannedOrderId},
                                 {"unreachable", value.navigation.unreachable},
                                 {"patrolInitialized", value.navigation.patrolInitialized},
                                 {"patrolOrigin", positionValue(value.navigation.patrolOrigin)},
                                 {"patrolTowardTarget", value.navigation.patrolTowardTarget},
                                 {"trafficWaiting", value.navigation.trafficWaiting},
                                 {"unreachableReported", value.navigation.unreachableReported},
                                 {"waypointIndex", static_cast<std::int64_t>(value.navigation.waypointIndex)},
                                 {"waypoints", Value(std::move(waypoints))}})},
            {"occupants", refsValue(value.occupants)},
            {"orders", ordersValue(value.orders, value.orderTargets)},
            {"shield", Value(Value::Object{{"capacity", value.shield.capacity},
                                           {"cooldown", value.shield.cooldown},
                                           {"regenDelay", value.shield.regenDelay},
                                           {"regenRate", value.shield.regenRate},
                                           {"value", value.shield.value}})},
            {"subject", value.subject.format()},
            {"tags", stringsValue(value.tags)},
            {"supply", Value(Value::Object{{"assignedTarget", value.supplyTarget.format()},
                                           {"autoDispatch", value.supply.autoDispatch},
                                           {"autoThreshold", value.supply.autoThreshold},
                                           {"capacity", value.supply.capacity},
                                           {"priority", value.supply.priority},
                                           {"convoyIndex", static_cast<std::int64_t>(value.supply.convoyIndex)},
                                           {"convoyLeader", value.convoyLeader.format()},
                                           {"convoyWaiting", value.supply.convoyWaiting},
                                           {"range", value.supply.range},
                                           {"relay", value.supply.relayEnabled},
                                           {"rendezvousActive", value.supply.rendezvousActive},
                                           {"rendezvousAvoidedThreat", value.supply.rendezvousAvoidedThreat},
                                           {"rendezvousPoint", positionValue(value.supply.rendezvousPoint)},
                                           {"rendezvousThreat", value.supply.rendezvousThreat},
                                           {"routeAvoidedThreat", value.supply.routeAvoidedThreat},
                                           {"routeThreat", value.supply.routeThreat},
                                           {"reserved", value.supply.reservedStock},
                                           {"returning", value.supply.returning},
                                           {"returnPoint", positionValue(value.supply.returnPoint)},
                                           {"stock", value.supply.stock},
                                           {"transferProgress", value.supply.transferProgress},
                                           {"transferRate", value.supply.transferRate}})},
            {"tactics", Value(Value::Object{
                            {"combatGroup", static_cast<std::int64_t>(value.tactics.combatGroup)},
                            {"coordinatedVolleyInterval", value.tactics.coordinatedVolleyInterval},
                            {"escortOffset", positionValue({value.tactics.escortOffsetX, value.tactics.escortOffsetY})},
                            {"escortTarget", value.escortTarget.format()},
                            {"fireControlEffectiveness", value.tactics.fireControlEffectiveness},
                            {"guard", positionValue({value.tactics.guardX, value.tactics.guardY})},
                            {"guardSet", value.tactics.guardSet},
                            {"protectionRange", value.tactics.protectionRange},
                            {"escortScreenSector", value.tactics.escortScreenSector},
                            {"escortSectorMatched", value.tactics.escortSectorMatched},
                            {"escortReinforcing", value.tactics.escortReinforcing},
                            {"escortReinforcementSector", value.tactics.escortReinforcementSector},
                            {"escortRearGuard", value.tactics.escortRearGuard},
                            {"escortInterceptTarget", value.tactics.escortInterceptTarget.format()},
                            {"escortHandoffCount", static_cast<std::int64_t>(value.tactics.escortHandoffCount)},
                            {"retreatCoverElapsed", value.tactics.retreatCoverElapsed},
                            {"retreatCovering", value.tactics.retreatCovering},
                            {"retreatFireTeam", value.tactics.retreatFireTeam},
                            {"threatSector", value.tactics.threatSector},
                            {"volleyHolding", value.tactics.volleyHolding},
                            {"volleyReleaseRemaining", value.tactics.volleyReleaseRemaining}})},
            {"technology", stringsValue(value.technology.applied)},
            {"veterancy", Value(Value::Object{{"eliteDamage", value.veterancy.eliteDamageFactor},
                                              {"eliteHealth", value.veterancy.eliteHealthFactor},
                                              {"eliteThreshold", value.veterancy.eliteThreshold},
                                              {"experience", value.veterancy.experience},
                                              {"level", value.veterancy.level},
                                              {"veteranDamage", value.veterancy.veteranDamageFactor},
                                              {"veteranHealth", value.veterancy.veteranHealthFactor},
                                              {"veteranThreshold", value.veterancy.veteranThreshold}})},
            {"vision", Value(Value::Object{{"cloaked", value.vision.cloaked},
                                           {"detectionRange", value.vision.detectionRange},
                                           {"detectionStrength", value.vision.detectionStrength},
                                           {"enabled", value.vision.enabled},
                                           {"jammingRange", value.vision.jammingRange},
                                           {"radarRange", value.vision.radarRange},
                                           {"radarResolution", value.vision.radarResolution},
                                           {"sightRange", value.vision.sightRange},
                                           {"stealth", value.vision.stealth}})},
            {"worker", Value(Value::Object{{"auto", value.worker.autoAssign},
                                           {"buildRate", value.worker.buildRate},
                                           {"capacity", value.worker.capacity},
                                           {"cargo", value.worker.cargo},
                                           {"dropoff", value.worker.dropoff.format()},
                                           {"gatherRate", value.worker.gatherRate},
                                           {"repairRate", value.worker.repairRate},
                                           {"resource", value.worker.resourceType},
                                           {"resourceNode", value.worker.resourceNode.format()}})}});
    }

    Value::Array buildings;
    for (const auto& value : state.buildings)
        buildings.emplace_back(Value::Object{
            {"builders", refsValue(value.builders)},
            {"capturingFaction", value.capturingFaction.format()},
            {"capture", Value(Value::Object{{"blockedByGarrison", value.capture.blockedByGarrison},
                                            {"capturable", value.capture.capturable},
                                            {"duration", value.capture.durationSeconds},
                                            {"progress", value.capture.progress}})},
            {"combat", Value(Value::Object{
                           {"acquisition", value.combat.acquisitionRange},
                           {"airDefenseNetworkRange", value.combat.airDefenseNetworkRange},
                           {"airDefenseNetworkRoot", value.airDefenseNetworkRoot.format()},
                           {"airDefenseNetworkSize", static_cast<std::int64_t>(value.combat.airDefenseNetworkSize)},
                           {"aimTolerance", value.combat.aimToleranceDegrees},
                           {"engagement", value.combat.engagementRange},
                           {"firingHeight", value.combat.firingHeight},
                           {"targetHeight", value.combat.targetHeight},
                           {"target", value.combatTarget.format()},
                           {"targetPriorities", prioritiesValue(value.combat.targetPriorities)},
                           {"shotSequence", static_cast<std::int64_t>(value.combat.shotSequence)},
                           {"turnRate", value.combat.turnRateDegrees}})},
            {"command", Value(Value::Object{{"active", value.command.active},
                                            {"capacity", value.command.capacity},
                                            {"jammed", value.command.jammed},
                                            {"jammingRange", value.command.jammingRange},
                                            {"load", value.command.load},
                                            {"range", value.command.range}})},
            {"construction", Value(Value::Object{{"buildTime", value.construction.buildTimeSeconds},
                                                 {"paused", value.construction.paused},
                                                 {"progress", value.construction.progress}})},
            {"definition", value.definition.format()},
            {"displayName", value.displayName},
            {"effects", effectsValue(value.effects)},
            {"faction", value.faction.format()},
            {"dropoff", Value(Value::Object{{"accepted", stringsValue(value.dropoff.acceptedResources)},
                                            {"radius", value.dropoff.radius}})},
            {"garrison", Value(Value::Object{{"capacity", static_cast<std::int64_t>(value.garrison.capacity)},
                                             {"damageBonus", value.garrison.damageBonusPerOccupant}})},
            {"indirectFire",
             Value(Value::Object{{"position", positionValue(value.indirectFire.lastFirePosition)},
                                 {"tick", static_cast<std::int64_t>(value.indirectFire.lastFireTick.value())}})},
            {"infrastructure", Value(Value::Object{{"buildInfluence", value.infrastructure.buildInfluenceRadius},
                                                   {"incomeProgress", value.infrastructure.incomeProgress},
                                                   {"incomeRate", value.infrastructure.incomeRate},
                                                   {"incomeResource", value.infrastructure.incomeResource},
                                                   {"powerConsumed", value.infrastructure.powerConsumed},
                                                   {"powerPriority", value.infrastructure.powerPriority},
                                                   {"powerProduced", value.infrastructure.powerProduced},
                                                   {"powered", value.infrastructure.powered}})},
            {"integrity", Value(Value::Object{{"alive", value.integrity.alive},
                                              {"health", value.integrity.state.health},
                                              {"maxHealth", value.integrity.state.maxHealth},
                                              {"maxPoise", value.integrity.state.maxPoise},
                                              {"poise", value.integrity.state.poise},
                                              {"repairCost", value.integrity.repairCostPerHealth},
                                              {"repairRemainder", value.integrity.repairCostRemainder},
                                              {"repairResource", value.integrity.repairResource}})},
            {"occupants", refsValue(value.occupants)},
            {"orders", ordersValue(value.orders, value.orderTargets)},
            {"placement", Value(Value::Object{{"cellX", value.placement.cellX},
                                              {"cellY", value.placement.cellY},
                                              {"placed", value.placement.placed},
                                              {"rotation", value.placement.rotation},
                                              {"worldX", value.placement.worldX},
                                              {"worldY", value.placement.worldY}})},
            {"production", value.productionJson},
            {"reinforcements", refsValue(value.reinforcements)},
            {"rally", Value(Value::Object{
                          {"combatGroup", static_cast<std::int64_t>(value.rally.combatGroup)},
                          {"blockedTask", value.rally.blockedProductionTask},
                          {"commandAppend", value.rally.command.append},
                          {"commandDefinition", value.rally.command.definitionId},
                          {"commandKind", static_cast<std::int64_t>(value.rally.command.kind)},
                          {"commandPriority", value.rally.command.priority},
                          {"commandRadius", value.rally.command.radius},
                          {"commandSecondary", positionValue(value.rally.command.secondaryTarget)},
                          {"commandTarget", positionValue(value.rally.command.target)},
                          {"commandTargetEntity", value.rallyCommandTarget.format()},
                          {"commandTimeout", value.rally.command.timeoutSeconds},
                          {"enabled", value.rally.enabled},
                          {"minimumLoad", static_cast<std::int64_t>(value.rally.minimumTransportLoad)},
                          {"reinforcementCapped", value.rally.reinforcementCapped},
                          {"reinforcementCappedSeconds", value.rally.reinforcementCappedSeconds},
                          {"reinforcementAutoCancelDelay", value.rally.reinforcementAutoCancelDelay},
                          {"reinforcementFallbacks", stringMapValue(value.rally.reinforcementFallbacks)},
                          {"reinforcementLimit", static_cast<std::int64_t>(value.rally.reinforcementLimit)},
                          {"reinforcementPausedTask", value.rally.reinforcementPolicyPausedTask},
                          {"reinforcementTypeLimits", integerMapValue(value.rally.reinforcementTypeLimits)},
                          {"reinforcementTypePriorities", integerMapValue(value.rally.reinforcementTypePriorities)},
                          {"spawnBlocked", value.rally.productionSpawnBlocked},
                          {"settledTasks", stringsValue(value.rally.settledProductionTasks)},
                          {"transport", value.rallyTransport.format()},
                          {"transportActive", value.rally.transportActive}})},
            {"shield", Value(Value::Object{{"capacity", value.shield.capacity},
                                           {"cooldown", value.shield.cooldown},
                                           {"regenDelay", value.shield.regenDelay},
                                           {"regenRate", value.shield.regenRate},
                                           {"value", value.shield.value}})},
            {"subject", value.subject.format()},
            {"tags", stringsValue(value.tags)},
            {"supply",
             Value(Value::Object{{"capacity", value.supply.capacity},
                                 {"productionCost", static_cast<std::int64_t>(value.supply.productionCostPerRound)},
                                 {"productionProgress", value.supply.productionProgress},
                                 {"productionRate", value.supply.productionRate},
                                 {"productionResource", value.supply.productionResource},
                                 {"range", value.supply.range},
                                 {"stock", value.supply.stock},
                                 {"transferProgress", value.supply.transferProgress},
                                 {"transferRate", value.supply.transferRate}})},
            {"technology", stringsValue(value.technology.applied)},
            {"vision", Value(Value::Object{{"detectionRange", value.vision.detectionRange},
                                           {"detectionStrength", value.vision.detectionStrength},
                                           {"enabled", value.vision.enabled},
                                           {"jammingRange", value.vision.jammingRange},
                                           {"radarRange", value.vision.radarRange},
                                           {"radarResolution", value.vision.radarResolution},
                                           {"sightRange", value.vision.sightRange}})}});

    Value::Array nodes;
    for (const auto& value : state.resourceNodes)
        nodes.emplace_back(Value::Object{{"capacity", static_cast<std::int64_t>(value.workerCapacity)},
                                         {"displayName", value.displayName},
                                         {"infinite", value.stock.infinite},
                                         {"maximum", value.stock.maximum},
                                         {"position", positionValue({value.position.x, value.position.y})},
                                         {"radius", value.position.radius},
                                         {"remaining", value.stock.remaining},
                                         {"resource", value.stock.resourceType},
                                         {"subject", value.subject.format()},
                                         {"workers", refsValue(value.workers)}});

    Value::Array players;
    for (const auto& value : state.players)
        players.emplace_back(Value::Object{{"buildings", refsValue(value.buildings)},
                                           {"displayName", value.displayName},
                                           {"subject", value.subject.format()},
                                           {"units", refsValue(value.units)}});

    Value::Array factions;
    for (const auto& value : state.factions) {
        Value::Array contacts;
        for (const auto& contact : value.intel.contacts)
            contacts.emplace_back(Value::Object{{"age", contact.ageSeconds},
                                                {"detected", contact.detected},
                                                {"kind", contact.kind},
                                                {"position", positionValue(contact.position)},
                                                {"subject", contact.subject.format()},
                                                {"visible", contact.visible}});
        Value::Object productionReserves;
        for (const auto& [resource, reserve] : value.productionPolicy.resourceReserves)
            productionReserves.emplace(resource, Value(Value::Object{{"amount", reserve.amount},
                                                                     {"minimumPriority", reserve.minimumPriority}}));
        Value::Object unlockedDefinitions;
        for (const auto& [upgrade, definition] : value.technology.unlockedDefinitions)
            unlockedDefinitions.emplace(upgrade, Value(definition));
        factions.emplace_back(Value::Object{
            {"buildings", refsValue(value.buildings)},
            {"contacts", Value(std::move(contacts))},
            {"displayName", value.displayName},
            {"intelEnabled", value.intel.enabled},
            {"strategy",
             Value(Value::Object{{"attackThreshold", value.strategy.attackThreshold},
                                 {"armyDefinition", value.strategy.armyDefinition.format()},
                                 {"enabled", value.strategy.enabled},
                                 {"formationSpacing", value.strategy.formationSpacing},
                                 {"thinkAccumulator", value.strategy.thinkAccumulator},
                                 {"thinkInterval", value.strategy.thinkInterval},
                                 {"targetBuildingDefinition", value.strategy.targetBuildingDefinition.format()},
                                 {"workerDefinition", value.strategy.workerDefinition.format()},
                                 {"workers", value.strategy.desiredWorkers}})},
            {"subject", value.subject.format()},
            {"technology", Value(Value::Object{{"consumed", stringsValue(value.technology.consumedTasks)},
                                               {"definitions", Value(std::move(unlockedDefinitions))},
                                               {"unlocked", stringsValue(value.technology.unlocked)}})},
            {"productionPolicy", Value(Value::Object{{"resourceReserves", Value(std::move(productionReserves))}})},
            {"units", refsValue(value.units)},
            {"workforce",
             Value(Value::Object{{"autoConstruction", value.workforce.autoConstruction},
                                 {"autoRepair", value.workforce.autoRepair},
                                 {"builders", static_cast<std::int64_t>(value.workforce.maxBuildersPerSite)},
                                 {"repairers", static_cast<std::int64_t>(value.workforce.maxRepairersPerBuilding)},
                                 {"reserve", static_cast<std::int64_t>(value.workforce.reserveWorkers)}})}});
    }

    Value::Array matches;
    for (const auto& value : state.matches) {
        Value::Array participants;
        for (const auto& participant : value.participants)
            participants.emplace_back(Value::Object{{"eliminated", participant.eliminated},
                                                    {"faction", participant.faction.format()},
                                                    {"reason", participant.reason},
                                                    {"surrendered", participant.surrendered},
                                                    {"team", participant.team}});
        Value::Array events;
        for (const auto& event : value.events.values)
            events.emplace_back(Value::Object{{"faction", event.faction.format()},
                                              {"kind", event.kind},
                                              {"reason", event.reason},
                                              {"sequence", static_cast<std::int64_t>(event.sequence)},
                                              {"team", event.team}});
        matches.emplace_back(Value::Object{{"events", Value(std::move(events))},
                                           {"participants", Value(std::move(participants))},
                                           {"phase", static_cast<std::int64_t>(value.state.phase)},
                                           {"rule", static_cast<std::int64_t>(value.rules.rule)},
                                           {"ruleArchetype", value.rules.archetype},
                                           {"sequence", static_cast<std::int64_t>(value.state.updateSequence)},
                                           {"subject", value.subject.format()},
                                           {"target", value.rules.targetValue},
                                           {"winningTeam", value.state.winningTeam}});
    }
    Value::Array movementGroups;
    for (const auto& group : state.movementGroups) {
        Value::Array members;
        for (const auto& member : group.members)
            members.emplace_back(Value::Object{{"subject", member.subject.format()}, {"orderId", member.orderId}});
        movementGroups.emplace_back(
            Value::Object{{"leadDistance", group.leadDistance}, {"members", Value(std::move(members))}});
    }
    Value root(Value::Object{{"movementGroups", Value(std::move(movementGroups))},
                             {"buildings", Value(std::move(buildings))},
                             {"factions", Value(std::move(factions))},
                             {"matches", Value(std::move(matches))},
                             {"players", Value(std::move(players))},
                             {"projectiles", Value(Value::Object{{"payloads", Value(std::move(projectilePayloads))},
                                                                 {"slots", Value(std::move(projectileSlots))}})},
                             {"resourceNodes", Value(std::move(nodes))},
                             {"units", Value(std::move(units))},
                             {"version", static_cast<std::int64_t>(state.version)}});
    return root.toJson();
}

Result<ContentId> RTS::stateHash(const SnapshotHashProvider& hashProvider) const {
    if (!hashProvider)
        return Result<ContentId>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "RTS state hash provider is required", "hashProvider"));
    auto canonical = canonicalStateJson();
    if (!canonical) return Result<ContentId>::failure(canonical.status());
    return hashProvider(canonical.value());
}

}  // namespace eve::rts
