// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"map_rpg_spawn"}
#include <functional>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <typeindex>
#include "MapSpawnSpaceFixture.h"
#include "common/Capability.h"
#include "common/SnapshotHash.h"
#include "common/StateAccess.h"
#include "graphics/Graphics.h"
#include "graphics/ISolidRectRenderer.h"
#include "map/MapGridState.h"
#include "map/PreparedMapLoad.h"
#include "map/rpg_spawn/MapRPGPresentation.h"
#include "map/rpg_spawn/MapRPGSpawn.h"
#include "map/rpg_spawn/RPGMapArchive.h"
#include "map/rpg_spawn/RPGMapCheckpoint.h"
#include "map/rpg_spawn/RPGMapRuntime.h"
#include "map/rpg_spawn/RPGMapSession.h"
#include "map/spawn/MapSpawnAssets.h"
#include "rpg/Effect.h"
#include "rpg/ImplementationRequirement.h"
#include "rpg/RPG.h"
#include "rpg/SimulationEventArchive.h"
#include "rpg/SimulationEvents.h"
#include "rpg/SkillConditionBindings.h"
#include "rpg/SkillSystem.h"
#include "rpg/StatusSystem.h"
#include "rpg/spawn/RPGSpawnCapture.h"
#include "rpg/spawn/RPGSpawnJointRestore.h"
#include "rpg/spawn/RPGSpawnRestoreState.h"
#include "spawn/SpawnRestoreState.h"
namespace test_map_spawn_space {

namespace {
eve::Result<eve::ContentId> checkpointHash(std::string_view text) {
    std::uint64_t state = 14695981039346656037ULL;
    for (const unsigned char byte : text) state = (state ^ byte) * 1099511628211ULL;
    eve::ContentId::Bytes bytes{};
    for (auto& byte : bytes) {
        state = state * 6364136223846793005ULL + 1;
        byte  = static_cast<std::uint8_t>(state >> 32);
    }
    return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
}
}  // namespace

TEST_CASE("Owned RPG runtime saves queues and independent lifecycle acknowledgements across source destruction") {
    Fixture           f;
    Assets            files;
    eve::Value::Array cells(100, eve::Value(1));
    files.text["forest/map.json"] =
        eve::Value::object(
            {{"width", 10}, {"height", 10}, {"tilewidth", 1}, {"tileheight", 1}, {"data", std::move(cells)}})
            .toJson()
            .expect("map asset");
    files.text["forest/spawns.json"] = f.map->canonicalJson();
    auto binding = eve::Value::fromJson(R"({"schema":"eve.map-spawn-binding","version":1,"map_id":"forest",
        "map_uri":"project://forest/map.json","spawns_uri":"project://forest/spawns.json",
        "pixel_to_world":{"origin":[0,0],"scale":1}})")
                       .expect("binding");
    auto unit = eve::Value::fromJson(R"({"schema":"eve.rpg.unit","version":1,"id":"wolf","display_name":"Wolf",
        "skill":"spatial.basic","attack":7,"defense":2,"max_hp":20,"speed":3})")
                    .expect("unit");
    auto compiled = Templates(*f.units)
                        .resolve(eve::DefinitionRef::parse("spawn.template:camp").expect("template ref"))
                        .expect("template");
    auto source = eve::Value::fromJson(compiled.canonicalJson()).expect("template source");
    auto runtime =
        eve::map_rpg_spawn::RPGMapRuntime::prepare({unit}, {source}, binding, files, f.owner.child("runtime"), 42, .4)
            .expect("runtime");
    REQUIRE(!runtime->advance({eve::SimulationTick(0), eve::Duration(1)}).ok());
    REQUIRE(!runtime->advance({eve::SimulationTick(1), eve::Duration(0)}).ok());
    runtime->advance({eve::SimulationTick(1), eve::Duration(1)}).expect("first frame");
    auto        state = runtime->captureState().expect("state");
    const auto& rows  = *state.find("sources")->getIf<eve::Value::Array>();
    REQUIRE_EQ(rows.size(), std::size_t(1));
    CHECK_EQ(rows[0].find("total")->asString(), "1");
    CHECK_EQ(rows[0].find("wave")->asInt(), 0);
    CHECK_EQ(rows[0].find("phase")->asString(), "running");
    const auto& subjects = *rows[0].find("subjects")->getIf<eve::Value::Array>();
    REQUIRE_EQ(subjects.size(), std::size_t(1));
    const auto subject = eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse(subjects[0].asString()));
    struct RuntimeRenderer final : eve::graphics::ISolidRectRenderer {
        eve::map_rpg_spawn::RPGMapRuntime& runtime;
        std::size_t                        draws    = 0;
        bool                               rejected = false;
        explicit RuntimeRenderer(eve::map_rpg_spawn::RPGMapRuntime& value) : runtime(value) {}
        void drawSolidRectRGBA(float, float, float, float, float, float, float, float) override {
            ++draws;
            rejected = !runtime.close().ok() && !runtime.captureState().ok();
        }
    } renderer(*runtime);
    REQUIRE_EQ(runtime->draw(renderer, {}).expect("owned presentation"), 1);
    CHECK_EQ(renderer.draws, 1);
    CHECK(renderer.rejected);

    REQUIRE_EQ(runtime->applyDamage(subject, 2).expect("damage one"), 2.0);
    auto prefix = runtime->readEvents().expect("prefix");
    REQUIRE_EQ(prefix.events().vitals.size(), std::size_t(1));
    REQUIRE_EQ(runtime->applyDamage(subject, 5).expect("damage two"), 5.0);
    runtime->acknowledge(prefix).expect("ack first prefix after append");
    REQUIRE(!runtime->acknowledge(prefix).ok());
    REQUIRE_EQ(runtime->readEvents().expect("remaining").events().vitals.size(), std::size_t(1));
    runtime->applyDamage(subject, 20).expect("death");
    REQUIRE(!runtime->draw(renderer, {}).ok());
    CHECK_EQ(renderer.draws, 1);
    auto oldReceipt = runtime->readEvents().expect("three pending resource facts");
    REQUIRE_EQ(oldReceipt.events().vitals.size(), std::size_t(3));
    const auto epoch = f.owner.child("save");
    eve::rpg::StatusSystem::registerLifecycleHook("unversioned", [](const eve::rpg::StatusChangeEvent&) {});
    REQUIRE(!runtime->captureCheckpoint(epoch, eve::Revision(3)).ok());
    eve::rpg::StatusSystem::clearLifecycleHooks();
    auto checkpoint = runtime->captureCheckpoint(epoch, eve::Revision(3)).expect("joined checkpoint");
    runtime.reset();
    files.text.clear();
    eve::rpg::SkillRegistry::clear();
    CHECK(!oldReceipt.events().vitals[0].actor.resolve());
    auto envelope = checkpoint.seal(checkpointHash).expect("seal after source retirement");
    auto text     = eve::serializeSnapshotEnvelope(envelope).expect("file");
    auto input    = eve::parseSnapshotEnvelope(text, checkpointHash).expect("file input");
    eve::rpg::StatusSystem::registerApplyCondition(
        "unversioned",
        [](eve::rpg::RPGActor*, const eve::rpg::EffectDefinition&, const std::string&, std::string&) { return true; });
    REQUIRE(!eve::map_rpg_spawn::PreparedRPGRuntimeRestore::prepare(input, epoch, checkpointHash).ok());
    eve::rpg::StatusSystem::clearApplyConditions();
    auto restore = eve::map_rpg_spawn::PreparedRPGRuntimeRestore::prepare(input, epoch, checkpointHash)
                       .expect("admit whole runtime");
    std::unique_ptr<eve::map_rpg_spawn::RPGMapRuntime> active;
    auto previous = restore.commit(active).expect("publish whole runtime");
    REQUIRE(!previous);
    REQUIRE(active != nullptr);
    REQUIRE(!restore.commit(active).ok());
    REQUIRE(!active->acknowledge(oldReceipt).ok());
    auto delivered = active->readEvents().expect("restored queues");
    REQUIRE_EQ(delivered.events().vitals.size(), std::size_t(3));
    CHECK(delivered.events().vitals.back().actor.capturedSubject() == subject);
    CHECK(delivered.events().vitals.back().actor.presence() == eve::rpg::ActorEventPresence::Retired);
    CHECK(!delivered.events().vitals.back().actor.resolve());
    active->advance({eve::SimulationTick(2), eve::Duration(1000000000)}).expect("consume retained death");
    active->advance({eve::SimulationTick(3), eve::Duration(1000000000)}).expect("replacement frame");
    auto        renewed = active->captureState().expect("renewed source");
    const auto& after   = *renewed.find("sources")->getIf<eve::Value::Array>();
    CHECK_EQ(after[0].find("alive")->asInt(), 1);
    const auto& replacements = *after[0].find("subjects")->getIf<eve::Value::Array>();
    REQUIRE_EQ(replacements.size(), std::size_t(1));
    CHECK(replacements[0].asString() != subject.format());
    active->acknowledge(delivered).expect("confirm restored prefix");
    REQUIRE(active->readEvents().expect("confirmed queues").events().vitals.empty());
    auto confirmed = active->captureCheckpoint(epoch, eve::Revision(4)).expect("checkpoint after ack");
    auto saved     = confirmed.seal(checkpointHash).expect("save confirmed queues");
    auto again     = eve::map_rpg_spawn::PreparedRPGRuntimeRestore::prepare(saved, epoch, checkpointHash)
                     .expect("read confirmed queues");
    auto* unchangedOwner = active.get();
    eve::rpg::SkillSystem::registerCastCondition(
        "unversioned", [](eve::rpg::RPGActor*, const eve::rpg::SkillDefinition&, std::string&) { return false; });
    REQUIRE(!again.commit(active).ok());
    CHECK(active.get() == unchangedOwner);
    eve::rpg::SkillSystem::clearCastConditions();
    auto  legacy      = saved;
    auto* frameFields = legacy.payload.find("frame")->getIf<eve::Value::Object>();
    frameFields->erase("players");
    frameFields->erase("pending_activation");
    auto v1 = eve::makeSnapshotEnvelope(legacy.type, legacy.schema, eve::SchemaVersion(1), legacy.instanceId,
                                        legacy.revision, legacy.tick, legacy.payload, checkpointHash)
                  .expect("legacy v1 artifact");
    REQUIRE(eve::map_rpg_spawn::PreparedRPGRuntimeRestore::prepare(v1, epoch, checkpointHash).ok());
    auto retired = again.commit(active).expect("replace active whole owner");
    REQUIRE(retired != nullptr);
    REQUIRE(!retired->advance({eve::SimulationTick(4), eve::Duration(1000000000)}).ok());
    REQUIRE(!retired->applyDamage(subject, 1).ok());
    REQUIRE(!retired->readEvents().ok());
    CHECK_EQ(retired->captureState().expect("retired owner state").find("phase")->asString(), "closed");
    CHECK(active->readEvents().expect("no event replay").events().vitals.empty());
    active->advance({eve::SimulationTick(4), eve::Duration(1000000000)}).expect("continue restored clock");
    auto malformed = envelope;
    malformed.payload.find("frame")->set("vitals_forwarded", "0");
    auto resealed =
        eve::makeSnapshotEnvelope(malformed.type, malformed.schema, malformed.schemaVersion, malformed.instanceId,
                                  malformed.revision, malformed.tick, malformed.payload, checkpointHash)
            .expect("malformed but integrity sealed");
    REQUIRE(!eve::map_rpg_spawn::PreparedRPGRuntimeRestore::prepare(resealed, epoch, checkpointHash).ok());
    CHECK(active->readEvents().expect("failed admission preserves active").events().vitals.empty());
    active->close().expect("explicit close");
    REQUIRE(!active->advance({eve::SimulationTick(5), eve::Duration(1)}).ok());
}

TEST_CASE("Owned RPG runtime preserves expired effect event content after source destruction") {
    Fixture           f;
    Assets            files;
    eve::Value::Array cells(100, eve::Value(1));
    files.text["forest/map.json"] =
        eve::Value::object(
            {{"width", 10}, {"height", 10}, {"tilewidth", 1}, {"tileheight", 1}, {"data", std::move(cells)}})
            .toJson()
            .expect("map asset");
    files.text["forest/spawns.json"] = f.map->canonicalJson();
    auto binding = eve::Value::fromJson(R"({"schema":"eve.map-spawn-binding","version":1,"map_id":"forest",
        "map_uri":"project://forest/map.json","spawns_uri":"project://forest/spawns.json",
        "pixel_to_world":{"origin":[0,0],"scale":1}})")
                       .expect("binding");
    auto unit = eve::Value::fromJson(R"({"schema":"eve.rpg.unit","version":1,"id":"wolf","display_name":"Wolf",
        "skill":"spatial.basic","attack":7,"defense":2,"max_hp":20,"speed":3})")
                    .expect("unit");
    auto compiled = Templates(*f.units)
                        .resolve(eve::DefinitionRef::parse("spawn.template:camp").expect("template ref"))
                        .expect("template");
    auto source = eve::Value::fromJson(compiled.canonicalJson()).expect("template source");
    auto runtime =
        eve::map_rpg_spawn::RPGMapRuntime::prepare({unit}, {source}, binding, files, f.owner.child("runtime"), 42, .4)
            .expect("runtime");
    REQUIRE(!runtime->advance({eve::SimulationTick(0), eve::Duration(1)}).ok());
    REQUIRE(!runtime->advance({eve::SimulationTick(1), eve::Duration(0)}).ok());
    runtime->advance({eve::SimulationTick(1), eve::Duration(1)}).expect("first frame");
    auto        state   = runtime->captureState().expect("actor identity");
    const auto& row     = state.find("sources")->getIf<eve::Value::Array>()->front();
    auto        subject = eve::SubjectRef::fromPersistentId(
        *eve::PersistentId::parse(row.find("subjects")->getIf<eve::Value::Array>()->front().asString()));
    runtime->applyDamage(subject, 1).expect("obtain weak actor observation");
    auto                       delivery = runtime->readEvents().expect("actor observation");
    eve::rpg::EffectDefinition effect;
    effect.id             = "runtime.historical.periodic";
    effect.durationPolicy = "duration";
    effect.duration       = 1;
    effect.period         = .25f;
    eve::rpg::EffectRegistry::registerEffect(effect);
    {
        auto actor = delivery.events().vitals.front().actor.resolve();
        REQUIRE(actor);
        eve::rpg::StatusSystem::apply(&actor->get(), effect.id, "history").expect("apply finite effect");
    }
    runtime->acknowledge(delivery).expect("ack observation");
    runtime->advance({eve::SimulationTick(2), eve::Duration(2000000000)}).expect("expire effect");
    auto historical = runtime->readEvents().expect("pending historical queues");
    REQUIRE(!historical.events().ticks.empty());
    REQUIRE(!historical.events().changes.empty());
    const auto epoch = f.owner.child("historical-events");
    eve::rpg::EffectRegistry::clear();
    REQUIRE(!runtime->captureCheckpoint(epoch, eve::Revision(1)).ok());
    eve::rpg::EffectRegistry::registerEffect(effect);
    auto checkpoint = runtime->captureCheckpoint(epoch, eve::Revision(1)).expect("include expired effect content");
    runtime.reset();
    files.text.clear();
    eve::rpg::EffectRegistry::clear();
    eve::rpg::SkillRegistry::clear();
    auto file     = checkpoint.seal(checkpointHash).expect("seal detached content");
    auto prepared = eve::map_rpg_spawn::PreparedRPGRuntimeRestore::prepare(file, epoch, checkpointHash)
                        .expect("admit historical content without original registry");
    std::unique_ptr<eve::map_rpg_spawn::RPGMapRuntime> restored;
    prepared.commit(restored).expect("restore whole owner");
    auto events = restored->readEvents().expect("restored history");
    CHECK_EQ(events.events().ticks.size(), historical.events().ticks.size());
    CHECK_EQ(events.events().changes.size(), historical.events().changes.size());
    CHECK_EQ(events.events().ticks.front().effectId, effect.id);
    restored->advance({eve::SimulationTick(3), eve::Duration(1000000000)}).expect("no expired effect execution");
    CHECK_EQ(restored->readEvents().expect("no tick replay").events().ticks.size(), events.events().ticks.size());
    restored->acknowledge(events).expect("ack historical prefix");
    REQUIRE(restored->readEvents().expect("history consumed").events().ticks.empty());
    restored->captureCheckpoint(epoch, eve::Revision(2)).expect("resave after ack");
    restored->close().expect("close restored owner");
}

TEST_CASE("Owned RPG activation binds external events and preserves explicit player capability") {
    Fixture           f;
    Assets            files;
    eve::Value::Array cells(100, eve::Value(1));
    files.text["map.json"] =
        eve::Value::object(
            {{"width", 10}, {"height", 10}, {"tilewidth", 1}, {"tileheight", 1}, {"data", std::move(cells)}})
            .toJson()
            .expect("map");
    auto  map       = eve::Value::fromJson(f.map->canonicalJson()).expect("map source");
    auto& instances = *map.find("instances")->getIf<eve::Value::Array>();
    auto  event     = instances.front();
    event.set("id", "event");
    event.set("activation", eve::Value::object({{"kind", "event"}, {"event", "alarm"}}));
    event.set("region",
              eve::Value::fromJson(R"({"kind":"points","space":"world2d","points":[[8,5]]})").expect("region"));
    instances.front().set(
        "activation", eve::Value::object({{"kind", "player_proximity"}, {"enter_distance", 3}, {"exit_distance", 5}}));
    instances.push_back(std::move(event));
    map.find("budgets")->getIf<eve::Value::Array>()->front().set("alive_limit", 2);
    files.text["spawns.json"] = map.toJson().expect("sidecar");
    auto binding = eve::Value::fromJson(R"({"schema":"eve.map-spawn-binding","version":1,"map_id":"forest",
        "map_uri":"project://map.json","spawns_uri":"project://spawns.json",
        "pixel_to_world":{"origin":[0,0],"scale":1}})")
                       .expect("binding");
    auto unit = eve::Value::fromJson(R"({"schema":"eve.rpg.unit","version":1,"id":"wolf","display_name":"Wolf",
        "skill":"spatial.basic","attack":7,"defense":2,"max_hp":20,"speed":3})")
                    .expect("unit");
    auto compiled =
        Templates(*f.units).resolve(eve::DefinitionRef::parse("spawn.template:camp").expect("ref")).expect("template");
    auto source = eve::Value::fromJson(compiled.canonicalJson()).expect("source");
    auto game   = eve::map_rpg_spawn::RPGMapRuntime::prepare({unit}, {source}, binding, files,
                                                             f.owner.child("activation"), 42, .4)
                    .expect("game");
    REQUIRE(!game->prepareActivation(std::vector<eve::spawn::Point>{{std::numeric_limits<double>::infinity(), 0}}, {})
                 .ok());
    REQUIRE(!game->prepareActivation(std::nullopt, {"missing"}).ok());
    REQUIRE(!game->prepareActivation(std::nullopt, {"alarm", "alarm"}).ok());
    game->advance({eve::SimulationTick(1), eve::Duration(1)}).expect("dormant sources");
    auto before = game->captureState().expect("before activation");
    for (const auto& row : *before.find("sources")->getIf<eve::Value::Array>()) CHECK_EQ(row.find("alive")->asInt(), 0);
    std::vector<eve::spawn::Point> positions{{5, 3}};
    auto                           input = game->prepareActivation(positions, {"alarm"}).expect("bind both inputs");
    positions[0]                         = {100, 100};
    REQUIRE_EQ(game->advance({eve::SimulationTick(2), eve::Duration(1)}, input).expect("activate both").units, 2);
    auto        state  = game->captureState().expect("owned player facts");
    const auto& points = *state.find("players")->getIf<eve::Value::Array>();
    CHECK_EQ(points[0].getIf<eve::Value::Array>()->at(0).asDouble(), 5.0);
    auto unchanged = game->prepareActivation(std::nullopt, {}).expect("preserve player capability");
    game->advance({eve::SimulationTick(3), eve::Duration(1)}, unchanged).expect("preserve");
    auto saved = game->captureCheckpoint(f.owner.child("activation-save"), eve::Revision(1))
                     .expect("freeze players")
                     .seal(checkpointHash)
                     .expect("v2");
    CHECK(saved.schemaVersion == eve::SchemaVersion(2));
    // A valid outer protocol size must also fit the admitted core's actual work bound.
    auto restricted = saved;
    auto mapEnvelope =
        eve::parseSnapshotEnvelopeValue(*restricted.payload.find("map"), checkpointHash).expect("map participant");
    auto runtimeEnvelope = eve::parseSnapshotEnvelopeValue(*mapEnvelope.payload.find("runtime"), checkpointHash)
                               .expect("runtime participant");
    auto coreEnvelope = eve::parseSnapshotEnvelopeValue(*runtimeEnvelope.payload.find("core"), checkpointHash)
                            .expect("core participant");
    coreEnvelope.payload.find("limits")->set("queued_commands", "1");
    auto reseal = [](eve::SnapshotEnvelope envelope) {
        return eve::makeSnapshotEnvelope(envelope.type, envelope.schema, envelope.schemaVersion, envelope.instanceId,
                                         envelope.revision, envelope.tick, envelope.payload, checkpointHash)
            .expect("reseal participant");
    };
    runtimeEnvelope.payload.set("core",
                                eve::snapshotEnvelopeValue(reseal(std::move(coreEnvelope))).expect("core value"));
    mapEnvelope.payload.set("runtime",
                            eve::snapshotEnvelopeValue(reseal(std::move(runtimeEnvelope))).expect("runtime value"));
    restricted.payload.set("map", eve::snapshotEnvelopeValue(reseal(std::move(mapEnvelope))).expect("map value"));
    restricted = reseal(std::move(restricted));
    REQUIRE(
        eve::map_rpg_spawn::PreparedRPGRuntimeRestore::prepare(restricted, restricted.instanceId, checkpointHash).ok());
    restricted.payload.find("frame")->set("players",
                                          eve::Value::Array{eve::Value::Array{5.0, 3.0}, eve::Value::Array{6.0, 3.0}});
    restricted     = reseal(std::move(restricted));
    auto* original = game.get();
    REQUIRE(!eve::map_rpg_spawn::PreparedRPGRuntimeRestore::prepare(restricted, restricted.instanceId, checkpointHash)
                 .ok());
    CHECK(game.get() == original);
    auto candidate = eve::map_rpg_spawn::PreparedRPGRuntimeRestore::prepare(saved, saved.instanceId, checkpointHash)
                         .expect("read players");
    auto retired = candidate.commit(game).expect("restore");
    REQUIRE(!game->advance({eve::SimulationTick(4), eve::Duration(1)}, input).ok());
    auto restored = game->captureState().expect("preserved spatial facts");
    CHECK_EQ(restored.find("players")->getIf<eve::Value::Array>()->front().getIf<eve::Value::Array>()->at(1).asDouble(),
             3.0);
    eve::SubjectRef camp;
    for (const auto& row : *restored.find("sources")->getIf<eve::Value::Array>())
        if (row.find("id")->asString() == "camp")
            camp = eve::SubjectRef::fromPersistentId(
                *eve::PersistentId::parse(row.find("subjects")->getIf<eve::Value::Array>()->front().asString()));
    REQUIRE(camp.isValid());
    auto empty = game->prepareActivation(std::vector<eve::spawn::Point>{}, {}).expect("known empty");
    game->advance({eve::SimulationTick(4), eve::Duration(1)}, empty).expect("pause proximity");
    CHECK(game->captureState().expect("empty players").find("players")->getIf<eve::Value::Array>()->empty());
    game->applyDamage(camp, 20).expect("paused camp death");
    game->advance({eve::SimulationTick(5), eve::Duration(10000000000)}, empty).expect("paused replacement");
    auto paused = game->captureState().expect("paused source");
    for (const auto& row : *paused.find("sources")->getIf<eve::Value::Array>())
        if (row.find("id")->asString() == "camp") CHECK_EQ(row.find("alive")->asInt(), 0);
    auto near = game->prepareActivation(std::vector<eve::spawn::Point>{{5, 3}}, {}).expect("near again");
    game->advance({eve::SimulationTick(6), eve::Duration(1000000000)}, near).expect("resume replacement");
    auto reactivated = game->captureState().expect("reactivated source");
    for (const auto& row : *reactivated.find("sources")->getIf<eve::Value::Array>())
        if (row.find("id")->asString() == "camp") CHECK_EQ(row.find("alive")->asInt(), 0);
    // Pause includes the reactivation transition's delta; only a subsequent active interval advances the timer.
    game->advance({eve::SimulationTick(7), eve::Duration(1000000000)}, near).expect("active replacement delay");
    auto resumed = game->captureState().expect("resumed source");
    for (const auto& row : *resumed.find("sources")->getIf<eve::Value::Array>())
        if (row.find("id")->asString() == "camp") CHECK_EQ(row.find("alive")->asInt(), 1);
}

TEST_CASE("Map RPG presentation draws actual actor positions without owning domain state") {
    Fixture f;
    auto    candidate = f.prepareAssets(f.mapModule).expect("scene");
    auto    session   = candidate.commit(f.mapModule).expect("published scene");
    session->advance(f.mapModule, {eve::SimulationTick(1), eve::Duration(0)}).expect("actor");
    struct Renderer final : eve::graphics::ISolidRectRenderer {
        std::vector<eve::spawn::Point> positions;
        std::function<void()>          callback;
        void drawSolidRectRGBA(float x, float y, float, float, float, float, float, float) override {
            positions.push_back({x, y});
            if (callback) callback();
        }
    } renderer;
    eve::map_rpg_spawn::ActorStyle style;
    style.width  = 2;
    style.height = 4;
    REQUIRE(eve::map_rpg_spawn::drawActors(*session, *f.domain, renderer, style).expect("draw actual actor") == 1);
    REQUIRE(renderer.positions.size() == 1);
    CHECK_EQ(renderer.positions[0].x, 4.0);
    CHECK_EQ(renderer.positions[0].y, 3.0);
    {
        auto                                                 view  = ecs::View<eve::rpg::RPGActor>(*f.table);
        auto*                                                actor = view.begin().entity();
        ecs::ComponentRef<eve::rpg_spawn::SpawnedActorState> state(actor);
        state->position = {7, 8};
    }
    renderer.positions.clear();
    REQUIRE(eve::map_rpg_spawn::drawActors(*session, *f.domain, renderer, style).expect("moved actor") == 1);
    CHECK_EQ(renderer.positions[0].x, 6.0);
    CHECK_EQ(renderer.positions[0].y, 6.0);
    {
        auto                                                 view = ecs::View<eve::rpg::RPGActor>(*f.table);
        ecs::ComponentRef<eve::rpg_spawn::SpawnedActorState> state(view.begin().entity());
        state->position.x = std::numeric_limits<double>::infinity();
    }
    renderer.positions.clear();
    REQUIRE(!eve::map_rpg_spawn::drawActors(*session, *f.domain, renderer, style).ok());
    CHECK(renderer.positions.empty());
    {
        auto                                                 view = ecs::View<eve::rpg::RPGActor>(*f.table);
        ecs::ComponentRef<eve::rpg_spawn::SpawnedActorState> state(view.begin().entity());
        state->position = {7, 8};
    }
    style.alpha = -1;
    renderer.positions.clear();
    REQUIRE(!eve::map_rpg_spawn::drawActors(*session, *f.domain, renderer, style).ok());
    CHECK(renderer.positions.empty());
    style.alpha       = 1;
    renderer.callback = [&] {
        session->close(eve::spawn::DetachPolicy::RemoveUnits).expect("renderer holds no ECS borrow");
    };
    REQUIRE(eve::map_rpg_spawn::drawActors(*session, *f.domain, renderer, style).expect("staged draw") == 1);
    renderer.positions.clear();
    renderer.callback = {};
    REQUIRE(eve::map_rpg_spawn::drawActors(*session, *f.domain, renderer, style).expect("closed scene") == 0);
    CHECK(renderer.positions.empty());
}


TEST_CASE("RPG map game session retains death handoff across rejected frames") {
    Fixture           f;
    Assets            files;
    eve::Value::Array cells(100, eve::Value(1));
    files.text["map.json"] = eve::Value::object({{"width", 10},
                                                 {"height", 10},
                                                 {"tilewidth", 1},
                                                 {"tileheight", 1},
                                                 {"data", eve::Value::array(std::move(cells))}})
                                 .toJson()
                                 .expect("map");
    files.text["spawns.json"] = f.map->canonicalJson();
    auto binding = eve::Value::fromJson(R"({"schema":"eve.map-spawn-binding","version":1,"map_id":"forest",
        "map_uri":"project://map.json","spawns_uri":"project://spawns.json",
        "pixel_to_world":{"origin":[0,0],"scale":1}})")
                       .expect("binding");
    unsigned                  policyCalls = 0;
    eve::rpg::SkillDefinition conditionalSkill;
    conditionalSkill.id            = "spatial.basic";
    conditionalSkill.castCondition = eve::condition::Condition::policyCall("map.actual.policy");
    eve::rpg::SkillRegistry::registerSkill(conditionalSkill);
    std::vector<eve::rpg::SkillConditionPolicy> policies{
        {"map.actual.policy", "map:policy:v1", {}, [&](const eve::rpg::RPGActor* actor, const eve::Value&) {
             REQUIRE(actor != nullptr);
             ++policyCalls;
             return std::optional<eve::condition::ConditionResult>(eve::condition::ConditionResult::success());
         }}};
    auto conditions = eve::rpg::SkillConditionBindings::prepare(policies).expect("actual map policy provider");
    auto prepared   = eve::map_rpg_spawn::PreparedRPGMapSession::prepare(
                        f.mapModule, binding, files, Templates(*f.units), f.table, f.units, f.owner.child("game"), 42,
                        R"({"schema":"eve.map.object-contract","version":1,"types":[]})", 0.4, {}, conditions)
                        .expect("game candidate");
    REQUIRE(f.count() == 0);
    auto game = prepared.commit(f.mapModule).expect("publish game");
    REQUIRE(!prepared.commit(f.mapModule).ok());
    bool observed        = false;
    bool closeRejected   = false;
    bool advanceRejected = false;
    bool captureRejected = false;
    auto subscription =
        game->subscribe([&](const auto&) {
                if (observed) return;
                observed        = true;
                captureRejected = !game->capture(f.mapModule, f.owner.child("save"), eve::Revision(1)).ok();
                closeRejected   = !game->close(eve::spawn::DetachPolicy::RemoveUnits).ok();
                advanceRejected = !game->advance(f.mapModule, {eve::SimulationTick(99), eve::Duration(0)}, {}, {}).ok();
                game->enqueue({game->spawning().sourceHandles()[0], eve::spawn::ControlKind::Activate})
                    .expect("observer queue");
            })
            .expect("game observer");
    REQUIRE(
        game->advance(f.mapModule, {eve::SimulationTick(1), eve::Duration(0)}, {}, {}).expect("first frame").units ==
        1);
    REQUIRE((observed && closeRejected && advanceRejected && captureRejected));
    auto futureSource = eve::Value::fromJson(R"({"schema":"eve.rpg.unit","version":1,"id":"wolf","display_name":"Wolf",
        "skill":"spatial.basic","attack":70,"defense":2,"max_hp":20,"speed":3})")
                            .expect("future update");
    f.units->publish(futureSource).expect("current recipe differs from existing actor");
    auto frozen = game->capture(f.mapModule, f.owner.child("save"), eve::Revision(1)).expect("joined idle capture");
    REQUIRE_EQ(frozen.core().membership().size(), std::size_t(1));
    REQUIRE_EQ(frozen.domain().actors().size(), std::size_t(1));
    REQUIRE_EQ(frozen.domain().futureUnits().size(), std::size_t(1));
    REQUIRE(frozen.domain().futureUnits()[0].generation != frozen.domain().actors()[0].recipe->definition.generation);
    REQUIRE_EQ(policyCalls, 0u);
    REQUIRE_EQ(frozen.domain().implementations().size(), std::size_t(1));
    REQUIRE_EQ(frozen.domain().implementations()[0].revision, "map:policy:v1");
    CHECK(frozen.domain().saveEpoch() == f.owner.child("save"));
    CHECK(frozen.pendingLifecycle().empty());
    auto savedMapArtifact = frozen.sealMap(checkpointHash).expect("complete map artifact with live actor");
    auto savedArchive =
        eve::map_rpg_spawn::RPGMapArchive::prepare(savedMapArtifact, f.owner.child("save"), checkpointHash, conditions)
            .expect("admitted map runtime");
    const auto& savedJoint    = savedArchive->runtimeState();
    auto        coreState     = savedJoint.core().clone().expect("independent coordinated commit attempt");
    auto        restoredTable = std::make_shared<ecs::Table>();
    std::shared_ptr<eve::rpg_spawn::RPGSpawnFactory> restoredDomain =
        eve::rpg_spawn::RPGSpawnFactory::prepareRestored(restoredTable, savedJoint.domainOwner())
            .expect("actual saved factory");
    auto restoredOccupancy = std::make_shared<eve::rpg_spawn::RPGSpawnOccupancy>(restoredTable, 0.4);
    auto restoredSpace     = eve::map_spawn::MapSpawnSpace::prepareRestored(coreState, {}, restoredOccupancy)
                             .expect("saved spatial bindings");
    eve::map::Map restoredActorMap;
    auto          structural = frozen.assets()->prepareMap(restoredActorMap).expect("saved structural map");
    structural->applyGridState(*frozen.gridState()).expect("saved navigation facts");
    restoredSpace->setNavigation(structural->captureNavigation(0).expect("actual restored navigation"))
        .expect("install navigation");
    restoredSpace->setPlayers({}).expect("known no players");
    auto decorated = std::make_shared<eve::map_spawn::SpatialSpawnFactory>(restoredDomain, restoredSpace);
    auto nested    = std::make_shared<eve::map_spawn::SpatialSpawnFactory>(decorated, restoredSpace);
    REQUIRE(&nested->domainFactory() == restoredDomain.get());
    REQUIRE(&restoredDomain->domainFactory() == restoredDomain.get());
    auto restoredProviders =
        std::make_shared<eve::spawn::SpawnProviders>(eve::spawn::SpawnProviders{restoredSpace, nested});
    auto restoredCore =
        eve::spawn::SpawnWorld::restore(coreState, restoredProviders).expect("bind actual decorated factory");
    auto unrelatedTable   = std::make_shared<ecs::Table>();
    auto unrelatedFactory = eve::rpg_spawn::RPGSpawnFactory::prepareRestored(unrelatedTable, savedJoint.domainOwner())
                                .expect("same content but another actual factory");
    REQUIRE(!unrelatedFactory->materializeSavedActors(restoredCore, savedJoint).ok());
    restoredDomain->materializeSavedActors(restoredCore, savedJoint).expect("saved actors through spatial composition");
    auto savedSubject = frozen.core().membership()[0].subject;
    REQUIRE_EQ(restoredDomain->inspect(savedSubject).expect("actual restored actor").attack, 7.0);
    REQUIRE_EQ(policyCalls, 0u);

    const auto          source = game->spawning().sourceHandles()[0];
    eve::rpg::RPGActor* actor  = nullptr;
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*f.table);
        actor     = view.begin().entity();
    }
    REQUIRE(actor->skills()->conditionBindings == conditions);
    REQUIRE(eve::rpg::SkillSystem::canCast(actor, conditionalSkill.id));
    REQUIRE_EQ(policyCalls, 1u);
    {
        ecs::ComponentRef<eve::rpg_spawn::SpawnedActorState> state(actor);
        state->origin.reset();
        REQUIRE(!game->capture(f.mapModule, f.owner.child("save"), eve::Revision(1)).ok());
        auto foreign        = source;
        foreign.incarnation = eve::spawn::WorldIncarnation(~std::uint64_t{0});
        state->origin       = foreign;
        REQUIRE(!game->capture(f.mapModule, f.owner.child("save"), eve::Revision(1)).ok());
        state->origin = source;
    }
    REQUIRE_EQ(game->spawning().world().inspect(source).expect("capture rejection preserves ledger").alive, 1u);
    eve::rpg::VitalsSystem::takeDamage(actor, "hp", 100);
    std::vector<eve::rpg::VitalsEvent> events;
    eve::rpg::VitalsSystem::pollEvents(events);
    REQUIRE(!game->capture(f.mapModule, f.owner.child("save"), eve::Revision(1)).ok());
    ecs::DestroyEntity(actor);
    REQUIRE(!game->capture(f.mapModule, f.owner.child("save"), eve::Revision(1)).ok());
    REQUIRE(!game->advance(f.mapModule, {eve::SimulationTick(0), eve::Duration(0)}, events, {}).ok());
    REQUIRE(game->spawning().world().inspect(source).expect("unaccepted ledger").alive == 1);
    auto deferred =
        game->capture(f.mapModule, f.owner.child("save"), eve::Revision(2)).expect("retained death capture");
    REQUIRE_EQ(deferred.core().membership().size(), std::size_t(1));
    REQUIRE_EQ(deferred.pendingLifecycle().size(), std::size_t(1));
    REQUIRE(deferred.assets() != nullptr);
    CHECK(deferred.domain().actors().empty());
    CHECK(deferred.pendingLifecycle()[0].source == source);
    CHECK(deferred.pendingLifecycle()[0].subject == deferred.core().membership()[0].subject);
    CHECK(deferred.pendingLifecycle()[0].reason == eve::spawn::RemovalReason::Death);
    bool sealReentryRejected = false;
    auto guardedHash         = [&](std::string_view text) {
        sealReentryRejected = !deferred.seal(checkpointHash).ok();
        return checkpointHash(text);
    };
    auto bundle = deferred.seal(guardedHash).expect("actual map checkpoint bundle");
    REQUIRE(deferred.gridState() != nullptr);
    auto gridEnvelope = deferred.gridState()
                            ->seal(bundle.instanceId, bundle.revision, bundle.tick, checkpointHash)
                            .expect("same-boundary actual grid");
    auto assetEnvelope = deferred.assets()
                             ->seal(bundle.instanceId, bundle.revision, bundle.tick, checkpointHash)
                             .expect("same-boundary structural participant");
    auto admittedAssets = eve::map_spawn::MapSpawnAssets::parse(assetEnvelope, bundle.instanceId, checkpointHash)
                              .expect("actual checkpoint asset owner");
    REQUIRE(!admittedAssets->files().empty());

    bool mapReentryRejected = false;
    auto mapHash            = [&](std::string_view text) {
        mapReentryRejected = !deferred.sealMap(checkpointHash).ok() && !deferred.seal(checkpointHash).ok();
        return checkpointHash(text);
    };
    auto mapSave = deferred.sealMap(mapHash).expect("whole map artifact");
    REQUIRE(mapReentryRejected);
    auto mapArchive = eve::map_rpg_spawn::RPGMapArchive::prepare(mapSave, bundle.instanceId, checkpointHash, conditions)
                          .expect("same-boundary map admission");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(mapArchive->runtime()).expect("nested runtime"),
               eve::serializeSnapshotEnvelope(bundle).expect("original runtime"));
    eve::map::Map restoredMap;
    auto restoredCandidate = mapArchive->prepareMap(restoredMap).expect("actual structural plus runtime candidate");
    restoredCandidate->commit(restoredMap).expect("publish separate restored map");
    auto actualGrid = eve::map::MapGridState::capture(restoredMap).expect("actual restored grid");
    auto actualGridEnvelope =
        actualGrid->seal(bundle.instanceId, bundle.revision, bundle.tick, checkpointHash).expect("restored facts");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(actualGridEnvelope).expect("restored bytes"),
               eve::serializeSnapshotEnvelope(gridEnvelope).expect("saved bytes"));
    auto reseal = [](const eve::SnapshotEnvelope& input) {
        return eve::makeSnapshotEnvelope(input.type, input.schema, input.schemaVersion, input.instanceId,
                                         input.revision, input.tick, input.payload, checkpointHash)
            .expect("valid digest on intentionally invalid content");
    };
    for (unsigned mode = 0; mode < 6; ++mode) {
        auto invalid = mapSave;
        if (mode == 0) invalid.payload.set("unknown", true);
        if (mode == 1) invalid.schemaVersion = eve::SchemaVersion(2);
        if (mode == 2) invalid.instanceId = f.owner.child("different-save");
        if (mode == 3) {
            auto child     = gridEnvelope;
            child.revision = eve::Revision(99);
            invalid.payload.set("grid", eve::snapshotEnvelopeValue(reseal(child)).expect("different boundary"));
        }
        if (mode == 4) invalid.payload.set("grid", eve::Value{});
        if (mode == 5) {
            auto child = assetEnvelope;
            child.payload.set("unknown", true);
            invalid.payload.set("assets",
                                eve::snapshotEnvelopeValue(reseal(child)).expect("invalid asset participant"));
        }
        REQUIRE(
            !eve::map_rpg_spawn::RPGMapArchive::prepare(reseal(invalid), bundle.instanceId, checkpointHash, conditions)
                 .ok());
    }
    auto copiedMapSave   = mapSave;
    auto mutatingMapHash = [&](std::string_view text) {
        copiedMapSave.payload = eve::Value{};
        return checkpointHash(text);
    };
    REQUIRE(
        eve::map_rpg_spawn::RPGMapArchive::prepare(copiedMapSave, bundle.instanceId, mutatingMapHash, conditions).ok());

    REQUIRE(sealReentryRejected);
    auto joint =
        eve::rpg_spawn::RPGSpawnJointRestore::prepareBundle(bundle, f.owner.child("save"), checkpointHash, conditions)
            .expect("retained map deletion has verified durable input");
    REQUIRE_EQ(joint.pendingLifecycle().size(), std::size_t(1));
    REQUIRE(joint.pendingLifecycle()[0].reason == eve::spawn::RemovalReason::Death);


    struct Renderer final : eve::graphics::ISolidRectRenderer {
        int  draws = 0;
        void drawSolidRectRGBA(float, float, float, float, float, float, float, float) override { ++draws; }
    } renderer;
    REQUIRE(!game->draw(renderer, {}).ok());
    CHECK_EQ(renderer.draws, 0);
    // No second event poll or replay is needed: the composition retains the owning facts.
    game->advance(f.mapModule, {eve::SimulationTick(2), eve::Duration(0)}, {}, {}).expect("retry handoff");
    REQUIRE(game->spawning().world().inspect(source).expect("death accepted").alive == 0);
    auto after = game->capture(f.mapModule, f.owner.child("save"), eve::Revision(3)).expect("capture consumed handoff");
    CHECK(after.core().membership().empty());
    CHECK(after.pendingLifecycle().empty());
    CHECK_EQ(deferred.pendingLifecycle().size(), std::size_t(1));
    CHECK_EQ(frozen.domain().actors().size(), std::size_t(1));

    REQUIRE(game->advance(f.mapModule, {eve::SimulationTick(3), eve::Duration(1000000000)}, {}, {})
                .expect("replacement")
                .units == 1);
    REQUIRE(game->draw(renderer, {}).expect("real replacement drawn") == 1);
    REQUIRE(renderer.draws == 1);
    REQUIRE(game->close(eve::spawn::DetachPolicy::RemoveUnits).ok());
    CHECK_EQ(f.count(), std::size_t(0));
    REQUIRE(!game->capture(f.mapModule, f.owner.child("save"), eve::Revision(4)).ok());
    auto detached = std::move(deferred);
    CHECK(deferred.pendingLifecycle().empty());
    REQUIRE(!game->advance(f.mapModule, {eve::SimulationTick(4), eve::Duration(0)}, {}, {}).ok());
    game.reset();
    CHECK_EQ(detached.core().membership().size(), std::size_t(1));
    CHECK_EQ(detached.pendingLifecycle().size(), std::size_t(1));
    CHECK_EQ(frozen.domain().actors().size(), std::size_t(1));
    REQUIRE_EQ(policyCalls, 1u);
    auto resealed = detached.seal(checkpointHash).expect("bundle after session destruction");
    REQUIRE(detached.assets() != nullptr);
    REQUIRE(detached.gridState() != nullptr);
    auto detachedGrid = detached.gridState()
                            ->seal(bundle.instanceId, bundle.revision, bundle.tick, checkpointHash)
                            .expect("grid survives session destruction");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(detachedGrid).expect("detached grid"),
               eve::serializeSnapshotEnvelope(gridEnvelope).expect("original grid"));
    auto detachedAssets = detached.assets()
                              ->seal(bundle.instanceId, bundle.revision, bundle.tick, checkpointHash)
                              .expect("asset participant survives session destruction");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(detachedAssets).expect("detached source bytes"),
               eve::serializeSnapshotEnvelope(assetEnvelope).expect("original source bytes"));

    REQUIRE_EQ(eve::serializeSnapshotEnvelope(bundle).expect("original artifact"),
               eve::serializeSnapshotEnvelope(resealed).expect("detached artifact"));
    auto detachedMap = detached.sealMap(checkpointHash).expect("whole map artifact after source destruction");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(detachedMap).expect("detached map artifact"),
               eve::serializeSnapshotEnvelope(mapSave).expect("original map artifact"));
    auto pendingRestore =
        eve::map_rpg_spawn::PreparedRPGMapRestore::prepare(f.mapModule, detachedMap, bundle.instanceId, checkpointHash,
                                                           0.4, std::vector<eve::spawn::Point>{}, conditions)
            .expect("pending restore candidate");
    auto absentOld = pendingRestore.commit(f.mapModule, game).expect("publish pending world");
    REQUIRE(!absentOld);
    auto pendingRoundtrip = game->capture(f.mapModule, bundle.instanceId, bundle.revision)
                                .expect("pending handoff preserved")
                                .sealMap(checkpointHash)
                                .expect("pending map save");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(pendingRoundtrip).expect("restored pending bytes"),
               eve::serializeSnapshotEnvelope(detachedMap).expect("saved pending bytes"));
    REQUIRE_EQ(game->advance(f.mapModule, {eve::SimulationTick(2), eve::Duration(0)}, {}, {})
                   .expect("handoff restored death")
                   .units,
               std::uint32_t(0));
    REQUIRE_EQ(game->advance(f.mapModule, {eve::SimulationTick(3), eve::Duration(1000000000)}, {}, {})
                   .expect("saved replacement schedule")
                   .units,
               std::uint32_t(1));
    REQUIRE(!deferred.seal(checkpointHash).ok());
    // The old frozen participants remain usable independently of the live session.
}

TEST_CASE("Map RPG script binding uses canonical transactions and shared cached events") {
    Fixture           f;
    Assets            files;
    eve::Value::Array cells(100, eve::Value(1));
    files.text["map.json"] = eve::Value::object({{"width", 10},
                                                 {"height", 10},
                                                 {"tilewidth", 1},
                                                 {"tileheight", 1},
                                                 {"data", eve::Value::array(std::move(cells))}})
                                 .toJson()
                                 .expect("map asset");
    files.text["spawns.json"] = f.map->canonicalJson();
    struct FileBinding {
        eve::service::IFileSystem* previous = eve::cap::query<eve::service::IFileSystem>();
        ~FileBinding() { eve::cap::provide<eve::service::IFileSystem>(previous); }
    } fileBinding;
    eve::cap::provide<eve::service::IFileSystem>(&files);
    eve::rpg::RPG rpg;
    auto*         unrelated = rpg.newActor();
    REQUIRE(unrelated != nullptr);
    unrelated->setBaseAttribute("hp", 20);
    unrelated->setCurrent("hp", 20);
    eve::rpg::VitalsSystem::takeDamage(unrelated, "hp", 100);
    const auto unrelatedHandle = ecs::handle_of(unrelated);
    ecs::DestroyEntity(unrelated);
    rpg.update(0);
    const auto cachedCount = rpg.vitalsEvents().size();
    REQUIRE(cachedCount > 0);
    REQUIRE(rpg.vitalsEvents().back().actor.handle().table == unrelatedHandle.table);
    REQUIRE(rpg.vitalsEvents().back().actor.handle().id == unrelatedHandle.id);
    REQUIRE(rpg.vitalsEvents().back().actor.handle().generation == unrelatedHandle.generation);
    REQUIRE((rpg.vitalsEvents().back().actor.handle().type == unrelatedHandle.type));
    ssq::VM vm(4096, ssq::Libs::ALL);
    auto    eveTable = vm.addTable("eve");
    eve::map::Map::expose(eveTable);
    eve::rpg::RPG::expose(eveTable);
    eve::graphics::Graphics::expose(eveTable);
    eve::map_rpg_spawn::MapRPGSpawn::expose(eveTable);
    vm.addFunc("target", [&]() -> eve::map::Map* { return &f.mapModule; });
    vm.addFunc("filesAvailable",
               [&](bool enabled) { eve::cap::provide<eve::service::IFileSystem>(enabled ? &files : nullptr); });
    vm.addFunc("domain", [&]() -> eve::rpg::RPG* { return &rpg; });

    vm.addFunc("unitJson", [] {
        return std::string(R"({"schema":"eve.rpg.unit","version":1,"id":"wolf","display_name":"Wolf",
        "skill":"spatial.basic","attack":7,"defense":2,"max_hp":20,"speed":3})");
    });
    vm.addFunc("templateJson", [&] {
        return Templates(*f.units)
            .resolve(eve::DefinitionRef::parse("spawn.template:camp").expect("ref"))
            .expect("template")
            .canonicalJson();
    });
    vm.run(vm.compileSource(R"(
        local game = eve.MapRPGSpawn();
        assert(game.publishUnit(unitJson()).ok);
        assert(game.publishTemplate(templateJson()).ok);
        assert(!game.publishUnit("{}").ok);
        local binding = "{\"schema\":\"eve.map-spawn-binding\",\"version\":1,\"map_id\":\"forest\",\"map_uri\":\"project://map.json\",\"spawns_uri\":\"project://spawns.json\",\"pixel_to_world\":{\"origin\":[0,0],\"scale\":1}}";
        local contract = "{\"schema\":\"eve.map.object-contract\",\"version\":1,\"types\":[]}";
        assert(!game.commit(target()).ok);
        local badTypeRejected = false;
        try { game.prepare(null,binding,"01900000-0000-7000-8000-000000000099",42,contract,0.4); }
        catch (error) { badTypeRejected = true; }
        assert(badTypeRejected);
        filesAvailable(false);
        assert(!game.prepare(target(),binding,"01900000-0000-7000-8000-000000000099",42,contract,0.4).ok);
        filesAvailable(true);
        assert(game.prepare(target(),binding,"01900000-0000-7000-8000-000000000099",42,contract,0.4).ok);
        assert(!game.prepare(target(),binding,"01900000-0000-7000-8000-000000000099",42,contract,0.4).ok);
        assert(game.commit(target()).ok);
        assert(!game.commit(target()).ok);
        assert(!game.advance(target(),domain(),0,0).ok);
        local first = game.advance(target(),domain(),1,0);
        assert(first.ok && first.value.units == 1 && first.value.diagnostic_count == 0);
        local observed = game.captureDiagnostics();
        assert(observed.ok && observed.value.tick == 1 && observed.value.sources.len() == 1);
        local source = observed.value.sources[0];
        assert(source.alive == 1 && source.total == 1 && source.subjects.len() == 1);
        assert(!game.damage("bad", 1.0).ok);
        assert(!game.damage(source.subjects[0], -1.0).ok);
        local applied = game.damage(source.subjects[0], 100.0);
        assert(applied.ok && applied.value == 20.0);
        assert(game.damage(source.subjects[0], 100.0).value == 0.0);
        assert(!game.advance(target(),domain(),1,0).ok);
        assert(game.advance(target(),domain(),2,0).ok);
        assert(game.captureDiagnostics().value.sources[0].alive == 0);
        if (game.damage(source.subjects[0], 1.0).ok) throw "borrowed retired damage unexpectedly accepted";
        local replacement = game.advance(target(),domain(),3,1000000000);
        assert(replacement.ok && replacement.value.units == 0);
        // A dead body remains a real domain footprint at this fixture's single point.
        local blocked = game.captureDiagnostics().value.sources[0];
        assert(blocked.total == 1 && blocked.alive == 0 && blocked.blocked == "placement_blocked");
        badTypeRejected = false;
        try { game.draw(null,2.0,4.0,0.8,0.2,0.1,1.0); }
        catch (error) { badTypeRejected = true; }
        assert(badTypeRejected);
        assert(!game.close("unknown").ok);
        assert(game.close("remove").ok);
        assert(game.close("remove").ok);
        assert(!game.advance(target(),domain(),4,0).ok);
        assert(!game.captureDiagnostics().ok);
        assert(!game.damage("01900000-0000-7000-8000-000000000099",1.0).ok);
    )"));

    eve::map_rpg_spawn::MapRPGSpawn foreignRoot;
    vm.addFunc("foreignRuntime", [&]() -> eve::map_rpg_spawn::MapRPGSpawn* { return &foreignRoot; });
    vm.addFunc("retireRuntimeSources", [&] {
        files.text.clear();
        eve::rpg::SkillRegistry::clear();
    });
    vm.addFunc("runtimeVitalCount", [](const std::string& json) {
        auto hash     = eve::snapshotContentHashProvider();
        auto envelope = eve::parseSnapshotEnvelope(json, hash).expect("event file");
        auto archive  = eve::rpg::SimulationEventArchive::parse(envelope, envelope.instanceId, hash).expect("events");
        return std::int64_t(archive.queueSizes()[3]);
    });
    vm.run(vm.compileSource(R"(
        local game = eve.MapRPGSpawn();
        local owner = "01900000-0000-7000-8000-000000000199";
        local epoch = "01900000-0000-7000-8000-000000000299";
        local binding = "{\"schema\":\"eve.map-spawn-binding\",\"version\":1,\"map_id\":\"forest\",\"map_uri\":\"project://map.json\",\"spawns_uri\":\"project://spawns.json\",\"pixel_to_world\":{\"origin\":[0,0],\"scale\":1}}";
        assert(!game.commitRuntime().ok);
        filesAvailable(false);
        assert(!game.prepareRuntime("["+unitJson()+"]", "["+templateJson()+"]", binding, owner, 42, 0.4).ok);
        filesAvailable(true);
        assert(game.prepareRuntime("["+unitJson()+"]", "["+templateJson()+"]", binding, owner, 42, 0.4).ok);
        assert(!game.publishUnit(unitJson()).ok);
        assert(!game.captureRuntimeState().ok);
        assert(game.commitRuntime().ok);
        assert(!game.commitRuntime().ok);
        assert(!game.prepareRuntimeActivation("[[9007199254740993,0]]", "[]").ok);
        assert(!game.prepareRuntimeActivation("[[1]]", "[]").ok);
        assert(!game.prepareRuntimeActivation("[]", "[1]").ok);
        assert(!game.prepareRuntimeActivation("[]", "[\"missing\"]").ok);
        assert(game.prepareRuntimeActivation("null", "[]").ok);
        assert(!game.advanceRuntime(0,1).ok);
        local first = game.advanceRuntime(1,1);
        assert(first.ok && first.value.units == 1);
        local subject = game.captureRuntimeState().value.sources[0].subjects[0];
        assert(game.damage(subject,1.0).ok);
        local prefix = game.readRuntimeEvents(epoch,1).value;
        assert(prefix.hash_algorithm == game.snapshotHashAlgorithm());
        assert(runtimeVitalCount(prefix.snapshot) == 1);
        assert(game.damage(subject,1.0).ok);
        assert(!game.acknowledgeRuntimeEvents(prefix.receipt+1).ok);
        assert(game.acknowledgeRuntimeEvents(prefix.receipt).ok);
        assert(!game.acknowledgeRuntimeEvents(prefix.receipt).ok);
        local suffix = game.readRuntimeEvents(epoch,1).value;
        assert(runtimeVitalCount(suffix.snapshot) == 1);
        local other = foreignRuntime();
        local otherPrepared = other.prepareRuntime("["+unitJson()+"]", "["+templateJson()+"]", binding, owner, 43, 0.4);
        if (!otherPrepared.ok) throw "other root prepare: " + otherPrepared.status.summary;
        assert(other.commitRuntime().ok);
        assert(other.advanceRuntime(1,1).ok);
        local otherSubject = other.captureRuntimeState().value.sources[0].subjects[0];
        assert(other.damage(otherSubject,1.0).ok);
        local foreign = other.readRuntimeEvents(epoch,1).value;
        assert(foreign.receipt != suffix.receipt);
        assert(!other.acknowledgeRuntimeEvents(suffix.receipt).ok);
        assert(!game.acknowledgeRuntimeEvents(foreign.receipt).ok);
        assert(other.close("remove").ok);
        assert(game.freezeRuntime(epoch,1).ok);
        local saved = game.sealRuntime().value;
        assert(!game.prepareRuntimeRestore("{}",epoch).ok);
        assert(game.captureRuntimeState().value.sources[0].alive == 1);
        assert(game.prepareRuntimeRestore(saved,epoch).ok);
        assert(game.damage(subject,100.0).ok);
        retireRuntimeSources();
        assert(game.commitRuntimeRestore().ok);
        assert(!game.commitRuntimeRestore().ok);
        assert(!game.acknowledgeRuntimeEvents(suffix.receipt).ok);
        local restored = game.readRuntimeEvents(epoch,2).value;
        assert(restored.receipt > suffix.receipt && runtimeVitalCount(restored.snapshot) == 1);
        assert(game.damage(subject,100.0).value == 18.0);
        assert(game.acknowledgeRuntimeEvents(restored.receipt).ok);
        assert(runtimeVitalCount(game.readRuntimeEvents(epoch,2).value.snapshot) == 2);
        assert(game.advanceRuntime(2,1000000000).ok);
        assert(game.advanceRuntime(3,1000000000).value.units == 1);
        assert(!game.close("keep").ok);
        assert(game.close("remove").ok);
        assert(game.sealRuntime().ok); // detached checkpoint survives closing its source owner
        assert(!game.advanceRuntime(4,1).ok);
    )"));

    REQUIRE(rpg.vitalsEvents().size() == cachedCount);
    REQUIRE(rpg.vitalsEvents().back().actor.handle().table == unrelatedHandle.table);
    REQUIRE(rpg.vitalsEvents().back().actor.handle().id == unrelatedHandle.id);
    REQUIRE(rpg.vitalsEvents().back().actor.handle().generation == unrelatedHandle.generation);
    REQUIRE((rpg.vitalsEvents().back().actor.handle().type == unrelatedHandle.type));
}

TEST_CASE("RPG map restore replaces actual owners atomically and retries map publication failure") {
    ecs::Table                             mapTable;
    ecs::ScopedTable                       mapScope(mapTable);
    Fixture                                f;
    eve::rpg::detail::SimulationEvents     gameplayEvents;
    eve::rpg::detail::SimulationEventScope eventScope(gameplayEvents);
    eve::rpg::EffectDefinition             buff;
    buff.id             = "map.restore.active";
    buff.durationPolicy = "duration";
    buff.duration       = 2;
    buff.modifiers.push_back({"attack", "add", 3, 0});
    eve::rpg::EffectRegistry::registerEffect(buff);
    eve::rpg::SkillDefinition channel;
    channel.id       = "spatial.basic";
    channel.castTime = 2;
    eve::rpg::SkillRegistry::registerSkill(channel);
    Assets            files;
    eve::Value::Array cells(100, eve::Value(1));
    files.text["map.json"] = eve::Value::object({{"width", 10},
                                                 {"height", 10},
                                                 {"tilewidth", 1},
                                                 {"tileheight", 1},
                                                 {"data", eve::Value(std::move(cells))}})
                                 .toJson()
                                 .expect("map");
    files.text["spawns.json"] = f.map->canonicalJson();
    auto binding =
        eve::Value::fromJson(
            R"({"schema":"eve.map-spawn-binding","version":1,"map_id":"forest","map_uri":"project://map.json","spawns_uri":"project://spawns.json","pixel_to_world":{"origin":[0,0],"scale":1}})")
            .expect("binding");
    auto prepared = eve::map_rpg_spawn::PreparedRPGMapSession::prepare(
                        f.mapModule, binding, files, Templates(*f.units), f.table, f.units, f.owner.child("replace"),
                        42, R"({"schema":"eve.map.object-contract","version":1,"types":[]})", 0.4, {})
                        .expect("source game");
    auto     active           = prepared.commit(f.mapModule).expect("source map");
    unsigned oldNotifications = 0;
    auto     subscription     = active->subscribe([&](const auto&) { ++oldNotifications; }).expect("old observer");
    active->advance(f.mapModule, {eve::SimulationTick(1), eve::Duration(0)}, {}, {}).expect("original actor");
    const auto oldSource = active->spawning().sourceHandles()[0];
    auto       oldLease  = active->spawning().world().sourceLease(oldSource).expect("old source");
    auto*      oldOwner  = active.get();
    auto       oldTable  = active->world();
    {
        ecs::ScopedTable scope(*f.table);
        {
            auto view = ecs::View<eve::rpg::RPGActor>(*f.table);
            eve::rpg::StatusSystem::apply(view.begin().entity(), buff.id, "map-active")
                .expect("real active map modifier");
        }
        eve::rpg::StatusSystem::update(0.375).expect("partial domain effect before joint save");
        eve::rpg::RPGActor* actor = nullptr;
        {
            auto view = ecs::View<eve::rpg::RPGActor>(*f.table);
            actor     = view.begin().entity();
        }
        REQUIRE(eve::rpg::SkillSystem::beginCast(actor, channel.id, actor));
        eve::rpg::SkillSystem::update(0.375f);
    }
    std::vector<eve::rpg::StatusChangeEvent> delivered;
    eve::rpg::StatusSystem::pollChanges(delivered);
    REQUIRE_EQ(delivered.size(), std::size_t(1));
    const auto epoch = f.owner.child("joint-save");
    auto       saved = active->capture(f.mapModule, epoch, eve::Revision(1))
                     .expect("save actual world")
                     .sealMap(checkpointHash)
                     .expect("whole artifact");
    saved =
        eve::parseSnapshotEnvelope(eve::serializeSnapshotEnvelope(saved).expect("whole map file bytes"), checkpointHash)
            .expect("read independent active-state file");
    auto restore = eve::map_rpg_spawn::PreparedRPGMapRestore::prepare(f.mapModule, saved, epoch, checkpointHash, 0.4,
                                                                      std::vector<eve::spawn::Point>{})
                       .expect("admit restore");
    auto  revokedTarget  = std::make_unique<eve::map::Map>();
    auto& borrowedTarget = *revokedTarget;
    auto  revokingHash   = [&](std::string_view input) {
        revokedTarget.reset();
        return checkpointHash(input);
    };
    REQUIRE(!eve::map_rpg_spawn::PreparedRPGMapRestore::prepare(borrowedTarget, saved, epoch, revokingHash, 0.4,
                                                                std::vector<eve::spawn::Point>{})
                 .ok());
    auto* previousLayer = f.mapModule.getLayer(0);
    previousLayer->setTile(0, 0, 9);
    auto refresh = active->prepareNavigationRefresh(f.mapModule).expect("old changed map");
    refresh->commit(f.mapModule).expect("old navigation current");
    files.text.clear();
    {
        eve::map::Map other;
        REQUIRE(!restore.commit(other, active).ok());
        auto otherCandidate = eve::map_rpg_spawn::PreparedRPGMapRestore::prepare(other, saved, epoch, checkpointHash,
                                                                                 0.4, std::vector<eve::spawn::Point>{})
                                  .expect("other Map target");
        REQUIRE(!otherCandidate.commit(other, active).ok());

        REQUIRE(active.get() == oldOwner);
        REQUIRE(oldLease.isCurrent());
    }
    {
        ecs::Table       wrongTable;
        ecs::ScopedTable wrongScope(wrongTable);
        REQUIRE(!restore.commit(f.mapModule, active).ok());
        REQUIRE(active.get() == oldOwner);
        REQUIRE(oldLease.isCurrent());
    }
    struct RejectAdmission final : ecs::IComponentSlotBuffer {
        bool                  armed = true;
        std::function<void()> callback;
        std::uint32_t         size() const override { return 0; }
        const std::type_info& getType() const override { return typeid(RejectAdmission); }
        void                  ensure_space(std::uint32_t) override {
            if (armed) {
                if (callback) callback();
                throw std::runtime_error("injected map publication failure");
            }
        }
        void                       transfer_slot(std::uint32_t, std::uint32_t, ecs::IComponentSlotBuffer*) override {}
        ecs::IComponentSlotBuffer* ensure_equivalent(ecs::IComponentManager*) override { return nullptr; }
        void                       reset_slot(std::uint32_t) override {}
    };
    auto* failure = new RejectAdmission;
    mapTable.getOrCreateManager<eve::map::TileLayer>().components.emplace(std::type_index(typeid(RejectAdmission)),
                                                                          failure);
    bool reentryRejected = false;
    failure->callback    = [&] {
        reentryRejected = !restore.commit(f.mapModule, active).ok() &&
                          !active->capture(f.mapModule, epoch, eve::Revision(2)).ok() &&
                          !active->enqueue({oldSource, eve::spawn::ControlKind::Activate}).ok() &&
                          !active->subscribe([](const auto&) {}).ok();
    };
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
    REQUIRE(!restore.commit(f.mapModule, active).ok());
    REQUIRE(reentryRejected);
    REQUIRE(active.get() == oldOwner);
    REQUIRE(oldLease.isCurrent());
    REQUIRE(f.mapModule.getLayer(0) == previousLayer);
    REQUIRE_EQ(previousLayer->getTile(0, 0), 9);
    REQUIRE_EQ(f.count(), std::size_t(1));
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
    failure->armed    = false;
    failure->callback = {};
    auto previous     = restore.commit(f.mapModule, active).expect("same candidate retries successfully");
    REQUIRE(previous.get() == oldOwner);
    REQUIRE(active.get() != oldOwner);
    REQUIRE(active->world().get() != oldTable.get());
    REQUIRE(!oldLease.isCurrent());
    REQUIRE_EQ(f.mapModule.getLayer(0)->getTile(0, 0), 1);
    REQUIRE_EQ(f.count(), std::size_t(1));
    auto restoredSave = active->capture(f.mapModule, epoch, eve::Revision(1))
                            .expect("restored actual world")
                            .sealMap(checkpointHash)
                            .expect("whole restored artifact");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(restoredSave).expect("restored bytes"),
               eve::serializeSnapshotEnvelope(saved).expect("original bytes"));
    delivered.clear();
    eve::rpg::StatusSystem::pollChanges(delivered);
    REQUIRE(delivered.empty());
    REQUIRE(gameplayEvents.casts.empty());
    {
        ecs::ScopedTable scope(*active->world());
        {
            auto  view  = ecs::View<eve::rpg::RPGActor>(*active->world());
            auto* actor = view.begin().entity();
            REQUIRE_EQ(actor->getFinalAttribute("attack"), actor->getBaseAttribute("attack") + 3);
            REQUIRE(actor->skills()->casting.active);
            REQUIRE_EQ(actor->skills()->casting.remaining, 1.625f);
            REQUIRE(actor->skills()->casting.target.entity.table == active->world().get());
            REQUIRE_EQ(actor->skills()->casting.target.entity.id, actor->id);
        }
        auto expired = eve::rpg::StatusSystem::update(1.625).expect("restored effect expires at original boundary");
        REQUIRE_EQ(expired.expired, std::uint32_t(1));
        eve::rpg::SkillSystem::update(1.625f);
        REQUIRE_EQ(gameplayEvents.casts.size(), std::size_t(1));
        auto caster = gameplayEvents.casts.front().caster.resolve();
        auto target = gameplayEvents.casts.front().target.resolve();
        REQUIRE(caster.has_value());
        REQUIRE(target.has_value());
        REQUIRE(&caster->get() == &target->get());
        REQUIRE(caster->get().table == active->world().get());
        auto  view  = ecs::View<eve::rpg::RPGActor>(*active->world());
        auto* actor = view.begin().entity();
        REQUIRE_EQ(actor->getFinalAttribute("attack"), actor->getBaseAttribute("attack"));
    }
    eve::rpg::StatusSystem::pollChanges(delivered);
    REQUIRE_EQ(delivered.size(), std::size_t(1));
    REQUIRE_EQ(delivered[0].action, "expire");
    const auto notified = oldNotifications;
    active->advance(f.mapModule, {eve::SimulationTick(2), eve::Duration(0)}, {}, {}).expect("new world advances");
    REQUIRE_EQ(oldNotifications, notified);
    REQUIRE(!previous->advance(f.mapModule, {eve::SimulationTick(2), eve::Duration(0)}, {}, {}).ok());
    REQUIRE(!previous->subscribe([](const auto&) {}).ok());
    REQUIRE(!restore.commit(f.mapModule, active).ok());
    eve::rpg::EffectRegistry::remove(buff.id);
}
TEST_CASE("RPG empty map restores real grid and session without source placeholders") {
    ecs::Table       mapTable;
    ecs::ScopedTable mapScope(mapTable);
    Fixture          f;
    Assets           files;
    files.text["map.json"] = eve::Value::object({{"width", 10},
                                                 {"height", 10},
                                                 {"tilewidth", 1},
                                                 {"tileheight", 1},
                                                 {"data", eve::Value::array(eve::Value::Array(100, eve::Value(1)))}})
                                 .toJson()
                                 .expect("map");
    auto source = eve::Value::fromJson(f.map->canonicalJson()).expect("source");
    source.set("instances", eve::Value::Array{});
    files.text["spawns.json"] = source.toJson().expect("zero spawner sidecar");
    auto binding =
        eve::Value::fromJson(
            R"({"schema":"eve.map-spawn-binding","version":1,"map_id":"forest","map_uri":"project://map.json","spawns_uri":"project://spawns.json","pixel_to_world":{"origin":[0,0],"scale":1}})")
            .expect("binding");
    auto prepared = eve::map_rpg_spawn::PreparedRPGMapSession::prepare(
                        f.mapModule, binding, files, Templates(*f.units), f.table, f.units, f.owner.child("empty"), 42,
                        R"({"schema":"eve.map.object-contract","version":1,"types":[]})", 0.4, {})
                        .expect("empty session preparation");
    auto active = prepared.commit(f.mapModule).expect("publish empty map");
    REQUIRE(active->spawning().sourceHandles().empty());
    active->advance(f.mapModule, {eve::SimulationTick(1), eve::Duration(0)}, {}, {}).expect("empty tick");
    REQUIRE_EQ(f.count(), std::size_t(0));
    const auto epoch = f.owner.child("empty-save");
    auto       saved = active->capture(f.mapModule, epoch, eve::Revision(1))
                     .expect("empty capture")
                     .sealMap(checkpointHash)
                     .expect("empty complete artifact");
    files.text.clear();
    f.mapModule.getLayer(0)->setTile(0, 0, 9);
    auto restore = eve::map_rpg_spawn::PreparedRPGMapRestore::prepare(f.mapModule, saved, epoch, checkpointHash, 0.4,
                                                                      std::vector<eve::spawn::Point>{})
                       .expect("closed empty archive admission");
    auto previous = restore.commit(f.mapModule, active).expect("replace actual empty session");
    REQUIRE(active->spawning().sourceHandles().empty());
    REQUIRE_EQ(f.mapModule.getLayer(0)->getTile(0, 0), 1);
    auto again = active->capture(f.mapModule, epoch, eve::Revision(1))
                     .expect("restored capture")
                     .sealMap(checkpointHash)
                     .expect("restored artifact");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(saved).expect("original bytes"),
               eve::serializeSnapshotEnvelope(again).expect("restored bytes"));
    active->advance(f.mapModule, {eve::SimulationTick(2), eve::Duration(0)}, {}, {}).expect("restored empty tick");
    REQUIRE(!previous->advance(f.mapModule, {eve::SimulationTick(2), eve::Duration(0)}, {}, {}).ok());
}
}  // namespace test_map_spawn_space
