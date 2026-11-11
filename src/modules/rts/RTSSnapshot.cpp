#include "rts/RTS.h"
#include "rts/RTSSnapshotInternal.h"

#include "common/Value.h"

#include <algorithm>
#include <optional>
#include <unordered_set>

namespace eve::rts {
Result<ecs::EntityHandle> RTS::resolveOwnedSubject(SubjectRef subject) const {
    using R = Result<ecs::EntityHandle>;
    if (!subject.isValid())
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Root subject is invalid"));
    if (!ownedWorld_)
        return R::failure(Diagnostic::error(DiagnosticCode::Unsupported, "Root resolution needs owned Table"));
    if (ownedWorld_->is_deferred())
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Root resolution needs idle Table"));
    ecs::ScopedTable  scope(*ownedWorld_);
    ecs::EntityHandle result;
    for (const auto* handles : {&units_, &buildings_, &resourceNodes_, &players_, &factions_, &matches_})
        for (auto handle : *handles) {
            if (handle.table != ownedWorld_.get())
                return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Foreign owned root Table"));
            if (snapshot_internal::subjectOf(handle) != subject) continue;
            if (result.table)
                return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Ambiguous owned root identity"));
            result = handle;
        }
    if (!result.table) return R::failure(Diagnostic::error(DiagnosticCode::NotFound, "Owned root identity absent"));
    return R::success(result);
}
Result<RTSCombatSnapshot> RTS::captureCombatState() const {
    auto* world = ownedWorld_ ? ownedWorld_.get() : ecs::current();
    if (!world || world->is_deferred())
        return Result<RTSCombatSnapshot>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "RTS combat capture needs an idle actual world", "rts.combat"));
    return Result<RTSCombatSnapshot>::success({combatState_.mirroredSubjects, combatState_.blockedSubjects});
}
Result<RTSFrameState> RTS::captureFrameState() const {
    auto* world = ownedWorld_ ? ownedWorld_.get() : ecs::current();
    if (!world || world->is_deferred())
        return Result<RTSFrameState>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "RTS event capture needs an idle actual world", "rts.frame"));
    return Result<RTSFrameState>::success({frameDamageEvents_, frameCombatEvents_, frameLifecycleEvents_,
                                           frameEventSequence_, preserveFrameEventsOnNextStep_, gameplayEvents_,
                                           nextGameplayEventSequence_});
}
namespace {

using snapshot_internal::subjectOf;

template <class... Components, class Entity>
std::string_view missingComponent(Entity& entity) noexcept {
    std::string_view missing;
    (void)((detail::existingComponent<Components>(entity) ? false : (missing = typeid(Components).name(), true)) ||
           ...);
    return missing;
}


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
    auto* currentTable = ownedWorld_ ? ownedWorld_.get() : ecs::current();
    if (!currentTable || currentTable->is_deferred())
        return Result<RTSStateSnapshot>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "RTS snapshot requires its idle owning world outside Views"));
    ecs::ScopedTable scope(*currentTable);
    for (const auto& handle : units_) {
        if (handle.table != currentTable) {
            if (ownedWorld_)
                return Result<RTSStateSnapshot>::failure(Diagnostic::error(
                    DiagnosticCode::StaleHandle, "Owned RTS root belongs to another world", "units_"));
            continue;
        }
        auto* root = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (!root) continue;
        const auto missing =
            missingComponent<Unit::Abilities, Unit::Artillery, Unit::Capture, Unit::Combat, Unit::Command,
                             Unit::Containment, Unit::Definition, Unit::Durability, Unit::Effects, Unit::Faction,
                             Unit::Identity, Unit::Morale, Unit::Motion, Unit::Navigation, Unit::Orders, Unit::Shield,
                             Unit::Supply, Unit::Tactics, Unit::Tags, Unit::Technology, Unit::Veterancy, Unit::Vision,
                             Unit::Worker, Unit::Attributes>(*root);
        if (!missing.empty())
            return Result<RTSStateSnapshot>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS snapshot root component is missing: " + std::string(missing), "units_"));
        if (!detail::existingComponent<Unit::Identity>(*root)->subject.isValid())
            return Result<RTSStateSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "RTS snapshot root has no stable identity", "units_"));
    }
    for (const auto& handle : buildings_) {
        if (handle.table != currentTable) {
            if (ownedWorld_)
                return Result<RTSStateSnapshot>::failure(Diagnostic::error(
                    DiagnosticCode::StaleHandle, "Owned RTS root belongs to another world", "buildings_"));
            continue;
        }
        auto* root = dynamic_cast<Building*>(ecs::try_get(handle));
        if (!root) continue;
        const auto missing =
            missingComponent<Building::Capture, Building::Combat, Building::Command, Building::Construction,
                             Building::Definition, Building::Dropoff, Building::Effects, Building::Faction,
                             Building::Garrison, Building::Identity, Building::IndirectFire, Building::Infrastructure,
                             Building::Integrity, Building::Orders, Building::Placement, Building::Production,
                             Building::Rally, Building::Shield, Building::Supply, Building::Tags, Building::Technology,
                             Building::Vision>(*root);
        if (!missing.empty())
            return Result<RTSStateSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::NotFound,
                                  "RTS snapshot root component is missing: " + std::string(missing), "buildings_"));
        if (!detail::existingComponent<Building::Identity>(*root)->subject.isValid())
            return Result<RTSStateSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "RTS snapshot root has no stable identity", "buildings_"));
    }
    for (const auto& handle : resourceNodes_) {
        if (handle.table != currentTable) {
            if (ownedWorld_)
                return Result<RTSStateSnapshot>::failure(Diagnostic::error(
                    DiagnosticCode::StaleHandle, "Owned RTS root belongs to another world", "resourceNodes_"));
            continue;
        }
        auto* root = dynamic_cast<ResourceNode*>(ecs::try_get(handle));
        if (!root) continue;
        const auto missing = missingComponent<ResourceNode::Harvest, ResourceNode::Identity, ResourceNode::Position,
                                              ResourceNode::Stock>(*root);
        if (!missing.empty())
            return Result<RTSStateSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::NotFound,
                                  "RTS snapshot root component is missing: " + std::string(missing), "resourceNodes_"));
        if (!detail::existingComponent<ResourceNode::Identity>(*root)->subject.isValid())
            return Result<RTSStateSnapshot>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS snapshot root has no stable identity", "resourceNodes_"));
    }
    for (const auto& handle : players_) {
        if (handle.table != currentTable) {
            if (ownedWorld_)
                return Result<RTSStateSnapshot>::failure(Diagnostic::error(
                    DiagnosticCode::StaleHandle, "Owned RTS root belongs to another world", "players_"));
            continue;
        }
        auto* root = dynamic_cast<Player*>(ecs::try_get(handle));
        if (!root) continue;
        const auto missing = missingComponent<Player::Identity, Player::Selection>(*root);
        if (!missing.empty())
            return Result<RTSStateSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::NotFound,
                                  "RTS snapshot root component is missing: " + std::string(missing), "players_"));
        if (!detail::existingComponent<Player::Identity>(*root)->subject.isValid())
            return Result<RTSStateSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "RTS snapshot root has no stable identity", "players_"));
    }
    for (const auto& handle : factions_) {
        if (handle.table != currentTable) {
            if (ownedWorld_)
                return Result<RTSStateSnapshot>::failure(Diagnostic::error(
                    DiagnosticCode::StaleHandle, "Owned RTS root belongs to another world", "factions_"));
            continue;
        }
        auto* root = dynamic_cast<Faction*>(ecs::try_get(handle));
        if (!root) continue;
        const auto missing =
            missingComponent<Faction::Identity, Faction::Intel, Faction::Members, Faction::ProductionPolicy,
                             Faction::Strategy, Faction::Technology, Faction::Workforce>(*root);
        if (!missing.empty())
            return Result<RTSStateSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::NotFound,
                                  "RTS snapshot root component is missing: " + std::string(missing), "factions_"));
        if (!detail::existingComponent<Faction::Identity>(*root)->subject.isValid())
            return Result<RTSStateSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "RTS snapshot root has no stable identity", "factions_"));
    }
    for (const auto& handle : matches_) {
        if (handle.table != currentTable) {
            if (ownedWorld_)
                return Result<RTSStateSnapshot>::failure(Diagnostic::error(
                    DiagnosticCode::StaleHandle, "Owned RTS root belongs to another world", "matches_"));
            continue;
        }
        auto* root = dynamic_cast<Match*>(ecs::try_get(handle));
        if (!root) continue;
        const auto missing =
            missingComponent<Match::Events, Match::Identity, Match::Participants, Match::Rules, Match::State>(*root);
        if (!missing.empty())
            return Result<RTSStateSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::NotFound,
                                  "RTS snapshot root component is missing: " + std::string(missing), "matches_"));
        if (!detail::existingComponent<Match::Identity>(*root)->subject.isValid())
            return Result<RTSStateSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "RTS snapshot root has no stable identity", "matches_"));
    }
    RTSStateSnapshot result;
    auto             groups = captureMovementGroups();
    if (!groups) return Result<RTSStateSnapshot>::failure(groups.status());
    result.movementGroups = std::move(groups).takeValue();
    for (const auto& handle : units_) {
        if (handle.table != currentTable) continue;
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (unit == nullptr) continue;
        RTSUnitSnapshot value;
        value.subject     = detail::existingComponent<Unit::Identity>(*unit)->subject;
        value.definition  = detail::existingComponent<Unit::Definition>(*unit)->id;
        value.displayName = detail::existingComponent<Unit::Identity>(*unit)->displayName;
        auto attributes   = RTSUnitAttributeAdapter::snapshot(*unit);
        if (!attributes) return Result<RTSStateSnapshot>::failure(attributes.status());
        value.attributes       = std::move(attributes).takeValue();
        value.attributes.owner = {};
        value.tags             = detail::existingComponent<Unit::Tags>(*unit)->values.values();
        value.effects          = detail::existingComponent<Unit::Effects>(*unit)->values.snapshot();
        value.faction          = subjectOf(detail::existingComponent<Unit::Faction>(*unit)->link.resolve());
        value.motion           = *detail::existingComponent<Unit::Motion>(*unit);
        value.navigation       = *detail::existingComponent<Unit::Navigation>(*unit);
        value.navigation.formationSpeedFactor = 1.f;
        value.navigation.formationTarget.reset();
        value.navigation.combatSlotTarget.reset();
        value.navigation.combatSlotWaiting = false;
        if (value.navigation.trafficRecoveryTarget) value.navigation.plannedOrderId.clear();
        value.navigation.trafficRecoveryTarget.reset();
        value.vision         = *detail::existingComponent<Unit::Vision>(*unit);
        value.worker         = {detail::existingComponent<Unit::Worker>(*unit)->resourceType,
                                subjectOf(detail::existingComponent<Unit::Worker>(*unit)->resourceNode.resolve()),
                                subjectOf(detail::existingComponent<Unit::Worker>(*unit)->dropoff.resolve()),
                                detail::existingComponent<Unit::Worker>(*unit)->cargo,
                                detail::existingComponent<Unit::Worker>(*unit)->capacity,
                                detail::existingComponent<Unit::Worker>(*unit)->gatherRate,
                                detail::existingComponent<Unit::Worker>(*unit)->buildRate,
                                detail::existingComponent<Unit::Worker>(*unit)->repairRate,
                                detail::existingComponent<Unit::Worker>(*unit)->autoAssign};
        value.combat         = *detail::existingComponent<Unit::Combat>(*unit);
        value.combatTarget   = subjectOf(value.combat.target);
        value.combat.target  = {};
        value.durability     = *detail::existingComponent<Unit::Durability>(*unit);
        value.shield         = *detail::existingComponent<Unit::Shield>(*unit);
        value.veterancy      = *detail::existingComponent<Unit::Veterancy>(*unit);
        value.command        = *detail::existingComponent<Unit::Command>(*unit);
        value.commandSource  = subjectOf(value.command.source);
        value.commandUplink  = subjectOf(value.command.uplink);
        value.command.source = {};
        value.command.uplink = {};
        value.abilities      = *detail::existingComponent<Unit::Abilities>(*unit);
        if (value.abilities.channel) {
            value.abilityTarget             = subjectOf(value.abilities.channel->target);
            value.abilities.channel->target = {};
        }
        value.capture             = *detail::existingComponent<Unit::Capture>(*unit);
        value.containmentCapacity = detail::existingComponent<Unit::Containment>(*unit)->capacity;
        value.container           = subjectOf(detail::existingComponent<Unit::Containment>(*unit)->container.resolve());
        value.occupants           = subjectsOf(detail::existingComponent<Unit::Containment>(*unit)->occupants);
        value.supply              = *detail::existingComponent<Unit::Supply>(*unit);
        value.supplyTarget        = subjectOf(value.supply.assignedTarget);
        value.convoyLeader        = subjectOf(value.supply.convoyLeader);
        value.supply.assignedTarget          = {};
        value.supply.convoyLeader            = {};
        value.morale                         = *detail::existingComponent<Unit::Morale>(*unit);
        value.artillery                      = *detail::existingComponent<Unit::Artillery>(*unit);
        value.fireSupportRequester           = subjectOf(value.artillery.fireSupportRequester);
        value.observedFireSpotter            = subjectOf(value.artillery.observedFireSpotter);
        value.artillery.fireSupportRequester = {};
        value.artillery.observedFireSpotter  = {};
        value.tactics                        = *detail::existingComponent<Unit::Tactics>(*unit);
        value.escortTarget                   = subjectOf(value.tactics.escortTarget);
        value.tactics.escortTarget           = {};
        value.technology                     = *detail::existingComponent<Unit::Technology>(*unit);
        auto orders                          = detail::existingComponent<Unit::Orders>(*unit)->values.snapshotState();
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
        value.subject     = detail::existingComponent<Building::Identity>(*building)->subject;
        value.definition  = detail::existingComponent<Building::Definition>(*building)->id;
        value.displayName = detail::existingComponent<Building::Identity>(*building)->displayName;
        value.tags        = detail::existingComponent<Building::Tags>(*building)->values.values();
        value.effects     = detail::existingComponent<Building::Effects>(*building)->values.snapshot();
        value.faction     = subjectOf(detail::existingComponent<Building::Faction>(*building)->link.resolve());
        value.placement   = *detail::existingComponent<Building::Placement>(*building);
        value.placement.link.reset();  // External provider binding belongs to the receiving composition.
        value.construction = *detail::existingComponent<Building::Construction>(*building);
        value.builders     = subjectsOf(value.construction.builders);
        value.construction.builders.clear();
        value.integrity                  = *detail::existingComponent<Building::Integrity>(*building);
        value.shield                     = *detail::existingComponent<Building::Shield>(*building);
        value.capture                    = *detail::existingComponent<Building::Capture>(*building);
        value.capturingFaction           = subjectOf(value.capture.capturingFaction);
        value.capture.capturingFaction   = {};
        value.dropoff                    = *detail::existingComponent<Building::Dropoff>(*building);
        value.rally                      = *detail::existingComponent<Building::Rally>(*building);
        value.rallyCommandTarget         = subjectOf(value.rally.command.targetEntity);
        value.rallyTransport             = subjectOf(value.rally.transport);
        value.reinforcements             = subjectsOf(value.rally.reinforcements);
        value.rally.transport            = {};
        value.rally.command.targetEntity = {};
        value.rally.reinforcements.clear();
        value.combat                       = *detail::existingComponent<Building::Combat>(*building);
        value.combatTarget                 = subjectOf(value.combat.target);
        value.airDefenseNetworkRoot        = subjectOf(value.combat.airDefenseNetworkRoot);
        value.combat.target                = {};
        value.combat.airDefenseNetworkRoot = {};
        value.garrison                     = *detail::existingComponent<Building::Garrison>(*building);
        value.occupants                    = subjectsOf(value.garrison.occupants);
        value.garrison.occupants.clear();
        value.supply         = *detail::existingComponent<Building::Supply>(*building);
        value.vision         = *detail::existingComponent<Building::Vision>(*building);
        value.technology     = *detail::existingComponent<Building::Technology>(*building);
        value.infrastructure = *detail::existingComponent<Building::Infrastructure>(*building);
        value.command        = *detail::existingComponent<Building::Command>(*building);
        value.indirectFire   = *detail::existingComponent<Building::IndirectFire>(*building);
        auto production      = detail::existingComponent<Building::Production>(*building)->values.snapshot();
        if (!production)
            return Result<RTSStateSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "failed to snapshot RTS production", "buildings.production"));
        value.productionJson = std::move(production).takeValue();
        auto orders          = detail::existingComponent<Building::Orders>(*building)->values.snapshotState();
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
        result.resourceNodes.push_back({detail::existingComponent<ResourceNode::Identity>(*node)->subject,
                                        detail::existingComponent<ResourceNode::Identity>(*node)->displayName,
                                        *detail::existingComponent<ResourceNode::Position>(*node),
                                        *detail::existingComponent<ResourceNode::Stock>(*node),
                                        detail::existingComponent<ResourceNode::Harvest>(*node)->capacity,
                                        subjectsOf(detail::existingComponent<ResourceNode::Harvest>(*node)->workers)});
    }
    for (const auto& handle : players_) {
        if (handle.table != currentTable) continue;
        auto* player = dynamic_cast<Player*>(ecs::try_get(handle));
        if (player == nullptr) continue;
        result.players.push_back({detail::existingComponent<Player::Identity>(*player)->subject,
                                  detail::existingComponent<Player::Identity>(*player)->displayName,
                                  subjectsOf(detail::existingComponent<Player::Selection>(*player)->units),
                                  subjectsOf(detail::existingComponent<Player::Selection>(*player)->buildings),
                                  detail::existingComponent<Player::Selection>(*player)->revision,
                                  detail::existingComponent<Player::Selection>(*player)->tick});
    }
    for (const auto& handle : factions_) {
        if (handle.table != currentTable) continue;
        auto* faction = dynamic_cast<Faction*>(ecs::try_get(handle));
        if (faction == nullptr) continue;
        result.factions.push_back({detail::existingComponent<Faction::Identity>(*faction)->subject,
                                   detail::existingComponent<Faction::Identity>(*faction)->displayName,
                                   subjectsOf(detail::existingComponent<Faction::Members>(*faction)->units),
                                   subjectsOf(detail::existingComponent<Faction::Members>(*faction)->buildings),
                                   *detail::existingComponent<Faction::Strategy>(*faction),
                                   *detail::existingComponent<Faction::Workforce>(*faction),
                                   *detail::existingComponent<Faction::ProductionPolicy>(*faction),
                                   *detail::existingComponent<Faction::Intel>(*faction),
                                   *detail::existingComponent<Faction::Technology>(*faction)});
    }
    for (const auto& handle : matches_) {
        if (handle.table != currentTable) continue;
        auto* match = dynamic_cast<Match*>(ecs::try_get(handle));
        if (match == nullptr) continue;
        RTSMatchSnapshot value;
        value.subject = detail::existingComponent<Match::Identity>(*match)->subject;
        value.rules   = *detail::existingComponent<Match::Rules>(*match);
        value.state   = *detail::existingComponent<Match::State>(*match);
        value.events  = *detail::existingComponent<Match::Events>(*match);
        for (const auto& participant : detail::existingComponent<Match::Participants>(*match)->entries)
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
                                                      {"observer", payload.observer.format()},
                                                      {"trajectoryTarget", payload.trajectoryTarget.format()},
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
                                           {"selectionRevision", std::to_string(value.selectionRevision)},
                                           {"selectionTick", std::to_string(value.selectionTick.value())},
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
    Value root(Value::Object{
        {"movementGroups", Value(std::move(movementGroups))},
        {"buildings", Value(std::move(buildings))},
        {"factions", Value(std::move(factions))},
        {"matches", Value(std::move(matches))},
        {"players", Value(std::move(players))},
        {"projectiles", Value(Value::Object{{"capacity", std::int64_t(state.projectiles.runtime.capacity)},
                                            {"payloads", Value(std::move(projectilePayloads))},
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

// RTS state restoration (previously RTSRestore.cpp).
#include <algorithm>
#include <unordered_set>
#include "rts/RTS.h"
#include "rts/RTSSnapshotInternal.h"

namespace eve::rts {
using snapshot_internal::subjectOf;
Result<void> RTS::restoreState(const RTSStateSnapshot& snapshot) {
    if (snapshot.version < 3 && std::any_of(snapshot.players.begin(), snapshot.players.end(), [](const auto& p) {
            return p.selectionRevision != 0 || p.selectionTick.value() != 0;
        }))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "Legacy RTS snapshots cannot carry v3 player watermarks", "players.selection"));

    if ((snapshot.version != 1 && snapshot.version != 2 && snapshot.version != 3) ||
        snapshot.units.size() != unitCount() || snapshot.buildings.size() != buildingCount() ||
        snapshot.resourceNodes.size() != resourceNodeCount() || snapshot.players.size() != playerCount() ||
        snapshot.factions.size() != factionCount() || snapshot.matches.size() != matchCount())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "RTS snapshot topology/version does not match this module", "snapshot"));

    auto resolve = [&](SubjectRef subject) -> ecs::Entity* {
        if (!subject.isValid()) return nullptr;
        if (auto* value = findUnit(subject)) return value;
        if (auto* value = findBuilding(subject)) return value;
        if (auto* value = findResourceNode(subject)) return value;
        for (const auto& handle : players_)
            if (subjectOf(handle) == subject) return ecs::try_get(handle);
        for (const auto& handle : factions_)
            if (subjectOf(handle) == subject) return ecs::try_get(handle);
        for (const auto& handle : matches_)
            if (subjectOf(handle) == subject) return ecs::try_get(handle);
        return nullptr;
    };
    auto findPlayer = [&](SubjectRef subject) -> Player* {
        for (const auto& value : players_)
            if (subjectOf(value) == subject) return dynamic_cast<Player*>(ecs::try_get(value));
        return nullptr;
    };
    auto findFaction = [&](SubjectRef subject) -> Faction* {
        for (const auto& value : factions_)
            if (subjectOf(value) == subject) return dynamic_cast<Faction*>(ecs::try_get(value));
        return nullptr;
    };
    auto findMatch = [&](SubjectRef subject) -> Match* {
        for (const auto& value : matches_)
            if (subjectOf(value) == subject) return dynamic_cast<Match*>(ecs::try_get(value));
        return nullptr;
    };
    std::unordered_set<SubjectRef> seen;
    auto                           requireUnique = [&](SubjectRef subject) {
        return subject.isValid() && resolve(subject) != nullptr && seen.insert(subject).second;
    };
    for (const auto& value : snapshot.units)
        if (!requireUnique(value.subject) || findUnit(value.subject) == nullptr)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "RTS snapshot contains an unknown/duplicate unit", "units.subject"));
    for (const auto& value : snapshot.buildings)
        if (!requireUnique(value.subject) || findBuilding(value.subject) == nullptr)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "RTS snapshot contains an unknown/duplicate building", "buildings.subject"));
    for (const auto& value : snapshot.resourceNodes)
        if (!requireUnique(value.subject) || findResourceNode(value.subject) == nullptr)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                           "RTS snapshot contains an unknown/duplicate resource node",
                                                           "resourceNodes.subject"));
    for (const auto& value : snapshot.players)
        if (!requireUnique(value.subject) || findPlayer(value.subject) == nullptr)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "RTS snapshot contains an unknown/duplicate player", "players.subject"));
    for (const auto& value : snapshot.factions)
        if (!requireUnique(value.subject) || findFaction(value.subject) == nullptr)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "RTS snapshot contains an unknown/duplicate faction", "factions.subject"));
    for (const auto& value : snapshot.matches)
        if (!requireUnique(value.subject) || findMatch(value.subject) == nullptr)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "RTS snapshot contains an unknown/duplicate match", "matches.subject"));

    auto validType = [&](SubjectRef subject, auto* tag) {
        using T = std::remove_pointer_t<decltype(tag)>;
        return !subject.isValid() || dynamic_cast<T*>(resolve(subject)) != nullptr;
    };
    auto validUnits = [&](const std::vector<SubjectRef>& subjects) {
        return std::all_of(subjects.begin(), subjects.end(),
                           [&](SubjectRef subject) { return validType(subject, static_cast<Unit*>(nullptr)); });
    };
    for (const auto& value : snapshot.units) {
        if (!validType(value.faction, static_cast<Faction*>(nullptr)) ||
            !validType(value.worker.resourceNode, static_cast<ResourceNode*>(nullptr)) ||
            !validType(value.worker.dropoff, static_cast<Building*>(nullptr)) ||
            (value.container.isValid() && !validType(value.container, static_cast<Unit*>(nullptr)) &&
             !validType(value.container, static_cast<Building*>(nullptr))) ||
            !validUnits(value.occupants) || !validType(value.supplyTarget, static_cast<Unit*>(nullptr)) ||
            !validType(value.fireSupportRequester, static_cast<Unit*>(nullptr)))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "RTS unit snapshot contains a relationship of the wrong type",
                "units.relationships"));
    }
    for (const auto& value : snapshot.buildings) {
        if (!validType(value.faction, static_cast<Faction*>(nullptr)) || !validUnits(value.builders) ||
            !validType(value.capturingFaction, static_cast<Faction*>(nullptr)) ||
            (value.rallyCommandTarget.isValid() && resolve(value.rallyCommandTarget) == nullptr) ||
            !validType(value.rallyTransport, static_cast<Unit*>(nullptr)) || !validUnits(value.reinforcements) ||
            !validUnits(value.occupants))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "RTS building snapshot contains a relationship of the wrong type",
                "buildings.relationships"));
    }
    for (const auto& value : snapshot.resourceNodes)
        if (!validUnits(value.workers))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "RTS resource snapshot contains a non-unit worker", "resourceNodes.workers"));
    for (const auto& value : snapshot.players)
        if (!validUnits(value.units) ||
            !std::all_of(value.buildings.begin(), value.buildings.end(),
                         [&](SubjectRef id) { return validType(id, static_cast<Building*>(nullptr)); }))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "RTS player snapshot contains an invalid selection", "players.selection"));
    for (const auto& value : snapshot.factions)
        if (!validUnits(value.units) ||
            !std::all_of(value.buildings.begin(), value.buildings.end(),
                         [&](SubjectRef id) { return validType(id, static_cast<Building*>(nullptr)); }))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "RTS faction snapshot contains invalid membership", "factions.members"));
    for (const auto& value : snapshot.matches)
        if (!std::all_of(value.participants.begin(), value.participants.end(), [&](const auto& participant) {
                return validType(participant.faction, static_cast<Faction*>(nullptr));
            }))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                           "RTS match snapshot contains an invalid participant",
                                                           "matches.participants"));

    RTSProjectileSystem stagedProjectiles;
    if (!snapshot.projectiles.runtime.slots.empty() || !snapshot.projectiles.payloads.empty()) {
        auto validProjectiles = stagedProjectiles.restore(snapshot.projectiles, resolve);
        if (!validProjectiles) return validProjectiles;
    }

    auto prepareOrders = [&](const OrderComponent::Snapshot&          source,
                             const std::map<std::string, SubjectRef>& targets) -> Result<OrderComponent::Snapshot> {
        auto candidate = source;
        for (const auto& [id, subject] : targets) {
            auto  found  = candidate.extended.find(id);
            auto* target = resolve(subject);
            if (found == candidate.extended.end() || target == nullptr)
                return Result<OrderComponent::Snapshot>::failure(Diagnostic::error(
                    DiagnosticCode::Conflict, "RTS snapshot order target cannot be rebound", "orders.target"));
            found->second.targetEntity = ecs::handle_of(target);
        }
        OrderComponent validator;
        auto           valid = validator.restoreState(candidate);
        if (!valid) return Result<OrderComponent::Snapshot>::failure(valid.status());
        return Result<OrderComponent::Snapshot>::success(std::move(candidate));
    };
    std::vector<OrderComponent::Snapshot> unitOrders;
    std::vector<OrderComponent::Snapshot> buildingOrders;
    unitOrders.reserve(snapshot.units.size());
    buildingOrders.reserve(snapshot.buildings.size());
    for (const auto& value : snapshot.units) {
        auto prepared = prepareOrders(value.orders, value.orderTargets);
        if (!prepared) return Result<void>::failure(prepared.status());
        auto* unit                            = findUnit(value.subject);
        auto  attributes                      = value.attributes;
        attributes.owner                      = unit->identity()->self;
        AttributeComponent attributeValidator = unit->attributes()->values;
        auto               currentAttributes  = RTSUnitAttributeAdapter::snapshot(*unit);
        if (!currentAttributes) return Result<void>::failure(currentAttributes.status());
        auto validAttributes = attributeValidator.restore(attributes, currentAttributes.value().capturedRevision);
        if (!validAttributes) return validAttributes;
        TagSet tagValidator;
        for (const auto& tag : value.tags) {
            auto validTag = tagValidator.add(tag);
            if (!validTag) return validTag;
        }
        RTSEffectAdapter effectValidator;
        auto             validEffects = effectValidator.restore(value.effects);
        if (!validEffects) return validEffects;
        unitOrders.push_back(std::move(prepared).takeValue());
    }
    for (const auto& value : snapshot.buildings) {
        auto prepared = prepareOrders(value.orders, value.orderTargets);
        if (!prepared) return Result<void>::failure(prepared.status());
        TagSet tagValidator;
        for (const auto& tag : value.tags) {
            auto validTag = tagValidator.add(tag);
            if (!validTag) return validTag;
        }
        RTSEffectAdapter effectValidator;
        auto             validEffects = effectValidator.restore(value.effects);
        if (!validEffects) return validEffects;
        ProductionComponent validator;
        auto                valid = validator.restore(value.productionJson);
        if (!valid) return valid;
        buildingOrders.push_back(std::move(prepared).takeValue());
    }

    auto groups = prepareMovementGroups(snapshot);
    if (!groups) return Result<void>::failure(groups.status());
    auto handle = [&](SubjectRef subject) {
        auto* entity = resolve(subject);
        return entity ? ecs::handle_of(entity) : ecs::EntityHandle{};
    };
    for (std::size_t index = 0; index < snapshot.units.size(); ++index) {
        const auto& value             = snapshot.units[index];
        Unit*       unit              = findUnit(value.subject);
        unit->definition()->id        = value.definition;
        unit->identity()->displayName = value.displayName;
        auto attributes               = value.attributes;
        attributes.owner              = unit->identity()->self;
        auto currentAttributes        = RTSUnitAttributeAdapter::snapshot(*unit);
        if (!currentAttributes) return Result<void>::failure(currentAttributes.status());
        auto restoredAttributes =
            RTSUnitAttributeAdapter::restore(*unit, attributes, currentAttributes.value().capturedRevision);
        if (!restoredAttributes) return restoredAttributes;
        unit->tags()->values = TagSet{};
        for (const auto& tag : value.tags) {
            auto added = unit->tags()->values.add(tag);
            if (!added) return added;
        }
        auto restoredEffects = unit->effects()->values.restore(value.effects);
        if (!restoredEffects) return restoredEffects;
        if (value.faction.isValid())
            unit->faction()->link = FactionLink::bind(handle(value.faction)).value();
        else
            unit->faction()->link = {};
        *unit->motion()                          = value.motion;
        *unit->navigation()                      = value.navigation;
        unit->navigation()->formationSpeedFactor = 1.f;
        unit->navigation()->formationTarget.reset();
        unit->navigation()->combatSlotTarget.reset();
        unit->navigation()->combatSlotWaiting = false;
        unit->navigation()->trafficRecoveryTarget.reset();
        *unit->vision()              = value.vision;
        unit->worker()->resourceType = value.worker.resourceType;
        unit->worker()->cargo        = value.worker.cargo;
        unit->worker()->capacity     = value.worker.capacity;
        unit->worker()->gatherRate   = value.worker.gatherRate;
        unit->worker()->buildRate    = value.worker.buildRate;
        unit->worker()->repairRate   = value.worker.repairRate;
        unit->worker()->autoAssign   = value.worker.autoAssign;
        if (value.worker.resourceNode.isValid())
            unit->worker()->resourceNode = ResourceNodeLink::bind(handle(value.worker.resourceNode)).value();
        else
            unit->worker()->resourceNode = {};
        if (value.worker.dropoff.isValid())
            unit->worker()->dropoff = BuildingLink::bind(handle(value.worker.dropoff)).value();
        else
            unit->worker()->dropoff = {};
        *unit->combat()                   = value.combat;
        unit->combat()->target            = handle(value.combatTarget);
        *unit->durability()               = value.durability;
        unit->durability()->state.subject = value.subject;
        *unit->shield()                   = value.shield;
        *unit->veterancy()                = value.veterancy;
        *unit->command()                  = value.command;
        unit->command()->source           = handle(value.commandSource);
        unit->command()->uplink           = handle(value.commandUplink);
        *unit->abilities()                = value.abilities;
        if (unit->abilities()->channel) unit->abilities()->channel->target = handle(value.abilityTarget);
        *unit->capture()              = value.capture;
        unit->containment()->capacity = value.containmentCapacity;
        unit->containment()->occupants.clear();
        for (auto subject : value.occupants) unit->containment()->occupants.push_back(handle(subject));
        if (value.container.isValid())
            unit->containment()->container = ContainerLink::bind(handle(value.container)).value();
        else
            unit->containment()->container = {};
        *unit->supply()                         = value.supply;
        unit->supply()->assignedTarget          = handle(value.supplyTarget);
        unit->supply()->convoyLeader            = handle(value.convoyLeader);
        *unit->morale()                         = value.morale;
        *unit->artillery()                      = value.artillery;
        unit->artillery()->fireSupportRequester = handle(value.fireSupportRequester);
        unit->artillery()->observedFireSpotter  = handle(value.observedFireSpotter);
        *unit->tactics()                        = value.tactics;
        unit->tactics()->escortTarget           = handle(value.escortTarget);
        *unit->technology()                     = value.technology;
        auto restored                           = unit->orders()->values.restoreState(unitOrders[index]);
        if (!restored) return restored;
    }
    for (std::size_t index = 0; index < snapshot.buildings.size(); ++index) {
        const auto& value                 = snapshot.buildings[index];
        Building*   building              = findBuilding(value.subject);
        building->definition()->id        = value.definition;
        building->identity()->displayName = value.displayName;
        building->tags()->values          = TagSet{};
        for (const auto& tag : value.tags) {
            auto added = building->tags()->values.add(tag);
            if (!added) return added;
        }
        auto restoredEffects = building->effects()->values.restore(value.effects);
        if (!restoredEffects) return restoredEffects;
        if (value.faction.isValid())
            building->faction()->link = FactionLink::bind(handle(value.faction)).value();
        else
            building->faction()->link = {};
        const auto placementLink    = building->placement()->link;
        *building->placement()      = value.placement;
        building->placement()->link = placementLink;
        *building->construction()   = value.construction;
        building->construction()->builders.clear();
        for (auto subject : value.builders) building->construction()->builders.push_back(handle(subject));
        *building->integrity()                  = value.integrity;
        building->integrity()->state.subject    = value.subject;
        *building->shield()                     = value.shield;
        *building->capture()                    = value.capture;
        building->capture()->capturingFaction   = handle(value.capturingFaction);
        *building->dropoff()                    = value.dropoff;
        *building->rally()                      = value.rally;
        building->rally()->transport            = handle(value.rallyTransport);
        building->rally()->command.targetEntity = handle(value.rallyCommandTarget);
        building->rally()->reinforcements.clear();
        for (auto subject : value.reinforcements) building->rally()->reinforcements.push_back(handle(subject));
        *building->combat()                       = value.combat;
        building->combat()->target                = handle(value.combatTarget);
        building->combat()->airDefenseNetworkRoot = handle(value.airDefenseNetworkRoot);
        *building->garrison()                     = value.garrison;
        building->garrison()->occupants.clear();
        for (auto subject : value.occupants) building->garrison()->occupants.push_back(handle(subject));
        *building->supply()         = value.supply;
        *building->vision()         = value.vision;
        *building->technology()     = value.technology;
        *building->infrastructure() = value.infrastructure;
        *building->command()        = value.command;
        *building->indirectFire()   = value.indirectFire;
        auto production             = building->production()->values.restore(value.productionJson);
        if (!production) return production;
        auto orders = building->orders()->values.restoreState(buildingOrders[index]);
        if (!orders) return orders;
    }
    for (const auto& value : snapshot.resourceNodes) {
        auto* node                    = findResourceNode(value.subject);
        node->identity()->displayName = value.displayName;
        *node->position()             = value.position;
        *node->stock()                = value.stock;
        node->harvest()->capacity     = value.workerCapacity;
        node->harvest()->workers.clear();
        for (auto subject : value.workers) node->harvest()->workers.push_back(handle(subject));
    }
    for (const auto& value : snapshot.players) {
        auto* player                    = findPlayer(value.subject);
        player->identity()->displayName = value.displayName;
        player->selection()->revision   = snapshot.version >= 3 ? value.selectionRevision : 0;
        player->selection()->tick       = snapshot.version >= 3 ? value.selectionTick : SimulationTick{};
        player->selection()->units.clear();
        for (auto subject : value.units) player->selection()->units.push_back(handle(subject));
        player->selection()->buildings.clear();
        for (auto subject : value.buildings) player->selection()->buildings.push_back(handle(subject));
    }
    for (const auto& value : snapshot.factions) {
        auto* faction                    = findFaction(value.subject);
        faction->identity()->displayName = value.displayName;
        faction->members()->units.clear();
        for (auto subject : value.units) faction->members()->units.push_back(handle(subject));
        faction->members()->buildings.clear();
        for (auto subject : value.buildings) faction->members()->buildings.push_back(handle(subject));
        *faction->strategy()         = value.strategy;
        *faction->workforce()        = value.workforce;
        *faction->productionPolicy() = value.productionPolicy;
        *faction->intel()            = value.intel;
        *faction->technology()       = value.technology;
    }
    for (const auto& value : snapshot.matches) {
        auto* match      = findMatch(value.subject);
        *match->rules()  = value.rules;
        *match->state()  = value.state;
        *match->events() = value.events;
        match->participants()->entries.clear();
        for (const auto& participant : value.participants) {
            Match::Participants::Entry entry;
            entry.faction     = FactionLink::bind(handle(participant.faction)).value();
            entry.team        = participant.team;
            entry.eliminated  = participant.eliminated;
            entry.surrendered = participant.surrendered;
            entry.reason      = participant.reason;
            match->participants()->entries.push_back(std::move(entry));
        }
    }
    movementGroups_ = std::move(groups).takeValue();
    for (auto& group : movementGroups_)
        for (std::size_t i = 0; i < group.handles.size(); ++i)
            group.epochs[i] = static_cast<Unit*>(ecs::try_get(group.handles[i]))->orders()->values.membershipEpoch_;
    projectiles_ = std::move(stagedProjectiles);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::rebuildState(const RTSStateSnapshot& snapshot) {
    if (snapshot.version < 3 && std::any_of(snapshot.players.begin(), snapshot.players.end(), [](const auto& p) {
            return p.selectionRevision != 0 || p.selectionTick.value() != 0;
        }))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "Legacy RTS snapshots cannot carry v3 player watermarks", "players.selection"));

    if (unitCount() != 0 || buildingCount() != 0 || resourceNodeCount() != 0 || playerCount() != 0 ||
        factionCount() != 0 || matchCount() != 0)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "RTS snapshot topology can only be rebuilt into an empty module", "snapshot"));
    if (snapshot.version != 1 && snapshot.version != 2 && snapshot.version != 3)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "unsupported RTS snapshot version", "snapshot.version"));

    // A topology rebuild can follow destruction performed while an ECS view was
    // deferred. Publish those tombstones before allocating replacement roots so
    // component storage (notably effect timelines) cannot be reused half-staged.
    ecs::commit();

    std::unordered_set<SubjectRef> subjects;
    auto reserveSubject = [&](SubjectRef subject) { return subject.isValid() && subjects.insert(subject).second; };
    for (const auto& value : snapshot.units)
        if (!reserveSubject(value.subject))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "invalid/duplicate unit subject", "units.subject"));
    for (const auto& value : snapshot.buildings)
        if (!reserveSubject(value.subject))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "invalid/duplicate building subject", "buildings.subject"));
    for (const auto& value : snapshot.resourceNodes)
        if (!reserveSubject(value.subject))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "invalid/duplicate resource subject", "resourceNodes.subject"));
    for (const auto& value : snapshot.players)
        if (!reserveSubject(value.subject))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "invalid/duplicate player subject", "players.subject"));
    for (const auto& value : snapshot.factions)
        if (!reserveSubject(value.subject))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "invalid/duplicate faction subject", "factions.subject"));
    for (const auto& value : snapshot.matches)
        if (!reserveSubject(value.subject))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "invalid/duplicate match subject", "matches.subject"));

    auto failAndClear = [&](const Status& status) {
        clearOwnedRoots();
        return Result<void>::failure(status);
    };
    for (const auto& value : snapshot.factions) {
        auto created = newFaction(value.subject);
        if (!created) return failAndClear(created.status());
    }
    for (const auto& value : snapshot.units) {
        auto created = newUnit(value.subject, value.definition);
        if (!created) return failAndClear(created.status());
        auto materialized = materialize(*created.value());
        if (!materialized) return failAndClear(materialized.status());
    }
    for (const auto& value : snapshot.buildings) {
        auto created = newBuilding(value.subject, value.definition);
        if (!created) return failAndClear(created.status());
        auto materialized = materialize(*created.value());
        if (!materialized) return failAndClear(materialized.status());
    }
    for (const auto& value : snapshot.resourceNodes) {
        auto created = newResourceNode(value.subject, value.stock.resourceType, value.stock.maximum,
                                       {value.position.x, value.position.y}, value.workerCapacity);
        if (!created) return failAndClear(created.status());
    }
    for (const auto& value : snapshot.players) {
        auto created = newPlayer(value.subject);
        if (!created) return failAndClear(created.status());
    }
    for (const auto& value : snapshot.matches) {
        auto created = newMatch(value.subject);
        if (!created) return failAndClear(created.status());
    }
    auto restored = restoreState(snapshot);
    if (!restored) return failAndClear(restored.status());
    return restored;
}

}  // namespace eve::rts
