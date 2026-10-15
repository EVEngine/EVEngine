#include "zeroerr/assert.h"
#include <cmath>
#include <cstdio>
#include <set>
#include "RtsCompositionFixtures.h"

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
