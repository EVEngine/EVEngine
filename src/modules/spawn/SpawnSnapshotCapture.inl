#pragma once
#include "common/Assert.h"
// Private checkpoint codec, included after SpawnWorld::Impl in its owning translation unit.
namespace eve::spawn {
namespace {
std::string checkpointRevisionHashInput(const std::string& source) { return "eve.spawn-template/v1\n" + source; }
Value       checkpointNumber(std::uint64_t value) { return Value(std::to_string(value)); }
Value       checkpointSigned(std::int64_t value) { return Value(std::to_string(value)); }
Value       checkpointHandle(SpawnerHandle source) {
    return Value::object({{"owner", source.owner.format()},
                                {"epoch", checkpointNumber(source.epoch)},
                                {"slot", checkpointNumber(source.slot.index())},
                                {"generation", checkpointNumber(source.slot.generation())}});
}
Value checkpointActivation(const Activation& activation) {
    if (const auto* event = std::get_if<EventActivation>(&activation))
        return Value::object({{"kind", "event"}, {"event", event->event}});
    if (const auto* proximity = std::get_if<ProximityActivation>(&activation))
        return Value::object({{"kind", "player_proximity"},
                              {"enter_distance", proximity->enterDistance},
                              {"exit_distance", proximity->exitDistance}});
    return Value::object({{"kind", "map_loaded"}});
}
const char* checkpointPhase(Phase value) {
    switch (value) {
        case Phase::Dormant: return "dormant";
        case Phase::Running: return "running";
        case Phase::Completed: return "completed";
        case Phase::Faulted: return "faulted";
    }
    EV_ASSERT(false, "Unknown scheduler phase");
    return "";
}
const char* checkpointBlock(BlockReason value) {
    switch (value) {
        case BlockReason::None: return "none";
        case BlockReason::BudgetFull: return "budget_full";
        case BlockReason::PlacementBlocked: return "placement_blocked";
        case BlockReason::ProviderUnavailable: return "provider_unavailable";
        case BlockReason::FactoryFailed: return "factory_failed";
        case BlockReason::UnexpectedRemoval: return "unexpected_removal";
        case BlockReason::ArithmeticOverflow: return "arithmetic_overflow";
    }
    EV_ASSERT(false, "Unknown scheduler block reason");
    return "";
}
const char* checkpointControl(ControlKind value) {
    switch (value) {
        case ControlKind::Activate: return "activate";
        case ControlKind::Pause: return "pause";
        case ControlKind::ReconcilePopulation: return "reconcile_population";
        case ControlKind::RetryFault: return "retry_fault";
    }
    EV_ASSERT(false, "Unknown scheduler control");
    return "";
}
const char* checkpointEvent(SpawnEventKind value) {
    switch (value) {
        case SpawnEventKind::Activated: return "activated";
        case SpawnEventKind::Paused: return "paused";
        case SpawnEventKind::BatchCommitted: return "batch_committed";
        case SpawnEventKind::Blocked: return "blocked";
        case SpawnEventKind::Unblocked: return "unblocked";
        case SpawnEventKind::WaveCompleted: return "wave_completed";
        case SpawnEventKind::Completed: return "completed";
        case SpawnEventKind::Faulted: return "faulted";
        case SpawnEventKind::Detached: return "detached";
        case SpawnEventKind::Reconciled: return "reconciled";
        case SpawnEventKind::RetryRequested: return "retry_requested";
    }
    EV_ASSERT(false, "Unknown scheduler event");
    return "";
}
}  // namespace
struct SpawnCheckpoint::Impl {
    PersistentId                   owner;
    Revision                       revision;
    SimulationTick                 tick;
    Value                          payload;
    bool                           sealing = false;
    std::vector<CheckpointSubject> membership;
    std::vector<DefinitionRef>     units;
};
SpawnCheckpoint::SpawnCheckpoint(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SpawnCheckpoint::~SpawnCheckpoint()                                                       = default;
SpawnCheckpoint::SpawnCheckpoint(SpawnCheckpoint&&) noexcept                              = default;
SpawnCheckpoint&                   SpawnCheckpoint::operator=(SpawnCheckpoint&&) noexcept = default;
std::span<const CheckpointSubject> SpawnCheckpoint::membership() const noexcept {
    return impl_ ? std::span<const CheckpointSubject>(impl_->membership) : std::span<const CheckpointSubject>{};
}
std::span<const DefinitionRef> SpawnCheckpoint::unitReferences() const noexcept {
    return impl_ ? std::span<const DefinitionRef>(impl_->units) : std::span<const DefinitionRef>{};
}
Result<SnapshotEnvelope> SpawnCheckpoint::seal(const SnapshotHashProvider& hashProvider) const {
    if (!impl_ || impl_->sealing)
        return failure<SnapshotEnvelope>(DiagnosticCode::PreconditionViolation, "Expected an idle checkpoint");
    if (!hashProvider)
        return failure<SnapshotEnvelope>(DiagnosticCode::Unsupported, "Checkpoint digest provider is required");
    impl_->sealing = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{impl_->sealing};
    auto                             payload = impl_->payload;
    std::map<ContentId, std::string> digests;
    for (auto& revision : *payload.find("revisions")->getIf<Value::Array>()) {
        const auto source = revision.find("source")->asString();
        auto       digest = invokeProvider([&] { return hashProvider(checkpointRevisionHashInput(source)); });
        if (!digest) return Result<SnapshotEnvelope>::failure(digest.status());
        auto collision = digests.find(digest.value());
        if (collision != digests.end() && collision->second != source)
            return failure<SnapshotEnvelope>(DiagnosticCode::Conflict, "Template revision digest collision");
        digests.emplace(digest.value(), source);
        revision.set("hash", digest.value().format());
    }
    return invokeProvider([&] {
        return makeSnapshotEnvelope("eve.spawn-runtime", *LogicalId::parse("spawn:runtime"), SchemaVersion(1),
                                    impl_->owner, impl_->revision, impl_->tick, std::move(payload), hashProvider);
    });
}
Result<SpawnCheckpoint> SpawnWorld::capture(PersistentId saveEpoch, Revision checkpointRevision) const {
    if (!impl_ || !impl_->attached || impl_->advancing || impl_->notifying)
        return failure<SpawnCheckpoint>(DiagnosticCode::PreconditionViolation,
                                        "Capture requires an idle attached world");
    if (saveEpoch.isNil() || checkpointRevision.isZero())
        return failure<SpawnCheckpoint>(DiagnosticCode::InvalidArgument,
                                        "Joint save identity and revision are required");
    auto& world     = *impl_;
    world.advancing = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{world.advancing};
    // No prepared factory token survives advance; only stable retry intent is captured below.
    Value::Array                         revisions, slots, budgets, commands, events;
    std::map<std::string, std::size_t>   revisionIndices;
    std::map<std::string, DefinitionRef> referenced;
    auto                                 collectMembers = [&](const std::vector<Member>& members) {
        for (const auto& member : members) referenced.emplace(member.unit.format(), member.unit);
    };
    for (const auto& slot : world.slots) {
        const auto& definition = slot.definition.revision->definition();
        if (const auto* fixed = std::get_if<FixedContent>(&definition.content))
            collectMembers(fixed->members);
        else
            collectMembers(std::get<WeightedContent>(definition.content).entries);
        if (const auto* waves = std::get_if<WaveSchedule>(&definition.schedule))
            for (const auto& wave : waves->waves) collectMembers(wave.members);
        if (slot.pending)
            for (const auto& unit : slot.pending->units) referenced.emplace(unit.definition.format(), unit.definition);
        const auto& source = slot.definition.revision->canonicalJson();
        auto        found  = revisionIndices.find(source);
        if (found == revisionIndices.end()) {
            found = revisionIndices.emplace(source, revisions.size()).first;
            revisions.push_back(Value::object({{"source", source}}));
        }
        Value::Array tracked, replacements;
        for (const auto& unit : slot.tracked)
            tracked.push_back(
                Value::object({{"subject", unit.subject.format()}, {"wave", checkpointNumber(unit.wave)}}));
        for (auto due : slot.replacements) replacements.push_back(checkpointSigned(due));
        Value pending;
        if (slot.pending) {
            Value::Array units;
            for (const auto& unit : slot.pending->units)
                units.push_back(
                    Value::object({{"subject", unit.subject.format()}, {"definition", unit.definition.format()}}));
            pending = Value::object({{"operation", slot.pending->operation.format()},
                                     {"ordinal", checkpointNumber(slot.pending->ordinal.value())},
                                     {"units", Value(std::move(units))}});
        }
        slots.push_back(Value::object({{"id", slot.definition.id},
                                       {"template", checkpointNumber(found->second)},
                                       {"budget", checkpointNumber(slot.definition.budgetIndex)},
                                       {"region", buildRegionSource(slot.definition.region)},
                                       {"activation", checkpointActivation(slot.definition.activation)},
                                       {"occupied", slot.occupied},
                                       {"generation", checkpointNumber(slot.generation)},
                                       {"announced", slot.announced},
                                       {"phase", checkpointPhase(slot.phase)},
                                       {"blocked", checkpointBlock(slot.blocked)},
                                       {"active", slot.active},
                                       {"elapsed_ns", checkpointSigned(slot.elapsed)},
                                       {"anchor_ns", checkpointSigned(slot.anchor)},
                                       {"due_ns", checkpointSigned(slot.due)},
                                       {"alive", checkpointNumber(slot.alive)},
                                       {"total", checkpointNumber(slot.total)},
                                       {"wave", checkpointNumber(slot.wave)},
                                       {"initial_done", slot.initialDone},
                                       {"wave_published", slot.wavePublished},
                                       {"operation", checkpointNumber(slot.operation)},
                                       {"content_rng", checkpointNumber(slot.contentRandom)},
                                       {"placement_rng", checkpointNumber(slot.placementRandom)},
                                       {"cooldown_rng", checkpointNumber(slot.cooldownRandom)},
                                       {"replacements", Value(std::move(replacements))},
                                       {"tracked", Value(std::move(tracked))},
                                       {"pending", std::move(pending)}}));
    }
    for (std::size_t i = 0; i < world.budgets.size(); ++i)
        budgets.push_back(Value::object({{"id", world.budgets[i].id},
                                         {"limit", checkpointNumber(world.budgets[i].alive)},
                                         {"alive", checkpointNumber(world.budgetAlive[i])}}));
    for (const auto& command : world.commands)
        commands.push_back(
            Value::object({{"source", checkpointHandle(command.source)}, {"kind", checkpointControl(command.kind)}}));
    for (const auto& event : world.pendingEvents) {
        Value::Array subjects;
        for (auto subject : event.subjects) subjects.push_back(Value(subject.format()));
        events.push_back(Value::object({{"sequence", checkpointNumber(event.sequence.value())},
                                        {"tick", checkpointNumber(event.tick.value())},
                                        {"source", checkpointHandle(event.source)},
                                        {"kind", checkpointEvent(event.kind)},
                                        {"reason", checkpointBlock(event.reason)},
                                        {"operation", event.operation.format()},
                                        {"wave", checkpointNumber(event.wave)},
                                        {"subjects", Value(std::move(subjects))}}));
    }
    auto payload =
        Value::object({{"save_epoch", saveEpoch.format()},
                       {"identity_epoch", checkpointNumber(world.epoch)},
                       {"seed", checkpointNumber(world.seed)},
                       {"random_algorithm", "splitmix64/v1"},
                       {"stream_derivation", "eve.spawn-streams/v1"},
                       {"now_ns", checkpointSigned(world.now)},
                       {"lifecycle_sequence", checkpointNumber(world.sequence)},
                       {"output_sequence", checkpointNumber(world.outputSequence)},
                       {"cursor", checkpointNumber(world.cursor)},
                       {"limits", Value::object({{"instances", checkpointNumber(world.limits.instancesPerTick)},
                                                 {"batches", checkpointNumber(world.limits.batchesPerTick)},
                                                 {"units", checkpointNumber(world.limits.unitsPerTick)},
                                                 {"candidates", checkpointNumber(world.limits.candidatesPerTick)},
                                                 {"queued_commands", checkpointNumber(world.limits.queuedCommands)}})},
                       {"revisions", Value(std::move(revisions))},
                       {"slots", Value(std::move(slots))},
                       {"budgets", Value(std::move(budgets))},
                       {"commands", Value(std::move(commands))},
                       {"events", Value(std::move(events))}});
    auto frozen      = std::make_unique<SpawnCheckpoint::Impl>();
    frozen->owner    = world.owner;
    frozen->revision = checkpointRevision;
    frozen->tick     = SimulationTick(world.tick);
    frozen->payload  = std::move(payload);
    frozen->units.reserve(referenced.size());
    for (auto& [key, reference] : referenced) {
        (void)key;
        frozen->units.push_back(std::move(reference));
    }
    frozen->membership.reserve(world.subjects.size());
    for (const auto& [id, location] : world.subjects)
        frozen->membership.push_back({SubjectRef::fromPersistentId(id), world.handle(location.first)});
    return Result<SpawnCheckpoint>::success(SpawnCheckpoint(std::move(frozen)));
}
}  // namespace eve::spawn
