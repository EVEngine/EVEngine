#include <cmath>
#include <cstdio>
#include <set>
#include <simplesquirrel/simplesquirrel.hpp>
#include "RtsCompositionFixtures.h"
#include "rts/RTSReplay.h"
#include "zeroerr/assert.h"

namespace test_rts_formation_composition {
TEST_CASE("rts.formation.largeTranslatedGridDoesNotReverseMarchingRows") {
    ecs::Table                   world;
    ecs::ScopedTable             guard(world);
    eve::rts::RTS                rts;
    std::vector<eve::SubjectRef> ids;
    for (int index = 0; index < 512; ++index) {
        char text[37];
        std::snprintf(text, sizeof(text), "00000000-0000-7000-8000-%012x", 0xb000 + index);
        const auto id         = subject(text);
        auto*      unit       = rts.newUnit(id).takeValue();
        unit->motion()->x     = 20.f - float(index % 8);
        unit->motion()->y     = 4.f + float(index / 8);
        unit->crowd()->radius = 0.35f;
        ids.push_back(id);
    }
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {36.f, 35.5f};
    REQUIRE(rts.commandUnits(ids, move, {FormationKind::Grid, 1.f, 8}).ok());
    for (const auto& id : ids) {
        auto*      unit   = rts.findUnit(id);
        const auto target = unit->orders()->values.current().value().target;
        CHECK(std::abs((target.x - unit->motion()->x) - 19.5f) < 1e-4f);
        CHECK(std::abs(target.y - unit->motion()->y) < 1e-4f);
    }
}
}  // namespace test_rts_formation_composition

namespace test_rts_attack_slots_composition {
namespace {
std::vector<Unit*> attackers(eve::rts::RTS& rts, Unit& target) {
    std::vector<Unit*> result;
    for (int i = 0; i < 12; ++i) {
        char id[37];
        std::snprintf(id, sizeof(id), "00000000-0000-7000-8000-%012x", 0xa600 + i);
        auto* unit                      = rts.newUnit(subject(id)).takeValue();
        unit->crowd()->radius           = 0.35f;
        unit->motion()->x               = 2.f;
        unit->motion()->y               = float(i) * 0.01f;
        unit->combat()->engagementRange = 1.2f;
        CommandSpec order;
        order.kind         = OrderKind::Attack;
        order.targetEntity = ecs::handle_of(&target);
        REQUIRE(unit->orders()->values.enqueue(order).ok());
        result.push_back(unit);
    }
    return result;
}
}  // namespace
TEST_CASE("rts.attackSlots.frontPositionsAreUniqueAndRearCannotFire") {
    ecs::Table         world;
    ecs::ScopedTable   guard(world);
    eve::rts::RTS      rts;
    PendingRTSExecutor executor;
    auto*              target = rts.newUnit(subject("00000000-0000-7000-8000-00000000a500")).takeValue();
    target->crowd()->radius   = 0.35f;
    const auto units          = attackers(rts, *target);
    REQUIRE(rts.step({eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    std::set<std::pair<float, float>> positions;
    int                               front = 0, rear = 0;
    for (auto* unit : units) {
        REQUIRE(unit->navigation()->combatSlotTarget.has_value());
        const auto slot = *unit->navigation()->combatSlotTarget;
        CHECK(positions.emplace(slot.x, slot.y).second);
        const float distance = std::hypot(slot.x - target->motion()->x, slot.y - target->motion()->y);
        if (unit->navigation()->combatSlotWaiting) {
            ++rear;
            CHECK(distance > 1.2f);
        } else {
            ++front;
            CHECK(distance <= 1.2f);
            CHECK(distance >= 0.7f);
        }
        CHECK(!unit->motion()->arrived);
    }
    CHECK_EQ(front, 7);
    CHECK_EQ(rear, 5);
    const auto saved = rts.snapshotState().takeValue();
    for (const auto& unit : saved.units) {
        CHECK(!unit.navigation.combatSlotTarget);
        CHECK(!unit.navigation.combatSlotWaiting);
    }
}
TEST_CASE("rts.attackSlots.targetDestructionAndReplacementOrderExpireReservations") {
    ecs::Table         world;
    ecs::ScopedTable   guard(world);
    eve::rts::RTS      rts;
    PendingRTSExecutor executor;
    const auto         id     = subject("00000000-0000-7000-8000-00000000a501");
    auto*              target = rts.newUnit(id).takeValue();
    target->crowd()->radius   = 0.35f;
    const auto units          = attackers(rts, *target);
    REQUIRE(rts.step({eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    REQUIRE(units.front()->navigation()->combatSlotTarget.has_value());
    REQUIRE(rts.remove(id).ok());
    REQUIRE(rts.newUnit(id).ok());
    REQUIRE(rts.step({eve::SimulationTick(2), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    for (auto* unit : units) {
        CHECK(!unit->navigation()->combatSlotTarget);
        CHECK(!unit->navigation()->combatSlotWaiting);
    }
    auto*       replacement = rts.findUnit(id);
    CommandSpec attack;
    attack.kind         = OrderKind::Attack;
    attack.targetEntity = ecs::handle_of(replacement);
    REQUIRE(units.front()->orders()->values.replace(attack).ok());
    REQUIRE(rts.step({eve::SimulationTick(3), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    REQUIRE(units.front()->navigation()->combatSlotTarget.has_value());
    CommandSpec stop;
    stop.kind = OrderKind::Stop;
    REQUIRE(units.front()->orders()->values.replace(stop).ok());
    REQUIRE(rts.step({eve::SimulationTick(4), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    CHECK(!units.front()->navigation()->combatSlotTarget);
}


TEST_CASE("rts.attackSlots.waitingMembersNeverProduceRealWeaponFire") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(32, 24, 1.f).ok());
    REQUIRE(rts.loadScriptContent(R"({
      "weapons":[{"id":"club","damageType":"normal","damage":10,"range":1.2,
        "cooldown":0.2,"projectileSpeed":0,"magazineSize":1000}],
      "units":[{"id":"fighter","weaponType":"club","health":40,"speed":3,"radius":0.35,
        "attackRange":1.2,"sightRange":10,"targetTags":["biological"]},
        {"id":"dummy","health":10000,"speed":0,"radius":0.35,"targetTags":["biological"]}]
    })")
                .ok());
    auto* blue = rts.newFaction(subject("00000000-0000-7000-8000-00000000a701")).takeValue();
    auto* red  = rts.newFaction(subject("00000000-0000-7000-8000-00000000a702")).takeValue();
    auto* target =
        rts.newFactionUnit(*red, subject("00000000-0000-7000-8000-00000000a703"), *eve::LogicalId::parse("unit:dummy"))
            .takeValue();
    target->motion()->x = 12.f;
    target->motion()->y = 8.f;
    std::vector<Unit*> units;
    for (int i = 0; i < 12; ++i) {
        char id[37];
        std::snprintf(id, sizeof(id), "00000000-0000-7000-8000-%012x", 0xa710 + i);
        auto* unit        = rts.newFactionUnit(*blue, subject(id), *eve::LogicalId::parse("unit:fighter")).takeValue();
        unit->motion()->x = 10.f;
        unit->motion()->y = 8.f + float(i) * 0.01f;
        CommandSpec attack;
        attack.kind         = OrderKind::Attack;
        attack.targetEntity = ecs::handle_of(target);
        REQUIRE(unit->orders()->values.enqueue(attack).ok());
        units.push_back(unit);
    }
    int  fired           = 0;
    bool observedWaiting = false;
    for (int tick = 0; tick < 100; ++tick) {
        REQUIRE(rts.stepScript(0.05).ok());
        for (auto* unit : units) observedWaiting = observedWaiting || unit->navigation()->combatSlotWaiting;
        const auto  frame  = rts.inspectFrameEvents();
        const auto* events = frame.getIf<eve::Value::Array>();
        REQUIRE(events != nullptr);
        for (const auto& event : *events) {
            const auto* object = event.getIf<eve::Value::Object>();
            REQUIRE(object != nullptr);
            const auto* type = object->at("type").getIf<std::string>();
            REQUIRE(type != nullptr);
            if (*type != "weapon_fired") continue;
            const auto* source = object->at("source").getIf<std::string>();
            REQUIRE(source != nullptr);
            auto* shooter = rts.findUnit(subject(source->c_str()));
            REQUIRE(shooter != nullptr);
            CHECK(!shooter->navigation()->combatSlotWaiting);
            ++fired;
        }
    }
    CHECK(observedWaiting);
    CHECK(fired > 0);
    CHECK(target->durability()->state.health < 10000.0);
}


TEST_CASE("rts.attackSlots.mixedRadiiAndRangesKeepSmallUnitEligible") {
    ecs::Table         world;
    ecs::ScopedTable   guard(world);
    eve::rts::RTS      rts;
    PendingRTSExecutor executor;
    auto*              target = rts.newUnit(subject("00000000-0000-7000-8000-00000000a901")).takeValue();
    auto*              small  = rts.newUnit(subject("00000000-0000-7000-8000-00000000a902")).takeValue();
    auto*              large  = rts.newUnit(subject("00000000-0000-7000-8000-00000000a903")).takeValue();
    target->crowd()->radius = small->crowd()->radius = 0.35f;
    large->crowd()->radius                           = 1.f;
    small->combat()->engagementRange                 = 0.8f;
    large->combat()->engagementRange                 = 2.7f;
    small->motion()->x                               = 1.f;
    large->motion()->x                               = 2.f;
    for (auto* unit : {small, large}) {
        CommandSpec attack;
        attack.kind         = OrderKind::Attack;
        attack.targetEntity = ecs::handle_of(target);
        REQUIRE(unit->orders()->values.enqueue(attack).ok());
    }
    REQUIRE(rts.step({eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    REQUIRE(small->navigation()->combatSlotTarget.has_value());
    REQUIRE(large->navigation()->combatSlotTarget.has_value());
    CHECK(!small->navigation()->combatSlotWaiting);
    CHECK(!large->navigation()->combatSlotWaiting);
    const auto a = *small->navigation()->combatSlotTarget, b = *large->navigation()->combatSlotTarget;
    CHECK(std::hypot(a.x, a.y) <= 0.8f);
    CHECK(std::hypot(b.x, b.y) <= 2.7f);
    CHECK(std::hypot(a.x - b.x, a.y - b.y) >= 1.45f);
}
}  // namespace test_rts_attack_slots_composition

namespace test_rts_group_attack_move_composition {
TEST_CASE("rts.groups.attackMoveAdmissionAndRestorePreserveOrders") {
    ecs::Table                   world;
    ecs::ScopedTable             guard(world);
    eve::rts::RTS                rts;
    auto                         a      = subject("00000000-0000-7000-8000-00000000a101");
    auto                         b      = subject("00000000-0000-7000-8000-00000000a102");
    auto*                        first  = rts.newUnit(a).takeValue();
    auto*                        second = rts.newUnit(b).takeValue();
    eve::rts::MovementGroupBatch batch;
    batch.units     = {a, b};
    batch.target    = {10.f, 5.f};
    batch.formation = {FormationKind::Grid, 1.f, 2};
    batch.kind      = OrderKind::AttackMove;
    REQUIRE(rts.submitMovementGroup(batch).ok());
    REQUIRE(first->orders()->values.current().value().kind == OrderKind::AttackMove);
    auto saved = rts.snapshotState();
    REQUIRE(saved.ok());
    REQUIRE_EQ(saved.value().movementGroups.size(), 1u);
    REQUIRE(rts.restoreState(saved.value()).ok());
    REQUIRE(second->orders()->values.current().value().kind == OrderKind::AttackMove);
    batch.kind = OrderKind::Attack;
    REQUIRE(!rts.submitMovementGroup(batch).ok());
    REQUIRE_EQ(rts.snapshotState().value().movementGroups.size(), 1u);
    REQUIRE(first->orders()->values.current().value().kind == OrderKind::AttackMove);
}

TEST_CASE("rts.groups.attackMoveReplayV6RoundTripsAndRejectsV5CombinationAtomically") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    auto             a = subject("00000000-0000-7000-8000-00000000a201");
    auto             b = subject("00000000-0000-7000-8000-00000000a202");
    REQUIRE(rts.newUnit(a).ok());
    REQUIRE(rts.newUnit(b).ok());
    eve::rts::RTSReplayCommand command;
    command.operation      = eve::rts::RTSReplayOperation::MovementGroup;
    command.tick           = eve::SimulationTick(1);
    command.units          = {a, b};
    command.command.kind   = OrderKind::AttackMove;
    command.command.target = {10.f, 5.f};
    command.formation      = {FormationKind::Grid, 1.f, 2};
    eve::rts::RTSCommandLog log;
    REQUIRE(log.queue(command).ok());
    auto encoded = log.exportText();
    REQUIRE(encoded.starts_with("EVERTS_COMMANDS 6"));
    eve::rts::RTSCommandLog imported;
    REQUIRE(imported.importText(encoded).ok());
    auto before = imported.exportText();
    auto legacy = encoded;
    legacy.replace(0, std::string("EVERTS_COMMANDS 6").size(), "EVERTS_COMMANDS 5");
    REQUIRE(!imported.importText(legacy).ok());
    REQUIRE_EQ(imported.exportText(), before);
    REQUIRE(imported.apply(eve::SimulationTick(1), rts).ok());
    REQUIRE_EQ(rts.snapshotState().value().movementGroups.size(), 1u);
}


TEST_CASE("rts.groups.attackMoveEngagedMemberDoesNotHoldBackAdvancingMember") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    auto             a         = subject("00000000-0000-7000-8000-00000000a301");
    auto             b         = subject("00000000-0000-7000-8000-00000000a302");
    auto             enemyId   = subject("00000000-0000-7000-8000-00000000a303");
    auto*            engaged   = rts.newUnit(a).takeValue();
    auto*            advancing = rts.newUnit(b).takeValue();
    auto*            enemy     = rts.newUnit(enemyId).takeValue();
    engaged->motion()->x       = -10.f;
    advancing->motion()->x     = 0.f;
    enemy->motion()->x         = -9.f;
    engaged->motion()->speed = advancing->motion()->speed = 4.f;
    // This case exercises ranged engagement; melee now moves into an admitted approach slot.
    engaged->crowd()->radius = advancing->crowd()->radius = enemy->crowd()->radius = 0.35f;
    engaged->combat()->target                                                      = ecs::handle_of(enemy);
    engaged->combat()->engagementRange                                             = 2.f;
    eve::rts::MovementGroupBatch batch;
    batch.units     = {a, b};
    batch.kind      = OrderKind::AttackMove;
    batch.target    = {10.f, 0.f};
    batch.formation = {FormationKind::Line, 1.f, 0};
    REQUIRE(rts.submitMovementGroup(batch).ok());
    PendingRTSExecutor executor;
    REQUIRE(rts.step({eve::SimulationTick(1), eve::Duration::fromSeconds(0.05).takeValue()}, executor).ok());
    CHECK_EQ(advancing->navigation()->formationSpeedFactor, 1.f);
    CHECK(advancing->motion()->x > 0.19f);
    CHECK_EQ(engaged->motion()->x, -10.f);
}


TEST_CASE("rts.groups.attackMoveArrivalRetiresMovingGroupAndKeepsCombatStance") {
    ecs::Table                   world;
    ecs::ScopedTable             guard(world);
    eve::rts::RTS                rts;
    const auto                   a      = subject("00000000-0000-7000-8000-00000000a401");
    const auto                   b      = subject("00000000-0000-7000-8000-00000000a402");
    auto*                        first  = rts.newUnit(a).takeValue();
    auto*                        second = rts.newUnit(b).takeValue();
    eve::rts::MovementGroupBatch batch;
    batch.units     = {a, b};
    batch.kind      = OrderKind::AttackMove;
    batch.target    = {10.f, 0.f};
    batch.formation = {FormationKind::Line, 1.f, 0};
    REQUIRE(rts.submitMovementGroup(batch).ok());
    for (auto* unit : {first, second}) {
        const auto target       = unit->orders()->values.current().value().target;
        unit->motion()->x       = target.x;
        unit->motion()->y       = target.y;
        unit->motion()->arrived = true;
    }
    PendingRTSExecutor executor;
    REQUIRE(rts.step({eve::SimulationTick(1), eve::Duration::fromSeconds(0.05).takeValue()}, executor).ok());
    REQUIRE(rts.snapshotState().value().movementGroups.empty());
    CHECK(first->orders()->values.current().value().kind == OrderKind::AttackMove);
    CHECK(second->orders()->values.current().value().kind == OrderKind::AttackMove);
}
}  // namespace test_rts_group_attack_move_composition

namespace test_rts_combat_suppression_aura_composition {
TEST_CASE("rts.combat.suppressionAuraUsesLiveProviderState") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto*            blue     = Faction::createFaction(subject("00000000-0000-7000-8000-000000000081"));
    auto*            red      = Faction::createFaction(subject("00000000-0000-7000-8000-000000000082"));
    auto*            attacker = Unit::createUnit(subject("00000000-0000-7000-8000-000000000083"));
    auto*            target   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000084"));
    auto*            leader   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000085"));
    attacker->faction()->link = eve::rts::FactionLink::bind(ecs::handle_of(blue)).takeValue();
    target->faction()->link   = eve::rts::FactionLink::bind(ecs::handle_of(red)).takeValue();
    leader->faction()->link   = eve::rts::FactionLink::bind(ecs::handle_of(red)).takeValue();
    target->motion()->x = leader->motion()->x = 5.f;
    attacker->combat()->target                = ecs::handle_of(target);
    attacker->combat()->acquisitionRange      = 10.f;
    attacker->combat()->suppressionPerShot    = 3.f;
    target->morale()->capacity                = 20.f;
    target->durability()->state.health        = 100.0;
    target->durability()->state.maxHealth     = 100.0;
    leader->morale()->auraRange               = 4.f;
    leader->morale()->auraSuppressionFactor   = 0.5f;

    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id                   = "aura-test-rifle";
    definition.kind                 = eve::weapon::WeaponKind::Ranged;
    definition.logic                = "rts-hitscan";
    definition.damage               = 1.f;
    definition.range                = 8.f;
    definition.cooldown             = 0.1f;
    definition.magSize              = 10;
    weapon->definition()->owned     = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def       = weapon->definition()->owned.get();
    weapon->state()->stages         = &weapon->definition()->def->stages;
    weapon->state()->resource.kind  = eve::weapon::ResourceKind::Ammo;
    weapon->state()->resource.value = 10.f;
    weapon->state()->resource.max   = 10.f;
    attacker->weapon()->link        = eve::rts::WeaponLink::bind(ecs::handle_of(weapon)).takeValue();

    eve::sensing::SensingWorld        sensing;
    eve::combat::DamageRuntime        damage;
    eve::rts::CombatFireSystem::State state;
    const auto                        dt = eve::Duration::fromSeconds(0.2).expect("aura test dt");
    auto fired = eve::rts::CombatFireSystem::step({eve::SimulationTick{1}, dt}, state, sensing, damage);
    REQUIRE(fired.ok());
    CHECK_EQ(fired.value(), 1u);
    CHECK(std::abs(target->morale()->suppression - 1.5f) < 1e-5f);

    leader->durability()->alive = false;
    fired = eve::rts::CombatFireSystem::step({eve::SimulationTick{2}, dt}, state, sensing, damage);
    REQUIRE(fired.ok());
    CHECK_EQ(fired.value(), 1u);
    CHECK(std::abs(target->morale()->suppression - 4.5f) < 1e-5f);

    weapon->release();
    attacker->release();
    target->release();
    leader->release();
    blue->release();
    red->release();
}
}  // namespace test_rts_combat_suppression_aura_composition

namespace test_rts_performance_semantics_composition {
TEST_CASE("rts.performanceCasualtyBatchRepairsOwnedReferencesWithoutCrowdProvider") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    sim;
    auto*            faction = sim.newFaction(subject("00000000-0000-7000-8000-00000000ba01")).expect("faction");
    auto* dead     = sim.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000ba02")).expect("dead");
    auto* survivor = sim.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000ba03")).expect("survivor");
    auto* building =
        sim.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-00000000ba04")).expect("building");
    auto*      player     = sim.newPlayer(subject("00000000-0000-7000-8000-00000000ba05")).expect("player");
    const auto deadHandle = ecs::handle_of(dead), aliveHandle = ecs::handle_of(survivor);
    survivor->combat()->target         = deadHandle;
    survivor->tactics()->escortTarget  = deadHandle;
    survivor->supply()->assignedTarget = deadHandle;
    survivor->supply()->convoyLeader   = deadHandle;
    building->garrison()->occupants    = {deadHandle};
    building->garrison()->capacity     = 1;
    dead->containment()->container     = eve::rts::ContainerLink::bind(ecs::handle_of(building)).expect("container");
    building->construction()->builders = {deadHandle, aliveHandle};
    building->rally()->reinforcements  = {deadHandle, aliveHandle};
    building->rally()->transport       = deadHandle;
    building->rally()->transportActive = true;
    building->capture()->blockedByGarrison = true;
    player->selection()->units             = {deadHandle, aliveHandle};
    dead->durability()->alive              = false;
    dead->durability()->state.health       = 0;
    const auto cleanupCount                = sim.cleanupDestroyed().expect("cleanup");
    REQUIRE_EQ(cleanupCount, 1);
    REQUIRE(ecs::try_get(deadHandle) == nullptr);
    REQUIRE(ecs::try_get(aliveHandle) == survivor);
    REQUIRE(survivor->combat()->target.table == nullptr);
    REQUIRE(survivor->tactics()->escortTarget.table == nullptr);
    REQUIRE(survivor->supply()->assignedTarget.table == nullptr);
    REQUIRE(survivor->supply()->convoyLeader.table == nullptr);
    REQUIRE(building->garrison()->occupants.empty());
    REQUIRE(!building->capture()->blockedByGarrison);
    REQUIRE(!building->rally()->transportActive);
    REQUIRE_EQ(building->construction()->builders.size(), 1);
    REQUIRE_EQ(building->rally()->reinforcements.size(), 1);
    REQUIRE_EQ(player->selection()->units.size(), 1);
    REQUIRE_EQ(faction->members()->units.size(), 1);
    const auto emptycleanupCount = sim.cleanupDestroyed().expect("empty cleanup");
    REQUIRE_EQ(emptycleanupCount, 0);
}

TEST_CASE("rts.performanceCasualtyBatchCrowdFailureLeavesReferencesAndRootsIntact") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd provider;
    eve::rts::RTS     sim;
    sim.setCrowdProvider(&provider);
    auto* first           = sim.newUnit(subject("00000000-0000-7000-8000-00000000bb01")).expect("first");
    auto* second          = sim.newUnit(subject("00000000-0000-7000-8000-00000000bb02")).expect("second");
    first->crowd()->link  = eve::rts::CrowdLink::bind("first").expect("link");
    second->crowd()->link = eve::rts::CrowdLink::bind("second").expect("link");
    second->motion()->x   = 4.f;
    PendingRTSExecutor executor;
    REQUIRE(sim.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(50000000)}, executor).ok());
    const eve::crowd::AgentRef original{second->crowd()->runtimeAgent, second->crowd()->runtimeOwner};
    const auto                 target = ecs::handle_of(first);
    second->combat()->target          = target;
    first->durability()->alive = second->durability()->alive = false;
    first->durability()->state.health = second->durability()->state.health = 0;
    second->crowd()->runtimeAgent                                          = first->crowd()->runtimeAgent;
    auto rejected                                                          = sim.cleanupDestroyed();
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);
    CHECK_EQ(sim.unitCount(), 2);
    CHECK_EQ(provider.getAgentCount(), 2);
    CHECK_EQ(second->combat()->target.table, target.table);
    CHECK_EQ(second->combat()->target.id, target.id);
    CHECK_EQ(second->combat()->target.generation, target.generation);
    second->crowd()->runtimeAgent = original.handle;
    const auto validbatchCount    = sim.cleanupDestroyed().expect("valid batch");
    CHECK_EQ(validbatchCount, 2);
    CHECK_EQ(provider.getAgentCount(), 0);
    CHECK(!provider.isAgentAlive(original));
}

TEST_CASE("rts.performanceNestedPassengerCleanupKeepsRecursiveLifecycle") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    sim;
    auto*            carrier         = sim.newUnit(subject("00000000-0000-7000-8000-00000000bc01")).expect("carrier");
    auto*            passenger       = sim.newUnit(subject("00000000-0000-7000-8000-00000000bc02")).expect("passenger");
    carrier->containment()->capacity = 1;
    carrier->containment()->occupants   = {ecs::handle_of(passenger)};
    passenger->containment()->container = eve::rts::ContainerLink::bind(ecs::handle_of(carrier)).expect("container");
    const auto carrierHandle = ecs::handle_of(carrier), passengerHandle = ecs::handle_of(passenger);
    carrier->durability()->alive        = false;
    carrier->durability()->state.health = 0;
    const auto recursivecleanupCount    = sim.cleanupDestroyed().expect("recursive cleanup");
    REQUIRE_EQ(recursivecleanupCount, 2);
    REQUIRE(ecs::try_get(carrierHandle) == nullptr);
    REQUIRE(ecs::try_get(passengerHandle) == nullptr);
    REQUIRE_EQ(sim.unitCount(), 0);
}
}  // namespace test_rts_performance_semantics_composition
