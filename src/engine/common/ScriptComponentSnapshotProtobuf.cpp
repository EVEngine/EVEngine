#include "common/ScriptComponentSnapshot.h"

#include "common/ECS.h"
#include "common/ProtobufDynamicValue.h"
#include "common/ProtobufSnapshot.h"
#include "common/ScriptComponentStore.h"
#include "script_component_store.pb.h"

#include <google/protobuf/message.h>

#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace eve {
namespace {

constexpr std::uint64_t binaryVersion = 2;

Result<void> invalid(std::string message, std::string path = {}) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::ParseError, std::move(message), std::move(path), {}, "ecs.snapshot"));
}

bool hasUnknownFields(const google::protobuf::Message& message) {
    const auto* reflection = message.GetReflection();
    if (!reflection->GetUnknownFields(message).empty()) return true;
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    reflection->ListFields(message, &fields);
    for (const auto* field : fields) {
        if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) continue;
        if (field->is_repeated()) {
            for (int index = 0; index < reflection->FieldSize(message, field); ++index)
                if (hasUnknownFields(reflection->GetRepeatedMessage(message, field, index))) return true;
        } else if (hasUnknownFields(reflection->GetMessage(message, field))) {
            return true;
        }
    }
    return false;
}

}  // namespace

Result<std::string> serializeScriptComponentSnapshotBinary(PersistentId instanceId, Revision revision,
                                                           SimulationTick              tick,
                                                           const SnapshotHashProvider& hashProvider) {
    script::proto::ScriptComponentStoreSave save;
    save.set_format_version(1);
    for (const auto& entry : scriptComponentStoreEntries()) {
        auto* record = save.add_components();
        record->set_entity_id(entry.key.entityId);
        record->set_name(entry.key.name);
        record->set_version(entry.key.version.value());
        if (!entry.values.isObject())
            return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                                  "stored script component fields are not an object",
                                                                  entry.key.name, {}, "ecs.snapshot"));
        auto encoded = proto_support::encodeDynamicValue(entry.values, *record->mutable_fields());
        if (!encoded.ok()) return Result<std::string>::failure(encoded.status());
    }
    std::string payload;
    if (!save.SerializeToString(&payload))
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::SerializationError,
                                                              "script component Protobuf serialization failed", {}, {},
                                                              "ecs.snapshot"));
    return proto_support::serializeBinarySnapshot(
        {kScriptComponentSnapshotType, scriptComponentSnapshotSchema(), SchemaVersion(binaryVersion), instanceId,
         revision, tick, std::move(payload)},
        hashProvider);
}

Result<void> restoreScriptComponentSnapshotBinary(std::string_view bytes, const SnapshotHashProvider& hashProvider) {
    auto envelope = proto_support::parseBinarySnapshot(bytes, hashProvider);
    if (!envelope.ok()) return Result<void>::failure(envelope.status());
    if (envelope.value().type != kScriptComponentSnapshotType ||
        envelope.value().schema != scriptComponentSnapshotSchema())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "snapshot does not belong to the script component store"));
    if (envelope.value().schemaVersion.value() != binaryVersion)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::UnknownVersion,
                                                       "script component Protobuf snapshot version is unsupported"));

    script::proto::ScriptComponentStoreSave save;
    const auto&                             payload = envelope.value().payloadWire;
    if (payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !save.ParseFromArray(payload.data(), static_cast<int>(payload.size())) || hasUnknownFields(save) ||
        !save.has_format_version() || save.format_version() != 1)
        return invalid("script component Protobuf payload is malformed or has unknown fields");

    std::map<ScriptComponentStoreKey, Value> candidate;
    for (const auto& entry : save.components()) {
        if (!entry.has_entity_id() || !entry.has_name() || entry.name().empty() || !entry.has_version() ||
            entry.version() == 0 || !entry.has_fields())
            return invalid("script component Protobuf entry is incomplete", "components");
        const SchemaVersion version(entry.version());
        const auto*         record = findScriptComponent(entry.name(), version);
        if (record == nullptr)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "snapshot describes an unknown script component",
                                  entry.name() + "@" + std::to_string(entry.version()), {}, "ecs.snapshot"));
        auto decoded = proto_support::decodeDynamicValue(entry.fields());
        if (!decoded.ok()) return Result<void>::failure(decoded.status());
        const auto* values = decoded.value().getIf<Value::Object>();
        if (values == nullptr) return invalid("script component fields must be an object", entry.name());
        for (const auto& [field, value] : *values) {
            if (std::find(record->fields.begin(), record->fields.end(), field) == record->fields.end())
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                               "script component field is not in the current schema",
                                                               entry.name() + "." + field, {}, "ecs.snapshot"));
        }
        const ScriptComponentStoreKey key{entry.entity_id(), entry.name(), version};
        if (!candidate.emplace(key, std::move(decoded).takeValue()).second)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                           "script component snapshot has a duplicate entry",
                                                           entry.name(), {}, "ecs.snapshot"));
    }
    std::vector<ScriptComponentStoreEntry> entries;
    entries.reserve(candidate.size());
    for (auto& [key, values] : candidate) entries.push_back({key, std::move(values)});
    replaceScriptComponentValues(std::move(entries));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

}  // namespace eve
