#include <cmath>
#include "rts/RTSTerrainTraceInternal.h"
#include "zeroerr/assert.h"

#include "common/Capability.h"
#include "common/ECS.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "rts/RTS.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

}  // namespace

TEST_CASE("gameplay.control.rtsUsesSelectionAuthorityForPlayerAndAutomation") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;

    const auto playerSubject = subject("00000000-0000-7000-8000-000000000501");
    const auto unitSubject   = subject("00000000-0000-7000-8000-000000000502");
    auto       playerResult  = rts.newPlayer(playerSubject);
    auto       unitResult    = rts.newUnit(unitSubject, action("unit:worker"));
    REQUIRE(playerResult.ok());
    REQUIRE(unitResult.ok());
    auto* player = std::move(playerResult).takeValue();
    auto* unit   = std::move(unitResult).takeValue();
    player->selection()->units.push_back(ecs::handle_of(unit));

    bool discovered = false;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](eve::IGameplayControlProvider* provider) {
        if (provider == &rts && provider->gameplayDomain() == "rts") discovered = true;
    });
    CHECK(discovered);

    eve::GameplaySession playerSession{"player", eve::GameplayAccess::PlayerEquivalent, {playerSubject}};
    auto                 observed = rts.observeGameplay(playerSession, playerSubject);
    REQUIRE(observed.ok());
    const auto before  = std::move(observed).takeValue();
    auto       actions = rts.availableGameplayActions(playerSession, playerSubject, unitSubject);
    REQUIRE(actions.ok());
    const auto playerActions = std::move(actions).takeValue();
    REQUIRE_EQ(playerActions.size(), std::size_t{2});
    eve::GameplaySession automationSession{"automation", eve::GameplayAccess::TestDriver, {playerSubject}};
    auto automationActions = rts.availableGameplayActions(automationSession, playerSubject, unitSubject);
    REQUIRE(automationActions.ok());
    const auto automationDescriptors = std::move(automationActions).takeValue();
    REQUIRE_EQ(automationDescriptors.size(), playerActions.size());
    for (std::size_t i = 0; i < playerActions.size(); ++i) CHECK_EQ(automationDescriptors[i].id, playerActions[i].id);

    eve::GameplayCommand move;
    move.id               = "rts-player-command-1";
    move.action           = action("rts:move");
    move.subject          = unitSubject;
    move.observedTick     = before.tick;
    move.expectedRevision = before.revision;
    move.parameters       = eve::Value(eve::Value::Object{{"x", eve::Value(2.0)}, {"y", eve::Value(0.0)}});
    auto submitted        = rts.submitGameplay(playerSession, playerSubject, move);
    REQUIRE(submitted.ok());
    const auto receipt = std::move(submitted).takeValue();
    CHECK_EQ(receipt.commandId, move.id);
    CHECK(receipt.resultingRevision > before.revision);

    auto events = rts.gameplayEvents(playerSession, playerSubject, 0);
    REQUIRE(events.ok());
    const auto batch = std::move(events).takeValue();
    REQUIRE_EQ(batch.size(), std::size_t{1});
    CHECK_EQ(batch.front().causationCommandId, move.id);

    auto advanced = rts.advanceGameplay(playerSession, playerSubject,
                                        {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1000000000)});
    REQUIRE(advanced.ok());
    // 先取出再断言：CHECK_EQ 会求值两次，`std::move(x).takeValue()` 第二次读的是已移动的 Result。
    const auto advancedObservation = std::move(advanced).takeValue();
    CHECK_EQ(advancedObservation.tick.value(), std::uint64_t{1});
}

TEST_CASE("gameplay.control.rtsRejectsUnselectedAndStaleCommands") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    const auto       playerSubject = subject("00000000-0000-7000-8000-000000000511");
    const auto       unitSubject   = subject("00000000-0000-7000-8000-000000000512");
    auto             playerResult  = rts.newPlayer(playerSubject);
    auto             unitResult    = rts.newUnit(unitSubject);
    REQUIRE(playerResult.ok());
    REQUIRE(unitResult.ok());
    auto*                player = std::move(playerResult).takeValue();
    auto*                unit   = std::move(unitResult).takeValue();
    eve::GameplaySession session{"player", eve::GameplayAccess::PlayerEquivalent, {playerSubject}};

    auto unselected = rts.availableGameplayActions(session, playerSubject, unitSubject);
    CHECK(!unselected.ok());
    player->selection()->units.push_back(ecs::handle_of(unit));

    eve::GameplayCommand stale;
    stale.id               = "rts-stale";
    stale.action           = action("rts:move");
    stale.subject          = unitSubject;
    stale.observedTick     = eve::SimulationTick(0);
    stale.expectedRevision = 99;
    stale.parameters       = eve::Value(eve::Value::Object{{"x", eve::Value(1)}, {"y", eve::Value(0)}});
    auto rejected          = rts.submitGameplay(session, playerSubject, stale);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);
    CHECK(unit->orders()->values.empty());
}

TEST_CASE("gameplay.control.rtsEnumeratesItsPlayerInstances") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;

    const auto first  = subject("00000000-0000-7000-8000-0000000005a1");
    const auto second = subject("00000000-0000-7000-8000-0000000005a2");
    const auto one    = rts.newPlayer(first);
    const auto two    = rts.newPlayer(second);
    REQUIRE(one.ok());
    REQUIRE(two.ok());

    // 实例身份就是玩家的 subject：与 observe 解析用的键一致，按字典序稳定返回。
    const auto instances = rts.gameplayInstances();
    REQUIRE_EQ(instances.size(), std::size_t{2});
    CHECK_EQ(instances[0].format(), first.format());
    CHECK_EQ(instances[1].format(), second.format());

    // 目录能力让 `instances` 操作能枚举该领域，而不是落进 unenumerable。
    eve::IGameplayInstanceCatalog* catalog = nullptr;
    eve::cap::forEach<eve::IGameplayInstanceCatalog>([&](auto* candidate) {
        if (candidate != nullptr && candidate->gameplayDomain() == "rts") catalog = candidate;
    });
    REQUIRE(catalog != nullptr);
    CHECK_EQ(catalog->gameplayInstances().size(), std::size_t{2});

    auto scoped = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances","domain":"rts"})");
    REQUIRE(scoped.ok());
    CHECK(scoped.value().find(first.format()) != std::string::npos);
    CHECK(scoped.value().find(second.format()) != std::string::npos);

    auto all = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances"})");
    REQUIRE(all.ok());
    CHECK(all.value().find("\"domain\":\"rts\"") != std::string::npos);
    CHECK(all.value().find("\"unenumerable\":[\"rts\"]") == std::string::npos);
}

using eve::rts::terrain_internal::firstTerrainImpact;

TEST_CASE("rts.terrainNarrowRidgeBlocksShotsInBothDirections") {
    const auto ridge = [](int x, int y) { return x == 1 && y == 0 ? 3.0f : 0.0f; };
    // This ray crosses cell (1,0) for less than 0.02 world units.
    const auto forward = firstTerrainImpact(0.1f, 0.10f, 1.f, 2.9f, 2.88f, 1.f, 0.f, 0.f, 1.f, ridge);
    const auto reverse = firstTerrainImpact(2.9f, 2.88f, 1.f, 0.1f, 0.10f, 1.f, 0.f, 0.f, 1.f, ridge);
    REQUIRE(forward.has_value());
    REQUIRE(reverse.has_value());
    CHECK(*forward < 0.33f);
    CHECK(*reverse > 0.67f);
}

TEST_CASE("rts.terrainHighGroundCanShootOverLowerRidge") {
    const auto ridge = [](int x, int) { return x == 1 ? 2.f : 0.f; };
    REQUIRE(firstTerrainImpact(0.5f, 0.5f, 1.f, 3.5f, 0.5f, 1.f, 0.f, 0.f, 1.f, ridge).has_value());
    REQUIRE(!firstTerrainImpact(0.5f, 0.5f, 5.f, 3.5f, 0.5f, 1.f, 0.f, 0.f, 1.f, ridge).has_value());
}

TEST_CASE("rts.terrainDescendingProjectileHitsInsideCell") {
    const auto ground = [](int, int) { return 0.f; };
    const auto impact = firstTerrainImpact(0.1f, 0.5f, 1.f, 0.9f, 0.5f, -1.f, 0.f, 0.f, 1.f, ground);
    REQUIRE(impact.has_value());
    CHECK(std::abs(*impact - 0.5f) < 0.00001f);
    CHECK(!firstTerrainImpact(0.1f, 0.5f, 1.f, 0.9f, 0.5f, 0.f, 0.f, 0.f, 1.f, ground).has_value());
}

TEST_CASE("rts.terrainGridOriginScaleAndVerticalSegments") {
    const auto ridge  = [](int x, int y) { return x == 1 && y == 0 ? 3.f : 0.f; };
    const auto impact = firstTerrainImpact(-9.f, 5.f, 1.f, -3.f, 5.f, 1.f, -10.f, 4.f, 2.f, ridge);
    REQUIRE(impact.has_value());
    CHECK(std::abs(*impact - 1.f / 6.f) < 0.00001f);
    const auto ground = [](int, int) { return 0.f; };
    REQUIRE(firstTerrainImpact(1.f, 1.f, 1.f, 1.f, 1.f, -1.f, 0.f, 0.f, 1.f, ground).has_value());
    CHECK(!firstTerrainImpact(1.f, 1.f, 1.f, 1.f, 1.f, 2.f, 0.f, 0.f, 1.f, ground).has_value());
}

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
