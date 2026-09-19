#include "graphics/material/editing/MaterialTarget.h"

#include <tuple>
#include <utility>

namespace eve::material_editing {
namespace {

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
    result.revision = revisionValue();
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
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.material.target-mismatch"),
                                          "Material operation targets another document");
    if (operation.type != "material.property.set.v1")
        return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("editor.material.operation-unsupported"),
                                          "Unsupported material operation: " + operation.type);
    const EditorValue* pathValue = objectField(operation.payload, "path");
    const EditorValue* value = objectField(operation.payload, "value");
    const auto* path = pathValue ? pathValue->getIf<std::string>() : nullptr;
    if (!path || !value)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.material.operation-payload"),
                                          "Material operation requires path and value");
    auto descriptor = materialSchema().find(PropertyPath(*path));
    if (!descriptor)
        return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("editor.material.property-unsupported"),
                                          "Unknown material property: " + *path);
    auto valid = validateAssignment(*descriptor, *value);
    if (!valid.ok()) return valid;
    values_[*path] = *value;
    bumpRevision();
    widenDirty(0, 0);
    return eve::editing::applied<void>();
}

std::unique_ptr<IDomainOperationTarget> MaterialDocumentTarget::cloneDomainState() const {
    return std::make_unique<MaterialDocumentTarget>(*this);
}

EditorResult<void> MaterialDocumentTarget::commitDomainState(
    std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<MaterialDocumentTarget*>(candidate.get());
    if (!typed || typed->id_ != id_)
        return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.material.candidate-mismatch"),
                                          "Material candidate belongs to another target");
    *this = *typed;
    return eve::editing::applied<void>();
}

eve::Result<eve::Revision> MaterialDocumentTarget::currentRevision(const SelectionSnapshot& selection) const {
    if (!selectionMatches(selection))
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Selection does not belong to this material",
            "editor.material.selection", {}, "editor.MaterialDocumentTarget"));
    return eve::Result<eve::Revision>::success(eve::Revision(revisionValue()));
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
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.material.selection"),
                                                     "Selection does not belong to this material");
    if (mode == PropertySetMode::Reset) return makeReset(selection, path);
    if (mode != PropertySetMode::Absolute)
        return eve::editing::failed<DomainOperation>(EditorStatus::Unsupported, RuleId("editor.material.set-mode"),
                                                     "Material properties require absolute assignment");
    auto descriptor = materialSchema().find(path);
    if (!descriptor)
        return eve::editing::failed<DomainOperation>(EditorStatus::Unsupported,
                                                     RuleId("editor.material.property-unsupported"),
                                                     "Unknown material property: " + path.value());
    auto valid = validateAssignment(*descriptor, value);
    if (!valid.ok()) return EditorResult<DomainOperation>::failure(valid.status());
    const auto previous = values_.find(path.value());
    if (previous == values_.end())
        return eve::editing::failed<DomainOperation>(EditorStatus::NotFound, RuleId("editor.material.property-missing"),
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
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

EditorResult<DomainOperation> MaterialDocumentTarget::makeReset(const SelectionSnapshot& selection,
                                                                 const PropertyPath& path) const {
    auto descriptor = materialSchema().find(path);
    if (!descriptor)
        return eve::editing::failed<DomainOperation>(EditorStatus::Unsupported,
                                                     RuleId("editor.material.property-unsupported"),
                                                     "Unknown material property: " + path.value());
    return makeSet(selection, path, descriptor->defaultValue, PropertySetMode::Absolute);
}

EditorValue MaterialDocumentTarget::snapshotValue() const {
    EditorValue::Object properties;
    for (const auto& [path, value] : values_) properties[path] = value;
    EditorValue::Object root;
    root["schemaVersion"] = 9;
    root["properties"] = EditorValue(std::move(properties));
    return EditorValue(std::move(root));
}

EditorResult<void> MaterialDocumentTarget::loadSnapshot(const EditorValue& snapshot) {
    const EditorValue* versionValue = objectField(snapshot, "schemaVersion");
    const EditorValue* propertiesValue = objectField(snapshot, "properties");
    const auto* version = versionValue ? versionValue->getIf<int64_t>() : nullptr;
    const auto* properties = propertiesValue ? propertiesValue->getIf<EditorValue::Object>() : nullptr;
    if (!version || (*version < 1 || *version > 9) || !properties)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.material.snapshot-format"),
                                          "Material snapshot requires schemaVersion 1 through 9 and properties");
    auto candidate = defaults();
    const PropertySchema schemaValue = materialSchema();
    for (const auto& [path, value] : *properties) {
        auto descriptor = schemaValue.find(PropertyPath(path));
        if (!descriptor)
            return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("editor.material.snapshot-property"),
                                              "Material snapshot contains unknown property: " + path);
        auto valid = validateAssignment(*descriptor, value);
        if (!valid.ok()) return valid;
        candidate[path] = value;
    }
    values_ = std::move(candidate);
    bumpRevision();
    clearDirtyRegion();
    return eve::editing::applied<void>();
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
        diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.material.custom-shader-required"),
            DiagnosticSeverity::Error, "Custom shading requires a shader asset"));
    if (stringValue("surface.mode") == "masked" && stringValue("textures.albedo").empty())
        diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.material.mask-without-albedo"),
            DiagnosticSeverity::Warning, "Masked material has no albedo texture providing alpha"));
    if (!stringValue("textures.height").empty()) {
        const auto* scale = values_.at("parallax.scale").getIf<double>();
        if (scale && *scale == 0.0)
            diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::PreconditionViolation, RuleId("editor.material.height-without-parallax"),
                DiagnosticSeverity::Info, "Height texture is assigned while parallax scale is zero"));
    }
    const auto boolValue = [&](const char* path) {
        const auto found = values_.find(path);
        const auto* value = found == values_.end() ? nullptr : found->second.getIf<bool>();
        return value && *value;
    };
    if (boolValue("vegetation.alpha.enabled") && stringValue("vegetation.alpha.noise").empty())
        diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.material.vegetation-alpha-noise"),
            DiagnosticSeverity::Warning,
            "Vegetation alpha fading uses the neutral noise volume until a 3D noise asset is assigned"));
    return diagnostics;
}

PropertySchema MaterialDocumentTarget::materialSchema() {
    PropertySchema schema;
    schema.typeId = "graphics.material";
    schema.version = 9;
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
                                     {"textures.orm", "editor.material.orm"},
                                     {"textures.emissive", "editor.material.emissive"},
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
    schema.properties.push_back(property("vegetation.alpha.enabled", "editor.material.vegetation-alpha-enabled",
                                         "vegetation-alpha", PropertyType::Bool, false));
    auto fadeNoise = property("vegetation.alpha.noise", "editor.material.vegetation-alpha-noise",
                              "vegetation-alpha", PropertyType::AssetRef, "");
    fadeNoise.assetTypeFilters = {"texture3d"};
    schema.properties.push_back(std::move(fadeNoise));
    auto vegetationNumeric = [&](const char* path, const char* label, double value, double minimum, double maximum) {
        auto descriptor = property(path, label, "vegetation-alpha", PropertyType::Float, value);
        descriptor.numeric.minimum = minimum;
        descriptor.numeric.maximum = maximum;
        descriptor.numeric.step    = 0.01;
        schema.properties.push_back(std::move(descriptor));
    };
    vegetationNumeric("vegetation.alpha.global", "editor.material.vegetation-alpha-global", 0.0, 0.0, 1.0);
    vegetationNumeric("vegetation.alpha.variation", "editor.material.vegetation-alpha-variation", 0.0, 0.0, 1.0);
    vegetationNumeric("vegetation.alpha.glancing", "editor.material.vegetation-alpha-glancing", 0.0, 0.0, 1.0);
    vegetationNumeric("vegetation.alpha.camera", "editor.material.vegetation-alpha-camera", 1.0, 0.0, 1.0);
    vegetationNumeric("vegetation.alpha.constant", "editor.material.vegetation-alpha-constant", 0.0, 0.0, 1.0);
    vegetationNumeric("vegetation.alpha.camera-min", "editor.material.vegetation-alpha-camera-min", 0.0, 0.0,
                      1000000.0);
    vegetationNumeric("vegetation.alpha.camera-max", "editor.material.vegetation-alpha-camera-max", 100.0, 0.0,
                      1000000.0);
    vegetationNumeric("vegetation.alpha.noise-tiling", "editor.material.vegetation-alpha-noise-tiling", 1.0, 0.0,
                      1000000.0);
    schema.properties.push_back(property("vegetation.alpha.detail-fade",
                                         "editor.material.vegetation-alpha-detail-fade", "vegetation-alpha",
                                         PropertyType::Bool, false));
    schema.properties.push_back(property("vegetation.emission.enabled", "editor.material.vegetation-emission-enabled",
                                         "vegetation-emission", PropertyType::Bool, false));
    auto vegetationStageNumeric = [&](const char* path, const char* label, const char* category, double value) {
        auto descriptor = property(path, label, category, PropertyType::Float, value);
        descriptor.numeric.minimum = 0.0;
        descriptor.numeric.maximum = 1.0;
        descriptor.numeric.step    = 0.01;
        schema.properties.push_back(std::move(descriptor));
    };
    vegetationStageNumeric("vegetation.emission.minimum", "editor.material.vegetation-emission-minimum",
                           "vegetation-emission", 0.0);
    vegetationStageNumeric("vegetation.emission.maximum", "editor.material.vegetation-emission-maximum",
                           "vegetation-emission", 1.0);
    vegetationStageNumeric("vegetation.emission.phase", "editor.material.vegetation-emission-phase",
                           "vegetation-emission", 1.0);
    vegetationStageNumeric("vegetation.emission.global", "editor.material.vegetation-emission-global",
                           "vegetation-emission", 1.0);
    schema.properties.push_back(property("vegetation.gradient.enabled", "editor.material.vegetation-gradient-enabled",
                                         "vegetation-gradient", PropertyType::Bool, false));
    schema.properties.push_back(property("vegetation.gradient.color-one", "editor.material.vegetation-gradient-color-one",
                                         "vegetation-gradient", PropertyType::Color,
                                         EditorValue::Array{1.0, 1.0, 1.0, 1.0}));
    schema.properties.push_back(property("vegetation.gradient.color-two", "editor.material.vegetation-gradient-color-two",
                                         "vegetation-gradient", PropertyType::Color,
                                         EditorValue::Array{1.0, 1.0, 1.0, 1.0}));
    vegetationStageNumeric("vegetation.gradient.minimum", "editor.material.vegetation-gradient-minimum",
                           "vegetation-gradient", 0.0);
    vegetationStageNumeric("vegetation.gradient.maximum", "editor.material.vegetation-gradient-maximum",
                           "vegetation-gradient", 1.0);
    schema.properties.push_back(property("vegetation.translucency.enabled",
                                         "editor.material.vegetation-translucency-enabled",
                                         "vegetation-translucency", PropertyType::Bool, false));
    schema.properties.push_back(property("vegetation.translucency.color",
                                         "editor.material.vegetation-translucency-color",
                                         "vegetation-translucency", PropertyType::Color,
                                         EditorValue::Array{1.0, 1.0, 1.0, 1.0}));
    auto translucencyNumeric = [&](const char* name, double value, double minimum, double maximum) {
        const std::string path  = std::string("vegetation.translucency.") + name;
        const std::string label = std::string("editor.material.vegetation-translucency-") + name;
        auto descriptor = property(path.c_str(), label.c_str(), "vegetation-translucency", PropertyType::Float, value);
        descriptor.numeric.minimum = minimum;
        descriptor.numeric.maximum = maximum;
        descriptor.numeric.step    = 0.01;
        schema.properties.push_back(std::move(descriptor));
    };
    translucencyNumeric("intensity", 0.0, 0.0, 1.0);
    translucencyNumeric("strength", 1.0, 0.0, 50.0);
    translucencyNumeric("normal-distortion", 0.5, 0.0, 1.0);
    translucencyNumeric("scattering", 2.0, 1.0, 50.0);
    translucencyNumeric("direct", 0.9, 0.0, 1.0);
    translucencyNumeric("ambient", 0.1, 0.0, 1.0);
    translucencyNumeric("shadow", 0.5, 0.0, 1.0);
    translucencyNumeric("mask-amount", 0.0, 0.0, 1.0);
    translucencyNumeric("global", 1.0, 0.0, 1000000.0);
    translucencyNumeric("overlay", 1.0, 0.0, 1000000.0);
    translucencyNumeric("mask-minimum", 0.0, 0.0, 1.0);
    translucencyNumeric("mask-maximum", 0.0, 0.0, 1.0);
    schema.properties.push_back(property("vegetation.color.enabled", "editor.material.vegetation-color-enabled",
                                         "vegetation-color", PropertyType::Bool, false));
    for (const auto& [path, label, value] : {
             std::tuple{"vegetation.color.field", "editor.material.vegetation-field-color",
                        EditorValue::Array{1.0, 1.0, 1.0, 0.0}},
             {"vegetation.color.overlay-color", "editor.material.vegetation-overlay-color",
              EditorValue::Array{1.0, 1.0, 1.0, 1.0}},
             {"vegetation.color.vertex-occlusion-color", "editor.material.vegetation-vertex-occlusion-color",
              EditorValue::Array{1.0, 1.0, 1.0, 1.0}}})
        schema.properties.push_back(property(path, label, "vegetation-color", PropertyType::Color, value));
    auto colorNumeric = [&](const char* name, double value, double minimum, double maximum) {
        const std::string path  = std::string("vegetation.color.") + name;
        const std::string label = std::string("editor.material.vegetation-color-") + name;
        auto descriptor = property(path.c_str(), label.c_str(), "vegetation-color", PropertyType::Float, value);
        descriptor.numeric.minimum = minimum;
        descriptor.numeric.maximum = maximum;
        descriptor.numeric.step    = 0.01;
        schema.properties.push_back(std::move(descriptor));
    };
    for (const auto& [name, value] : {
             std::pair{"overlay", 0.0}, {"wetness", 0.0}, {"overlay-variation", 0.5},
             {"overlay-projection", 0.5}, {"vertex-occlusion-alpha", 0.5019608},
             {"overlay-normal", 0.5}, {"wetness-normal", 0.5}, {"overlay-smoothness", 0.5},
             {"wetness-contrast", 0.5}, {"overlay-subsurface", 0.5}, {"colors-coverage", 1.0},
             {"colors-mask", 1.0}, {"colors-variation", 0.5}, {"color-mask-minimum", 0.1},
             {"color-mask-maximum", 0.2}, {"overlay-mask-minimum", 0.1}, {"overlay-mask-maximum", 0.2},
             {"vertex-occlusion-minimum", 0.0}, {"vertex-occlusion-maximum", 1.0}})
        colorNumeric(name, value, 0.0, 1.0);
    colorNumeric("colors-intensity", 1.0, 0.0, 2.0);
    colorNumeric("alpha-threshold-offset", 0.0, -0.5, 0.5);
    schema.properties.push_back(property("vegetation.color.invert-vertex-occlusion",
                                         "editor.material.vegetation-color-invert-vertex-occlusion",
                                         "vegetation-color", PropertyType::Bool, false));
    schema.properties.push_back(property("vegetation.color.invert-vertex-occlusion-colors",
                                         "editor.material.vegetation-color-invert-vertex-occlusion-colors",
                                         "vegetation-color", PropertyType::Bool, false));
    auto backface = property("vegetation.color.backface-normal", "editor.material.vegetation-backface-normal",
                             "vegetation-color", PropertyType::Enum, "flip");
    backface.enumItems = {"flip", "mirror", "same"};
    schema.properties.push_back(std::move(backface));
    schema.properties.push_back(property("vegetation.detail.enabled", "editor.material.vegetation-detail-enabled",
                                         "vegetation-detail", PropertyType::Bool, false));
    for (const auto& [path, label] : {
             std::pair{"vegetation.detail.albedo", "editor.material.vegetation-detail-albedo"},
             {"vegetation.detail.normal", "editor.material.vegetation-detail-normal"},
             {"vegetation.detail.mask", "editor.material.vegetation-detail-mask"}}) {
        auto descriptor =
            property(path, label, "vegetation-detail", PropertyType::AssetRef, EditorValue(std::string{}));
        descriptor.assetTypeFilters = {"texture"};
        schema.properties.push_back(std::move(descriptor));
    }
    schema.properties.push_back(property("vegetation.detail.color", "editor.material.vegetation-detail-color",
                                         "vegetation-detail", PropertyType::Color,
                                         EditorValue::Array{1.0, 1.0, 1.0, 1.0}));
    schema.properties.push_back(property("vegetation.detail.color-two", "editor.material.vegetation-detail-color-two",
                                         "vegetation-detail", PropertyType::Color,
                                         EditorValue::Array{1.0, 1.0, 1.0, 1.0}));
    schema.properties.push_back(property("vegetation.detail.uv-scale", "editor.material.vegetation-detail-uv-scale",
                                         "vegetation-detail", PropertyType::Vec2,
                                         EditorValue::Array{1.0, 1.0}));
    schema.properties.push_back(property("vegetation.detail.uv-offset", "editor.material.vegetation-detail-uv-offset",
                                         "vegetation-detail", PropertyType::Vec2,
                                         EditorValue::Array{0.0, 0.0}));
    schema.properties.push_back(property("vegetation.detail.inverse-uv-scale",
                                         "editor.material.vegetation-detail-inverse-uv-scale",
                                         "vegetation-detail", PropertyType::Bool, false));
    auto detailEnum = [&](const char* name, std::vector<std::string> choices, const char* defaultValue) {
        const std::string path  = std::string("vegetation.detail.") + name;
        const std::string label = std::string("editor.material.vegetation-detail-") + name;
        auto descriptor = property(path.c_str(), label.c_str(), "vegetation-detail", PropertyType::Enum,
                                   EditorValue(std::string(defaultValue)));
        descriptor.enumItems = std::move(choices);
        schema.properties.push_back(std::move(descriptor));
    };
    detailEnum("uv-mode", {"primary", "detail", "world"}, "primary");
    detailEnum("color-mode", {"single", "variation"}, "single");
    detailEnum("blend-mode", {"multiply", "replace"}, "multiply");
    detailEnum("alpha-mode", {"main", "detail"}, "detail");
    detailEnum("mask-mode", {"regular", "inverse"}, "regular");
    detailEnum("mesh-mode", {"regular", "inverse"}, "regular");
    auto detailNumeric = [&](const char* name, double value, double minimum, double maximum) {
        const std::string path  = std::string("vegetation.detail.") + name;
        const std::string label = std::string("editor.material.vegetation-detail-") + name;
        auto descriptor = property(path.c_str(), label.c_str(), "vegetation-detail", PropertyType::Float, value);
        descriptor.numeric.minimum = minimum;
        descriptor.numeric.maximum = maximum;
        descriptor.numeric.step    = 0.01;
        schema.properties.push_back(std::move(descriptor));
    };
    detailNumeric("value", 1.0, 0.0, 1.0);
    detailNumeric("normal-value", 1.0, -8.0, 8.0);
    for (const auto& [name, value] : {
             std::pair{"normal-blend", 1.0}, {"albedo-value", 1.0}, {"metallic", 0.0}, {"occlusion", 1.0},
             {"smoothness", 1.0}, {"blend-minimum", 0.0}, {"blend-maximum", 1.0}, {"mask-minimum", 0.0},
             {"mask-maximum", 1.0}, {"mesh-minimum", 0.0}, {"mesh-maximum", 1.0}})
        detailNumeric(name, value, 0.0, 1.0);
    const auto fieldGroup = [&](const char* group, EditorValue::Array fallback) {
        const std::string prefix = std::string("vegetation.") + group;
        const std::string label  = std::string("editor.material.vegetation-") + group;
        schema.properties.push_back(property((prefix + ".enabled").c_str(), (label + "-enabled").c_str(),
                                             "vegetation-fields", PropertyType::Bool, false));
        auto texture = property((prefix + ".texture").c_str(), (label + "-texture").c_str(),
                                "vegetation-fields", PropertyType::AssetRef, EditorValue(std::string{}));
        texture.assetTypeFilters = {"texture2d-array"};
        schema.properties.push_back(std::move(texture));
        auto layer = property((prefix + ".layer").c_str(), (label + "-layer").c_str(),
                              "vegetation-fields", PropertyType::Int, std::int64_t(0));
        layer.numeric.minimum = 0;
        layer.numeric.maximum = 8;
        layer.numeric.step    = 1;
        schema.properties.push_back(std::move(layer));
        schema.properties.push_back(property((prefix + ".fallback").c_str(), (label + "-fallback").c_str(),
                                             "vegetation-fields", PropertyType::Vec4, std::move(fallback)));
        schema.properties.push_back(property((prefix + ".coords").c_str(), (label + "-coords").c_str(),
                                             "vegetation-fields", PropertyType::Vec4,
                                             EditorValue::Array{1.0, 1.0, 0.0, 0.0}));
        schema.properties.push_back(property((prefix + ".use-pivot-position").c_str(),
                                             (label + "-use-pivot-position").c_str(), "vegetation-fields",
                                             PropertyType::Bool, false));
        for (std::int64_t i = 0; i < 9; ++i) {
            const std::string path = prefix + ".usage-" + std::to_string(i);
            auto usage = property(path.c_str(), (label + "-usage").c_str(), "vegetation-fields",
                                  PropertyType::Float, 0.0);
            usage.numeric.minimum = 0.0;
            usage.numeric.maximum = 1.0;
            usage.numeric.step    = 0.01;
            schema.properties.push_back(std::move(usage));
        }
    };
    fieldGroup("extras", EditorValue::Array{1.0, 0.0, 0.0, 1.0});
    fieldGroup("colors", EditorValue::Array{1.0, 1.0, 1.0, 0.0});
    schema.properties.push_back(property("vegetation.vertex.enabled", "editor.material.vegetation-vertex-enabled",
                                         "vegetation-vertex", PropertyType::Bool, false));
    auto vertexTexture = property("vegetation.vertex.texture", "editor.material.vegetation-vertex-texture",
                                  "vegetation-vertex", PropertyType::AssetRef, EditorValue(std::string{}));
    vertexTexture.assetTypeFilters = {"texture2d-array"};
    schema.properties.push_back(std::move(vertexTexture));
    auto vertexLayer = property("vegetation.vertex.layer", "editor.material.vegetation-vertex-layer",
                                "vegetation-vertex", PropertyType::Int, std::int64_t(0));
    vertexLayer.numeric.minimum = 0;
    vertexLayer.numeric.maximum = 8;
    vertexLayer.numeric.step    = 1;
    schema.properties.push_back(std::move(vertexLayer));
    schema.properties.push_back(property("vegetation.vertex.fallback", "editor.material.vegetation-vertex-fallback",
                                         "vegetation-vertex", PropertyType::Vec4,
                                         EditorValue::Array{0.0, 0.0, 0.0, 1.0}));
    schema.properties.push_back(property("vegetation.vertex.coords", "editor.material.vegetation-vertex-coords",
                                         "vegetation-vertex", PropertyType::Vec4,
                                         EditorValue::Array{1.0, 1.0, 0.0, 0.0}));
    for (std::int64_t i = 0; i < 9; ++i) {
        const std::string path = "vegetation.vertex.usage-" + std::to_string(i);
        auto usage = property(path.c_str(), "editor.material.vegetation-vertex-usage", "vegetation-vertex",
                              PropertyType::Float, 0.0);
        usage.numeric.minimum = 0.0;
        usage.numeric.maximum = 1.0;
        usage.numeric.step    = 0.01;
        schema.properties.push_back(std::move(usage));
    }
    auto vertexSource = property("vegetation.vertex.source", "editor.material.vegetation-vertex-source",
                                 "vegetation-vertex", PropertyType::Enum, "rest-mesh");
    vertexSource.enumItems = {"rest-mesh", "cpu-deformed", "gpu-fields"};
    schema.properties.push_back(std::move(vertexSource));
    const auto vertexNumeric = [&](const char* name, double defaultValue, double minimum, double maximum) {
        const std::string path  = std::string("vegetation.vertex.") + name;
        const std::string label = std::string("editor.material.vegetation-vertex-") + name;
        auto descriptor = property(path.c_str(), label.c_str(), "vegetation-vertex", PropertyType::Float,
                                   defaultValue);
        descriptor.numeric.minimum = minimum;
        descriptor.numeric.maximum = maximum;
        descriptor.numeric.step    = 0.01;
        schema.properties.push_back(std::move(descriptor));
    };
    vertexNumeric("global-size", 1.0, 0.0, 1.0);
    vertexNumeric("size-fade-start", 0.0, 0.0, 1000000.0);
    vertexNumeric("size-fade-end", 100.0, 0.0, 1000000.0);
    vertexNumeric("distance-fade-bias", 1.0, 0.0001, 1000000.0);
    auto motionMode = property("vegetation.motion.mode", "editor.material.vegetation-motion-mode",
                               "vegetation-motion", PropertyType::Enum, "disabled");
    motionMode.enumItems = {"disabled", "object"};
    schema.properties.push_back(std::move(motionMode));
    for (const auto& [name, filter] : {
             std::pair{"texture", "texture2d-array"}, {"noise", "texture2d"}}) {
        const std::string path  = std::string("vegetation.motion.") + name;
        const std::string label = std::string("editor.material.vegetation-motion-") + name;
        auto texture = property(path.c_str(), label.c_str(), "vegetation-motion", PropertyType::AssetRef,
                                EditorValue(std::string{}));
        texture.assetTypeFilters = {filter};
        schema.properties.push_back(std::move(texture));
    }
    auto motionLayer = property("vegetation.motion.layer", "editor.material.vegetation-motion-layer",
                                "vegetation-motion", PropertyType::Int, std::int64_t(0));
    motionLayer.numeric.minimum = 0;
    motionLayer.numeric.maximum = 8;
    motionLayer.numeric.step    = 1;
    schema.properties.push_back(std::move(motionLayer));
    schema.properties.push_back(property("vegetation.motion.fallback", "editor.material.vegetation-motion-fallback",
                                         "vegetation-motion", PropertyType::Vec4,
                                         EditorValue::Array{1.0, 0.0, 0.5, 0.0}));
    schema.properties.push_back(property("vegetation.motion.coords", "editor.material.vegetation-motion-coords",
                                         "vegetation-motion", PropertyType::Vec4,
                                         EditorValue::Array{1.0, 1.0, 0.0, 0.0}));
    schema.properties.push_back(property("vegetation.motion.global-direction",
                                         "editor.material.vegetation-motion-global-direction", "vegetation-motion",
                                         PropertyType::Vec2, EditorValue::Array{1.0, 0.0}));
    schema.properties.push_back(property("vegetation.motion.world-origin",
                                         "editor.material.vegetation-motion-world-origin", "vegetation-motion",
                                         PropertyType::Vec3, EditorValue::Array{0.0, 0.0, 0.0}));
    for (std::int64_t i = 0; i < 9; ++i) {
        const std::string path = "vegetation.motion.usage-" + std::to_string(i);
        auto usage = property(path.c_str(), "editor.material.vegetation-motion-usage", "vegetation-motion",
                              PropertyType::Float, 0.0);
        usage.numeric.minimum = 0.0;
        usage.numeric.maximum = 1.0;
        usage.numeric.step    = 0.01;
        schema.properties.push_back(std::move(usage));
    }
    const auto motionNumeric = [&](const char* name, double defaultValue, double minimum, double maximum) {
        const std::string path  = std::string("vegetation.motion.") + name;
        const std::string label = std::string("editor.material.vegetation-motion-") + name;
        auto descriptor = property(path.c_str(), label.c_str(), "vegetation-motion", PropertyType::Float,
                                   defaultValue);
        descriptor.numeric.minimum = minimum;
        descriptor.numeric.maximum = maximum;
        descriptor.numeric.step    = 0.01;
        schema.properties.push_back(std::move(descriptor));
    };
    motionNumeric("time", 0.0, -1000000000000.0, 1000000000000.0);
    for (const auto& [name, value] : {
             std::pair{"dynamic-mode", 0.0}, {"rigidity", 0.5}, {"facing", 0.5}, {"interaction-mask", 1.0}})
        motionNumeric(name, value, 0.0, 1.0);
    for (const auto& [name, value] : {
             std::pair{"bending", 0.2}, {"bending-speed", 2.0}, {"bending-scale", 1.0},
             {"bending-variation", 0.0}, {"branch", 0.2}, {"rolling", 0.2}, {"branch-speed", 6.0},
             {"branch-scale", 3.0}, {"branch-variation", 0.0}, {"flutter", 0.2}, {"flutter-speed", 20.0},
             {"flutter-scale", 10.0}, {"flutter-variation", 0.0}, {"global-bending", 1.0},
             {"global-branch", 1.0}, {"global-flutter", 1.0}, {"noise-tiling", 1.0},
             {"interaction", 1.0}, {"fade-distance", 100.0}, {"perspective-push", 0.0},
             {"perspective-noise", 0.0}, {"perspective-angle", 1.0}})
        motionNumeric(name, value, 0.0, 1000000.0);
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
