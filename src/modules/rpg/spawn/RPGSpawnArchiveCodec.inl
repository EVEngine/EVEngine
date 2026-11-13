#pragma once
#include <charconv>
#include <limits>
#include <tuple>
#include "rpg/EffectDefinitionCodec.h"
#include "rpg/ImplementationRequirement.h"
#include "rpg/spawn/RPGSpawnArchive.h"
namespace eve::rpg_spawn {
namespace {
struct ArchiveError {
    Diagnostic diagnostic;
};
[[noreturn]] void archiveReject(std::string message, DiagnosticCode code = DiagnosticCode::InvalidArgument) {
    throw ArchiveError{Diagnostic::error(code, std::move(message), "rpg.spawn.archive")};
}
struct ArchiveReader {
    const Value& value;
    void         fields(std::initializer_list<std::string_view> allowed) const {
        if (!value.isObject()) archiveReject("Expected archive object");
        for (const auto& key : value.keys())
            if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
                archiveReject("Unknown archive field: " + key);
    }
    ArchiveReader at(const std::string& key) const {
        const auto* field = value.find(key);
        if (!field) archiveReject("Missing archive field: " + key);
        return {*field};
    }
    std::string text() const {
        if (!value.isString() || value.asString().empty()) archiveReject("Expected nonempty archive string");
        return value.asString();
    }
    std::uint64_t integer(std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max()) const {
        auto          source = text();
        std::uint64_t result = 0;
        auto          parsed = std::from_chars(source.data(), source.data() + source.size(), result);
        if (parsed.ec != std::errc{} || parsed.ptr != source.data() + source.size() || result > maximum ||
            source != std::to_string(result))
            archiveReject("Expected canonical bounded unsigned decimal");
        return result;
    }
    PersistentId identity() const {
        auto source = text();
        auto result = PersistentId::parse(source);
        if (!result || result->isNil() || result->format() != source)
            archiveReject("Invalid canonical persistent identity");
        return *result;
    }
    const Value::Array& array() const {
        const auto* result = value.getIf<Value::Array>();
        if (!result || result->size() > 1000000) archiveReject("Expected bounded archive array");
        return *result;
    }
};
std::string_view archiveImplementationKind(rpg::ImplementationKind kind) {
    switch (kind) {
        case rpg::ImplementationKind::ConditionPolicy: return "condition_policy";
        case rpg::ImplementationKind::EffectStackPolicy: return "effect_stack_policy";
        case rpg::ImplementationKind::AttributeOperation: return "attribute_operation";
    }
    archiveReject("Unknown implementation kind");
}
rpg::ImplementationKind archiveImplementationKind(std::string_view name) {
    for (auto kind : {rpg::ImplementationKind::ConditionPolicy, rpg::ImplementationKind::EffectStackPolicy,
                      rpg::ImplementationKind::AttributeOperation})
        if (archiveImplementationKind(kind) == name) return kind;
    archiveReject("Unknown implementation kind");
}
LogicalId archiveSchema() { return *LogicalId::fromParts("rpg.spawn", "archive"); }
Value     archiveNumber(std::uint64_t number) { return Value(std::to_string(number)); }
Value     archiveRecord(const definitions::Definition& record) {
    return Value::object({{"type", record.type},
                              {"id", record.id},
                              {"version", static_cast<std::int64_t>(record.version.value())},
                              {"generation", archiveNumber(record.generation.value())},
                              {"json", record.json}});
}
using ArchiveKey = std::tuple<std::string, std::string, std::uint64_t>;
ArchiveKey archiveKey(const definitions::Definition& record) {
    return {record.type, record.id, record.generation.value()};
}
definitions::Definition archiveRecord(const ArchiveReader& reader, std::string_view type) {
    reader.fields({"type", "id", "version", "generation", "json"});
    definitions::Definition record;
    record.type = reader.at("type").text();
    record.id   = reader.at("id").text();
    if (record.type != type || !LogicalId::fromParts(record.type, record.id))
        archiveReject("Invalid definition record key");
    const auto& version = reader.at("version").value;
    if (!version.isInt64() || version.asInt() != 1)
        archiveReject("Unsupported archived definition version", DiagnosticCode::UnknownVersion);
    record.generation = Generation(reader.at("generation").integer());
    if (record.generation.isZero()) archiveReject("Archived definition generation is zero");
    record.json = reader.at("json").text();
    auto parsed = Value::fromJson(record.json);
    if (!parsed || !parsed.value().isObject()) archiveReject("Definition JSON must be an object");
    const auto* id = parsed.value().find("id");
    if ((type == "rpg.unit" && !id) || (id && (!id->isString() || id->asString() != record.id)))
        archiveReject("Definition payload id differs from archive key", DiagnosticCode::Conflict);
    if (type == "rpg.unit") {
        const auto* schema      = parsed.value().find("schema");
        const auto* bodyVersion = parsed.value().find("version");
        if (!schema || !schema->isString() || schema->asString() != "eve.rpg.unit" || !bodyVersion ||
            !bodyVersion->isInt64() || bodyVersion->asInt() != 1)
            archiveReject("Unsupported unit payload header", DiagnosticCode::UnknownVersion);
    } else {
        auto typed = rpg::SkillDefinitionRuntime::prepareCaptured(record);
        if (!typed) throw ArchiveError{*typed.status().primaryDiagnostic()};
    }
    return record;
}
Value archiveOrigin(const std::optional<spawn::SpawnerHandle>& source) {
    if (!source) return Value{};
    return Value::object({{"owner", source->owner.format()},
                          {"epoch", archiveNumber(source->epoch)},
                          {"slot", archiveNumber(source->slot.index())},
                          {"generation", archiveNumber(source->slot.generation())}});
}
std::optional<ArchivedSpawnOrigin> archiveOrigin(const ArchiveReader& reader) {
    if (reader.value.isNull()) return {};
    reader.fields({"owner", "epoch", "slot", "generation"});
    auto owner      = reader.at("owner").identity();
    auto epoch      = reader.at("epoch").integer();
    auto slot       = reader.at("slot").integer(std::numeric_limits<std::uint32_t>::max() - 1);
    auto generation = reader.at("generation").integer(std::numeric_limits<std::uint32_t>::max());
    if (!epoch || !generation) archiveReject("Invalid durable origin");
    return ArchivedSpawnOrigin{owner, epoch, static_cast<std::uint32_t>(slot), static_cast<std::uint32_t>(generation)};
}
template <class F>
auto archiveCallback(F&& callback) -> decltype(callback()) {
    using R = decltype(callback());
    try {
        return callback();
    } catch (const std::exception& error) {
        return R::failure(Diagnostic::error(DiagnosticCode::CallbackFailure, error.what()));
    } catch (...) {
        return R::failure(Diagnostic::error(DiagnosticCode::CallbackFailure, "Archive digest provider threw"));
    }
}
}  // namespace
Result<SnapshotEnvelope> RPGSpawnCapture::seal(Revision revision, SimulationTick tick,
                                               const SnapshotHashProvider& hash) const {
    using R = Result<SnapshotEnvelope>;
    if (!impl_ || impl_->sealing || revision.isZero())
        return R::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Expected idle capture and nonzero revision"));
    if (!hash) return R::failure(Diagnostic::error(DiagnosticCode::Unsupported, "Archive digest provider required"));
    impl_->sealing = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{impl_->sealing};
    std::map<ArchiveKey, definitions::Definition> records;
    for (const auto& actor : impl_->actors) {
        const auto&             recipe = *actor.recipe;
        definitions::Definition record{std::string(recipe.definition.reference.id().namespaceName()),
                                       std::string(recipe.definition.reference.id().name()), SchemaVersion(1),
                                       recipe.definition.generation, recipe.canonicalJson};
        auto [found, inserted] = records.emplace(archiveKey(record), record);
        if (!inserted && found->second.json != record.json)
            return R::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "One unit revision has conflicting source bytes"));
    }
    for (const auto& record : impl_->future) {
        auto [found, inserted] = records.emplace(archiveKey(record), record);
        if (!inserted && found->second.json != record.json)
            return R::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Future and actor unit revision sources disagree"));
    }
    Value::Array units, skills, actors, effects, implementations, future;

    std::map<ArchiveKey, std::size_t> indices;
    try {
        for (const auto& [key, record] : records) {
            auto encoded = archiveRecord(record);
            auto checked = archiveRecord(ArchiveReader{encoded}, "rpg.unit");
            indices.emplace(key, units.size());
            units.push_back(archiveRecord(checked));
        }
    } catch (const ArchiveError& error) {
        return R::failure(error.diagnostic);
    }
    for (const auto& record : impl_->future) future.push_back(archiveNumber(indices.at(archiveKey(record))));
    for (const auto& record : impl_->skills) skills.push_back(archiveRecord(record));
    for (const auto& effect : impl_->effects) {
        auto encoded = rpg::encodeEffectDefinition(effect);
        if (!encoded) return R::failure(encoded.status());
        effects.push_back(std::move(encoded).takeValue());
    }
    for (const auto& identity : impl_->implementations)
        implementations.push_back(Value::object({{"kind", std::string(archiveImplementationKind(identity.kind))},
                                                 {"name", identity.name},
                                                 {"revision", identity.revision}}));
    for (const auto& actor : impl_->actors) {
        const auto&      definition = actor.recipe->definition;
        const ArchiveKey key{std::string(definition.reference.id().namespaceName()),
                             std::string(definition.reference.id().name()), definition.generation.value()};
        actors.push_back(Value::object({{"subject", actor.subject.persistentId().format()},
                                        {"origin", archiveOrigin(actor.origin)},
                                        {"position", Value::array({actor.position.x, actor.position.y})},
                                        {"unit_revision", archiveNumber(indices.at(key))},
                                        {"checkpoint", actor.checkpointJson}}));
    }
    auto payload = Value::object({{"units", Value(std::move(units))},
                                  {"skills", Value(std::move(skills))},
                                  {"effects", Value(std::move(effects))},
                                  {"implementations", Value(std::move(implementations))},
                                  {"future_units", Value(std::move(future))},
                                  {"actors", Value(std::move(actors))}});
    return archiveCallback([&] {
        return makeSnapshotEnvelope("eve.rpg-spawn-archive", archiveSchema(), SchemaVersion(4), impl_->saveEpoch,
                                    revision, tick, std::move(payload), hash);
    });
}
struct RPGSpawnArchive::Impl {
    PersistentId                             epoch;
    Revision                                 revision;
    SimulationTick                           tick;
    std::vector<ArchivedSpawnActor>          actors;
    std::vector<definitions::Definition>     units, skills;
    std::vector<rpg::EffectDefinition>       effects;
    std::vector<rpg::ImplementationIdentity> implementations;
    std::vector<std::size_t>                 future;
};
RPGSpawnArchive::RPGSpawnArchive(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RPGSpawnArchive::~RPGSpawnArchive()                                     = default;
RPGSpawnArchive::RPGSpawnArchive(RPGSpawnArchive&&) noexcept            = default;
RPGSpawnArchive& RPGSpawnArchive::operator=(RPGSpawnArchive&&) noexcept = default;
PersistentId     RPGSpawnArchive::saveEpoch() const noexcept { return impl_ ? impl_->epoch : PersistentId{}; }
Revision         RPGSpawnArchive::revision() const noexcept { return impl_ ? impl_->revision : Revision{}; }
SimulationTick   RPGSpawnArchive::tick() const noexcept { return impl_ ? impl_->tick : SimulationTick{}; }
std::span<const ArchivedSpawnActor> RPGSpawnArchive::actors() const noexcept {
    return impl_ ? std::span<const ArchivedSpawnActor>(impl_->actors) : std::span<const ArchivedSpawnActor>{};
}
std::span<const definitions::Definition> RPGSpawnArchive::units() const noexcept {
    return impl_ ? std::span<const definitions::Definition>(impl_->units) : std::span<const definitions::Definition>{};
}
std::span<const std::size_t> RPGSpawnArchive::futureUnitRevisions() const noexcept {
    return impl_ ? std::span<const std::size_t>(impl_->future) : std::span<const std::size_t>{};
}
std::span<const definitions::Definition> RPGSpawnArchive::skills() const noexcept {
    return impl_ ? std::span<const definitions::Definition>(impl_->skills) : std::span<const definitions::Definition>{};
}
std::span<const rpg::EffectDefinition> RPGSpawnArchive::effects() const noexcept {
    return impl_ ? std::span<const rpg::EffectDefinition>(impl_->effects) : std::span<const rpg::EffectDefinition>{};
}
std::span<const rpg::ImplementationIdentity> RPGSpawnArchive::implementations() const noexcept {
    return impl_ ? std::span<const rpg::ImplementationIdentity>(impl_->implementations)
                 : std::span<const rpg::ImplementationIdentity>{};
}
Result<RPGSpawnArchive> RPGSpawnArchive::parse(const SnapshotEnvelope& input, PersistentId epoch,
                                               const SnapshotHashProvider& hash) {
    using R       = Result<RPGSpawnArchive>;
    auto snapshot = input;
    auto verified = archiveCallback([&] { return verifySnapshotEnvelope(snapshot, hash); });
    if (!verified) return R::failure(verified.status());
    if (snapshot.type != "eve.rpg-spawn-archive" || snapshot.schema != archiveSchema() ||
        (snapshot.schemaVersion.value() < 1 || snapshot.schemaVersion.value() > 4))
        return R::failure(
            Diagnostic::error(DiagnosticCode::UnknownVersion, "Unsupported RPG spawn archive schema/version"));
    if (epoch.isNil() || snapshot.instanceId != epoch || snapshot.revision.isZero())
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Archive joint identity/revision mismatch"));
    try {
        ArchiveReader root{snapshot.payload};
        if (snapshot.schemaVersion.value() == 1)
            root.fields({"units", "skills", "actors"});
        else if (snapshot.schemaVersion.value() == 2)
            root.fields({"units", "skills", "effects", "actors"});
        else if (snapshot.schemaVersion.value() == 3)
            root.fields({"units", "skills", "effects", "implementations", "actors"});
        else
            root.fields({"units", "skills", "effects", "implementations", "future_units", "actors"});
        auto state      = std::make_unique<Impl>();
        state->epoch    = epoch;
        state->revision = snapshot.revision;
        state->tick     = snapshot.tick;
        for (const auto& [name, type] : {std::pair{"units", "rpg.unit"}, std::pair{"skills", "rpg.skill"}}) {
            auto&                 target = std::string_view(name) == "units" ? state->units : state->skills;
            std::set<ArchiveKey>  unique;
            std::set<std::string> skillIds;
            for (const auto& value : root.at(name).array()) {
                auto record = archiveRecord(ArchiveReader{value}, type);
                if (!unique.insert(archiveKey(record)).second ||
                    (std::string_view(name) == "skills" && !skillIds.insert(record.id).second))
                    archiveReject("Duplicate archived revision/key", DiagnosticCode::Conflict);
                target.push_back(std::move(record));
            }
        }
        if (snapshot.schemaVersion.value() >= 2) {
            std::set<std::string> ids;
            for (const auto& source : root.at("effects").array()) {
                auto effect = rpg::decodeEffectDefinition(source);
                if (!effect) return R::failure(effect.status());
                if (!ids.insert(effect.value().id).second)
                    archiveReject("Duplicate archived effect", DiagnosticCode::Conflict);
                state->effects.push_back(std::move(effect).takeValue());
            }
        }
        if (snapshot.schemaVersion.value() >= 3) {
            std::set<std::pair<rpg::ImplementationKind, std::string>> unique;
            for (const auto& source : root.at("implementations").array()) {
                ArchiveReader reader{source};
                reader.fields({"kind", "name", "revision"});
                rpg::ImplementationIdentity identity{archiveImplementationKind(reader.at("kind").text()),
                                                     reader.at("name").text(), reader.at("revision").text()};
                if (!unique.emplace(identity.kind, identity.name).second)
                    archiveReject("Duplicate implementation identity", DiagnosticCode::Conflict);
                state->implementations.push_back(std::move(identity));
            }
            std::sort(state->implementations.begin(), state->implementations.end(), [](const auto& a, const auto& b) {
                return std::pair{a.kind, a.name} < std::pair{b.kind, b.name};
            });
        }
        if (snapshot.schemaVersion.value() >= 4) {
            std::set<std::string> unique;
            for (const auto& value : root.at("future_units").array()) {
                auto index =
                    static_cast<std::size_t>(ArchiveReader{value}.integer(std::numeric_limits<std::size_t>::max()));
                if (index >= state->units.size())
                    archiveReject("Future unit index is absent", DiagnosticCode::NotFound);
                if (!unique.insert(state->units[index].id).second)
                    archiveReject("Duplicate logical future unit binding", DiagnosticCode::Conflict);
                state->future.push_back(index);
            }
        }
        std::set<PersistentId> subjects;
        for (const auto& value : root.at("actors").array()) {
            ArchiveReader reader{value};
            reader.fields({"subject", "origin", "position", "unit_revision", "checkpoint"});
            ArchivedSpawnActor actor;
            auto               subject = reader.at("subject").identity();
            if (!subjects.insert(subject).second) archiveReject("Duplicate actor identity", DiagnosticCode::Conflict);
            actor.subject        = SubjectRef::fromPersistentId(subject);
            actor.origin         = archiveOrigin(reader.at("origin"));
            const auto& position = reader.at("position").array();
            if (position.size() != 2) archiveReject("Expected World2D point");
            auto number = [](const Value& coordinate) {
                if (!coordinate.isNumeric()) archiveReject("Coordinate must be numeric");
                auto result = coordinate.isInt64() ? double(coordinate.asInt()) : coordinate.asDouble();
                if (!std::isfinite(result)) archiveReject("Coordinate must be finite");
                return result;
            };
            actor.position = {number(position[0]), number(position[1])};
            actor.unitRevision =
                static_cast<std::size_t>(reader.at("unit_revision").integer(std::numeric_limits<std::size_t>::max()));
            if (actor.unitRevision >= state->units.size())
                archiveReject("Missing actor unit revision", DiagnosticCode::NotFound);
            actor.checkpointJson = reader.at("checkpoint").text();
            auto checkpoint      = Value::fromJson(actor.checkpointJson);
            if (!checkpoint || !checkpoint.value().isObject()) archiveReject("Actor checkpoint must be JSON object");
            const auto* schema  = checkpoint.value().find("schema");
            const auto* version = checkpoint.value().find("version");
            if (!schema || !schema->isString() || schema->asString() != "eve.rpg.actor-checkpoint" || !version ||
                !version->isInt64() || (version->asInt() < 1 || version->asInt() > 5))
                archiveReject("Unsupported actor checkpoint header", DiagnosticCode::UnknownVersion);
            state->actors.push_back(std::move(actor));
        }
        return R::success(RPGSpawnArchive(std::move(state)));
    } catch (const ArchiveError& error) {
        return R::failure(error.diagnostic);
    }
}
}  // namespace eve::rpg_spawn
