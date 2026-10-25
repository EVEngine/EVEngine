// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "gameplay_control_rts.cpp"
  ]
}
*/
// clang-format on

#include "common/Capability.h"
#include "common/ECS.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "rts/RTS.h"
#include "rts/RTSTerrainTraceInternal.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace {
eve::SubjectRef subject(const char* text) {
    auto parsed = eve::PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

eve::LogicalId action(const char* text) {
    auto parsed = eve::LogicalId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

// Discovery contract: the provider advertises the RTS domain, and the same
// player subject is discoverable through both native and JSON catalogues.
TEST_CASE("rts.control.spec.provider_and_instance_discovery") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    const auto       first  = subject("00000000-0000-7000-8000-0000000005a1");
    const auto       second = subject("00000000-0000-7000-8000-0000000005a2");
    REQUIRE(rts.newPlayer(first).ok());
    REQUIRE(rts.newPlayer(second).ok());
    bool foundProvider = false;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* provider) {
        if (provider == &rts && provider->gameplayDomain() == "rts") foundProvider = true;
    });
    CHECK(foundProvider);
    const auto instances = rts.gameplayInstances();
    REQUIRE_EQ(instances.size(), std::size_t{2});
    CHECK_EQ(instances[0].format(), first.format());
    CHECK_EQ(instances[1].format(), second.format());
    eve::IGameplayInstanceCatalog* catalog = nullptr;
    eve::cap::forEach<eve::IGameplayInstanceCatalog>([&](auto* candidate) {
        if (candidate && candidate->gameplayDomain() == "rts") catalog = candidate;
    });
    REQUIRE(catalog != nullptr);
    CHECK_EQ(catalog->gameplayInstances().size(), std::size_t{2});
    auto scoped = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances","domain":"rts"})");
    REQUIRE(scoped.ok());
    CHECK(scoped.value().find(first.format()) != std::string::npos);
    CHECK(scoped.value().find(second.format()) != std::string::npos);
}

// Access contract: player and test-driver sessions see the same selected-unit
// actions. A valid move returns a revisioned receipt and caused event.
TEST_CASE("rts.control.spec.selected_unit_move_receipt_and_event") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    const auto       playerId     = subject("00000000-0000-7000-8000-000000000501");
    const auto       unitId       = subject("00000000-0000-7000-8000-000000000502");
    auto             playerResult = rts.newPlayer(playerId);
    auto             unitResult   = rts.newUnit(unitId, action("unit:worker"));
    REQUIRE(playerResult.ok());
    REQUIRE(unitResult.ok());
    auto* player = std::move(playerResult).takeValue();
    auto* unit   = std::move(unitResult).takeValue();
    player->selection()->units.push_back(ecs::handle_of(unit));
    eve::GameplaySession playerSession{"player", eve::GameplayAccess::PlayerEquivalent, {playerId}};
    eve::GameplaySession driverSession{"driver", eve::GameplayAccess::TestDriver, {playerId}};
    auto                 before        = rts.observeGameplay(playerSession, playerId);
    auto                 playerActions = rts.availableGameplayActions(playerSession, playerId, unitId);
    auto                 driverActions = rts.availableGameplayActions(driverSession, playerId, unitId);
    REQUIRE(before.ok());
    REQUIRE(playerActions.ok());
    REQUIRE(driverActions.ok());
    REQUIRE_EQ(playerActions.value().size(), std::size_t{2});
    REQUIRE_EQ(driverActions.value().size(), playerActions.value().size());
    for (std::size_t i = 0; i < playerActions.value().size(); ++i)
        CHECK_EQ(driverActions.value()[i].id, playerActions.value()[i].id);
    eve::GameplayCommand command;
    command.id               = "rts-move-1";
    command.action           = action("rts:move");
    command.subject          = unitId;
    command.observedTick     = before.value().tick;
    command.expectedRevision = before.value().revision;
    command.parameters       = eve::Value(eve::Value::Object{{"x", 2.0}, {"y", 0.0}});
    auto submitted           = rts.submitGameplay(playerSession, playerId, command);
    REQUIRE(submitted.ok());
    const auto receipt = std::move(submitted).takeValue();
    CHECK_EQ(receipt.commandId, command.id);
    CHECK(receipt.resultingRevision > before.value().revision);
    auto events = rts.gameplayEvents(playerSession, playerId, 0);
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t{1});
    CHECK_EQ(events.value()[0].causationCommandId, command.id);
    auto advanced = rts.advanceGameplay(playerSession, playerId,
                                        {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1000000000)});
    REQUIRE(advanced.ok());
    const auto next = std::move(advanced).takeValue();
    CHECK_EQ(next.tick.value(), std::uint64_t{1});
}

// Rejection contract: an unselected unit is invisible to available-actions,
// and a stale revision cannot enqueue any order or alter the instance.
TEST_CASE("rts.control.spec.unselected_and_stale_commands") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    const auto       playerId     = subject("00000000-0000-7000-8000-000000000511");
    const auto       unitId       = subject("00000000-0000-7000-8000-000000000512");
    auto             playerResult = rts.newPlayer(playerId);
    auto             unitResult   = rts.newUnit(unitId);
    REQUIRE(playerResult.ok());
    REQUIRE(unitResult.ok());
    auto*                player = std::move(playerResult).takeValue();
    auto*                unit   = std::move(unitResult).takeValue();
    eve::GameplaySession session{"player", eve::GameplayAccess::PlayerEquivalent, {playerId}};
    CHECK(!rts.availableGameplayActions(session, playerId, unitId).ok());
    player->selection()->units.push_back(ecs::handle_of(unit));
    eve::GameplayCommand stale;
    stale.id               = "rts-stale";
    stale.action           = action("rts:move");
    stale.subject          = unitId;
    stale.observedTick     = eve::SimulationTick(0);
    stale.expectedRevision = 99;
    stale.parameters       = eve::Value(eve::Value::Object{{"x", 1}, {"y", 0}});
    auto rejected          = rts.submitGameplay(session, playerId, stale);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);
    CHECK(unit->orders()->values.empty());
}

using eve::rts::terrain_internal::firstTerrainImpact;

// Terrain contract: even a very short cell crossing observes a blocking ridge
// in either direction; a high launch point can clear the same ridge.
TEST_CASE("rts.terrain.spec_short_crossing_and_height") {
    const auto ridge   = [](int x, int y) { return x == 1 && y == 0 ? 3.f : 0.f; };
    const auto forward = firstTerrainImpact(0.1f, 0.10f, 1.f, 2.9f, 2.88f, 1.f, 0.f, 0.f, 1.f, ridge);
    const auto reverse = firstTerrainImpact(2.9f, 2.88f, 1.f, 0.1f, 0.10f, 1.f, 0.f, 0.f, 1.f, ridge);
    REQUIRE(forward.has_value());
    REQUIRE(reverse.has_value());
    CHECK(*forward < 0.33f);
    CHECK(*reverse > 0.67f);
    const auto lower = [](int x, int) { return x == 1 ? 2.f : 0.f; };
    CHECK(firstTerrainImpact(0.5f, 0.5f, 1.f, 3.5f, 0.5f, 1.f, 0.f, 0.f, 1.f, lower).has_value());
    CHECK(!firstTerrainImpact(0.5f, 0.5f, 5.f, 3.5f, 0.5f, 1.f, 0.f, 0.f, 1.f, lower).has_value());
}

// Crossing contract: descending shots intersect within the cell. World origin
// and cell size transform the same grid index, including vertical segments.
TEST_CASE("rts.terrain.spec_fractional_impact_and_grid_transform") {
    const auto ground = [](int, int) { return 0.f; };
    auto       descending = firstTerrainImpact(0.1f, 0.5f, 1.f, 0.9f, 0.5f, -1.f, 0.f, 0.f, 1.f, ground);
    REQUIRE(descending.has_value());
    CHECK(std::abs(*descending - .5f) < 1e-5f);
    CHECK(!firstTerrainImpact(0.1f, 0.5f, 1.f, 0.9f, 0.5f, 0.f, 0.f, 0.f, 1.f, ground).has_value());
    const auto ridge       = [](int x, int y) { return x == 1 && y == 0 ? 3.f : 0.f; };
    auto       transformed = firstTerrainImpact(-9.f, 5.f, 1.f, -3.f, 5.f, 1.f, -10.f, 4.f, 2.f, ridge);
    REQUIRE(transformed.has_value());
    CHECK(std::abs(*transformed - 1.f / 6.f) < 1e-5f);
    CHECK(firstTerrainImpact(1.f, 1.f, 1.f, 1.f, 1.f, -1.f, 0.f, 0.f, 1.f, ground).has_value());
    CHECK(!firstTerrainImpact(1.f, 1.f, 1.f, 1.f, 1.f, 2.f, 0.f, 0.f, 1.f, ground).has_value());
}
}  // namespace


#include "rts/RTSTypes.h"
namespace test_rts_performance_semantics_migrated {
TEST_CASE("rts.performanceOrderProjectionInvalidatesOnLifecycleAndSameIdRestore") {
    eve::rts::OrderComponent orders;
    eve::rts::CommandSpec    first;
    first.kind   = eve::rts::OrderKind::AttackMove;
    first.target = {3.f, 4.f};
    REQUIRE(orders.enqueue(first, 5).ok());
    for (int i = 0; i < 3; ++i) {
        auto current = orders.current();
        REQUIRE(current.ok());
        CHECK_EQ(current.value().target.x, 3.f);
        CHECK_EQ(current.value().formationSlot, 5);
    }
    eve::rts::OrderComponent restored;
    first.target = {7.f, 8.f};
    REQUIRE(restored.enqueue(first, 9).ok());
    auto snapshot = restored.snapshotState();
    REQUIRE(snapshot.ok());
    REQUIRE(orders.restoreState(snapshot.value()).ok());
    auto current = orders.current();
    REQUIRE(current.ok());
    CHECK_EQ(current.value().target.x, 7.f);
    CHECK_EQ(current.value().formationSlot, 9);
    REQUIRE(orders.restore(snapshot.value().queueJson).ok());
    current = orders.current();
    REQUIRE(current.ok());
    CHECK_EQ(current.value().target.x, 7.f);
    eve::rts::OrderComponent copied(orders);
    first.target = {11.f, 12.f};
    REQUIRE(copied.enqueue(first, 2).ok());
    REQUIRE(copied.complete(copied.current().expect("active").id).ok());
    current = copied.current();
    REQUIRE(current.ok());
    CHECK_EQ(current.value().target.x, 11.f);
    copied.clear();
    REQUIRE(copied.enqueue(first, 3).ok());
    const auto afterClear = copied.current().expect("after clear");
    CHECK_EQ(afterClear.formationSlot, 3);
    REQUIRE(copied.replace(first, 4).ok());
    const auto afterReplace = copied.current().expect("after replace");
    CHECK_EQ(afterReplace.formationSlot, 4);
}
}  // namespace test_rts_performance_semantics_migrated
