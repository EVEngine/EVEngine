#include "editor/EditorProjectSettings.h"

#include <algorithm>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> settingsError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

DomainOperation operation(const std::string& target, const PropertyPath& path, EditorValue value, EditorValue inverse) {
    DomainOperation result;
    result.type        = "project.setting.set.v1";
    result.inverseType = result.type;
    result.target      = TargetId(target);
    result.payload     = EditorValue::Object{{"path", path.value()}, {"value", std::move(value)}};
    result.inverse     = EditorValue::Object{{"path", path.value()}, {"value", std::move(inverse)}};
    result.hasInverse  = true;
    result.affectedProperties.push_back(path.value());
    return result;
}

PropertyDescriptor property(std::string path, PropertyType type, EditorValue value,
                            PropertyFlag flags = PropertyFlag::None) {
    PropertyDescriptor result;
    result.path           = PropertyPath(path);
    result.displayNameKey = "editor.settings." + path;
    result.category       = "project";
    result.type           = type;
    result.flags          = flags;
    result.defaultValue   = std::move(value);
    return result;
}

}  // namespace

ProjectSettingsTarget::ProjectSettingsTarget(std::string id, ProjectSettingsSchema schema)
    : id_(std::move(id)), schema_(std::move(schema)) {
    for (const auto& setting : schema_.settings)
        values_.emplace(setting.property.path.value(), setting.property.defaultValue);
}

TargetDescriptor ProjectSettingsTarget::describe() const {
    return {TargetId(id_), "project-settings", revision_, false, {CapabilityId("eve.editor.target.project-settings")}};
}

void* ProjectSettingsTarget::queryCapability(const CapabilityId& capability) {
    return capability == CapabilityId("eve.editor.target.project-settings") ? static_cast<IPropertyProvider*>(this)
                                                                            : nullptr;
}

const ProjectSettingDescriptor* ProjectSettingsTarget::descriptor(const PropertyPath& path) const {
    const auto found =
        std::find_if(schema_.settings.begin(), schema_.settings.end(),
                     [&](const ProjectSettingDescriptor& setting) { return setting.property.path == path; });
    return found == schema_.settings.end() ? nullptr : &*found;
}

bool ProjectSettingsTarget::selectionMatches(const SelectionSnapshot& selection) const {
    return selection.items.size() == 1 && selection.items.front().target == TargetId(id_);
}

EditorResult<void> ProjectSettingsTarget::applyDomainOperation(const DomainOperation& domainOperation) {
    if (domainOperation.target != TargetId(id_) || domainOperation.type != "project.setting.set.v1")
        return settingsError<void>(EditorStatus::Rejected, "editor.settings.operation",
                                   "Unsupported or mismatched settings operation");
    const auto* pathEntry = field(domainOperation.payload, "path");
    const auto* value     = field(domainOperation.payload, "value");
    const auto* path      = pathEntry ? pathEntry->getIf<std::string>() : nullptr;
    const auto* setting   = path ? descriptor(PropertyPath(*path)) : nullptr;
    if (!setting || !value)
        return settingsError<void>(EditorStatus::Rejected, "editor.settings.payload",
                                   "Settings operation payload is incomplete");
    auto valid = validatePropertyValue(setting->property, *value);
    if (!valid.ok()) return valid;
    if (setting->sensitive) {
        const auto* reference = value->getIf<std::string>();
        if (!reference || !reference->starts_with("secret://"))
            return settingsError<void>(
                EditorStatus::Rejected, "editor.settings.raw-secret",
                "Sensitive settings accept secret:// references only; raw credentials are never stored");
    }
    if (values_.at(*path) == *value) return eve::editing::noOp();
    values_[*path] = *value;
    if (setting->requiresRestart) restartDirty_[*path] = true;
    ++revision_;
    dirty_.include(0, 0);
    return eve::editing::applied<void>();
}

eve::Result<eve::Revision> ProjectSettingsTarget::currentRevision(const SelectionSnapshot& selection) const {
    if (!selectionMatches(selection))
        return eve::Result<eve::Revision>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Selection does not match settings target"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

PropertySchema ProjectSettingsTarget::schema(const SelectionSnapshot&) const {
    PropertySchema result;
    result.typeId  = schema_.typeId;
    result.version = schema_.version;
    for (const auto& setting : schema_.settings) result.properties.push_back(setting.property);
    return result;
}

PropertyReadResult ProjectSettingsTarget::read(const SelectionSnapshot& selection, const PropertyPath& path) const {
    if (!selectionMatches(selection))
        return {PropertyReadState::Error,
                {},
                {eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument, RuleId("editor.settings.selection"),
                                              DiagnosticSeverity::Error, "Selection does not match settings target")}};
    const auto found = values_.find(path.value());
    if (found == values_.end()) return {};
    const auto* setting = descriptor(path);
    if (setting && setting->sensitive) {
        const auto* reference = found->second.getIf<std::string>();
        if (!reference)
            return {PropertyReadState::Error,
                    {},
                    {eve::editing::ruleDiagnostic(
                        eve::DiagnosticCode::PreconditionViolation, RuleId("editor.settings.invalid-sensitive-schema"),
                        DiagnosticSeverity::Error, "Sensitive setting schema must use a string value")}};
        if (!reference->empty()) return {PropertyReadState::Value, EditorValue("secret://redacted"), {}};
    }
    return {PropertyReadState::Value, found->second, {}};
}

EditorResult<DomainOperation> ProjectSettingsTarget::makeSet(const SelectionSnapshot& selection,
                                                             const PropertyPath& path, const EditorValue& value,
                                                             PropertySetMode mode) const {
    if (!selectionMatches(selection))
        return settingsError<DomainOperation>(EditorStatus::Rejected, "editor.settings.selection",
                                              "Selection does not match settings target");
    const auto* setting = descriptor(path);
    if (!setting)
        return settingsError<DomainOperation>(EditorStatus::Unsupported, "editor.settings.property",
                                              "Unknown project setting: " + path.value());
    const EditorValue candidate = mode == PropertySetMode::Reset ? setting->property.defaultValue : value;
    auto              valid     = validatePropertyValue(setting->property, candidate);
    if (!valid.ok())
        return settingsError<DomainOperation>(valid.code(), "editor.settings.value",
                                              "Project setting value is invalid");
    if (setting->sensitive) {
        const auto* reference = candidate.getIf<std::string>();
        if (!reference || (!reference->empty() && !reference->starts_with("secret://")))
            return settingsError<DomainOperation>(
                EditorStatus::Rejected, "editor.settings.raw-secret",
                "Sensitive settings accept an empty value or secret:// reference only");
    }
    return eve::editing::applied<DomainOperation>(operation(id_, path, candidate, values_.at(path.value())));
}

EditorResult<DomainOperation> ProjectSettingsTarget::makeReset(const SelectionSnapshot& selection,
                                                               const PropertyPath&      path) const {
    return makeSet(selection, path, {}, PropertySetMode::Reset);
}

std::vector<PropertyPath> ProjectSettingsTarget::pendingRestart() const {
    std::vector<PropertyPath> result;
    for (const auto& [path, dirty] : restartDirty_)
        if (dirty) result.emplace_back(path);
    return result;
}

EditorValue ProjectSettingsTarget::snapshotValue() const {
    EditorValue::Object values;
    for (const auto& setting : schema_.settings) {
        const auto& value     = values_.at(setting.property.path.value());
        const auto* reference = setting.sensitive ? value.getIf<std::string>() : nullptr;
        if (!setting.sensitive || (reference && (reference->empty() || reference->starts_with("secret://"))))
            values.emplace(setting.property.path.value(), value);
    }
    return EditorValue::Object{
        {"schema", schema_.typeId}, {"schemaVersion", int64_t{schema_.version}}, {"values", std::move(values)}};
}

EditorResult<void> ProjectSettingsTarget::loadSnapshot(const EditorValue& snapshot) {
    const auto* schemaEntry  = field(snapshot, "schema");
    const auto* versionEntry = field(snapshot, "schemaVersion");
    const auto* valuesEntry  = field(snapshot, "values");
    const auto* schema       = schemaEntry ? schemaEntry->getIf<std::string>() : nullptr;
    const auto* version      = versionEntry ? versionEntry->getIf<int64_t>() : nullptr;
    const auto* values       = valuesEntry ? valuesEntry->getIf<EditorValue::Object>() : nullptr;
    if (!schema || *schema != schema_.typeId || !version || *version != schema_.version || !values)
        return settingsError<void>(EditorStatus::Rejected, "editor.settings.snapshot",
                                   "Settings snapshot schema does not match this target");
    ProjectSettingsTarget candidate(id_, schema_);
    for (const auto& [path, value] : *values) {
        const auto* setting = candidate.descriptor(PropertyPath(path));
        if (!setting)
            return settingsError<void>(EditorStatus::Rejected, "editor.settings.unknown-snapshot-key",
                                       "Settings snapshot contains an unknown key");
        auto valid = validatePropertyValue(setting->property, value);
        if (!valid.ok())
            return settingsError<void>(EditorStatus::Rejected, "editor.settings.invalid-snapshot-value",
                                       "Settings snapshot contains an invalid value");
        if (setting->sensitive) {
            const auto* reference = value.getIf<std::string>();
            if (!reference || (!reference->empty() && !reference->starts_with("secret://")))
                return settingsError<void>(EditorStatus::Rejected, "editor.settings.raw-secret",
                                           "Settings snapshot contains a raw sensitive value");
        }
        candidate.values_[path] = value;
    }
    values_ = std::move(candidate.values_);
    restartDirty_.clear();
    ++revision_;
    dirty_.include(0, 0);
    return eve::editing::applied<void>();
}

ProjectSettingsSchema defaultProjectSettingsSchema() {
    ProjectSettingsSchema result;
    result.typeId   = "engine.project-settings";
    result.settings = {
        {property("content.asset-root", PropertyType::String, "assets"), "filesystem", true, false},
        {property("content.import-cache", PropertyType::String, ".cache/import"), "filesystem", true, false},
        {property("plugins.allow-unsigned", PropertyType::Bool, false, PropertyFlag::Dangerous), "plugins", true,
         false},
        {property("network.endpoint", PropertyType::String, ""), "network", true, false},
        {property("network.auth-token", PropertyType::String, ""), "network", true, true},
        {property("database.connector", PropertyType::Enum, "SQLite"), "database", true, false},
        {property("database.connection", PropertyType::String, ""), "database", true, true}};
    result.settings[5].property.enumItems = {"SQLite", "MySQL", "PostgreSQL"};
    return result;
}

ProjectSettingsSchema importerSettingsSchema(std::string importerId, std::vector<ProjectSettingDescriptor> settings,
                                             std::uint32_t version) {
    ProjectSettingsSchema result;
    result.typeId   = "engine.importer-settings." + importerId;
    result.version  = version;
    result.settings = std::move(settings);
    return result;
}

}  // namespace eve::editor
