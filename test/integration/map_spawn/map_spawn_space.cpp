// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"map_spawn"}
#include <array>
#include <limits>
#include "MapSpawnSpaceFixture.h"
#include "common/ScriptProvider.h"
#include "graphics/RenderSystem.h"
#include "map/TileRenderViewport.h"
#include "map/TileSystem.h"
#include "map/spawn/MapSpawnLoad.h"
#include "spawn/SpawnCheckpoint.h"
#include "spawn/SpawnRestoreState.h"
namespace test_map_spawn_space {
TEST_CASE("Map script seam remains declared until the final native map identity is retired") {
    auto& registry = eve::ScriptProviderRegistry::instance();
    REQUIRE(!registry.hasCapabilitySeam(eve::map::ITileCollisionSink::capabilityName));
    for (const bool firstLeavesFirst : {true, false}) {
        auto first  = std::make_unique<eve::map::Map>();
        auto second = std::make_unique<eve::map::Map>();
        if (firstLeavesFirst)
            first.reset();
        else
            second.reset();
        CHECK(registry.hasCapabilitySeam(eve::map::ITileCollisionSink::capabilityName));
        if (firstLeavesFirst)
            second.reset();
        else
            first.reset();
        CHECK(!registry.hasCapabilitySeam(eve::map::ITileCollisionSink::capabilityName));
    }
}

TEST_CASE("Map spatial factory preserves real RPG creation replay and cleanup contracts") {
    {
        Fixture fixture;
        spawn_factory_contract::creationAndReplay(fixture);
    }
    {
        Fixture fixture;
        spawn_factory_contract::removal(fixture, eve::spawn::DetachPolicy::RemoveUnits);
    }
    {
        Fixture fixture;
        spawn_factory_contract::sourceRevocation(fixture);
    }
}
TEST_CASE("Map spatial commit rechecks changed terrain without publishing any RPG actor") {
    Fixture f;
    f.space->resetTrace(eve::map_spawn::PlacementTraceMode::RecentAttempts);
    auto  prepared = f.factory->prepare(f.request, f.positions).expect("prepared real batch");
    auto& token    = std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(prepared);
    f.layer->setTile(5, 5, 0);
    f.space->setNavigation(f.navigation.captureNavigationGrid().expect("changed grid")).expect("refresh");
    REQUIRE(token->commit().expect("normal late blocking") == eve::spawn::CommitOutcome::PlacementBlocked);
    CHECK_EQ(f.count(), std::size_t(0));
    const auto blockedTrace = f.space->captureTrace();
    REQUIRE(blockedTrace.attempts.back().stage == eve::map_spawn::PlacementStage::FinalCheck);
    REQUIRE(blockedTrace.attempts.back().decision == eve::map_spawn::PlacementDecision::Walkability);
    f.layer->setTile(5, 5, 1);
    f.space->setNavigation(f.navigation.captureNavigationGrid().expect("unblocked grid")).expect("refresh");
    REQUIRE(token->commit().expect("same hidden batch") == eve::spawn::CommitOutcome::Committed);
    CHECK_EQ(f.count(), std::size_t(1));
}
TEST_CASE("Map spatial plan respects players and live actor occupancy with bounded rejection") {
    Fixture                        f;
    const auto&                    instance = f.map->instances()[0];
    auto                           random   = std::uint64_t(42);
    std::vector<eve::spawn::Point> player{{5, 5}};
    f.space->setPlayers(player).expect("player occupies camp");
    auto blocked = f.space->plan(f.request, instance.revision->definition().placement, instance.region, random);
    REQUIRE(blocked.ok());
    CHECK(std::holds_alternative<eve::spawn::PlacementBlocked>(blocked.value()));
    f.space->setPlayers({}).expect("player left");
    auto prepared = f.factory->prepare(f.request, f.positions).expect("clear camp");
    REQUIRE(std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(prepared)->commit().expect("actor created") ==
            eve::spawn::CommitOutcome::Committed);
    auto occupied = f.space->plan(f.request, instance.revision->definition().placement, instance.region, random);
    REQUIRE(occupied.ok());
    CHECK(std::holds_alternative<eve::spawn::PlacementBlocked>(occupied.value()));
    CHECK_EQ(f.occupancy->capture().expect("actual domain capture").size(), std::size_t(1));
    {
        auto                                                 view = ecs::View<eve::rpg::RPGActor>(*f.table);
        ecs::ComponentRef<eve::rpg_spawn::SpawnedActorState> state(view.begin().entity());
        state->position = {8, 8};
    }
    auto moved = f.space->plan(f.request, instance.revision->definition().placement, instance.region, random);
    REQUIRE(moved.ok());
    CHECK(std::holds_alternative<std::vector<eve::spawn::Point>>(moved.value()));
}
TEST_CASE("Map placement trace observes real decisions without changing random draws and bounds retained history") {
    Fixture f;
    using namespace eve::map_spawn;
    const auto& instance    = f.map->instances()[0];
    const auto& spec        = instance.revision->definition().placement;
    auto        random      = std::uint64_t(42);
    auto        plainRandom = random;
    auto        plain = f.space->plan(f.request, spec, instance.region, plainRandom).expect("untraced actual plan");
    REQUIRE(f.space->captureTrace().attempts.empty());
    f.space->resetTrace(PlacementTraceMode::RecentAttempts);
    auto observed = f.space->plan(f.request, spec, instance.region, random).expect("same traced plan");
    REQUIRE_EQ(random, plainRandom);
    REQUIRE_EQ(plain.index(), observed.index());
    auto accepted = f.space->captureTrace();
    REQUIRE_EQ(accepted.attempts.size(), std::size_t(1));
    REQUIRE(accepted.attempts[0].decision == PlacementDecision::Accepted);
    REQUIRE(accepted.attempts[0].source == f.request.source);
    REQUIRE(accepted.attempts[0].operation == f.request.operation);
    REQUIRE(accepted.attempts[0].subject == f.request.units[0].subject);
    REQUIRE_EQ(accepted.attempts[0].point->x, 5.0);
    std::vector<eve::spawn::Point> player{{5, 5}};
    f.space->setPlayers(player).expect("real player projection");
    f.space->resetTrace(PlacementTraceMode::RecentAttempts);
    for (int i = 0; i < 100; ++i) {
        auto blocked = f.space->plan(f.request, spec, instance.region, random).expect("bounded blocked plan");
        REQUIRE(std::holds_alternative<eve::spawn::PlacementBlocked>(blocked));
    }
    const auto retained = f.space->captureTrace();
    REQUIRE_EQ(retained.attempts.size(), std::size_t(256));
    REQUIRE_EQ(retained.dropped, std::uint64_t(44));
    REQUIRE_EQ(retained.attempts.front().attempt, std::uint32_t(3));
    for (const auto& row : retained.attempts) REQUIRE(row.decision == PlacementDecision::PlayerDistance);
    f.space->setPlayers({}).expect("clear players");
    std::vector<eve::spawn::Point> outside{{8, 8}};
    f.space->check(f.request, outside).expect("normal final region blocking");
    REQUIRE(f.space->captureTrace().attempts.back().decision == PlacementDecision::OutsideRegion);
    REQUIRE(f.space->captureTrace().attempts.back().stage == PlacementStage::FinalCheck);
    f.layer->setTile(5, 5, 0);
    f.space->setNavigation(f.navigation.captureNavigationGrid().expect("blocked grid")).expect("install grid");
    f.space->plan(f.request, spec, instance.region, random).expect("walkability blocking");
    REQUIRE(f.space->captureTrace().attempts.back().decision == PlacementDecision::Walkability);
    f.layer->setTile(5, 5, 1);
    f.layer->setTile(4, 5, 0);
    f.space->setNavigation(f.navigation.captureNavigationGrid().expect("clearance grid")).expect("install grid");
    f.space->check(f.request, f.positions).expect("terrain clearance blocking");
    REQUIRE(f.space->captureTrace().attempts.back().decision == PlacementDecision::TerrainClearance);
    f.layer->setTile(4, 5, 1);
    f.space->setNavigation(f.navigation.captureNavigationGrid().expect("clear grid")).expect("install grid");
    auto batch = f.request;
    batch.units.push_back({f.request.lifetime.unitIdentity(f.request.ordinal, 1).expect("second identity"),
                           f.request.units[0].definition});
    f.space->plan(batch, spec, instance.region, random).expect("batch collision");
    REQUIRE(f.space->captureTrace().attempts.back().decision == PlacementDecision::BatchClearance);
    auto token = f.factory->prepare(f.request, f.positions).expect("prepare actual actor");
    std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(token)->commit().expect("publish actual actor");
    f.space->plan(f.request, spec, instance.region, random).expect("domain occupancy blocking");
    REQUIRE(f.space->captureTrace().attempts.back().decision == PlacementDecision::Occupied);
    f.space->resetTrace(PlacementTraceMode::Disabled);
    REQUIRE(f.space->captureTrace().attempts.empty());
    REQUIRE_EQ(retained.attempts.size(), std::size_t(256));
    REQUIRE(accepted.attempts[0].decision == PlacementDecision::Accepted);
    REQUIRE_EQ(f.count(), std::size_t(1));
}
TEST_CASE("Map spatial admission rejects missing required capabilities and expired domain projection") {
    Fixture                       f;
    const auto&                   instance = f.map->instances()[0];
    eve::map_spawn::MapSpawnSpace absent(*f.map, f.owner, {}, nullptr);
    REQUIRE(!absent.validate(instance.revision->definition().placement, instance.region).ok());
    absent.setNavigation(f.navigation.captureNavigationGrid().expect("grid")).expect("navigation");
    absent.setPlayers({}).expect("player frame");
    CHECK(!absent.validate(instance.revision->definition().placement, instance.region).ok());
    eve::map_spawn::MapSpawnSpace noPlayers(*f.map, f.owner, {}, f.occupancy);
    noPlayers.setNavigation(f.navigation.captureNavigationGrid().expect("grid")).expect("navigation");
    CHECK(!noPlayers.validate(instance.revision->definition().placement, instance.region).ok());
    {
        ecs::ScopedTable scope(*f.table);
        auto*            actor = eve::rpg::RPGActor::createActor();
        REQUIRE(actor != nullptr);
        CHECK(!f.occupancy->capture().ok());
        ecs::DestroyEntity(actor);
    }
    {
        Templates simple(*f.units);
        simple.footprints = false;
        auto asset        = eve::Value::fromJson(f.map->canonicalJson()).expect("sidecar");
        auto map          = eve::spawn::compileMap(asset, simple, *f.units).expect("walk-only content");
        auto owner        = f.owner.child("walk-only");
        auto space =
            std::make_shared<eve::map_spawn::MapSpawnSpace>(map, owner, eve::map_spawn::SpawnGridTransform{}, nullptr);
        space->setNavigation(f.navigation.captureNavigationGrid().expect("grid")).expect("navigation");
        auto factory   = std::make_shared<eve::map_spawn::SpatialSpawnFactory>(f.domain, space);
        auto providers = std::make_shared<eve::spawn::SpawnProviders>(eve::spawn::SpawnProviders{space, factory});
        eve::spawn::SpawnWorld world(owner, 42);
        auto                   sources = world.attach(map, providers).expect("no occupancy/player provider required");
        REQUIRE(world.advance({eve::SimulationTick(1), eve::Duration(0)}).expect("walk-only spawn").units == 1);
        REQUIRE(world.detach(sources[0], eve::spawn::DetachPolicy::RemoveUnits).ok());
    }
    f.table.reset();
    CHECK(!f.occupancy->capture().ok());
}
TEST_CASE("Map spatial world creates real RPG actors and replaces a confirmed death") {
    Fixture f;
    auto    first = f.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("real spatial step");
    REQUIRE(first.units == 1);
    REQUIRE(f.count() == 1);
    ecs::ScopedTable    scope(*f.table);
    eve::rpg::RPGActor* actor = nullptr;
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*f.table);
        actor     = view.begin().entity();
    }
    eve::rpg::VitalsSystem::takeDamage(actor, "hp", 100);
    std::vector<eve::rpg::VitalsEvent> events;
    eve::rpg::VitalsSystem::pollEvents(events);
    ecs::DestroyEntity(actor);
    auto facts = f.domain->collectLifecycle(events).expect("death bridge");
    REQUIRE(facts.size() == 1);
    f.sourceWorld->advance({eve::SimulationTick(2), eve::Duration(0)}, facts).expect("death accounting");
    auto replacement = f.sourceWorld->advance({eve::SimulationTick(3), eve::Duration(1000000000)}).expect("cooldown");
    REQUIRE(replacement.units == 1);
    CHECK_EQ(f.count(), std::size_t(1));
    CHECK_EQ(f.sourceWorld->inspect(f.request.source).expect("ledger").total, std::uint64_t(2));
}

TEST_CASE("Map spawn asset transaction derives new map navigation before real RPG publication") {
    Fixture f;
    f.layer->fill(0);
    const auto countLayers = [] {
        std::size_t n    = 0;
        auto        view = ecs::View<eve::map::TileLayer>();
        for (auto it = view.begin(); it != view.end(); ++it) ++n;
        return n;
    };
    const auto before = countLayers();
    REQUIRE(!f.prepareAssets(f.mapModule, false).ok());
    REQUIRE(countLayers() == before);
    auto candidate = f.prepareAssets(f.mapModule).expect("fully composed new map");
    REQUIRE(countLayers() == before);
    CHECK_EQ(f.count(), std::size_t(0));
    auto session = candidate.commit(f.mapModule).expect("map and scheduler published");
    REQUIRE(f.mapModule.getLayer(0) != nullptr);
    CHECK(f.mapModule.getLayer(1) == nullptr);
    CHECK_EQ(f.mapModule.getLayer(0)->getTile(5, 5), 1);
    REQUIRE(session->advance(f.mapModule, {eve::SimulationTick(1), eve::Duration(0)})
                .expect("new map navigation spawn")
                .units == 1);
    CHECK_EQ(f.count(), std::size_t(1));
    REQUIRE(session->close(eve::spawn::DetachPolicy::RemoveUnits).ok());
    CHECK_EQ(f.count(), std::size_t(0));
}
TEST_CASE("Map spawn session rejects publication after its observed Map owner is destroyed") {
    Fixture f;
    auto    target    = std::make_unique<eve::map::Map>();
    auto    candidate = f.prepareAssets(*target).expect("composed candidate");
    auto    session   = candidate.commit(*target).expect("published map");
    target.reset();
    auto report = session->advance(f.mapModule, {eve::SimulationTick(1), eve::Duration(0)});
    REQUIRE(!report.ok());
    CHECK_EQ(f.count(), std::size_t(0));
    REQUIRE(session->close(eve::spawn::DetachPolicy::KeepUnits).ok());
}

TEST_CASE("Map scene blocks stale navigation and publishes an explicit current capture") {
    Fixture f;
    auto    candidate = f.prepareAssets(f.mapModule).expect("scene");
    auto    session   = candidate.commit(f.mapModule).expect("published scene");
    REQUIRE(!session->navigationNeedsRefresh(f.mapModule).expect("initial navigation"));
    auto* layer = f.mapModule.getLayer(0);
    REQUIRE(layer != nullptr);
    layer->setTile(5, 5, 0);
    REQUIRE(session->navigationNeedsRefresh(f.mapModule).expect("edited map"));
    REQUIRE(!session->advance(f.mapModule, {eve::SimulationTick(1), eve::Duration(0)}).ok());
    auto update = session->prepareNavigationRefresh(f.mapModule).expect("blocked capture");
    REQUIRE(update->commit(f.mapModule).expect("publish navigation") == eve::spawn::Change::Changed);
    auto blocked =
        session->advance(f.mapModule, {eve::SimulationTick(1), eve::Duration(0)}).expect("accepted blocked frame");
    CHECK_EQ(blocked.units, std::uint32_t(0));
    CHECK_EQ(f.count(), std::size_t(0));
    layer->setTile(5, 5, 1);
    auto stale = session->prepareNavigationRefresh(f.mapModule).expect("clear capture");
    layer->setTile(5, 5, 0);
    REQUIRE(!stale->commit(f.mapModule).ok());
    REQUIRE(session->navigationNeedsRefresh(f.mapModule).expect("stale token did not publish"));
    layer->setTile(5, 5, 1);
    auto current = session->prepareNavigationRefresh(f.mapModule).expect("current clear capture");
    REQUIRE(current->commit(f.mapModule).expect("clear update") == eve::spawn::Change::Changed);
    CHECK(current->commit(f.mapModule).expect("idempotent update") == eve::spawn::Change::Unchanged);
    REQUIRE(session->advance(f.mapModule, {eve::SimulationTick(2), eve::Duration(1000000000)})
                .expect("retry on clear grid")
                .units == 1);
    CHECK_EQ(f.count(), std::size_t(1));
    REQUIRE(session->close(eve::spawn::DetachPolicy::RemoveUnits).ok());
}
TEST_CASE("Map scene player projection rolls back invalid frames and blocks closed sessions") {
    Fixture                        f;
    auto                           candidate = f.prepareAssets(f.mapModule).expect("scene");
    auto                           session   = candidate.commit(f.mapModule).expect("published scene");
    std::vector<eve::spawn::Point> players{{5, 5}};
    REQUIRE(!session
                 ->advance(f.mapModule, {eve::SimulationTick(0), eve::Duration(0)}, {},
                           {std::span<const eve::spawn::Point>(players), {}})
                 .ok());
    REQUIRE(session->advance(f.mapModule, {eve::SimulationTick(1), eve::Duration(0)})
                .expect("prior empty player frame")
                .units == 1);
    REQUIRE(session->close(eve::spawn::DetachPolicy::RemoveUnits).ok());
    CHECK(!session->advance(f.mapModule, {eve::SimulationTick(2), eve::Duration(0)}).ok());
    CHECK(session->sourceHandles().empty());
}
TEST_CASE("Map navigation tokens reject source session destruction and coordinate frame changes") {
    Fixture f;
    auto    candidate = f.prepareAssets(f.mapModule).expect("scene");
    auto    session   = candidate.commit(f.mapModule).expect("published scene");
    auto    token     = session->prepareNavigationRefresh(f.mapModule).expect("capture");
    auto*   layer     = f.mapModule.getLayer(0);
    REQUIRE(layer != nullptr);
    layer->setOrigin(10, 0);
    REQUIRE(!session->navigationNeedsRefresh(f.mapModule).ok());
    CHECK(!token->commit(f.mapModule).ok());
    layer->setOrigin(0, 0);
    session.reset();
    CHECK(!token->commit(f.mapModule).ok());
    CHECK_EQ(f.count(), std::size_t(0));
}

TEST_CASE("Map scene accepted players persist and observer mutations remain queued") {
    Fixture f;
    auto    candidate  = f.prepareAssets(f.mapModule).expect("scene");
    auto    session    = candidate.commit(f.mapModule).expect("published scene");
    auto    navigation = session->prepareNavigationRefresh(f.mapModule).expect("navigation token");
    bool    nested = false, closing = false, refreshing = false;
    auto    subscription = session
                            ->subscribe([&](const eve::spawn::SpawnEvent& event) {
                                if (event.kind != eve::spawn::SpawnEventKind::BatchCommitted) return;
                                nested =
                                    !session->advance(f.mapModule, {eve::SimulationTick(99), eve::Duration(0)}).ok();
                                closing    = !session->close(eve::spawn::DetachPolicy::KeepUnits).ok();
                                refreshing = !navigation->commit(f.mapModule).ok();
                                session->enqueue({event.source, eve::spawn::ControlKind::Pause})
                                    .expect("observer queues next-frame pause");
                            })
                            .expect("scene observer");
    std::vector<eve::spawn::Point> players{{5, 5}};
    REQUIRE(session
                ->advance(f.mapModule, {eve::SimulationTick(1), eve::Duration(0)}, {},
                          {std::span<const eve::spawn::Point>(players), {}})
                .expect("player blocks spawn")
                .units == 0);
    REQUIRE(session->advance(f.mapModule, {eve::SimulationTick(2), eve::Duration(1000000000)})
                .expect("prior player persists")
                .units == 0);
    players.clear();
    auto created = session
                       ->advance(f.mapModule, {eve::SimulationTick(3), eve::Duration(1000000000)}, {},
                                 {std::span<const eve::spawn::Point>(players), {}})
                       .expect("player left");
    REQUIRE(created.units == 1);
    CHECK(created.diagnostics.empty());
    CHECK(nested);
    CHECK(closing);
    CHECK(refreshing);
    session->advance(f.mapModule, {eve::SimulationTick(4), eve::Duration(0)}).expect("queued pause boundary");
    CHECK(session->world().inspect(session->sourceHandles()[0]).expect("paused source").phase ==
          eve::spawn::Phase::Dormant);
    REQUIRE(session->close(eve::spawn::DetachPolicy::RemoveUnits).ok());
}

TEST_CASE("Map scene failed cleanup freezes advancement until explicit retry") {
    Fixture f;
    auto    candidate = f.prepareAssets(f.mapModule).expect("scene");
    auto    session   = candidate.commit(f.mapModule).expect("published scene");
    REQUIRE(session->advance(f.mapModule, {eve::SimulationTick(1), eve::Duration(0)}).expect("created actor").units ==
            1);
    const auto source = session->sourceHandles()[0];
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*f.table);
        REQUIRE(!session->close(eve::spawn::DetachPolicy::RemoveUnits).ok());
    }
    CHECK_EQ(f.count(), std::size_t(1));
    CHECK_EQ(session->world().inspect(source).expect("retained cleanup responsibility").alive, std::uint32_t(1));
    REQUIRE(!session->advance(f.mapModule, {eve::SimulationTick(2), eve::Duration(0)}).ok());
    CHECK(!session->enqueue({source, eve::spawn::ControlKind::Activate}).ok());
    REQUIRE(session->close(eve::spawn::DetachPolicy::RemoveUnits).ok());
    CHECK_EQ(f.count(), std::size_t(0));
    CHECK(!session->world().inspect(source).ok());
}


}  // namespace test_map_spawn_space

namespace test_map_spawn_space {
TEST_CASE("Map restored spatial bindings preserve retired slots and actual pinned placement") {
    Fixture f;
    auto    source    = eve::Value::fromJson(f.map->canonicalJson()).expect("map source");
    auto*   instances = source.find("instances")->getIf<eve::Value::Array>();
    auto    retired   = instances->at(0);
    retired.set("id", "a.retired");
    instances->push_back(retired);
    auto                   compiled = eve::spawn::compileMap(source, Templates(*f.units), *f.units).expect("two slots");
    eve::spawn::SpawnWorld original(f.owner, 42);
    auto                   handles = original.attach(compiled, f.providers).expect("original slots");
    original.detach(handles[0], eve::spawn::DetachPolicy::KeepUnits).expect("retired first slot");
    auto hash = [](std::string_view text) {
        std::uint64_t state = 14695981039346656037ULL;
        for (unsigned char byte : text) state = (state ^ byte) * 1099511628211ULL;
        eve::ContentId::Bytes bytes{};
        for (auto& byte : bytes) {
            byte  = static_cast<std::uint8_t>(state);
            state = (state ^ byte) * 1099511628211ULL;
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
    const auto epoch    = f.owner.child("placement-save");
    auto       snapshot = original.capture(epoch, eve::Revision(1)).expect("capture").seal(hash).expect("seal");
    auto       admitted = eve::spawn::SpawnRestoreState::decode(snapshot, epoch, hash, *f.units).expect("admit");
    REQUIRE_EQ(admitted.placementBindings().size(), std::size_t(1));
    REQUIRE_EQ(admitted.placementBindings()[0].source.slot, std::uint32_t(1));
    auto space =
        eve::map_spawn::MapSpawnSpace::prepareRestored(admitted, {}, f.occupancy).expect("saved spatial provider");
    space->setNavigation(f.navigation.captureNavigationGrid().expect("grid")).expect("navigation");
    space->setPlayers({}).expect("players");
    auto factory   = std::make_shared<eve::map_spawn::SpatialSpawnFactory>(f.domain, space);
    auto providers = std::make_shared<eve::spawn::SpawnProviders>(eve::spawn::SpawnProviders{space, factory});
    auto restored  = eve::spawn::SpawnWorld::restore(admitted, providers).expect("bind restored world");
    REQUIRE(admitted.placementBindings().empty());
    auto report = restored.advance({eve::SimulationTick(1), eve::Duration(0)}, {}).expect("saved slot spawns");
    REQUIRE_EQ(report.units, std::size_t(1));
    REQUIRE_EQ(f.count(), std::size_t(1));
    auto active = restored.restoreBindings().expect("current sources");
    restored.detach(active.sources[0].lifetime.source(), eve::spawn::DetachPolicy::RemoveUnits)
        .expect("retire final source");
    auto emptySnapshot = restored.capture(epoch, eve::Revision(2))
                             .expect("all retired capture")
                             .seal(hash)
                             .expect("all retired artifact");
    auto empty =
        eve::spawn::SpawnRestoreState::decode(emptySnapshot, epoch, hash, *f.units).expect("all retired admission");
    REQUIRE(empty.placementBindings().empty());
    REQUIRE(eve::map_spawn::MapSpawnSpace::prepareRestored(empty, {}, f.occupancy).ok());
}
TEST_CASE("Map explicit viewport culls selected real layers without mutating inherited cameras") {
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    eve::map::Map    map;
    auto*            selected = map.newLayer(96, 32, 1, 1);
    selected->fill(1);
    auto* unrelated = map.newLayer(96, 32, 1, 1);
    unrelated->fill(2);
    auto* inherited = eve::graphics::Camera2D::createCamera(table);
    inherited->setPosition(10000, 10000);
    selected->draw()->camera                        = inherited;
    const auto                             revision = selected->getRevision();
    const std::array                       layers{ecs::handle_of(selected)};
    std::vector<eve::graphics::DrawItem2D> items;
    eve::map::TileRenderViewport           viewport{16, 16, 8, 8, 1};
    eve::map::TileRenderSystem::collectViewport(items, layers, viewport)
        .expect("explicit camera overrides layer camera");
    REQUIRE_EQ(eve::map::TileRenderSystem::lastVisitedChunkCount(), 1);
    REQUIRE_EQ(items.size(), std::size_t(1024));
    REQUIRE(items.front().camValid);
    REQUIRE(items.front().camera == nullptr);
    REQUIRE_EQ(items.front().camX, 8.f);
    REQUIRE_EQ(items.front().camZoom, 1.f);
    REQUIRE(selected->draw()->camera == inherited);
    REQUIRE_EQ(selected->getRevision(), revision);
    items.clear();
    viewport.centerX = 80;
    viewport.zoom    = 2;
    eve::map::TileRenderSystem::collectViewport(items, layers, viewport).expect("pan and zoom culling");
    REQUIRE_EQ(eve::map::TileRenderSystem::lastVisitedChunkCount(), 1);
    REQUIRE_EQ(items.size(), std::size_t(1024));
    for (const auto& item : items) {
        REQUIRE(item.x >= 64.f);
        REQUIRE_EQ(item.camX, 80.f);
        REQUIRE_EQ(item.camZoom, 2.f);
    }
    const auto       size = items.size();
    const std::array duplicate{layers[0], layers[0]};
    REQUIRE(!eve::map::TileRenderSystem::collectViewport(items, duplicate, viewport).ok());
    REQUIRE_EQ(items.size(), size);
    REQUIRE_EQ(eve::map::TileRenderSystem::lastVisitedChunkCount(), 1);
    viewport.zoom = std::numeric_limits<float>::denorm_min();
    REQUIRE(!eve::map::TileRenderSystem::collectViewport(items, layers, viewport).ok());
    viewport.zoom = 1;
    ecs::Table        foreignWorld;
    ecs::EntityHandle foreign;
    {
        ecs::ScopedTable other(foreignWorld);
        foreign = ecs::handle_of(map.newLayer(1, 1, 1, 1));
    }
    const std::array foreignLayers{foreign};
    REQUIRE(!eve::map::TileRenderSystem::collectViewport(items, foreignLayers, viewport).ok());
    REQUIRE_EQ(items.size(), size);
    eve::map::TileRenderSystem::collectViewport(items, {}, viewport).expect("explicit empty selection");
    REQUIRE_EQ(items.size(), size);
    REQUIRE_EQ(eve::map::TileRenderSystem::lastVisitedChunkCount(), 0);
    std::vector<eve::graphics::DrawItem2D> legacy;
    eve::map::TileRenderSystem::collect(legacy, 16, 16);
    REQUIRE_EQ(legacy.size(), std::size_t(3072));
    for (const auto& item : legacy) REQUIRE(!item.camValid);
    ecs::DestroyEntity(selected);
    REQUIRE(!eve::map::TileRenderSystem::collectViewport(items, layers, viewport).ok());
}
}  // namespace test_map_spawn_space
