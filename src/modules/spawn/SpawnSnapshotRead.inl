#pragma once
#include <charconv>
#include "spawn/SpawnDecode.inl"
#include "spawn/SpawnIdentityInternal.h"
#include "spawn/SpawnRestoreState.h"
namespace eve::spawn {
namespace {
using CheckpointReader = detail::Reader;
std::uint64_t checkpointUnsigned(const CheckpointReader& reader,
                                 std::uint64_t           maximum = std::numeric_limits<std::uint64_t>::max()) {
    const auto    text   = reader.text();
    std::uint64_t value  = 0;
    const auto    parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || (text.size() > 1 && text[0] == '0') ||
        value > maximum)
        detail::reject(reader.path, "Expected canonical bounded decimal integer");
    return value;
}
std::size_t checkpointSize(const CheckpointReader& reader) {
    return static_cast<std::size_t>(checkpointUnsigned(reader, std::numeric_limits<std::size_t>::max()));
}
bool checkpointBool(const CheckpointReader& reader) {
    if (!reader.value.isBool()) detail::reject(reader.path, "Expected boolean");
    return reader.value.asBool();
}
const Value::Array& checkpointArray(const CheckpointReader& reader, std::size_t maximum = 1000000) {
    const auto* array = reader.value.getIf<Value::Array>();
    if (!array || array->size() > maximum) detail::reject(reader.path, "Expected bounded array");
    return *array;
}
PersistentId checkpointId(const CheckpointReader& reader, bool nil = false) {
    const auto text   = reader.text();
    auto       parsed = PersistentId::parse(text);
    if (!parsed || (!nil && parsed->isNil()) || parsed->format() != text)
        detail::reject(reader.path, "Expected canonical persistent identity");
    return *parsed;
}
SubjectRef checkpointSubject(const CheckpointReader& reader) {
    return SubjectRef::fromPersistentId(checkpointId(reader));
}
template <class E>
E checkpointEnum(const CheckpointReader& reader, E last, const char* (*encode)(E)) {
    auto text = reader.text();
    for (unsigned value = 0; value <= static_cast<unsigned>(last); ++value)
        if (text == encode(static_cast<E>(value))) return static_cast<E>(value);
    detail::reject(reader.path, "Unknown checkpoint enum", DiagnosticCode::Unsupported);
}
void checkpointRequire(bool condition, const CheckpointReader& reader, const char* message) {
    if (!condition) detail::reject(reader.path, message, DiagnosticCode::Conflict);
}
template <class T>
T checkpointResult(Result<T> result, const CheckpointReader& reader) {
    if (!result) {
        const auto* diagnostic = result.status().primaryDiagnostic();
        detail::reject(reader.path, diagnostic ? diagnostic->message() : "Checkpoint admission failed",
                       diagnostic ? diagnostic->code() : DiagnosticCode::InvalidArgument);
    }
    return std::move(result).takeValue();
}
struct CheckpointTemplate final : ITemplateResolver {
    const CompiledTemplate& revision;
    explicit CheckpointTemplate(const CompiledTemplate& value) : revision(value) {}
    Result<CompiledTemplate> resolve(const DefinitionRef& reference) const override {
        if (reference.format() != "spawn.template:" + revision.definition().id)
            return failure<CompiledTemplate>(DiagnosticCode::NotFound, "Exact pinned revision missing");
        return Result<CompiledTemplate>::success(revision);
    }
};
}  // namespace
struct SpawnRestoreState::Impl {
    PersistentId                       saveEpoch;
    Revision                           revision;
    std::vector<SavedSpawnMember>      membership;
    std::vector<SavedSpawnerKey>       sources;
    std::vector<SavedPlacementBinding> placement;
    bool                               binding = false;
    std::unique_ptr<SpawnWorld::Impl>  world;
};
SpawnRestoreState::SpawnRestoreState(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SpawnRestoreState::~SpawnRestoreState()                                                           = default;
SpawnRestoreState::SpawnRestoreState(SpawnRestoreState&&) noexcept                                = default;
SpawnRestoreState&                     SpawnRestoreState::operator=(SpawnRestoreState&&) noexcept = default;
std::span<const SavedPlacementBinding> SpawnRestoreState::placementBindings() const noexcept {
    return impl_ ? std::span<const SavedPlacementBinding>(impl_->placement) : std::span<const SavedPlacementBinding>{};
}
Result<SpawnRestoreState> SpawnRestoreState::clone() const {
    using R = Result<SpawnRestoreState>;
    if (!impl_ || impl_->binding)
        return R::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Restore candidate consumed or binding"));
    const auto& source = *impl_->world;
    EV_ASSERT((source.incarnation.isZero() && source.providers.expired()));
    for (const auto& slot : source.slots) EV_ASSERT((!slot.marker && !slot.lifetime.isCurrent()));
    auto copy        = std::make_unique<Impl>();
    copy->saveEpoch  = impl_->saveEpoch;
    copy->revision   = impl_->revision;
    copy->membership = impl_->membership;
    copy->sources    = impl_->sources;
    copy->placement  = impl_->placement;
    copy->world      = std::make_unique<SpawnWorld::Impl>(static_cast<const SpawnWorldFacts&>(source));
    return R::success(SpawnRestoreState(std::move(copy)));
}
PersistentId  SpawnRestoreState::owner() const noexcept { return impl_ ? impl_->world->owner : PersistentId{}; }
PersistentId  SpawnRestoreState::saveEpoch() const noexcept { return impl_ ? impl_->saveEpoch : PersistentId{}; }
std::size_t   SpawnRestoreState::slotCount() const noexcept { return impl_ ? impl_->world->slots.size() : 0; }
std::size_t   SpawnRestoreState::trackedCount() const noexcept { return impl_ ? impl_->world->subjects.size() : 0; }
Revision      SpawnRestoreState::revision() const noexcept { return impl_ ? impl_->revision : Revision{}; }
std::uint32_t SpawnRestoreState::activationInputLimit() const noexcept {
    return impl_ ? impl_->world->limits.queuedCommands : 0;
}
SimulationTick SpawnRestoreState::tick() const noexcept { return SimulationTick(impl_ ? impl_->world->tick : 0); }
std::uint64_t  SpawnRestoreState::acceptedLifecycleSequence() const noexcept {
    return impl_ ? impl_->world->sequence : 0;
}
std::span<const SavedSpawnMember> SpawnRestoreState::membership() const noexcept {
    return impl_ ? std::span<const SavedSpawnMember>(impl_->membership) : std::span<const SavedSpawnMember>{};
}
std::span<const SavedSpawnerKey> SpawnRestoreState::sources() const noexcept {
    return impl_ ? std::span<const SavedSpawnerKey>(impl_->sources) : std::span<const SavedSpawnerKey>{};
}
Result<SourceLease> SpawnWorld::resolveSavedSource(const SavedSpawnerKey& source) const {
    if (!impl_ || source.owner != impl_->owner || source.epoch != impl_->epoch || source.slot >= impl_->slots.size() ||
        !impl_->slots[source.slot].occupied || source.generation != impl_->slots[source.slot].generation ||
        impl_->incarnation.isZero())
        return failure<SourceLease>(DiagnosticCode::StaleHandle, "Saved source does not address this live world");
    return sourceLease(impl_->handle(source.slot));
}
Result<SpawnRestoreBindings> SpawnWorld::restoreBindings() const {
    if (!impl_ || !impl_->attached || impl_->advancing || impl_->notifying)
        return failure<SpawnRestoreBindings>(DiagnosticCode::PreconditionViolation,
                                             "Source projection requires an idle attached world");
    SpawnRestoreBindings result;
    result.saveEpoch         = impl_->restoredSaveEpoch;
    result.revision          = impl_->restoredRevision;
    result.tick              = SimulationTick(impl_->tick);
    result.lifecycleSequence = impl_->sequence;
    auto providers           = impl_->providers.lock();
    result.factory           = providers ? providers->factory : nullptr;
    result.sources.reserve(impl_->slots.size());
    for (const auto& slot : impl_->slots)
        if (slot.occupied)
            result.sources.push_back({slot.lifetime, OperationOrdinal(slot.operation - (slot.pending ? 1 : 0))});
    result.membership.reserve(impl_->subjects.size());
    for (const auto& [id, location] : impl_->subjects) {
        const auto& slot = impl_->slots[location.first];
        result.membership.push_back(
            {SubjectRef::fromPersistentId(id),
             {impl_->owner, impl_->epoch, static_cast<std::uint32_t>(location.first), slot.generation}});
    }
    return Result<SpawnRestoreBindings>::success(std::move(result));
}
Result<SpawnWorld> SpawnWorld::restore(SpawnRestoreState& admitted, const std::shared_ptr<SpawnProviders>& providers) {
    using R = Result<SpawnWorld>;
    if (!admitted.impl_ || admitted.impl_->binding)
        return failure<SpawnWorld>(DiagnosticCode::PreconditionViolation, "Restore candidate consumed or binding");
    const auto binding = providers;
    if (!binding || !binding->placement || !binding->factory)
        return failure<SpawnWorld>(DiagnosticCode::NotFound, "Required restore providers are absent");
    auto placement          = binding->placement;
    auto factory            = binding->factory;
    admitted.impl_->binding = true;
    // @lifetime Guard observes only this synchronous, caller-stable candidate; disarmed before success consumes it.
    struct Reset {
        bool* flag;
        ~Reset() {
            if (flag) *flag = false;
        }
    } reset{&admitted.impl_->binding};
    auto& world = *admitted.impl_->world;
    for (const auto& slot : world.slots) {
        if (!slot.occupied) continue;
        auto valid = invokeProvider([&] {
            return placement->validate(slot.definition.revision->definition().placement, slot.definition.region);
        });
        if (!valid) return R::failure(valid.status());
    }
    if (providers != binding || binding->placement != placement || binding->factory != factory)
        return failure<SpawnWorld>(DiagnosticCode::Conflict, "Restore provider bindings changed during validation");
    SpawnWorld restored(world.owner, world.seed, world.limits);
    const auto incarnation = restored.impl_->incarnation;
    if (incarnation.isZero())
        return failure<SpawnWorld>(DiagnosticCode::PreconditionViolation, "World incarnation exhausted");
    struct Source {
        std::shared_ptr<const std::byte> marker;
        SourceLease                      lease;
    };
    std::vector<Source> sources(world.slots.size());
    for (std::size_t index = 0; index < world.slots.size(); ++index) {
        const auto& slot = world.slots[index];
        if (!slot.occupied) continue;
        auto handle           = world.handle(index);
        handle.incarnation    = incarnation;
        sources[index].marker = std::make_shared<const std::byte>();
        sources[index].lease  = SourceLease(handle, slot.definition.id, sources[index].marker);
    }
    // All fallible work is complete. Transfer canonical facts and issue the new, independently revocable sources.
    world.incarnation       = incarnation;
    world.restoredSaveEpoch = admitted.impl_->saveEpoch;
    world.restoredRevision  = admitted.impl_->revision;
    world.providers         = binding;
    for (std::size_t index = 0; index < world.slots.size(); ++index) {
        auto& slot = world.slots[index];
        if (!slot.occupied) continue;
        slot.marker   = std::move(sources[index].marker);
        slot.lifetime = std::move(sources[index].lease);
        if (slot.pending) {
            slot.pending->source   = world.handle(index);
            slot.pending->lifetime = slot.lifetime;
        }
    }
    for (auto& command : world.commands) command.source.incarnation = incarnation;
    for (auto& event : world.pendingEvents) event.source.incarnation = incarnation;
    restored.impl_ = std::move(admitted.impl_->world);
    reset.flag     = nullptr;
    admitted.impl_.reset();
    return R::success(std::move(restored));
}
Result<SpawnRestoreState> SpawnRestoreState::decode(const SnapshotEnvelope& input, PersistentId saveEpoch,
                                                    const SnapshotHashProvider&    hash,
                                                    const IUnitDefinitionResolver& units) {
    // Copy before unknown callbacks, so a callback cannot change the verified input underneath admission.
    auto snapshot = input;
    auto verified = invokeProvider([&] { return verifySnapshotEnvelope(snapshot, hash); });
    if (!verified) return Result<SpawnRestoreState>::failure(verified.status());
    if (snapshot.type != "eve.spawn-runtime" || snapshot.schema.format() != "spawn:runtime" ||
        snapshot.schemaVersion.value() != 1)
        return failure<SpawnRestoreState>(DiagnosticCode::UnknownVersion, "Unsupported Spawn runtime schema/version");
    if (snapshot.instanceId.isNil() || snapshot.revision.isZero() || saveEpoch.isNil())
        return failure<SpawnRestoreState>(DiagnosticCode::InvalidArgument,
                                          "Runtime owner/save epoch/revision required");
    try {
        CheckpointReader root{snapshot.payload, "$"};
        root.fields({"save_epoch", "identity_epoch", "seed", "random_algorithm", "stream_derivation", "now_ns",
                     "lifecycle_sequence", "output_sequence", "cursor", "limits", "revisions", "slots", "budgets",
                     "commands", "events"});
        checkpointRequire(checkpointId(root.at("save_epoch")) == saveEpoch, root.at("save_epoch"),
                          "Joint save epoch mismatch");
        root.at("random_algorithm").literal("splitmix64/v1");
        root.at("stream_derivation").literal("eve.spawn-streams/v1");
        auto state       = std::make_unique<Impl>();
        state->saveEpoch = saveEpoch;
        state->revision  = snapshot.revision;
        state->world     = std::make_unique<SpawnWorld::Impl>();
        auto& world      = *state->world;
        world.owner      = snapshot.instanceId;
        world.seed       = checkpointUnsigned(root.at("seed"));
        world.epoch      = checkpointUnsigned(root.at("identity_epoch"));
        checkpointRequire(world.epoch != 0, root, "Zero identity epoch");
        world.now            = static_cast<std::int64_t>(checkpointUnsigned(root.at("now_ns"), INT64_MAX));
        world.tick           = snapshot.tick.value();
        world.sequence       = checkpointUnsigned(root.at("lifecycle_sequence"));
        world.outputSequence = checkpointUnsigned(root.at("output_sequence"));
        auto limits          = root.at("limits");
        limits.fields({"instances", "batches", "units", "candidates", "queued_commands"});
        auto limit = [&](const char* name) {
            auto value = checkpointUnsigned(limits.at(name), UINT32_MAX);
            checkpointRequire(value != 0, limits.at(name), "Zero work limit");
            return static_cast<std::uint32_t>(value);
        };
        world.limits = {limit("instances"), limit("batches"), limit("units"), limit("candidates"),
                        limit("queued_commands")};
        std::vector<std::shared_ptr<const CompiledTemplate>> revisions;
        std::set<std::string>                                revisionSources;
        std::set<ContentId>                                  revisionHashes;
        for (const auto& item : checkpointArray(root.at("revisions"))) {
            CheckpointReader entry{item, "$.revisions[" + std::to_string(revisions.size()) + "]"};
            entry.fields({"source", "hash"});
            auto source = entry.at("source").text();
            auto digest = ContentId::parse(entry.at("hash").text());
            checkpointRequire(digest.has_value() && digest->format() == entry.at("hash").text(), entry,
                              "Invalid revision digest");
            auto computed =
                checkpointResult(invokeProvider([&] { return hash(checkpointRevisionHashInput(source)); }), entry);
            checkpointRequire(
                computed == *digest && revisionSources.insert(source).second && revisionHashes.insert(*digest).second,
                entry, "Revision digest mismatch/duplicate");
            auto value    = checkpointResult(Value::fromJson(source), entry);
            auto compiled = checkpointResult(invokeProvider([&] { return compileTemplate(value, units); }), entry);
            checkpointRequire(compiled.canonicalJson() == source, entry, "Template source is not canonical");
            revisions.push_back(std::make_shared<const CompiledTemplate>(std::move(compiled)));
        }
        Value::Array          mapBudgets;
        std::set<std::string> budgetNames;
        for (const auto& item : checkpointArray(root.at("budgets"))) {
            CheckpointReader entry{item, "$.budgets[" + std::to_string(world.budgets.size()) + "]"};
            entry.fields({"id", "limit", "alive"});
            auto id      = detail::identifier(entry.at("id"));
            auto maximum = checkpointUnsigned(entry.at("limit"), UINT32_MAX);
            auto alive   = checkpointUnsigned(entry.at("alive"), UINT32_MAX);
            checkpointRequire(maximum && alive <= maximum && budgetNames.insert(id).second, entry,
                              "Invalid/duplicate budget");
            world.budgets.push_back({id, static_cast<std::uint32_t>(maximum)});
            world.budgetAlive.push_back(static_cast<std::uint32_t>(alive));
            mapBudgets.push_back(
                Value::object({{"id", id}, {"alive_limit", Value(static_cast<std::int64_t>(maximum))}}));
        }
        checkpointRequire(!world.budgets.empty(), root, "No budgets");
        std::vector<std::uint64_t> sums(world.budgets.size());
        std::set<std::string>      ids;
        std::set<PersistentId>     pendingIds;
        const auto& slots = checkpointArray(root.at("slots"), RuntimeHandle<SpawnerTag>::invalidIndex - 1);
        checkpointRequire(slots.empty() ? revisions.empty() : !revisions.empty(), root,
                          "Pinned revisions must match the presence of source slots");
        world.slots.reserve(slots.size());
        for (const auto& item : slots) {
            const auto       index = world.slots.size();
            CheckpointReader entry{item, "$.slots[" + std::to_string(index) + "]"};
            entry.fields({"id",          "template",      "budget",       "region",         "activation",
                          "occupied",    "generation",    "announced",    "phase",          "blocked",
                          "active",      "elapsed_ns",    "anchor_ns",    "due_ns",         "alive",
                          "total",       "wave",          "initial_done", "wave_published", "operation",
                          "content_rng", "placement_rng", "cooldown_rng", "replacements",   "tracked",
                          "pending"});
            const auto id = detail::identifier(entry.at("id"));
            checkpointRequire(ids.insert(id).second, entry, "Duplicate source id");
            if (index)
                checkpointRequire(world.slots.back().definition.id < id, entry,
                                  "Source slots are not in stable id order");
            const auto revision = checkpointUnsigned(entry.at("template"), revisions.size() - 1);
            auto       instance = Value::object({{"id", id},
                                                 {"template", Value::object({{"type", "spawn.template"},
                                                                             {"id", revisions[revision]->definition().id}})},
                                                 {"region", entry.at("region").value},
                                                 {"activation", entry.at("activation").value},
                                                 {"inactive_policy", "pause"},
                                                 {"unload_policy", "persist"}});
            auto       map      = Value::object({{"schema", "eve.map-spawns"},
                                                 {"version", 1},
                                                 {"map", "checkpoint"},
                                                 {"budgets", Value(mapBudgets)},
                                                 {"instances", Value::array({std::move(instance)})}});
            auto       compiled = checkpointResult(
                invokeProvider([&] { return compileMap(map, CheckpointTemplate(*revisions[revision]), units); }),
                entry);
            SpawnWorld::Impl::Slot slot;
            slot.definition   = compiled.instances()[0];
            const auto budget = checkpointUnsigned(entry.at("budget"), world.budgets.size() - 1);
            checkpointRequire(slot.definition.budgetIndex == budget, entry, "Budget reference mismatch");
            slot.occupied   = checkpointBool(entry.at("occupied"));
            slot.generation = static_cast<std::uint32_t>(checkpointUnsigned(entry.at("generation"), UINT32_MAX));
            checkpointRequire(slot.generation != 0, entry, "Invalid source generation");
            slot.announced = checkpointBool(entry.at("announced"));
            slot.phase     = checkpointEnum(entry.at("phase"), Phase::Faulted, checkpointPhase);
            slot.blocked   = checkpointEnum(entry.at("blocked"), BlockReason::ArithmeticOverflow, checkpointBlock);
            slot.active    = checkpointBool(entry.at("active"));
            slot.elapsed   = static_cast<std::int64_t>(checkpointUnsigned(entry.at("elapsed_ns"), INT64_MAX));
            slot.anchor    = static_cast<std::int64_t>(checkpointUnsigned(entry.at("anchor_ns"), INT64_MAX));
            slot.due       = static_cast<std::int64_t>(checkpointUnsigned(entry.at("due_ns"), INT64_MAX));
            checkpointRequire(slot.anchor <= world.now && slot.elapsed <= world.now, entry, "Invalid local age clock");
            if (slot.active)
                checkpointRequire(slot.elapsed <= INT64_MAX - (world.now - slot.anchor), entry, "Local age overflow");
            slot.alive             = static_cast<std::uint32_t>(checkpointUnsigned(entry.at("alive"), UINT32_MAX));
            slot.total             = checkpointUnsigned(entry.at("total"));
            slot.wave              = checkpointSize(entry.at("wave"));
            slot.initialDone       = checkpointBool(entry.at("initial_done"));
            slot.wavePublished     = checkpointBool(entry.at("wave_published"));
            slot.operation         = checkpointUnsigned(entry.at("operation"));
            slot.contentRandom     = checkpointUnsigned(entry.at("content_rng"));
            slot.placementRandom   = checkpointUnsigned(entry.at("placement_rng"));
            slot.cooldownRandom    = checkpointUnsigned(entry.at("cooldown_rng"));
            const auto& definition = slot.definition.revision->definition();
            const auto* waves      = std::get_if<WaveSchedule>(&definition.schedule);
            checkpointRequire((waves && slot.wave <= waves->waves.size()) || (!waves && slot.wave == 0), entry,
                              "Invalid wave cursor");
            for (const auto& tracked : checkpointArray(entry.at("tracked"), definition.limits.alive)) {
                CheckpointReader member{tracked, entry.path + ".tracked"};
                member.fields({"subject", "wave"});
                auto subject = checkpointSubject(member.at("subject"));
                auto wave    = checkpointSize(member.at("wave"));
                checkpointRequire(wave == slot.wave && (!waves || wave < waves->waves.size()), member,
                                  "Invalid tracked wave");
                checkpointRequire(
                    world.subjects.emplace(subject.persistentId(), std::pair{index, slot.tracked.size()}).second,
                    member, "Duplicate tracked subject");
                slot.tracked.push_back({subject, wave});
            }
            checkpointRequire(slot.tracked.size() == slot.alive && slot.total >= slot.alive &&
                                  (!definition.limits.total || slot.total <= *definition.limits.total),
                              entry, "Population ledger mismatch");
            for (const auto& value : checkpointArray(entry.at("replacements"), definition.limits.alive))
                slot.replacements.push_back(
                    static_cast<std::int64_t>(checkpointUnsigned({value, entry.path + ".replacements"}, INT64_MAX)));
            const auto* population = std::get_if<PopulationSchedule>(&definition.schedule);
            checkpointRequire((population && slot.alive + slot.replacements.size() <= population->target) ||
                                  (!population && slot.replacements.empty()),
                              entry, "Invalid replacement ledger");
            checkpointRequire(std::is_sorted(slot.replacements.begin(), slot.replacements.end()), entry,
                              "Unsorted replacement queue");
            if (!entry.at("pending").value.isNull()) {
                auto pending = entry.at("pending");
                pending.fields({"operation", "ordinal", "units"});
                BatchRequest request;
                request.source  = {RuntimeHandle<SpawnerTag>(static_cast<std::uint32_t>(index), slot.generation),
                                   world.owner, world.epoch};
                request.ordinal = OperationOrdinal(checkpointUnsigned(pending.at("ordinal")));
                checkpointRequire(!request.ordinal.isZero() && request.ordinal.value() == slot.operation, pending,
                                  "Pending ordinal mismatch");
                request.operation = checkpointId(pending.at("operation"));
                checkpointRequire(request.operation == detail::spawnIdentity(world.owner, world.epoch, id,
                                                                             request.ordinal.value(), std::nullopt),
                                  pending, "Pending operation identity mismatch");
                for (const auto& value : checkpointArray(pending.at("units"), world.limits.unitsPerTick)) {
                    CheckpointReader member{value, pending.path + ".units"};
                    member.fields({"subject", "definition"});
                    auto subject = checkpointSubject(member.at("subject"));
                    checkpointRequire(
                        subject.persistentId() ==
                                detail::spawnIdentity(world.owner, world.epoch, id, request.ordinal.value(),
                                                      static_cast<std::uint32_t>(request.units.size())) &&
                            !world.subjects.contains(subject.persistentId()) &&
                            pendingIds.insert(subject.persistentId()).second,
                        member, "Pending member identity conflict");
                    auto reference = checkpointResult(DefinitionRef::parse(member.at("definition").text()), member);
                    const std::vector<Member>* members = nullptr;
                    if (waves && slot.wave < waves->waves.size())
                        members = &waves->waves[slot.wave].members;
                    else if (auto* fixed = std::get_if<FixedContent>(&definition.content))
                        members = &fixed->members;
                    else if (auto* weighted = std::get_if<WeightedContent>(&definition.content))
                        members = &weighted->entries;
                    checkpointRequire(members && std::any_of(members->begin(), members->end(),
                                                             [&](const auto& m) { return m.unit == reference; }),
                                      member, "Pending unit not in pinned content");
                    request.units.push_back({subject, std::move(reference)});
                }
                checkpointRequire(
                    !request.units.empty() && request.units.size() <= definition.limits.alive - slot.alive, pending,
                    "Invalid pending batch size");
                std::uint64_t              expected     = 0;
                const std::vector<Member>* fixedMembers = nullptr;
                if (waves && slot.wave < waves->waves.size())
                    fixedMembers = &waves->waves[slot.wave].members;
                else if (std::holds_alternative<OnceSchedule>(definition.schedule))
                    fixedMembers = &std::get<FixedContent>(definition.content).members;
                if (fixedMembers) {
                    for (const auto& member : *fixedMembers) {
                        for (std::uint32_t n = 0; n < member.count; ++n) {
                            checkpointRequire(
                                expected < request.units.size() && request.units[expected].definition == member.unit,
                                pending, "Fixed pending content order/count mismatch");
                            ++expected;
                        }
                    }
                } else if (const auto* interval = std::get_if<IntervalSchedule>(&definition.schedule))
                    expected = interval->count;
                else if (population) {
                    expected = slot.initialDone ? 1 : population->initial;
                    if (slot.initialDone)
                        checkpointRequire(!slot.replacements.empty() && slot.replacements.front() <= world.age(slot),
                                          pending, "Pending replacement has no due entry");
                }
                checkpointRequire(request.units.size() == expected && expected <= UINT64_MAX - slot.total &&
                                      (!definition.limits.total || expected <= *definition.limits.total - slot.total),
                                  pending, "Pending batch violates schedule or quota");
                slot.pending = std::move(request);
            }
            if (waves) {
                checkpointRequire(!slot.wavePublished || slot.wave < waves->waves.size(), entry,
                                  "Published wave cursor is invalid");
                std::uint64_t published = 0;
                for (std::size_t n = 0; n < slot.wave; ++n) published += groupSize(waves->waves[n].members);
                if (slot.wavePublished) published += groupSize(waves->waves[slot.wave].members);
                checkpointRequire(slot.total == published, entry, "Wave publication total mismatch");
            } else {
                checkpointRequire(!slot.wavePublished, entry, "Non-wave source has a wave publication");
                if (const auto* interval = std::get_if<IntervalSchedule>(&definition.schedule))
                    checkpointRequire(slot.total % interval->count == 0, entry, "Interval publication total mismatch");
                if (std::holds_alternative<OnceSchedule>(definition.schedule)) {
                    const auto count = groupSize(std::get<FixedContent>(definition.content).members);
                    checkpointRequire(slot.total == 0 || slot.total == count, entry, "Once publication total mismatch");
                }
            }
            checkpointRequire(slot.occupied || (slot.alive == 0 && slot.replacements.empty() && !slot.pending), entry,
                              "Retired slot owns live work");
            if (slot.phase == Phase::Running) checkpointRequire(slot.active, entry, "Running source is inactive");
            if (slot.phase == Phase::Dormant) checkpointRequire(!slot.active, entry, "Dormant source is active");
            if (slot.phase == Phase::Completed)
                checkpointRequire(!slot.pending, entry, "Completed source has pending creation");
            const auto maximum = maximumBatch(definition);
            checkpointRequire(
                maximum <= world.limits.unitsPerTick &&
                    std::uint64_t(maximum) * definition.placement.attempts <= world.limits.candidatesPerTick,
                entry, "Pinned batch exceeds work quota");
            sums[budget] += slot.alive;
            if (slot.occupied)
                state->placement.push_back(
                    {{world.owner, world.epoch, static_cast<std::uint32_t>(index), slot.generation},
                     std::make_shared<const CompiledMap>(std::move(compiled)),
                     0});
            world.slots.push_back(std::move(slot));
        }
        for (std::size_t i = 0; i < sums.size(); ++i)
            checkpointRequire(sums[i] == world.budgetAlive[i], root.at("budgets"), "Shared budget sum mismatch");
        for (auto id : pendingIds)
            checkpointRequire(!world.subjects.contains(id), root, "Pending/tracked identity collision");
        world.cursor =
            static_cast<std::size_t>(checkpointUnsigned(root.at("cursor"), slots.empty() ? 0 : slots.size() - 1));
        auto sourceHandle = [&](const CheckpointReader& reader) {
            reader.fields({"owner", "epoch", "slot", "generation"});
            checkpointRequire(!world.slots.empty(), reader, "No source slot for queued reference");
            auto owner      = checkpointId(reader.at("owner"));
            auto epoch      = checkpointUnsigned(reader.at("epoch"));
            auto index      = checkpointUnsigned(reader.at("slot"), world.slots.size() - 1);
            auto generation = checkpointUnsigned(reader.at("generation"), UINT32_MAX);
            checkpointRequire(owner == world.owner && epoch == world.epoch &&
                                  generation == world.slots[index].generation && world.slots[index].occupied,
                              reader, "Stale queued source reference");
            return world.handle(index);
        };
        for (const auto& value : checkpointArray(root.at("commands"), world.limits.queuedCommands)) {
            CheckpointReader command{value, "$.commands"};
            command.fields({"source", "kind"});
            world.commands.push_back({sourceHandle(command.at("source")),
                                      checkpointEnum(command.at("kind"), ControlKind::RetryFault, checkpointControl)});
        }
        for (const auto& value : checkpointArray(root.at("events"), world.limits.queuedCommands)) {
            CheckpointReader event{value, "$.events"};
            event.fields({"sequence", "tick", "source", "kind", "reason", "operation", "wave", "subjects"});
            SpawnEvent result;
            result.sequence = EventSequence(checkpointUnsigned(event.at("sequence")));
            checkpointRequire(result.sequence.value() == 0, event, "Pending event already has a published sequence");
            result.tick = SimulationTick(checkpointUnsigned(event.at("tick")));
            checkpointRequire(result.tick.value() == world.tick, event, "Future event tick");
            result.source    = sourceHandle(event.at("source"));
            result.kind      = checkpointEnum(event.at("kind"), SpawnEventKind::RetryRequested, checkpointEvent);
            result.reason    = checkpointEnum(event.at("reason"), BlockReason::ArithmeticOverflow, checkpointBlock);
            result.operation = checkpointId(event.at("operation"), true);
            result.wave      = checkpointSize(event.at("wave"));
            for (const auto& subject : checkpointArray(event.at("subjects"), world.limits.unitsPerTick))
                result.subjects.push_back(checkpointSubject({subject, "$.events.subjects"}));
            const auto& eventSlot = world.slots[result.source.slot.index()];
            checkpointRequire(
                (result.kind == SpawnEventKind::Activated || result.kind == SpawnEventKind::Paused) &&
                    result.wave == eventSlot.wave && result.reason == eventSlot.blocked && result.subjects.empty() &&
                    result.operation == (eventSlot.pending ? eventSlot.pending->operation : PersistentId{}),
                event, "Queued event is inconsistent with source state");
            world.pendingEvents.push_back(std::move(result));
        }
        checkpointRequire(world.pendingEvents.size() <= UINT64_MAX - world.outputSequence, root,
                          "Event sequence overflow");
        state->membership.reserve(world.subjects.size());
        for (const auto& [id, location] : world.subjects) {
            const auto& slot = world.slots[location.first];
            state->membership.push_back(
                {SubjectRef::fromPersistentId(id),
                 {world.owner, world.epoch, static_cast<std::uint32_t>(location.first), slot.generation}});
        }
        state->sources.reserve(world.slots.size());
        for (std::size_t index = 0; index < world.slots.size(); ++index) {
            const auto& slot = world.slots[index];
            if (slot.occupied)
                state->sources.push_back(
                    {world.owner, world.epoch, static_cast<std::uint32_t>(index), slot.generation});
        }
        world.attached = true;
        return Result<SpawnRestoreState>::success(SpawnRestoreState(std::move(state)));
    } catch (const detail::InvalidInput& invalid) {
        return Result<SpawnRestoreState>::failure(invalid.diagnostic);
    }
}
Result<SpawnCheckpoint> SpawnCheckpoint::parse(SnapshotEnvelope snapshot, PersistentId saveEpoch,
                                               const SnapshotHashProvider&    hashProvider,
                                               const IUnitDefinitionResolver& units) {
    struct References final : IUnitDefinitionResolver {
        const IUnitDefinitionResolver&               resolver;
        mutable std::map<std::string, DefinitionRef> requested;
        explicit References(const IUnitDefinitionResolver& value) : resolver(value) {}
        Result<void> validate(const DefinitionRef& reference) const override {
            auto valid = resolver.validate(reference);
            if (!valid) return Result<void>::failure(valid.status());
            requested.emplace(reference.format(), reference);
            return Result<void>::success();
        }
    } references(units);
    auto admitted = SpawnRestoreState::decode(snapshot, saveEpoch, hashProvider, references);
    if (!admitted) return Result<SpawnCheckpoint>::failure(admitted.status());
    auto state      = std::make_unique<Impl>();
    state->owner    = snapshot.instanceId;
    state->revision = admitted.value().revision();
    state->tick     = admitted.value().tick();
    state->payload  = std::move(snapshot.payload);
    state->membership.reserve(admitted.value().membership().size());
    for (const auto& member : admitted.value().membership()) {
        const auto& source = member.source;
        state->membership.push_back(
            {member.subject,
             {RuntimeHandle<SpawnerTag>(source.slot, source.generation), source.owner, source.epoch, {}}});
    }
    state->units.reserve(references.requested.size());
    for (auto& [key, reference] : references.requested) state->units.push_back(std::move(reference));
    return Result<SpawnCheckpoint>::success(SpawnCheckpoint(std::move(state)));
}
}  // namespace eve::spawn
