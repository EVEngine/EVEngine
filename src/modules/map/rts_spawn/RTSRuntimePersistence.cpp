#include <algorithm>
#include "crowd/Crowd.h"
#include "map/MapGridState.h"
#include "map/rts_spawn/RTSMapCheckpointInternal.h"
#include "map/rts_spawn/RTSRuntimeCheckpointInternal.h"
#include "map/rts_spawn/RTSRuntimeHandoff.h"
#include "map/spawn/MapSpawnAssets.h"
#include "rts/RTSCombatArchive.h"
#include "rts/RTSFrameArchive.h"
#include "spawn/SpawnRestoreState.h"
namespace eve::map_rts_spawn {
namespace {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message, DiagnosticCode code = DiagnosticCode::ParseError) {
    throw Invalid{Status::failure(Diagnostic::error(code, message, "map.rts-runtime"))};
}
template <class T>
T take(Result<T> value) {
    if (!value) throw Invalid{value.status()};
    return std::move(value).takeValue();
}
void fields(const Value& value, std::initializer_list<const char*> names) {
    const auto* object = value.getIf<Value::Object>();
    if (!object || object->size() != names.size()) invalid("Unexpected runtime artifact fields");
    for (auto name : names)
        if (!value.find(name)) invalid("Missing runtime artifact participant");
}
struct Units final : spawn::IUnitDefinitionResolver {
    const rts_spawn::RTSSpawnCapture& saved;
    explicit Units(const rts_spawn::RTSSpawnCapture& value) : saved(value) {}
    Result<void> validate(const DefinitionRef& reference) const override {
        auto recipe = saved.resolveFuture(reference);
        if (!recipe) return Result<void>::failure(recipe.status());
        return Result<void>::success();
    }
};
}  // namespace
Result<void> rebuildRuntimeMembers(const rts::RTSStateSnapshot& roots, const rts::RTSCrowdCapture& captured,
                                   std::vector<RTSCrowdCheckpointMember>& target) {
    using R   = Result<void>;
    auto fail = [](const char* text) { return R::failure(Diagnostic::error(DiagnosticCode::Conflict, text)); };
    if (!captured.frame()) return fail("Crowd frame is missing");
    const auto  members = captured.members();
    const auto& frame   = captured.frame()->get();
    if (members.size() != roots.units.size()) return fail("Native roots and Crowd Unit scope differ");
    std::vector<RTSCrowdCheckpointMember> result;
    result.reserve(roots.units.size());
    for (const auto& root : roots.units) {
        auto found =
            std::lower_bound(members.begin(), members.end(), root.subject.persistentId(),
                             [](const auto& member, const auto& id) { return member.subject.persistentId() < id; });
        if (found == members.end() || found->subject != root.subject || found->container != root.container)
            return fail("Native root has no captured Crowd relationship");
        if (!found->frameIndex) {
            if (root.container.isValid()) continue;
            return fail("Active native root has no captured Crowd agent");
        }
        if (*found->frameIndex >= frame.agents.size()) return fail("Crowd frame index is out of range");
        const auto& agent = frame.agents[*found->frameIndex].state;
        if (agent.x != root.motion.x || agent.y != root.motion.y) return fail("Native and Crowd positions disagree");
        result.push_back({root.subject, *found->frameIndex});
    }
    target = std::move(result);
    return R::success();
}
Result<SnapshotEnvelope> RTSRuntimeCheckpoint::seal(const SnapshotHashProvider& hash) const {
    using R   = Result<SnapshotEnvelope>;
    auto data = impl_;
    if (!data || data->sealing)
        return R::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Runtime checkpoint is moved or sealing"));
    if (!data->context.isSuccess()) return R::failure(data->context);
    data->sealing = true;
    struct Reset {
        const Impl& data;
        ~Reset() { data.sealing = false; }
    } reset{*data};
    try {
        auto boundary = validateRTSRuntimeBoundary(*this);
        if (!boundary) return R::failure(boundary.status());
        // Freeze the public handoff projection before the first digest may retire this checkpoint.
        auto       handoff  = take(RTSRuntimeHandoff::capture(*this));
        const auto epoch    = data->map.saveEpoch();
        const auto revision = data->map.revision();
        const auto tick     = data->pending ? data->pending->tick : data->completed;
        auto       frame    = take(rts::RTSFrameArchive::capture(data->events, epoch, revision, tick));
        auto       combat   = take(rts::RTSCombatArchive::capture(data->combatState, epoch, revision, tick));
        Value      payload  = Value::object({{"profile", "native.closed/v1"}});
        auto       add      = [&](const char* key, Result<SnapshotEnvelope> envelope) {
            auto owned = take(std::move(envelope));
            payload.set(key, take(snapshotEnvelopeValue(owned)));
        };
        add("handoff", handoff.seal(hash));
        add("core", data->map.core()->get().seal(hash));
        add("provenance", data->map.provenance()->get().seal(revision, tick, hash));
        payload.set("roots", take(data->map.roots()->get().encodeState()));
        if (!data->map.assets() || !data->map.gridState()) invalid("Runtime map asset/grid closure missing");
        add("assets", data->map.assets()->seal(epoch, revision, tick, hash));
        add("grid", data->map.gridState()->seal(epoch, revision, tick, hash));
        add("definitions", data->definitions.seal(hash));
        add("damage", data->damage.seal(epoch, revision, tick, hash));
        add("crowd", data->crowdState.seal(revision, tick, hash));
        add("navigation", data->navigation.seal(revision, tick, hash));
        add("attributes", data->unitAttributes.seal(revision, tick, hash));
        add("weapons", data->weapons.seal(revision, tick, hash));
        add("actions", data->actions.seal(revision, tick, hash));
        add("frame", frame.seal(hash));
        add("sensing", data->sensingState.seal(epoch, revision, tick, hash));
        add("combat", combat.seal(hash));
        return makeSnapshotEnvelope("eve.map-rts-runtime", *LogicalId::fromParts("map", "rts-runtime"),
                                    SchemaVersion(1), epoch, revision, tick, std::move(payload), hash);
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
Result<RTSRuntimeCheckpoint> RTSRuntimeCheckpoint::parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                         const SnapshotHashProvider& hash) {
    using R = Result<RTSRuntimeCheckpoint>;
    try {
        auto verified = verifySnapshotEnvelope(snapshot, hash);
        if (!verified) return R::failure(verified.status());
        if (snapshot.type != "eve.map-rts-runtime" || snapshot.schema != *LogicalId::fromParts("map", "rts-runtime") ||
            snapshot.schemaVersion != SchemaVersion(1))
            invalid("Unsupported runtime artifact schema or version", DiagnosticCode::Unsupported);
        if (epoch.isNil() || snapshot.instanceId != epoch || snapshot.revision.isZero())
            invalid("Runtime artifact save identity or revision mismatch", DiagnosticCode::Conflict);
        const auto& root = snapshot.payload;
        fields(root, {"profile", "handoff", "core", "provenance", "roots", "assets", "grid", "definitions", "damage",
                      "crowd", "navigation", "attributes", "weapons", "actions", "frame", "sensing", "combat"});
        if (!root.find("profile")->isString() || root.find("profile")->asString() != "native.closed/v1")
            invalid("Unsupported runtime implementation profile", DiagnosticCode::Unsupported);
        auto participant = [&](const char* key, bool sameBoundary = true) {
            auto envelope = take(parseSnapshotEnvelopeValue(*root.find(key), hash));
            if (sameBoundary && (envelope.instanceId != epoch || envelope.revision != snapshot.revision ||
                                 envelope.tick != snapshot.tick))
                invalid("Runtime participant boundary mismatch");
            return envelope;
        };
        auto       handoff    = take(RTSRuntimeHandoff::parse(participant("handoff"), epoch, hash));
        const auto domainTick = handoff.pendingStep() ? handoff.pendingStep()->tick : handoff.completedTick();
        if (domainTick != snapshot.tick) invalid("Runtime handoff tick mismatch");
        auto provenance   = take(rts_spawn::RTSSpawnCapture::parse(participant("provenance"), epoch, hash));
        auto coreEnvelope = participant("core", false);
        // Core envelope instanceId is its own namespace; save epoch is inside its canonical payload.
        if (coreEnvelope.revision != snapshot.revision || coreEnvelope.tick != handoff.completedTick())
            invalid("Core consumed boundary mismatch");
        auto core  = take(spawn::SpawnCheckpoint::parse(std::move(coreEnvelope), epoch, hash, Units(provenance)));
        auto roots = take(rts::RTSStateSnapshot::prepareState(*root.find("roots")));
        auto mapData =
            std::make_unique<RTSMapCheckpoint::Impl>(epoch, snapshot.revision, handoff.completedTick(), std::move(core),
                                                     std::move(provenance), std::move(roots));
        mapData->assets = take(map_spawn::MapSpawnAssets::parse(participant("assets"), epoch, hash));
        mapData->grid   = take(map::MapGridState::parse(participant("grid"), epoch, hash));
        mapData->inputs.assign(handoff.damageInputs().begin(), handoff.damageInputs().end());
        for (const auto& input : handoff.lifecycleInputs()) {
            const auto& source = input.source;
            mapData->pending.push_back(
                {input.subject,
                 input.sequence,
                 input.reason,
                 {RuntimeHandle<spawn::SpawnerTag>(source.slot, source.generation), source.owner, source.epoch, {}}});
        }
        // Registry metadata belongs to the registry, not the game save boundary.
        auto definitions = take(definitions::DefinitionRegistryCapture::parse(participant("definitions", false), hash));
        auto damage      = take(combat::DamageCapture::parse(participant("damage"), epoch, hash));
        auto crowd       = take(rts::RTSCrowdCapture::parse(participant("crowd"), epoch, hash));
        auto navigation  = take(rts::RTSNavigationCapture::parse(participant("navigation"), epoch, hash));
        auto attributes  = take(rts::RTSUnitAttributeCapture::parse(participant("attributes"), epoch, hash));
        auto weapons     = take(rts::RTSWeaponCapture::parse(participant("weapons"), epoch, hash));
        auto actions     = take(rts::RTSActionCapture::parse(participant("actions"), epoch, hash));
        auto frame       = take(rts::RTSFrameArchive::parse(participant("frame"), epoch, hash));
        auto sensing     = take(sensing::SensingCapture::parse(participant("sensing"), epoch, hash));
        auto combat      = take(rts::RTSCombatArchive::parse(participant("combat"), epoch, hash));
        auto data        = std::make_unique<Impl>(Status::success(), std::move(damage), std::move(definitions),
                                                  RTSMapCheckpoint(std::move(mapData)), std::move(crowd), std::move(actions),
                                                  frame.state()->get(), std::move(sensing), combat.state()->get(),
                                                  std::move(weapons), std::move(attributes), std::move(navigation),
                                                  handoff.completedTick(), handoff.pendingStep(), *handoff.phase());
        auto members     = rebuildRuntimeMembers(data->map.roots()->get(), data->crowdState, data->members);
        if (!members) return R::failure(members.status());
        RTSRuntimeCheckpoint result(std::move(data));
        auto                 boundary = validateRTSRuntimeBoundary(result);
        if (!boundary) return R::failure(boundary.status());
        return R::success(std::move(result));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
}  // namespace eve::map_rts_spawn
