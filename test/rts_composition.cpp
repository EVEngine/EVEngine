#include "common/ECS.h"
#include "common/Identity.h"
#include "rts/RTS.h"
#include "rts/RTSAction.h"
#include "rts/RTSSystems.h"

#include "action/Action.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using eve::rts::Building;
using eve::rts::CommandSpec;
using eve::rts::Faction;
using eve::rts::FormationKind;
using eve::rts::FormationSpec;
using eve::rts::OrderKind;
using eve::rts::Player;
using eve::rts::Unit;
using eve::rts::WorldPosition;

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

class Scout final : public Unit {
public:
    ENTITY(Scout, Unit)

    void release() override { ecs::DestroyEntity(this); }
};

void initializeScout(Scout& scout) {
    scout.identity()->self = ecs::handle_of(&scout);
    (void)scout.motion();
    (void)scout.orders();
    (void)scout.action();
}

}  // namespace

static_assert(std::is_base_of_v<ecs::Entity, Unit>);
static_assert(std::is_base_of_v<ecs::Entity, Building>);
static_assert(std::is_base_of_v<ecs::Entity, Player>);
static_assert(std::is_base_of_v<ecs::Entity, Faction>);
static_assert(!std::is_base_of_v<Unit, Building>);
static_assert(!std::is_base_of_v<Building, Unit>);
static_assert(!std::is_base_of_v<Player, Faction>);

TEST_CASE("rts.compositionRootsAndBatchViews") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    Unit* unit = Unit::createUnit();
    Building* building = Building::createBuilding();
    unit->motion()->x = 10.0f;
    building->placement()->cellX = 4;

    int units = 0;
    auto unitView = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders>();
    for (auto it = unitView.begin(); it != unitView.end(); ++it) {
        auto [identity, motion, orders] = *it;
        CHECK(identity != nullptr);
        CHECK(motion != nullptr);
        CHECK(orders != nullptr);
        ++units;
    }
    CHECK_EQ(units, 1);

    int buildings = 0;
    auto buildingView = ecs::View<Building, Building::Identity, Building::Placement>();
    for (auto it = buildingView.begin(); it != buildingView.end(); ++it) {
        auto [identity, placement] = *it;
        CHECK(identity != nullptr);
        CHECK(placement != nullptr);
        ++buildings;
    }
    CHECK_EQ(buildings, 1);

    unit->release();
    building->release();
}

TEST_CASE("rts.baseViewIncludesSubclassButNotOtherRoot") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    Unit* unit = Unit::createUnit();
    Scout* scout = Scout::create();
    initializeScout(*scout);
    Building* building = Building::createBuilding();

    int allUnits = 0;
    auto baseView = ecs::View<Unit, Unit::Identity, Unit::Motion>();
    for (auto it = baseView.begin(); it != baseView.end(); ++it) {
        auto [identity, motion] = *it;
        CHECK(identity != nullptr);
        CHECK(motion != nullptr);
        ++allUnits;
    }
    CHECK_EQ(allUnits, 2);

    int exactScouts = 0;
    auto scoutView = ecs::View<Scout, Unit::Identity, Unit::Motion>();
    for (auto it = scoutView.begin(); it != scoutView.end(); ++it) ++exactScouts;
    CHECK_EQ(exactScouts, 1);

    int unitOrderCount = 0;
    auto orderView = ecs::View<Unit, Unit::Orders>();
    for (auto it = orderView.begin(); it != orderView.end(); ++it) ++unitOrderCount;
    CHECK_EQ(unitOrderCount, 2);

    int buildingAsUnit = 0;
    auto noBuildingView = ecs::View<Unit, Unit::Identity, Unit::Motion>();
    for (auto it = noBuildingView.begin(); it != noBuildingView.end(); ++it) {
        auto [identity, motion] = *it;
        (void)identity;
        (void)motion;
        ++buildingAsUnit;
    }
    CHECK_EQ(buildingAsUnit, 2);

    unit->release();
    scout->release();
    building->release();
}

TEST_CASE("rts.typedLinkDetectsStaleTarget") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    Faction* faction = Faction::createFaction();
    Unit* unit = Unit::createUnit();
    auto linked = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(linked.ok());
    unit->faction()->link = std::move(linked).takeValue();
    CHECK(unit->faction()->link.isBound());
    CHECK(!unit->faction()->link.isStale());
    CHECK(unit->faction()->link.resolve() == faction);

    faction->release();
    CHECK(unit->faction()->link.isStale());
    CHECK(unit->faction()->link.resolve() == nullptr);

    unit->release();
}

TEST_CASE("rts.formationPlannerIsDeterministic") {
    const FormationSpec spec{FormationKind::Grid, 10.0f, 2};
    auto planned = eve::rts::FormationPlanner::plan(3, {100.0f, 200.0f}, spec);
    REQUIRE(planned.ok());
    const auto positions = std::move(planned).takeValue();
    REQUIRE_EQ(positions.size(), 3u);
    CHECK(std::abs(positions[0].x - 95.0f) < 1e-5f);
    CHECK(std::abs(positions[0].y - 195.0f) < 1e-5f);
    CHECK(std::abs(positions[1].x - 105.0f) < 1e-5f);
    CHECK(std::abs(positions[1].y - 195.0f) < 1e-5f);
    CHECK(std::abs(positions[2].x - 95.0f) < 1e-5f);
    CHECK(std::abs(positions[2].y - 205.0f) < 1e-5f);
}

TEST_CASE("rts.commandFanOutUsesGenericOrdersAndFormation") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    std::vector<Unit*> units;
    std::vector<ecs::EntityHandle> handles;
    for (int index = 0; index < 3; ++index) {
        Unit* unit = Unit::createUnit();
        unit->motion()->speed = 100.0f;
        units.push_back(unit);
        handles.push_back(ecs::handle_of(unit));
    }

    const CommandSpec command{OrderKind::Move, {100.0f, 200.0f}, {}, 0, 0.0};
    const FormationSpec formation{FormationKind::Grid, 10.0f, 2};
    auto receiptResult = eve::rts::CommandFanOutSystem::fanOut(handles, command, formation);
    REQUIRE(receiptResult.ok());
    const auto receipt = std::move(receiptResult).takeValue();
    CHECK_EQ(receipt.requested, 3u);
    CHECK_EQ(receipt.accepted, 3u);
    CHECK_EQ(receipt.orderIds.size(), 3u);

    for (std::size_t index = 0; index < units.size(); ++index) {
        auto current = units[index]->orders()->values.current();
        REQUIRE(current.ok());
        const auto order = std::move(current).takeValue();
        CHECK_EQ(static_cast<int>(order.kind), static_cast<int>(OrderKind::Move));
        CHECK_EQ(order.formationSlot, static_cast<int>(index));
        CHECK_EQ(order.id, receipt.orderIds[index]);
    }

    const std::size_t beforeRejectedFanOut = units[0]->orders()->values.orderCount();
    units[2]->release();
    auto staleResult = eve::rts::CommandFanOutSystem::fanOut(handles, command, formation);
    CHECK(!staleResult.ok());
    staleResult.ignore("expected stale selection in fan-out test");
    CHECK_EQ(units[0]->orders()->values.orderCount(), beforeRejectedFanOut);

    units[0]->release();
    units[1]->release();
}

TEST_CASE("rts.actionAdapterReusesSharedActionRuntime") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    Unit* unit = Unit::createUnit();
    unit->motion()->speed = 10.0f;
    const CommandSpec move{OrderKind::Move, {20.0f, 0.0f}, {}, 0, 0.0};
    auto order = unit->orders()->values.enqueue(move);
    REQUIRE(order.ok());
    std::move(order).takeValue();

    eve::action::ActionRuntime runtime;
    eve::rts::ActionAdapter adapter(runtime);
    const eve::SimulationStep first{eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("first dt")};
    auto moved = eve::rts::MotionSystem::step(first);
    REQUIRE(moved.ok());
    std::move(moved).takeValue();
    auto pending = eve::rts::OrderActionSystem::step(first, adapter);
    REQUIRE(pending.ok());
    std::move(pending).takeValue();
    CHECK(!unit->orders()->values.empty());
    CHECK_EQ(runtime.executionCount(), 1u);

    const eve::SimulationStep second{eve::SimulationTick{2}, eve::Duration::fromSeconds(1.0).expect("second dt")};
    auto completed = eve::rts::MotionSystem::step(second);
    REQUIRE(completed.ok());
    std::move(completed).takeValue();
    auto action = eve::rts::OrderActionSystem::step(second, adapter);
    REQUIRE(action.ok());
    std::move(action).takeValue();
    CHECK(unit->orders()->values.empty());

    unit->release();
}

TEST_CASE("rts.buildingProductionAndModuleFactoryCompose") {
    eve::rts::RTS module;
    auto unitResult = module.newUnit(subject("00000000-0000-7000-8000-000000000011"));
    REQUIRE(unitResult.ok());
    Unit* unit = std::move(unitResult).takeValue();
    CHECK_EQ(module.unitCount(), 1u);

    auto buildingResult = module.newBuilding(subject("00000000-0000-7000-8000-000000000012"));
    REQUIRE(buildingResult.ok());
    Building* building = std::move(buildingResult).takeValue();
    CHECK_EQ(module.buildingCount(), 1u);

    auto duration = eve::Duration::fromSeconds(1.0);
    REQUIRE(duration.ok());
    auto task = building->production()->values.enqueue("faction", "train", "worker",
                                                       std::move(duration).takeValue());
    REQUIRE(task.ok());
    std::move(task).takeValue();

    eve::action::ActionRuntime runtime;
    eve::rts::ActionAdapter adapter(runtime);
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(500000000)};
    auto processed = module.step(step, adapter);
    REQUIRE(processed.ok());
    CHECK(processed.value() >= 2u);
    CHECK_EQ(building->production()->values.taskCount(), 1u);

    (void)unit;
}
