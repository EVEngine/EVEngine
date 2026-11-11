#include "map/rts_spawn/RTSRuntimeHandoff.h"
#include <charconv>
#include <limits>
#include "map/rts_spawn/RTSMapCheckpoint.h"
#include "map/rts_spawn/RTSRuntimeCheckpoint.h"
#include "rts/RTS.h"
#include "rts/RTSFrameArchive.h"
#include "spawn/SpawnSavedState.h"
#include "spawn/SpawnWorld.h"
namespace eve::map_rts_spawn {
namespace {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message) {
    throw Invalid{Status::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "map.rts-handoff"))};
}
template <class T>
T take(Result<T> value) {
    if (!value) throw Invalid{value.status()};
    return std::move(value).takeValue();
}
std::uint64_t counter(const Value& value) {
    const auto* s = value.getIf<std::string>();
    if (!s) invalid("Expected handoff counter");
    std::uint64_t n = 0;
    auto          p = std::from_chars(s->data(), s->data() + s->size(), n);
    if (p.ec != std::errc{} || p.ptr != s->data() + s->size() || std::to_string(n) != *s)
        invalid("Invalid handoff counter");
    return n;
}
SimulationTick validate(RTSCheckpointPhase phase, SimulationTick completed,
                        const std::optional<SimulationStep>& pending, std::span<const rts::RTSFrameDamageEvent> damage,
                        std::span<const spawn::SavedLifecycleInput> lifecycle) {
    if (phase != RTSCheckpointPhase::Ready && phase != RTSCheckpointPhase::SpawnPending)
        invalid("Unknown handoff phase");
    if ((phase == RTSCheckpointPhase::SpawnPending) != pending.has_value()) invalid("Phase and pending step disagree");
    if (pending && (pending->delta.nanoseconds() <= 0 || completed.value() == UINT64_MAX ||
                    pending->tick.value() != completed.value() + 1))
        invalid("Invalid pending frame responsibility");
    if (phase == RTSCheckpointPhase::Ready && (!damage.empty() || !lifecycle.empty()))
        invalid("Ready owner retained unconsumed inputs");
    const auto tick = pending ? pending->tick : completed;
    for (const auto& event : damage)
        if (event.sequence == 0 || event.tick > tick) invalid("Damage input lies outside handoff boundary");
    return tick;
}
}  // namespace
struct RTSRuntimeHandoff::Data {
    PersistentId                            epoch;
    Revision                                revision;
    SimulationTick                          completed;
    RTSCheckpointPhase                      phase = RTSCheckpointPhase::Ready;
    std::optional<SimulationStep>           pending;
    std::vector<rts::RTSFrameDamageEvent>   damage;
    std::vector<spawn::SavedLifecycleInput> lifecycle;
};
RTSRuntimeHandoff::RTSRuntimeHandoff(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
RTSRuntimeHandoff::~RTSRuntimeHandoff()                                       = default;
RTSRuntimeHandoff::RTSRuntimeHandoff(RTSRuntimeHandoff&&) noexcept            = default;
RTSRuntimeHandoff& RTSRuntimeHandoff::operator=(RTSRuntimeHandoff&&) noexcept = default;
PersistentId       RTSRuntimeHandoff::saveEpoch() const noexcept { return data_ ? data_->epoch : PersistentId{}; }
Revision           RTSRuntimeHandoff::revision() const noexcept { return data_ ? data_->revision : Revision{}; }
SimulationTick RTSRuntimeHandoff::completedTick() const noexcept { return data_ ? data_->completed : SimulationTick{}; }
std::optional<RTSCheckpointPhase> RTSRuntimeHandoff::phase() const noexcept {
    return data_ ? std::optional{data_->phase} : std::nullopt;
}
std::optional<SimulationStep> RTSRuntimeHandoff::pendingStep() const noexcept {
    return data_ ? data_->pending : std::nullopt;
}
std::span<const rts::RTSFrameDamageEvent> RTSRuntimeHandoff::damageInputs() const noexcept {
    return data_ ? std::span<const rts::RTSFrameDamageEvent>(data_->damage)
                 : std::span<const rts::RTSFrameDamageEvent>{};
}
std::span<const spawn::SavedLifecycleInput> RTSRuntimeHandoff::lifecycleInputs() const noexcept {
    return data_ ? std::span<const spawn::SavedLifecycleInput>(data_->lifecycle)
                 : std::span<const spawn::SavedLifecycleInput>{};
}
Result<RTSRuntimeHandoff> RTSRuntimeHandoff::capture(const RTSRuntimeCheckpoint& checkpoint) {
    using R = Result<RTSRuntimeHandoff>;
    if (checkpoint.saveEpoch().isNil() || checkpoint.revision().isZero() || !checkpoint.phase() || !checkpoint.map())
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Incomplete runtime checkpoint"));
    try {
        auto data       = std::make_shared<Data>();
        data->epoch     = checkpoint.saveEpoch();
        data->revision  = checkpoint.revision();
        data->completed = checkpoint.completedTick();
        data->phase     = *checkpoint.phase();
        data->pending   = checkpoint.pendingStep();
        const auto& map = checkpoint.map()->get();
        if (map.saveEpoch() != data->epoch || map.revision() != data->revision || map.coreTick() != data->completed)
            invalid("Handoff and map/core boundary disagree");
        auto damage  = take(rts::RTSFrameArchive::encodeDamageInputs(map.pendingDamage()));
        data->damage = take(rts::RTSFrameArchive::prepareDamageInputs(damage));
        data->lifecycle.reserve(map.pendingLifecycle().size());
        for (const auto& input : map.pendingLifecycle()) data->lifecycle.push_back(spawn::savedLifecycleInput(input));
        auto encoded = spawn::encodeSavedLifecycleInputs(data->lifecycle);
        if (!encoded) return R::failure(encoded.status());
        validate(data->phase, data->completed, data->pending, data->damage, data->lifecycle);
        return R::success(RTSRuntimeHandoff(std::move(data)));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
Result<SnapshotEnvelope> RTSRuntimeHandoff::seal(const SnapshotHashProvider& hash) const {
    using R         = Result<SnapshotEnvelope>;
    const auto data = data_;
    if (!data) return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Handoff archive moved"));
    try {
        const auto tick      = validate(data->phase, data->completed, data->pending, data->damage, data->lifecycle);
        auto       damage    = take(rts::RTSFrameArchive::encodeDamageInputs(data->damage));
        auto       lifecycle = take(spawn::encodeSavedLifecycleInputs(data->lifecycle));
        Value      pending;
        if (data->pending)
            pending = Value::Array{std::to_string(data->pending->tick.value()),
                                   std::to_string(data->pending->delta.nanoseconds())};
        auto payload = Value::object({{"phase", data->phase == RTSCheckpointPhase::Ready ? "ready" : "spawnPending"},
                                      {"completed", std::to_string(data->completed.value())},
                                      {"pending", std::move(pending)},
                                      {"damage", std::move(damage)},
                                      {"lifecycle", std::move(lifecycle)}});
        return makeSnapshotEnvelope("eve.map-rts-handoff", *LogicalId::fromParts("map", "rts-handoff"),
                                    SchemaVersion(1), data->epoch, data->revision, tick, std::move(payload), hash);
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
Result<RTSRuntimeHandoff> RTSRuntimeHandoff::parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                   const SnapshotHashProvider& hash) {
    using R       = Result<RTSRuntimeHandoff>;
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return R::failure(verified.status());
    if (snapshot.schemaVersion != SchemaVersion(1))
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown RTS handoff version"));
    if (epoch.isNil() || snapshot.instanceId != epoch || snapshot.revision.isZero() ||
        snapshot.type != "eve.map-rts-handoff" || snapshot.schema != *LogicalId::fromParts("map", "rts-handoff"))
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "RTS handoff save boundary mismatch"));
    try {
        const auto* root = snapshot.payload.getIf<Value::Object>();
        if (!root || root->size() != 5) invalid("Unknown or missing handoff fields");
        for (const auto* key : {"phase", "completed", "pending", "damage", "lifecycle"})
            if (!root->contains(key)) invalid("Missing handoff field");
        const auto* phase = root->at("phase").getIf<std::string>();
        if (!phase || (*phase != "ready" && *phase != "spawnPending")) invalid("Unknown handoff phase");
        auto data       = std::make_shared<Data>();
        data->epoch     = epoch;
        data->revision  = snapshot.revision;
        data->completed = SimulationTick(counter(root->at("completed")));
        data->phase     = *phase == "ready" ? RTSCheckpointPhase::Ready : RTSCheckpointPhase::SpawnPending;
        if (!root->at("pending").isNull()) {
            const auto* a = root->at("pending").getIf<Value::Array>();
            if (!a || a->size() != 2) invalid("Invalid pending step fields");
            const auto nanos = counter((*a)[1]);
            if (nanos > INT64_MAX) invalid("Pending duration exceeds simulation range");
            data->pending = SimulationStep{SimulationTick(counter((*a)[0])), Duration(std::int64_t(nanos))};
        }
        data->damage    = take(rts::RTSFrameArchive::prepareDamageInputs(root->at("damage")));
        data->lifecycle = take(spawn::prepareSavedLifecycleInputs(root->at("lifecycle")));
        const auto tick = validate(data->phase, data->completed, data->pending, data->damage, data->lifecycle);
        if (tick != snapshot.tick) invalid("Envelope and handoff domain clock disagree");
        return R::success(RTSRuntimeHandoff(std::move(data)));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
}  // namespace eve::map_rts_spawn
