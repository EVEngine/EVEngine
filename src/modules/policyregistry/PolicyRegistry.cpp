#include "policyregistry/PolicyRegistry.h"

#include "common/SquirrelBinding.h"
#include "policy_registry.pb.h"
#include "schema/SchemaRegistry.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/util/json_util.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <charconv>
#include <cstdint>
#include <exception>
#include <limits>
#include <utility>

namespace eve::policyregistry {
namespace {

bool serializeDeterministic(const google::protobuf::MessageLite& message, std::string& bytes) {
    google::protobuf::io::StringOutputStream output(&bytes);
    google::protobuf::io::CodedOutputStream  coded(&output);
    coded.SetSerializationDeterministic(true);
    return message.SerializeToCodedStream(&coded) && !coded.HadError();
}

bool hasUnknownProtoFields(const google::protobuf::Message& message) {
    const auto* reflection = message.GetReflection();
    if (!reflection->GetUnknownFields(message).empty()) return true;
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    reflection->ListFields(message, &fields);
    for (const auto* field : fields) {
        if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) continue;
        if (field->is_repeated()) {
            for (int index = 0; index < reflection->FieldSize(message, field); ++index)
                if (hasUnknownProtoFields(reflection->GetRepeatedMessage(message, field, index))) return true;
        } else if (hasUnknownProtoFields(reflection->GetMessage(message, field))) {
            return true;
        }
    }
    return false;
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

eve::LogicalId policySchema() {
    const auto schema = eve::LogicalId::parse("policyregistry:registry");
    if (!schema) std::terminate();
    return *schema;
}

const eve::SnapshotMigrationChain& policyMigrations() {
    static const eve::SnapshotMigrationChain chain = [] {
        eve::SnapshotMigrationChain result;
        const auto                  registration =
            result.add(policySchema(), eve::SchemaVersion(0), eve::SchemaVersion(1),
                       [](const eve::Value& payload) -> eve::Result<eve::Value> {
                           const auto* object = payload.getIf<eve::Value::Object>();
                           if (!object)
                               return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                                   eve::DiagnosticCode::ParseError, "policy snapshot payload must be an object"));
                           eve::Value::Object migrated = *object;
                           migrated["version"]         = eve::Value(std::int64_t(1));
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
        return eve::Result<PolicyDescriptor>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "policy domain and name must not be empty"));
    if (version <= 0)
        return eve::Result<PolicyDescriptor>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "policy schema version must be positive"));

    ImplementationKind kind = ImplementationKind::Builtin;
    if (!parseImplementationKind(kindName, kind))
        return eve::Result<PolicyDescriptor>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "invalid implementation kind"));

    auto metadata = eve::Value::fromJson(metadataJson);
    if (!metadata.ok()) return eve::Result<PolicyDescriptor>::failure(metadata.status());
    auto metadataValue = std::move(metadata).takeValue();
    if (!metadataValue.isObject())
        return eve::Result<PolicyDescriptor>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "policy metadata must be a JSON object"));
    auto canonical = metadataValue.toJson();
    if (!canonical.ok()) return eve::Result<PolicyDescriptor>::failure(canonical.status());

    if (!schemaId.empty() && eve::schema::SchemaRegistry::resolve(schemaId, version)) {
        const auto errors = eve::schema::SchemaRegistry::validate(schemaId, version, metadataJson);
        if (!errors.empty()) {
            const auto& error = errors.front();
            return eve::Result<PolicyDescriptor>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, error.message, error.path,
                eve::DiagnosticDetails{
                    {"schemaId", schemaId}, {"schemaVersion", std::to_string(version)}, {"validationCode", error.code}},
                "policyregistry.schema"));
        }
    }

    PolicyDescriptor descriptor;
    descriptor.domain       = domain;
    descriptor.name         = name;
    descriptor.version      = eve::SchemaVersion(static_cast<std::uint64_t>(version));
    descriptor.priority     = priority;
    descriptor.enabled      = enabled;
    descriptor.kind         = kind;
    descriptor.schemaId     = schemaId;
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
    result.domain     = event.key.first;
    result.policyName = event.key.second;
    result.version    = event.data.version;
    result.generation = event.generation;
    result.enabled    = event.data.enabled;
    return result;
}

PolicyHandle PolicyRegistry::projectHandle(const Storage::Handle& handle) {
    return PolicyHandle{handle.key.first, handle.key.second, handle.generation};
}

PolicyRegistry::Storage::Handle PolicyRegistry::storageHandle(const PolicyHandle& handle) {
    return Storage::Handle{Key{handle.domain, handle.name}, handle.generation};
}

void PolicyRegistry::clearEventProjection() const { eventProjection_.clear(); }

eve::Result<PolicyHandle> PolicyRegistry::insert(const std::string& domain, const std::string& name, int version,
                                                 int priority, bool enabled, const std::string& kind,
                                                 const std::string& schemaId, const std::string& metadataJson) {
    auto descriptor = makeDescriptor(domain, name, version, priority, enabled, kind, schemaId, metadataJson);
    if (!descriptor.ok()) {
        return eve::Result<PolicyHandle>::failure(descriptor.status());
    }
    PolicyDescriptor value = std::move(descriptor).takeValue();
    const EventData  data{value.version, value.enabled};
    auto             result = storage_.insert(Key{domain, name}, std::move(value), data);
    if (!result.ok()) {
        return eve::Result<PolicyHandle>::failure(result.status());
    }
    const eve::Status mutationStatus = result.status();
    const auto        handle         = std::move(result).takeValue();
    clearEventProjection();
    if (const auto next = revision_.incremented()) revision_ = *next;
    return eve::Result<PolicyHandle>::success(projectHandle(handle), mutationStatus);
}

eve::Result<PolicyHandle> PolicyRegistry::replace(const std::string& domain, const std::string& name, int version,
                                                  int priority, bool enabled, const std::string& kind,
                                                  const std::string& schemaId, const std::string& metadataJson) {
    auto descriptor = makeDescriptor(domain, name, version, priority, enabled, kind, schemaId, metadataJson);
    if (!descriptor.ok()) {
        return eve::Result<PolicyHandle>::failure(descriptor.status());
    }
    PolicyDescriptor value = std::move(descriptor).takeValue();
    const EventData  data{value.version, value.enabled};
    auto             result = storage_.replace(Key{domain, name}, std::move(value), data);
    if (!result.ok()) {
        return eve::Result<PolicyHandle>::failure(result.status());
    }
    const eve::Status mutationStatus = result.status();
    const auto        handle         = std::move(result).takeValue();
    clearEventProjection();
    if (const auto next = revision_.incremented()) revision_ = *next;
    return eve::Result<PolicyHandle>::success(projectHandle(handle), mutationStatus);
}

eve::Result<PolicyHandle> PolicyRegistry::remove(const std::string& domain, const std::string& name) {
    const Key key{domain, name};
    auto      current = storage_.resolve(key);
    if (!current.ok()) {
        return eve::Result<PolicyHandle>::failure(current.status());
    }
    const auto&     descriptor = current.value().get();
    const EventData data{descriptor.version, descriptor.enabled};
    auto            result = storage_.remove(key, data);
    if (!result.ok()) {
        return eve::Result<PolicyHandle>::failure(result.status());
    }
    const eve::Status mutationStatus = result.status();
    const auto        handle         = std::move(result).takeValue();
    clearEventProjection();
    if (const auto next = revision_.incremented()) revision_ = *next;
    return eve::Result<PolicyHandle>::success(projectHandle(handle), mutationStatus);
}

eve::Result<PolicyHandle> PolicyRegistry::enable(const std::string& domain, const std::string& name, bool enabled) {
    const Key key{domain, name};
    auto      current = storage_.resolve(key);
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
    auto            result = storage_.replace(key, std::move(value), data, "enabled");
    if (!result.ok()) {
        return eve::Result<PolicyHandle>::failure(result.status());
    }
    const eve::Status mutationStatus = result.status();
    const auto        handle         = std::move(result).takeValue();
    clearEventProjection();
    if (const auto next = revision_.incremented()) revision_ = *next;
    return eve::Result<PolicyHandle>::success(projectHandle(handle), mutationStatus);
}

eve::ResultRef<const PolicyDescriptor> PolicyRegistry::resolve(const std::string& domain,
                                                               const std::string& name) const {
    auto result = storage_.resolve(Key{domain, name});
    if (!result.ok()) return eve::ResultRef<const PolicyDescriptor>::failure(result.status());
    return eve::ResultRef<const PolicyDescriptor>::success(std::cref(result.value().get()));
}

eve::ResultRef<const PolicyDescriptor> PolicyRegistry::resolveHandle(const PolicyHandle& handle) const {
    auto result = storage_.resolve(storageHandle(handle));
    if (!result.ok()) return eve::ResultRef<const PolicyDescriptor>::failure(result.status());
    return eve::ResultRef<const PolicyDescriptor>::success(std::cref(result.value().get()));
}

eve::Result<PolicyHandle> PolicyRegistry::handle(const std::string& domain, const std::string& name) const {
    auto result = storage_.handle(Key{domain, name});
    if (!result.ok()) return eve::Result<PolicyHandle>::failure(result.status());
    return eve::Result<PolicyHandle>::success(projectHandle(result.value()));
}

eve::Result<eve::Generation> PolicyRegistry::generationOf(const std::string& domain, const std::string& name) const {
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

eve::Result<std::string> PolicyRegistry::snapshotBinary() const {
    const auto                state = storage_.snapshotState();
    proto::PolicyRegistrySave save;
    save.set_schema("policyregistry:registry");
    save.set_version(2);
    save.set_next_event_sequence(state.nextEventSequence.value());
    for (const auto& [key, entry] : state.entries) {
        auto* slot = save.add_slots();
        slot->set_domain(key.first);
        slot->set_name(key.second);
        slot->set_generation(entry.generation.value());
        if (!entry.value) continue;
        const auto& descriptor = *entry.value;
        auto*       item       = slot->mutable_policy();
        item->set_version(descriptor.version.value());
        item->set_priority(descriptor.priority);
        item->set_enabled(descriptor.enabled);
        switch (descriptor.kind) {
            case ImplementationKind::Builtin: item->set_kind(proto::Descriptor::BUILTIN); break;
            case ImplementationKind::Script: item->set_kind(proto::Descriptor::SCRIPT); break;
            case ImplementationKind::Batch: item->set_kind(proto::Descriptor::BATCH); break;
        }
        item->set_schema_id(descriptor.schemaId);
        if (!google::protobuf::util::JsonStringToMessage(descriptor.metadataJson, item->mutable_metadata()).ok())
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "invalid retained policy metadata"));
    }
    for (const auto& event : state.events) {
        auto* item = save.add_events();
        item->set_sequence(event.sequence.value());
        item->set_domain(event.key.first);
        item->set_name(event.key.second);
        item->set_generation(event.generation.value());
        switch (event.operation) {
            case eve::RegistryOperation::Insert: item->set_operation(proto::Mutation::INSERT); break;
            case eve::RegistryOperation::Replace: item->set_operation(proto::Mutation::REPLACE); break;
            case eve::RegistryOperation::Remove: item->set_operation(proto::Mutation::REMOVE); break;
        }
        item->set_tombstone(event.tombstone);
        item->set_label(event.label);
        item->set_version(event.data.version.value());
        item->set_enabled(event.data.enabled);
    }
    std::string bytes;
    if (!serializeDeterministic(save, bytes))
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "policy Protobuf serialization failed"));
    return eve::Result<std::string>::success(std::move(bytes));
}

eve::Result<void> PolicyRegistry::restoreBinary(std::string_view bytes) {
    const auto invalid = [](std::string message) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError, std::move(message)));
    };
    proto::PolicyRegistrySave save;
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !save.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())) || hasUnknownProtoFields(save) ||
        !save.has_schema() || save.schema() != "policyregistry:registry" || !save.has_version() ||
        save.version() != 2 || !save.has_next_event_sequence() || save.next_event_sequence() == 0)
        return invalid("invalid policy Protobuf snapshot");

    Storage::State candidate;
    for (const auto& slot : save.slots()) {
        if (!slot.has_domain() || slot.domain().empty() || !slot.has_name() || slot.name().empty() ||
            !slot.has_generation() || slot.generation() == 0)
            return invalid("invalid policy slot");
        Storage::Entry entry{eve::Generation(slot.generation()), std::nullopt};
        if (slot.has_policy()) {
            const auto& item = slot.policy();
            if (!item.has_version() || item.version() == 0 || !item.has_priority() || !item.has_enabled() ||
                !item.has_kind() || !item.has_schema_id() || !item.has_metadata())
                return invalid("incomplete policy descriptor");
            PolicyDescriptor descriptor;
            switch (item.kind()) {
                case proto::Descriptor::BUILTIN: descriptor.kind = ImplementationKind::Builtin; break;
                case proto::Descriptor::SCRIPT: descriptor.kind = ImplementationKind::Script; break;
                case proto::Descriptor::BATCH: descriptor.kind = ImplementationKind::Batch; break;
                default: return invalid("invalid policy implementation kind");
            }
            descriptor.domain     = slot.domain();
            descriptor.name       = slot.name();
            descriptor.version    = eve::SchemaVersion(item.version());
            descriptor.priority   = item.priority();
            descriptor.enabled    = item.enabled();
            descriptor.schemaId   = item.schema_id();
            descriptor.generation = eve::Generation(slot.generation());
            std::string metadataJson;
            if (!google::protobuf::util::MessageToJsonString(item.metadata(), &metadataJson).ok())
                return invalid("invalid policy metadata");
            auto metadata = eve::Value::fromJson(metadataJson);
            if (!metadata.ok() || !metadata.value().isObject()) return invalid("invalid policy metadata object");
            auto canonical = metadata.value().toJson();
            if (!canonical.ok()) return eve::Result<void>::failure(canonical.status());
            descriptor.metadataJson = std::move(canonical).takeValue();
            entry.value             = std::move(descriptor);
        }
        if (!candidate.entries.emplace(Key{slot.domain(), slot.name()}, std::move(entry)).second)
            return invalid("duplicate policy slot");
    }

    eve::EventSequence previous;
    for (const auto& item : save.events()) {
        if (!item.has_sequence() || item.sequence() == 0 || eve::EventSequence(item.sequence()) <= previous ||
            !item.has_domain() || item.domain().empty() || !item.has_name() || item.name().empty() ||
            !item.has_generation() || item.generation() == 0 || !item.has_operation() || !item.has_tombstone() ||
            !item.has_label() || !item.has_version() || item.version() == 0 || !item.has_enabled())
            return invalid("invalid policy event");
        Storage::Event event;
        switch (item.operation()) {
            case proto::Mutation::INSERT: event.operation = eve::RegistryOperation::Insert; break;
            case proto::Mutation::REPLACE: event.operation = eve::RegistryOperation::Replace; break;
            case proto::Mutation::REMOVE: event.operation = eve::RegistryOperation::Remove; break;
            default: return invalid("invalid policy mutation operation");
        }
        if (item.tombstone() != (event.operation == eve::RegistryOperation::Remove) ||
            (!item.label().empty() &&
             (item.label() != "enabled" || event.operation != eve::RegistryOperation::Replace)))
            return invalid("inconsistent policy event");
        event.sequence     = eve::EventSequence(item.sequence());
        event.key          = Key{item.domain(), item.name()};
        event.generation   = eve::Generation(item.generation());
        event.tombstone    = item.tombstone();
        event.label        = item.label();
        event.data.version = eve::SchemaVersion(item.version());
        event.data.enabled = item.enabled();
        candidate.events.push_back(std::move(event));
        previous = eve::EventSequence(item.sequence());
    }
    if (!previous.isZero() && eve::EventSequence(save.next_event_sequence()) <= previous)
        return invalid("invalid policy next event sequence");
    candidate.nextEventSequence = eve::EventSequence(save.next_event_sequence());
    auto restored               = storage_.restoreState(std::move(candidate));
    if (!restored.ok()) return eve::Result<void>::failure(restored.status());
    clearEventProjection();
    revision_ = eve::Revision(save.next_event_sequence() - 1);
    tick_     = eve::SimulationTick{};
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::string> PolicyRegistry::snapshotEnvelopeBinary(const eve::SnapshotHashProvider& hashProvider) const {
    if (!hashProvider)
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "policy snapshot requires a hash provider"));
    auto payload = snapshotBinary();
    if (!payload.ok()) return eve::Result<std::string>::failure(payload.status());
    proto::PolicyRegistryEnvelope envelope;
    envelope.set_type("policyregistry.registry");
    envelope.set_schema("policyregistry:registry");
    envelope.set_version(2);
    envelope.set_instance_id(instanceId_.format());
    envelope.set_revision(revision_.value());
    envelope.set_tick(tick_.value());
    envelope.set_payload_wire(std::move(payload).takeValue());
    std::string hashInput;
    if (!serializeDeterministic(envelope, hashInput))
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "policy envelope serialization failed"));
    auto digest = hashProvider(hashInput);
    if (!digest.ok()) return eve::Result<std::string>::failure(digest.status());
    const auto digestBytes = digest.value().bytes();
    envelope.set_digest(std::string(reinterpret_cast<const char*>(digestBytes.data()), digestBytes.size()));
    std::string wire;
    if (!serializeDeterministic(envelope, wire))
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "policy envelope serialization failed"));
    return eve::Result<std::string>::success(std::move(wire));
}

eve::Result<void> PolicyRegistry::restoreSnapshotBinary(std::string_view                 bytes,
                                                        const eve::SnapshotHashProvider& hashProvider) {
    if (!hashProvider)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "policy restore requires a hash provider"));
    proto::PolicyRegistryEnvelope envelope;
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !envelope.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())) || hasUnknownProtoFields(envelope) ||
        !envelope.has_type() || envelope.type() != "policyregistry.registry" || !envelope.has_schema() ||
        envelope.schema() != "policyregistry:registry" || !envelope.has_version() || !envelope.has_instance_id() ||
        !envelope.has_revision() || !envelope.has_tick() || !envelope.has_payload_wire() || !envelope.has_digest())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid policy Protobuf envelope"));
    if (envelope.version() != 2)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::UnknownVersion, "unsupported policy envelope version"));
    const auto identity = eve::PersistentId::parse(envelope.instance_id());
    if (!identity || (!instanceId_.isNil() && *identity != instanceId_))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "policy snapshot identity differs from target"));
    if (envelope.payload_wire().empty() || envelope.digest().size() != eve::ContentId::Bytes{}.size())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "incomplete policy Protobuf envelope"));
    const std::string expectedDigest = envelope.digest();
    envelope.clear_digest();
    std::string hashInput;
    if (!serializeDeterministic(envelope, hashInput))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "policy envelope serialization failed"));
    auto digest = hashProvider(hashInput);
    if (!digest.ok()) return eve::Result<void>::failure(digest.status());
    const auto digestBytes = digest.value().bytes();
    if (expectedDigest != std::string(reinterpret_cast<const char*>(digestBytes.data()), digestBytes.size()))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::HashMismatch, "policy snapshot digest differs from envelope"));
    auto restored = restoreBinary(envelope.payload_wire());
    if (!restored.ok()) return eve::Result<void>::failure(restored.status());
    instanceId_ = *identity;
    revision_   = eve::Revision(envelope.revision());
    tick_       = eve::SimulationTick(envelope.tick());
    return eve::Result<void>::success();
}

std::string PolicyRegistry::snapshotJson() const {
    auto bytes = snapshotBinary();
    if (!bytes.ok()) return {};
    proto::PolicyRegistrySave save;
    if (!save.ParseFromString(bytes.value())) return {};
    std::string json;
    return google::protobuf::util::MessageToJsonString(save, &json).ok() ? json : std::string{};
}

eve::Result<void> PolicyRegistry::restoreJson(const std::string& json) {
    proto::PolicyRegistrySave save;
    if (google::protobuf::util::JsonStringToMessage(json, &save).ok() && save.has_schema()) {
        std::string bytes;
        if (!serializeDeterministic(save, bytes))
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "policy JSON serialization failed"));
        return restoreBinary(bytes);
    }

    // The version-1 Value shape remains a read-only migration source.
    auto parsed = eve::Value::fromJson(json);
    if (!parsed.ok()) {
        return eve::Result<void>::failure(parsed.status());
    }
    const eve::Value value = std::move(parsed).takeValue();
    const auto*      root  = value.getIf<eve::Value::Object>();
    if (!root) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "policy snapshot must be an object"));
    }
    const auto*   snapshotVersion   = field(*root, "version");
    const auto*   versionNumber     = snapshotVersion ? snapshotVersion->getIf<std::int64_t>() : nullptr;
    const auto*   descriptorValue   = field(*root, "descriptors");
    const auto*   generationValue   = field(*root, "generations");
    const auto*   eventValue        = field(*root, "events");
    const auto*   nextSequenceValue = field(*root, "nextEventSequence");
    const auto*   descriptorArray   = descriptorValue ? descriptorValue->getIf<eve::Value::Array>() : nullptr;
    const auto*   generationArray   = generationValue ? generationValue->getIf<eve::Value::Array>() : nullptr;
    const auto*   eventArray        = eventValue ? eventValue->getIf<eve::Value::Array>() : nullptr;
    std::uint64_t nextSequence      = 0;
    if (!versionNumber || *versionNumber != 1 || !descriptorArray || !generationArray || !eventArray ||
        !nextSequenceValue || !readUint64String(*nextSequenceValue, nextSequence) || nextSequence == 0) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid policy snapshot fields"));
    }

    Storage::State candidate;
    for (std::size_t index = 0; index < generationArray->size(); ++index) {
        const auto*   item = (*generationArray)[index].getIf<eve::Value::Object>();
        std::string   domain;
        std::string   name;
        std::uint64_t generation      = 0;
        const auto*   generationField = item ? field(*item, "generation") : nullptr;
        if (!item || !readString(*item, "domain", domain) || !readString(*item, "name", name) || domain.empty() ||
            name.empty() || !generationField || !readUint64String(*generationField, generation) || generation == 0 ||
            !candidate.entries.emplace(Key{domain, name}, Storage::Entry{eve::Generation(generation), std::nullopt})
                 .second) {
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "invalid policy generation at index " + std::to_string(index)));
        }
    }

    for (std::size_t index = 0; index < descriptorArray->size(); ++index) {
        const auto*        item = (*descriptorArray)[index].getIf<eve::Value::Object>();
        std::string        domain;
        std::string        name;
        std::string        kindName;
        std::string        schemaId;
        eve::SchemaVersion version;
        int                priority        = 0;
        std::uint64_t      generation      = 0;
        bool               enabled         = false;
        const auto*        versionField    = item ? field(*item, "version") : nullptr;
        const auto*        priorityField   = item ? field(*item, "priority") : nullptr;
        const auto*        enabledField    = item ? field(*item, "enabled") : nullptr;
        const auto*        generationField = item ? field(*item, "generation") : nullptr;
        const auto*        metadata        = item ? field(*item, "metadata") : nullptr;
        ImplementationKind kind            = ImplementationKind::Builtin;
        if (!item || !readString(*item, "domain", domain) || !readString(*item, "name", name) || domain.empty() ||
            name.empty() || !versionField || !readPositiveSchemaVersion(*versionField, version) || !priorityField ||
            !readInt(*priorityField, priority) || !enabledField || !enabledField->isBool() || !generationField ||
            !readUint64String(*generationField, generation) || generation == 0 ||
            !readString(*item, "kind", kindName) || !parseImplementationKind(kindName, kind) || !metadata ||
            !metadata->isObject() || !readString(*item, "schemaId", schemaId)) {
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "invalid policy descriptor at index " + std::to_string(index)));
        }
        enabled    = *enabledField->getIf<bool>();
        auto entry = candidate.entries.find(Key{domain, name});
        if (entry == candidate.entries.end() || entry->second.generation != eve::Generation(generation) ||
            entry->second.value.has_value()) {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                       "inconsistent policy generation at index " + std::to_string(index)));
        }
        auto metadataJson = metadata->toJson();
        if (!metadataJson.ok()) {
            return eve::Result<void>::failure(metadataJson.status());
        }
        PolicyDescriptor descriptor;
        descriptor.domain       = domain;
        descriptor.name         = name;
        descriptor.version      = version;
        descriptor.priority     = priority;
        descriptor.enabled      = enabled;
        descriptor.kind         = kind;
        descriptor.schemaId     = schemaId;
        descriptor.generation   = eve::Generation(generation);
        descriptor.metadataJson = std::move(metadataJson).takeValue();
        entry->second.value     = std::move(descriptor);
    }

    eve::EventSequence previousSequence;
    for (std::size_t index = 0; index < eventArray->size(); ++index) {
        const auto*        item = (*eventArray)[index].getIf<eve::Value::Object>();
        std::string        name;
        std::string        domain;
        std::string        policyName;
        std::uint64_t      sequence        = 0;
        std::uint64_t      generation      = 0;
        const auto*        sequenceField   = item ? field(*item, "sequence") : nullptr;
        const auto*        generationField = item ? field(*item, "generation") : nullptr;
        const auto*        enabledField    = item ? field(*item, "enabled") : nullptr;
        const auto*        versionField    = item ? field(*item, "version") : nullptr;
        eve::SchemaVersion version{1};
        if (!item || !readString(*item, "name", name) || !readString(*item, "domain", domain) ||
            !readString(*item, "policyName", policyName) || domain.empty() || policyName.empty() ||
            (name != "policy_registered" && name != "policy_replaced" && name != "policy_removed" &&
             name != "policy_enabled") ||
            !sequenceField || !readUint64String(*sequenceField, sequence) || sequence == 0 ||
            (!previousSequence.isZero() && eve::EventSequence(sequence) <= previousSequence) || !generationField ||
            !readUint64String(*generationField, generation) || generation == 0 || !enabledField ||
            !enabledField->isBool()) {
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "invalid policy event at index " + std::to_string(index)));
        }
        if (versionField && !readPositiveSchemaVersion(*versionField, version)) {
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "invalid policy event version at index " + std::to_string(index)));
        }
        Storage::Event event;
        event.sequence     = eve::EventSequence(sequence);
        event.operation    = name == "policy_removed"
                                 ? eve::RegistryOperation::Remove
                                 : (generation == 1 ? eve::RegistryOperation::Insert : eve::RegistryOperation::Replace);
        event.key          = Key{domain, policyName};
        event.generation   = eve::Generation(generation);
        event.tombstone    = name == "policy_removed";
        event.label        = name == "policy_enabled" ? "enabled" : std::string{};
        event.data.version = version;
        event.data.enabled = *enabledField->getIf<bool>();
        candidate.events.push_back(std::move(event));
        previousSequence = eve::EventSequence(sequence);
    }
    if (!previousSequence.isZero() && eve::EventSequence(nextSequence) <= previousSequence) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                                                 "next event sequence must exceed retained events"));
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

eve::Result<eve::SnapshotEnvelope> PolicyRegistry::snapshot(const eve::SnapshotHashProvider& hashProvider) const {
    auto payload = eve::Value::fromJson(snapshotJson());
    if (!payload.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(payload.status());
    return eve::makeSnapshotEnvelope("policyregistry.registry", policySchema(), eve::SchemaVersion(2), instanceId_,
                                     revision_, tick_, std::move(payload).takeValue(), hashProvider);
}

eve::Result<void> PolicyRegistry::restoreSnapshot(const eve::SnapshotEnvelope&     source,
                                                  const eve::SnapshotHashProvider& hashProvider) {
    if (source.type != "policyregistry.registry" || source.schema != policySchema())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "snapshot does not belong to policyregistry::PolicyRegistry"));
    if (!instanceId_.isNil() && source.instanceId != instanceId_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "snapshot instanceId does not match policyregistry::PolicyRegistry"));
    auto migrated =
        policyMigrations().migrate(source, eve::SchemaVersion(source.schemaVersion.value() == 2 ? 2 : 1), hashProvider);
    if (!migrated.ok()) return eve::Result<void>::failure(migrated.status());
    auto payload = migrated.value().payload.toJson();
    if (!payload.ok()) return eve::Result<void>::failure(payload.status());
    auto restored = restoreJson(std::move(payload).takeValue());
    if (!restored.ok()) return eve::Result<void>::failure(restored.status());
    const auto& candidate = migrated.value();
    instanceId_           = candidate.instanceId;
    revision_             = candidate.revision;
    tick_                 = candidate.tick;
    return eve::Result<void>::success();
}

eve::Result<std::string> PolicyRegistry::snapshotEnvelopeJson(const eve::SnapshotHashProvider& hashProvider) const {
    auto bytes = snapshotEnvelopeBinary(hashProvider);
    if (!bytes.ok()) return eve::Result<std::string>::failure(bytes.status());
    proto::PolicyRegistryEnvelope envelope;
    if (!envelope.ParseFromString(bytes.value()))
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "policy envelope decoding failed"));
    proto::PolicyRegistryJsonDocument document;
    document.set_type(envelope.type());
    document.set_schema(envelope.schema());
    document.set_version(envelope.version());
    document.set_instance_id(envelope.instance_id());
    document.set_revision(envelope.revision());
    document.set_tick(envelope.tick());
    document.set_digest(envelope.digest());
    if (!document.mutable_payload()->ParseFromString(envelope.payload_wire()))
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "policy payload decoding failed"));
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(document, &json).ok())
        return eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::SerializationError,
                                                                        "policy envelope JSON serialization failed"));
    return eve::Result<std::string>::success(std::move(json));
}

eve::Result<void> PolicyRegistry::restoreSnapshotJson(std::string_view                 json,
                                                      const eve::SnapshotHashProvider& hashProvider) {
    proto::PolicyRegistryJsonDocument document;
    if (google::protobuf::util::JsonStringToMessage(std::string(json), &document).ok() && document.has_type()) {
        if (hasUnknownProtoFields(document) || !document.has_schema() || !document.has_version() ||
            !document.has_instance_id() || !document.has_revision() || !document.has_tick() ||
            !document.has_payload() || !document.has_digest())
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "incomplete policy Protobuf JSON envelope"));
        proto::PolicyRegistryEnvelope envelope;
        envelope.set_type(document.type());
        envelope.set_schema(document.schema());
        envelope.set_version(document.version());
        envelope.set_instance_id(document.instance_id());
        envelope.set_revision(document.revision());
        envelope.set_tick(document.tick());
        envelope.set_digest(document.digest());
        if (!serializeDeterministic(document.payload(), *envelope.mutable_payload_wire()))
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "policy payload serialization failed"));
        std::string wire;
        if (!serializeDeterministic(envelope, wire))
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::SerializationError,
                                                                     "policy envelope serialization failed"));
        return restoreSnapshotBinary(wire, hashProvider);
    }
    // Older common Value envelopes are decoded only for migration.
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
    const HSQUIRRELVM vm         = table.getHandle();
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
    event.addFunc("getSequence",
                  [](PolicyEvent* v) { return v ? static_cast<std::int64_t>(v->sequence.value()) : std::int64_t{0}; });
    event.addFunc("getName", [](PolicyEvent* v) { return v ? v->name : std::string{}; });
    event.addFunc("getDomain", [](PolicyEvent* v) { return v ? v->domain : std::string{}; });
    event.addFunc("getPolicyName", [](PolicyEvent* v) { return v ? v->policyName : std::string{}; });
    event.addFunc("getVersion",
                  [](PolicyEvent* v) { return v ? static_cast<std::int64_t>(v->version.value()) : std::int64_t{0}; });
    event.addFunc("getGeneration", [](PolicyEvent* v) {
        return v ? static_cast<std::int64_t>(v->generation.value()) : std::int64_t{0};
    });
    event.addFunc("isEnabled", [](PolicyEvent* v) { return v && v->enabled; });

    auto registry = table.addClass<PolicyRegistry>(
        "PolicyRegistry", std::function<PolicyRegistry*()>([]() -> PolicyRegistry* { return nullptr; }), false);
    registry.addFunc("insert", [vm](PolicyRegistry* value, const std::string& domain, const std::string& policyName,
                                    int version, int priority, bool enabled, const std::string& kind,
                                    const std::string& schemaId, const std::string& metadataJson) {
        if (!value)
            return eve::script::projectResult(
                vm,
                eve::Result<PolicyHandle>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "policy registry must not be null",
                                                                          "registry", {}, "policyregistry")),
                [](PolicyHandle&& handle) { return policyHandleValue(handle); });
        return eve::script::projectResult(
            vm, value->insert(domain, policyName, version, priority, enabled, kind, schemaId, metadataJson),
            [](PolicyHandle&& handle) { return policyHandleValue(handle); });
    });
    registry.addFunc("replace", [vm](PolicyRegistry* value, const std::string& domain, const std::string& policyName,
                                     int version, int priority, bool enabled, const std::string& kind,
                                     const std::string& schemaId, const std::string& metadataJson) {
        if (!value)
            return eve::script::projectResult(
                vm,
                eve::Result<PolicyHandle>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "policy registry must not be null",
                                                                          "registry", {}, "policyregistry")),
                [](PolicyHandle&& handle) { return policyHandleValue(handle); });
        return eve::script::projectResult(
            vm, value->replace(domain, policyName, version, priority, enabled, kind, schemaId, metadataJson),
            [](PolicyHandle&& handle) { return policyHandleValue(handle); });
    });
    registry.addFunc("remove", [vm](PolicyRegistry* value, const std::string& domain, const std::string& policyName) {
        if (!value)
            return eve::script::projectResult(
                vm,
                eve::Result<PolicyHandle>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "policy registry must not be null",
                                                                          "registry", {}, "policyregistry")),
                [](PolicyHandle&& handle) { return policyHandleValue(handle); });
        return eve::script::projectResult(vm, value->remove(domain, policyName),
                                          [](PolicyHandle&& handle) { return policyHandleValue(handle); });
    });
    registry.addFunc(
        "enable", [vm](PolicyRegistry* value, const std::string& domain, const std::string& policyName, bool enabled) {
            if (!value)
                return eve::script::projectResult(
                    vm,
                    eve::Result<PolicyHandle>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                              "policy registry must not be null",
                                                                              "registry", {}, "policyregistry")),
                    [](PolicyHandle&& handle) { return policyHandleValue(handle); });
            return eve::script::projectResult(vm, value->enable(domain, policyName, enabled),
                                              [](PolicyHandle&& handle) { return policyHandleValue(handle); });
        });
    registry.addFunc("resolve", [vm](PolicyRegistry* value, const std::string& domain, const std::string& policyName) {
        if (!value)
            return eve::script::projectResult(
                vm,
                eve::Result<std::reference_wrapper<const PolicyDescriptor>>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "policy registry must not be null",
                                           "registry", {}, "policyregistry")),
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
                vm,
                eve::Result<DescriptorRef>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                           "policy registry must not be null",
                                                                           "registry", {}, "policyregistry")),
                [](DescriptorRef&& descriptor) { return policyDescriptorValue(descriptor.get()); });
        if (generation <= 0)
            return eve::script::projectResult(
                vm,
                eve::Result<DescriptorRef>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                           "policy generation must be positive",
                                                                           "generation", {}, "policyregistry")),
                [](DescriptorRef&& descriptor) { return policyDescriptorValue(descriptor.get()); });
        return eve::script::projectResult(
            vm,
            value->resolveHandle(
                PolicyHandle{domain, policyName, eve::Generation(static_cast<std::uint64_t>(generation))}),
            [](DescriptorRef&& descriptor) { return policyDescriptorValue(descriptor.get()); });
    });
    registry.addFunc("handle", [vm](PolicyRegistry* value, const std::string& domain, const std::string& policyName) {
        if (!value)
            return eve::script::projectResult(
                vm,
                eve::Result<PolicyHandle>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "policy registry must not be null",
                                                                          "registry", {}, "policyregistry")),
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
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "policy registry must not be null", "registry",
                                                                      {}, "policyregistry")));
        return eve::script::projectResult(vm, value->restoreJson(json));
    });
    registry.addFunc("generationOf", [vm](PolicyRegistry* value, const std::string& domain,
                                          const std::string& policyName) {
        if (!value)
            return eve::script::projectResult(
                vm,
                eve::Result<eve::Generation>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                             "policy registry must not be null",
                                                                             "registry", {}, "policyregistry")),
                [](eve::Generation generation) { return eve::Value(static_cast<std::int64_t>(generation.value())); });
        return eve::script::projectResult(vm, value->generationOf(domain, policyName), [](eve::Generation generation) {
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
