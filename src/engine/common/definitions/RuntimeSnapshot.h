#pragma once

/**
 * @file RuntimeSnapshot.h
 * @brief Shared SnapshotEnvelope codec for typed definition runtime instances.
 *
 * The codec owns only the stable identity/instance-state envelope shape. A
 * domain supplies an encoder and decoder for its typed state. Definitions are
 * intentionally referenced, not copied into this common protocol: the
 * registry remains the sole owner of definition data.
 */

#include "common/Snapshot.h"
#include "common/definitions/DefinitionRuntime.h"

#include <charconv>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace eve::definition {

/** @brief Converts one domain-owned runtime state into an owning Value. */
template <class State>
using RuntimeStateEncoder = std::function<eve::Result<eve::Value>(const State&)>;

/** @brief Decodes one domain-owned runtime state from an owning Value. */
template <class State>
using RuntimeStateDecoder = std::function<eve::Result<State>(const eve::Value&)>;

namespace detail {

inline eve::Result<std::uint64_t> runtimeSnapshotUint(const eve::Value& value, std::string_view path) {
    const auto text = value.getIf<std::string>();
    if (text == nullptr || text->empty())
        return eve::Result<std::uint64_t>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "runtime snapshot integer must be a decimal string",
                                   std::string(path), {}, "common.definitions.snapshot"));
    std::uint64_t result    = 0;
    const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), result);
    if (error != std::errc{} || end != text->data() + text->size())
        return eve::Result<std::uint64_t>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "runtime snapshot integer is out of range",
                                   std::string(path), {}, "common.definitions.snapshot"));
    return eve::Result<std::uint64_t>::success(result);
}

inline eve::Result<void> runtimeSnapshotShape(const eve::Value::Object& object) {
    static const std::set<std::string> fields = {"active", "definition", "definitionGeneration", "instanceId", "state"};
    for (const auto& [name, unused] : object) {
        (void)unused;
        if (!fields.contains(name))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "runtime snapshot payload contains an unknown field", "state." + name,
                {}, "common.definitions.snapshot"));
    }
    for (const auto& name : fields) {
        if (!object.contains(name))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "runtime snapshot payload is missing a required field",
                "state." + name, {}, "common.definitions.snapshot"));
    }
    return eve::Result<void>::success();
}

}  // namespace detail

/**
 * @brief Capture a typed runtime instance in the common snapshot envelope.
 * @param runtime Instance whose identity and state are captured.
 * @param type Stable runtime type name, owned by the domain adapter.
 * @param schema Stable payload schema identifier.
 * @param schemaVersion Exact version emitted by the adapter.
 * @param revision Domain revision associated with this capture.
 * @param tick Deterministic simulation tick associated with this capture.
 * @param hashProvider Explicit content-digest provider.
 * @param encode Domain encoder; it must not retain the state reference.
 * @return A sealed envelope or a structured encoding/validation failure.
 */
template <class State>
[[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshotRuntimeInstance(
    const RuntimeInstance<State>& runtime, std::string type, eve::LogicalId schema, eve::SchemaVersion schemaVersion,
    eve::Revision revision, eve::SimulationTick tick, const eve::SnapshotHashProvider& hashProvider,
    const RuntimeStateEncoder<State>& encode) {
    if (!encode)
        return eve::Result<eve::SnapshotEnvelope>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "runtime snapshot encoder is required",
                                   "encode", {}, "common.definitions.snapshot"));
    auto encoded = encode(runtime.state());
    if (!encoded) return eve::Result<eve::SnapshotEnvelope>::failure(encoded.status());
    eve::Value::Object payload;
    payload.emplace("active", eve::Value(runtime.isActive()));
    payload.emplace("definition", eve::Value(runtime.identity().definition.format()));
    payload.emplace("definitionGeneration",
                    eve::Value(std::to_string(runtime.identity().definitionGeneration.value())));
    payload.emplace("instanceId", eve::Value(runtime.identity().instanceId.format()));
    payload.emplace("state", std::move(encoded).takeValue());
    return eve::makeSnapshotEnvelope(std::move(type), std::move(schema), schemaVersion, runtime.identity().instanceId,
                                     revision, tick, eve::Value(std::move(payload)), hashProvider);
}

/**
 * @brief Verify and atomically restore a typed runtime instance.
 * @param runtime Destination instance; no state is changed on any failure.
 * @param snapshot Candidate common envelope.
 * @param expectedType Exact adapter-owned snapshot type.
 * @param expectedSchema Exact adapter-owned payload schema.
 * @param expectedVersion Only this version is accepted; migration belongs to
 *        the domain adapter and must happen before this function.
 * @param hashProvider Explicit provider used to verify the envelope hash.
 * @param decode Domain decoder; it must return a complete candidate state.
 * @return Success, or a schema/hash/identity/stale/decode failure.
 */
template <class State>
[[nodiscard]] eve::Result<void> restoreRuntimeInstance(
    RuntimeInstance<State>& runtime, const eve::SnapshotEnvelope& snapshot, std::string_view expectedType,
    const eve::LogicalId& expectedSchema, eve::SchemaVersion expectedVersion,
    const eve::SnapshotHashProvider& hashProvider, const RuntimeStateDecoder<State>& decode) {
    if (snapshot.type != expectedType || snapshot.schema != expectedSchema)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "runtime snapshot type or schema does not match",
                                                                 "schema", {}, "common.definitions.snapshot"));
    if (snapshot.schemaVersion != expectedVersion)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            snapshot.schemaVersion > expectedVersion ? eve::DiagnosticCode::UnknownVersion
                                                     : eve::DiagnosticCode::Unsupported,
            "runtime snapshot schema version is not supported", "schemaVersion", {}, "common.definitions.snapshot"));
    auto verified = eve::verifySnapshotEnvelope(snapshot, hashProvider);
    if (!verified) return verified;
    auto metadata = eve::validateSnapshotPayloadMetadata(snapshot.payload, snapshot.revision, snapshot.tick);
    if (!metadata) return metadata;
    const auto object = snapshot.payload.getIf<eve::Value::Object>();
    if (object == nullptr) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                 "runtime snapshot payload must be an object",
                                                                 "payload", {}, "common.definitions.snapshot"));
    }
    auto shape = detail::runtimeSnapshotShape(*object);
    if (!shape) return shape;

    const auto active         = object->at("active").getIf<bool>();
    const auto definitionText = object->at("definition").getIf<std::string>();
    const auto instanceText   = object->at("instanceId").getIf<std::string>();
    if (active == nullptr || definitionText == nullptr || instanceText == nullptr)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                 "runtime snapshot identity fields have invalid types",
                                                                 "payload", {}, "common.definitions.snapshot"));
    const auto instanceId = eve::PersistentId::parse(*instanceText);
    if (!instanceId)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "runtime snapshot instanceId is invalid",
                                   "payload.instanceId", {}, "common.definitions.snapshot"));
    auto definition = eve::DefinitionRef::parse(*definitionText);
    if (!definition) return eve::Result<void>::failure(definition.status());
    auto generation = detail::runtimeSnapshotUint(object->at("definitionGeneration"), "payload.definitionGeneration");
    if (!generation) return eve::Result<void>::failure(generation.status());
    const eve::definition::InstanceIdentity identity{*instanceId, std::move(definition).takeValue(),
                                                     eve::Generation(std::move(generation).takeValue())};
    if (snapshot.instanceId != identity.instanceId || identity.instanceId != runtime.identity().instanceId)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "runtime snapshot instanceId does not match the destination",
            "payload.instanceId", {}, "common.definitions.snapshot"));
    if (identity.definition != runtime.identity().definition)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "runtime snapshot definition reference does not match", "payload.definition",
            {}, "common.definitions.snapshot"));
    if (identity.definitionGeneration != runtime.identity().definitionGeneration)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "runtime snapshot definition generation does not match the destination",
            "payload.definitionGeneration", {}, "common.definitions.snapshot"));
    if (!decode)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "runtime snapshot decoder is required", "decode", {},
                                                                 "common.definitions.snapshot"));
    auto candidate = decode(object->at("state"));
    if (!candidate) return eve::Result<void>::failure(candidate.status());
    return runtime.restoreExact(identity, std::move(candidate).takeValue(), *active);
}

}  // namespace eve::definition
