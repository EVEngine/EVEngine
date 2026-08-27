#include "policyregistry/PolicyRegistry.h"

#include "common/SquirrelBinding.h"
#include "schema/SchemaRegistry.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <charconv>
#include <cstdint>
#include <exception>
#include <limits>
#include <utility>

namespace eve::policyregistry {
namespace {

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
    const auto* text = value ? value->getIf<std::string>() : nullptr;
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

bool readInt(const eve::Value& value, int& output) {
    const auto* number = value.getIf<std::int64_t>();
    if (!number || *number < std::numeric_limits<int>::min() || *number > std::numeric_limits<int>::max()) return false;
    output = static_cast<int>(*number);
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
    if (!parsed.ok()) std::terminate();
    return std::move(parsed).takeValue();
}

eve::LogicalId policySchema() {
    const auto schema = eve::LogicalId::parse("policyregistry:registry");
    if (!schema) std::terminate();
    return *schema;
}

const eve::SnapshotMigrationChain& policyMigrations() {
    static const eve::SnapshotMigrationChain chain = [] {
        eve::SnapshotMigrationChain result;
        const auto registration = result.add(
            policySchema(), eve::SchemaVersion(0), eve::SchemaVersion(1),
            [](const eve::Value& payload) -> eve::Result<eve::Value> {
                const auto* object = payload.getIf<eve::Value::Object>();
                if (!object)
                    return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::ParseError, "policy snapshot payload must be an object"));
                eve::Value::Object migrated = *object;
                migrated["version"] = eve::Value(std::int64_t(1));
                return eve::Result<eve::Value>::success(eve::Value(std::move(migrated)));
            });
        if (!registration.ok()) std::terminate();
        return result;
    }();
    return chain;
}

eve::Result<PolicyDescriptor> makeDescriptor(const std::string& domain, const std::string& name, int version,
                                             int priority, bool enabled, const std::string& kindName,
                                             const std::string& schemaId, const std::string& metadataJson) {
    if (domain.empty() || name.empty())
        return failure<PolicyDescriptor>(eve::DiagnosticCode::InvalidArgument,
                                         "policy domain and name must not be empty");
    if (version <= 0)
        return failure<PolicyDescriptor>(eve::DiagnosticCode::InvalidArgument,
                                         "policy schema version must be positive");

    ImplementationKind kind = ImplementationKind::Builtin;
    if (!parseImplementationKind(kindName, kind))
        return failure<PolicyDescriptor>(eve::DiagnosticCode::InvalidArgument, "invalid implementation kind");

    auto metadata = eve::Value::fromJson(metadataJson);
    if (!metadata.ok()) return eve::Result<PolicyDescriptor>::failure(metadata.status());
    auto metadataValue = std::move(metadata).takeValue();
    if (!metadataValue.isObject())
        return failure<PolicyDescriptor>(eve::DiagnosticCode::InvalidArgument,
                                         "policy metadata must be a JSON object");
    auto canonical = metadataValue.toJson();
    if (!canonical.ok()) return eve::Result<PolicyDescriptor>::failure(canonical.status());

    if (!schemaId.empty() && eve::schema::SchemaRegistry::resolve(schemaId, version)) {
        const auto errors = eve::schema::SchemaRegistry::validate(schemaId, version, metadataJson);
        if (!errors.empty()) {
            const auto& error = errors.front();
            return eve::Result<PolicyDescriptor>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, error.message, error.path,
                eve::DiagnosticDetails{{"schemaId", schemaId},
                                       {"schemaVersion", std::to_string(version)},
                                       {"validationCode", error.code}},
                "policyregistry.schema"));
        }
    }

    PolicyDescriptor descriptor;
    descriptor.domain = domain;
    descriptor.name = name;
    descriptor.version = eve::SchemaVersion(static_cast<std::uint64_t>(version));
    descriptor.priority = priority;
    descriptor.enabled = enabled;
    descriptor.kind = kind;
    descriptor.schemaId = schemaId;
    descriptor.metadataJson = std::move(canonical).takeValue();
    return eve::Result<PolicyDescriptor>::success(std::move(descriptor));
}

eve::Value policyHandleValue(const PolicyHandle& handle) {
    return eve::Value(eve::Value::Object{
        {"domain", handle.domain},
        {"name", handle.name},
        {"generation", eve::Value(static_cast<std::int64_t>(handle.generation.value()))},
    });
}

eve::Value policyDescriptorValue(const PolicyDescriptor& descriptor) {
    return eve::Value(eve::Value::Object{
        {"domain", descriptor.domain},
        {"name", descriptor.name},
        {"version", eve::Value(static_cast<std::int64_t>(descriptor.version.value()))},
        {"priority", eve::Value(descriptor.priority)},
        {"enabled", eve::Value(descriptor.enabled)},
        {"kind", eve::Value(implementationKindName(descriptor.kind))},
        {"schemaId", eve::Value(descriptor.schemaId)},
        {"metadataJson", eve::Value(descriptor.metadataJson)},
        {"generation", eve::Value(static_cast<std::int64_t>(descriptor.generation.value()))},
    });
}

template <class T>
eve::Result<T> registryFailure(eve::DiagnosticCode code, std::string message,
                               std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "policyregistry"));
}

}  // namespace

std::string implementationKindName(ImplementationKind kind) {
    switch (kind) {
    case ImplementationKind::Builtin: return "builtin";
    case ImplementationKind::Script: return "script";
    case ImplementationKind::Batch: return "batch";
    }
    return {};
}

bool parseImplementationKind(const std::string& name, ImplementationKind& kind) {
    if (name == "builtin")
        kind = ImplementationKind::Builtin;
    else if (name == "script")
        kind = ImplementationKind::Script;
    else if (name == "batch")
        kind = ImplementationKind::Batch;
    else
        return false;
    return true;
}

PolicyRegistry::PolicyRegistry(eve::PersistentId instanceId)
    : storage_([](PolicyDescriptor& value, eve::Generation generation) { value.generation = generation; }),
      instanceId_(instanceId) {}

PolicyEvent PolicyRegistry::projectEvent(const Storage::Event& event) {
    PolicyEvent result;
    result.sequence = event.sequence;
    if (event.label == "enabled")
        result.name = "policy_enabled";
    else if (event.operation == eve::RegistryOperation::Insert)
        result.name = "policy_registered";
    else if (event.operation == eve::RegistryOperation::Replace)
        result.name = "policy_replaced";
    else
        result.name = "policy_removed";
    result.domain = event.key.first;
    result.policyName = event.key.second;
    result.version = event.data.version;
    result.generation = event.generation;
    result.enabled = event.data.enabled;
    return result;
}

PolicyHandle PolicyRegistry::projectHandle(const Storage::Handle& handle) {
    return PolicyHandle{handle.key.first, handle.key.second, handle.generation};
}

PolicyRegistry::Storage::Handle PolicyRegistry::storageHandle(const PolicyHandle& handle) {
    return Storage::Handle{Key{handle.domain, handle.name}, handle.generation};
}

void PolicyRegistry::clearEventProjection() const { eventProjection_.clear(); }

eve::Result<PolicyHandle> PolicyRegistry::insert(
    const std::string& domain, const std::string& name, int version, int priority, bool enabled,
    const std::string& kind, const std::string& schemaId, const std::string& metadataJson) {
    auto descriptor = makeDescriptor(domain, name, version, priority, enabled, kind, schemaId, metadataJson);
    if (!descriptor.ok()) {
        return eve::Result<PolicyHandle>::failure(descriptor.status());
    }
    PolicyDescriptor value = std::move(descriptor).takeValue();
    const EventData data{value.version, value.enabled};
    auto result = storage_.insert(Key{domain, name}, std::move(value), data);
    if (!result.ok()) {
        return eve::Result<PolicyHandle>::failure(result.status());
    }
    const eve::Status mutationStatus = result.status();
    const auto handle = std::move(result).takeValue();
    clearEventProjection();
    if (const auto next = revision_.incremented()) revision_ = *next;
    return eve::Result<PolicyHandle>::success(projectHandle(handle), mutationStatus);
}

eve::Result<PolicyHandle> PolicyRegistry::replace(
    const std::string& domain, const std::string& name, int version, int priority, bool enabled,
    const std::string& kind, const std::string& schemaId, const std::string& metadataJson) {
    auto descriptor = makeDescriptor(domain, name, version, priority, enabled, kind, schemaId, metadataJson);
    if (!descriptor.ok()) {
        return eve::Result<PolicyHandle>::failure(descriptor.status());
    }
    PolicyDescriptor value = std::move(descriptor).takeValue();
    const EventData data{value.version, value.enabled};
    auto result = storage_.replace(Key{domain, name}, std::move(value), data);
    if (!result.ok()) {
        return eve::Result<PolicyHandle>::failure(result.status());
    }
    const eve::Status mutationStatus = result.status();
    const auto handle = std::move(result).takeValue();
    clearEventProjection();
    if (const auto next = revision_.incremented()) revision_ = *next;
    return eve::Result<PolicyHandle>::success(projectHandle(handle), mutationStatus);
}

eve::Result<PolicyHandle> PolicyRegistry::remove(const std::string& domain, const std::string& name) {
    const Key key{domain, name};
    auto current = storage_.resolve(key);
    if (!current.ok()) {
        return eve::Result<PolicyHandle>::failure(current.status());
    }
    const auto& descriptor = current.value().get();
    const EventData data{descriptor.version, descriptor.enabled};
    auto result = storage_.remove(key, data);
    if (!result.ok()) {
        return eve::Result<PolicyHandle>::failure(result.status());
    }
    const eve::Status mutationStatus = result.status();
    const auto handle = std::move(result).takeValue();
    clearEventProjection();
    if (const auto next = revision_.incremented()) revision_ = *next;
    return eve::Result<PolicyHandle>::success(projectHandle(handle), mutationStatus);
}

eve::Result<PolicyHandle> PolicyRegistry::enable(const std::string& domain, const std::string& name, bool enabled) {
    const Key key{domain, name};
    auto current = storage_.resolve(key);
    if (!current.ok()) {
        return eve::Result<PolicyHandle>::failure(current.status());
    }
    PolicyDescriptor value = current.value().get();
    if (value.enabled == enabled) {
        auto handle = storage_.handle(key);
        if (!handle.ok()) {
            return eve::Result<PolicyHandle>::failure(handle.status());
        }
        return eve::Result<PolicyHandle>::success(projectHandle(handle.value()),
                                                   eve::Status::success(eve::StatusCode::NoOp));
    }
    value.enabled = enabled;
    const EventData data{value.version, value.enabled};
    auto result = storage_.replace(key, std::move(value), data, "enabled");
    if (!result.ok()) {
        return eve::Result<PolicyHandle>::failure(result.status());
    }
    const eve::Status mutationStatus = result.status();
    const auto handle = std::move(result).takeValue();
    clearEventProjection();
    if (const auto next = revision_.incremented()) revision_ = *next;
    return eve::Result<PolicyHandle>::success(projectHandle(handle), mutationStatus);
}

eve::ResultRef<const PolicyDescriptor> PolicyRegistry::resolve(
    const std::string& domain, const std::string& name) const {
    auto result = storage_.resolve(Key{domain, name});
    if (!result.ok()) return eve::ResultRef<const PolicyDescriptor>::failure(result.status());
    return eve::ResultRef<const PolicyDescriptor>::success(std::cref(result.value().get()));
}

eve::ResultRef<const PolicyDescriptor> PolicyRegistry::resolveHandle(
    const PolicyHandle& handle) const {
    auto result = storage_.resolve(storageHandle(handle));
    if (!result.ok()) return eve::ResultRef<const PolicyDescriptor>::failure(result.status());
    return eve::ResultRef<const PolicyDescriptor>::success(std::cref(result.value().get()));
}

eve::Result<PolicyHandle> PolicyRegistry::handle(
    const std::string& domain, const std::string& name) const {
    auto result = storage_.handle(Key{domain, name});
    if (!result.ok()) return eve::Result<PolicyHandle>::failure(result.status());
    return eve::Result<PolicyHandle>::success(projectHandle(result.value()));
}

eve::Result<eve::Generation> PolicyRegistry::generationOf(
    const std::string& domain, const std::string& name) const {
    return storage_.generationOf(Key{domain, name});
}

bool PolicyRegistry::isTombstone(const std::string& domain, const std::string& name) const noexcept {
    return storage_.isTombstone(Key{domain, name});
}

bool PolicyRegistry::isStale(const PolicyHandle& handle) const noexcept {
    return storage_.isStale(storageHandle(handle));
}

const PolicyDescriptor* PolicyRegistry::select(const std::string& domain) const {
    const PolicyDescriptor* selected = nullptr;
    for (std::size_t index = 0;; ++index) {
        const auto* key = storage_.keyAt(index);
        if (!key) break;
        if (key->first != domain) continue;
        const auto* candidate = storage_.at(index);
        if (candidate && candidate->enabled &&
            (!selected || candidate->priority > selected->priority ||
             (candidate->priority == selected->priority && candidate->name < selected->name)))
            selected = candidate;
    }
    return selected;
}

int PolicyRegistry::size() const { return static_cast<int>(storage_.size()); }

int PolicyRegistry::countDomain(const std::string& domain) const {
    int count = 0;
    for (std::size_t index = 0;; ++index) {
        const auto* key = storage_.keyAt(index);
        if (!key) break;
        if (key->first == domain) ++count;
    }
    return count;
}

const PolicyDescriptor* PolicyRegistry::at(int index) const {
    if (index < 0) return nullptr;
    return storage_.at(static_cast<std::size_t>(index));
}

const PolicyDescriptor* PolicyRegistry::atDomain(const std::string& domain, int index) const {
    if (index < 0) return nullptr;
    int matching = 0;
    for (std::size_t position = 0;; ++position) {
        const auto* key = storage_.keyAt(position);
        if (!key) break;
        if (key->first != domain) continue;
        if (matching++ == index) return storage_.at(position);
    }
    return nullptr;
}

int PolicyRegistry::eventCount() const { return static_cast<int>(storage_.eventCount()); }

const PolicyEvent* PolicyRegistry::eventAt(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= storage_.eventCount()) return nullptr;
    if (eventProjection_.size() != storage_.eventCount()) {
        eventProjection_.clear();
        eventProjection_.reserve(storage_.eventCount());
        for (std::size_t i = 0; i < storage_.eventCount(); ++i)
            eventProjection_.push_back(projectEvent(*storage_.eventAt(i)));
    }
    return &eventProjection_[static_cast<std::size_t>(index)];
}

void PolicyRegistry::clearEvents() {
    storage_.clearEvents();
    clearEventProjection();
    if (const auto next = revision_.incremented()) revision_ = *next;
}

std::string PolicyRegistry::snapshotJson() const {
    const auto state = storage_.snapshotState();
    eve::Value::Array descriptors;
    eve::Value::Array events;
    eve::Value::Array generations;
    descriptors.reserve(storage_.size());
    events.reserve(storage_.eventCount());
    generations.reserve(state.entries.size());

    for (const auto& [key, entry] : state.entries) {
        eve::Value::Object generation;
        generation.emplace("domain", eve::Value(key.first));
        generation.emplace("generation", eve::Value(std::to_string(entry.generation.value())));
        generation.emplace("name", eve::Value(key.second));
        generations.emplace_back(std::move(generation));

        if (!entry.value.has_value()) continue;
        const auto& descriptor = *entry.value;
        eve::Value::Object item;
        item.emplace("domain", eve::Value(descriptor.domain));
        item.emplace("enabled", eve::Value(descriptor.enabled));
        item.emplace("generation", eve::Value(std::to_string(entry.generation.value())));
        item.emplace("kind", eve::Value(implementationKindName(descriptor.kind)));
        item.emplace("metadata", parseStoredJson(descriptor.metadataJson));
        item.emplace("name", eve::Value(descriptor.name));
        item.emplace("priority", eve::Value(static_cast<std::int64_t>(descriptor.priority)));
        item.emplace("schemaId", eve::Value(descriptor.schemaId));
        item.emplace("version", eve::Value(static_cast<std::int64_t>(descriptor.version.value())));
        descriptors.emplace_back(std::move(item));
    }

    for (const auto& raw : state.events) {
        const auto event = projectEvent(raw);
        eve::Value::Object item;
        item.emplace("domain", eve::Value(event.domain));
        item.emplace("enabled", eve::Value(event.enabled));
        item.emplace("generation", eve::Value(std::to_string(event.generation.value())));
        item.emplace("name", eve::Value(event.name));
        item.emplace("policyName", eve::Value(event.policyName));
        item.emplace("sequence", eve::Value(std::to_string(event.sequence.value())));
        item.emplace("version", eve::Value(static_cast<std::int64_t>(event.version.value())));
        events.emplace_back(std::move(item));
    }

    eve::Value::Object root;
    root.emplace("descriptors", eve::Value(std::move(descriptors)));
    root.emplace("events", eve::Value(std::move(events)));
    root.emplace("generations", eve::Value(std::move(generations)));
    root.emplace("nextEventSequence", eve::Value(std::to_string(state.nextEventSequence.value())));
    root.emplace("version", eve::Value(std::int64_t(1)));
    auto encoded = eve::Value(std::move(root)).toJson();
    if (!encoded.ok()) std::terminate();
    return std::move(encoded).takeValue();
}

eve::Result<void> PolicyRegistry::restoreJson(const std::string& json) {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed.ok()) {
        return eve::Result<void>::failure(parsed.status());
    }
    const eve::Value value = std::move(parsed).takeValue();
    const auto* root = value.getIf<eve::Value::Object>();
    if (!root) {
        return failure<void>(eve::DiagnosticCode::ParseError, "policy snapshot must be an object");
    }
    const auto* snapshotVersion = field(*root, "version");
    const auto* versionNumber = snapshotVersion ? snapshotVersion->getIf<std::int64_t>() : nullptr;
    const auto* descriptorValue = field(*root, "descriptors");
    const auto* generationValue = field(*root, "generations");
    const auto* eventValue = field(*root, "events");
    const auto* nextSequenceValue = field(*root, "nextEventSequence");
    const auto* descriptorArray = descriptorValue ? descriptorValue->getIf<eve::Value::Array>() : nullptr;
    const auto* generationArray = generationValue ? generationValue->getIf<eve::Value::Array>() : nullptr;
    const auto* eventArray = eventValue ? eventValue->getIf<eve::Value::Array>() : nullptr;
    std::uint64_t nextSequence = 0;
    if (!versionNumber || *versionNumber != 1 || !descriptorArray || !generationArray || !eventArray ||
        !nextSequenceValue || !readUint64String(*nextSequenceValue, nextSequence) || nextSequence == 0) {
        return failure<void>(eve::DiagnosticCode::ParseError, "invalid policy snapshot fields");
    }

    Storage::State candidate;
    for (std::size_t index = 0; index < generationArray->size(); ++index) {
        const auto* item = (*generationArray)[index].getIf<eve::Value::Object>();
        std::string domain;
        std::string name;
        std::uint64_t generation = 0;
        const auto* generationField = item ? field(*item, "generation") : nullptr;
        if (!item || !readString(*item, "domain", domain) || !readString(*item, "name", name) || domain.empty() ||
            name.empty() || !generationField || !readUint64String(*generationField, generation) || generation == 0 ||
            !candidate.entries.emplace(Key{domain, name}, Storage::Entry{
                                                                  eve::Generation(generation), std::nullopt})
                                      .second) {
            return failure<void>(eve::DiagnosticCode::ParseError,
                                 "invalid policy generation at index " + std::to_string(index));
        }
    }

    for (std::size_t index = 0; index < descriptorArray->size(); ++index) {
        const auto* item = (*descriptorArray)[index].getIf<eve::Value::Object>();
        std::string domain;
        std::string name;
        std::string kindName;
        std::string schemaId;
        eve::SchemaVersion version;
        int priority = 0;
        std::uint64_t generation = 0;
        bool enabled = false;
        const auto* versionField = item ? field(*item, "version") : nullptr;
        const auto* priorityField = item ? field(*item, "priority") : nullptr;
        const auto* enabledField = item ? field(*item, "enabled") : nullptr;
        const auto* generationField = item ? field(*item, "generation") : nullptr;
        const auto* metadata = item ? field(*item, "metadata") : nullptr;
        ImplementationKind kind = ImplementationKind::Builtin;
        if (!item || !readString(*item, "domain", domain) || !readString(*item, "name", name) || domain.empty() ||
            name.empty() || !versionField || !readPositiveSchemaVersion(*versionField, version) || !priorityField ||
            !readInt(*priorityField, priority) || !enabledField || !enabledField->isBool() || !generationField ||
            !readUint64String(*generationField, generation) || generation == 0 || !readString(*item, "kind", kindName) ||
            !parseImplementationKind(kindName, kind) || !metadata || !metadata->isObject() ||
            !readString(*item, "schemaId", schemaId)) {
            return failure<void>(eve::DiagnosticCode::ParseError,
                                 "invalid policy descriptor at index " + std::to_string(index));
        }
        enabled = *enabledField->getIf<bool>();
        auto entry = candidate.entries.find(Key{domain, name});
        if (entry == candidate.entries.end() || entry->second.generation != eve::Generation(generation) ||
            entry->second.value.has_value()) {
            return failure<void>(eve::DiagnosticCode::InvariantViolation,
                                 "inconsistent policy generation at index " + std::to_string(index));
        }
        auto metadataJson = metadata->toJson();
        if (!metadataJson.ok()) {
            return eve::Result<void>::failure(metadataJson.status());
        }
        PolicyDescriptor descriptor;
        descriptor.domain = domain;
        descriptor.name = name;
        descriptor.version = version;
        descriptor.priority = priority;
        descriptor.enabled = enabled;
        descriptor.kind = kind;
        descriptor.schemaId = schemaId;
        descriptor.generation = eve::Generation(generation);
        descriptor.metadataJson = std::move(metadataJson).takeValue();
        entry->second.value = std::move(descriptor);
    }

    eve::EventSequence previousSequence;
    for (std::size_t index = 0; index < eventArray->size(); ++index) {
        const auto* item = (*eventArray)[index].getIf<eve::Value::Object>();
        std::string name;
        std::string domain;
        std::string policyName;
        std::uint64_t sequence = 0;
        std::uint64_t generation = 0;
        const auto* sequenceField = item ? field(*item, "sequence") : nullptr;
        const auto* generationField = item ? field(*item, "generation") : nullptr;
        const auto* enabledField = item ? field(*item, "enabled") : nullptr;
        const auto* versionField = item ? field(*item, "version") : nullptr;
        eve::SchemaVersion version{1};
        if (!item || !readString(*item, "name", name) || !readString(*item, "domain", domain) ||
            !readString(*item, "policyName", policyName) || domain.empty() || policyName.empty() ||
            (name != "policy_registered" && name != "policy_replaced" && name != "policy_removed" &&
             name != "policy_enabled") || !sequenceField || !readUint64String(*sequenceField, sequence) || sequence == 0 ||
            (!previousSequence.isZero() && eve::EventSequence(sequence) <= previousSequence) || !generationField ||
            !readUint64String(*generationField, generation) || generation == 0 || !enabledField ||
            !enabledField->isBool()) {
            return failure<void>(eve::DiagnosticCode::ParseError,
                                 "invalid policy event at index " + std::to_string(index));
        }
        if (versionField && !readPositiveSchemaVersion(*versionField, version)) {
            return failure<void>(eve::DiagnosticCode::ParseError,
                                 "invalid policy event version at index " + std::to_string(index));
        }
        Storage::Event event;
        event.sequence = eve::EventSequence(sequence);
        event.operation = name == "policy_removed"
                              ? eve::RegistryOperation::Remove
                              : (generation == 1 ? eve::RegistryOperation::Insert : eve::RegistryOperation::Replace);
        event.key = Key{domain, policyName};
        event.generation = eve::Generation(generation);
        event.tombstone = name == "policy_removed";
        event.label = name == "policy_enabled" ? "enabled" : std::string{};
        event.data.version = version;
        event.data.enabled = *enabledField->getIf<bool>();
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
    tick_ = eve::SimulationTick{};
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<eve::SnapshotEnvelope> PolicyRegistry::snapshot(
    const eve::SnapshotHashProvider& hashProvider) const {
    auto payload = eve::Value::fromJson(snapshotJson());
    if (!payload.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(payload.status());
    return eve::makeSnapshotEnvelope("policyregistry.registry", policySchema(), eve::SchemaVersion(1), instanceId_,
                                     revision_, tick_, std::move(payload).takeValue(), hashProvider);
}

eve::Result<void> PolicyRegistry::restoreSnapshot(
    const eve::SnapshotEnvelope& source, const eve::SnapshotHashProvider& hashProvider) {
    if (source.type != "policyregistry.registry" || source.schema != policySchema())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "snapshot does not belong to policyregistry::PolicyRegistry");
    if (!instanceId_.isNil() && source.instanceId != instanceId_)
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "snapshot instanceId does not match policyregistry::PolicyRegistry");
    auto migrated = policyMigrations().migrate(source, eve::SchemaVersion(1), hashProvider);
    if (!migrated.ok()) return eve::Result<void>::failure(migrated.status());
    auto payload = migrated.value().payload.toJson();
    if (!payload.ok()) return eve::Result<void>::failure(payload.status());
    auto restored = restoreJson(std::move(payload).takeValue());
    if (!restored.ok()) return eve::Result<void>::failure(restored.status());
    const auto& candidate = migrated.value();
    instanceId_ = candidate.instanceId;
    revision_ = candidate.revision;
    tick_ = candidate.tick;
    return eve::Result<void>::success();
}

eve::Result<std::string> PolicyRegistry::snapshotEnvelopeJson(
    const eve::SnapshotHashProvider& hashProvider) const {
    auto value = snapshot(hashProvider);
    if (!value.ok()) return eve::Result<std::string>::failure(value.status());
    return std::move(value).andThen(
        [](eve::SnapshotEnvelope&& envelope) { return eve::serializeSnapshotEnvelope(envelope); });
}

eve::Result<void> PolicyRegistry::restoreSnapshotJson(
    std::string_view json, const eve::SnapshotHashProvider& hashProvider) {
    auto source = eve::parseSnapshotEnvelope(json, hashProvider);
    if (!source.ok()) return eve::Result<void>::failure(source.status());
    return restoreSnapshot(std::move(source).takeValue(), hashProvider);
}

eve::Subscription PolicyRegistry::subscribe(std::function<void(const PolicyEvent&)> callback) {
    return storage_.subscribe([callback = std::move(callback)](const Storage::Event& event) {
        if (!callback) return;
        const PolicyEvent projection = PolicyRegistry::projectEvent(event);
        callback(projection);
    });
}

PolicyRegistry* PolicyRegistryModule::newRegistry() {
    registries_.push_back(std::make_unique<PolicyRegistry>());
    return registries_.back().get();
}

Module_IMPL(PolicyRegistryModule, new PolicyRegistryModule());

void PolicyRegistryModule::expose(ssq::Table& table) {
    const HSQUIRRELVM vm = table.getHandle();
    auto descriptor = table.addClass<PolicyDescriptor>(
        "PolicyDescriptor", std::function<PolicyDescriptor*()>([]() -> PolicyDescriptor* { return nullptr; }), false);
    descriptor.addFunc("getDomain", [](PolicyDescriptor* v) { return v ? v->domain : std::string{}; });
    descriptor.addFunc("getName", [](PolicyDescriptor* v) { return v ? v->name : std::string{}; });
    descriptor.addFunc("getVersion", [](PolicyDescriptor* v) {
        return v ? static_cast<std::int64_t>(v->version.value()) : std::int64_t{0};
    });
    descriptor.addFunc("getPriority", [](PolicyDescriptor* v) { return v ? v->priority : 0; });
    descriptor.addFunc("isEnabled", [](PolicyDescriptor* v) { return v && v->enabled; });
    descriptor.addFunc("getKind",
                       [](PolicyDescriptor* v) { return v ? implementationKindName(v->kind) : std::string{}; });
    descriptor.addFunc("getSchemaId", [](PolicyDescriptor* v) { return v ? v->schemaId : std::string{}; });
    descriptor.addFunc("getMetadataJson", [](PolicyDescriptor* v) { return v ? v->metadataJson : std::string{}; });
    descriptor.addFunc("getGeneration", [](PolicyDescriptor* v) {
        return v ? static_cast<std::int64_t>(v->generation.value()) : std::int64_t{0};
    });

    auto event = table.addClass<PolicyEvent>(
        "PolicyEvent", std::function<PolicyEvent*()>([]() -> PolicyEvent* { return nullptr; }), false);
    event.addFunc("getSequence", [](PolicyEvent* v) {
        return v ? static_cast<std::int64_t>(v->sequence.value()) : std::int64_t{0};
    });
    event.addFunc("getName", [](PolicyEvent* v) { return v ? v->name : std::string{}; });
    event.addFunc("getDomain", [](PolicyEvent* v) { return v ? v->domain : std::string{}; });
    event.addFunc("getPolicyName", [](PolicyEvent* v) { return v ? v->policyName : std::string{}; });
    event.addFunc("getVersion", [](PolicyEvent* v) {
        return v ? static_cast<std::int64_t>(v->version.value()) : std::int64_t{0};
    });
    event.addFunc("getGeneration", [](PolicyEvent* v) {
        return v ? static_cast<std::int64_t>(v->generation.value()) : std::int64_t{0};
    });
    event.addFunc("isEnabled", [](PolicyEvent* v) { return v && v->enabled; });

    auto registry = table.addClass<PolicyRegistry>(
        "PolicyRegistry", std::function<PolicyRegistry*()>([]() -> PolicyRegistry* { return nullptr; }), false);
    registry.addFunc("insert", [vm](PolicyRegistry* value, const std::string& domain,
                                       const std::string& policyName, int version, int priority,
                                       bool enabled, const std::string& kind,
                                       const std::string& schemaId, const std::string& metadataJson) {
        if (!value)
            return eve::script::projectResult(
                vm, registryFailure<PolicyHandle>(eve::DiagnosticCode::InvalidArgument,
                                                   "policy registry must not be null", "registry"),
                [](PolicyHandle&& handle) { return policyHandleValue(handle); });
        return eve::script::projectResult(
            vm, value->insert(domain, policyName, version, priority, enabled, kind, schemaId, metadataJson),
            [](PolicyHandle&& handle) { return policyHandleValue(handle); });
    });
    registry.addFunc("replace", [vm](PolicyRegistry* value, const std::string& domain,
                                        const std::string& policyName, int version, int priority,
                                        bool enabled, const std::string& kind,
                                        const std::string& schemaId, const std::string& metadataJson) {
        if (!value)
            return eve::script::projectResult(
                vm, registryFailure<PolicyHandle>(eve::DiagnosticCode::InvalidArgument,
                                                   "policy registry must not be null", "registry"),
                [](PolicyHandle&& handle) { return policyHandleValue(handle); });
        return eve::script::projectResult(
            vm, value->replace(domain, policyName, version, priority, enabled, kind, schemaId, metadataJson),
            [](PolicyHandle&& handle) { return policyHandleValue(handle); });
    });
    registry.addFunc("remove", [vm](PolicyRegistry* value, const std::string& domain,
                                       const std::string& policyName) {
        if (!value)
            return eve::script::projectResult(
                vm, registryFailure<PolicyHandle>(eve::DiagnosticCode::InvalidArgument,
                                                   "policy registry must not be null", "registry"),
                [](PolicyHandle&& handle) { return policyHandleValue(handle); });
        return eve::script::projectResult(vm, value->remove(domain, policyName),
                                          [](PolicyHandle&& handle) { return policyHandleValue(handle); });
    });
    registry.addFunc("enable", [vm](PolicyRegistry* value, const std::string& domain,
                                       const std::string& policyName, bool enabled) {
        if (!value)
            return eve::script::projectResult(
                vm, registryFailure<PolicyHandle>(eve::DiagnosticCode::InvalidArgument,
                                                   "policy registry must not be null", "registry"),
                [](PolicyHandle&& handle) { return policyHandleValue(handle); });
        return eve::script::projectResult(vm, value->enable(domain, policyName, enabled),
                                          [](PolicyHandle&& handle) { return policyHandleValue(handle); });
    });
    registry.addFunc("resolve", [vm](PolicyRegistry* value, const std::string& domain,
                                        const std::string& policyName) {
        if (!value)
            return eve::script::projectResult(
                vm, registryFailure<std::reference_wrapper<const PolicyDescriptor>>(
                        eve::DiagnosticCode::InvalidArgument, "policy registry must not be null", "registry"),
                [](std::reference_wrapper<const PolicyDescriptor>&& descriptor) {
                    return policyDescriptorValue(descriptor.get());
                });
        return eve::script::projectResult(vm, value->resolve(domain, policyName),
                                          [](std::reference_wrapper<const PolicyDescriptor>&& descriptor) {
                                              return policyDescriptorValue(descriptor.get());
                                          });
    });
    registry.addFunc("resolveHandle", [vm](PolicyRegistry* value, const std::string& domain,
                                             const std::string& policyName, std::int64_t generation) {
        using DescriptorRef = std::reference_wrapper<const PolicyDescriptor>;
        if (!value)
            return eve::script::projectResult(
                vm, registryFailure<DescriptorRef>(eve::DiagnosticCode::InvalidArgument,
                                                    "policy registry must not be null", "registry"),
                [](DescriptorRef&& descriptor) { return policyDescriptorValue(descriptor.get()); });
        if (generation <= 0)
            return eve::script::projectResult(
                vm, registryFailure<DescriptorRef>(eve::DiagnosticCode::InvalidArgument,
                                                    "policy generation must be positive", "generation"),
                [](DescriptorRef&& descriptor) { return policyDescriptorValue(descriptor.get()); });
        return eve::script::projectResult(
            vm, value->resolveHandle(PolicyHandle{domain, policyName,
                                                  eve::Generation(static_cast<std::uint64_t>(generation))}),
            [](DescriptorRef&& descriptor) { return policyDescriptorValue(descriptor.get()); });
    });
    registry.addFunc("handle", [vm](PolicyRegistry* value, const std::string& domain,
                                       const std::string& policyName) {
        if (!value)
            return eve::script::projectResult(
                vm, registryFailure<PolicyHandle>(eve::DiagnosticCode::InvalidArgument,
                                                   "policy registry must not be null", "registry"),
                [](PolicyHandle&& handle) { return policyHandleValue(handle); });
        return eve::script::projectResult(vm, value->handle(domain, policyName),
                                          [](PolicyHandle&& handle) { return policyHandleValue(handle); });
    });
    registry.addFunc("select", [](PolicyRegistry* v, const std::string& d) -> PolicyDescriptor* {
        return v ? const_cast<PolicyDescriptor*>(v->select(d)) : nullptr;
    });
    registry.addFunc("size", &PolicyRegistry::size);
    registry.addFunc("countDomain", &PolicyRegistry::countDomain);
    registry.addFunc("at", [](PolicyRegistry* v, int i) -> PolicyDescriptor* {
        return v ? const_cast<PolicyDescriptor*>(v->at(i)) : nullptr;
    });
    registry.addFunc("atDomain", [](PolicyRegistry* v, const std::string& d, int i) -> PolicyDescriptor* {
        return v ? const_cast<PolicyDescriptor*>(v->atDomain(d, i)) : nullptr;
    });
    registry.addFunc("eventCount", &PolicyRegistry::eventCount);
    registry.addFunc("eventAt", [](PolicyRegistry* v, int i) -> PolicyEvent* {
        return v ? const_cast<PolicyEvent*>(v->eventAt(i)) : nullptr;
    });
    registry.addFunc("clearEvents", &PolicyRegistry::clearEvents);
    registry.addFunc("snapshotJson", &PolicyRegistry::snapshotJson);
    registry.addFunc("restoreJson", [vm](PolicyRegistry* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, registryFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                          "policy registry must not be null", "registry"));
        return eve::script::projectResult(vm, value->restoreJson(json));
    });
    registry.addFunc("generationOf", [vm](PolicyRegistry* value, const std::string& domain,
                                            const std::string& policyName) {
        if (!value)
            return eve::script::projectResult(
                vm, registryFailure<eve::Generation>(eve::DiagnosticCode::InvalidArgument,
                                                     "policy registry must not be null", "registry"),
                [](eve::Generation generation) {
                    return eve::Value(static_cast<std::int64_t>(generation.value()));
                });
        return eve::script::projectResult(vm, value->generationOf(domain, policyName),
                                          [](eve::Generation generation) {
                                              return eve::Value(static_cast<std::int64_t>(generation.value()));
                                          });
    });

    auto cls = table.addClass(name, PolicyRegistryModule::create, false);
    expose(cls);
}

void PolicyRegistryModule::expose(ssq::Class& cls) {
    cls.addFunc("getName", &PolicyRegistryModule::getName);
    cls.addFunc("newRegistry", &PolicyRegistryModule::newRegistry);
}

}  // namespace eve::policyregistry
