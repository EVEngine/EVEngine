// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"module":"map_rts_spawn","default":"module-composition","modules":["map_rts_spawn","map_spawn","rts_spawn","rts","spawn","combat","weapon"]}
#include <functional>
#include <map>
#include <stdexcept>
#include "RTSSpawnFixture.h"
#include "RTSSpawnRestoreFixture.h"
#include "action/ActionCapture.h"
#include "combat/Damage.h"
#include "combat/DamageCapture.h"
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/ServiceInterfaces.h"
#include "common/SquirrelBinding.h"
#include "definitions/DefinitionRegistryCapture.h"
#include "map/Map.h"
#include "map/MapGridState.h"
#include "map/Pathfinder.h"
#include "map/PathfinderState.h"
#include "map/rts_spawn/MapRTSSpawn.h"
#include "map/rts_spawn/RTSMapCheckpoint.h"
#include "map/rts_spawn/RTSMapRestoreState.h"
#include "map/rts_spawn/RTSMapRuntime.h"
#include "map/rts_spawn/RTSMapSession.h"
#include "map/rts_spawn/RTSRuntimeCheckpoint.h"
#include "map/rts_spawn/RTSRuntimeHandoff.h"
#include "map/rts_spawn/RTSRuntimeRestore.h"
#include "map/spawn/MapSpawnAssets.h"
#include "map/spawn/MapSpawnLoad.h"
#include "rts/RTSActionCapture.h"
#include "rts/RTSAttributeCapture.h"
#include "rts/RTSCombatArchive.h"
#include "rts/RTSCrowdCapture.h"
#include "rts/RTSFrameArchive.h"
#include "rts/RTSNavigationCapture.h"
#include "rts/RTSWeaponCapture.h"
#include "rts/spawn/RTSSpawnCapture.h"
#include "sensing/Sensing.h"
#include "sensing/SensingCapture.h"
#include "spawn/SpawnCheckpoint.h"
#include "spawn/SpawnSavedState.h"
#include "weapon/WeaponCapture.h"
#include "zeroerr/unittest.h"
namespace test_map_rts_session {
struct Files final : eve::service::IFileSystem {
    std::map<std::string, std::string> text;
    std::function<void()>              onRead;
    bool                               readFile(const std::string& path, std::vector<std::uint8_t>& out) override {
        if (onRead) onRead();
        auto row = text.find(path);
        if (row == text.end()) return false;
        out.assign(row->second.begin(), row->second.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, std::size_t) override { return false; }
    bool fileExists(const std::string& path) override { return text.contains(path); }
};
struct Templates final : eve::spawn::ITemplateResolver {
    eve::spawn::CompiledTemplate value;
    explicit Templates(const eve::spawn::IUnitDefinitionResolver& units)
        : value(eve::spawn::compileTemplate(
                    eve::Value::fromJson(R"({"schema":"eve.spawn-template","version":1,"id":"reinforcement",
        "content":{"kind":"weighted","selection":"per_unit","entries":[{"unit":{"type":"unit","id":"fighter"},"weight":1}]},
        "schedule":{"kind":"maintain_population","target_alive":1,"initial_population":1,"replacement":{"after":"death","delay_seconds":[1,1]}},
        "limits":{"alive":1,"total":{"kind":"unlimited"},"budget_group":"reinforcements"},
        "placement":{"kind":"random_in_region","constraints":[{"kind":"walkable"},{"kind":"clearance","radius":0.35}],"attempts_per_unit":3},
        "batch_policy":"atomic","failure":{"placement_blocked":{"kind":"retry","delay_seconds":1}}})")
                        .expect("RTS template source"),
                    units)
                    .expect("actual RTS template")) {}
    eve::Result<eve::spawn::CompiledTemplate> resolve(const eve::DefinitionRef&) const override {
        return eve::Result<eve::spawn::CompiledTemplate>::success(value);
    }
};
struct Scene {
    test_rts_spawn::Fixture domain;
    Templates               templates{*domain.factory};
    Files                   files;
    eve::Value              binding = eve::Value::fromJson(R"({"schema":"eve.map-spawn-binding","version":1,
        "map_id":"reinforcement-map","map_uri":"project://map.json","spawns_uri":"project://spawns.json",
        "pixel_to_world":{"origin":[0,0],"scale":1}})")
                             .expect("map binding");
    Scene() {
        auto grid                 = eve::Value::object({{"width", 20},
                                                        {"height", 20},
                                                        {"tilewidth", 1},
                                                        {"tileheight", 1},
                                                        {"data", eve::Value::array(eve::Value::Array(400, eve::Value(1)))}});
        files.text["map.json"]    = grid.toJson().expect("actual map bytes");
        files.text["spawns.json"] = R"({"schema":"eve.map-spawns","version":1,"map":"reinforcement-map",
        "budgets":[{"id":"reinforcements","alive_limit":1}],"instances":[{"id":"reinforcement",
        "template":{"type":"spawn.template","id":"reinforcement"},"region":{"kind":"points","space":"world2d","points":[[5,5]]},
        "activation":{"kind":"map_loaded"},"inactive_policy":"pause","unload_policy":"persist"}]})";
    }
    eve::Result<eve::map_rts_spawn::PreparedRTSMapSession> prepare(eve::map::Map& map) {
        return eve::map_rts_spawn::PreparedRTSMapSession::prepare(
            map, binding, files, templates, *domain.domain,
            eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse("01900000-0000-7000-8000-000000000889")),
            domain.registry, domain.request.source.owner.child("native-map"), 42,
            R"({"schema":"eve.map.object-contract","version":1,"types":[]})", {}, {});
    }
};
std::string runtimeContent() {
    return R"({"weapons":[{"id":"club","damageType":"normal","damage":10,"range":1.2,
        "cooldown":0.2,"projectileSpeed":0,"magazineSize":1000}],
        "units":[{"id":"fighter","weaponType":"club","health":40,"speed":3,"radius":0.35}]})";
}
TEST_CASE("RTS map session retains canonical damage across rejected navigation and retries without stepping domain") {
    Scene         scene;
    eve::map::Map map;
    scene.domain.domain->setCrowdProvider(nullptr);
    REQUIRE(!scene.prepare(map).ok());
    REQUIRE_EQ(map.getLayerCount(), 0);
    scene.domain.domain->setCrowdProvider(&scene.domain.crowd);
    auto prepared = scene.prepare(map).expect("native map preparation");
    REQUIRE_EQ(scene.domain.count(), std::size_t(0));
    scene.domain.domain->setCrowdProvider(nullptr);
    REQUIRE(!prepared.commit(map).ok());
    REQUIRE_EQ(map.getLayerCount(), 0);
    scene.domain.domain->setCrowdProvider(&scene.domain.crowd);
    auto session = prepared.commit(map).expect("native map commit");
    REQUIRE(!prepared.commit(map).ok());
    REQUIRE_EQ(
        session->advance(map, {eve::SimulationTick(1), eve::Duration(0)}, {}, {}).expect("real initial unit").units,
        std::uint32_t(1));
    const auto source  = session->spawning().sourceHandles()[0];
    const auto subject = session->spawning().world().inspect(source).expect("actual identity").subjects[0];
    std::vector<eve::rts::RTSFrameDamageEvent> events;
    {
        ecs::ScopedTable           scope(*scene.domain.table);
        auto*                      unit = scene.domain.domain->findUnit(subject);
        eve::combat::DamageRequest request;
        request.target       = subject;
        request.healthDamage = 100;
        request.damageType   = "damage.physical";
        request.tick         = eve::SimulationTick(2);
        eve::combat::DamageRuntime damage;
        auto outcome = damage.apply(unit->durability()->state, request).expect("canonical actual HP death");
        REQUIRE_EQ(outcome.health, 0.0);
        events.push_back({1, request.tick, eve::rts::DamageChannel::Weapon, request, outcome});
        scene.domain.domain->cleanupDestroyed().expect("RTS step casualty phase precedes event dispatch");
    }
    map.getLayer(0)->setTile(0, 0, 2);
    REQUIRE(!session->advance(map, {eve::SimulationTick(2), eve::Duration(0)}, events, {}).ok());
    const auto repeatedEvents = events;
    events.clear();
    REQUIRE(
        !session->capture(map, scene.domain.request.source.owner.child("retained-map-save"), eve::Revision(6)).ok());
    auto refresh = session->prepareNavigationRefresh(map).expect("explicit grid capture");
    refresh->commit(map).expect("explicit navigation publication");
    const auto epoch  = scene.domain.request.source.owner.child("retained-map-save");
    auto       frozen = session->capture(map, epoch, eve::Revision(7)).expect("freeze retained translated removal");
    REQUIRE(frozen.saveEpoch() == epoch);
    REQUIRE(frozen.revision() == eve::Revision(7));
    REQUIRE(frozen.coreTick() == eve::SimulationTick(1));
    REQUIRE_EQ(frozen.core()->get().membership().size(), std::size_t(1));
    REQUIRE(frozen.roots()->get().units.empty());
    REQUIRE(frozen.provenance()->get().units().empty());
    REQUIRE(frozen.pendingDamage().empty());
    REQUIRE_EQ(frozen.pendingLifecycle().size(), std::size_t(1));
    REQUIRE(frozen.pendingLifecycle()[0].subject == subject);
    REQUIRE(frozen.pendingLifecycle()[0].reason == eve::spawn::RemovalReason::Death);
    REQUIRE_EQ(frozen.provenance()->get().lifecycleSequence(), std::uint64_t(1));
    REQUIRE_EQ(session->spawning().world().inspect(source).expect("capture does not consume pending").alive,
               std::uint32_t(1));
    {
        auto view = ecs::View<eve::rts::Faction>(*scene.domain.table);
        REQUIRE(!session->advance(map, {eve::SimulationTick(2), eve::Duration(0)}, repeatedEvents, {}).ok());
    }
    auto both = session->capture(map, epoch, eve::Revision(8)).expect("both retained stages coexist");
    const std::vector<eve::rts::RTSFrameDamageEvent> repeated{both.pendingDamage()[0], both.pendingDamage()[0]};
    const auto                                       encodedDamage =
        eve::rts::RTSFrameArchive::encodeDamageInputs(repeated).expect("encode repeated retained observations");
    const auto readDamage =
        eve::rts::RTSFrameArchive::prepareDamageInputs(encodedDamage).expect("admit repeated observations");
    REQUIRE_EQ(readDamage.size(), 2u);
    CHECK_EQ(readDamage[0].sequence, readDamage[1].sequence);
    CHECK_EQ(readDamage[0].outcome.health, both.pendingDamage()[0].outcome.health);
    const std::vector<eve::spawn::SavedLifecycleInput> lifecycle{
        eve::spawn::savedLifecycleInput(both.pendingLifecycle()[0]),
        eve::spawn::savedLifecycleInput(both.pendingLifecycle()[0])};
    const auto encodedLifecycle =
        eve::spawn::encodeSavedLifecycleInputs(lifecycle).expect("encode repeated durable handoff");
    const auto readLifecycle =
        eve::spawn::prepareSavedLifecycleInputs(encodedLifecycle).expect("admit repeated durable handoff");
    REQUIRE_EQ(readLifecycle.size(), 2u);
    CHECK(readLifecycle[0].source == lifecycle[0].source);
    CHECK_EQ(readLifecycle[0].sequence, readLifecycle[1].sequence);
    REQUIRE_EQ(both.pendingDamage().size(), std::size_t(1));
    REQUIRE_EQ(both.pendingLifecycle().size(), std::size_t(1));
    REQUIRE_EQ(both.provenance()->get().lifecycleSequence(), std::uint64_t(1));
    REQUIRE(frozen.pendingDamage().empty());


    REQUIRE_EQ(session->advance(map, {eve::SimulationTick(2), eve::Duration(0)}, {}, {})
                   .expect("retained death handoff")
                   .units,
               std::uint32_t(0));
    REQUIRE_EQ(session->spawning().world().inspect(source).expect("actual ledger release").alive, std::uint32_t(0));
    REQUIRE_EQ(session->advance(map, {eve::SimulationTick(3), eve::Duration(1000000000)}, {}, {})
                   .expect("actual delayed replacement")
                   .units,
               std::uint32_t(1));
    REQUIRE_EQ(scene.domain.count(), std::size_t(1));
    REQUIRE_EQ(scene.domain.crowd.getAgentCount(), 1);
    REQUIRE(scene.domain.domain->frameDamageEvents().empty());
    REQUIRE_EQ(frozen.core()->get().membership().size(), std::size_t(1));
    REQUIRE_EQ(frozen.pendingLifecycle().size(), std::size_t(1));
    REQUIRE(frozen.roots()->get().units.empty());

    session->close(eve::spawn::DetachPolicy::RemoveUnits).expect("explicit domain cleanup");
    REQUIRE_EQ(scene.domain.count(), std::size_t(0));
    REQUIRE_EQ(scene.domain.crowd.getAgentCount(), 0);
    REQUIRE(!session->advance(map, {eve::SimulationTick(4), eve::Duration(0)}, {}, {}).ok());
}
TEST_CASE("RTS map joined freeze owns live roots weapons map and core after every source owner retires") {
    const auto epoch = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000a19");
    std::optional<eve::map_rts_spawn::RTSMapCheckpoint> retained;
    std::weak_ptr<ecs::Table>                           world;
    std::weak_ptr<eve::definitions::DefinitionRegistry> registry;
    {
        Scene scene;
        world    = scene.domain.table;
        registry = scene.domain.registry;
        eve::map::Map map;
        auto          prepared = scene.prepare(map).expect("prepare actual map");
        auto          session  = prepared.commit(map).expect("publish actual session");
        auto          empty    = session->capture(map, epoch, eve::Revision(1)).expect("same-boundary empty source");
        REQUIRE(empty.core()->get().membership().empty());
        REQUIRE(empty.roots()->get().units.empty());
        REQUIRE(!session->capture(map, {}, eve::Revision(1)).ok());
        REQUIRE(!session->capture(map, epoch, eve::Revision::zero()).ok());
        session->advance(map, {eve::SimulationTick(1), eve::Duration(0)}, {}, {}).expect("actual initial unit");
        {
            ecs::Table       unrelated;
            ecs::ScopedTable unrelatedScope(unrelated);
            retained.emplace(
                session->capture(map, epoch, eve::Revision(2)).expect("actual owners independent of Scope"));
        }
        REQUIRE(retained->saveEpoch() == epoch);
        REQUIRE(retained->coreTick() == eve::SimulationTick(1));
        REQUIRE_EQ(retained->roots()->get().units.size(), std::size_t(1));
        REQUIRE_EQ(retained->provenance()->get().units().size(), std::size_t(1));
        const auto& member = retained->provenance()->get().units()[0];
        REQUIRE(member.subject == retained->roots()->get().units[0].subject);
        REQUIRE(member.subject == retained->core()->get().membership()[0].subject);
        REQUIRE(member.origin == retained->core()->get().membership()[0].source);
        REQUIRE(member.weapon != nullptr);
        REQUIRE(member.weapon->saveEpoch() == epoch);
        REQUIRE(member.weapon->facts().has_value());
        REQUIRE(retained->assets() != nullptr);
        REQUIRE_EQ(retained->gridState()->layerCount(), std::size_t(1));
        {
            auto view = ecs::View<eve::rts::Unit>(*scene.domain.table);
            REQUIRE(!session->capture(map, epoch, eve::Revision(3)).ok());
        }
        scene.domain.domain->setCrowdProvider(nullptr);
        REQUIRE(!session->capture(map, epoch, eve::Revision(3)).ok());
        scene.domain.domain->setCrowdProvider(&scene.domain.crowd);
        REQUIRE(session->capture(map, epoch, eve::Revision(3)).ok());
        session->close(eve::spawn::DetachPolicy::RemoveUnits).expect("explicit source cleanup");
        REQUIRE(!session->capture(map, epoch, eve::Revision(3)).ok());
    }
    REQUIRE(world.expired());
    REQUIRE(registry.expired());
    REQUIRE_EQ(retained->roots()->get().units.size(), std::size_t(1));
    REQUIRE_EQ(retained->core()->get().membership().size(), std::size_t(1));
    REQUIRE_EQ(retained->provenance()->get().units()[0].weapon->facts()->get().definition.damage, 10.f);
    auto moved = std::move(*retained);
    REQUIRE(!retained->core().has_value());
    REQUIRE(!retained->roots().has_value());
    REQUIRE(!retained->provenance().has_value());
    REQUIRE(retained->saveEpoch().isNil());
    REQUIRE(retained->revision().isZero());
    REQUIRE(moved.saveEpoch() == epoch);
}
TEST_CASE("RTS map joined freeze preserves untranslated real death without reconciling the live factory") {
    Scene         scene;
    eve::map::Map map;
    auto          prepared = scene.prepare(map).expect("prepare actual map");
    auto          session  = prepared.commit(map).expect("publish actual session");
    session->advance(map, {eve::SimulationTick(1), eve::Duration(0)}, {}, {}).expect("real initial unit");
    const auto source  = session->spawning().sourceHandles()[0];
    const auto subject = session->spawning().world().inspect(source).expect("actual managed identity").subjects[0];
    const auto epoch   = scene.domain.request.source.owner.child("untranslated-save");
    std::vector<eve::rts::RTSFrameDamageEvent> events;
    {
        ecs::ScopedTable           scope(*scene.domain.table);
        auto*                      unit = scene.domain.domain->findUnit(subject);
        eve::combat::DamageRequest request;
        request.target       = subject;
        request.healthDamage = 100;
        request.damageType   = "damage.physical";
        request.tick         = eve::SimulationTick(2);
        eve::combat::DamageRuntime damage;
        auto outcome = damage.apply(unit->durability()->state, request).expect("real committed death");
        events.push_back({1, request.tick, eve::rts::DamageChannel::Weapon, request, outcome});
        scene.domain.domain->cleanupDestroyed().expect("real casualty cleanup");
    }
    REQUIRE(!session->capture(map, epoch, eve::Revision(1)).ok());
    {
        auto view = ecs::View<eve::rts::Faction>(*scene.domain.table);
        REQUIRE(!session->advance(map, {eve::SimulationTick(2), eve::Duration(0)}, events, {}).ok());
    }
    events.clear();
    auto frozen = session->capture(map, epoch, eve::Revision(1)).expect("own untranslated admitted input");
    REQUIRE_EQ(frozen.pendingDamage().size(), std::size_t(1));
    REQUIRE(frozen.pendingLifecycle().empty());
    REQUIRE_EQ(frozen.provenance()->get().lifecycleSequence(), std::uint64_t(0));
    REQUIRE_EQ(frozen.provenance()->get().units().size(), std::size_t(1));
    REQUIRE(frozen.provenance()->get().units()[0].presence == eve::rts_spawn::CapturedUnitPresence::Absent);
    REQUIRE_EQ(frozen.core()->get().membership().size(), std::size_t(1));
    REQUIRE(frozen.roots()->get().units.empty());
    REQUIRE(frozen.pendingDamage()[0].outcome.target == subject);
    REQUIRE_EQ(frozen.pendingDamage()[0].outcome.health, 0.0);
    REQUIRE_EQ(session->spawning().world().inspect(source).expect("no implicit reconciliation").alive,
               std::uint32_t(1));
    session->advance(map, {eve::SimulationTick(2), eve::Duration(0)}, {}, {}).expect("exact retained handoff");
    REQUIRE_EQ(session->spawning().world().inspect(source).expect("actual death released capacity").alive,
               std::uint32_t(0));
    auto after = session->capture(map, epoch, eve::Revision(2)).expect("post-handoff checkpoint");
    REQUIRE(after.pendingDamage().empty());
    REQUIRE(after.pendingLifecycle().empty());
    REQUIRE(after.core()->get().membership().empty());
    REQUIRE_EQ(frozen.pendingDamage().size(), std::size_t(1));
    session->close(eve::spawn::DetachPolicy::RemoveUnits).expect("close actual session");
}
TEST_CASE("RTS map prepared publication and cleanup observe both map and domain retirement") {
    {
        Scene         scene;
        eve::map::Map map;
        auto          prepared = scene.prepare(map).expect("hidden map session");
        scene.domain.domain.reset();
        REQUIRE(!prepared.commit(map).ok());
        REQUIRE_EQ(map.getLayerCount(), 0);
    }
    for (bool mapFirst : {false, true}) {
        Scene scene;
        auto  map      = std::make_unique<eve::map::Map>();
        auto  prepared = scene.prepare(*map).expect("owner order preparation");
        auto  session  = prepared.commit(*map).expect("owner order commit");
        session->advance(*map, {eve::SimulationTick(1), eve::Duration(0)}, {}, {}).expect("actual managed unit");
        if (mapFirst) {
            map.reset();
            eve::map::Map replacement;
            REQUIRE(
                !session->capture(replacement, scene.domain.request.source.owner.child("retired-map"), eve::Revision(1))
                     .ok());
        } else {
            scene.domain.domain.reset();
            REQUIRE(!session->capture(*map, scene.domain.request.source.owner.child("retired-domain"), eve::Revision(1))
                         .ok());
        }
        session->close(eve::spawn::DetachPolicy::RemoveUnits).expect("safe explicit cleanup after owner retirement");
        REQUIRE_EQ(scene.domain.count(), std::size_t(0));
        REQUIRE_EQ(scene.domain.crowd.getAgentCount(), 0);
    }
}
}  // namespace test_map_rts_session

namespace test_map_rts_session {
TEST_CASE("Owned RTS map runtime drives real command movement with one injected clock and explicit cleanup") {
    Scene      scene;
    const auto content = runtimeContent();
    auto       source  = eve::Value::fromJson(R"({"schema":"eve.spawn-template","version":1,"id":"reinforcement",
        "content":{"kind":"fixed","members":[{"id":"fighter","unit":{"type":"unit","id":"fighter"},"count":1}]},
        "schedule":{"kind":"once"},"limits":{"alive":1,"total":{"kind":"finite","count":1},"budget_group":"reinforcements"},
        "placement":{"kind":"random_in_region","constraints":[{"kind":"walkable"},{"kind":"clearance","radius":0.35}],"attempts_per_unit":3},
        "batch_policy":"atomic","failure":{"placement_blocked":{"kind":"retry","delay_seconds":1}}})")
                      .expect("runtime template");
    auto command    = std::make_shared<eve::rts::CommandSpec>();
    command->kind   = eve::rts::OrderKind::AttackMove;
    command->target = {12, 12};
    auto runtime =
        eve::map_rts_spawn::RTSMapRuntime::prepare(
            content, {source}, scene.binding, scene.files,
            eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse("01900000-0000-7000-8000-000000000889")),
            scene.domain.request.source.owner.child("owned-runtime"), 42, command)
            .expect("isolated real runtime");
    auto       initial = runtime->captureState().expect("owning initial state");
    const auto epoch   = scene.domain.request.source.owner.child("owned-checkpoint");
    REQUIRE(!runtime->captureCheckpoint({}, eve::Revision(1)).ok());
    REQUIRE(!runtime->captureCheckpoint(epoch, eve::Revision::zero()).ok());
    auto empty = runtime->captureCheckpoint(epoch, eve::Revision(1)).expect("initial owner boundary");
    REQUIRE(empty.phase() == eve::map_rts_spawn::RTSCheckpointPhase::Ready);
    REQUIRE_EQ(empty.completedTick().value(), std::uint64_t(0));
    REQUIRE(empty.crowdMembers().empty());
    REQUIRE(empty.navigation());
    REQUIRE(empty.navigation()->get().saveEpoch() == epoch);
    REQUIRE(empty.navigationProvider());
    REQUIRE(empty.navigationProvider()->get().bound());
    REQUIRE_EQ(initial.find("units")->arraySize(), std::size_t(0));
    REQUIRE(!runtime->advance({eve::SimulationTick(1), eve::Duration(0)}).ok());
    REQUIRE(!runtime->advance({eve::SimulationTick(2), eve::Duration(100000000)}).ok());
    REQUIRE_EQ(runtime->advance({eve::SimulationTick(1), eve::Duration(100000000)})
                   .expect("first actual domain and spawn frame")
                   .units,
               std::uint32_t(1));
    auto departure = runtime->captureState().expect("real departure");
    auto frozen    = runtime->captureCheckpoint(epoch, eve::Revision(2)).expect("joint actual owner freeze");
    REQUIRE(frozen.saveEpoch() == epoch);
    REQUIRE(frozen.revision() == eve::Revision(2));
    REQUIRE_EQ(frozen.completedTick().value(), std::uint64_t(1));
    REQUIRE(!frozen.pendingStep());
    REQUIRE_EQ(frozen.map()->get().coreTick().value(), std::uint64_t(1));
    REQUIRE_EQ(frozen.crowdFrame()->get().tick.value(), std::uint64_t(1));
    REQUIRE_EQ(frozen.crowdMembers().size(), std::size_t(1));
    REQUIRE(frozen.actions()->get().actions()->get().saveEpoch() == epoch);
    REQUIRE(frozen.sensing()->get().subjects().empty());
    REQUIRE(frozen.combat()->get().mirroredSubjects.empty());
    const auto& member = frozen.crowdMembers()[0];
    REQUIRE(member.subject == frozen.map()->get().roots()->get().units[0].subject);
    REQUIRE_EQ(frozen.crowdFrame()->get().agents[member.frameIndex].state.x, 5.f);
    const auto queuedEvents = frozen.events()->get().gameplay.size();
    REQUIRE_EQ(runtime->captureCheckpoint(epoch, eve::Revision(3))
                   .expect("repeat freeze does not poll")
                   .events()
                   ->get()
                   .gameplay.size(),
               queuedEvents);
    REQUIRE_EQ(departure.find("units")->arraySize(), std::size_t(1));
    REQUIRE_EQ(scene.domain.count(), std::size_t(0));
    REQUIRE_EQ(departure.find("phase")->asString(), "ready");
    REQUIRE(departure.find("movement_surface")->asInt() != 0);
    const auto& before = departure.find("units")->at(0);
    REQUIRE_EQ(before.find("x")->asDouble(), 5.0);
    REQUIRE_EQ(before.find("faction")->asString(), "01900000-0000-7000-8000-000000000889");
    REQUIRE_EQ(before.find("order")->asString(), "attack_move");
    REQUIRE(!runtime->advance({eve::SimulationTick(1), eve::Duration(100000000)}).ok());
    for (std::uint64_t tick = 2; tick <= 11; ++tick)
        REQUIRE_EQ(runtime->advance({eve::SimulationTick(tick), eve::Duration(100000000)})
                       .expect("real runtime movement frame")
                       .units,
                   std::uint32_t(0));
    auto arrived = runtime->captureState().expect("owning moved state");
    REQUIRE_EQ(arrived.find("tick")->asInt(), std::int64_t(11));
    REQUIRE_EQ(arrived.find("units")->arraySize(), std::size_t(1));
    REQUIRE(arrived.find("units")->at(0).find("x")->asDouble() > 5.0);
    REQUIRE_EQ(arrived.find("sources")->at(0).find("total")->asInt(), std::int64_t(1));
    REQUIRE_EQ(before.find("x")->asDouble(), 5.0);
    auto moved = runtime->captureCheckpoint(epoch, eve::Revision(4)).expect("moved owner boundary");
    REQUIRE_EQ(moved.completedTick().value(), std::uint64_t(11));
    REQUIRE_EQ(moved.crowdFrame()->get().tick.value(), std::uint64_t(11));
    REQUIRE_EQ(moved.sensing()->get().subjects().size(), std::size_t(1));
    REQUIRE(moved.combat()->get().mirroredSubjects.contains(member.subject.format()));
    REQUIRE_EQ(moved.sensing()->get().subjects()[0].x, moved.crowdFrame()->get().agents[0].state.x);
    REQUIRE(frozen.sensing()->get().subjects().empty());
    REQUIRE_EQ(frozen.crowdFrame()->get().agents[0].state.x, 5.f);
    runtime->close().expect("explicit actual runtime cleanup");
    REQUIRE(!runtime->captureCheckpoint(epoch, eve::Revision(5)).ok());
    runtime->close().expect("idempotent runtime cleanup");
    REQUIRE_EQ(runtime->captureState().expect("closed actual state").find("units")->arraySize(), std::size_t(0));
    REQUIRE(!runtime->advance({eve::SimulationTick(12), eve::Duration(100000000)}).ok());
    runtime.reset();
    // Restore actual structural/grid facts before attaching the saved native provider, after source death.
    eve::map::Map replayMap;
    auto          replay = moved.map()->get().assets()->prepareMap(replayMap).expect("detached original map structure");
    replay->applyGridState(*moved.map()->get().gridState()).expect("exact saved runtime tiles");
    replay->commit(replayMap).expect("isolated map publication");
    auto replayProvider =
        eve::map::Pathfinder::prepareRestored(moved.navigationProvider()->get(), std::ref(*replayMap.getLayer(0)))
            .expect("source-retired map provider");
    REQUIRE(replayProvider->isRestoredFrom(moved.navigationProvider()->get()));
    REQUIRE(replayProvider->captureNavigationGrid().expect("saved provider publication history").valid());
    auto              restoredTable = std::make_shared<ecs::Table>();
    eve::crowd::Crowd restoredCrowd;
    eve::rts::RTS     restoredDomain(restoredTable);
    restoredDomain.setCrowdProvider(&restoredCrowd);
    {
        ecs::ScopedTable scope(*restoredTable);
        restoredDomain.rebuildState(moved.map()->get().roots()->get()).expect("detached game native roots");
    }
    REQUIRE(moved.crowd()->get().saveEpoch() == epoch);
    REQUIRE_EQ(moved.crowd()->get().members().size(), std::size_t(1));
    auto preparedCrowd = eve::rts::PreparedRTSCrowdRestore::prepare(restoredDomain, moved.crowd()->get())
                             .expect("actual runtime Crowd binding candidate");
    restoredDomain.restoreCrowd(std::move(preparedCrowd)).expect("source-retired native body/backend publication");
    const auto surfaceBeforeNavigation = restoredCrowd.getFieldStatus();
    {
        ecs::ScopedTable scope(*restoredTable);
        auto             recovery       = restoredDomain.findUnit(member.subject)->navigation();
        recovery->trafficRecoveryTarget = eve::rts::WorldPosition{7.f, 8.f};
        recovery->plannedOrderId        = "saved-recovery";
        restoredDomain.restoreNavigation(moved.navigation()->get(), *replayProvider)
            .expect("source-retired actual map tracker publication");
        REQUIRE(recovery->trafficRecoveryTarget.has_value());
        REQUIRE_EQ(recovery->trafficRecoveryTarget->x, 7.f);
        REQUIRE_EQ(recovery->plannedOrderId, "saved-recovery");
    }
    REQUIRE(restoredCrowd.getFieldStatus().surface == surfaceBeforeNavigation.surface);
    REQUIRE_EQ(restoredCrowd.getFieldStatus().inputRevision, surfaceBeforeNavigation.inputRevision);
    REQUIRE_EQ(restoredCrowd.getAgentCount(), 1);
    {
        ecs::ScopedTable scope(*restoredTable);
        auto             body = restoredDomain.findUnit(member.subject)->crowd();
        REQUIRE(restoredCrowd.isAgentAlive({body->runtimeAgent, body->runtimeOwner}));
        auto state =
            restoredCrowd.readAgent({body->runtimeAgent, body->runtimeOwner}).expect("restored native velocity");
        REQUIRE_EQ(state.vx, moved.crowdFrame()->get().agents[0].state.vx);
        REQUIRE_EQ(state.x, moved.crowdFrame()->get().agents[0].state.x);
    }
    eve::sensing::SensingWorld independent;
    independent
        .restore(
            eve::sensing::PreparedSensingRestore::prepare(moved.sensing()->get()).expect("detached sensing candidate"))
        .expect("source-retired sensing publication");
    REQUIRE_EQ(independent.subjects().size(), std::size_t(1));
    REQUIRE(moved.combat()->get().mirroredSubjects.contains(independent.subjects().begin()->first));
    REQUIRE_EQ(frozen.map()->get().roots()->get().units.size(), std::size_t(1));
    REQUIRE_EQ(frozen.crowdFrame()->get().agents.size(), std::size_t(1));
    REQUIRE_EQ(frozen.events()->get().gameplay.size(), queuedEvents);
    auto transferred = std::move(frozen);
    REQUIRE(frozen.saveEpoch().isNil());
    REQUIRE(!frozen.phase());
    REQUIRE(!frozen.map());
    REQUIRE(!frozen.crowd());
    REQUIRE(!frozen.navigationProvider());
    REQUIRE(!frozen.navigation());
    REQUIRE_EQ(transferred.crowdMembers().size(), std::size_t(1));
    REQUIRE_EQ(arrived.find("units")->arraySize(), std::size_t(1));
}
}  // namespace test_map_rts_session

namespace test_map_rts_session {
TEST_CASE("RTS map script face handles filesystem absence and drives the actual isolated domain") {
    Scene scene;
    struct RestoreFiles {
        eve::service::IFileSystem* previous = eve::cap::query<eve::service::IFileSystem>();
        ~RestoreFiles() { eve::cap::provide<eve::service::IFileSystem>(previous); }
    } restore;
    ssq::VM vm(4096, ssq::Libs::ALL);
    auto    eveTable = vm.addTable("eve");
    eve::map_rts_spawn::MapRTSSpawn::expose(eveTable);
    vm.addFunc("retireRuntimeAssets", [&] { scene.files.text.clear(); });
    vm.addFunc("contentJson", [] { return runtimeContent(); });
    vm.addFunc("templateJson", [&] { return "[" + scene.templates.value.canonicalJson() + "]"; });
    vm.addFunc("bindingJson", [&] { return scene.binding.toJson().expect("script binding"); });
    vm.addFunc("filesAvailable",
               [&](bool enabled) { eve::cap::provide<eve::service::IFileSystem>(enabled ? &scene.files : nullptr); });
    vm.run(vm.compileSource(R"(
        local game = eve.MapRTSSpawn();
        local owner = "01900000-0000-7000-8000-000000000779";
        local faction = "01900000-0000-7000-8000-000000000889";
        assert(!game.commit().ok);
        assert(!game.advance(1,100000000).ok);
        assert(!game.captureState().ok);
        filesAvailable(false);
        assert(!game.prepare(contentJson(),templateJson(),bindingJson(),owner,faction,42,12.0,12.0).ok);
        filesAvailable(true);
        assert(!game.prepare(contentJson(),"{}",bindingJson(),owner,faction,42,12.0,12.0).ok);
        assert(game.prepare(contentJson(),templateJson(),bindingJson(),owner,faction,42,12.0,12.0).ok);
        assert(!game.prepare(contentJson(),templateJson(),bindingJson(),owner,faction,42,12.0,12.0).ok);
        assert(game.commit().ok);
        assert(!game.commit().ok);
        assert(game.captureState().value.units.len() == 0);
        assert(!game.advance(1,0).ok);
        assert(game.advance(1,100000000).value.units == 1);
        local departure = game.captureState().value;
        assert(departure.phase == "ready" && departure.units[0].x == 5.0);
        assert(departure.units[0].faction == faction && departure.units[0].order == "attack_move");
        assert(!game.advance(1,100000000).ok);
        assert(!game.advance(3,100000000).ok);
        for (local tick=2; tick<=11; tick+=1) assert(game.advance(tick,100000000).ok);
        local arrived = game.captureState().value;
        assert(arrived.tick == 11 && arrived.units.len() == 1 && arrived.units[0].x > 5.0);
        assert(arrived.crowd_agents == 1 && arrived.movement_surface != 0);
        assert(departure.units[0].x == 5.0);
        local epoch = "01900000-0000-7000-8000-000000000789";
        assert(!game.freezeRuntime("bad",1).ok);
        assert(!game.freezeRuntime(epoch,0).ok);
        assert(!game.commitRuntimeRestore().ok);
        assert(game.freezeRuntime(epoch,1).ok);
        local saved = game.sealRuntime().value;
        assert(game.snapshotHashAlgorithm().len() != 0);
        assert(!game.prepareRuntimeRestore("{}",epoch).ok);
        assert(game.captureState().value.tick == 11);
        assert(game.prepareRuntimeRestore(saved,epoch).ok);
        assert(game.advance(12,100000000).ok);
        retireRuntimeAssets();
        filesAvailable(false);
        assert(game.commitRuntimeRestore().ok);
        assert(!game.commitRuntimeRestore().ok);
        local restored = game.captureState().value;
        assert(restored.tick == 11 && restored.units.len() == 1 && restored.crowd_agents == 1);
        assert(restored.units[0].x == arrived.units[0].x && restored.units[0].y == arrived.units[0].y);
        assert(restored.units[0].subject == arrived.units[0].subject && restored.units[0].faction == faction);
        assert(restored.units[0].order == "attack_move");
        assert(game.advance(12,100000000).ok);
        assert(game.captureState().value.units[0].x >= restored.units[0].x);
        assert(game.freezeRuntime(epoch,2).ok);
        assert(game.close().ok);
        local detached = game.sealRuntime().value;
        assert(game.prepareRuntimeRestore(detached,epoch).ok);
        assert(game.commitRuntimeRestore().ok);
        assert(game.captureState().value.tick == 12 && game.captureState().value.units.len() == 1);
        assert(game.close().ok);
        assert(game.close().ok);
        assert(!game.advance(12,100000000).ok);
        assert(!game.captureState().ok);
    )"));
    REQUIRE_EQ(scene.domain.count(), std::size_t(0));
}

std::shared_ptr<const eve::map_rts_spawn::RTSMapCheckpoint> savedSessionHandoff(int mode) {
    Scene            scene;
    ecs::ScopedTable sourceScope(*scene.domain.table);
    eve::map::Map    map;
    auto             session = scene.prepare(map).takeValue().commit(map).takeValue();
    if (mode >= 0)
        session->advance(map, {eve::SimulationTick(1), eve::Duration(0)}, {}, {}).expect("source initial member");
    if (mode > 0) {
        const auto source  = session->spawning().sourceHandles()[0];
        const auto subject = session->spawning().world().inspect(source).takeValue().subjects[0];
        std::vector<eve::rts::RTSFrameDamageEvent> events;
        {
            ecs::ScopedTable           scope(*scene.domain.table);
            auto*                      unit = scene.domain.domain->findUnit(subject);
            eve::combat::DamageRequest request;
            request.target       = subject;
            request.healthDamage = 100;
            request.damageType   = "damage.physical";
            request.tick         = eve::SimulationTick(2);
            eve::combat::DamageRuntime damage;
            auto                       outcome = damage.apply(unit->durability()->state, request).takeValue();
            events.push_back({1, request.tick, eve::rts::DamageChannel::Weapon, request, outcome});
            scene.domain.domain->cleanupDestroyed().expect("source real casualty cleanup");
        }
        if (mode >= 2) {
            map.getLayer(0)->setTile(0, 0, 2);
            REQUIRE(!session->advance(map, {eve::SimulationTick(2), eve::Duration(0)}, events, {}).ok());
            auto refresh = session->prepareNavigationRefresh(map).takeValue();
            refresh->commit(map).expect("source map current before freeze");
        }
        if (mode == 1 || mode == 3) {
            auto view = ecs::View<eve::rts::Faction>(*scene.domain.table);
            REQUIRE(!session->advance(map, {eve::SimulationTick(2), eve::Duration(0)}, events, {}).ok());
        }
    }
    auto saved =
        session->capture(map, scene.domain.request.source.owner.child("native-session-restore"), eve::Revision(7))
            .expect("same-boundary source session");
    session->close(eve::spawn::DetachPolicy::RemoveUnits).expect("source explicit unload");
    return std::make_shared<const eve::map_rts_spawn::RTSMapCheckpoint>(std::move(saved));
}
TEST_CASE("RTS restored map session owns source-retired content core and both retained handoff stages") {
    for (int mode : {-1, 0, 1, 2, 3}) {
        auto saved    = savedSessionHandoff(mode);
        auto admitted = eve::map_rts_spawn::RTSMapRestoreState::prepare(saved, test_rts_spawn::restoreHash).takeValue();
        auto table    = std::make_shared<ecs::Table>();
        ecs::ScopedTable  scope(*table);
        eve::map::Map     map;
        eve::crowd::Crowd crowd;
        eve::rts::RTS     domain(table);
        domain.setCrowdProvider(&crowd);
        domain.rebuildState(saved->roots()->get()).expect("target isolated native roots");
        auto prepared = eve::map_rts_spawn::PreparedRTSMapSession::prepareRestored(map, admitted, domain,
                                                                                   std::vector<eve::spawn::Point>{})
                            .expect("actual saved map/source/core preparation");
        REQUIRE_EQ(map.getLayerCount(), 0);
        auto session = prepared.commit(map).expect("map and restored session publication");
        REQUIRE(!prepared.commit(map).ok());
        REQUIRE_EQ(map.getLayerCount(), 1);
        auto resumed = session->capture(map, saved->saveEpoch(), saved->revision()).expect("restored same boundary");
        REQUIRE_EQ(resumed.coreTick().value(), std::uint64_t(mode < 0 ? 0 : 1));
        REQUIRE_EQ(resumed.pendingDamage().size(), saved->pendingDamage().size());
        REQUIRE_EQ(resumed.pendingLifecycle().size(), saved->pendingLifecycle().size());
        REQUIRE(resumed.provenance()->get().sourceIncarnation() != saved->provenance()->get().sourceIncarnation());
        const auto source = session->spawning().sourceHandles()[0];
        if (mode >= 0) REQUIRE(source.incarnation != saved->core()->get().membership()[0].source.incarnation);
        if (mode > 0) {
            REQUIRE_EQ(session->advance(map, {eve::SimulationTick(2), eve::Duration(0)}, {}, {})
                           .expect("retained handoff exactly once")
                           .units,
                       std::uint32_t(0));
            REQUIRE_EQ(session->spawning().world().inspect(source).takeValue().alive, std::uint32_t(0));
            REQUIRE_EQ(session->advance(map, {eve::SimulationTick(3), eve::Duration(1000000000)}, {}, {})
                           .expect("future recipe without live registry")
                           .units,
                       std::uint32_t(1));
            REQUIRE_EQ(crowd.getAgentCount(), 1);
            REQUIRE_EQ(session->spawning().world().inspect(source).takeValue().total, std::uint64_t(2));
        }
        if (mode < 0) {
            REQUIRE_EQ(session->advance(map, {eve::SimulationTick(1), eve::Duration(0)}, {}, {})
                           .expect("saved future before first publication")
                           .units,
                       std::uint32_t(1));
            REQUIRE_EQ(crowd.getAgentCount(), 1);
        }
        session->close(eve::spawn::DetachPolicy::RemoveUnits).expect("restored source cleanup");
    }
}
TEST_CASE("RTS restored map candidate rejects missing roots and supports provider retry before publication") {
    auto saved    = savedSessionHandoff(0);
    auto admitted = eve::map_rts_spawn::RTSMapRestoreState::prepare(saved, test_rts_spawn::restoreHash).takeValue();
    auto table    = std::make_shared<ecs::Table>();
    ecs::ScopedTable  scope(*table);
    eve::map::Map     map;
    eve::crowd::Crowd crowd;
    auto              domain = std::make_unique<eve::rts::RTS>(table);
    domain->setCrowdProvider(&crowd);
    REQUIRE(!eve::map_rts_spawn::PreparedRTSMapSession::prepareRestored(map, admitted, *domain,
                                                                        std::vector<eve::spawn::Point>{})
                 .ok());
    domain->rebuildState(saved->roots()->get()).expect("real target roots");
    auto prepared = eve::map_rts_spawn::PreparedRTSMapSession::prepareRestored(map, admitted, *domain,
                                                                               std::vector<eve::spawn::Point>{})
                        .takeValue();
    domain->setCrowdProvider(nullptr);
    REQUIRE(!prepared.commit(map).ok());
    REQUIRE_EQ(map.getLayerCount(), 0);
    domain->setCrowdProvider(&crowd);
    auto session = prepared.commit(map).expect("same candidate after provider retry");
    session->close(eve::spawn::DetachPolicy::RemoveUnits).expect("explicit cleanup");
    auto another = eve::map_rts_spawn::PreparedRTSMapSession::prepareRestored(map, admitted, *domain,
                                                                              std::vector<eve::spawn::Point>{});
    REQUIRE(!another.ok());  // Saved member was removed by close; no implicit recreation.
}

TEST_CASE("RTS restored map commit rejects roots lost or replaced after preparation") {
    auto saved    = savedSessionHandoff(0);
    auto admitted = eve::map_rts_spawn::RTSMapRestoreState::prepare(saved, test_rts_spawn::restoreHash).takeValue();
    auto table    = std::make_shared<ecs::Table>();
    ecs::ScopedTable  scope(*table);
    eve::map::Map     map;
    eve::crowd::Crowd crowd;
    auto              domain = std::make_unique<eve::rts::RTS>(table);
    domain->setCrowdProvider(&crowd);
    domain->rebuildState(saved->roots()->get()).expect("saved roots");
    auto prepared = eve::map_rts_spawn::PreparedRTSMapSession::prepareRestored(map, admitted, *domain,
                                                                               std::vector<eve::spawn::Point>{})
                        .takeValue();
    const auto subject = saved->roots()->get().units[0].subject;
    domain->prepareUnitRemoval(std::span(&subject, 1)).takeValue().commit().expect("external candidate root loss");
    REQUIRE(!prepared.commit(map).ok());
    REQUIRE_EQ(map.getLayerCount(), 0);
    crowd.setClampToField(false);
    crowd.resizeField(20, 20, 1.f, 0.f, 0.f);
    const auto&                body = saved->roots()->get().units[0];
    eve::rts::UnitBatchRequest replacement{
        subject, *saved->provenance()->get().units()[0].recipe, body.motion.x, body.motion.y, {}};
    auto created =
        domain->prepareUnitBatch(*domain->findFaction(saved->provenance()->get().faction()), std::span(&replacement, 1))
            .takeValue()
            .commit()
            .takeValue();
    REQUIRE(created.state == eve::rts::UnitBatchState::Committed);
    REQUIRE(!prepared.commit(map).ok());
    domain.reset();
    REQUIRE(!prepared.commit(map).ok());
    REQUIRE_EQ(map.getLayerCount(), 0);
}
TEST_CASE("RTS map CPU admission survives source retirement and retains no digest or target world") {
    auto                      saved         = savedSessionHandoff(0);
    auto                      table         = std::make_shared<ecs::Table>();
    auto                      unrelated     = std::make_unique<eve::rts::RTS>(table);
    const auto                controls      = eve::cap::listenerCount<eve::IGameplayControlProvider>();
    eve::SnapshotHashProvider rejectingHash = [](std::string_view) {
        return eve::Result<eve::ContentId>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected digest failure"));
    };
    REQUIRE(!eve::map_rts_spawn::RTSMapRestoreState::prepare(saved, rejectingHash).ok());
    REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), controls);
    int  digests  = 0;
    auto admitted = [&] {
        eve::SnapshotHashProvider temporaryHash = [&](std::string_view text) {
            ++digests;
            unrelated.reset();
            saved.reset();  // The preparation already pins its own immutable checkpoint owner.
            return test_rts_spawn::restoreHash(text);
        };
        return eve::map_rts_spawn::RTSMapRestoreState::prepare(saved, temporaryHash).takeValue();
    }();
    REQUIRE(!saved);
    REQUIRE(!unrelated);
    REQUIRE(digests > 0);
    const auto digestCount = digests;
    auto       moved       = std::move(admitted);
    REQUIRE(!admitted.core());
    REQUIRE(!admitted.checkpoint());
    // Reuse the same CPU candidate for two physically distinct target worlds after hash destruction.
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto              targetTable = std::make_shared<ecs::Table>();
        ecs::ScopedTable  scope(*targetTable);
        eve::map::Map     map;
        eve::crowd::Crowd crowd;
        eve::rts::RTS     domain(targetTable);
        domain.setCrowdProvider(&crowd);
        domain.rebuildState(moved.checkpoint()->roots()->get()).expect("actual native target roots");
        REQUIRE(!eve::map_rts_spawn::PreparedRTSMapSession::prepareRestored(map, admitted, domain,
                                                                            std::vector<eve::spawn::Point>{})
                     .ok());
        auto prepared = eve::map_rts_spawn::PreparedRTSMapSession::prepareRestored(map, moved, domain,
                                                                                   std::vector<eve::spawn::Point>{})
                            .takeValue();
        auto session = prepared.commit(map).takeValue();
        REQUIRE_EQ(session->spawning().world().lastConsumedTick(), moved.checkpoint()->coreTick());
        session->close(eve::spawn::DetachPolicy::RemoveUnits).expect("isolated retry cleanup");
        REQUIRE_EQ(digests, digestCount);
    }
}

TEST_CASE("RTS owning preparation hides its world from callbacks and preserves external registrations") {
    Scene                          scene;
    auto                           externalTable = std::make_shared<ecs::Table>();
    std::unique_ptr<eve::rts::RTS> external;
    const auto                     controls = eve::cap::listenerCount<eve::IGameplayControlProvider>();
    const auto                     catalogs = eve::cap::listenerCount<eve::IGameplayInstanceCatalog>();
    int                            reads    = 0;
    scene.files.onRead                      = [&] {
        ++reads;
        REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), controls + (external ? 1u : 0u));
        REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayInstanceCatalog>(), catalogs + (external ? 1u : 0u));
        if (!external) external = std::make_unique<eve::rts::RTS>(externalTable);
        REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), controls + 1);
    };
    auto runtime =
        eve::map_rts_spawn::RTSMapRuntime::prepare(
            runtimeContent(), {eve::Value::fromJson(scene.templates.value.canonicalJson()).takeValue()}, scene.binding,
            scene.files,
            eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse("01900000-0000-7000-8000-000000000889")),
            scene.domain.request.source.owner.child("hidden-runtime"), 42, {})
            .expect("world publication after complete preparation");
    REQUIRE(reads > 0);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), controls + 2);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayInstanceCatalog>(), catalogs + 2);
    runtime->advance({eve::SimulationTick(1), eve::Duration(100000000)}).expect("published world usable");
    runtime->close().expect("explicit close");
    runtime.reset();
    REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), controls + 1);
    external.reset();
    REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), controls);
}
TEST_CASE("RTS owning preparation withdraws all world listeners after filesystem callback failure") {
    Scene      scene;
    const auto controls = eve::cap::listenerCount<eve::IGameplayControlProvider>();
    const auto catalogs = eve::cap::listenerCount<eve::IGameplayInstanceCatalog>();
    scene.files.onRead  = [&] {
        REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), controls);
        REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayInstanceCatalog>(), catalogs);
        throw std::runtime_error("expected external read failure");
    };
    auto failed = eve::map_rts_spawn::RTSMapRuntime::prepare(
        runtimeContent(), {eve::Value::fromJson(scene.templates.value.canonicalJson()).takeValue()}, scene.binding,
        scene.files,
        eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse("01900000-0000-7000-8000-000000000889")),
        scene.domain.request.source.owner.child("hidden-failure"), 42, {});
    REQUIRE(!failed.ok());
    REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), controls);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayInstanceCatalog>(), catalogs);
}
TEST_CASE("RTS owning checkpoint retains actual content outside spawn recipes after game and files retire") {
    auto saved = [&] {
        Scene source;
        auto  content = eve::Value::fromJson(runtimeContent()).takeValue();
        content.find("units")->pushBack(
            eve::Value::fromJson(R"({"id":"reserve","weaponType":"club","health":77,"speed":4,"radius":0.35})")
                .takeValue());
        auto runtime =
            eve::map_rts_spawn::RTSMapRuntime::prepare(
                content.toJson().takeValue(),
                {eve::Value::fromJson(source.templates.value.canonicalJson()).takeValue()}, source.binding,
                source.files,
                eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse("01900000-0000-7000-8000-000000000889")),
                source.domain.request.source.owner.child("content-owner"), 42, {})
                .takeValue();
        runtime->advance({eve::SimulationTick(1), eve::Duration(100000000)}).expect("real source reinforcement");
        auto checkpoint =
            runtime->captureCheckpoint(source.domain.request.source.owner.child("saved-content"), eve::Revision(7))
                .takeValue();
        REQUIRE(checkpoint.definitions());
        REQUIRE_EQ(checkpoint.map()->get().provenance()->get().futureUnits().size(), std::size_t(1));
        runtime->close().expect("source normal unload");
        return checkpoint;
    }();  // Source game, registry, file service, templates and map all gone.
    auto moved = std::move(saved);
    REQUIRE(!saved.definitions());
    auto registry = eve::definitions::DefinitionRegistry::prepareRestored(moved.definitions()->get()).takeValue();
    REQUIRE(registry->resolve("spawn.template", "reinforcement").ok());
    REQUIRE(registry->resolve("unit", "reserve").ok());
    auto recipe =
        eve::rts::PreparedUnitArchetype::prepare(*registry, *eve::LogicalId::parse("unit:reserve")).takeValue();
    auto              table = std::make_shared<ecs::Table>();
    ecs::ScopedTable  scope(*table);
    eve::crowd::Crowd crowd;
    crowd.setClampToField(false);
    crowd.resizeField(20, 20, 1.f, 0.f, 0.f);
    eve::rts::RTS domain(table);
    domain.setCrowdProvider(&crowd);
    auto       faction = domain.newFaction(moved.map()->get().provenance()->get().faction()).takeValue();
    const auto id      = eve::SubjectRef::fromPersistentId(moved.saveEpoch().child("new-reserve"));
    eve::rts::UnitBatchRequest request{id, recipe, 3.f, 3.f, {}};
    auto created = domain.prepareUnitBatch(*faction, std::span(&request, 1)).takeValue().commit().takeValue();
    REQUIRE(created.state == eve::rts::UnitBatchState::Committed);
    auto* unit = domain.findUnit(id);
    REQUIRE(unit);
    REQUIRE_EQ(unit->durability()->state.health, 77.0);
    REQUIRE_EQ(crowd.getAgentCount(), 1);
    auto independent = eve::definitions::DefinitionRegistry::prepareRestored(moved.definitions()->get()).takeValue();
    registry->remove("unit", "reserve").expect("target content mutation is independent");
    REQUIRE(independent->resolve("unit", "reserve").ok());
}
TEST_CASE("RTS owning checkpoint freezes actual configured damage rather than recreating default rules") {
    auto saved = [&] {
        Scene source;
        auto  runtime =
            eve::map_rts_spawn::RTSMapRuntime::prepare(
                runtimeContent(), {eve::Value::fromJson(source.templates.value.canonicalJson()).takeValue()},
                source.binding, source.files,
                eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse("01900000-0000-7000-8000-000000000889")),
                source.domain.request.source.owner.child("damage-owner"), 42, {})
                .takeValue();
        runtime->advance({eve::SimulationTick(1), eve::Duration(100000000)}).expect("actual live source");
        eve::settlement::SettlementRule rule;
        rule.id           = "saved.damage";
        rule.value        = 7;
        rule.operation    = eve::settlement::RuleOperation::Add;
        rule.filter.kinds = {"damage"};
        eve::settlement::SettlementRuleSet rules;
        rules.configure({rule}).expect("actual game rule");
        int configured = 0;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* provider) {
            auto* domain = dynamic_cast<eve::rts::RTS*>(provider);
            if (!domain || !domain->ownedWorld() || domain->ownedWorld() == source.domain.table) return;
            const auto       world = domain->ownedWorld();
            ecs::ScopedTable scope(*world);
            if (domain->unitCount() != 1) return;
            domain->configureSettlementRules(rules).expect("actual bound damage service");
            ++configured;
        });
        REQUIRE_EQ(configured, 1);
        auto checkpoint =
            runtime->captureCheckpoint(source.domain.request.source.owner.child("saved-damage"), eve::Revision(1))
                .takeValue();
        REQUIRE(checkpoint.damage());
        runtime->close().expect("source normal unload");
        return checkpoint;
    }();
    auto moved = std::move(saved);
    REQUIRE(!saved.damage());
    auto artifact = moved.damage()
                        ->get()
                        .seal(moved.saveEpoch(), moved.revision(), moved.completedTick(), test_rts_spawn::restoreHash)
                        .takeValue();
    auto text = eve::serializeSnapshotEnvelope(artifact).takeValue();
    auto parsed =
        eve::combat::DamageCapture::parse(eve::parseSnapshotEnvelope(text, test_rts_spawn::restoreHash).takeValue(),
                                          moved.saveEpoch(), test_rts_spawn::restoreHash)
            .takeValue();
    auto                     damage = eve::combat::DamageRuntime::prepareRestored(parsed).takeValue();
    eve::combat::CombatState target{eve::SubjectRef::fromPersistentId(moved.saveEpoch().child("target")), 100, 100, 50,
                                    50};
    eve::combat::DamageRequest request;
    request.target       = target.subject;
    request.damageType   = "Damage.Physical.Slash";
    request.healthDamage = 10;
    auto result          = damage->apply(target, request).takeValue();
    REQUIRE_EQ(result.appliedHealthDamage, 17.0);
    REQUIRE_EQ(target.health, 83.0);
}
TEST_CASE("RTS native owning restore survives source retirement and continues real frames") {
    using namespace eve::map_rts_spawn;
    const auto               controls = eve::cap::listenerCount<eve::IGameplayControlProvider>();
    std::vector<std::string> expected;
    auto                     saved = [&] {
        auto command    = std::make_shared<eve::rts::CommandSpec>();
        command->kind   = eve::rts::OrderKind::AttackMove;
        command->target = {12, 12};
        Scene source;
        auto  runtime =
            RTSMapRuntime::prepare(
                runtimeContent(), {eve::Value::fromJson(source.templates.value.canonicalJson()).takeValue()},
                source.binding, source.files,
                eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse("01900000-0000-7000-8000-000000000889")),
                source.domain.request.source.owner.child("native-owner"), 42, command)
                .takeValue();
        runtime->advance({eve::SimulationTick(1), eve::Duration(100000000)}).expect("source actual spawn");
        auto checkpoint =
            runtime->captureCheckpoint(source.domain.request.source.owner.child("native-save"), eve::Revision(1))
                .takeValue();
        checkpoint.validateRestoreContext().expect("closed native context");
        expected.push_back(runtime->captureState().takeValue().toJson().takeValue());
        for (std::uint64_t tick = 2; tick <= 5; ++tick) {
            runtime->advance({eve::SimulationTick(tick), eve::Duration(100000000)}).expect("uninterrupted reference");
            expected.push_back(runtime->captureState().takeValue().toJson().takeValue());
        }
        runtime->close().expect("retire source");
        return std::make_shared<const RTSRuntimeCheckpoint>(std::move(checkpoint));
    }();
    REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), controls);
    auto sensingEnvelope =
        saved->sensing()
            ->get()
            .seal(saved->saveEpoch(), saved->revision(), saved->completedTick(), test_rts_spawn::restoreHash)
            .takeValue();
    auto sensingText   = eve::serializeSnapshotEnvelope(sensingEnvelope).takeValue();
    auto sensingParsed = eve::sensing::SensingCapture::parse(
                             eve::parseSnapshotEnvelope(sensingText, test_rts_spawn::restoreHash).takeValue(),
                             saved->saveEpoch(), test_rts_spawn::restoreHash)
                             .takeValue();
    REQUIRE_EQ(sensingParsed.subjects().size(), saved->sensing()->get().subjects().size());
    REQUIRE(sensingParsed.indexKind() == saved->sensing()->get().indexKind());
    auto frames = eve::rts::RTSFrameArchive::capture(saved->events()->get(), saved->saveEpoch(), saved->revision(),
                                                     saved->completedTick())
                      .takeValue();
    const auto frameText =
        eve::serializeSnapshotEnvelope(frames.seal(test_rts_spawn::restoreHash).takeValue()).takeValue();
    auto parsedFrames =
        eve::rts::RTSFrameArchive::parse(eve::parseSnapshotEnvelope(frameText, test_rts_spawn::restoreHash).takeValue(),
                                         saved->saveEpoch(), test_rts_spawn::restoreHash)
            .takeValue();
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(parsedFrames.seal(test_rts_spawn::restoreHash).takeValue()).takeValue(),
               frameText);
    auto combatArchive = eve::rts::RTSCombatArchive::capture(saved->combat()->get(), saved->saveEpoch(),
                                                             saved->revision(), saved->completedTick())
                             .takeValue();
    const auto combatText =
        eve::serializeSnapshotEnvelope(combatArchive.seal(test_rts_spawn::restoreHash).takeValue()).takeValue();
    auto combatParsed = eve::rts::RTSCombatArchive::parse(
                            eve::parseSnapshotEnvelope(combatText, test_rts_spawn::restoreHash).takeValue(),
                            saved->saveEpoch(), test_rts_spawn::restoreHash)
                            .takeValue();
    REQUIRE(combatParsed.state()->get().mirroredSubjects == saved->combat()->get().mirroredSubjects);
    auto attributeEnvelope = saved->unitAttributes()
                                 ->get()
                                 .seal(saved->revision(), saved->completedTick(), test_rts_spawn::restoreHash)
                                 .takeValue();
    const auto attributeText    = eve::serializeSnapshotEnvelope(attributeEnvelope).takeValue();
    auto       parsedAttributes = eve::rts::RTSUnitAttributeCapture::parse(
                                eve::parseSnapshotEnvelope(attributeText, test_rts_spawn::restoreHash).takeValue(),
                                saved->saveEpoch(), test_rts_spawn::restoreHash)
                                .takeValue();
    REQUIRE_EQ(parsedAttributes.members().size(), saved->unitAttributes()->get().members().size());
    auto sourceEnvelope = saved->map()
                              ->get()
                              .provenance()
                              ->get()
                              .seal(saved->revision(), saved->completedTick(), test_rts_spawn::restoreHash)
                              .takeValue();
    const auto sourceText   = eve::serializeSnapshotEnvelope(sourceEnvelope).takeValue();
    auto       parsedSource = eve::rts_spawn::RTSSpawnCapture::parse(
                            eve::parseSnapshotEnvelope(sourceText, test_rts_spawn::restoreHash).takeValue(),
                            saved->saveEpoch(), test_rts_spawn::restoreHash)
                            .takeValue();
    REQUIRE(parsedSource.sourceIncarnation().isZero());
    REQUIRE_EQ(parsedSource.units().size(), saved->map()->get().provenance()->get().units().size());
    REQUIRE_EQ(
        eve::serializeSnapshotEnvelope(
            parsedSource.seal(saved->revision(), saved->completedTick(), test_rts_spawn::restoreHash).takeValue())
            .takeValue(),
        sourceText);
    auto weaponEnvelope = saved->weapons()
                              ->get()
                              .seal(saved->revision(), saved->completedTick(), test_rts_spawn::restoreHash)
                              .takeValue();
    const auto weaponText    = eve::serializeSnapshotEnvelope(weaponEnvelope).takeValue();
    auto       parsedWeapons = eve::rts::RTSWeaponCapture::parse(
                             eve::parseSnapshotEnvelope(weaponText, test_rts_spawn::restoreHash).takeValue(),
                             saved->saveEpoch(), test_rts_spawn::restoreHash)
                             .takeValue();
    REQUIRE_EQ(parsedWeapons.members().size(), saved->weapons()->get().members().size());
    REQUIRE_EQ(
        eve::serializeSnapshotEnvelope(
            parsedWeapons.seal(saved->revision(), saved->completedTick(), test_rts_spawn::restoreHash).takeValue())
            .takeValue(),
        weaponText);
    auto prepared = PreparedRTSRuntimeRestore::prepare(saved, test_rts_spawn::restoreHash).takeValue();
    REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), controls);
    std::unique_ptr<RTSMapRuntime> restored;
    prepared.commit(restored).expect("publish independent complete owner");
    REQUIRE(restored);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), controls + 1);
    REQUIRE_EQ(restored->captureState().takeValue().toJson().takeValue(), expected.front());
    auto repeated = prepared.commit(restored);
    REQUIRE(!repeated.ok());
    for (std::uint64_t tick = 2; tick <= 5; ++tick) {
        restored->advance({eve::SimulationTick(tick), eve::Duration(100000000)}).expect("continue exact next frame");
        REQUIRE_EQ(restored->captureState().takeValue().toJson().takeValue(), expected.at(tick - 1));
    }
    auto next = restored->captureCheckpoint(saved->saveEpoch(), eve::Revision(2)).takeValue();
    REQUIRE_EQ(next.completedTick().value(), std::uint64_t(5));
    next.validateRestoreContext().expect("restored actual services remain joined");
    eve::rts::RTS* actual = nullptr;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* provider) {
        if (auto* domain = dynamic_cast<eve::rts::RTS*>(provider); domain && domain->ownedWorld()) actual = domain;
    });
    REQUIRE(actual);
    int callbacks = 0;
    actual->setResourceCredit([&](auto&, const auto&) -> eve::Result<eve::resource::Receipt> {
        ++callbacks;
        throw std::runtime_error("unadmitted callback must never run during checkpoint admission");
    });
    auto unadmitted = std::make_shared<const RTSRuntimeCheckpoint>(
        restored->captureCheckpoint(saved->saveEpoch(), eve::Revision(3)).takeValue());
    REQUIRE(!unadmitted->validateRestoreContext().ok());
    int                       digests = 0;
    eve::SnapshotHashProvider digest  = [&](std::string_view text) {
        ++digests;
        return test_rts_spawn::restoreHash(text);
    };
    REQUIRE(!unadmitted->seal(digest).ok());
    REQUIRE(!PreparedRTSRuntimeRestore::prepare(unadmitted, digest).ok());
    REQUIRE_EQ(digests, 0);
    auto  replacement = PreparedRTSRuntimeRestore::prepare(saved, test_rts_spawn::restoreHash).takeValue();
    auto* before      = restored.get();
    REQUIRE(!replacement.commit(restored).ok());
    REQUIRE_EQ(restored.get(), before);
    REQUIRE_EQ(restored->captureState().takeValue().toJson().takeValue(), expected.back());
    actual->setResourceCredit({});
    REQUIRE(!unadmitted->validateRestoreContext().ok());
    REQUIRE_EQ(callbacks, 0);
    replacement.commit(restored).expect("replace running owner as one complete world");
    REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), controls + 1);
    REQUIRE_EQ(restored->captureState().takeValue().toJson().takeValue(), expected.front());
    restored->close().expect("restored cleanup");
    restored.reset();
    REQUIRE_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), controls);
}
}  // namespace test_map_rts_session

namespace test_map_rts_session {
TEST_CASE("RTS runtime handoff file preserves owner clocks and rejects malformed phases") {
    using namespace eve;
    using namespace eve::map_rts_spawn;
    Scene scene;
    auto  runtime =
        RTSMapRuntime::prepare(
            runtimeContent(), {Value::fromJson(scene.templates.value.canonicalJson()).takeValue()}, scene.binding,
            scene.files, SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000889")),
            scene.domain.request.source.owner.child("handoff-owner"), 42, {})
            .expect("actual owning runtime");
    const auto epoch      = scene.domain.request.source.owner.child("handoff-file");
    auto       checkpoint = runtime->captureCheckpoint(epoch, Revision(3)).expect("actual ready boundary");
    std::optional<RTSRuntimeHandoff> handoff(
        RTSRuntimeHandoff::capture(checkpoint).expect("capture actual responsibility"));
    runtime->close().expect("retire live source");
    runtime.reset();
    const SnapshotHashProvider retiring = [&](std::string_view bytes) {
        handoff.reset();
        return test_rts_spawn::restoreHash(bytes);
    };
    auto envelope = handoff->seal(retiring).expect("seal while callback retires archive");
    REQUIRE(!handoff);
    auto wire = serializeSnapshotEnvelope(envelope).expect("write handoff file");
    auto parsed =
        RTSRuntimeHandoff::parse(parseSnapshotEnvelope(wire, test_rts_spawn::restoreHash).expect("read handoff file"),
                                 epoch, test_rts_spawn::restoreHash)
            .expect("admit handoff file");
    REQUIRE(parsed.phase() == RTSCheckpointPhase::Ready);
    CHECK_EQ(parsed.completedTick().value(), 0u);
    REQUIRE(!parsed.pendingStep());
    CHECK_EQ(serializeSnapshotEnvelope(parsed.seal(test_rts_spawn::restoreHash).expect("repeat handoff"))
                 .expect("repeat file"),
             wire);
    for (int mode = 0; mode < 7; ++mode) {
        auto  payload = envelope.payload;
        auto& root    = *payload.getIf<Value::Object>();
        if (mode == 0) root.emplace("unknown", true);
        if (mode == 1) root.at("phase") = "executing";
        if (mode == 2) root.at("pending") = Value::Array{"1", "1"};
        if (mode >= 3) {
            root.at("phase")   = "spawnPending";
            root.at("pending") = Value::Array{"1", "0"};
        }
        if (mode == 4) root.at("completed") = "18446744073709551615";
        if (mode == 5) root.at("pending") = Value::Array{"1", "9223372036854775808"};
        if (mode == 6) root.at("pending") = Value::Array{"1", "1"};
        auto bad =
            makeSnapshotEnvelope(envelope.type, envelope.schema, envelope.schemaVersion, epoch, envelope.revision,
                                 envelope.tick, std::move(payload), test_rts_spawn::restoreHash)
                .expect("seal malformed boundary");
        REQUIRE(!RTSRuntimeHandoff::parse(std::move(bad), epoch, test_rts_spawn::restoreHash).ok());
    }
    REQUIRE(!RTSRuntimeHandoff::parse(envelope, epoch.child("wrong"), test_rts_spawn::restoreHash).ok());
}
}  // namespace test_map_rts_session

namespace test_map_rts_session {
TEST_CASE("RTS whole runtime file survives source retirement and restores actual movement through owning publication") {
    using namespace eve;
    using namespace eve::map_rts_spawn;
    std::vector<std::string> expected;
    const auto               listeners = cap::listenerCount<IGameplayControlProvider>();
    const auto epoch = PersistentId::parse("01900000-0000-7000-8000-000000000889")->child("whole-runtime-file");
    auto       file  = [&] {
        Scene source;
        auto  command   = std::make_shared<rts::CommandSpec>();
        command->kind   = rts::OrderKind::AttackMove;
        command->target = {12, 12};
        auto runtime    = RTSMapRuntime::prepare(
                           runtimeContent(), {Value::fromJson(source.templates.value.canonicalJson()).takeValue()},
                           source.binding, source.files,
                           SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000889")),
                           source.domain.request.source.owner.child("file-owner"), 42, command)
                           .expect("source runtime");
        runtime->advance({SimulationTick(1), Duration(100000000)}).expect("actual spawn");
        auto saved = std::make_unique<RTSRuntimeCheckpoint>(runtime->captureCheckpoint(epoch, Revision(7)).takeValue());
        const auto reference =
            serializeSnapshotEnvelope(saved->seal(test_rts_spawn::restoreHash).takeValue()).takeValue();
        bool retired  = false;
        auto envelope = saved
                            ->seal([&](std::string_view text) {
                                if (!retired) {
                                    REQUIRE(!saved->seal(test_rts_spawn::restoreHash).ok());
                                    retired = true;
                                    saved.reset();
                                }
                                return test_rts_spawn::restoreHash(text);
                            })
                            .expect("facts pinned before source checkpoint retirement");
        REQUIRE(retired);
        REQUIRE_EQ(serializeSnapshotEnvelope(envelope).takeValue(), reference);
        expected.push_back(runtime->captureState().takeValue().toJson().takeValue());
        for (std::uint64_t tick = 2; tick <= 5; ++tick) {
            runtime->advance({SimulationTick(tick), Duration(100000000)}).expect("reference continuation");
            expected.push_back(runtime->captureState().takeValue().toJson().takeValue());
        }
        runtime->close().expect("retire source world and services");
        return reference;
    }();
    REQUIRE_EQ(cap::listenerCount<IGameplayControlProvider>(), listeners);
    auto envelope = parseSnapshotEnvelope(file, test_rts_spawn::restoreHash).takeValue();
    auto parsed   = RTSRuntimeCheckpoint::parse(envelope, epoch, test_rts_spawn::restoreHash).expect("read whole file");
    auto resealed = parsed.seal(test_rts_spawn::restoreHash).expect("read facts reseal");
    REQUIRE_EQ(serializeSnapshotEnvelope(resealed).takeValue(), file);
    for (const auto& member : parsed.map()->get().core()->get().membership())
        REQUIRE(member.source.incarnation.isZero());
    auto admitted = PreparedRTSRuntimeRestore::prepare(std::make_shared<const RTSRuntimeCheckpoint>(std::move(parsed)),
                                                       test_rts_spawn::restoreHash)
                        .expect("same native joint admission");
    std::unique_ptr<RTSMapRuntime> restored;
    admitted.commit(restored).expect("publish one complete owning world");
    REQUIRE_EQ(restored->captureState().takeValue().toJson().takeValue(), expected[0]);
    for (std::uint64_t tick = 2; tick <= 5; ++tick) {
        restored->advance({SimulationTick(tick), Duration(100000000)}).expect("restored real movement");
        REQUIRE_EQ(restored->captureState().takeValue().toJson().takeValue(), expected[tick - 1]);
    }
    restored->close().expect("restored cleanup");
    auto reject = [&](Value payload) {
        auto malformed =
            makeSnapshotEnvelope(envelope.type, envelope.schema, envelope.schemaVersion, epoch, envelope.revision,
                                 envelope.tick, std::move(payload), test_rts_spawn::restoreHash)
                .takeValue();
        REQUIRE(!RTSRuntimeCheckpoint::parse(std::move(malformed), epoch, test_rts_spawn::restoreHash).ok());
    };
    auto unknown = envelope.payload;
    unknown.set("unexpected", true);
    reject(std::move(unknown));
    auto profile = envelope.payload;
    profile.set("profile", "custom.unregistered/v1");
    reject(std::move(profile));
    auto boundary = envelope.payload;
    auto damage   = parseSnapshotEnvelopeValue(*boundary.find("damage"), test_rts_spawn::restoreHash).takeValue();
    damage = makeSnapshotEnvelope(damage.type, damage.schema, damage.schemaVersion, epoch, Revision(8), damage.tick,
                                  damage.payload, test_rts_spawn::restoreHash)
                 .takeValue();
    boundary.set("damage", snapshotEnvelopeValue(damage).takeValue());
    reject(std::move(boundary));
    auto pipeline   = envelope.payload;
    auto rtsActions = parseSnapshotEnvelopeValue(*pipeline.find("actions"), test_rts_spawn::restoreHash).takeValue();
    auto actions =
        parseSnapshotEnvelopeValue(*rtsActions.payload.find("actions"), test_rts_spawn::restoreHash).takeValue();
    actions.payload.set("pipeline", int(action::ActionActivePipeline::Transaction));
    actions = makeSnapshotEnvelope(actions.type, actions.schema, actions.schemaVersion, actions.instanceId,
                                   actions.revision, actions.tick, actions.payload, test_rts_spawn::restoreHash)
                  .takeValue();
    rtsActions.payload.set("actions", snapshotEnvelopeValue(actions).takeValue());
    rtsActions =
        makeSnapshotEnvelope(rtsActions.type, rtsActions.schema, rtsActions.schemaVersion, rtsActions.instanceId,
                             rtsActions.revision, rtsActions.tick, rtsActions.payload, test_rts_spawn::restoreHash)
            .takeValue();
    pipeline.set("actions", snapshotEnvelopeValue(rtsActions).takeValue());
    reject(std::move(pipeline));
    auto future = makeSnapshotEnvelope(envelope.type, envelope.schema, SchemaVersion(2), epoch, envelope.revision,
                                       envelope.tick, envelope.payload, test_rts_spawn::restoreHash)
                      .takeValue();
    auto unsupported = RTSRuntimeCheckpoint::parse(std::move(future), epoch, test_rts_spawn::restoreHash);
    REQUIRE(!unsupported.ok());
    REQUIRE(unsupported.status().primaryDiagnostic()->code() == DiagnosticCode::Unsupported);
    REQUIRE(!RTSRuntimeCheckpoint::parse(envelope, epoch.child("foreign"), test_rts_spawn::restoreHash).ok());
}
}  // namespace test_map_rts_session
