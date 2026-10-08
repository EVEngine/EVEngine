#include <algorithm>
#include <unordered_set>
#include "rts/RTS.h"
#include "rts/RTSSnapshotInternal.h"

namespace eve::rts {
using snapshot_internal::subjectOf;
Result<void> RTS::restoreState(const RTSStateSnapshot& snapshot) {
    if ((snapshot.version != 1 && snapshot.version != 2) || snapshot.units.size() != unitCount() ||
        snapshot.buildings.size() != buildingCount() || snapshot.resourceNodes.size() != resourceNodeCount() ||
        snapshot.players.size() != playerCount() || snapshot.factions.size() != factionCount() ||
        snapshot.matches.size() != matchCount())
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
    if (unitCount() != 0 || buildingCount() != 0 || resourceNodeCount() != 0 || playerCount() != 0 ||
        factionCount() != 0 || matchCount() != 0)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "RTS snapshot topology can only be rebuilt into an empty module", "snapshot"));
    if (snapshot.version != 1 && snapshot.version != 2)
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
