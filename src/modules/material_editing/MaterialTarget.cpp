#include "material_editing/MaterialTarget.h"

#include <tuple>
#include <utility>

namespace eve::material_editing {
namespace {

template <class T>
EditorResult<T> materialError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

PropertyDescriptor property(const char* path, const char* label, const char* category,
                            PropertyType type, EditorValue defaultValue,
                            PropertyFlag flags = PropertyFlag::Runtime) {
    PropertyDescriptor result;
    result.path = PropertyPath(path);
    result.displayNameKey = label;
    result.category = category;
    result.type = type;
    result.flags = flags;
    result.defaultValue = std::move(defaultValue);
    return result;
}

const EditorValue* objectField(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

}  // namespace

MaterialDocumentTarget::MaterialDocumentTarget(std::string id)
    : id_(std::move(id)), values_(defaults()) {}

TargetDescriptor MaterialDocumentTarget::describe() const {
    TargetDescriptor result;
    result.id = TargetId(id_);
    result.type = "material-document";
    result.revision = revision_;
    result.capabilities = {IPropertyProvider::editingCapabilityId(),
                           eve::editing::IEditingSnapshotProvider::editingCapabilityId()};
    return result;
}

void* MaterialDocumentTarget::queryCapability(const CapabilityId& capability) {
    if (capability == IPropertyProvider::editingCapabilityId())
        return static_cast<IPropertyProvider*>(this);
    if (capability == eve::editing::IEditingSnapshotProvider::editingCapabilityId())
        return static_cast<eve::editing::IEditingSnapshotProvider*>(this);
    return nullptr;
}

EditorResult<void> MaterialDocumentTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_))
        return materialError<void>(EditorStatus::Rejected, "editor.material.target-mismatch",
                                   "Material operation targets another document");
    if (operation.type != "material.property.set.v1")
        return materialError<void>(EditorStatus::Unsupported, "editor.material.operation-unsupported",
                                   "Unsupported material operation: " + operation.type);
    const EditorValue* pathValue = objectField(operation.payload, "path");
    const EditorValue* value = objectField(operation.payload, "value");
    const auto* path = pathValue ? pathValue->getIf<std::string>() : nullptr;
    if (!path || !value)
        return materialError<void>(EditorStatus::Rejected, "editor.material.operation-payload",
                                   "Material operation requires path and value");
    auto descriptor = materialSchema().find(PropertyPath(*path));
    if (!descriptor)
        return materialError<void>(EditorStatus::Unsupported, "editor.material.property-unsupported",
                                   "Unknown material property: " + *path);
    auto valid = validateAssignment(*descriptor, *value);
    if (!valid.isAccepted()) return valid;
    values_[*path] = *value;
    ++revision_;
    dirty_.include(0, 0);
    return EditorResult<void>::applied();
}

std::unique_ptr<IDomainOperationTarget> MaterialDocumentTarget::cloneDomainState() const {
    return std::make_unique<MaterialDocumentTarget>(*this);
}

EditorResult<void> MaterialDocumentTarget::commitDomainState(
    std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<MaterialDocumentTarget*>(candidate.get());
    if (!typed || typed->id_ != id_)
        return materialError<void>(EditorStatus::Conflict, "editor.material.candidate-mismatch",
                                   "Material candidate belongs to another target");
    *this = *typed;
    return EditorResult<void>::applied();
}

eve::Result<eve::Revision> MaterialDocumentTarget::currentRevision(const SelectionSnapshot& selection) const {
    if (!selectionMatches(selection))
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Selection does not belong to this material",
            "editor.material.selection", {}, "editor.MaterialDocumentTarget"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

PropertySchema MaterialDocumentTarget::schema(const SelectionSnapshot&) const { return materialSchema(); }

PropertyReadResult MaterialDocumentTarget::read(const SelectionSnapshot& selection,
                                                const PropertyPath& path) const {
    if (!selectionMatches(selection)) return {};
    const auto found = values_.find(path.value());
    if (found == values_.end()) return {};
    return {PropertyReadState::Value, found->second, {}};
}

EditorResult<DomainOperation> MaterialDocumentTarget::makeSet(const SelectionSnapshot& selection,
                                                               const PropertyPath& path,
                                                               const EditorValue& value,
                                                               PropertySetMode mode) const {
    if (!selectionMatches(selection))
        return materialError<DomainOperation>(EditorStatus::Rejected, "editor.material.selection",
                                              "Selection does not belong to this material");
    if (mode == PropertySetMode::Reset) return makeReset(selection, path);
    if (mode != PropertySetMode::Absolute)
        return materialError<DomainOperation>(EditorStatus::Unsupported, "editor.material.set-mode",
                                              "Material properties require absolute assignment");
    auto descriptor = materialSchema().find(path);
    if (!descriptor)
        return materialError<DomainOperation>(EditorStatus::Unsupported, "editor.material.property-unsupported",
                                              "Unknown material property: " + path.value());
    auto valid = validateAssignment(*descriptor, value);
    if (!valid.isAccepted()) {
        EditorResult<DomainOperation> failed;
        failed.status = valid.status;
        failed.diagnostics = std::move(valid.diagnostics);
        return failed;
    }
    const auto previous = values_.find(path.value());
    if (previous == values_.end())
        return materialError<DomainOperation>(EditorStatus::NotFound, "editor.material.property-missing",
                                              "Material property has no current value");
    auto payload = [&](const EditorValue& assigned) {
        EditorValue::Object object;
        object["path"] = path.value();
        object["value"] = assigned;
        return EditorValue(std::move(object));
    };
    DomainOperation operation;
    operation.type = "material.property.set.v1";
    operation.target = TargetId(id_);
    operation.payload = payload(value);
    operation.inverse = payload(previous->second);
    operation.hasInverse = true;
    operation.affectedProperties.push_back(path.value());
    operation.mergeKey = "material:" + id_ + ":" + path.value();
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorResult<DomainOperation> MaterialDocumentTarget::makeReset(const SelectionSnapshot& selection,
                                                                 const PropertyPath& path) const {
    auto descriptor = materialSchema().find(path);
    if (!descriptor)
        return materialError<DomainOperation>(EditorStatus::Unsupported, "editor.material.property-unsupported",
                                              "Unknown material property: " + path.value());
    return makeSet(selection, path, descriptor->defaultValue, PropertySetMode::Absolute);
}

EditorValue MaterialDocumentTarget::snapshotValue() const {
    EditorValue::Object properties;
    for (const auto& [path, value] : values_) properties[path] = value;
    EditorValue::Object root;
    root["schemaVersion"] = 1;
    root["properties"] = EditorValue(std::move(properties));
    return EditorValue(std::move(root));
}

EditorResult<void> MaterialDocumentTarget::loadSnapshot(const EditorValue& snapshot) {
    const EditorValue* versionValue = objectField(snapshot, "schemaVersion");
    const EditorValue* propertiesValue = objectField(snapshot, "properties");
    const auto* version = versionValue ? versionValue->getIf<int64_t>() : nullptr;
    const auto* properties = propertiesValue ? propertiesValue->getIf<EditorValue::Object>() : nullptr;
    if (!version || *version != 1 || !properties)
        return materialError<void>(EditorStatus::Rejected, "editor.material.snapshot-format",
                                   "Material snapshot requires schemaVersion 1 and properties");
    auto candidate = defaults();
    const PropertySchema schemaValue = materialSchema();
    for (const auto& [path, value] : *properties) {
        auto descriptor = schemaValue.find(PropertyPath(path));
        if (!descriptor)
            return materialError<void>(EditorStatus::Unsupported, "editor.material.snapshot-property",
                                       "Material snapshot contains unknown property: " + path);
        auto valid = validateAssignment(*descriptor, value);
        if (!valid.isAccepted()) return valid;
        candidate[path] = value;
    }
    values_ = std::move(candidate);
    ++revision_;
    dirty_.clear();
    return EditorResult<void>::applied();
}

std::vector<EditorDiagnostic> MaterialDocumentTarget::validate() const {
    std::vector<EditorDiagnostic> diagnostics;
    const auto stringValue = [&](const char* path) -> std::string {
        const auto found = values_.find(path);
        if (found == values_.end()) return {};
        const auto* value = found->second.getIf<std::string>();
        return value ? *value : std::string{};
    };
    if (stringValue("shading.model") == "custom" && stringValue("textures.shader").empty())
        diagnostics.push_back({RuleId("editor.material.custom-shader-required"), DiagnosticSeverity::Error,
                               "Custom shading requires a shader asset"});
    if (stringValue("surface.mode") == "masked" && stringValue("textures.albedo").empty())
        diagnostics.push_back({RuleId("editor.material.mask-without-albedo"), DiagnosticSeverity::Warning,
                               "Masked material has no albedo texture providing alpha"});
    if (!stringValue("textures.height").empty()) {
        const auto* scale = values_.at("parallax.scale").getIf<double>();
        if (scale && *scale == 0.0)
            diagnostics.push_back({RuleId("editor.material.height-without-parallax"), DiagnosticSeverity::Info,
                                   "Height texture is assigned while parallax scale is zero"});
    }
    return diagnostics;
}

PropertySchema MaterialDocumentTarget::materialSchema() {
    PropertySchema schema;
    schema.typeId = "graphics.material";
    schema.version = 1;
    auto shading = property("shading.model", "editor.material.shading-model", "shading",
                            PropertyType::Enum, "pbr");
    shading.enumItems = {"pbr", "unlit", "hair", "custom"};
    schema.properties.push_back(std::move(shading));
    auto tint = property("shading.tint", "editor.material.tint", "shading", PropertyType::Color,
                         EditorValue::Array{1.0, 1.0, 1.0, 1.0});
    schema.properties.push_back(std::move(tint));
    auto numeric = [&](const char* path, const char* label, double value, double minimum, double maximum) {
        auto descriptor = property(path, label, "shading", PropertyType::Float, value);
        descriptor.numeric.minimum = minimum;
        descriptor.numeric.maximum = maximum;
        descriptor.numeric.step = 0.01;
        schema.properties.push_back(std::move(descriptor));
    };
    numeric("shading.metallic", "editor.material.metallic", 0.0, 0.0, 1.0);
    numeric("shading.roughness", "editor.material.roughness", 0.45, 0.04, 1.0);
    for (const auto& [path, label] : {std::pair{"textures.albedo", "editor.material.albedo"},
                                     {"textures.normal", "editor.material.normal"},
                                     {"textures.height", "editor.material.height"},
                                     {"textures.shader", "editor.material.shader"}}) {
        auto descriptor = property(path, label, "textures", PropertyType::AssetRef, "");
        descriptor.assetTypeFilters = path == std::string("textures.shader")
                                          ? std::vector<std::string>{"shader"}
                                          : std::vector<std::string>{"texture"};
        schema.properties.push_back(std::move(descriptor));
    }
    auto surface = property("surface.mode", "editor.material.surface-mode", "surface",
                            PropertyType::Enum, "opaque");
    surface.enumItems = {"opaque", "masked", "transparent"};
    schema.properties.push_back(std::move(surface));
    auto blend = property("surface.blend", "editor.material.blend-mode", "surface",
                          PropertyType::Enum, "alpha");
    blend.enumItems = {"alpha", "premultiplied", "additive", "multiply"};
    schema.properties.push_back(std::move(blend));
    numeric("surface.alpha-cutoff", "editor.material.alpha-cutoff", 0.5, 0.0, 1.0);
    numeric("parallax.scale", "editor.material.parallax-scale", 0.0, 0.0, 0.25);
    for (const auto& [path, label, value] : {
             std::tuple{"surface.depth-write", "editor.material.depth-write", false},
             {"surface.double-sided", "editor.material.double-sided", false},
             {"lighting.receive", "editor.material.receive-light", true},
             {"shadow.cast", "editor.material.cast-shadow", true},
             {"shadow.receive", "editor.material.receive-shadow", true}})
        schema.properties.push_back(property(path, label, "surface", PropertyType::Bool, value));
    return schema;
}

std::map<std::string, EditorValue> MaterialDocumentTarget::defaults() {
    std::map<std::string, EditorValue> result;
    for (const PropertyDescriptor& descriptor : materialSchema().properties)
        result[descriptor.path.value()] = descriptor.defaultValue;
    return result;
}

EditorResult<void> MaterialDocumentTarget::validateAssignment(const PropertyDescriptor& descriptor,
                                                               const EditorValue& value) {
    return validatePropertyValue(descriptor, value);
}

bool MaterialDocumentTarget::selectionMatches(const SelectionSnapshot& selection) const {
    if (selection.items.size() != 1) return false;
    return selection.items.front().target == TargetId(id_);
}

}  // namespace eve::material_editing
