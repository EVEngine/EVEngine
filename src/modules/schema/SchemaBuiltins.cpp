#include "schema/SchemaBuiltins.h"

#include "schema/SchemaRegistry.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace eve::schema {
namespace {

FieldDefinition field(std::string name, ValueType type, bool required = true) {
    FieldDefinition result;
    result.name = std::move(name);
    result.type = type;
    result.required = required;
    return result;
}

SchemaDefinition snapshotEnvelopeSchema() {
    SchemaDefinition result;
    result.id = "snapshot:envelope";
    result.version = 1;
    result.title = "Snapshot Envelope";
    result.description = "Stable outer header and owning payload for persistence.";
    result.additionalProperties = false;
    result.fields = {
        field("type", ValueType::String),
        field("schema", ValueType::String),
        field("schemaVersion", ValueType::String),
        field("instanceId", ValueType::String),
        field("revision", ValueType::String),
        field("tick", ValueType::String),
        field("contentHash", ValueType::String),
        field("payload", ValueType::Any),
    };
    return result;
}

SchemaDefinition eventEnvelopeSchema() {
    SchemaDefinition result;
    result.id = "game_event:envelope";
    result.version = 1;
    result.title = "Event Envelope";
    result.description = "Causal/order metadata around one domain-owned JSON payload.";
    result.additionalProperties = false;
    result.fields = {
        field("eventId", ValueType::String),
        field("sequence", ValueType::String),
        field("type", ValueType::String),
        field("source", ValueType::String),
        field("subject", ValueType::String),
        field("causationKind", ValueType::String),
        field("causation", ValueType::String),
        field("correlationKind", ValueType::String),
        field("correlation", ValueType::String),
        field("schemaId", ValueType::String),
        field("schemaVersion", ValueType::String),
        field("tick", ValueType::String),
        field("flags", ValueType::Integer),
        field("payload", ValueType::Any),
    };
    return result;
}

SchemaDefinition definitionMetadataSchema() {
    SchemaDefinition result;
    result.id = "definitions:metadata";
    result.version = 1;
    result.title = "Definition Metadata";
    result.description = "Identity and version metadata for one definition instance.";
    result.additionalProperties = false;
    result.fields = {
        field("type", ValueType::String),
        field("id", ValueType::String),
        field("version", ValueType::Integer),
        field("generation", ValueType::String),
        field("json", ValueType::Any),
    };
    return result;
}

}  // namespace

eve::Result<void> registerStandardSchemas() {
    const std::array<SchemaDefinition, 3> definitions = {
        snapshotEnvelopeSchema(), eventEnvelopeSchema(), definitionMetadataSchema()};

    bool anyRegistered = false;
    bool allRegistered = true;
    for (const auto& definition : definitions) {
        if (SchemaRegistry::resolve(definition.id, definition.version)) anyRegistered = true;
        else allRegistered = false;
    }
    if (allRegistered) return eve::Result<void>::success();
    if (anyRegistered)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict,
            "standard schema set is partially registered; refusing a partial bootstrap"));

    std::vector<std::string> registered;
    registered.reserve(definitions.size());
    for (const auto& definition : definitions) {
        auto status = SchemaRegistry::registerVersioned(definition);
        if (status.ok()) {
            registered.push_back(definition.id);
            continue;
        }
        const auto diagnostic = status.status().describe();
        for (const auto& id : registered) SchemaRegistry::remove(id, 1).ignore("standard schema teardown");
        return eve::Result<void>::failure(eve::Diagnostic::error(
            status.code() == eve::StatusCode::Conflict ? eve::DiagnosticCode::Conflict
                                                       : eve::DiagnosticCode::InvalidArgument,
            diagnostic.empty() ? "standard schema registration failed" : diagnostic, definition.id));
    }
    return eve::Result<void>::success();
}

}  // namespace eve::schema
