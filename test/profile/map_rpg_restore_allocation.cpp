// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"compile-contract","default":"module-composition","module":"map_rpg_spawn"}
// OBJECT-only ordinary new/new[] interception for real map/RPG admission and joint publication.
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <optional>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Capability.h"
#include "common/ScriptProvider.h"
#include "common/StateAccess.h"
#include "integration/map_spawn/MapSpawnSpaceFixture.h"
#include "map/rpg_spawn/RPGMapCheckpoint.h"
#include "map/rpg_spawn/RPGMapRuntime.h"
#include "map/rpg_spawn/RPGMapSession.h"
#include "rpg/SimulationEvents.h"
#include "rpg/SkillSystem.h"
namespace {
thread_local bool           measuring = false, injected = false;
thread_local std::size_t    allocations = 0, failureIndex = std::numeric_limits<std::size_t>::max();
eve::Result<eve::ContentId> hash(std::string_view text) {
    std::uint64_t state = 14695981039346656037ULL;
    for (unsigned char byte : text) state = (state ^ byte) * 1099511628211ULL;
    eve::ContentId::Bytes bytes{};
    for (auto& byte : bytes) {
        state = state * 6364136223846793005ULL + 1;
        byte  = static_cast<std::uint8_t>(state >> 32);
    }
    return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
}
std::size_t actorCount(ecs::Table& table) {
    std::size_t count = 0;
    auto        view  = ecs::View<eve::rpg::RPGActor>(table);
    for (auto it = view.begin(); it != view.end(); ++it) ++count;
    return count;
}
std::size_t layerCount(ecs::Table& table) {
    std::size_t count = 0;
    auto        view  = ecs::View<eve::map::TileLayer>(table);
    for (auto it = view.begin(); it != view.end(); ++it) ++count;
    return count;
}
}  // namespace
void* operator new(std::size_t size) {
    if (measuring && allocations++ == failureIndex) {
        injected = true;
        throw std::bad_alloc();
    }
    if (auto* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void  operator delete(void* value) noexcept { std::free(value); }
void  operator delete[](void* value) noexcept { std::free(value); }
void  operator delete(void* value, std::size_t) noexcept { std::free(value); }
void  operator delete[](void* value, std::size_t) noexcept { std::free(value); }
int   checkMapCleanup() {
    auto&   registry = eve::ScriptProviderRegistry::instance();
    ssq::VM vm(1024);
    struct ClearBeforeVm {
        ~ClearBeforeVm() { eve::ScriptProviderRegistry::instance().clearAll(); }
    } clearBeforeVm;
    auto code = vm.compileSource(R"SQ(
        class CleanupSink {
            calls = 0;
            function replaceTileCollision(payload) { ++calls; }
            function count(dt) { return calls; }
        }
        cleanupSink <- CleanupSink();
    )SQ");
    vm.run(code);
    auto           first       = std::make_unique<eve::map::Map>();
    auto           second      = std::make_unique<eve::map::Map>();
    auto           provider    = vm.find("cleanupSink");
    constexpr auto bindingName = "map.binding.rollback.with-a-long-native-name";
    auto           publish     = [&] {
        return registry.publishForCapability(eve::map::ITileCollisionSink::capabilityName, bindingName, provider);
    };
    auto warm = publish().expect("warm binding storage");
    registry.unload(warm).expect("warm binding retirement");
    allocations   = 0;
    measuring     = true;
    auto baseline = publish();
    measuring     = false;
    if (!baseline) return 65;
    const auto bindingAllocations = allocations;
    registry.unload(std::move(baseline).takeValue()).expect("baseline binding retirement");
    if (bindingAllocations == 0) return 66;
    for (std::size_t fail = 0; fail < bindingAllocations; ++fail) {
        failureIndex  = fail;
        allocations   = 0;
        injected      = false;
        measuring     = true;
        bool rejected = false;
        try {
            auto published = publish();
            measuring      = false;
            rejected       = !published.ok();
        } catch (const std::bad_alloc&) {
            measuring = false;
            rejected  = true;
        }
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (!injected || !rejected || registry.contains(bindingName) ||
            eve::cap::listenerCount<eve::map::ITileCollisionSink>() != 0) {
            std::fprintf(stderr, "Map binding failpoint %zu/%zu: injected=%d rejected=%d published=%d listeners=%zu\n",
                           fail, bindingAllocations, int(injected), int(rejected), int(registry.contains(bindingName)),
                           eve::cap::listenerCount<eve::map::ITileCollisionSink>());
            return 67;
        }
        auto retry = publish().expect("same binding retries after allocation failure");
        registry.unload(retry).expect("retry binding retirement");
    }
    auto handle =
        registry
            .publishForCapability(eve::map::ITileCollisionSink::capabilityName, "map.cleanup", vm.find("cleanupSink"))
            .expect("publish actual script sink");
    auto* borrowed = eve::cap::listenerAt<eve::map::ITileCollisionSink>(0);
    if (!borrowed || eve::cap::listenerCount<eve::map::ITileCollisionSink>() != 1) return 60;
    auto destroyWithoutAllocation = [&](auto& owner) {
        allocations  = 0;
        injected     = false;
        failureIndex = 0;
        measuring    = true;
        owner.reset();
        measuring    = false;
        failureIndex = std::numeric_limits<std::size_t>::max();
        return !injected && allocations == 0;
    };
    if (!destroyWithoutAllocation(first) || !registry.hasCapabilitySeam(eve::map::ITileCollisionSink::capabilityName) ||
        eve::cap::listenerCount<eve::map::ITileCollisionSink>() != 1)
        return 61;
    if (!destroyWithoutAllocation(second) || registry.hasCapabilitySeam(eve::map::ITileCollisionSink::capabilityName) ||
        eve::cap::listenerCount<eve::map::ITileCollisionSink>() != 0)
        return 62;
    // A listener already observed by synchronous dispatch remains owned after retirement.
    borrowed->replaceTileCollision({}, nullptr, 0);
    if (registry.callNumber(handle, "count", 0).expect("retired dispatch remains safe") != 1) return 63;
    registry.unload(handle).expect("unload a provider whose native adapter was already retired");
    auto missing = std::make_unique<eve::map::Map>();
    registry.removeCapability(eve::map::ITileCollisionSink::capabilityName).expect("explicit seam withdrawal");
    if (!destroyWithoutAllocation(missing)) return 64;
    std::puts(
        "Map cleanup: shared seam survives old-owner retirement; final retirement and absent-seam teardown allocate "
          "zero storage");
    return 0;
}
int checkRuntime(std::string_view mode) {
    using namespace eve;
    using namespace eve::map_rpg_spawn;
    test_map_spawn_space::Fixture fixture;
    test_map_spawn_space::Assets  files;
    Value::Array                  cells(100, Value(1));
    files.text["map.json"] =
        Value::object({{"width", 10}, {"height", 10}, {"tilewidth", 1}, {"tileheight", 1}, {"data", std::move(cells)}})
            .toJson()
            .expect("runtime map");
    auto mapSource = Value::fromJson(fixture.map->canonicalJson()).expect("map source");
    mapSource.find("instances")
        ->getIf<Value::Array>()
        ->front()
        .set("activation", Value::object({{"kind", "event"}, {"event", "wake"}}));
    files.text["spawns.json"] = mapSource.toJson().expect("event map");
    auto binding              = Value::fromJson(R"({"schema":"eve.map-spawn-binding","version":1,"map_id":"forest",
        "map_uri":"project://map.json","spawns_uri":"project://spawns.json",
        "pixel_to_world":{"origin":[0,0],"scale":1}})")
                       .expect("runtime binding");
    auto unit = Value::fromJson(R"({"schema":"eve.rpg.unit","version":1,"id":"wolf","display_name":"Wolf",
        "skill":"spatial.basic","attack":7,"defense":2,"max_hp":20,"speed":3})")
                    .expect("runtime unit");
    auto compiled = test_map_spawn_space::Templates(*fixture.units)
                        .resolve(DefinitionRef::parse("spawn.template:camp").expect("template ref"))
                        .expect("template");
    auto source  = Value::fromJson(compiled.canonicalJson()).expect("template source");
    auto runtime = RPGMapRuntime::prepare({unit}, {source}, binding, files, fixture.owner.child("runtime"), 42, .4)
                       .expect("runtime");
    auto firstInput = runtime->prepareActivation(std::vector<spawn::Point>{}, {"wake"}).expect("bind first event");
    runtime->advance({SimulationTick(1), Duration(1)}, firstInput).expect("spawn");
    const auto  state   = runtime->captureState().expect("runtime state");
    const auto& rows    = *state.find("sources")->getIf<Value::Array>();
    const auto& ids     = *rows[0].find("subjects")->getIf<Value::Array>();
    auto        subject = SubjectRef::fromPersistentId(*PersistentId::parse(ids[0].asString()));
    runtime->applyDamage(subject, 2).expect("retained damage");
    {
        auto delivery = runtime->readEvents().expect("source identity");
        auto actor    = delivery.events().vitals.front().actor.resolve();
        if (!actor) return 36;
        eve::rpg::SkillSystem::setCooldownRemaining(&actor->get(), "spatial.basic", 5.f);
    }
    const auto epoch      = fixture.owner.child("runtime-save");
    auto       checkpoint = runtime->captureCheckpoint(epoch, Revision(1)).expect("checkpoint");
    auto       artifact   = checkpoint.seal(hash).expect("runtime file");
    const auto bytes      = serializeSnapshotEnvelope(artifact).expect("original bytes");
    runtime.reset();
    files.text.clear();
    auto prepare     = [&]() { return PreparedRPGRuntimeRestore::prepare(artifact, epoch, hash); };
    auto makeRuntime = [&]() {
        auto                           candidate = prepare().expect("admit existing");
        std::unique_ptr<RPGMapRuntime> active;
        auto                           retired = candidate.commit(active).expect("existing runtime");
        if (retired) std::abort();
        return active;
    };
    // Warm simultaneous owners and listener capacity before enumerating the stable allocation path.
    {
        auto active    = makeRuntime();
        auto candidate = prepare().expect("warm admit");
        auto old       = candidate.commit(active).expect("warm commit");
    }
    auto warmRoster = [](RPGMapRuntime& active) {
        auto delivery = active.readEvents().expect("warm actor roster");
        auto actor    = delivery.events().vitals.front().actor.resolve();
        if (!actor) std::abort();
        ecs::ScopedTable scope(*actor->get().table);
        (void)eve::rpg::RPGActor::liveActors();
    };
    const auto  idleQueries     = cap::listenerCount<IStateQuery>();
    const auto  idleMutations   = cap::listenerCount<IStateMutation>();
    std::size_t count           = 0;
    std::size_t pendingFailures = 0, faultedFailures = 0, readyDamageFailures = 0, recoveredWorkspaces = 0;
    std::optional<RPGActivation> frameInput;
    auto run = [&](RPGMapRuntime& active, std::unique_ptr<RPGMapRuntime>& owner, PreparedRPGRuntimeRestore& candidate,
                   RPGRuntimeCheckpoint& frozen) {
        if (mode == "runtime_prepare") return prepare().ok();
        if (mode == "runtime_capture") return active.captureCheckpoint(epoch, Revision(1)).ok();
        if (mode == "runtime_seal") return frozen.seal(hash).ok();
        if (mode == "runtime_advance")
            return active.advance({SimulationTick(2), Duration(1000000000)}, *frameInput).ok();
        if (mode == "runtime_damage") return active.applyDamage(subject, 50).ok();
        return candidate.commit(owner).ok();
    };
    {
        auto active    = makeRuntime();
        auto candidate = prepare().expect("baseline candidate");
        auto frozen    = active->captureCheckpoint(epoch, Revision(1)).expect("baseline freeze");
        if (mode == "runtime_advance") {
            warmRoster(*active);
            frameInput.emplace(
                active->prepareActivation(std::vector<spawn::Point>{{1, 1}}, {"wake"}).expect("prepare actual input"));
        }
        allocations         = 0;
        measuring           = true;
        const bool accepted = run(*active, active, candidate, frozen);
        measuring           = false;
        count               = allocations;
        if (!accepted || !count) return 30;
    }
    for (std::size_t fail = 0; fail < count; ++fail) {
        auto       active    = makeRuntime();
        auto*      original  = active.get();
        auto       receipt   = active->readEvents().expect("old receipt");
        auto       candidate = prepare().expect("candidate");
        auto       frozen    = active->captureCheckpoint(epoch, Revision(1)).expect("freeze");
        const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
        if (mode == "runtime_advance") {
            warmRoster(*active);
            frameInput.emplace(
                active->prepareActivation(std::vector<spawn::Point>{{1, 1}}, {"wake"}).expect("prepare actual input"));
        }
        failureIndex  = fail;
        allocations   = 0;
        injected      = false;
        measuring     = true;
        bool rejected = false;
        try {
            rejected = !run(*active, active, candidate, frozen);
        } catch (const std::bad_alloc&) {
            rejected = true;
        }
        measuring    = false;
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (mode == "runtime_advance" || mode == "runtime_damage") {
            if (!injected || active.get() != original) {
                std::fprintf(stderr, "Unreached runtime failpoint %zu/%zu injected=%d allocations=%zu\n", fail, count,
                             int(injected), allocations);
                return 40;
            }
            if (!rejected) {
                if (mode != "runtime_advance") return 52;
                // std::stable_sort may recover from a failed optional temporary buffer allocation.
                const auto state    = active->captureState().expect("recovered workspace phase");
                auto       delivery = active->readEvents().expect("recovered workspace events");
                if (state.find("phase")->asString() != "ready" || state.find("completed")->asString() != "2" ||
                    delivery.events().vitals.size() != 1)
                    return 53;
                {
                    auto actor = delivery.events().vitals.front().actor.resolve();
                    if (!actor || eve::rpg::SkillSystem::getCooldownRemaining(&actor->get(), "spatial.basic") != 4.f)
                        return 54;
                }
                if (active->advance({SimulationTick(2), Duration(1000000000)}).ok()) return 55;
                ++recoveredWorkspaces;
                continue;
            }
            const auto state = active->captureState().expect("phase after injected failure");
            const auto phase = state.find("phase")->asString();
            if (phase == "faulted") {
                ++faultedFailures;
                if (active->captureCheckpoint(epoch, Revision(2)).ok() ||
                    active->advance({SimulationTick(2), Duration(1000000000)}).ok())
                    return 41;
                if (mode == "runtime_damage" &&
                    receipt.events().vitals.front().actor.presence() != eve::rpg::ActorEventPresence::Retired)
                    return 42;
                active->close().expect("explicit fault cleanup");
            } else if (mode == "runtime_advance" && phase == "spawn_pending") {
                ++pendingFailures;
                auto changed =
                    active->prepareActivation(std::vector<spawn::Point>{{2, 2}}, {"wake"}).expect("different input");
                if (active->advance({SimulationTick(2), Duration(1000000000)}, changed).ok()) return 56;
                auto file = active->captureCheckpoint(epoch, Revision(2))
                                .expect("freeze actual pending frame")
                                .seal(hash)
                                .expect("pending file");
                for (int corruption = 0; corruption < 2; ++corruption) {
                    auto  malformed = file;
                    auto* frame     = malformed.payload.find("frame");
                    if (corruption == 0)
                        frame->find("pending_activation")
                            ->find("events")
                            ->getIf<Value::Array>()
                            ->front()
                            .set("owner", fixture.owner.child("unknown-source").format());
                    else
                        frame->set("players", Value::Array{Value::Array{2.0, 2.0}});
                    auto invalid = makeSnapshotEnvelope(malformed.type, malformed.schema, malformed.schemaVersion,
                                                        malformed.instanceId, malformed.revision, malformed.tick,
                                                        malformed.payload, hash)
                                       .expect("sealed malformed activation");
                    if (PreparedRPGRuntimeRestore::prepare(invalid, epoch, hash).ok()) return 59;
                    if (active.get() != original) return 60;
                }
                auto pending = PreparedRPGRuntimeRestore::prepare(file, epoch, hash).expect("admit pending frame");
                auto retired = pending.commit(active).expect("restore actual pending frame");
                if (active->advance({SimulationTick(2), Duration(1000000000)}, *frameInput).ok()) return 57;
                const auto  restored = active->captureState().expect("restored input");
                const auto& points   = *restored.find("players")->getIf<Value::Array>();
                if (points.size() != 1 || points.front().getIf<Value::Array>()->front().asDouble() != 1.0) return 58;
                if (active->advance({SimulationTick(2), Duration(2000000000)}).ok()) return 43;
                active->advance({SimulationTick(2), Duration(1000000000)}).expect("retry exact domain-complete step");
                auto delivered = active->readEvents().expect("events after pending restore");
                if (delivered.events().vitals.size() != 1) return 44;
                auto cooldown = [&]() {
                    auto actor = delivered.events().vitals.front().actor.resolve();
                    return actor ? eve::rpg::SkillSystem::getCooldownRemaining(&actor->get(), "spatial.basic") : -1.f;
                };
                if (cooldown() != 4.f) return 45;
                active->advance({SimulationTick(3), Duration(1000000000)}).expect("first later domain step");
                if (cooldown() != 3.f) return 46;
            } else if (mode == "runtime_damage" && phase == "ready") {
                ++readyDamageFailures;
                auto       delivery = active->readEvents().expect("retained damage facts");
                const auto count    = delivery.events().vitals.size();
                if (count != 1 && count != 3) return 47;
                {
                    auto actor = receipt.events().vitals.front().actor.resolve();
                    if (!actor || actor->get().getCurrent("hp") != (count == 1 ? 18.0 : 0.0)) return 48;
                }
                active->acknowledge(receipt).expect("old accepted prefix remains confirmable");
                active->advance({SimulationTick(2), Duration(1000000000)}).expect("resume retained lifecycle");
                active->advance({SimulationTick(3), Duration(1000000000)}).expect("resume replacement");
                const auto resumed = active->captureState().expect("resumed state");
                if (resumed.find("sources")->getIf<Value::Array>()->front().find("alive")->asInt() != 1 ||
                    active->readEvents().expect("no duplicate death").events().vitals.size() != (count == 1 ? 0 : 2))
                    return 49;
            } else
                return 50;
            continue;
        }
        if (!injected || !rejected || active.get() != original || cap::listenerCount<IStateQuery>() != queries ||
            cap::listenerCount<IStateMutation>() != mutations ||
            active->readEvents().expect("events after fail").events().vitals.size() != 1) {
            std::fprintf(stderr, "Runtime %.*s failpoint %zu/%zu: injected=%d rejected=%d\n", int(mode.size()),
                         mode.data(), fail, count, int(injected), int(rejected));
            return 31;
        }
        auto unchanged = active->captureCheckpoint(epoch, Revision(1))
                             .expect("unchanged checkpoint")
                             .seal(hash)
                             .expect("unchanged file");
        if (serializeSnapshotEnvelope(unchanged).expect("unchanged bytes") != bytes) return 32;
        if (!run(*active, active, candidate, frozen)) return 33;
        if (mode == "runtime_commit") {
            if (active.get() == original || active->acknowledge(receipt).ok()) return 34;
            active->applyDamage(subject, 20).expect("death after retry");
            active->advance({SimulationTick(2), Duration(1000000000)}).expect("pending death after retry");
            active->advance({SimulationTick(3), Duration(1000000000)}).expect("future creation after retry");
        }
    }
    if (cap::listenerCount<IStateQuery>() != idleQueries || cap::listenerCount<IStateMutation>() != idleMutations)
        return 35;
    if (mode == "runtime_advance" || mode == "runtime_damage") {
        if ((mode == "runtime_advance" && (!pendingFailures || !faultedFailures)) ||
            (mode == "runtime_damage" && (!readyDamageFailures || faultedFailures))) {
            std::fprintf(
                stderr,
                "Missing required phase coverage: pending=%zu faulted=%zu ready_damage=%zu recovered_workspace=%zu\n",
                pendingFailures, faultedFailures, readyDamageFailures, recoveredWorkspaces);
            return 51;
        }
        std::printf(
            "Owned RPG runtime %.*s: %zu allocation failures, %zu pending restores without repeated cooldown, %zu "
            "faulted no-replay owners, %zu retryable damage handoffs, %zu recovered stable-sort workspaces\n",
            int(mode.size()), mode.data(), count, pendingFailures, faultedFailures, readyDamageFailures,
            recoveredWorkspaces);
        return 0;
    }
    std::printf(
        "Owned RPG runtime %.*s: %zu ordinary allocation failpoints preserve complete old owner/events/listeners; same "
        "candidate retries\n",
        int(mode.size()), mode.data(), count);
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view mode(argv[1]);
    if (mode == "map_cleanup") return checkMapCleanup();
    if (mode.starts_with("runtime_")) return checkRuntime(mode);
    const bool committing = mode == "commit" || mode == "pending_commit";
    const bool pending    = mode == "pending_prepare" || mode == "pending_commit";
    if (!committing && mode != "prepare" && mode != "pending_prepare") return 2;
    const std::size_t             expectedActors = pending ? 0 : 1;
    ecs::Table                    sourceMapTable;
    ecs::ScopedTable              sourceScope(sourceMapTable);
    test_map_spawn_space::Fixture fixture;
    test_map_spawn_space::Assets  files;
    eve::Value::Array             cells(100, eve::Value(1));
    files.text["map.json"] = eve::Value::object({{"width", 10},
                                                 {"height", 10},
                                                 {"tilewidth", 1},
                                                 {"tileheight", 1},
                                                 {"data", eve::Value(std::move(cells))}})
                                 .toJson()
                                 .expect("source map");
    files.text["spawns.json"] = fixture.map->canonicalJson();
    auto binding =
        eve::Value::fromJson(
            R"({"schema":"eve.map-spawn-binding","version":1,"map_id":"forest","map_uri":"project://map.json","spawns_uri":"project://spawns.json","pixel_to_world":{"origin":[0,0],"scale":1}})")
            .expect("binding");
    auto sourceCandidate = eve::map_rpg_spawn::PreparedRPGMapSession::prepare(
                               fixture.mapModule, binding, files, test_map_spawn_space::Templates(*fixture.units),
                               fixture.table, fixture.units, fixture.owner.child("source"), 42,
                               R"({"schema":"eve.map.object-contract","version":1,"types":[]})", 0.4, {})
                               .expect("source candidate");
    auto source = sourceCandidate.commit(fixture.mapModule).expect("source publication");
    source->advance(fixture.mapModule, {eve::SimulationTick(1), eve::Duration(0)}, {}, {})
        .expect("actual source actor");
    if (pending) {
        eve::rpg::RPGActor* actor = nullptr;
        {
            auto view = ecs::View<eve::rpg::RPGActor>(*source->world());
            actor     = view.begin().entity();
        }
        {
            ecs::ScopedTable scope(*source->world());
            eve::rpg::VitalsSystem::takeDamage(actor, "hp", 100);
        }
        std::vector<eve::rpg::VitalsEvent> events;
        eve::rpg::VitalsSystem::pollEvents(events);
        {
            ecs::ScopedTable scope(*source->world());
            ecs::DestroyEntity(actor);
        }
        if (source->advance(fixture.mapModule, {eve::SimulationTick(0), eve::Duration(0)}, events, {}).ok()) return 3;
    }
    const auto epoch    = fixture.owner.child("map-allocation-save");
    auto       artifact = source->capture(fixture.mapModule, epoch, eve::Revision(1))
                        .expect("freeze")
                        .sealMap(hash)
                        .expect("map artifact");
    const auto bytes = eve::serializeSnapshotEnvelope(artifact).expect("source bytes");
    files.text.clear();
    auto prepare = [&](eve::map::Map& target) {
        return eve::map_rpg_spawn::PreparedRPGMapRestore::prepare(target, artifact, epoch, hash, 0.4,
                                                                  std::vector<eve::spawn::Point>{});
    };
    auto makeSession = [&](eve::map::Map& target) {
        auto candidate = prepare(target).expect("prepare initial session");
        std::unique_ptr<eve::map_rpg_spawn::RPGMapSession> active;
        auto previous = candidate.commit(target, active).expect("initial restored world");
        if (previous) std::abort();
        return active;
    };
    {
        ecs::Table       world;
        ecs::ScopedTable scope(world);
        eve::map::Map    target;
        auto             active    = makeSession(target);
        auto             candidate = prepare(target).expect("warm simultaneous old/new worlds");
        auto             retired   = candidate.commit(target, active).expect("warm full publication capacity");
    }
    const auto  idleQueries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto  idleMutations = eve::cap::listenerCount<eve::IStateMutation>();
    std::size_t count         = 0;
    {
        ecs::Table                                               world;
        ecs::ScopedTable                                         scope(world);
        eve::map::Map                                            target;
        auto                                                     active = makeSession(target);
        std::optional<eve::map_rpg_spawn::PreparedRPGMapRestore> candidate;
        if (committing) candidate.emplace(prepare(target).expect("admit baseline"));
        allocations = 0;
        measuring   = true;
        if (committing) {
            auto result = candidate->commit(target, active);
            measuring   = false;
            if (!result) return 10;
        } else {
            auto result = prepare(target);
            measuring   = false;
            if (!result) return 10;
            candidate.emplace(std::move(result).takeValue());
        }
        count = allocations;
    }
    if (count == 0) return 11;
    for (std::size_t fail = 0; fail < count; ++fail) {
        ecs::Table       world;
        ecs::ScopedTable scope(world);
        eve::map::Map    target;
        auto             active       = makeSession(target);
        auto*            original     = active.get();
        auto             table        = active->world();
        const auto       sourceHandle = active->spawning().sourceHandles()[0];
        auto             lease        = active->spawning().world().sourceLease(sourceHandle).expect("old authority");
        const auto       layer        = ecs::handle_of(target.getLayer(0));
        const auto       queries      = eve::cap::listenerCount<eve::IStateQuery>();
        const auto       mutations    = eve::cap::listenerCount<eve::IStateMutation>();
        std::optional<eve::map_rpg_spawn::PreparedRPGMapRestore> candidate;
        if (committing) candidate.emplace(prepare(target).expect("admit attempt"));
        allocations   = 0;
        failureIndex  = fail;
        injected      = false;
        measuring     = true;
        bool rejected = false;
        try {
            if (committing) {
                auto result = candidate->commit(target, active);
                measuring   = false;
                rejected    = !result.ok();
            } else {
                auto result = prepare(target);
                measuring   = false;
                rejected    = !result.ok();
                if (result) candidate.emplace(std::move(result).takeValue());
            }
        } catch (const std::bad_alloc&) {
            measuring = false;
            rejected  = true;
        }
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (!injected || !rejected || active.get() != original || !lease.isCurrent() ||
            ecs::try_get(layer) != target.getLayer(0) || actorCount(*table) != expectedActors ||
            layerCount(world) != 1 || fixture.count() != expectedActors ||
            eve::cap::listenerCount<eve::IStateQuery>() != queries ||
            eve::cap::listenerCount<eve::IStateMutation>() != mutations ||
            active->spawning().world().inspect(sourceHandle).expect("old ledger").alive != 1 ||
            eve::serializeSnapshotEnvelope(artifact).expect("input intact") != bytes) {
            std::fprintf(
                stderr,
                "Map session %s failpoint %zu/%zu violated isolation: injected=%d rejected=%d allocations=%zu\n",
                argv[1], fail, count, int(injected), int(rejected), allocations);
            return 12;
        }
        auto previousSave = active->capture(target, epoch, eve::Revision(1))
                                .expect("old capture after failure")
                                .sealMap(hash)
                                .expect("old artifact");
        if (eve::serializeSnapshotEnvelope(previousSave).expect("old bytes") != bytes) return 13;
        if (!committing) candidate.emplace(prepare(target).expect("same artifact retries"));
        auto previous = candidate->commit(target, active).expect("same candidate retries publication");
        if (previous.get() != original || active.get() == original || lease.isCurrent()) return 14;
        auto restored =
            active->capture(target, epoch, eve::Revision(1)).expect("new capture").sealMap(hash).expect("new artifact");
        if (eve::serializeSnapshotEnvelope(restored).expect("new bytes") != bytes) return 15;
        if (pending) {
            if (active->advance(target, {eve::SimulationTick(2), eve::Duration(0)}, {}, {})
                    .expect("restored pending handoff")
                    .units != 0)
                return 17;
            if (active->advance(target, {eve::SimulationTick(3), eve::Duration(1000000000)}, {}, {})
                        .expect("saved replacement delay")
                        .units != 1 ||
                actorCount(*active->world()) != 1)
                return 18;
        }
    }
    if (eve::cap::listenerCount<eve::IStateQuery>() != idleQueries ||
        eve::cap::listenerCount<eve::IStateMutation>() != idleMutations)
        return 16;
    std::printf(
        "RPG map session %s: %zu ordinary new/new[] failpoints; actual Map/actors/owner/leases/capabilities unchanged; "
        "exact retry artifact\n",
        argv[1], count);
    return 0;
}
