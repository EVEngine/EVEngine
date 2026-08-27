#include "definitions/Definitions.h"

#include "common/SquirrelBinding.h"
#include "schema/SchemaRegistry.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <charconv>
#include <cstdint>
#include <exception>
#include <limits>
#include <utility>

namespace eve::definitions {
namespace {

eve::schema::FieldDefinition schemaField(std::string name, eve::schema::ValueType type, bool required = true) {
    eve::schema::FieldDefinition result;
    result.name     = std::move(name);
    result.type     = type;
    result.required = required;
    return result;
}

eve::schema::SchemaNode arrayOfObject(std::vector<eve::schema::FieldDefinition> fields) {
    eve::schema::SchemaDefinition object;
    object.additionalProperties = false;
    object.fields               = std::move(fields);
    eve::schema::SchemaNode item;
    item.type         = eve::schema::ValueType::Object;
    item.objectSchema = std::make_shared<const eve::schema::SchemaDefinition>(std::move(object));
    eve::schema::SchemaNode array;
    array.type       = eve::schema::ValueType::Array;
    array.itemSchema = std::make_shared<const eve::schema::SchemaNode>(std::move(item));
    return array;
}

eve::schema::SchemaDefinition definitionsSnapshotSchema() {
    eve::schema::SchemaDefinition schema;
    schema.id                                          = "definitions:registry";
    schema.version                                     = 1;
    schema.title                                       = "Definition Registry Snapshot";
    schema.description                                 = "Persistent state owned by DefinitionRegistry.";
    schema.additionalProperties                        = false;
    auto definitions                                   = schemaField("definitions", eve::schema::ValueType::Array);
    static_cast<eve::schema::SchemaNode&>(definitions) = arrayOfObject({
        schemaField("type", eve::schema::ValueType::String),
        schemaField("id", eve::schema::ValueType::String),
        schemaField("version", eve::schema::ValueType::Integer),
        schemaField("generation", eve::schema::ValueType::String),
        schemaField("json", eve::schema::ValueType::Any),
    });
    definitions.name                                   = "definitions";
    definitions.required                               = true;
    auto generations                                   = schemaField("generations", eve::schema::ValueType::Array);
    static_cast<eve::schema::SchemaNode&>(generations) = arrayOfObject({
        schemaField("type", eve::schema::ValueType::String),
        schemaField("id", eve::schema::ValueType::String),
        schemaField("generation", eve::schema::ValueType::String),
    });
    generations.name                                   = "generations";
    generations.required                               = true;
    auto events                                        = schemaField("events", eve::schema::ValueType::Array);
    static_cast<eve::schema::SchemaNode&>(events)      = arrayOfObject({
        schemaField("sequence", eve::schema::ValueType::String),
        schemaField("name", eve::schema::ValueType::String),
        schemaField("type", eve::schema::ValueType::String),
        schemaField("id", eve::schema::ValueType::String),
        schemaField("version", eve::schema::ValueType::Integer),
        schemaField("generation", eve::schema::ValueType::String),
    });
    events.name                                        = "events";
    events.required                                    = true;
    schema.fields                                      = {
        schemaField("version", eve::schema::ValueType::Integer),
        std::move(definitions),
        std::move(generations),
        std::move(events),
        schemaField("nextEventSequence", eve::schema::ValueType::String),
    };
    return schema;
}

eve::Result<void> validateDefinitionsSnapshot(const std::string& json) {
    constexpr auto id = "definitions:registry";
    if (!eve::schema::SchemaRegistry::resolve(id, 1)) {
        auto registration = eve::schema::SchemaRegistry::registerVersioned(definitionsSnapshotSchema());
        if (!registration.ok()) return eve::Result<void>::failure(registration.status());
    }
    const auto errors = eve::schema::SchemaRegistry::validate(id, 1, json);
    if (errors.empty()) return eve::Result<void>::success();
    const auto& error = errors.front();
    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::ParseError, error.message, error.path,
        eve::DiagnosticDetails{{"schemaId", id}, {"schemaVersion", "1"}, {"validationCode", error.code}},
        "definitions.snapshot.schema"));
}

template <class T>
eve::Result<T> definitionsBindingFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "definitions.squirrel"));
}

eve::Value definitionHandleProjection(const DefinitionHandle& handle) {
    const auto& logical = handle.reference.id();
    return eve::Value(eve::Value::Object{
        {"type", eve::Value(std::string(logical.namespaceName()))},
        {"id", eve::Value(std::string(logical.name()))},
        {"reference", eve::Value(handle.reference.format())},
        {"generation", eve::Value(static_cast<std::int64_t>(handle.generation.value()))},
    });
}

eve::Value definitionProjection(const Definition& definition) {
    return eve::Value(eve::Value::Object{
        {"type", eve::Value(definition.type)},
        {"id", eve::Value(definition.id)},
        {"version", eve::Value(static_cast<std::int64_t>(definition.version.value()))},
        {"generation", eve::Value(static_cast<std::int64_t>(definition.generation.value()))},
        {"json", eve::Value(definition.json)},
    });
}

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message)));
}

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto it = object.find(std::string(name));
    return it == object.end() ? nullptr : &it->second;
}

bool readString(const eve::Value::Object& object, std::string_view name, std::string& output) {
    const auto* value = field(object, name);
    const auto* text  = value ? value->getIf<std::string>() : nullptr;
    if (!text) return false;
    output = *text;
    return true;
}

bool readPositiveSchemaVersion(const eve::Value& value, eve::SchemaVersion& output) {
    const auto* number = value.getIf<std::int64_t>();
    if (!number || *number <= 0) return false;
    output = eve::SchemaVersion(static_cast<std::uint64_t>(*number));
    return true;
}

bool readUint64String(const eve::Value& value, std::uint64_t& output) {
    const auto* text = value.getIf<std::string>();
    if (!text || text->empty()) return false;
    const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), output);
    return error == std::errc{} && end == text->data() + text->size();
}

eve::Value parseStoredJson(std::string_view text) {
    auto parsed = eve::Value::fromJson(text);
    if (!parsed.ok()) {
        // A Definition can enter the registry only after this exact parse
        // succeeds. Reaching this point means the registry's invariant was
        // violated, not that a caller supplied recoverable input.
        std::terminate();
    }
    return std::move(parsed).takeValue();
}

eve::LogicalId definitionsSchema() {
    const auto schema = eve::LogicalId::parse("definitions:registry");
    if (!schema) std::terminate();
    return *schema;
}

DefinitionRef makeDefinitionReference(std::string_view type, std::string_view id) {
    const auto logical = eve::LogicalId::fromParts(type, id);
    if (!logical) return {};
    auto reference = eve::DefinitionRef::fromId(*logical);
    if (!reference.ok()) return {};
    return std::move(reference).takeValue();
}

const eve::SnapshotMigrationChain& definitionsMigrations() {
    static const eve::SnapshotMigrationChain chain = [] {
        eve::SnapshotMigrationChain result;
        const auto                  registration =
            result.add(definitionsSchema(), eve::SchemaVersion(0), eve::SchemaVersion(1),
                       [](const eve::Value& payload) -> eve::Result<eve::Value> {
                           const auto* object = payload.getIf<eve::Value::Object>();
                           if (!object)
                               return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                                   eve::DiagnosticCode::ParseError, "definitions snapshot payload must be an object"));
                           eve::Value::Object migrated = *object;
                           migrated["version"]         = eve::Value(std::int64_t(1));
                           return eve::Result<eve::Value>::success(eve::Value(std::move(migrated)));
                       });
        if (!registration.ok()) std::terminate();
        return result;
    }();
    return chain;
}

eve::Result<Definition> makeDefinition(const std::string& type, const std::string& id, int version,
                                       const std::string& json) {
    if (type.empty() || id.empty())
        return failure<Definition>(eve::DiagnosticCode::InvalidArgument, "definition type and id must not be empty");
    if (!eve::LogicalId::fromParts(type, id))
        return failure<Definition>(eve::DiagnosticCode::InvalidArgument,
                                   "definition type and id must form a valid namespace:name reference");
    if (version <= 0)
        return failure<Definition>(eve::DiagnosticCode::InvalidArgument, "definition schema version must be positive");

    auto parsed = eve::Value::fromJson(json);
    if (!parsed.ok()) return eve::Result<Definition>::failure(parsed.status());
    auto value     = std::move(parsed).takeValue();
    auto canonical = value.toJson();
    if (!canonical.ok()) return eve::Result<Definition>::failure(canonical.status());

    if (eve::schema::SchemaRegistry::resolve(type, version)) {
        const auto errors = eve::schema::SchemaRegistry::validate(type, version, json);
        if (!errors.empty()) {
            const auto& error = errors.front();
            return eve::Result<Definition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, error.message, error.path,
                eve::DiagnosticDetails{
                    {"schemaId", type}, {"schemaVersion", std::to_string(version)}, {"validationCode", error.code}},
                "definitions.schema"));
        }
    }

    Definition definition;
    definition.type    = type;
    definition.id      = id;
    definition.version = eve::SchemaVersion(static_cast<std::uint64_t>(version));
    definition.json    = std::move(canonical).takeValue();
    return eve::Result<Definition>::success(std::move(definition));
}

}  // namespace

DefinitionRegistry::DefinitionRegistry(eve::PersistentId instanceId)
    : storage_([](Definition& value, eve::Generation generation) { value.generation = generation; }),
      instanceId_(instanceId) {}

DefinitionEvent DefinitionRegistry::projectEvent(const Storage::Event& event) {
    DefinitionEvent result;
    result.sequence = event.sequence;
    result.name     = event.operation == eve::RegistryOperation::Remove ? "definition_removed" : "definition_reloaded";
    result.type     = event.key.first;
    result.id       = event.key.second;
    result.version  = event.data.version;
    result.generation = event.generation;
    return result;
}

DefinitionHandle DefinitionRegistry::projectHandle(const Storage::Handle& handle) {
    return DefinitionHandle{makeDefinitionReference(handle.key.first, handle.key.second), handle.generation};
}

DefinitionRegistry::Storage::Handle DefinitionRegistry::storageHandle(const DefinitionHandle& handle) {
    const auto& logical = handle.reference.id();
    return Storage::Handle{Key{std::string(logical.namespaceName()), std::string(logical.name())}, handle.generation};
}

void DefinitionRegistry::clearEventProjection() const { eventProjection_.clear(); }

eve::Result<DefinitionHandle> DefinitionRegistry::insert(const std::string& type, const std::string& id, int version,
                                                         const std::string& json) {
    auto definition = makeDefinition(type, id, version, json);
    if (!definition.ok()) {
        return eve::Result<DefinitionHandle>::failure(definition.status());
    }
    Definition      value = std::move(definition).takeValue();
    const EventData data{value.version};
    auto            result = storage_.insert(Key{type, id}, std::move(value), data);
    if (!result.ok()) {
        return eve::Result<DefinitionHandle>::failure(result.status());
    }
    const eve::Status mutationStatus = result.status();
    const auto        handle         = std::move(result).takeValue();
    clearEventProjection();
    if (const auto next = revision_.incremented()) revision_ = *next;
    return eve::Result<DefinitionHandle>::success(projectHandle(handle), mutationStatus);
}

eve::Result<DefinitionHandle> DefinitionRegistry::replace(const std::string& type, const std::string& id, int version,
                                                          const std::string& json) {
    auto definition = makeDefinition(type, id, version, json);
    if (!definition.ok()) {
        return eve::Result<DefinitionHandle>::failure(definition.status());
    }
    Definition      value = std::move(definition).takeValue();
    const EventData data{value.version};
    auto            result = storage_.replace(Key{type, id}, std::move(value), data);
    if (!result.ok()) {
        return eve::Result<DefinitionHandle>::failure(result.status());
    }
    const eve::Status mutationStatus = result.status();
    const auto        handle         = std::move(result).takeValue();
    clearEventProjection();
    if (const auto next = revision_.incremented()) revision_ = *next;
    return eve::Result<DefinitionHandle>::success(projectHandle(handle), mutationStatus);
}

eve::Result<DefinitionHandle> DefinitionRegistry::remove(const std::string& type, const std::string& id) {
    const Key key{type, id};
    auto      current = storage_.resolve(key);
    if (!current.ok()) {
        return eve::Result<DefinitionHandle>::failure(current.status());
    }
    const EventData data{current.value().get().version};
    auto            result = storage_.remove(key, data);
    if (!result.ok()) {
        return eve::Result<DefinitionHandle>::failure(result.status());
    }
    const eve::Status mutationStatus = result.status();
    const auto        handle         = std::move(result).takeValue();
    clearEventProjection();
    if (const auto next = revision_.incremented()) revision_ = *next;
    return eve::Result<DefinitionHandle>::success(projectHandle(handle), mutationStatus);
}

eve::ResultRef<const Definition> DefinitionRegistry::resolve(const std::string& type, const std::string& id) const {
    auto result = storage_.resolve(Key{type, id});
    if (!result.ok()) return eve::ResultRef<const Definition>::failure(result.status());
    return eve::ResultRef<const Definition>::success(std::cref(result.value().get()));
}

eve::ResultRef<const Definition> DefinitionRegistry::resolveHandle(const DefinitionHandle& handle) const {
    auto result = storage_.resolve(storageHandle(handle));
    if (!result.ok()) return eve::ResultRef<const Definition>::failure(result.status());
    return eve::ResultRef<const Definition>::success(std::cref(result.value().get()));
}

eve::Result<DefinitionHandle> DefinitionRegistry::handle(const std::string& type, const std::string& id) const {
    auto result = storage_.handle(Key{type, id});
    if (!result.ok()) return eve::Result<DefinitionHandle>::failure(result.status());
    return eve::Result<DefinitionHandle>::success(projectHandle(result.value()));
}

eve::Result<eve::Generation> DefinitionRegistry::generationOf(const std::string& type, const std::string& id) const {
    return storage_.generationOf(Key{type, id});
}

bool DefinitionRegistry::isTombstone(const std::string& type, const std::string& id) const noexcept {
    return storage_.isTombstone(Key{type, id});
}

bool DefinitionRegistry::isStale(const DefinitionHandle& handle) const noexcept {
    return storage_.isStale(storageHandle(handle));
}

DefinitionRef DefinitionRegistry::reference(const std::string& type, const std::string& id) const {
    return makeDefinitionReference(type, id);
}

int DefinitionRegistry::size() const { return static_cast<int>(storage_.size()); }

int DefinitionRegistry::countType(const std::string& type) const {
    int result = 0;
    for (std::size_t index = 0;; ++index) {
        const auto* key = storage_.keyAt(index);
        if (!key) break;
        if (key->first == type) ++result;
    }
    return result;
}

const Definition* DefinitionRegistry::at(int index) const {
    if (index < 0) return nullptr;
    return storage_.at(static_cast<std::size_t>(index));
}

const Definition* DefinitionRegistry::atType(const std::string& type, int index) const {
    if (index < 0) return nullptr;
    int matching = 0;
    for (std::size_t position = 0;; ++position) {
        const auto* key = storage_.keyAt(position);
        if (!key) break;
        if (key->first != type) continue;
        if (matching++ == index) return storage_.at(position);
    }
    return nullptr;
}

int DefinitionRegistry::eventCount() const { return static_cast<int>(storage_.eventCount()); }

const DefinitionEvent* DefinitionRegistry::eventAt(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= storage_.eventCount()) return nullptr;
    if (eventProjection_.size() != storage_.eventCount()) {
        eventProjection_.clear();
        eventProjection_.reserve(storage_.eventCount());
        for (std::size_t i = 0; i < storage_.eventCount(); ++i)
            eventProjection_.push_back(projectEvent(*storage_.eventAt(i)));
    }
    return &eventProjection_[static_cast<std::size_t>(index)];
}

void DefinitionRegistry::clearEvents() {
    storage_.clearEvents();
    clearEventProjection();
    if (const auto next = revision_.incremented()) revision_ = *next;
}

std::string DefinitionRegistry::snapshotJson() const {
    const auto        state = storage_.snapshotState();
    eve::Value::Array definitions;
    eve::Value::Array events;
    eve::Value::Array generations;
    definitions.reserve(storage_.size());
    events.reserve(storage_.eventCount());
    generations.reserve(state.entries.size());

    for (const auto& [key, entry] : state.entries) {
        eve::Value::Object generation;
        generation.emplace("generation", eve::Value(std::to_string(entry.generation.value())));
        generation.emplace("id", eve::Value(key.second));
        generation.emplace("type", eve::Value(key.first));
        generations.emplace_back(std::move(generation));

        if (!entry.value.has_value()) continue;
        const auto&        definition = *entry.value;
        eve::Value::Object item;
        item.emplace("generation", eve::Value(std::to_string(entry.generation.value())));
        item.emplace("id", eve::Value(definition.id));
        item.emplace("json", parseStoredJson(definition.json));
        item.emplace("type", eve::Value(definition.type));
        item.emplace("version", eve::Value(static_cast<std::int64_t>(definition.version.value())));
        definitions.emplace_back(std::move(item));
    }

    for (const auto& raw : state.events) {
        const auto         event = projectEvent(raw);
        eve::Value::Object item;
        item.emplace("generation", eve::Value(std::to_string(event.generation.value())));
        item.emplace("id", eve::Value(event.id));
        item.emplace("name", eve::Value(event.name));
        item.emplace("sequence", eve::Value(std::to_string(event.sequence.value())));
        item.emplace("type", eve::Value(event.type));
        item.emplace("version", eve::Value(static_cast<std::int64_t>(event.version.value())));
        events.emplace_back(std::move(item));
    }

    eve::Value::Object root;
    root.emplace("definitions", eve::Value(std::move(definitions)));
    root.emplace("events", eve::Value(std::move(events)));
    root.emplace("generations", eve::Value(std::move(generations)));
    root.emplace("nextEventSequence", eve::Value(std::to_string(state.nextEventSequence.value())));
    root.emplace("version", eve::Value(std::int64_t(1)));
    auto encoded = eve::Value(std::move(root)).toJson();
    if (!encoded.ok()) std::terminate();
    return std::move(encoded).takeValue();
}

eve::Result<void> DefinitionRegistry::restoreJson(const std::string& json) {
    auto schemaValidation = validateDefinitionsSnapshot(json);
    if (!schemaValidation.ok()) return schemaValidation;
    auto parsed = eve::Value::fromJson(json);
    if (!parsed.ok()) {
        return eve::Result<void>::failure(parsed.status());
    }
    const eve::Value value = std::move(parsed).takeValue();
    const auto*      root  = value.getIf<eve::Value::Object>();
    if (!root) {
        return failure<void>(eve::DiagnosticCode::ParseError, "definitions snapshot must be an object");
    }
    const auto*   snapshotVersion   = field(*root, "version");
    const auto*   versionNumber     = snapshotVersion ? snapshotVersion->getIf<std::int64_t>() : nullptr;
    const auto*   definitions       = field(*root, "definitions");
    const auto*   generations       = field(*root, "generations");
    const auto*   events            = field(*root, "events");
    const auto*   nextSequenceValue = field(*root, "nextEventSequence");
    const auto*   definitionArray   = definitions ? definitions->getIf<eve::Value::Array>() : nullptr;
    const auto*   generationArray   = generations ? generations->getIf<eve::Value::Array>() : nullptr;
    const auto*   eventArray        = events ? events->getIf<eve::Value::Array>() : nullptr;
    std::uint64_t nextSequence      = 0;
    if (!versionNumber || *versionNumber != 1 || !definitionArray || !generationArray || !eventArray ||
        !nextSequenceValue || !readUint64String(*nextSequenceValue, nextSequence) || nextSequence == 0) {
        return failure<void>(eve::DiagnosticCode::ParseError, "invalid definitions snapshot fields");
    }

    Storage::State candidate;
    for (std::size_t index = 0; index < generationArray->size(); ++index) {
        const auto*   item = (*generationArray)[index].getIf<eve::Value::Object>();
        std::string   type;
        std::string   id;
        std::uint64_t generation      = 0;
        const auto*   generationValue = item ? field(*item, "generation") : nullptr;
        if (!item || !readString(*item, "type", type) || !readString(*item, "id", id) || type.empty() || id.empty() ||
            !generationValue || !readUint64String(*generationValue, generation) || generation == 0 ||
            !candidate.entries.emplace(Key{type, id}, Storage::Entry{eve::Generation(generation), std::nullopt})
                 .second) {
            return failure<void>(eve::DiagnosticCode::ParseError,
                                 "invalid definition generation at index " + std::to_string(index));
        }
    }

    for (std::size_t index = 0; index < definitionArray->size(); ++index) {
        const auto*        item = (*definitionArray)[index].getIf<eve::Value::Object>();
        std::string        type;
        std::string        id;
        eve::SchemaVersion versionValue;
        std::uint64_t      generation      = 0;
        const auto*        jsonValue       = item ? field(*item, "json") : nullptr;
        const auto*        versionField    = item ? field(*item, "version") : nullptr;
        const auto*        generationField = item ? field(*item, "generation") : nullptr;
        if (!item || !readString(*item, "type", type) || !readString(*item, "id", id) || type.empty() || id.empty() ||
            !versionField || !readPositiveSchemaVersion(*versionField, versionValue) || !generationField ||
            !readUint64String(*generationField, generation) || generation == 0 || !jsonValue) {
            return failure<void>(eve::DiagnosticCode::ParseError,
                                 "invalid definition at index " + std::to_string(index));
        }
        const Key key{type, id};
        auto      entry = candidate.entries.find(key);
        if (entry == candidate.entries.end() || entry->second.generation != eve::Generation(generation) ||
            entry->second.value.has_value()) {
            return failure<void>(eve::DiagnosticCode::InvariantViolation,
                                 "inconsistent definition generation at index " + std::to_string(index));
        }
        auto canonical = jsonValue->toJson();
        if (!canonical.ok()) {
            return eve::Result<void>::failure(canonical.status());
        }
        Definition definition;
        definition.type       = type;
        definition.id         = id;
        definition.version    = versionValue;
        definition.generation = eve::Generation(generation);
        definition.json       = std::move(canonical).takeValue();
        entry->second.value   = std::move(definition);
    }

    eve::EventSequence previousSequence;
    for (std::size_t index = 0; index < eventArray->size(); ++index) {
        const auto*        item = (*eventArray)[index].getIf<eve::Value::Object>();
        std::string        name;
        std::string        type;
        std::string        id;
        eve::SchemaVersion versionValue;
        std::uint64_t      sequence        = 0;
        std::uint64_t      generation      = 0;
        const auto*        versionField    = item ? field(*item, "version") : nullptr;
        const auto*        sequenceField   = item ? field(*item, "sequence") : nullptr;
        const auto*        generationField = item ? field(*item, "generation") : nullptr;
        if (!item || !readString(*item, "name", name) || !readString(*item, "type", type) ||
            !readString(*item, "id", id) || (name != "definition_reloaded" && name != "definition_removed") ||
            type.empty() || id.empty() || !versionField || !readPositiveSchemaVersion(*versionField, versionValue) ||
            !sequenceField || !readUint64String(*sequenceField, sequence) || sequence == 0 ||
            (!previousSequence.isZero() && eve::EventSequence(sequence) <= previousSequence) || !generationField ||
            !readUint64String(*generationField, generation) || generation == 0) {
            return failure<void>(eve::DiagnosticCode::ParseError,
                                 "invalid definition event at index " + std::to_string(index));
        }
        const bool     removed = name == "definition_removed";
        Storage::Event event;
        event.sequence     = eve::EventSequence(sequence);
        event.operation    = removed
                                 ? eve::RegistryOperation::Remove
                                 : (generation == 1 ? eve::RegistryOperation::Insert : eve::RegistryOperation::Replace);
        event.key          = Key{type, id};
        event.generation   = eve::Generation(generation);
        event.tombstone    = removed;
        event.data.version = versionValue;
        candidate.events.push_back(std::move(event));
        previousSequence = eve::EventSequence(sequence);
    }
    if (!previousSequence.isZero() && eve::EventSequence(nextSequence) <= previousSequence) {
        return failure<void>(eve::DiagnosticCode::InvariantViolation,
                             "next event sequence must exceed retained events");
    }
    candidate.nextEventSequence = eve::EventSequence(nextSequence);

    auto restored = storage_.restoreState(std::move(candidate));
    if (!restored.ok()) {
        return eve::Result<void>::failure(restored.status());
    }
    clearEventProjection();
    revision_ = eve::Revision(nextSequence - 1);
    tick_     = eve::SimulationTick{};
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<eve::SnapshotEnvelope> DefinitionRegistry::snapshot(const eve::SnapshotHashProvider& hashProvider) const {
    auto payload = eve::Value::fromJson(snapshotJson());
    if (!payload.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(payload.status());
    return eve::makeSnapshotEnvelope("definitions.registry", definitionsSchema(), eve::SchemaVersion(1), instanceId_,
                                     revision_, tick_, std::move(payload).takeValue(), hashProvider);
}

eve::Result<void> DefinitionRegistry::restoreSnapshot(const eve::SnapshotEnvelope&     source,
                                                      const eve::SnapshotHashProvider& hashProvider) {
    if (source.type != "definitions.registry" || source.schema != definitionsSchema())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "snapshot does not belong to definitions::DefinitionRegistry");
    if (!instanceId_.isNil() && source.instanceId != instanceId_)
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "snapshot instanceId does not match definitions::DefinitionRegistry");

    auto migrated = definitionsMigrations().migrate(source, eve::SchemaVersion(1), hashProvider);
    if (!migrated.ok()) return eve::Result<void>::failure(migrated.status());
    const auto& candidate = migrated.value();
    auto        metadata  = eve::validateSnapshotPayloadMetadata(candidate.payload, candidate.revision, candidate.tick);
    if (!metadata.ok()) return eve::Result<void>::failure(metadata.status());
    auto payload = candidate.payload.toJson();
    if (!payload.ok()) return eve::Result<void>::failure(payload.status());
    const std::string payloadJson = std::move(payload).takeValue();

    // restoreJson validates into a candidate State before the single storage swap.
    auto restored = restoreJson(payloadJson);
    if (!restored.ok()) return eve::Result<void>::failure(restored.status());

    instanceId_ = candidate.instanceId;
    revision_   = candidate.revision;
    tick_       = candidate.tick;
    return eve::Result<void>::success();
}

eve::Result<std::string> DefinitionRegistry::snapshotEnvelopeJson(const eve::SnapshotHashProvider& hashProvider) const {
    auto value = snapshot(hashProvider);
    if (!value.ok()) return eve::Result<std::string>::failure(value.status());
    return std::move(value).andThen(
        [](eve::SnapshotEnvelope&& envelope) { return eve::serializeSnapshotEnvelope(envelope); });
}

eve::Result<void> DefinitionRegistry::restoreSnapshotJson(std::string_view                 json,
                                                          const eve::SnapshotHashProvider& hashProvider) {
    auto source = eve::parseSnapshotEnvelope(json, hashProvider);
    if (!source.ok()) return eve::Result<void>::failure(source.status());
    return restoreSnapshot(std::move(source).takeValue(), hashProvider);
}

eve::Subscription DefinitionRegistry::subscribe(ChangeCallback callback) {
    return storage_.subscribe([callback = std::move(callback)](const Storage::Event& event) {
        if (!callback) return;
        const DefinitionEvent projection = DefinitionRegistry::projectEvent(event);
        callback(projection);
    });
}

DefinitionRegistry* Definitions::newRegistry() {
    registries_.push_back(std::make_unique<DefinitionRegistry>());
    return registries_.back().get();
}

Module_IMPL(Definitions, new Definitions());

void Definitions::expose(ssq::Table& table) {
    const HSQUIRRELVM vm         = table.getHandle();
    auto definition = table.addClass<Definition>(
        "Definition", std::function<Definition*()>([]() -> Definition* { return nullptr; }), false);
    definition.addFunc("getType", [](Definition* value) { return value ? value->type : std::string{}; });
    definition.addFunc("getId", [](Definition* value) { return value ? value->id : std::string{}; });
    definition.addFunc("getVersion", [](Definition* value) {
        return value ? static_cast<std::int64_t>(value->version.value()) : std::int64_t{0};
    });
    definition.addFunc("getGeneration", [](Definition* value) {
        return value ? static_cast<std::int64_t>(value->generation.value()) : std::int64_t{0};
    });
    definition.addFunc("getJson", [](Definition* value) { return value ? value->json : std::string{}; });

    auto event = table.addClass<DefinitionEvent>(
        "DefinitionEvent", std::function<DefinitionEvent*()>([]() -> DefinitionEvent* { return nullptr; }), false);
    event.addFunc("getSequence", [](DefinitionEvent* value) {
        return value ? static_cast<std::int64_t>(value->sequence.value()) : std::int64_t{0};
    });
    event.addFunc("getName", [](DefinitionEvent* value) { return value ? value->name : std::string{}; });
    event.addFunc("getType", [](DefinitionEvent* value) { return value ? value->type : std::string{}; });
    event.addFunc("getId", [](DefinitionEvent* value) { return value ? value->id : std::string{}; });
    event.addFunc("getVersion", [](DefinitionEvent* value) {
        return value ? static_cast<std::int64_t>(value->version.value()) : std::int64_t{0};
    });
    event.addFunc("getGeneration", [](DefinitionEvent* value) {
        return value ? static_cast<std::int64_t>(value->generation.value()) : std::int64_t{0};
    });

    auto registry = table.addClass<DefinitionRegistry>(
        "DefinitionRegistry", std::function<DefinitionRegistry*()>([]() -> DefinitionRegistry* { return nullptr; }),
        false);
    registry.addFunc("insert", [vm](DefinitionRegistry* value, const std::string& type, const std::string& id,
                                    int version, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm,
                definitionsBindingFailure<DefinitionHandle>(eve::DiagnosticCode::InvalidArgument,
                                                            "definition registry must not be null", "registry"),
                [](DefinitionHandle&& handle) { return definitionHandleProjection(handle); });
        return eve::script::projectResult(vm, value->insert(type, id, version, json),
                                          [](DefinitionHandle&& handle) { return definitionHandleProjection(handle); });
    });
    registry.addFunc("replace", [vm](DefinitionRegistry* value, const std::string& type, const std::string& id,
                                     int version, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm,
                definitionsBindingFailure<DefinitionHandle>(eve::DiagnosticCode::InvalidArgument,
                                                            "definition registry must not be null", "registry"),
                [](DefinitionHandle&& handle) { return definitionHandleProjection(handle); });
        return eve::script::projectResult(vm, value->replace(type, id, version, json),
                                          [](DefinitionHandle&& handle) { return definitionHandleProjection(handle); });
    });
    registry.addFunc("remove", [vm](DefinitionRegistry* value, const std::string& type, const std::string& id) {
        if (!value)
            return eve::script::projectResult(
                vm,
                definitionsBindingFailure<DefinitionHandle>(eve::DiagnosticCode::InvalidArgument,
                                                            "definition registry must not be null", "registry"),
                [](DefinitionHandle&& handle) { return definitionHandleProjection(handle); });
        return eve::script::projectResult(vm, value->remove(type, id),
                                          [](DefinitionHandle&& handle) { return definitionHandleProjection(handle); });
    });
    registry.addFunc("resolve", [vm](DefinitionRegistry* value, const std::string& type, const std::string& id) {
        if (!value)
            return eve::script::projectResult(
                vm,
                definitionsBindingFailure<std::reference_wrapper<const Definition>>(
                    eve::DiagnosticCode::InvalidArgument, "definition registry must not be null", "registry"),
                [](std::reference_wrapper<const Definition>&& definition) {
                    return definitionProjection(definition.get());
                });
        return eve::script::projectResult(vm, value->resolve(type, id),
                                          [](std::reference_wrapper<const Definition>&& definition) {
                                              return definitionProjection(definition.get());
                                          });
    });
    registry.addFunc("resolveHandle", [vm](DefinitionRegistry* value, const std::string& type, const std::string& id,
                                           std::int64_t generation) {
        if (!value)
            return eve::script::projectResult(
                vm,
                definitionsBindingFailure<std::reference_wrapper<const Definition>>(
                    eve::DiagnosticCode::InvalidArgument, "definition registry must not be null", "registry"),
                [](std::reference_wrapper<const Definition>&& definition) {
                    return definitionProjection(definition.get());
                });
        if (generation <= 0)
            return eve::script::projectResult(
                vm,
                definitionsBindingFailure<std::reference_wrapper<const Definition>>(
                    eve::DiagnosticCode::InvalidArgument, "definition generation must be positive", "generation"),
                [](std::reference_wrapper<const Definition>&& definition) {
                    return definitionProjection(definition.get());
                });
        return eve::script::projectResult(
            vm,
            value->resolveHandle(
                DefinitionHandle{value->reference(type, id), eve::Generation(static_cast<std::uint64_t>(generation))}),
            [](std::reference_wrapper<const Definition>&& definition) {
                return definitionProjection(definition.get());
            });
    });
    registry.addFunc("size", &DefinitionRegistry::size);
    registry.addFunc("countType", &DefinitionRegistry::countType);
    registry.addFunc("at", [](DefinitionRegistry* value, int index) -> Definition* {
        return value ? const_cast<Definition*>(value->at(index)) : nullptr;
    });
    registry.addFunc("atType", [](DefinitionRegistry* value, const std::string& type, int index) -> Definition* {
        return value ? const_cast<Definition*>(value->atType(type, index)) : nullptr;
    });
    registry.addFunc("eventCount", &DefinitionRegistry::eventCount);
    registry.addFunc("eventAt", [](DefinitionRegistry* value, int index) -> DefinitionEvent* {
        return value ? const_cast<DefinitionEvent*>(value->eventAt(index)) : nullptr;
    });
    registry.addFunc("clearEvents", &DefinitionRegistry::clearEvents);
    registry.addFunc("snapshotJson", &DefinitionRegistry::snapshotJson);
    registry.addFunc("restoreJson", [vm](DefinitionRegistry* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, definitionsBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                    "definition registry must not be null", "registry"));
        return eve::script::projectResult(vm, value->restoreJson(json));
    });
    registry.addFunc("handle", [vm](DefinitionRegistry* value, const std::string& type, const std::string& id) {
        if (!value)
            return eve::script::projectResult(
                vm,
                definitionsBindingFailure<DefinitionHandle>(eve::DiagnosticCode::InvalidArgument,
                                                            "definition registry must not be null", "registry"),
                [](DefinitionHandle&& handle) { return definitionHandleProjection(handle); });
        return eve::script::projectResult(vm, value->handle(type, id),
                                          [](DefinitionHandle&& handle) { return definitionHandleProjection(handle); });
    });
    registry.addFunc("generationOf", [vm](DefinitionRegistry* value, const std::string& type, const std::string& id) {
        if (!value)
            return eve::script::projectResult(
                vm,
                definitionsBindingFailure<eve::Generation>(eve::DiagnosticCode::InvalidArgument,
                                                           "definition registry must not be null", "registry"),
                [](eve::Generation generation) { return eve::Value(static_cast<std::int64_t>(generation.value())); });
        return eve::script::projectResult(vm, value->generationOf(type, id), [](eve::Generation generation) {
            return eve::Value(static_cast<std::int64_t>(generation.value()));
        });
    });

    auto cls = table.addClass(name, Definitions::create, false);
    expose(cls);
}

void Definitions::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Definitions::getName);
    cls.addFunc("newRegistry", &Definitions::newRegistry);
}

}  // namespace eve::definitions
