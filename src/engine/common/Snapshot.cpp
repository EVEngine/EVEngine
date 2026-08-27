#include "common/Snapshot.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>

namespace eve {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

bool hasOnlyEnvelopeFields(const Value::Object& object) {
    static const std::set<std::string> fields = {
        "contentHash", "instanceId", "payload", "revision", "schema", "schemaVersion", "tick", "type"};
    for (const auto& entry : object)
        if (!fields.contains(entry.first)) return false;
    return true;
}

Result<std::uint64_t> readUint64(const Value& value, std::string_view field) {
    const auto* text = value.getIf<std::string>();
    if (!text || text->empty())
        return failure<std::uint64_t>(DiagnosticCode::ParseError, "snapshot integer must be a decimal string",
                                      std::string(field));
    std::uint64_t output = 0;
    const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), output);
    if (error != std::errc{} || end != text->data() + text->size())
        return failure<std::uint64_t>(DiagnosticCode::ParseError, "snapshot integer is out of range",
                                      std::string(field));
    return Result<std::uint64_t>::success(output);
}

Result<std::string> canonicalHashInputFor(const SnapshotEnvelope& snapshot) {
    Value::Object object;
    object.emplace("instanceId", Value(snapshot.instanceId.format()));
    object.emplace("payload", snapshot.payload);
    object.emplace("revision", Value(std::to_string(snapshot.revision.value())));
    object.emplace("schema", Value(snapshot.schema.format()));
    object.emplace("schemaVersion", Value(std::to_string(snapshot.schemaVersion.value())));
    object.emplace("tick", Value(std::to_string(snapshot.tick.value())));
    object.emplace("type", Value(snapshot.type));
    return Value(std::move(object)).toJson();
}

Result<void> validateHeader(const SnapshotEnvelope& snapshot) {
    if (snapshot.type.empty())
        return failure<void>(DiagnosticCode::InvalidArgument, "snapshot type must not be empty", "type");
    if (!snapshot.schema.isValid())
        return failure<void>(DiagnosticCode::InvalidArgument, "snapshot schema must be a valid logical ID", "schema");
    return Result<void>::success();
}

}  // namespace

Result<std::string> snapshotHashInput(const SnapshotEnvelope& snapshot) {
    auto valid = validateHeader(snapshot);
    if (!valid.ok()) return Result<std::string>::failure(valid.status());
    return canonicalHashInputFor(snapshot);
}

Result<void> verifySnapshotEnvelope(const SnapshotEnvelope& snapshot, const SnapshotHashProvider& hashProvider) {
    auto valid = validateHeader(snapshot);
    if (!valid.ok()) return valid;
    if (!hashProvider)
        return failure<void>(DiagnosticCode::Unsupported, "snapshot hash provider is required", "contentHash");

    auto input = snapshotHashInput(snapshot);
    if (!input.ok()) return Result<void>::failure(input.status());
    auto computed = hashProvider(input.value());
    if (!computed.ok()) return Result<void>::failure(computed.status());
    if (std::move(computed).takeValue() != snapshot.contentHash)
        return failure<void>(DiagnosticCode::HashMismatch, "snapshot content hash does not match", "contentHash");
    return Result<void>::success();
}

Result<void> validateSnapshotPayloadMetadata(const Value& payload, Revision revision, SimulationTick tick) {
    const auto* object = payload.getIf<Value::Object>();
    if (!object) return Result<void>::success();

    const auto validate = [&](std::string_view field, std::uint64_t expected) -> Result<void> {
        const auto found = object->find(std::string(field));
        if (found == object->end()) return Result<void>::success();
        auto parsed = readUint64(found->second, field);
        if (!parsed.ok()) return Result<void>::failure(parsed.status());
        if (parsed.value() != expected)
            return failure<void>(DiagnosticCode::Conflict,
                                 "snapshot payload " + std::string(field) +
                                     " disagrees with the envelope",
                                 "payload." + std::string(field));
        return Result<void>::success();
    };

    auto revisionResult = validate("revision", revision.value());
    if (!revisionResult.ok()) return revisionResult;
    auto tickResult = validate("tick", tick.value());
    if (!tickResult.ok()) return tickResult;
    return Result<void>::success();
}

Result<SnapshotEnvelope> makeSnapshotEnvelope(
    std::string type, LogicalId schema, SchemaVersion schemaVersion, PersistentId instanceId,
    Revision revision, SimulationTick tick, Value payload, const SnapshotHashProvider& hashProvider) {
    SnapshotEnvelope snapshot{std::move(type), std::move(schema), schemaVersion, instanceId, revision, tick,
                              ContentId::nil(), std::move(payload)};
    auto valid = validateHeader(snapshot);
    if (!valid.ok()) return Result<SnapshotEnvelope>::failure(valid.status());
    if (!hashProvider)
        return failure<SnapshotEnvelope>(DiagnosticCode::Unsupported, "snapshot hash provider is required",
                                         "contentHash");
    auto input = snapshotHashInput(snapshot);
    if (!input.ok()) return Result<SnapshotEnvelope>::failure(input.status());
    auto hash = hashProvider(input.value());
    if (!hash.ok()) return Result<SnapshotEnvelope>::failure(hash.status());
    snapshot.contentHash = std::move(hash).takeValue();
    return Result<SnapshotEnvelope>::success(std::move(snapshot));
}

Result<Value> snapshotEnvelopeValue(const SnapshotEnvelope& snapshot) {
    auto valid = validateHeader(snapshot);
    if (!valid.ok()) return Result<Value>::failure(valid.status());
    Value::Object object;
    object.emplace("contentHash", Value(snapshot.contentHash.format()));
    object.emplace("instanceId", Value(snapshot.instanceId.format()));
    object.emplace("payload", snapshot.payload);
    object.emplace("revision", Value(std::to_string(snapshot.revision.value())));
    object.emplace("schema", Value(snapshot.schema.format()));
    object.emplace("schemaVersion", Value(std::to_string(snapshot.schemaVersion.value())));
    object.emplace("tick", Value(std::to_string(snapshot.tick.value())));
    object.emplace("type", Value(snapshot.type));
    return Result<Value>::success(Value(std::move(object)));
}

Result<SnapshotEnvelope> parseSnapshotEnvelopeValue(const Value& value, const SnapshotHashProvider& hashProvider) {
    const auto* object = value.getIf<Value::Object>();
    if (!object || !hasOnlyEnvelopeFields(*object))
        return failure<SnapshotEnvelope>(DiagnosticCode::ParseError,
                                         "snapshot envelope must be an object with the exact public fields");

    const auto type = value.getIf<Value::Object>()->find("type");
    const auto schemaText = object->find("schema");
    const auto schemaVersion = object->find("schemaVersion");
    const auto instanceId = object->find("instanceId");
    const auto revision = object->find("revision");
    const auto tick = object->find("tick");
    const auto contentHash = object->find("contentHash");
    const auto payload = object->find("payload");
    if (type == object->end() || schemaText == object->end() || schemaVersion == object->end() ||
        instanceId == object->end() || revision == object->end() || tick == object->end() ||
        contentHash == object->end() || payload == object->end() || !type->second.isString() ||
        !schemaText->second.isString() || !instanceId->second.isString() || !contentHash->second.isString())
        return failure<SnapshotEnvelope>(DiagnosticCode::ParseError, "snapshot envelope has invalid fields");

    const auto schema = LogicalId::parse(*schemaText->second.getIf<std::string>());
    const auto persistentId = PersistentId::parse(*instanceId->second.getIf<std::string>());
    const auto contentId = ContentId::parse(*contentHash->second.getIf<std::string>());
    if (!schema || !persistentId || !contentId)
        return failure<SnapshotEnvelope>(DiagnosticCode::ParseError, "snapshot envelope has invalid identity fields");

    auto version = readUint64(schemaVersion->second, "schemaVersion");
    if (!version.ok()) return Result<SnapshotEnvelope>::failure(version.status());
    auto revisionValue = readUint64(revision->second, "revision");
    if (!revisionValue.ok()) return Result<SnapshotEnvelope>::failure(revisionValue.status());
    auto tickValue = readUint64(tick->second, "tick");
    if (!tickValue.ok()) return Result<SnapshotEnvelope>::failure(tickValue.status());

    SnapshotEnvelope snapshot{*type->second.getIf<std::string>(), *schema,
                              SchemaVersion(std::move(version).takeValue()), *persistentId,
                              Revision(std::move(revisionValue).takeValue()),
                              SimulationTick(std::move(tickValue).takeValue()), *contentId,
                              payload->second};
    auto verified = verifySnapshotEnvelope(snapshot, hashProvider);
    if (!verified.ok()) return Result<SnapshotEnvelope>::failure(verified.status());
    return Result<SnapshotEnvelope>::success(std::move(snapshot));
}

Result<std::string> serializeSnapshotEnvelope(const SnapshotEnvelope& snapshot) {
    auto value = snapshotEnvelopeValue(snapshot);
    if (!value.ok()) return Result<std::string>::failure(value.status());
    return std::move(value).andThen([](Value&& owned) { return owned.toJson(); });
}

Result<SnapshotEnvelope> parseSnapshotEnvelope(
    std::string_view json, const SnapshotHashProvider& hashProvider) {
    auto value = Value::fromJson(json);
    if (!value.ok()) return Result<SnapshotEnvelope>::failure(value.status());
    return parseSnapshotEnvelopeValue(value.value(), hashProvider);
}

Result<void> SnapshotMigrationChain::add(LogicalId schema, SchemaVersion from, SchemaVersion to,
                                         Migration migration) {
    if (!schema.isValid() || from.value() >= to.value() || !migration)
        return failure<void>(DiagnosticCode::InvalidArgument, "invalid snapshot migration edge");
    const Key key{schema.format(), from.value()};
    if (steps_.contains(key))
        return failure<void>(DiagnosticCode::Conflict, "snapshot migration edge already exists");
    steps_.emplace(key, Step{to, std::move(migration)});
    return Result<void>::success();
}

Result<SnapshotEnvelope> SnapshotMigrationChain::migrate(
    SnapshotEnvelope snapshot, SchemaVersion targetVersion, const SnapshotHashProvider& hashProvider) const {
    auto verified = verifySnapshotEnvelope(snapshot, hashProvider);
    if (!verified.ok()) return Result<SnapshotEnvelope>::failure(verified.status());
    if (snapshot.schemaVersion.value() > targetVersion.value())
        return failure<SnapshotEnvelope>(DiagnosticCode::UnknownVersion,
                                         "snapshot schema version is newer than the supported target", "schemaVersion");
    if (snapshot.schemaVersion == targetVersion) return Result<SnapshotEnvelope>::success(std::move(snapshot));

    while (snapshot.schemaVersion.value() < targetVersion.value()) {
        const Key key{snapshot.schema.format(), snapshot.schemaVersion.value()};
        const auto step = steps_.find(key);
        if (step == steps_.end())
            return failure<SnapshotEnvelope>(DiagnosticCode::Unsupported,
                                             "no snapshot migration step reaches the requested version",
                                             "schemaVersion");
        if (step->second.to.value() > targetVersion.value())
            return failure<SnapshotEnvelope>(DiagnosticCode::Unsupported,
                                             "snapshot migration step overshoots the requested version",
                                             "schemaVersion");
        auto payload = step->second.migration(snapshot.payload);
        if (!payload.ok()) return Result<SnapshotEnvelope>::failure(payload.status());
        snapshot.payload = std::move(payload).takeValue();
        snapshot.schemaVersion = step->second.to;
    }

    return makeSnapshotEnvelope(snapshot.type, snapshot.schema, snapshot.schemaVersion, snapshot.instanceId,
                                snapshot.revision, snapshot.tick, std::move(snapshot.payload), hashProvider);
}

}  // namespace eve
