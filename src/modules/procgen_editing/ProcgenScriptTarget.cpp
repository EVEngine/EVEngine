#include "procgen_editing/ProcgenScriptTarget.h"

#include "editing/EditingResult.h"
#include "editing/EditingProperty.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace eve::procgen_editing {
namespace {

template <class T>
EditorResult<T> fail(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, editing::RuleId(rule), std::move(message));
}

EditorDiagnostic diagnostic(const char* rule, editing::DiagnosticSeverity severity, std::string message) {
    return eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument, editing::RuleId(rule), severity,
                                        std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

bool hasErrors(const std::vector<EditorDiagnostic>& diagnostics) {
    for (const auto& item : diagnostics)
        if (item.severity() == editing::DiagnosticSeverity::Error) return true;
    return false;
}

std::string requireString(const EditorValue& object, const char* key, std::string fallback = {}) {
    const EditorValue* entry = field(object, key);
    if (!entry) return fallback;
    if (const auto* text = entry->getIf<std::string>()) return *text;
    return fallback;
}

bool readNumber(const EditorValue& object, const char* key, double& output) {
    const EditorValue* entry = field(object, key);
    if (!entry) return false;
    if (const auto* real = entry->getIf<double>()) {
        output = *real;
        return std::isfinite(output);
    }
    if (const auto* integer = entry->getIf<std::int64_t>()) {
        output = static_cast<double>(*integer);
        return true;
    }
    return false;
}

procgen::ParamKind parseKind(const std::string& kind) {
    if (kind == "int" || kind == "integer") return procgen::ParamKind::Integer;
    if (kind == "float" || kind == "number") return procgen::ParamKind::Float;
    if (kind == "bool" || kind == "boolean") return procgen::ParamKind::Boolean;
    if (kind == "choice" || kind == "enum") return procgen::ParamKind::Choice;
    return procgen::ParamKind::String;
}

std::string encodeDefault(const procgen::ParamDescriptor& param, const EditorValue& value) {
    if (const auto* integer = value.getIf<std::int64_t>()) return std::to_string(*integer);
    if (const auto* real = value.getIf<double>()) {
        return std::to_string(*real);
    }
    if (const auto* flag = value.getIf<bool>()) return *flag ? "1" : "0";
    if (const auto* text = value.getIf<std::string>()) return *text;
    return param.defaultValue;
}

}  // namespace

ProcgenScriptDocumentTarget::ProcgenScriptDocumentTarget(std::string id) : id_(std::move(id)) {}

editing::TargetDescriptor ProcgenScriptDocumentTarget::describe() const {
    return {editing::TargetId(id_),
            "procgen-script",
            revision_,
            false,
            {propertyCapabilityId()}};
}

void* ProcgenScriptDocumentTarget::queryCapability(const editing::CapabilityId& capability) {
    return capability == propertyCapabilityId() ? static_cast<editing::IPropertyProvider*>(this) : nullptr;
}

bool ProcgenScriptDocumentTarget::matches(const editing::SelectionSnapshot& selection) const {
    return selection.items.size() == 1 && selection.items[0].target == editing::TargetId(id_) &&
           selection.items[0].type == "procgen-script";
}

eve::Result<eve::Revision> ProcgenScriptDocumentTarget::currentRevision(
    const editing::SelectionSnapshot& selection) const {
    if (!matches(selection))
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Procgen script selection mismatch",
            "editor.procgen-script.selection"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

EditorValue ProcgenScriptDocumentTarget::defaultValue(const procgen::ParamDescriptor& param) {
    try {
        switch (param.kind) {
            case procgen::ParamKind::Integer:
                return std::int64_t{std::stoll(param.defaultValue.empty() ? "0" : param.defaultValue)};
            case procgen::ParamKind::Float:
                return std::stod(param.defaultValue.empty() ? "0" : param.defaultValue);
            case procgen::ParamKind::Boolean:
                return param.defaultValue == "1" || param.defaultValue == "true";
            case procgen::ParamKind::String:
            case procgen::ParamKind::Choice:
                return param.defaultValue;
        }
    } catch (...) {
        return {};
    }
    return {};
}

editing::PropertyType ProcgenScriptDocumentTarget::propertyType(procgen::ParamKind kind) {
    switch (kind) {
        case procgen::ParamKind::Integer:
            return editing::PropertyType::Int;
        case procgen::ParamKind::Float:
            return editing::PropertyType::Float;
        case procgen::ParamKind::Boolean:
            return editing::PropertyType::Bool;
        case procgen::ParamKind::Choice:
            return editing::PropertyType::Enum;
        case procgen::ParamKind::String:
            return editing::PropertyType::String;
    }
    return editing::PropertyType::String;
}

const procgen::ParamDescriptor* ProcgenScriptDocumentTarget::findParam(const std::string& key) const {
    for (const auto& param : params_)
        if (param.key == key) return &param;
    return nullptr;
}

editing::PropertySchema ProcgenScriptDocumentTarget::schema(const editing::SelectionSnapshot&) const {
    editing::PropertySchema out;
    out.typeId = "procgen-script";
    for (const auto& param : params_) {
        editing::PropertyDescriptor descriptor;
        descriptor.path           = editing::PropertyPath("param." + param.key);
        descriptor.displayNameKey = param.displayName.empty() ? param.key : param.displayName;
        descriptor.descriptionKey = param.description;
        descriptor.category       = param.category.empty() ? "Generator" : param.category;
        descriptor.type           = propertyType(param.kind);
        descriptor.defaultValue   = defaultValue(param);
        descriptor.flags          = editing::PropertyFlag::Runtime;
        if (param.advanced) descriptor.flags = descriptor.flags | editing::PropertyFlag::Advanced;
        descriptor.enumItems = param.choices;
        if (param.hasMinimum) descriptor.numeric.minimum = param.minimum;
        if (param.hasMaximum) descriptor.numeric.maximum = param.maximum;
        if (param.step != 0.0) descriptor.numeric.step = param.step;
        out.properties.push_back(std::move(descriptor));
    }
    return out;
}

editing::PropertyReadResult ProcgenScriptDocumentTarget::read(const editing::SelectionSnapshot& selection,
                                                              const editing::PropertyPath& path) const {
    if (!matches(selection) || path.value().rfind("param.", 0) != 0) return {};
    const auto found = values_.find(path.value().substr(6));
    return found == values_.end() ? editing::PropertyReadResult{}
                                  : editing::PropertyReadResult{editing::PropertyReadState::Value, found->second, {}};
}

EditorValue ProcgenScriptDocumentTarget::schemaValue() const {
    EditorValue::Array rows;
    rows.reserve(params_.size());
    for (const auto& param : params_) {
        EditorValue::Object row;
        row["key"]         = param.key;
        row["displayName"] = param.displayName;
        row["description"] = param.description;
        row["category"]    = param.category;
        switch (param.kind) {
            case procgen::ParamKind::Integer:
                row["kind"] = std::string("int");
                break;
            case procgen::ParamKind::Float:
                row["kind"] = std::string("float");
                break;
            case procgen::ParamKind::Boolean:
                row["kind"] = std::string("bool");
                break;
            case procgen::ParamKind::Choice:
                row["kind"] = std::string("choice");
                break;
            case procgen::ParamKind::String:
                row["kind"] = std::string("string");
                break;
        }
        row["defaultValue"] = param.defaultValue;
        row["advanced"] = param.advanced;
        if (param.hasMinimum) row["min"] = param.minimum;
        if (param.hasMaximum) row["max"] = param.maximum;
        if (param.step != 0.0) row["step"] = param.step;
        if (!param.choices.empty()) {
            EditorValue::Array choices;
            for (const auto& choice : param.choices) choices.emplace_back(choice);
            row["choices"] = std::move(choices);
        }
        rows.emplace_back(std::move(row));
    }
    return rows;
}

EditorValue ProcgenScriptDocumentTarget::contentValue() const {
    return EditorValue::Object{{"uri", uri_},
                               {"id", moduleId_},
                               {"displayName", displayName_},
                               {"kind", kind_},
                               {"schema", schemaValue()},
                               {"values", values_}};
}

void ProcgenScriptDocumentTarget::applySpec(ProcgenScriptModuleSpec spec) {
    EditorValue::Object next;
    for (const auto& param : spec.params) {
        const auto existing = values_.find(param.key);
        if (existing != values_.end()) {
            editing::PropertyDescriptor descriptor;
            descriptor.path         = editing::PropertyPath("param." + param.key);
            descriptor.type         = propertyType(param.kind);
            descriptor.defaultValue = defaultValue(param);
            descriptor.enumItems    = param.choices;
            if (param.hasMinimum) descriptor.numeric.minimum = param.minimum;
            if (param.hasMaximum) descriptor.numeric.maximum = param.maximum;
            if (editing::validatePropertyValue(descriptor, existing->second).ok()) {
                next[param.key] = existing->second;
                continue;
            }
        }
        next[param.key] = defaultValue(param);
    }
    uri_         = std::move(spec.uri);
    moduleId_    = std::move(spec.id);
    displayName_ = std::move(spec.displayName);
    kind_        = std::move(spec.kind);
    params_      = std::move(spec.params);
    values_      = std::move(next);
}

EditorResult<ProcgenScriptModuleSpec> ProcgenScriptDocumentTarget::parseSpec(std::string uri, std::string id,
                                                                             std::string displayName, std::string kind,
                                                                             const EditorValue& schema) {
    if (id.empty())
        return fail<ProcgenScriptModuleSpec>(EditorStatus::Rejected, "editor.procgen-script.id",
                                             "Generator module id must not be empty");
    if (kind.empty()) kind = "points";
    if (kind != "points")
        return fail<ProcgenScriptModuleSpec>(EditorStatus::Unsupported, "editor.procgen-script.kind",
                                             "Only kind=points generators are supported in this editor version");
    const auto* rows = schema.getIf<EditorValue::Array>();
    if (!rows)
        return fail<ProcgenScriptModuleSpec>(EditorStatus::Rejected, "editor.procgen-script.schema",
                                             "Generator schema must be an array");
    if (rows->size() > 128)
        return fail<ProcgenScriptModuleSpec>(EditorStatus::Rejected, "editor.procgen-script.schema-size",
                                             "Generator schema exceeds 128 parameters");

    ProcgenScriptModuleSpec spec;
    spec.uri         = std::move(uri);
    spec.id          = std::move(id);
    spec.displayName = displayName.empty() ? spec.id : std::move(displayName);
    spec.kind        = std::move(kind);
    spec.params.reserve(rows->size());
    for (const auto& rowValue : *rows) {
        const auto* row = rowValue.getIf<EditorValue::Object>();
        if (!row)
            return fail<ProcgenScriptModuleSpec>(EditorStatus::Rejected, "editor.procgen-script.param",
                                                 "Each schema entry must be an object");
        procgen::ParamDescriptor param;
        param.key = requireString(rowValue, "key");
        if (param.key.empty())
            return fail<ProcgenScriptModuleSpec>(EditorStatus::Rejected, "editor.procgen-script.param-key",
                                                 "Parameter key must not be empty");
        for (const auto& existing : spec.params)
            if (existing.key == param.key)
                return fail<ProcgenScriptModuleSpec>(EditorStatus::Rejected, "editor.procgen-script.param-dup",
                                                     "Duplicate parameter key: " + param.key);
        param.displayName = requireString(rowValue, "displayName", requireString(rowValue, "label", param.key));
        param.description = requireString(rowValue, "description");
        param.category    = requireString(rowValue, "category", "Generator");
        param.kind        = parseKind(requireString(rowValue, "kind", "string"));
        param.advanced    = false;
        if (const auto* advanced = field(rowValue, "advanced"))
            if (const auto* flag = advanced->getIf<bool>()) param.advanced = *flag;

        double numeric = 0.0;
        if (readNumber(rowValue, "min", numeric)) {
            param.hasMinimum = true;
            param.minimum    = numeric;
        }
        if (readNumber(rowValue, "max", numeric)) {
            param.hasMaximum = true;
            param.maximum    = numeric;
        }
        if (readNumber(rowValue, "step", numeric)) param.step = numeric;

        if (const EditorValue* choices = field(rowValue, "choices")) {
            if (const auto* array = choices->getIf<EditorValue::Array>()) {
                for (const auto& choice : *array) {
                    const auto* text = choice.getIf<std::string>();
                    if (!text)
                        return fail<ProcgenScriptModuleSpec>(EditorStatus::Rejected,
                                                             "editor.procgen-script.choice",
                                                             "Choice values must be strings");
                    param.choices.push_back(*text);
                }
            }
        }

        EditorValue def;
        if (const EditorValue* defaultField = field(rowValue, "defaultValue")) {
            def = *defaultField;
        } else if (const EditorValue* defaultField = field(rowValue, "default")) {
            def = *defaultField;
        } else {
            switch (param.kind) {
                case procgen::ParamKind::Integer:
                    def = std::int64_t{0};
                    break;
                case procgen::ParamKind::Float:
                    def = 0.0;
                    break;
                case procgen::ParamKind::Boolean:
                    def = false;
                    break;
                case procgen::ParamKind::String:
                case procgen::ParamKind::Choice:
                    def = param.choices.empty() ? std::string{} : param.choices.front();
                    break;
            }
        }
        param.defaultValue = encodeDefault(param, def);
        spec.params.push_back(std::move(param));
    }
    return eve::editing::applied(std::move(spec));
}

EditorResult<editing::DomainOperation> ProcgenScriptDocumentTarget::makeLoadModule(
    ProcgenScriptModuleSpec spec) const {
    if (spec.id.empty())
        return fail<editing::DomainOperation>(EditorStatus::Rejected, "editor.procgen-script.id",
                                              "Generator module id must not be empty");
    if (spec.kind != "points")
        return fail<editing::DomainOperation>(EditorStatus::Unsupported, "editor.procgen-script.kind",
                                              "Only kind=points generators are supported in this editor version");
    if (spec.params.size() > 128)
        return fail<editing::DomainOperation>(EditorStatus::Rejected, "editor.procgen-script.schema-size",
                                              "Generator schema exceeds 128 parameters");

    ProcgenScriptDocumentTarget candidate = *this;
    candidate.applySpec(std::move(spec));
    if (hasErrors(candidate.validate()))
        return fail<editing::DomainOperation>(EditorStatus::Rejected, "editor.procgen-script.invalid",
                                              "Generator module validation failed");

    editing::DomainOperation operation;
    operation.type        = "procgen.script.replace.v1";
    operation.inverseType = operation.type;
    operation.target      = editing::TargetId(id_);
    operation.payload     = candidate.contentValue();
    operation.inverse     = contentValue();
    operation.hasInverse  = true;
    operation.mergeKey    = "procgen-script:" + id_ + ":module";
    return eve::editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> ProcgenScriptDocumentTarget::makeSet(
    const editing::SelectionSnapshot& selection, const editing::PropertyPath& path, const EditorValue& value,
    editing::PropertySetMode mode) const {
    if (mode == editing::PropertySetMode::Reset) return makeReset(selection, path);
    const auto descriptor = schema(selection).find(path);
    if (!matches(selection) || !descriptor || mode != editing::PropertySetMode::Absolute ||
        !editing::validatePropertyValue(*descriptor, value).ok())
        return fail<editing::DomainOperation>(EditorStatus::Rejected, "editor.procgen-script.set",
                                              "Generator parameter edit is invalid");
    auto values                    = values_;
    values[path.value().substr(6)] = value;
    editing::DomainOperation operation;
    operation.type        = "procgen.script.replace.v1";
    operation.inverseType = operation.type;
    operation.target      = editing::TargetId(id_);
    EditorValue          content = contentValue();
    EditorValue::Object  payload =
        content.getIf<EditorValue::Object>() ? *content.getIf<EditorValue::Object>() : EditorValue::Object{};
    payload["values"] = std::move(values);
    operation.payload = std::move(payload);
    operation.inverse     = contentValue();
    operation.hasInverse  = true;
    operation.affectedProperties.push_back(path.value());
    operation.mergeKey    = "procgen-script:" + id_ + ":" + path.value();
    return eve::editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> ProcgenScriptDocumentTarget::makeReset(
    const editing::SelectionSnapshot& selection, const editing::PropertyPath& path) const {
    const auto descriptor = schema(selection).find(path);
    if (!descriptor)
        return fail<editing::DomainOperation>(EditorStatus::Unsupported, "editor.procgen-script.property",
                                              "Unknown generator parameter");
    return makeSet(selection, path, descriptor->defaultValue, editing::PropertySetMode::Absolute);
}

std::vector<EditorDiagnostic> ProcgenScriptDocumentTarget::validate() const {
    std::vector<EditorDiagnostic> diagnostics;
    if (!kind_.empty() && kind_ != "points")
        diagnostics.push_back(diagnostic("editor.procgen-script.kind", editing::DiagnosticSeverity::Error,
                                         "Only kind=points generators are supported"));
    const auto propertySchema = schema({});
    for (const auto& param : params_) {
        const auto found       = values_.find(param.key);
        const auto descriptor  = propertySchema.find(editing::PropertyPath("param." + param.key));
        if (found == values_.end() || !descriptor ||
            !editing::validatePropertyValue(*descriptor, found->second).ok())
            diagnostics.push_back(diagnostic("editor.procgen-script.parameter", editing::DiagnosticSeverity::Error,
                                             "Invalid generator parameter: " + param.key));
    }
    if (values_.size() != params_.size())
        diagnostics.push_back(diagnostic("editor.procgen-script.extra", editing::DiagnosticSeverity::Error,
                                         "Generator document contains unknown parameters"));
    return diagnostics;
}

EditorResult<void> ProcgenScriptDocumentTarget::applyDomainOperation(const editing::DomainOperation& operation) {
    if (operation.target != editing::TargetId(id_) || operation.type != "procgen.script.replace.v1")
        return fail<void>(EditorStatus::Rejected, "editor.procgen-script.operation",
                          "Generator operation is invalid");
    const auto parsed = parseSpec(requireString(operation.payload, "uri"), requireString(operation.payload, "id"),
                                  requireString(operation.payload, "displayName"),
                                  requireString(operation.payload, "kind", "points"),
                                  field(operation.payload, "schema") ? *field(operation.payload, "schema")
                                                                     : EditorValue{});
    if (!parsed.ok()) return EditorResult<void>::failure(parsed.status());
    const EditorValue* valuesField = field(operation.payload, "values");
    const auto*        values      = valuesField ? valuesField->getIf<EditorValue::Object>() : nullptr;
    if (!values || values->size() > 128)
        return fail<void>(EditorStatus::Rejected, "editor.procgen-script.payload",
                          "Generator payload is invalid");

    ProcgenScriptDocumentTarget candidate = *this;
    candidate.applySpec(parsed.value());
    candidate.values_ = *values;
    if (hasErrors(candidate.validate()))
        return fail<void>(EditorStatus::Rejected, "editor.procgen-script.invalid",
                          "Generator validation failed");
    *this = std::move(candidate);
    ++revision_;
    dirty_.include(0, 0);
    return eve::editing::applied<void>();
}

std::unique_ptr<editing::IDomainOperationTarget> ProcgenScriptDocumentTarget::cloneDomainState() const {
    return std::make_unique<ProcgenScriptDocumentTarget>(*this);
}

EditorResult<void> ProcgenScriptDocumentTarget::commitDomainState(
    std::unique_ptr<editing::IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<ProcgenScriptDocumentTarget*>(candidate.get());
    if (!typed || typed->id_ != id_)
        return fail<void>(EditorStatus::Conflict, "editor.procgen-script.candidate",
                          "Generator candidate mismatch");
    *this = *typed;
    return eve::editing::applied<void>();
}

EditorValue ProcgenScriptDocumentTarget::snapshotValue() const {
    return EditorValue::Object{{"schemaVersion", std::int64_t{1}}, {"content", contentValue()}};
}

EditorResult<void> ProcgenScriptDocumentTarget::loadSnapshot(const EditorValue& snapshot) {
    const EditorValue* versionField = field(snapshot, "schemaVersion");
    const EditorValue* content      = field(snapshot, "content");
    const auto*        version      = versionField ? versionField->getIf<std::int64_t>() : nullptr;
    if (!version || *version != 1 || !content)
        return fail<void>(EditorStatus::Unsupported, "editor.procgen-script.snapshot",
                          "Unsupported generator snapshot");
    editing::DomainOperation operation;
    operation.target  = editing::TargetId(id_);
    operation.type    = "procgen.script.replace.v1";
    operation.payload = *content;
    auto result       = applyDomainOperation(operation);
    if (result.ok()) dirty_.clear();
    return result;
}

}  // namespace eve::procgen_editing
