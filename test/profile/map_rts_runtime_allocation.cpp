// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"compile-contract","default":"module-composition","module":"map_rts_spawn"}
// Enumerate ordinary new/new[] failures in the actual domain -> spawn frame, in one OBJECT consumer.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <new>
#include "attributes/AttributeProjection.h"
#include "attributes/AttributeProjectionCapture.h"
#include "attributes/AttributeSet.h"
#include "combat/DamageCapture.h"
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/ListenerStaging.h"
#include "common/ServiceInterfaces.h"
#include "common/StateAccess.h"
#include "crowd/Crowd.h"
#include "definitions/DefinitionRegistryCapture.h"
#include "definitions/Definitions.h"
#include "map/Map.h"
#include "map/MapGridState.h"
#include "map/Pathfinder.h"
#include "map/PathfinderState.h"
#include "map/rts_spawn/RTSMapCheckpoint.h"
#include "map/rts_spawn/RTSMapRestoreState.h"
#include "map/rts_spawn/RTSMapRuntime.h"
#include "map/rts_spawn/RTSMapSession.h"
#include "map/rts_spawn/RTSRuntimeCheckpoint.h"
#include "map/rts_spawn/RTSRuntimeHandoff.h"
#include "map/rts_spawn/RTSRuntimeRestore.h"
#include "map/spawn/MapSpawnLoad.h"
#include "rts/RTS.h"
#include "rts/RTSAttributeCapture.h"
#include "rts/RTSCombatArchive.h"
#include "rts/RTSCrowdCapture.h"
#include "rts/RTSFrameArchive.h"
#include "rts/RTSNavigationCapture.h"
#include "rts/RTSTypes.h"
#include "rts/RTSWeaponCapture.h"
#include "rts/spawn/RTSSpawnCapture.h"
#include "rts/spawn/RTSSpawnFactory.h"
#include "sensing/Sensing.h"
#include "sensing/SensingCapture.h"
#include "spawn/SpawnCheckpoint.h"
#include "spawn/definitions/SpawnTemplateSource.h"
namespace {
thread_local bool                                    measuring = false, injected = false;
thread_local std::size_t                             allocations = 0, failure = std::numeric_limits<std::size_t>::max();
std::size_t                                          pendingCount = 0, faultedCount = 0, completedCount = 0;
std::vector<double>                                  expectedPositions;
std::optional<eve::map_rts_spawn::RTSRuntimeHandoff> retainedHandoff;
std::shared_ptr<const eve::map_rts_spawn::RTSRuntimeCheckpoint> retainedCheckpoint;
bool                                                            retainHandoff = false;
void require(bool condition, const char* message, std::size_t index) {
    if (!condition) {
        std::fprintf(stderr, "RTS runtime allocation %zu: %s\n", index, message);
        std::exit(2);
    }
}
struct Files final : eve::service::IFileSystem {
    std::function<void()> onRead;
    bool                  readFile(const std::string& path, std::vector<std::uint8_t>& out) override {
        if (onRead) onRead();
        std::ifstream stream(std::string(EVE_RTSSPAWN_EXAMPLE_DIR) + "/" + path, std::ios::binary);
        if (!stream) return false;
        out.assign(std::istreambuf_iterator<char>(stream), {});
        return !stream.bad();
    }
    bool writeFile(const std::string&, const void*, std::size_t) override { return false; }
    bool fileExists(const std::string& path) override {
        std::ifstream stream(std::string(EVE_RTSSPAWN_EXAMPLE_DIR) + "/" + path, std::ios::binary);
        return static_cast<bool>(stream);
    }
    std::string text(const std::string& path) {
        std::vector<std::uint8_t> bytes;
        require(readFile(path, bytes), "actual example asset missing", 0);
        return std::string(bytes.begin(), bytes.end());
    }
};
// Exercise the real owner and facade protocols, including publication allocation failure.
std::size_t publicationAttempt(std::size_t index, bool inject, const char* stage) {
    using namespace eve;
    Files                    files;
    const auto               content = files.text("data/content.json");
    const std::vector<Value> templates{Value::fromJson(files.text("data/template.json")).takeValue()};
    const auto               binding = Value::fromJson(files.text("data/binding.json")).takeValue();
    const auto faction = SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000889"));
    const auto owner   = *PersistentId::parse("01900000-0000-7000-8000-000000000779");
    auto       old = map_rts_spawn::RTSMapRuntime::prepare(content, templates, binding, files, faction, owner, 42, {})
                   .expect("existing actual owner");
    old->advance({SimulationTick(1), Duration(100000000)}).expect("old owner live unit");
    const auto                                    before    = old->captureState().takeValue().toJson().takeValue();
    const auto                                    controls  = cap::listenerCount<IGameplayControlProvider>();
    const auto                                    catalogs  = cap::listenerCount<IGameplayInstanceCatalog>();
    const auto                                    queries   = cap::listenerCount<IStateQuery>();
    const auto                                    mutations = cap::listenerCount<IStateMutation>();
    auto                                          table     = std::make_shared<ecs::Table>();
    std::unique_ptr<rts::RTS>                     candidate;
    std::unique_ptr<map_rts_spawn::RTSMapRuntime> runtime;
    std::unique_ptr<cap::ListenerStaging>         staging;
    const bool                                    publish   = std::strcmp(stage, "listener-publish") == 0;
    const bool                                    construct = std::strcmp(stage, "rts-construct") == 0;
    if (publish) {
        constexpr std::array<std::string_view, 2> families{IGameplayControlProvider::capabilityName,
                                                           IGameplayInstanceCatalog::capabilityName};
        staging   = cap::ListenerStaging::begin(families).takeValue();
        candidate = std::make_unique<rts::RTS>(table);
    }
    files.onRead = [&] {
        require(cap::listenerCount<IGameplayControlProvider>() == controls &&
                    cap::listenerCount<IGameplayInstanceCatalog>() == catalogs &&
                    cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
                "external file callback observed an unpublished world", index);
    };
    allocations  = 0;
    injected     = false;
    failure      = index;
    measuring    = true;
    bool success = false;
    try {
        if (publish)
            success = staging->publish().ok();
        else if (construct) {
            candidate = std::make_unique<rts::RTS>(table);
            success   = true;
        } else {
            auto result = map_rts_spawn::RTSMapRuntime::prepare(content, templates, binding, files, faction,
                                                                owner.child("candidate"), 42, {});
            success     = result.ok();
            if (success) runtime = std::move(result).takeValue();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const auto hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "publication must abort every injected allocation", index);
    require(cap::listenerCount<IGameplayControlProvider>() == controls + (success ? 1u : 0u) &&
                cap::listenerCount<IGameplayInstanceCatalog>() == catalogs + (success ? 1u : 0u) &&
                cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "failed publication exposed or leaked a world listener", index);
    require(old->captureState().takeValue().toJson().takeValue() == before, "candidate changed existing owner", index);
    if (!success) {
        if (publish)
            staging->publish().expect("same listener batch retries");
        else if (construct)
            candidate = std::make_unique<rts::RTS>(table);
        else
            runtime = map_rts_spawn::RTSMapRuntime::prepare(content, templates, binding, files, faction,
                                                            owner.child("candidate"), 42, {})
                          .takeValue();
    }
    if (runtime) {
        runtime->advance({SimulationTick(1), Duration(100000000)}).expect("new owner usable after retry");
        runtime->close().expect("close candidate");
        runtime.reset();
    }
    candidate.reset();
    staging.reset();
    require(cap::listenerCount<IGameplayControlProvider>() == controls &&
                cap::listenerCount<IGameplayInstanceCatalog>() == catalogs &&
                cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "candidate cleanup changed old registrations", index);
    old->advance({SimulationTick(2), Duration(100000000)}).expect("old owner remains usable");
    old->close().expect("close old owner");
    return count;
}
eve::Result<eve::ContentId> hash(std::string_view text);
std::size_t                 trackerCodecAttempt(std::size_t index, bool inject, bool seal) {
    using namespace eve;
    auto             table = std::make_shared<ecs::Table>();
    ecs::ScopedTable scope(*table);
    auto*            layer = map::TileLayer::createLayer(5, 4, 1, 1);
    map::Pathfinder  provider(layer);
    provider.setBlockEmpty(false);
    crowd::Crowd backend;
    rts::RTS     source(table);
    source.setCrowdProvider(&backend);
    source.setNavigationProvider(&provider);
    source
        .configureCrowdSurfaceTracking({crowd::NavigationSurfaceId(1), crowd::MovementClassId(1), 1, 1, .35f, 1000000})
        .expect("tracker");
    action::ActionRuntime actions;
    rts::ActionAdapter    executor(actions);
    source.step({SimulationTick(1), Duration(100000000)}, executor).expect("published source");
    provider.blockGid(3);
    layer->setTile(0, 0, 3);
    source.setNavigationProvider(&provider, {1.25f, 2, -3});
    const auto   epoch    = *PersistentId::parse("01900000-0000-7000-8000-000000000c88");
    const auto   saved    = source.captureNavigation(epoch, provider).expect("pending tracker facts");
    const auto   envelope = saved.seal(Revision(9), SimulationTick(1), hash).expect("baseline seal");
    const auto   wire     = serializeSnapshotEnvelope(envelope).expect("baseline wire");
    auto         targetTable = std::make_shared<ecs::Table>();
    crowd::Crowd targetBackend;
    targetBackend
        .commitRestore(targetBackend.prepareRestore(backend.captureSnapshot().expect("matching backend"))
                                           .expect("backend candidate"))
        .expect("backend publish");
    std::unique_ptr<map::Pathfinder> targetProvider;
    rts::RTS                         target(targetTable);
    target.setCrowdProvider(&targetBackend);
    const auto roots = source.canonicalStateJson().expect("original roots");
    const auto hint  = provider.getNavigationChangeStamp();
    const auto field = targetBackend.getFieldStatus();
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    allocations  = 0;
    injected     = false;
    failure      = index;
    measuring    = true;
    bool success = false;
    try {
        if (seal) {
            auto result = saved.seal(Revision(9), SimulationTick(1), hash);
            success     = result.ok();
        } else {
            auto result = rts::RTSNavigationCapture::parse(envelope, epoch, hash);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "tracker codec must abort each injected allocation", index);
    require(source.canonicalStateJson().expect("source roots unchanged") == roots &&
                                provider.getNavigationChangeStamp() == hint &&
                                targetBackend.getFieldStatus().surface == field.surface &&
                                targetBackend.getFieldStatus().inputRevision == field.inputRevision &&
                                cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
                            "tracker codec mutated world/publication", index);
    require(serializeSnapshotEnvelope(saved.seal(Revision(9), SimulationTick(1), hash).expect("source retry"))
                                    .expect("retry text") == wire,
                            "tracker codec changed source facts", index);
    auto parsed = rts::RTSNavigationCapture::parse(parseSnapshotEnvelope(wire, hash).expect("read bytes"), epoch, hash)
                      .expect("same input retry");
    targetProvider =
        map::Pathfinder::prepareRestored(parsed.provider()->get(), std::ref(*layer)).expect("actual layer");
    require(!target.restoreNavigation(saved, *targetProvider).ok(), "parsed provider adopted original live provenance",
                            index);
    target.restoreNavigation(parsed, *targetProvider).expect("same parsed tracker publication");
    require(targetBackend.getFieldStatus().surface == field.surface &&
                                targetBackend.getFieldStatus().inputRevision == field.inputRevision,
                            "native restore republished surface", index);
    source.step({SimulationTick(2), Duration(100000000)}, executor).expect("source pending rebind");
    action::ActionRuntime targetActions;
    rts::ActionAdapter    targetExecutor(targetActions);
    {
        ecs::ScopedTable targetScope(*targetTable);
        target.step({SimulationTick(2), Duration(100000000)}, targetExecutor).expect("target pending rebind");
    }
    require(targetBackend.getFieldStatus().surface == backend.getFieldStatus().surface &&
                                targetBackend.getFieldStatus().inputRevision == backend.getFieldStatus().inputRevision &&
                                targetBackend.getFieldStatus().surface.surfaceRevision == field.surface.surfaceRevision + 1,
                            "tracker retry reset or duplicated pending publication", index);
    return count;
}
std::size_t trackerAttempt(std::size_t index, bool inject, bool capture) {
    using namespace eve;
    auto             table = std::make_shared<ecs::Table>();
    ecs::ScopedTable scope(*table);
    auto*            layer = map::TileLayer::createLayer(5, 4, 1.f, 1.f);
    map::Pathfinder  provider(layer);
    provider.setBlockEmpty(false);
    crowd::Crowd backend;
    rts::RTS     source(table);
    source.setCrowdProvider(&backend);
    source.setNavigationProvider(&provider);
    source
        .configureCrowdSurfaceTracking({crowd::NavigationSurfaceId(1), crowd::MovementClassId(1), 1, 1, .35f, 1000000})
        .expect("source tracker");
    action::ActionRuntime actions;
    rts::ActionAdapter    executor(actions);
    source.step({SimulationTick(1), Duration(100000000)}, executor).expect("actual surface publication");
    const auto epoch = *PersistentId::parse("01900000-0000-7000-8000-000000000c87");
    auto       saved = source.captureNavigation(epoch, provider).expect("actual navigation facts");
    auto       targetProvider =
        map::Pathfinder::prepareRestored(saved.provider()->get(), std::ref(*layer)).expect("native provider");
    auto         targetTable = std::make_shared<ecs::Table>();
    crowd::Crowd targetBackend;
    targetBackend.commitRestore(targetBackend.prepareRestore(backend.captureSnapshot().takeValue()).takeValue())
        .expect("matching native backend");
    rts::RTS target(targetTable);
    target.setCrowdProvider(&targetBackend);
    const auto before = source.canonicalStateJson().expect("source roots");
    const auto hint   = provider.getNavigationChangeStamp();
    const auto field  = targetBackend.getFieldStatus();
    allocations       = 0;
    injected          = false;
    failure           = index;
    measuring         = true;
    bool success      = false;
    try {
        if (capture) {
            auto result = source.captureNavigation(epoch, provider);
            success     = result.ok();
        } else {
            auto result = target.restoreNavigation(saved, *targetProvider);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const auto hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? (hit && !success) : success, "tracker operation must abort injected allocations", index);
    require(source.canonicalStateJson().expect("source remains intact") == before &&
                provider.getNavigationChangeStamp() == hint &&
                targetBackend.getFieldStatus().surface == field.surface &&
                targetBackend.getFieldStatus().inputRevision == field.inputRevision,
            "tracker operation changed roots/provider/surface", index);
    if (capture || !success) target.restoreNavigation(saved, *targetProvider).expect("same tracker candidate retry");
    auto observed = target.captureNavigation(epoch, *targetProvider).expect("target tracker observation");
    require(observed.saveEpoch() == epoch, "tracker retry lost epoch", index);
    require(!target.restoreNavigation(saved, *targetProvider).ok(), "claimed tracker accepted a second publication",
            index);
    return count;
}
std::size_t navigationCodecAttempt(std::size_t index, bool inject, bool encode) {
    using namespace eve;
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            layer = map::TileLayer::createLayer(4, 3, 1, 1);
    map::Pathfinder  source(layer);
    source.setBlockEmpty(false);
    source.blockGid(3);
    auto                            initial = source.captureNavigationGrid().expect("source published grid");
    std::unique_ptr<map::FlowField> cached(source.buildFlowField(3, 2));
    layer->setTile(1, 1, 3);
    const auto hint    = source.getNavigationChangeStamp();
    const auto saved   = source.captureState().expect("pending layer with actual old cache");
    const auto value   = saved.encodeState().expect("baseline state");
    const auto before  = value.toJson().expect("wire");
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    allocations  = 0;
    injected     = false;
    failure      = index;
    measuring    = true;
    bool success = false;
    try {
        if (encode) {
            auto result = saved.encodeState();
            success     = result.ok();
        } else {
            auto result = map::PathfinderState::prepareState(value);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "navigation codec must abort each injected allocation", index);
    require(source.getNavigationChangeStamp() == hint && source.getCellCost(1, 1) == 1.f &&
                cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "codec mutated provider or published bindings", index);
    require(saved.encodeState().expect("source retry").toJson().expect("repeat wire") == before,
            "navigation codec changed detached cache/history", index);
    auto parsed = map::PathfinderState::prepareState(value).expect("same input retry");
    require(parsed.encodeState().expect("parsed state").toJson().expect("parsed wire") == before,
            "navigation codec lost native facts", index);
    auto target = map::Pathfinder::prepareRestored(parsed, std::ref(*layer)).expect("actual restored layer");
    require(target->isRestoredFrom(parsed) && target->getNavigationChangeStamp() == hint &&
                target->getCellCost(1, 1) == 1.f,
            "native restore reset old grid cache", index);
    const auto actual   = target->captureNavigationGrid().expect("resume pending sync");
    const auto expected = source.captureNavigationGrid().expect("source same sync");
    require(actual.revision() == initial.revision() + 1 && actual.revision() == expected.revision() &&
                actual.costs()[5] == 0.f,
            "navigation history or pending layer lost", index);
    return count;
}
std::size_t navigationAttempt(std::size_t index, bool inject, bool restore) {
    using namespace eve;
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            layer = map::TileLayer::createLayer(5, 4, 1.f, 1.f);
    map::Pathfinder  provider(layer);
    provider.setBlockEmpty(false);
    provider.blockGid(3);
    std::unique_ptr<map::FlowField> flow(provider.buildFlowField(4, 3));
    const auto revision = provider.captureNavigationGrid().expect("initial provider capture").revision();
    layer->setTile(1, 1, 3);
    const auto before = provider.getNavigationChangeStamp();
    auto       saved  = provider.captureState().expect("detached provider");
    allocations       = 0;
    injected          = false;
    failure           = index;
    measuring         = true;
    bool success      = false;
    try {
        if (restore) {
            auto candidate = map::Pathfinder::prepareRestored(saved, std::ref(*layer));
            success        = candidate.ok();
        } else {
            auto capture = provider.captureState();
            success      = capture.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const auto hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? (hit && !success) : success, "navigation failure must not publish", index);
    require(
        provider.getNavigationChangeStamp() == before && provider.getCellCost(1, 1) == 1.f && layer->getTile(1, 1) == 3,
        "navigation failure changed cached or pending layer facts", index);
    auto retry = map::Pathfinder::prepareRestored(saved, std::ref(*layer)).expect("same provider checkpoint retry");
    require(retry->isRestoredFrom(saved) && retry->getCellCost(1, 1) == 1.f,
            "retry lost provider provenance or pending sync", index);
    const auto resumed = retry->captureNavigationGrid().expect("resumed pending sync");
    require(resumed.revision() == revision + 1 && resumed.costs()[6] == 0.f, "retry lost exact capture history", index);
    return count;
}
std::size_t attempt(std::size_t index, bool inject) {
    using namespace eve;
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    Files      files;
    auto       command = std::make_shared<rts::CommandSpec>();
    command->kind      = rts::OrderKind::AttackMove;
    command->target    = {16, 8};
    auto runtime       = map_rts_spawn::RTSMapRuntime::prepare(
                       files.text("data/content.json"),
                       {Value::fromJson(files.text("data/template.json")).expect("actual reinforcement template")},
                       Value::fromJson(files.text("data/binding.json")).expect("actual binding"), files,
                       SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000889")),
                       *PersistentId::parse("01900000-0000-7000-8000-000000000779"), 42, command)
                       .expect("actual runtime");
    runtime->advance({SimulationTick(1), Duration(100000000)}).expect("first actual unit");
    const SimulationStep frame{SimulationTick(2), Duration(400000000)};
    allocations             = 0;
    injected                = false;
    failure                 = index;
    measuring               = true;
    bool        success     = false;
    std::size_t diagnostics = 0;
    try {
        auto result = runtime->advance(frame);
        success     = result.ok();
        if (success) diagnostics = result.value().diagnostics.size();
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const auto hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit : success, "baseline/injected allocation not observed", index);
    auto       state = runtime->captureState().expect("actual post-failure state");
    const auto phase = state.find("phase")->asString();
    if (phase == "spawn_pending") {
        ++pendingCount;
        require(!success && state.find("tick")->asInt() == 1, "pending frame already acknowledged", index);
        require(!runtime->advance({SimulationTick(3), frame.delta}).ok(), "different tick accepted during retry",
                index);
        require(!runtime->advance({frame.tick, Duration(100000000)}).ok(), "different delta accepted during retry",
                index);
        auto frozen =
            runtime->captureCheckpoint(*PersistentId::parse("01900000-0000-7000-8000-000000000a34"), Revision(1))
                .expect("pending owner checkpoint");
        require(frozen.phase() == map_rts_spawn::RTSCheckpointPhase::SpawnPending &&
                    frozen.completedTick() == SimulationTick(1) && frozen.pendingStep()->tick == frame.tick &&
                    frozen.pendingStep()->delta == frame.delta && frozen.crowdFrame()->get().tick == frame.tick &&
                    frozen.map()->get().coreTick() == SimulationTick(1),
                "freeze lost pending frame responsibility", index);
        auto handoff = map_rts_spawn::RTSRuntimeHandoff::capture(frozen).expect("freeze actual pending responsibility");
        auto wire    = serializeSnapshotEnvelope(handoff.seal(hash).expect("seal pending handoff"))
                        .expect("write pending handoff");
        auto read = map_rts_spawn::RTSRuntimeHandoff::parse(
                        parseSnapshotEnvelope(wire, hash).expect("read pending handoff"), frozen.saveEpoch(), hash)
                        .expect("admit pending handoff");
        require(read.phase() == frozen.phase() && read.completedTick() == frozen.completedTick() &&
                    read.pendingStep()->tick == frame.tick && read.pendingStep()->delta == frame.delta,
                "file handoff lost actual frame responsibility", index);
        auto saved = std::make_shared<const map_rts_spawn::RTSRuntimeCheckpoint>(std::move(frozen));
        if (retainHandoff && !retainedHandoff) {
            retainedHandoff.emplace(std::move(read));
            retainedCheckpoint = saved;
        }
        auto admitted = map_rts_spawn::PreparedRTSRuntimeRestore::prepare(saved, hash).takeValue();
        std::unique_ptr<map_rts_spawn::RTSMapRuntime> restored;
        admitted.commit(restored).expect("restore exact pending frame responsibility");
        require(!restored->advance({SimulationTick(3), frame.delta}).ok(),
                "restored pending owner accepted a different frame", index);
        restored->advance(frame).expect("restored handoff without duplicate domain movement");
        runtime->advance(frame).expect("same frame retry without another domain step");
        state = runtime->captureState().expect("retried actual state");
        require(restored->captureState().takeValue().toJson().takeValue() == state.toJson().takeValue(),
                "restored pending frame repeated or lost simulation work", index);
        restored->close().expect("pending restored cleanup");
    } else if (phase == "faulted") {
        ++faultedCount;
        require(!success && !runtime->advance(frame).ok(), "partial domain step replayed", index);
        require(
            !runtime->captureCheckpoint(*PersistentId::parse("01900000-0000-7000-8000-000000000a34"), Revision(1)).ok(),
            "faulted runtime appeared saveable", index);
    } else {
        ++completedCount;
        require(phase == "ready" && success, "frame phase disagrees with result", index);
        require(!runtime->advance(frame).ok(), "completed domain frame replayed", index);
    }
    const auto& units = *state.find("units");
    require(state.find("sources")->at(0).find("alive")->asInt() == std::int64_t(units.arraySize()),
            "actual roots and source membership diverged", index);
    require(state.find("crowd_agents")->asInt() == std::int64_t(units.arraySize()), "Crowd projection diverged", index);
    if (!inject) {
        require(units.arraySize() == 2 && diagnostics == 0, "baseline reinforcement failed", index);
        expectedPositions.clear();
        for (std::size_t i = 0; i < units.arraySize(); ++i)
            expectedPositions.push_back(units.at(i).find("x")->asDouble());
    } else if (phase == "spawn_pending") {
        require(units.arraySize() == expectedPositions.size(), "retry lost or duplicated a unit", index);
        for (std::size_t i = 0; i < units.arraySize(); ++i)
            require(std::abs(units.at(i).find("x")->asDouble() - expectedPositions[i]) < 1e-5,
                    "retry advanced actual movement twice", index);
    }
    runtime->close().expect("explicit cleanup after failure");
    auto closed = runtime->captureState().expect("actual closed state");
    require(closed.find("units")->arraySize() == 0 && closed.find("crowd_agents")->asInt() == 0,
            "cleanup left roots or agents", index);
    runtime.reset();
    require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "runtime destruction retained attribute registrations", index);
    return count;
}
// Test-only deterministic digest; no cryptographic/integrity-strength claim.
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
std::size_t unitAttributeArchiveAttempt(std::size_t index, bool inject, bool seal) {
    using namespace eve;
    Files files;
    auto  command      = std::make_shared<rts::CommandSpec>();
    command->kind      = rts::OrderKind::AttackMove;
    command->target    = {16, 8};
    const auto owner   = *PersistentId::parse("01900000-0000-7000-8000-000000000779");
    auto       runtime = map_rts_spawn::RTSMapRuntime::prepare(
                       files.text("data/content.json"), {Value::fromJson(files.text("data/template.json")).takeValue()},
                       Value::fromJson(files.text("data/binding.json")).takeValue(), files,
                       SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000889")),
                       owner, 42, command)
                       .takeValue();
    runtime->advance({SimulationTick(1), Duration(100000000)}).expect("first actual unit");
    runtime->advance({SimulationTick(2), Duration(400000000)}).expect("second actual unit");
    auto        saved      = runtime->captureCheckpoint(owner.child("unit-attributes"), Revision(1)).takeValue();
    const auto& attributes = saved.unitAttributes()->get();
    require(attributes.members().size() == 2, "fixture must create two actual units before measuring", index);
    const auto envelope = attributes.seal(saved.revision(), saved.completedTick(), hash).takeValue();
    const auto before   = serializeSnapshotEnvelope(envelope).takeValue();
    const auto world    = runtime->captureState().takeValue().toJson().takeValue();
    const auto controls = cap::listenerCount<IGameplayControlProvider>(), queries = cap::listenerCount<IStateQuery>(),
               mutations = cap::listenerCount<IStateMutation>();
    allocations          = 0;
    injected             = false;
    failure              = index;
    measuring            = true;
    bool success         = false;
    try {
        if (seal) {
            auto result = attributes.seal(saved.revision(), saved.completedTick(), hash);
            success     = result.ok();
        } else {
            auto result = rts::RTSUnitAttributeCapture::parse(envelope, saved.saveEpoch(), hash);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "Unit attribute archive must abort injected allocation", index);
    require(cap::listenerCount<IGameplayControlProvider>() == controls &&
                cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "Unit archive registered providers", index);
    require(runtime->captureState().takeValue().toJson().takeValue() == world, "Unit archive changed actual game",
            index);
    auto retried = attributes.seal(saved.revision(), saved.completedTick(), hash).takeValue();
    require(serializeSnapshotEnvelope(retried).takeValue() == before, "failed Unit sealing changed frozen facts",
            index);
    auto parsed = rts::RTSUnitAttributeCapture::parse(std::move(retried), saved.saveEpoch(), hash).takeValue();
    require(parsed.members().size() == 2 &&
                serializeSnapshotEnvelope(parsed.seal(saved.revision(), saved.completedTick(), hash).takeValue())
                        .takeValue() == before,
            "Unit archive retry lost complete projection scope", index);
    runtime->close().expect("actual game retirement");
    runtime.reset();
    require(parsed.members().size() == 2, "Unit archive retained actual game instead of facts", index);
    return count;
}
std::size_t projectionStateAttempt(std::size_t index, bool inject, bool encode) {
    using namespace eve;
    using namespace eve::attributes;
    AttributeProjection source("projection-codec");
    source.setBase("mana", 3).expect("actual projection");
    source.setBase("mana", 4).expect("independent projection revision");
    source.addModifier(AttributeModifier{{}, "mana", "origin", AttributeOperation::Add, 2}).expect("actual modifier");
    auto       captured = source.capture().takeValue();
    const auto state    = captured.encodeState().takeValue();
    const auto before   = state.toJson().takeValue();
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    allocations  = 0;
    injected     = false;
    failure      = index;
    measuring    = true;
    bool success = false;
    try {
        if (encode) {
            auto result = captured.encodeState();
            success     = result.ok();
        } else {
            auto result = AttributeProjectionCapture::prepareState(state);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "projection codec must abort every injected allocation", index);
    require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "projection codec registered adapters", index);
    require(source.capture().takeValue().encodeState().takeValue().toJson().takeValue() == before,
            "projection codec changed source or revision", index);
    auto retry = AttributeProjectionCapture::prepareState(state).takeValue();
    require(retry.revision() == source.revision() && retry.initialized() == true &&
                retry.encodeState().takeValue().toJson().takeValue() == before,
            "projection retry lost metadata or modifier facts", index);
    auto native = PreparedAttributeProjection::prepare(retry).takeValue();
    return count;
}
std::size_t attributeStateAttempt(std::size_t index, bool inject, bool encode) {
    using namespace eve;
    using namespace eve::attributes;
    AttributeSet source("native-codec");
    source.setBase("power", 10);
    source.addModifier(AttributeModifier{{}, "power", "origin", AttributeOperation::Add, 2}).expect("actual modifier");
    const auto removed =
        source.addModifier(AttributeModifier{{}, "power", "origin", AttributeOperation::Add, 4}).takeValue();
    source.removeModifier(removed).expect("retain allocator gaps");
    source.addModifier(AttributeModifier{"policy", "power", "origin", AttributeOperation::Custom, 3, 1, 0, "saved.op"})
        .expect("declarative policy");
    const auto state   = source.encodeState().takeValue();
    const auto before  = state.toJson().takeValue();
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    allocations  = 0;
    injected     = false;
    failure      = index;
    measuring    = true;
    bool success = false;
    try {
        if (encode) {
            auto result = source.encodeState();
            success     = result.ok();
        } else {
            auto result = AttributeSet::prepareState(state);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "attribute codec must abort every injected allocation", index);
    require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "attribute candidate registered state adapters", index);
    require(source.encodeState().takeValue().toJson().takeValue() == before, "attribute codec mutated source facts",
            index);
    auto         prepared = AttributeSet::prepareState(state).takeValue();
    AttributeSet target("native-codec");
    target.commitPrepared(std::move(prepared));
    require(target.encodeState().takeValue().toJson().takeValue() == before,
            "attribute retry lost counters or modifier facts", index);
    const auto sourceId =
        source.addModifier(AttributeModifier{{}, "power", "future", AttributeOperation::Add, 1}).takeValue();
    const auto targetId =
        target.addModifier(AttributeModifier{{}, "power", "future", AttributeOperation::Add, 1}).takeValue();
    require(sourceId == targetId && source.encodeState().takeValue() == target.encodeState().takeValue(),
            "attribute continuation renumbered modifiers", index);
    return count;
}
std::size_t combatArchiveAttempt(std::size_t index, bool inject, const char* stage) {
    using namespace eve;
    const auto             epoch = *PersistentId::parse("01900000-0000-7000-8000-000000000d51");
    const auto             id    = epoch.child("retired-body").format();
    rts::RTSCombatSnapshot state;
    state.mirroredSubjects.insert(id);
    state.blockedSubjects.insert(id);
    auto       saved    = rts::RTSCombatArchive::capture(state, epoch, Revision(1), SimulationTick(3)).takeValue();
    const auto envelope = saved.seal(hash).takeValue();
    const auto before   = serializeSnapshotEnvelope(envelope).takeValue();
    allocations         = 0;
    injected            = false;
    failure             = index;
    measuring           = true;
    bool success        = false;
    try {
        if (std::strcmp(stage, "combat-capture") == 0) {
            auto result = rts::RTSCombatArchive::capture(state, epoch, Revision(1), SimulationTick(3));
            success     = result.ok();
        } else if (std::strcmp(stage, "combat-seal") == 0) {
            auto result = saved.seal(hash);
            success     = result.ok();
        } else {
            auto result = rts::RTSCombatArchive::parse(envelope, epoch, hash);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "combat archive must abort injected allocation", index);
    require(state.mirroredSubjects.size() == 1 && state.blockedSubjects.contains(id), "capture mutated combat source",
            index);
    auto retry = rts::RTSCombatArchive::capture(state, epoch, Revision(1), SimulationTick(3)).takeValue();
    require(serializeSnapshotEnvelope(retry.seal(hash).takeValue()).takeValue() == before,
            "combat retry changed source facts", index);
    auto parsed = rts::RTSCombatArchive::parse(saved.seal(hash).takeValue(), epoch, hash).takeValue();
    require(serializeSnapshotEnvelope(parsed.seal(hash).takeValue()).takeValue() == before,
            "combat parse retry changed facts", index);
    return count;
}
std::size_t frameArchiveAttempt(std::size_t index, bool inject, const char* stage) {
    using namespace eve;
    const auto            epoch = *PersistentId::parse("01900000-0000-7000-8000-000000000d41");
    combat::DamageRuntime damage;
    combat::CombatState   body{SubjectRef::fromPersistentId(epoch.child("retired")), 100, 100, 50, 50};
    combat::DamageRequest request;
    request.target             = body.subject;
    request.healthDamage       = 10;
    request.damageType         = "Damage.Physical.Slash";
    request.tick               = SimulationTick(4);
    auto               outcome = damage.apply(body, request).takeValue();
    rts::RTSFrameState frame;
    frame.damage.push_back({0, request.tick, rts::DamageChannel::Weapon, request, outcome});
    frame.combat.push_back({1, request.tick, {rts::CombatFireEventKind::WeaponFired, {}, body.subject, {1, 2}}});
    frame.lifecycle.push_back(
        {2, request.tick, {rts::LifecycleEventKind::StatusApplied, {}, body.subject, "status\nhistory", 1}});
    frame.sequence           = 3;
    frame.preserveOnNextStep = true;
    frame.gameplay.push_back(
        {1, request.tick, "history", body.subject, "command", "correlation", Value::object({{"saved", true}})});
    frame.nextGameplaySequence = 2;
    auto       saved           = rts::RTSFrameArchive::capture(frame, epoch, Revision(1), request.tick).takeValue();
    const auto envelope        = saved.seal(hash).takeValue();
    const auto before          = serializeSnapshotEnvelope(envelope).takeValue();
    allocations                = 0;
    injected                   = false;
    failure                    = index;
    measuring                  = true;
    bool success               = false;
    try {
        if (std::strcmp(stage, "frame-capture") == 0) {
            auto result = rts::RTSFrameArchive::capture(frame, epoch, Revision(1), request.tick);
            success     = result.ok();
        } else if (std::strcmp(stage, "frame-seal") == 0) {
            auto result = saved.seal(hash);
            success     = result.ok();
        } else {
            auto result = rts::RTSFrameArchive::parse(envelope, epoch, hash);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const auto hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "frame archive must abort every injected allocation", index);
    auto retry = rts::RTSFrameArchive::capture(frame, epoch, Revision(1), request.tick).takeValue();
    require(serializeSnapshotEnvelope(retry.seal(hash).takeValue()).takeValue() == before,
            "frame capture failure mutated source facts", index);
    auto parsed = rts::RTSFrameArchive::parse(saved.seal(hash).takeValue(), epoch, hash).takeValue();
    require(serializeSnapshotEnvelope(parsed.seal(hash).takeValue()).takeValue() == before && body.health == 90,
            "frame archive failed retry or replayed damage", index);
    return count;
}
std::size_t replayAttempt(std::size_t index, bool inject, bool verify, bool resultOnly = false,
                          const char* requestStage = nullptr) {
    using namespace eve;
    combat::DamageRuntime source;
    combat::CombatState   target{
        SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000d30")), 100, 100, 50, 50};
    combat::DamageRequest request;
    request.target       = target.subject;
    request.damageType   = "Damage.Physical.Slash";
    request.healthDamage = 10;
    const auto outcome   = source.previewSettlement(target, request).takeValue();
    auto       input     = outcome.settlementRequest;
    input.decisions.push_back({*LogicalId::parse("test:frame"), 4, .1, .5, true});
    const auto digest = hash("actual frame rules").takeValue();
    const auto record =
        settlement::createSettlementReplayRecord(input, digest, outcome.settlementResult, hash).takeValue();
    const auto semantic    = settlement::settlementResultCanonicalJson(outcome.settlementResult).takeValue();
    const auto requestText = settlement::settlementRequestCanonicalJson(input).takeValue();
    allocations            = 0;
    injected               = false;
    failure                = index;
    measuring              = true;
    bool success           = false;
    try {
        if (requestStage && std::strcmp(requestStage, "request-seal") == 0) {
            auto result = settlement::settlementRequestCanonicalJson(input);
            success     = result.ok();
        } else if (requestStage) {
            auto result = settlement::settlementRequestFromJson(requestText);
            success     = result.ok();
        } else if (resultOnly) {
            auto result = settlement::settlementResultFromJson(semantic);
            success     = result.ok();
        } else if (verify) {
            auto result = settlement::verifySettlementReplayRecord(record, digest, outcome.settlementResult, hash);
            success     = result.ok();
            if (success) success = result.value().empty();
        } else {
            auto result = settlement::settlementReplayRequest(record);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "replay must abort every injected allocation", index);
    auto requestRetry = settlement::settlementRequestFromJson(requestText).takeValue();
    require(settlement::settlementRequestCanonicalJson(requestRetry).takeValue() == requestText,
            "standalone request retry changed input facts", index);
    auto decoded = settlement::settlementResultFromJson(semantic).takeValue();
    require(settlement::settlementResultCanonicalJson(decoded).takeValue() == semantic,
            "semantic result retry changed actual audit", index);
    auto retried = settlement::settlementReplayRequest(record).takeValue();
    require(retried.target == input.target && retried.decisions.size() == 1 && retried.decisions[0].sequence == 4,
            "replay retry lost exact input or recorded decisions", index);
    auto checked = settlement::verifySettlementReplayRecord(record, digest, outcome.settlementResult, hash).takeValue();
    require(checked.empty() && target.health == 100, "replay validation mutated or diverged from actual audit", index);
    return count;
}
std::size_t sensingAttempt(std::size_t index, bool inject, bool parse) {
    using namespace eve;
    sensing::SensingWorld source;
    source.upsertPosition("saved\nsubject", .1f, 2.5f, 3.75f, "blue", "unit,elite", "player").expect("actual facts");
    source.setZones("saved\nsubject", "zone:one").expect("actual zones");
    source.rebuildIndex(sensing::SpatialIndexKind::XYZ, 2.5f).expect("actual index");
    auto       saved    = source.capture().takeValue();
    const auto epoch    = *PersistentId::parse("01900000-0000-7000-8000-000000000d29");
    const auto envelope = saved.seal(epoch, Revision(1), SimulationTick(3), hash).takeValue();
    const auto text     = serializeSnapshotEnvelope(envelope).takeValue();
    const auto before   = source.snapshotJson();
    allocations         = 0;
    injected            = false;
    failure             = index;
    measuring           = true;
    bool success        = false;
    try {
        if (parse) {
            auto result = sensing::SensingCapture::parse(envelope, epoch, hash);
            success     = result.ok();
        } else {
            auto result = saved.seal(epoch, Revision(1), SimulationTick(3), hash);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "sensing operation must abort injected allocation", index);
    require(source.snapshotJson() == before && source.indexKind() == sensing::SpatialIndexKind::XYZ,
            "sensing persistence mutated source facts or index", index);
    auto retried = saved.seal(epoch, Revision(1), SimulationTick(3), hash).takeValue();
    require(serializeSnapshotEnvelope(retried).takeValue() == text, "failed sealing changed capture", index);
    auto                  parsed = sensing::SensingCapture::parse(std::move(retried), epoch, hash).takeValue();
    sensing::SensingWorld restored;
    restored.restore(sensing::PreparedSensingRestore::prepare(parsed).takeValue()).expect("same facts retry");
    require(
        restored.snapshotJson() == before && restored.indexKind() == source.indexKind() && parsed.cellSize() == 2.5f,
        "parsed sensing facts or index policy differ", index);
    return count;
}
std::size_t damageAttempt(std::size_t index, bool inject, const char* stage) {
    using namespace eve;
    combat::DamageRuntime      source(nullptr, {20, .1});
    settlement::SettlementRule oldRule;
    oldRule.id           = "saved.rule";
    oldRule.value        = 1.5;
    oldRule.operation    = settlement::RuleOperation::Multiply;
    oldRule.filter.kinds = {"damage"};
    oldRule.when         = "magnitude > 0";
    settlement::SettlementRuleSet oldRules;
    oldRules.configure({oldRule}).expect("original conditional rules");
    source.configureSettlementRules(oldRules).expect("actual original pipeline");
    auto saved    = source.capture().takeValue();
    auto newRule  = oldRule;
    newRule.value = 2;
    settlement::SettlementRuleSet newRules;
    newRules.configure({newRule}).expect("replacement rules");
    combat::CombatState target{
        SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000d22")), 100, 100, 50, 50};
    combat::DamageRequest request;
    request.target       = target.subject;
    request.healthDamage = 10;
    request.damageType   = "Damage.Physical.Slash";
    auto observe         = [&](const combat::DamageRuntime& value) {
        return settlement::settlementResultCanonicalJson(
                   value.previewSettlement(target, request).takeValue().settlementResult)
            .takeValue();
    };
    const auto before  = observe(source);
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    const bool configure = std::strcmp(stage, "damage-configure") == 0;
    const auto epoch     = target.subject.persistentId();
    const auto envelope  = saved.seal(epoch, Revision(1), SimulationTick(3), hash).takeValue();
    allocations          = 0;
    injected             = false;
    failure              = index;
    measuring            = true;
    bool success         = false;
    try {
        if (configure) {
            auto result = source.configureSettlementRules(newRules);
            success     = result.ok();
        } else if (std::strcmp(stage, "damage-capture") == 0) {
            auto result = source.capture();
            success     = result.ok();
        } else if (std::strcmp(stage, "damage-seal") == 0) {
            auto result = saved.seal(epoch, Revision(1), SimulationTick(3), hash);
            success     = result.ok();
        } else if (std::strcmp(stage, "damage-parse") == 0) {
            auto result = combat::DamageCapture::parse(envelope, epoch, hash);
            success     = result.ok();
        } else {
            auto result = combat::DamageRuntime::prepareRestored(saved);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "damage operation must abort injected allocation", index);
    if (!configure || !success) require(observe(source) == before, "damage failure split rules and pipeline", index);
    require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "damage operation registered external state providers", index);
    auto parsed =
        combat::DamageCapture::parse(saved.seal(epoch, Revision(1), SimulationTick(3), hash).takeValue(), epoch, hash)
            .takeValue();
    auto retry = combat::DamageRuntime::prepareRestored(parsed).takeValue();
    require(observe(*retry) == before, "damage retry lost exact rule trace", index);
    if (configure) {
        source.configureSettlementRules(newRules).expect("same rule configuration retry");
        require(source.previewSettlement(target, request).takeValue().appliedHealthDamage == 20,
                "replacement rules did not publish after retry", index);
        auto changed = combat::DamageRuntime::prepareRestored(source.capture().takeValue()).takeValue();
        require(observe(*changed) == observe(source), "frozen rules differ from actual pipeline", index);
    }
    return count;
}
std::size_t definitionAttempt(std::size_t index, bool inject, const char* stage) {
    using namespace eve;
    auto                            table = std::make_shared<ecs::Table>();
    definitions::DefinitionRegistry registry(*PersistentId::parse("01900000-0000-7000-8000-000000000d12"));
    rts::RTS                        domain(table);
    domain
        .loadContent(registry, R"({"weapons":[{"id":"club","damage":10}],
        "units":[{"id":"fighter","weaponType":"club","health":77}]})")
        .expect("actual native content");
    registry.insert("unit", "retired", 1, "{}").expect("retained content history");
    registry.remove("unit", "retired").expect("actual tombstone");
    const auto before   = registry.snapshotJson();
    const auto envelope = serializeSnapshotEnvelope(registry.snapshot(hash).takeValue()).takeValue();
    auto       captured = domain.captureDefinitions(registry).takeValue();
    const auto input    = captured.seal(hash).takeValue();
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    int        callbacks    = 0;
    auto       subscription = registry.subscribe([&](const auto&) { ++callbacks; });
    allocations             = 0;
    injected                = false;
    failure                 = index;
    measuring               = true;
    bool success            = false;
    try {
        if (std::strcmp(stage, "definition-capture") == 0) {
            auto result = domain.captureDefinitions(registry);
            success     = result.ok();
        } else if (std::strcmp(stage, "definition-prepare") == 0) {
            auto result = definitions::DefinitionRegistry::prepareRestored(captured);
            success     = result.ok();
        } else if (std::strcmp(stage, "definition-parse") == 0) {
            auto result = definitions::DefinitionRegistryCapture::parse(input, hash);
            success     = result.ok();
        } else {
            auto result = captured.seal(hash);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "definition operation must abort injected allocation", index);
    require(registry.snapshotJson() == before && callbacks == 0 && cap::listenerCount<IStateQuery>() == queries &&
                cap::listenerCount<IStateMutation>() == mutations,
            "definition failure changed source, callbacks or capability registry", index);
    auto parsed = definitions::DefinitionRegistryCapture::parse(input, hash).expect("same registry file retry");
    require(serializeSnapshotEnvelope(parsed.seal(hash).takeValue()).takeValue() == envelope,
            "registry file lost exact content/history/metadata", index);
    auto retry = definitions::DefinitionRegistry::prepareRestored(parsed).takeValue();
    require(retry->snapshotJson() == before &&
                serializeSnapshotEnvelope(retry->snapshot(hash).takeValue()).takeValue() == envelope &&
                serializeSnapshotEnvelope(captured.seal(hash).takeValue()).takeValue() == envelope,
            "definition retry changed content or envelope metadata", index);
    require(domain.captureDefinitions(registry).ok(), "source cannot capture after failure", index);
    return count;
}
std::size_t restoredSessionAttempt(std::size_t index, bool inject, bool commit, bool admit = false) {
    using namespace eve;
    const auto initialQueries   = cap::listenerCount<IStateQuery>(),
               initialMutations = cap::listenerCount<IStateMutation>();
    std::size_t count           = 0;
    {
        Files      files;
        const auto owner = *PersistentId::parse("01900000-0000-7000-8000-000000000779");
        auto       runtime =
            map_rts_spawn::RTSMapRuntime::prepare(
                files.text("data/content.json"), {Value::fromJson(files.text("data/template.json")).takeValue()},
                Value::fromJson(files.text("data/binding.json")).takeValue(), files,
                SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000889")), owner, 42,
                {})
                .expect("actual source owner");
        runtime->advance({SimulationTick(1), Duration(100000000)}).expect("actual first reinforcement");
        auto joint = std::make_shared<const map_rts_spawn::RTSRuntimeCheckpoint>(
            runtime->captureCheckpoint(owner.child("session-restore"), Revision(1)).takeValue());
        auto             saved    = std::shared_ptr<const map_rts_spawn::RTSMapCheckpoint>(joint, &joint->map()->get());
        const auto       oldState = runtime->captureState().takeValue().toJson().takeValue();
        auto             table    = std::make_shared<ecs::Table>();
        ecs::ScopedTable scope(*table);
        map::Map         target;
        target
            .loadFromTextWithObjectContract(R"({"width":2,"height":2,"tilewidth":1,"tileheight":1,"data":[9,0,0,0]})",
                                            R"({"schema":"eve.map.object-contract","version":1,"types":[]})")
            .expect("actual previously loaded target map");
        const auto   oldLayer = ecs::handle_of(target.getLayer(0));
        crowd::Crowd crowd;
        rts::RTS     domain(table);
        domain.setCrowdProvider(&crowd);
        domain.rebuildState(saved->roots()->get()).expect("isolated saved roots");
        const auto                                          roots = domain.canonicalStateJson().takeValue();
        std::optional<map_rts_spawn::PreparedRTSMapSession> candidate;
        std::unique_ptr<map_rts_spawn::RTSMapSession>       session;
        std::optional<map_rts_spawn::RTSMapRestoreState>    admitted;
        if (!admit) admitted.emplace(map_rts_spawn::RTSMapRestoreState::prepare(saved, hash).takeValue());
        if (commit)
            candidate.emplace(map_rts_spawn::PreparedRTSMapSession::prepareRestored(target, *admitted, domain,
                                                                                    std::vector<spawn::Point>{})
                                  .takeValue());
        const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
        allocations  = 0;
        injected     = false;
        failure      = index;
        measuring    = true;
        bool success = false;
        try {
            if (admit) {
                auto result = map_rts_spawn::RTSMapRestoreState::prepare(saved, hash);
                success     = result.ok();
                if (success) admitted.emplace(std::move(result).takeValue());
            } else if (commit) {
                auto result = candidate->commit(target);
                success     = result.ok();
                if (success) session = std::move(result).takeValue();
            } else {
                auto result = map_rts_spawn::PreparedRTSMapSession::prepareRestored(target, *admitted, domain,
                                                                                    std::vector<spawn::Point>{});
                success     = result.ok();
                if (success) candidate.emplace(std::move(result).takeValue());
            }
        } catch (const std::bad_alloc&) {
        }
        measuring      = false;
        count          = allocations;
        const auto hit = injected;
        failure        = std::numeric_limits<std::size_t>::max();
        require(inject ? hit && !success : success, "restored session allocation outcome", index);
        require(runtime->captureState().takeValue().toJson().takeValue() == oldState &&
                    domain.canonicalStateJson().takeValue() == roots && cap::listenerCount<IStateQuery>() == queries &&
                    cap::listenerCount<IStateMutation>() == mutations,
                "restored session altered old game/roots or retained capabilities", index);
        if (!commit || !success)
            require(target.getLayerCount() == 1 && ecs::try_get(oldLayer) && target.getLayer(0) &&
                        ecs::try_get(oldLayer) == target.getLayer(0) && target.getLayer(0)->getTile(0, 0) == 9,
                    "failed/prepared session partially published map", index);
        if (!admitted) admitted.emplace(map_rts_spawn::RTSMapRestoreState::prepare(saved, hash).takeValue());
        if (!candidate)
            candidate.emplace(map_rts_spawn::PreparedRTSMapSession::prepareRestored(target, *admitted, domain,
                                                                                    std::vector<spawn::Point>{})
                                  .expect("same checkpoint preparation retry"));
        if (!session) session = candidate->commit(target).expect("same prepared session publication retry");
        auto captured = session->capture(target, saved->saveEpoch(), saved->revision()).expect("restored boundary");
        require(captured.coreTick() == saved->coreTick() && captured.core()->get().membership().size() == 1 &&
                    captured.provenance()->get().sourceIncarnation() != saved->provenance()->get().sourceIncarnation(),
                "restored session lost source/core boundary or reused authority", index);
        session->close(spawn::DetachPolicy::RemoveUnits).expect("target cleanup");
        runtime->close().expect("old game still usable");
    }
    require(
        cap::listenerCount<IStateQuery>() == initialQueries && cap::listenerCount<IStateMutation>() == initialMutations,
        "restored session scope leaked capabilities", index);
    return count;
}
std::size_t ownedRestoreAttempt(std::size_t index, bool inject, bool commit) {
    using namespace eve;
    using namespace eve::map_rts_spawn;
    Files      files;
    const auto owner   = *PersistentId::parse("01900000-0000-7000-8000-000000000779");
    auto       command = std::make_shared<rts::CommandSpec>();
    command->kind      = rts::OrderKind::AttackMove;
    command->target    = {16, 8};
    auto runtime       = RTSMapRuntime::prepare(
                       files.text("data/content.json"), {Value::fromJson(files.text("data/template.json")).takeValue()},
                       Value::fromJson(files.text("data/binding.json")).takeValue(), files,
                       SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000889")),
                       owner, 42, command)
                       .takeValue();
    runtime->advance({SimulationTick(1), Duration(100000000)}).expect("source first unit");
    runtime->advance({SimulationTick(2), Duration(400000000)}).expect("source second unit and movement");
    const auto expected = runtime->captureState().takeValue().toJson().takeValue();
    auto       saved    = std::make_shared<const RTSRuntimeCheckpoint>(
        runtime->captureCheckpoint(owner.child("restore-allocation"), Revision(1)).takeValue());
    runtime->advance({SimulationTick(3), Duration(100000000)}).expect("old receiver later than checkpoint");
    const auto                before = runtime->captureState().takeValue().toJson().takeValue();
    auto* const               old    = runtime.get();
    std::weak_ptr<ecs::Table> oldTable;
    cap::forEach<IGameplayControlProvider>([&](auto* provider) {
        if (auto* domain = dynamic_cast<rts::RTS*>(provider); domain && domain->ownedWorld())
            oldTable = domain->ownedWorld();
    });
    const auto                               controls  = cap::listenerCount<IGameplayControlProvider>();
    const auto                               catalogs  = cap::listenerCount<IGameplayInstanceCatalog>();
    const auto                               queries   = cap::listenerCount<IStateQuery>();
    const auto                               mutations = cap::listenerCount<IStateMutation>();
    std::optional<PreparedRTSRuntimeRestore> token;
    if (commit) token.emplace(PreparedRTSRuntimeRestore::prepare(saved, hash).takeValue());
    allocations  = 0;
    injected     = false;
    failure      = index;
    measuring    = true;
    bool success = false;
    try {
        if (commit)
            success = token->commit(runtime).ok();
        else {
            auto prepared = PreparedRTSRuntimeRestore::prepare(saved, hash);
            success       = prepared.ok();
            if (success) token.emplace(std::move(prepared).takeValue());
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "restore must abort every injected allocation", index);
    require(cap::listenerCount<IGameplayControlProvider>() == controls &&
                cap::listenerCount<IGameplayInstanceCatalog>() == catalogs &&
                cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "restore exposed or leaked a partial world", index);
    if (!success || !commit) {
        require(runtime.get() == old && !oldTable.expired(), "failed restore retired old world", index);
        require(runtime->captureState().takeValue().toJson().takeValue() == before, "failed restore mutated old game",
                index);
    }
    if (!token) token.emplace(PreparedRTSRuntimeRestore::prepare(saved, hash).takeValue());
    if (!success || !commit) token->commit(runtime).expect("same owner restore token retries");
    require(oldTable.expired(), "successful replacement retained old world", index);
    require(runtime->captureState().takeValue().toJson().takeValue() == expected,
            "restored native facts differ from saved boundary", index);
    runtime->advance({SimulationTick(3), Duration(100000000)}).expect("restored next domain frame");
    require(runtime->captureState().takeValue().toJson().takeValue() == before,
            "restored two-unit continuation differs from uninterrupted game", index);
    runtime->close().expect("restored cleanup");
    return count;
}
std::size_t ownedCaptureAttempt(std::size_t index, bool inject) {
    using namespace eve;
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    Files      files;
    auto       command = std::make_shared<rts::CommandSpec>();
    command->kind      = rts::OrderKind::AttackMove;
    command->target    = {16, 8};
    const auto owner   = *PersistentId::parse("01900000-0000-7000-8000-000000000779");
    auto       runtime = map_rts_spawn::RTSMapRuntime::prepare(
                       files.text("data/content.json"),
                       {Value::fromJson(files.text("data/template.json")).expect("actual template")},
                       Value::fromJson(files.text("data/binding.json")).expect("actual binding"), files,
                       SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000889")),
                       owner, 42, command)
                       .expect("actual owner");
    runtime->advance({SimulationTick(1), Duration(100000000)}).expect("first frame");
    runtime->advance({SimulationTick(2), Duration(400000000)}).expect("actual movement and second unit");
    const auto before = runtime->captureState().expect("owner before freeze").toJson().expect("owner baseline JSON");
    allocations       = 0;
    injected          = false;
    failure           = index;
    measuring         = true;
    bool success      = false;
    try {
        auto result = runtime->captureCheckpoint(owner.child("owned-save"), Revision(1));
        success     = result.ok();
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "owned capture must abort injected allocation", index);
    require(runtime->captureState().expect("owner after attempt").toJson().expect("owner after JSON") == before,
            "capture changed owner facts or phase", index);
    auto retry = runtime->captureCheckpoint(owner.child("owned-save"), Revision(1)).expect("same-boundary owner retry");
    require(retry.completedTick() == SimulationTick(2) && retry.map()->get().coreTick() == SimulationTick(2) &&
                retry.crowdFrame()->get().tick == SimulationTick(2) && retry.crowdMembers().size() == 2 &&
                retry.crowd()->get().members().size() == 2 &&
                retry.crowd()->get().saveEpoch() == owner.child("owned-save") && !retry.pendingStep() &&
                retry.damage() && retry.definitions() && retry.weapons() &&
                retry.weapons()->get().members().size() == 2 &&
                retry.weapons()->get().saveEpoch() == owner.child("owned-save") && retry.unitAttributes() &&
                retry.unitAttributes()->get().saveEpoch() == owner.child("owned-save") &&
                retry.unitAttributes()->get().members().size() == 2 && retry.navigationProvider() &&
                retry.navigationProvider()->get().bound() && retry.navigation() &&
                retry.navigation()->get().saveEpoch() == owner.child("owned-save"),
            "retry lost joined owner facts", index);
    runtime->advance({SimulationTick(3), Duration(100000000)}).expect("capture did not consume next frame");
    require(retry.completedTick() == SimulationTick(2), "frozen clock followed source", index);
    runtime->close().expect("explicit close");
    runtime.reset();
    require(retry.map()->get().roots()->get().units.size() == 2 && retry.crowdFrame()->get().agents.size() == 2,
            "source destruction invalidated owner freeze", index);
    require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "owner checkpoint retained source services", index);
    return count;
}
std::size_t captureAttempt(std::size_t index, bool inject, const char* stage) {
    using namespace eve;
    const bool raw              = std::strcmp(stage, "capture-raw") == 0;
    const bool pending          = std::strcmp(stage, "capture-pending") == 0;
    const auto initialQueries   = cap::listenerCount<IStateQuery>(),
               initialMutations = cap::listenerCount<IStateMutation>();
    std::size_t count           = 0;
    {
        Files            files;
        auto             registry = std::make_shared<definitions::DefinitionRegistry>();
        auto             table    = std::make_shared<ecs::Table>();
        ecs::ScopedTable scope(*table);
        crowd::Crowd     crowd;
        crowd.setClampToField(false);
        rts::RTS domain(table);
        domain.setCrowdProvider(&crowd);
        domain.loadContent(*registry, files.text("data/content.json")).expect("actual native content");
        const auto owner   = *PersistentId::parse("01900000-0000-7000-8000-000000000779");
        const auto faction = SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000889"));
        domain.newFaction(faction).expect("actual faction");
        rts_spawn::RTSSpawnFactory validator(domain, faction, registry);
        spawn::SpawnTemplateSource templates(*registry, validator);
        templates.publish(Value::fromJson(files.text("data/template.json")).expect("actual template"))
            .expect("template publication");
        map::Map map;
        auto     prepared = map_rts_spawn::PreparedRTSMapSession::prepare(
                            map, Value::fromJson(files.text("data/binding.json")).expect("actual binding"), files,
                            templates, domain, faction, registry, owner, 42,
                            R"({"schema":"eve.map.object-contract","version":1,"types":[]})", {}, {})
                            .expect("actual map preparation");
        auto session = prepared.commit(map).expect("actual map publication");
        session->advance(map, {SimulationTick(1), Duration(100000000)}, {}, {}).expect("actual initial reinforcement");
        const auto source  = session->spawning().sourceHandles()[0];
        const auto initial = session->spawning().world().inspect(source).expect("actual source state");
        require(initial.subjects.size() == 1, "baseline must own one real member", index);
        const auto subject = initial.subjects[0];
        if (pending || raw) {
            auto*                 unit = domain.findUnit(subject);
            combat::DamageRequest request;
            request.target       = subject;
            request.healthDamage = 10000;
            request.damageType   = "damage.physical";
            request.tick         = SimulationTick(2);
            combat::DamageRuntime damage;
            auto outcome = damage.apply(unit->durability()->state, request).expect("real death before freeze");
            std::vector<rts::RTSFrameDamageEvent> events{
                {1, request.tick, rts::DamageChannel::Weapon, request, outcome}};
            domain.cleanupDestroyed().expect("actual casualty retirement");
            if (raw) {
                auto view = ecs::View<rts::Faction>(*table);
                require(!session->advance(map, {SimulationTick(2), Duration(100000000)}, events, {}).ok(),
                        "View must retain raw input before translation", index);
            } else {
                map.getLayer(0)->setTile(0, 0, 2);
                require(!session->advance(map, {SimulationTick(2), Duration(100000000)}, events, {}).ok(),
                        "stale navigation must retain translated removal", index);
                auto refresh = session->prepareNavigationRefresh(map).expect("actual navigation capture");
                refresh->commit(map).expect("actual navigation refresh");
            }
        }
        const auto epoch       = owner.child("joint-save");
        const auto corePayload = [&]() {
            auto core = session->spawning().world().capture(epoch, Revision(4)).expect("core observation");
            return core.seal(hash).expect("test-only core digest").payload.toJson().expect("core JSON");
        };
        const auto gridPayload = [&]() {
            auto grid = map::MapGridState::capture(map).expect("grid observation");
            return grid->seal(epoch, Revision(4), SimulationTick(1), hash)
                .expect("test-only grid digest")
                .payload.toJson()
                .expect("grid JSON");
        };
        const auto beforeGrid = gridPayload();
        const auto beforeCore = corePayload();
        const auto beforeRoot = domain.canonicalStateJson().expect("root observation");
        const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
        allocations  = 0;
        injected     = false;
        failure      = index;
        measuring    = true;
        bool success = false;
        try {
            auto captured = session->capture(map, epoch, Revision(4));
            success       = captured.ok();
        } catch (const std::bad_alloc&) {
        }
        measuring      = false;
        count          = allocations;
        const auto hit = injected;
        failure        = std::numeric_limits<std::size_t>::max();
        require(inject ? (hit && !success) : success, "capture must abort every injected allocation", index);
        require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
                "capture changed attribute registrations", index);
        require(corePayload() == beforeCore && gridPayload() == beforeGrid &&
                    domain.canonicalStateJson().expect("root after capture") == beforeRoot,
                "capture changed authoritative core/grid/domain facts", index);
        auto captured = session->capture(map, epoch, Revision(4)).expect("same-boundary joint capture retry");
        require(captured.core()->get().membership().size() == 1 && captured.coreTick() == SimulationTick(1) &&
                    captured.provenance()->get().saveEpoch() == epoch && captured.assets() && captured.gridState(),
                "capture omitted joined participants", index);
        require(captured.pendingDamage().size() == (raw ? 1u : 0u) &&
                    captured.pendingLifecycle().size() == (pending ? 1u : 0u) &&
                    captured.provenance()->get().lifecycleSequence() == (pending ? 1u : 0u),
                "capture consumed or lost a retained event stage", index);
        const auto bodies = (pending || raw) ? 0u : 1u;
        require(captured.roots()->get().units.size() == bodies && crowd.getAgentCount() == static_cast<int>(bodies),
                "native body/Crowd facts diverged", index);
        session->close(spawn::DetachPolicy::RemoveUnits).expect("explicit cleanup after capture");
        require(crowd.getAgentCount() == 0, "capture cleanup left agents", index);
    }
    require(
        cap::listenerCount<IStateQuery>() == initialQueries && cap::listenerCount<IStateMutation>() == initialMutations,
        "capture scenario destruction retained attribute registrations", index);
    return count;
}
std::size_t runtimeFileAttempt(std::size_t index, bool inject, const char* stage) {
    using namespace eve;
    const auto epoch       = retainedCheckpoint->saveEpoch();
    const auto envelope    = retainedCheckpoint->seal(hash).expect("whole pending file baseline");
    const auto wire        = serializeSnapshotEnvelope(envelope).takeValue();
    const auto idleQueries = cap::listenerCount<IStateQuery>(), idleMutations = cap::listenerCount<IStateMutation>();
    auto       initial          = map_rts_spawn::RTSRuntimeCheckpoint::parse(envelope, epoch, hash).takeValue();
    auto       initialCandidate = map_rts_spawn::PreparedRTSRuntimeRestore::prepare(
                                std::make_shared<const map_rts_spawn::RTSRuntimeCheckpoint>(std::move(initial)), hash)
                                .takeValue();
    std::unique_ptr<map_rts_spawn::RTSMapRuntime> target;
    initialCandidate.commit(target).expect("live receiver before file failure");
    const auto* oldOwner = target.get();
    const auto  oldState = target->captureState().takeValue();
    const auto  queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    allocations   = 0;
    failure       = inject ? index : std::numeric_limits<std::size_t>::max();
    injected      = false;
    bool complete = false;
    measuring     = true;
    try {
        if (std::strcmp(stage, "runtime-seal") == 0) {
            auto result = retainedCheckpoint->seal(hash);
            complete    = result.ok();
        } else {
            auto result = map_rts_spawn::RTSRuntimeCheckpoint::parse(envelope, epoch, hash);
            complete    = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? injected && !complete : complete, "whole runtime file swallowed failure", index);
    require(target.get() == oldOwner && target->captureState().takeValue() == oldState &&
                cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "file codec changed live receiver or published services", index);
    require(serializeSnapshotEnvelope(retainedCheckpoint->seal(hash).takeValue()).takeValue() == wire,
            "failed file operation mutated frozen source or left it busy", index);
    auto parsed = map_rts_spawn::RTSRuntimeCheckpoint::parse(envelope, epoch, hash).expect("same whole file retry");
    require(serializeSnapshotEnvelope(parsed.seal(hash).takeValue()).takeValue() == wire,
            "whole file retry changed canonical facts", index);
    auto candidate = map_rts_spawn::PreparedRTSRuntimeRestore::prepare(
                         std::make_shared<const map_rts_spawn::RTSRuntimeCheckpoint>(std::move(parsed)), hash)
                         .expect("file joins existing native admission");
    candidate.commit(target).expect("whole pending file replaces complete owner");
    require(target.get() != oldOwner && target->captureState().takeValue() == oldState,
            "file replacement failed to retire old owner or changed pending state", index);
    const auto pending = *retainedCheckpoint->pendingStep();
    require(!target->advance({SimulationTick(pending.tick.value() + 1), pending.delta}).ok(),
            "file pending owner accepted another tick", index);
    target->advance(pending).expect("file resumes exact retained handoff");
    target->advance({SimulationTick(pending.tick.value() + 1), pending.delta}).expect("file continues real next frame");
    target->close().expect("file restored owner cleanup");
    target.reset();
    require(cap::listenerCount<IStateQuery>() == idleQueries && cap::listenerCount<IStateMutation>() == idleMutations,
            "restored file owner retained services after retirement", index);
    return count;
}
std::size_t handoffAttempt(std::size_t index, bool inject, const char* stage) {
    using namespace eve;
    const auto epoch    = retainedHandoff->saveEpoch();
    const auto envelope = retainedHandoff->seal(hash).expect("pending handoff baseline");
    const auto wire     = serializeSnapshotEnvelope(envelope).expect("pending handoff baseline wire");
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    allocations   = 0;
    failure       = inject ? index : std::numeric_limits<std::size_t>::max();
    injected      = false;
    bool complete = false;
    measuring     = true;
    try {
        if (std::strcmp(stage, "handoff-capture") == 0) {
            auto result = map_rts_spawn::RTSRuntimeHandoff::capture(*retainedCheckpoint);
            complete    = result.ok();
        } else if (std::strcmp(stage, "handoff-seal") == 0) {
            auto result = retainedHandoff->seal(hash);
            complete    = result.ok();
        } else {
            auto result = map_rts_spawn::RTSRuntimeHandoff::parse(envelope, epoch, hash);
            complete    = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? injected && !complete : complete, "handoff codec swallowed failure", index);
    require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "handoff codec registered services", index);
    require(serializeSnapshotEnvelope(retainedHandoff->seal(hash).expect("handoff source retry"))
                    .expect("handoff retry wire") == wire,
            "handoff codec changed source", index);
    auto parsed = map_rts_spawn::RTSRuntimeHandoff::parse(envelope, epoch, hash).expect("same handoff input retry");
    require(parsed.phase() == retainedHandoff->phase() && parsed.completedTick() == retainedHandoff->completedTick() &&
                parsed.pendingStep()->tick == retainedHandoff->pendingStep()->tick &&
                parsed.pendingStep()->delta == retainedHandoff->pendingStep()->delta,
            "handoff retry changed pending responsibility", index);
    require(serializeSnapshotEnvelope(parsed.seal(hash).expect("parsed handoff seal")).expect("parsed handoff wire") ==
                wire,
            "handoff retry changed file facts", index);
    return count;
}
}  // namespace
void* operator new(std::size_t bytes) {
    if (measuring && allocations++ == failure) {
        injected = true;
        throw std::bad_alloc();
    }
    if (auto* value = std::malloc(bytes ? bytes : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void  operator delete(void* value) noexcept { std::free(value); }
void  operator delete[](void* value) noexcept { std::free(value); }
void  operator delete(void* value, std::size_t) noexcept { std::free(value); }
void  operator delete[](void* value, std::size_t) noexcept { std::free(value); }
int   main(int argc, char** argv) {
    if (argc > 1) {
        const char* stage = argv[1];
        if (std::strcmp(stage, "runtime-seal") == 0 || std::strcmp(stage, "runtime-parse") == 0) {
            const auto never      = std::numeric_limits<std::size_t>::max();
            retainHandoff         = true;
            const auto frameCount = attempt(never, false);
            for (std::size_t i = 0; i < frameCount && !retainedHandoff; ++i) attempt(i, true);
            retainHandoff = false;
            require(retainedCheckpoint != nullptr, "actual pending checkpoint missing", frameCount);
            runtimeFileAttempt(never, false, stage);
            const auto count = runtimeFileAttempt(never, false, stage);
            for (std::size_t i = 0; i < count; ++i) runtimeFileAttempt(i, true, stage);
            std::printf(
                "RTS %s: %zu ordinary allocation failpoints; exact file retry and pending owning continuation\n", stage,
                count);
            return 0;
        }
        if (std::strcmp(stage, "handoff-capture") == 0 || std::strcmp(stage, "handoff-seal") == 0 ||
            std::strcmp(stage, "handoff-parse") == 0) {
            const auto never      = std::numeric_limits<std::size_t>::max();
            retainHandoff         = true;
            const auto frameCount = attempt(never, false);
            for (std::size_t i = 0; i < frameCount && !retainedHandoff; ++i) attempt(i, true);
            retainHandoff = false;
            require(retainedHandoff.has_value(), "actual SpawnPending boundary not observed", frameCount);
            handoffAttempt(never, false, stage);
            const auto count = handoffAttempt(never, false, stage);
            for (std::size_t i = 0; i < count; ++i) handoffAttempt(i, true, stage);
            std::printf(
                "RTS %s: %zu ordinary allocation failpoints, retired-source pending responsibility and exact file "
                  "retry\n",
                stage, count);
            return 0;
        }
        if (std::strcmp(stage, "unit-attributes-seal") == 0 || std::strcmp(stage, "unit-attributes-parse") == 0) {
            const bool seal  = std::strcmp(stage, "unit-attributes-seal") == 0;
            const auto never = std::numeric_limits<std::size_t>::max();
            unitAttributeArchiveAttempt(never, false, seal);
            const auto count = unitAttributeArchiveAttempt(never, false, seal);
            for (std::size_t index = 0; index < count; ++index) unitAttributeArchiveAttempt(index, true, seal);
            std::printf("RTS %s: %zu ordinary allocation failpoints, intact game/projections and same-state retry\n",
                          stage, count);
            return 0;
        }
        if (std::strcmp(stage, "projection-encode") == 0 || std::strcmp(stage, "projection-prepare") == 0) {
            const bool encode = std::strcmp(stage, "projection-encode") == 0;
            const auto never  = std::numeric_limits<std::size_t>::max();
            projectionStateAttempt(never, false, encode);
            const auto count = projectionStateAttempt(never, false, encode);
            for (std::size_t index = 0; index < count; ++index) projectionStateAttempt(index, true, encode);
            std::printf(
                "Projection %s: %zu ordinary allocation failpoints, intact revision/init/value facts and retry\n",
                stage, count);
            return 0;
        }
        if (std::strcmp(stage, "attributes-encode") == 0 || std::strcmp(stage, "attributes-prepare") == 0) {
            const bool encode = std::strcmp(stage, "attributes-encode") == 0;
            const auto never  = std::numeric_limits<std::size_t>::max();
            attributeStateAttempt(never, false, encode);
            const auto count = attributeStateAttempt(never, false, encode);
            for (std::size_t index = 0; index < count; ++index) attributeStateAttempt(index, true, encode);
            std::printf(
                "Attributes %s: %zu ordinary allocation failpoints, intact values/counters and same-state retry\n",
                stage, count);
            return 0;
        }
        if (std::strcmp(stage, "combat-capture") == 0 || std::strcmp(stage, "combat-seal") == 0 ||
            std::strcmp(stage, "combat-parse") == 0) {
            const auto never = std::numeric_limits<std::size_t>::max();
            combatArchiveAttempt(never, false, stage);
            const auto count = combatArchiveAttempt(never, false, stage);
            for (std::size_t index = 0; index < count; ++index) combatArchiveAttempt(index, true, stage);
            std::printf("RTS %s: %zu ordinary allocation failpoints, intact mirror/latch facts and same-state retry\n",
                          stage, count);
            return 0;
        }
        if (std::strcmp(stage, "frame-capture") == 0 || std::strcmp(stage, "frame-seal") == 0 ||
            std::strcmp(stage, "frame-parse") == 0) {
            const auto never = std::numeric_limits<std::size_t>::max();
            frameArchiveAttempt(never, false, stage);
            const auto count = frameArchiveAttempt(never, false, stage);
            for (std::size_t index = 0; index < count; ++index) frameArchiveAttempt(index, true, stage);
            std::printf("RTS %s: %zu ordinary allocation failpoints, intact audit/queues and same-state retry\n", stage,
                          count);
            return 0;
        }
        if (std::strcmp(stage, "request-seal") == 0 || std::strcmp(stage, "request-parse") == 0) {
            const auto never = std::numeric_limits<std::size_t>::max();
            replayAttempt(never, false, false, false, stage);
            const auto count = replayAttempt(never, false, false, false, stage);
            for (std::size_t index = 0; index < count; ++index) replayAttempt(index, true, false, false, stage);
            std::printf("Settlement %s: %zu ordinary allocation failpoints, exact input and same-state retry\n", stage,
                          count);
            return 0;
        }
        if (std::strcmp(stage, "replay-request") == 0 || std::strcmp(stage, "replay-verify") == 0 ||
            std::strcmp(stage, "replay-result") == 0) {
            const bool verify     = std::strcmp(stage, "replay-verify") == 0;
            const bool resultOnly = std::strcmp(stage, "replay-result") == 0;
            const auto never      = std::numeric_limits<std::size_t>::max();
            replayAttempt(never, false, verify, resultOnly);
            const auto count = replayAttempt(never, false, verify, resultOnly);
            for (std::size_t index = 0; index < count; ++index) replayAttempt(index, true, verify, resultOnly);
            std::printf("Settlement %s: %zu ordinary allocation failpoints, exact audit and same-state retry\n", stage,
                          count);
            return 0;
        }
        if (std::strcmp(stage, "sensing-seal") == 0 || std::strcmp(stage, "sensing-parse") == 0) {
            const bool parse = std::strcmp(stage, "sensing-parse") == 0;
            const auto never = std::numeric_limits<std::size_t>::max();
            sensingAttempt(never, false, parse);
            const auto count = sensingAttempt(never, false, parse);
            for (std::size_t index = 0; index < count; ++index) sensingAttempt(index, true, parse);
            std::printf("Sensing %s: %zu ordinary allocation failpoints, intact facts/index and same-state retry\n",
                          stage, count);
            return 0;
        }
        if (std::strcmp(stage, "owner-admit") == 0 || std::strcmp(stage, "owner-restore") == 0) {
            const bool commit = std::strcmp(stage, "owner-restore") == 0;
            const auto never  = std::numeric_limits<std::size_t>::max();
            ownedRestoreAttempt(never, false, commit);
            const auto count = ownedRestoreAttempt(never, false, commit);
            for (std::size_t index = 0; index < count; ++index) ownedRestoreAttempt(index, true, commit);
            std::printf("RTS %s: %zu ordinary allocation failpoints, intact old game/listeners and same-token retry\n",
                          stage, count);
            return 0;
        }
        if (std::strcmp(stage, "damage-configure") == 0 || std::strcmp(stage, "damage-capture") == 0 ||
            std::strcmp(stage, "damage-prepare") == 0 || std::strcmp(stage, "damage-seal") == 0 ||
            std::strcmp(stage, "damage-parse") == 0) {
            const auto never = std::numeric_limits<std::size_t>::max();
            damageAttempt(never, false, stage);
            const auto count = damageAttempt(never, false, stage);
            for (std::size_t index = 0; index < count; ++index) damageAttempt(index, true, stage);
            std::printf(
                "Damage %s: %zu ordinary allocation failpoints, intact rules/pipeline/policy and same-state retry\n",
                stage, count);
            return 0;
        }

        if (std::strcmp(stage, "definition-capture") == 0 || std::strcmp(stage, "definition-prepare") == 0 ||
            std::strcmp(stage, "definition-seal") == 0 || std::strcmp(stage, "definition-parse") == 0) {
            const auto never = std::numeric_limits<std::size_t>::max();
            // Warm canonical schema registration so every measured attempt has the same process state.
            definitionAttempt(never, false, stage);
            const auto count = definitionAttempt(never, false, stage);
            for (std::size_t index = 0; index < count; ++index) definitionAttempt(index, true, stage);
            std::printf(
                "Definitions %s: %zu ordinary allocation failpoints, intact source/metadata and same-state retry\n",
                stage, count);
            return 0;
        }

        if (std::strcmp(stage, "listener-publish") == 0 || std::strcmp(stage, "rts-construct") == 0 ||
            std::strcmp(stage, "owner-prepare") == 0) {
            const auto never = std::numeric_limits<std::size_t>::max();
            publicationAttempt(never, false, stage);
            const auto count = publicationAttempt(never, false, stage);
            for (std::size_t index = 0; index < count; ++index) publicationAttempt(index, true, stage);
            std::printf("RTS %s: %zu ordinary allocation failpoints, hidden candidate and intact old owner\n", stage,
                          count);
            return 0;
        }

        if (std::strcmp(stage, "session-admit") == 0 || std::strcmp(stage, "session-prepare") == 0 ||
            std::strcmp(stage, "session-commit") == 0) {
            const bool commit = std::strcmp(stage, "session-commit") == 0;
            const bool admit  = std::strcmp(stage, "session-admit") == 0;
            const auto never  = std::numeric_limits<std::size_t>::max();
            const auto count  = restoredSessionAttempt(never, false, commit, admit);
            for (std::size_t index = 0; index < count; ++index) restoredSessionAttempt(index, true, commit, admit);
            std::printf("RTS restored %s: %zu ordinary allocation failpoints, intact old owner and same-state retry\n",
                          stage, count);
            return 0;
        }
        if (std::strcmp(stage, "tracker-seal") == 0 || std::strcmp(stage, "tracker-parse") == 0) {
            const bool seal  = std::strcmp(stage, "tracker-seal") == 0;
            const auto never = std::numeric_limits<std::size_t>::max();
            trackerCodecAttempt(never, false, seal);
            const auto count = trackerCodecAttempt(never, false, seal);
            for (std::size_t index = 0; index < count; ++index) trackerCodecAttempt(index, true, seal);
            std::printf(
                "RTS tracker %s: %zu ordinary allocation failpoints, unchanged worlds and exact pending publication "
                  "retry\n",
                stage, count);
            return 0;
        }
        if (std::strcmp(stage, "tracker-capture") == 0 || std::strcmp(stage, "tracker-restore") == 0) {
            const bool capture = std::strcmp(stage, "tracker-capture") == 0;
            const auto never   = std::numeric_limits<std::size_t>::max();
            const auto count   = trackerAttempt(never, false, capture);
            for (std::size_t index = 0; index < count; ++index) trackerAttempt(index, true, capture);
            std::printf("RTS tracker %s: %zu ordinary allocation failpoints, intact roots and retryable binding\n",
                          stage, count);
            return 0;
        }
        if (std::strcmp(stage, "navigation-encode") == 0 || std::strcmp(stage, "navigation-decode") == 0) {
            const bool encode = std::strcmp(stage, "navigation-encode") == 0;
            const auto never  = std::numeric_limits<std::size_t>::max();
            navigationCodecAttempt(never, false, encode);
            const auto count = navigationCodecAttempt(never, false, encode);
            for (std::size_t index = 0; index < count; ++index) navigationCodecAttempt(index, true, encode);
            std::printf(
                "Map provider %s: %zu ordinary allocation failpoints, unchanged cache/history and pending sync retry\n",
                stage, count);
            return 0;
        }
        if (std::strcmp(stage, "navigation-capture") == 0 || std::strcmp(stage, "navigation-prepare") == 0) {
            const bool restore = std::strcmp(stage, "navigation-prepare") == 0;
            const auto never   = std::numeric_limits<std::size_t>::max();
            const auto count   = navigationAttempt(never, false, restore);
            for (std::size_t index = 0; index < count; ++index) navigationAttempt(index, true, restore);
            std::printf("Map provider %s: %zu ordinary allocation failpoints, intact source and same-state retry\n",
                          stage, count);
            return 0;
        }
        if (std::strcmp(stage, "owned-capture") == 0) {
            const auto never = std::numeric_limits<std::size_t>::max();
            ownedCaptureAttempt(never, false);
            const auto count = ownedCaptureAttempt(never, false);
            for (std::size_t index = 0; index < count; ++index) ownedCaptureAttempt(index, true);
            std::printf(
                "RTS owner checkpoint: %zu allocation failpoints; intact phase/clocks/roots and same-boundary retry\n",
                count);
            return 0;
        }
        require(std::strcmp(stage, "capture") == 0 || std::strcmp(stage, "capture-raw") == 0 ||
                      std::strcmp(stage, "capture-pending") == 0,
                  "unknown allocation scenario", 0);
        const auto never = std::numeric_limits<std::size_t>::max();
        captureAttempt(never, false, stage);
        const auto count = captureAttempt(never, false, stage);
        for (std::size_t index = 0; index < count; ++index) captureAttempt(index, true, stage);
        std::printf(
            "RTS map %s: %zu allocation failpoints; intact core/domain/events and successful same-boundary retry\n",
            stage, count);
        return 0;
    }
    const auto never = std::numeric_limits<std::size_t>::max();
    attempt(never, false);
    const auto count = attempt(never, false);
    pendingCount = faultedCount = completedCount = 0;
    for (std::size_t index = 0; index < count; ++index) attempt(index, true);
    require(pendingCount && faultedCount, "both actual frame phases must be exercised", count);
    std::printf(
        "RTS runtime: %zu allocation failpoints; pending=%zu faulted=%zu completed=%zu, coherent roots/Crowd and no "
          "repeated domain movement\n",
        count, pendingCount, faultedCount, completedCount);
}
