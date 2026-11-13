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

#include "combat/DamageCapture.h"
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "definitions/DefinitionRegistryCapture.h"
#include "definitions/Definitions.h"
#include "rts/RTS.h"
#include "rts/RTSTerrainTraceInternal.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
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
    const auto frozenEvents = rts.captureFrameState().expect("native gameplay queue freeze");
    REQUIRE_EQ(frozenEvents.gameplay.size(), std::size_t(1));
    REQUIRE_EQ(frozenEvents.nextGameplaySequence, std::uint64_t(2));
    REQUIRE_EQ(frozenEvents.gameplay[0].causationCommandId, command.id);
    {
        auto view = ecs::View<eve::rts::Unit>(world);
        REQUIRE(!rts.captureFrameState().ok());
    }
    auto events = rts.gameplayEvents(playerSession, playerId, 0);
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t{1});
    CHECK_EQ(events.value()[0].causationCommandId, command.id);
    auto advanced = rts.advanceGameplay(playerSession, playerId,
                                        {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1000000000)});
    REQUIRE(advanced.ok());
    const auto next = std::move(advanced).takeValue();
    CHECK_EQ(next.tick.value(), std::uint64_t{1});
    REQUIRE_EQ(frozenEvents.gameplay[0].causationCommandId, command.id);
    REQUIRE_EQ(rts.captureFrameState().expect("native queue was not consumed").gameplay.size(), std::size_t(1));
    player->selection()->revision = UINT64_MAX;
    const auto queue              = unit->orders()->values.snapshot().expect("queue before exhaustion");
    const auto xBefore            = unit->motion()->x;
    REQUIRE(!rts.advanceGameplay(playerSession, playerId, {eve::SimulationTick(2), eve::Duration(1000000000)}).ok());
    CHECK_EQ(player->selection()->tick.value(), std::uint64_t(1));
    CHECK_EQ(unit->motion()->x, xBefore);
    command.id               = "exhausted-revision";
    command.observedTick     = eve::SimulationTick(1);
    command.expectedRevision = UINT64_MAX;
    REQUIRE(!rts.submitGameplay(playerSession, playerId, command).ok());
    CHECK_EQ(player->selection()->revision, UINT64_MAX);
    CHECK_EQ(unit->orders()->values.snapshot().expect("intact queue"), queue);
    CHECK_EQ(rts.captureFrameState().expect("intact events").gameplay.size(), std::size_t(1));
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

namespace test_rts_snapshot_readonly {
TEST_CASE("rts.snapshot.canonicalRootsFreezeWithoutMaterializingAndIgnoreUnrelatedCurrentScope") {
    auto             world = std::make_shared<ecs::Table>();
    eve::rts::RTS    domain(world);
    ecs::ScopedTable actualOwner(*world);
    const auto       base = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000a01");
    auto             id   = [&](const char* name) { return eve::SubjectRef::fromPersistentId(base.child(name)); };
    {
        ecs::ScopedTable scope(*world);
        domain.newUnit(id("unit")).expect("native unit");
        domain.newBuilding(id("building")).expect("native building");
        domain.newResourceNode(id("node"), "ore", 10, {1, 2}).expect("native resource");
        domain.newPlayer(id("player")).expect("native player");
        domain.newFaction(id("faction")).expect("native faction");
        domain.newMatch(id("match")).expect("native match");
    }
    auto*      unit    = domain.findUnit(id("unit"));
    const auto buffers = unit->getComponentManager().components.size();
    auto       before  = domain.canonicalStateJson().expect("native initial state");
    {
        ecs::Table       unrelated;
        ecs::ScopedTable unrelatedScope(unrelated);
        auto             snapshot = domain.snapshotState().expect("actual owning Table selected");
        REQUIRE_EQ(snapshot.units.size(), std::size_t(1));
        REQUIRE_EQ(snapshot.buildings.size(), std::size_t(1));
        REQUIRE_EQ(snapshot.resourceNodes.size(), std::size_t(1));
        REQUIRE_EQ(snapshot.players.size(), std::size_t(1));
        REQUIRE_EQ(snapshot.factions.size(), std::size_t(1));
        REQUIRE_EQ(snapshot.matches.size(), std::size_t(1));
        REQUIRE(snapshot.units[0].attributes.owner.table == nullptr);
        REQUIRE_EQ(domain.canonicalStateJson().expect("same actual state"), before);
    }
    REQUIRE_EQ(unit->getComponentManager().components.size(), buffers);
    {
        auto view = ecs::View<eve::rts::Unit>(*world);
        REQUIRE(!domain.snapshotState().ok());
    }
}
TEST_CASE("rts.snapshot.missingRootComponentsAndAttributesRejectWithoutRepair") {
    auto             world = std::make_shared<ecs::Table>();
    ecs::ScopedTable scope(*world);
    eve::rts::RTS    domain(world);
    const auto       id     = subject("01900000-0000-7000-8000-000000000a02");
    auto*            unit   = domain.newUnit(id).expect("native root");
    auto*            motion = unit->getComponentManager().getComponentBuffer<eve::rts::Unit::Motion>();
    motion->reset_slot(unit->id);
    const auto buffers = unit->getComponentManager().components.size();
    REQUIRE(!domain.snapshotState().ok());
    REQUIRE(!motion->has(unit->id));
    REQUIRE_EQ(unit->getComponentManager().components.size(), buffers);
    (void)*unit->motion();
    auto* attributes = unit->getComponentManager().getComponentBuffer<eve::rts::Unit::Attributes>();
    attributes->reset_slot(unit->id);
    REQUIRE(!domain.snapshotState().ok());
    REQUIRE(!eve::rts::RTSUnitAttributeAdapter::snapshot(*unit).ok());
    REQUIRE(!attributes->has(unit->id));
    (void)*unit->attributes();
    REQUIRE(!domain.snapshotState().ok());
    REQUIRE(!unit->attributes()->values.initialized());
    eve::rts::RTSUnitAttributeAdapter::ensure(*unit).expect("explicit repair");
    REQUIRE(domain.snapshotState().ok());
}
TEST_CASE("rts.snapshot.foreignAttributesAndMissingFactionIdentityRemainUnchangedOnFailure") {
    auto             world = std::make_shared<ecs::Table>();
    ecs::ScopedTable scope(*world);
    eve::rts::RTS    domain(world);
    const auto       base       = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000a03");
    auto             id         = [&](const char* name) { return eve::SubjectRef::fromPersistentId(base.child(name)); };
    auto*            first      = domain.newUnit(id("first")).expect("first native root");
    auto*            second     = domain.newUnit(id("second")).expect("second native root");
    first->attributes()->values = second->attributes()->values;
    REQUIRE(!domain.snapshotState().ok());
    REQUIRE(!eve::rts::RTSUnitAttributeAdapter::snapshot(*first).ok());
    const std::array<std::string_view, 1> names{"health"};
    auto raw = first->attributes()->values.snapshot(names).expect("observe existing wrong owner");
    REQUIRE(ecs::try_get(raw.owner) == second);
    first->attributes()->values = eve::rts::AttributeComponent{};
    eve::rts::RTSUnitAttributeAdapter::ensure(*first).expect("explicit owner rebuild");
    auto*      faction    = domain.newFaction(id("faction")).expect("native faction");
    auto*      identities = faction->getComponentManager().getComponentBuffer<eve::rts::Faction::Identity>();
    const auto prior      = *faction->identity();
    identities->reset_slot(faction->id);
    REQUIRE(!domain.snapshotState().ok());
    REQUIRE(!identities->has(faction->id));
    *faction->identity() = prior;
    REQUIRE(domain.snapshotState().ok());
}
}  // namespace test_rts_snapshot_readonly

TEST_CASE("rts.snapshot.definitionCaptureRequiresActualOwnedContentAuthorityAndIdleWorld") {
    eve::definitions::DefinitionRegistry content, foreign;
    content.insert("unit", "fighter", 1, R"({"health":77})").expect("actual bound content");
    foreign.insert("unit", "fighter", 1, R"({"health":1})").expect("different authority");
    auto          table = std::make_shared<ecs::Table>();
    eve::rts::RTS domain(table);
    REQUIRE(!domain.captureDefinitions(content).ok());
    domain.setDefinitionRegistry(&content);
    REQUIRE(!domain.captureDefinitions(foreign).ok());
    const auto before = content.snapshotJson();
    {
        auto view = ecs::View<eve::rts::Unit>(*table);
        REQUIRE(!domain.captureDefinitions(content).ok());
    }
    auto captured = domain.captureDefinitions(content).takeValue();
    REQUIRE_EQ(content.snapshotJson(), before);
    domain.setDefinitionRegistry(nullptr);
    auto restored = eve::definitions::DefinitionRegistry::prepareRestored(captured).takeValue();
    REQUIRE_EQ(restored->snapshotJson(), before);
    eve::rts::RTS borrowed;
    borrowed.setDefinitionRegistry(&content);
    REQUIRE(!borrowed.captureDefinitions(content).ok());
}

TEST_CASE("rts.snapshot.damageCaptureRequiresTheActualBoundGameServiceAndIdleOwnedWorld") {
    eve::combat::DamageRuntime service, foreign;
    auto                       table = std::make_shared<ecs::Table>();
    eve::rts::RTS              domain(table);
    REQUIRE(!domain.captureDamage(service).ok());
    domain.setCombatProviders(nullptr, &service);
    REQUIRE(!domain.captureDamage(foreign).ok());
    {
        auto view = ecs::View<eve::rts::Unit>(*table);
        REQUIRE(!domain.captureDamage(service).ok());
    }
    auto captured = domain.captureDamage(service).takeValue();
    domain.setCombatProviders(nullptr, nullptr);
    REQUIRE(eve::combat::DamageRuntime::prepareRestored(captured).ok());
    eve::rts::RTS borrowed;
    borrowed.setCombatProviders(nullptr, &service);
    REQUIRE(!borrowed.captureDamage(service).ok());
}
