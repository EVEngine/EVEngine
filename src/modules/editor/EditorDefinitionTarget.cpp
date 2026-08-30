#include "editor/EditorDefinitionTarget.h"
#include "editor/EditorValueJson.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <set>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> definitionError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

bool looksLikeJsonValue(const std::string& json) {
    const auto first = std::find_if_not(json.begin(), json.end(), [](unsigned char c) { return std::isspace(c); });
    if (first == json.end()) return false;
    const auto last = std::find_if_not(json.rbegin(), json.rend(), [](unsigned char c) { return std::isspace(c); });
    const char open = *first;
    const char close = *last;
    return (open == '{' && close == '}') || (open == '[' && close == ']');
}

}  // namespace

DefinitionDocument::DefinitionDocument(std::string type, std::string id, int version)
    : type_(std::move(type)), id_(std::move(id)), targetId_("definition:" + type_ + ":" + id_),
      version_(version > 0 ? version : 1) {}

TargetDescriptor DefinitionDocument::describe() const {
    return {TargetId(targetId_), "definition-document", revision_, false, {}};
}

EditorResult<void> DefinitionDocument::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(targetId_) || operation.type != "definition.field.set.v1")
        return definitionError<void>(EditorStatus::Rejected, "editor.definition.operation-mismatch",
                                     "Definition field operation targets another document or type");
    const auto* pathValue = field(operation.payload, "field");
    const auto* assigned = field(operation.payload, "value");
    const auto* presentValue = field(operation.payload, "present");
    const auto* path = pathValue ? pathValue->getIf<std::string>() : nullptr;
    const auto* present = presentValue ? presentValue->getIf<bool>() : nullptr;
    auto parsed = editorValueFromJson(json_);
    auto* object = parsed.value ? parsed.value->getIf<EditorValue::Object>() : nullptr;
    if (!path || path->empty() || !assigned || !present || !object)
        return definitionError<void>(EditorStatus::Rejected, "editor.definition.invalid-field-operation",
                                     "Definition field operation requires an object payload and field name");
    if (*present) (*object)[*path] = *assigned;
    else object->erase(*path);
    json_ = editorValueToJson(*parsed.value);
    ++revision_;
    dirty_.include(0, 0);
    return EditorResult<void>::applied();
}

EditorResult<void> DefinitionDocument::setJson(std::string json) {
    if (!looksLikeJsonValue(json))
        return definitionError<void>(EditorStatus::Rejected, "editor.definition.invalid-json-shape",
                                     "Definition payload must be a JSON object or array");
    json_ = std::move(json);
    ++revision_;
    dirty_.include(0, 0);
    return EditorResult<void>::applied();
}

EditorResult<void> DefinitionDocument::setVersion(int version) {
    if (version <= 0)
        return definitionError<void>(EditorStatus::Rejected, "editor.definition.invalid-version",
                                     "Definition schema version must be positive");
    version_ = version;
    ++revision_;
    dirty_.include(0, 0);
    return EditorResult<void>::applied();
}

EditorResult<void> DefinitionDocument::setReferences(std::vector<DefinitionReferenceField> references) {
    std::set<std::string> paths;
    for (const DefinitionReferenceField& reference : references)
        if (reference.path.empty() || !paths.insert(reference.path).second)
            return definitionError<void>(EditorStatus::Rejected, "editor.definition.invalid-reference-path",
                                         "Definition reference paths must be unique and non-empty");
    references_ = std::move(references);
    ++revision_;
    dirty_.include(0, 0);
    return EditorResult<void>::applied();
}

EditorResult<DomainOperation> DefinitionDocument::makeSetField(const std::string& fieldName,
                                                               const EditorValue& value) const {
    if (fieldName.empty())
        return definitionError<DomainOperation>(EditorStatus::Rejected, "editor.definition.empty-field",
                                                "Definition field name must not be empty");
    auto parsed = editorValueFromJson(json_);
    const auto* object = parsed.value ? parsed.value->getIf<EditorValue::Object>() : nullptr;
    if (!object)
        return definitionError<DomainOperation>(EditorStatus::Unsupported, "editor.definition.form-object-required",
                                                "Schema form editing requires an object definition payload");
    const auto current = object->find(fieldName);
    const EditorValue before = current == object->end() ? EditorValue{} : current->second;
    DomainOperation operation;
    operation.type = "definition.field.set.v1";
    operation.inverseType = operation.type;
    operation.target = TargetId(targetId_);
    operation.payload = EditorValue::Object{{"field", fieldName}, {"value", value}, {"present", true}};
    operation.inverse = EditorValue::Object{{"field", fieldName}, {"value", before},
                                            {"present", current != object->end()}};
    operation.hasInverse = true;
    operation.affectedObjects.push_back({TargetId(targetId_), id_, 0});
    operation.affectedProperties.push_back(fieldName);
    operation.mergeKey = targetId_ + ":" + fieldName;
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

std::vector<EditorDiagnostic> DefinitionDocument::validate(const SchemaValidator& schema,
                                                            const ReferenceResolver& references) const {
    std::vector<EditorDiagnostic> diagnostics;
    if (type_.empty() || id_.empty() || version_ <= 0)
        diagnostics.push_back({RuleId("editor.definition.invalid-identity"), DiagnosticSeverity::Error,
                               "Definition type, id and positive version are required"});
    if (!looksLikeJsonValue(json_))
        diagnostics.push_back({RuleId("editor.definition.invalid-json-shape"), DiagnosticSeverity::Error,
                               "Definition payload must be a JSON object or array"});
    if (schema) {
        auto schemaDiagnostics = schema(type_, version_, json_);
        diagnostics.insert(diagnostics.end(), std::make_move_iterator(schemaDiagnostics.begin()),
                           std::make_move_iterator(schemaDiagnostics.end()));
    }
    for (const DefinitionReferenceField& reference : references_) {
        if (reference.type.empty() || reference.id.empty()) {
            if (reference.required)
                diagnostics.push_back({RuleId("editor.definition.reference-required"), DiagnosticSeverity::Error,
                                       "Required reference is empty: " + reference.path});
            continue;
        }
        if (references && !references(reference.type, reference.id))
            diagnostics.push_back({RuleId("editor.definition.reference-not-found"), DiagnosticSeverity::Error,
                                   "Definition reference could not be resolved: " + reference.path + " -> " +
                                       reference.type + ":" + reference.id});
    }
    return diagnostics;
}

EditorValue DefinitionDocument::snapshotValue() const {
    EditorValue::Array references;
    for (const DefinitionReferenceField& reference : references_)
        references.emplace_back(EditorValue::Object{{"path", reference.path}, {"type", reference.type},
                                                     {"id", reference.id}, {"required", reference.required}});
    return EditorValue::Object{{"schemaVersion", int64_t{1}}, {"type", type_}, {"id", id_},
                               {"definitionVersion", int64_t{version_}}, {"json", json_},
                               {"references", std::move(references)}};
}

EditorResult<void> DefinitionDocument::loadSnapshot(const EditorValue& snapshot) {
    const auto* schemaValue = field(snapshot, "schemaVersion");
    const auto* typeValue = field(snapshot, "type");
    const auto* idValue = field(snapshot, "id");
    const auto* versionValue = field(snapshot, "definitionVersion");
    const auto* jsonValue = field(snapshot, "json");
    const auto* referencesValue = field(snapshot, "references");
    const auto* schema = schemaValue ? schemaValue->getIf<int64_t>() : nullptr;
    const auto* type = typeValue ? typeValue->getIf<std::string>() : nullptr;
    const auto* id = idValue ? idValue->getIf<std::string>() : nullptr;
    const auto* version = versionValue ? versionValue->getIf<int64_t>() : nullptr;
    const auto* json = jsonValue ? jsonValue->getIf<std::string>() : nullptr;
    const auto* references = referencesValue ? referencesValue->getIf<EditorValue::Array>() : nullptr;
    if (!schema || *schema != 1 || !type || type->empty() || !id || id->empty() || !version || *version <= 0 ||
        !json || !looksLikeJsonValue(*json) || !references)
        return definitionError<void>(EditorStatus::Rejected, "editor.definition.invalid-snapshot",
                                     "Definition snapshot is invalid or unsupported");
    std::vector<DefinitionReferenceField> parsed;
    for (const EditorValue& value : *references) {
        const auto* pathValue = field(value, "path");
        const auto* refTypeValue = field(value, "type");
        const auto* refIdValue = field(value, "id");
        const auto* requiredValue = field(value, "required");
        const auto* path = pathValue ? pathValue->getIf<std::string>() : nullptr;
        const auto* refType = refTypeValue ? refTypeValue->getIf<std::string>() : nullptr;
        const auto* refId = refIdValue ? refIdValue->getIf<std::string>() : nullptr;
        const auto* required = requiredValue ? requiredValue->getIf<bool>() : nullptr;
        if (!path || !refType || !refId || !required)
            return definitionError<void>(EditorStatus::Rejected, "editor.definition.invalid-reference",
                                         "Definition snapshot contains an invalid reference");
        parsed.push_back({*path, *refType, *refId, *required});
    }
    DefinitionDocument candidate(*type, *id, static_cast<int>(*version));
    if (!candidate.setJson(*json).isAccepted() || !candidate.setReferences(std::move(parsed)).isAccepted())
        return definitionError<void>(EditorStatus::Rejected, "editor.definition.invalid-snapshot",
                                     "Definition snapshot could not be applied");
    type_ = std::move(candidate.type_);
    id_ = std::move(candidate.id_);
    version_ = candidate.version_;
    json_ = std::move(candidate.json_);
    references_ = std::move(candidate.references_);
    targetId_ = "definition:" + type_ + ":" + id_;
    ++revision_;
    dirty_.clear();
    return EditorResult<void>::applied();
}

}  // namespace eve::editor
