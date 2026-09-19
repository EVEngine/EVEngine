#include "fluids/editing/VolumeFluidTarget.h"
#include "fluids/VolumeFluid.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::fluids_editing {
namespace {
constexpr const char*   kSchemaId                         = "eve.volume-fluid-authoring";
constexpr std::int64_t  kSchemaVersion                    = 1;
constexpr std::uint64_t kEstimatedScratchBytesPerParticle = 2ULL * sizeof(fluids::VolumeFluidParticle) +
                                                            12ULL * sizeof(glm::vec3) + 2ULL * sizeof(glm::vec4) +
                                                            5ULL * sizeof(float) + 4ULL * sizeof(std::uint32_t);
EditorDiagnostic targetDiagnostic(const char* rule, DiagnosticSeverity severity, std::string message) {
    return eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument, RuleId(rule), severity,
                                        std::move(message));
}
const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) {
        return nullptr;
    }
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}
EditorValue vec3(double x, double y, double z) { return EditorValue::Array{x, y, z}; }
EditorValue settingsValue(const VolumeFluidAuthoringSettings& s) {
    return EditorValue::Object{{"capacity", static_cast<std::int64_t>(s.capacity)},
                               {"previewParticles", static_cast<std::int64_t>(s.previewParticles)},
                               {"spacing", s.spacing},
                               {"gravity", vec3(s.gravityX, s.gravityY, s.gravityZ)},
                               {"minimum", vec3(s.minimumX, s.minimumY, s.minimumZ)},
                               {"maximum", vec3(s.maximumX, s.maximumY, s.maximumZ)},
                               {"iterations", static_cast<std::int64_t>(s.iterations)}};
}
bool readVec3(const EditorValue& value, const char* key, double& x, double& y, double& z) {
    const auto* item   = field(value, key);
    const auto* values = item ? item->getIf<EditorValue::Array>() : nullptr;
    if (!values || values->size() != 3) {
        return false;
    }
    const auto* vx = (*values)[0].getIf<double>();
    const auto* vy = (*values)[1].getIf<double>();
    const auto* vz = (*values)[2].getIf<double>();
    if (!vx || !vy || !vz || !std::isfinite(*vx) || !std::isfinite(*vy) || !std::isfinite(*vz)) {
        return false;
    }
    x = *vx;
    y = *vy;
    z = *vz;
    return true;
}
std::vector<EditorDiagnostic> validateSettings(const VolumeFluidAuthoringSettings& s) {
    std::vector<EditorDiagnostic> out;
    const double                  finite[]{s.spacing,  s.gravityX, s.gravityY, s.gravityZ, s.minimumX,
                                           s.minimumY, s.minimumZ, s.maximumX, s.maximumY, s.maximumZ};
    if (std::any_of(std::begin(finite), std::end(finite), [](double v) { return !std::isfinite(v); })) {
        out.push_back(targetDiagnostic("editor.volume-fluid.nonfinite", DiagnosticSeverity::Error,
                                       "Volume fluid settings must be finite"));
    }
    if (s.capacity == 0 || s.capacity > 1000000 || s.previewParticles > s.capacity || s.spacing < 0.0001 ||
        s.spacing > 100.0 || s.iterations == 0 || s.iterations > 20) {
        out.push_back(targetDiagnostic("editor.volume-fluid.range", DiagnosticSeverity::Error,
                                       "Volume fluid settings are outside runtime ranges"));
    }
    if (s.maximumX - s.minimumX < s.spacing || s.maximumY - s.minimumY < s.spacing ||
        s.maximumZ - s.minimumZ < s.spacing) {
        out.push_back(targetDiagnostic("editor.volume-fluid.bounds", DiagnosticSeverity::Error,
                                       "Every container axis must fit at least one particle spacing"));
    }
    if (s.previewParticles > 4096) {
        out.push_back(targetDiagnostic("editor.volume-fluid.preview-size", DiagnosticSeverity::Warning,
                                       "Large CPU previews can exceed the interactive frame budget"));
    }
    return out;
}
EditorResult<VolumeFluidAuthoringSettings> parseSettings(const EditorValue& value) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object || object->size() != 7)
        return eve::editing::failed<VolumeFluidAuthoringSettings>(EditorStatus::Rejected,
                                                                  RuleId("editor.volume-fluid.fields"),
                                                                  "Volume fluid settings require exactly seven fields");
    const auto* cv         = field(value, "capacity");
    const auto* pv         = field(value, "previewParticles");
    const auto* sv         = field(value, "spacing");
    const auto* iv         = field(value, "iterations");
    const auto* capacity   = cv ? cv->getIf<std::int64_t>() : nullptr;
    const auto* preview    = pv ? pv->getIf<std::int64_t>() : nullptr;
    const auto* spacing    = sv ? sv->getIf<double>() : nullptr;
    const auto* iterations = iv ? iv->getIf<std::int64_t>() : nullptr;
    if (!capacity || !preview || !spacing || !iterations || *capacity < 0 || *preview < 0 || *iterations < 0 ||
        *capacity > 1000000 || *preview > 1000000 || *iterations > 20)
        return eve::editing::failed<VolumeFluidAuthoringSettings>(
            EditorStatus::Rejected, RuleId("editor.volume-fluid.types"), "Volume fluid scalar fields are invalid");
    VolumeFluidAuthoringSettings s;
    s.capacity         = static_cast<std::uint32_t>(*capacity);
    s.previewParticles = static_cast<std::uint32_t>(*preview);
    s.spacing          = *spacing;
    s.iterations       = static_cast<std::uint32_t>(*iterations);
    if (!readVec3(value, "gravity", s.gravityX, s.gravityY, s.gravityZ) ||
        !readVec3(value, "minimum", s.minimumX, s.minimumY, s.minimumZ) ||
        !readVec3(value, "maximum", s.maximumX, s.maximumY, s.maximumZ))
        return eve::editing::failed<VolumeFluidAuthoringSettings>(EditorStatus::Rejected,
                                                                  RuleId("editor.volume-fluid.vectors"),
                                                                  "Gravity and bounds must be finite Vec3 values");
    const auto diagnostics = validateSettings(s);
    if (std::any_of(diagnostics.begin(), diagnostics.end(),
                    [](const auto& d) { return d.severity() == DiagnosticSeverity::Error; })) {
        return eve::editing::failed<VolumeFluidAuthoringSettings>(
            EditorStatus::Rejected, RuleId("editor.volume-fluid.invalid"), "Volume fluid settings are invalid");
    }
    return eve::editing::applied<VolumeFluidAuthoringSettings>(s);
}
PropertyDescriptor descriptor(const char* path, PropertyType type, EditorValue value, const char* category,
                              double minimum = 0, double maximum = 0, bool ranged = false) {
    PropertyDescriptor d;
    d.path           = PropertyPath(path);
    d.displayNameKey = std::string("editor.volume-fluid.") + path;
    d.category       = category;
    d.type           = type;
    d.flags          = PropertyFlag::Runtime;
    d.defaultValue   = std::move(value);
    if (ranged) {
        d.numeric.minimum = minimum;
        d.numeric.maximum = maximum;
    }
    return d;
}
EditorValue setting(const VolumeFluidAuthoringSettings& s, const std::string& path) {
    return settingsValue(s).getIf<EditorValue::Object>()->at(path);
}
}  // namespace

VolumeFluidTarget::VolumeFluidTarget(std::string id) : id_(std::move(id)) {}
TargetDescriptor VolumeFluidTarget::describe() const {
    return {
        TargetId(id_), "volume-fluid", revisionValue(), false, {CapabilityId("eve.editor.target.volume-fluid-properties")}};
}
void* VolumeFluidTarget::queryCapability(const CapabilityId& capability) {
    return capability == CapabilityId("eve.editor.target.volume-fluid-properties")
               ? static_cast<IPropertyProvider*>(this)
               : nullptr;
}
bool VolumeFluidTarget::matches(const SelectionSnapshot& s) const {
    return s.items.size() == 1 && s.items.front().target == TargetId(id_);
}
eve::Result<eve::Revision> VolumeFluidTarget::currentRevision(const SelectionSnapshot& s) const {
    if (!matches(s)) {
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Volume fluid selection mismatch", "editor.volume-fluid.selection"));
    }
    return eve::Result<eve::Revision>::success(eve::Revision(revisionValue()));
}
PropertySchema VolumeFluidTarget::schema(const SelectionSnapshot&) const {
    const VolumeFluidAuthoringSettings d;
    PropertySchema                     result;
    result.typeId     = "fluids.volume";
    result.properties = {
        descriptor("capacity", PropertyType::Int, std::int64_t{d.capacity}, "solver", 1, 1000000, true),
        descriptor("previewParticles", PropertyType::Int, std::int64_t{d.previewParticles}, "preview", 0, 1000000,
                   true),
        descriptor("spacing", PropertyType::Float, d.spacing, "solver", 0.0001, 100.0, true),
        descriptor("gravity", PropertyType::Vec3, vec3(d.gravityX, d.gravityY, d.gravityZ), "solver"),
        descriptor("minimum", PropertyType::Vec3, vec3(d.minimumX, d.minimumY, d.minimumZ), "container"),
        descriptor("maximum", PropertyType::Vec3, vec3(d.maximumX, d.maximumY, d.maximumZ), "container"),
        descriptor("iterations", PropertyType::Int, std::int64_t{d.iterations}, "solver", 1, 20, true)};
    return result;
}
PropertyReadResult VolumeFluidTarget::read(const SelectionSnapshot& s, const PropertyPath& p) const {
    if (!matches(s) || !schema(s).find(p)) {
        return {};
    }
    return {PropertyReadState::Value, setting(settings_, p.value()), {}};
}
EditorResult<DomainOperation> VolumeFluidTarget::makeSet(const SelectionSnapshot& s, const PropertyPath& p,
                                                         const EditorValue& value, PropertySetMode mode) const {
    if (mode == PropertySetMode::Reset) {
        return makeReset(s, p);
    }
    auto property = schema(s).find(p);
    if (!matches(s) || !property || mode != PropertySetMode::Absolute) {
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.volume-fluid.set"),
                                                     "Volume fluid property requires a matching absolute edit");
    }
    auto valid = validatePropertyValue(*property, value);
    if (!valid.ok()) {
        return EditorResult<DomainOperation>::failure(valid.status());
    }
    EditorValue candidate                                = settingsValue(settings_);
    (*candidate.getIf<EditorValue::Object>())[p.value()] = value;
    auto parsed                                          = parseSettings(candidate);
    if (!parsed.ok()) {
        return EditorResult<DomainOperation>::failure(parsed.status());
    }
    DomainOperation op;
    op.type        = "volume-fluid.settings.replace.v1";
    op.inverseType = op.type;
    op.target      = TargetId(id_);
    op.payload     = settingsValue(parsed.value());
    op.inverse     = settingsValue(settings_);
    op.hasInverse  = true;
    op.affectedProperties.push_back(p.value());
    op.mergeKey = "volume-fluid:" + id_ + ":" + p.value();
    return eve::editing::applied<DomainOperation>(std::move(op));
}
EditorResult<DomainOperation> VolumeFluidTarget::makeReset(const SelectionSnapshot& s, const PropertyPath& p) const {
    auto property = schema(s).find(p);
    if (!property) {
        return eve::editing::failed<DomainOperation>(EditorStatus::Unsupported, RuleId("editor.volume-fluid.property"),
                                                     "Unknown volume fluid property");
    }
    return makeSet(s, p, property->defaultValue, PropertySetMode::Absolute);
}
EditorResult<void> VolumeFluidTarget::applyDomainOperation(const DomainOperation& op) {
    if (op.target != TargetId(id_) || op.type != "volume-fluid.settings.replace.v1") {
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.volume-fluid.operation"),
                                          "Volume fluid operation mismatch");
    }
    auto parsed = parseSettings(op.payload);
    if (!parsed.ok()) {
        return EditorResult<void>::failure(parsed.status());
    }
    settings_ = parsed.value();
    bumpRevision();
    widenDirty(0, 0);
    return eve::editing::applied<void>();
}
std::vector<EditorDiagnostic> VolumeFluidTarget::validate() const { return validateSettings(settings_); }
VolumeFluidAuthoringPreview   VolumeFluidTarget::previewBudget(std::uint64_t byteBudget,
                                                               std::uint64_t visitBudget) const {
    VolumeFluidAuthoringPreview result;
    result.documentRevision = revisionValue();
    result.estimatedBytes = static_cast<std::uint64_t>(settings_.previewParticles) * kEstimatedScratchBytesPerParticle;
    result.estimatedConstraintVisits =
        static_cast<std::uint64_t>(settings_.previewParticles) * 96ULL * settings_.iterations * 2ULL;
    result.diagnostics = validate();
    if (result.estimatedBytes > byteBudget) {
        result.diagnostics.push_back(targetDiagnostic("editor.volume-fluid.memory-budget", DiagnosticSeverity::Error,
                                                      "Volume fluid preview exceeds its memory budget"));
    }
    if (result.estimatedConstraintVisits > visitBudget) {
        result.diagnostics.push_back(targetDiagnostic("editor.volume-fluid.work-budget", DiagnosticSeverity::Error,
                                                      "Volume fluid preview exceeds its work budget"));
    }
    const bool error = std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                                   [](const auto& d) { return d.severity() == DiagnosticSeverity::Error; });
    result.status    = error ? EditorStatus::Rejected : EditorStatus::Applied;
    return result;
}
EditorValue VolumeFluidTarget::snapshotValue() const {
    return EditorValue::Object{
        {"schemaId", kSchemaId}, {"schemaVersion", kSchemaVersion}, {"settings", settingsValue(settings_)}};
}
EditorResult<void> VolumeFluidTarget::loadSnapshot(const EditorValue& snapshot) {
    const auto* object       = snapshot.getIf<EditorValue::Object>();
    const auto* sid          = field(snapshot, "schemaId");
    const auto* versionValue = field(snapshot, "schemaVersion");
    const auto* settings     = field(snapshot, "settings");
    const auto* schemaId     = sid ? sid->getIf<std::string>() : nullptr;
    const auto* version      = versionValue ? versionValue->getIf<std::int64_t>() : nullptr;
    if (!object || object->size() != 3 || !schemaId || *schemaId != kSchemaId || !version ||
        *version != kSchemaVersion || !settings)
        return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("editor.volume-fluid.snapshot"),
                                          "Unsupported or non-canonical volume fluid authoring snapshot");
    auto parsed = parseSettings(*settings);
    if (!parsed.ok()) {
        return EditorResult<void>::failure(parsed.status());
    }
    settings_ = parsed.value();
    bumpRevision();
    clearDirtyRegion();
    return eve::editing::applied<void>();
}
EditorResult<void> VolumeFluidRuntimeApplier::apply(const VolumeFluidTarget& target,
                                                    fluids::VolumeFluid*     simulation) const {
    if (!simulation) {
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.volume-fluid.runtime-required"),
                                          "Volume fluid publication requires a live simulation");
    }
    for (const auto& d : target.validate()) {
        if (d.severity() == DiagnosticSeverity::Error) {
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.volume-fluid.runtime-invalid"),
                                              d.message());
        }
    }
    auto       snapshot          = simulation->snapshot();
    const auto s                 = target.settings();
    snapshot.settings.capacity   = s.capacity;
    snapshot.settings.spacing    = static_cast<float>(s.spacing);
    snapshot.settings.gravity    = {static_cast<float>(s.gravityX), static_cast<float>(s.gravityY),
                                    static_cast<float>(s.gravityZ)};
    snapshot.settings.minimum    = {static_cast<float>(s.minimumX), static_cast<float>(s.minimumY),
                                    static_cast<float>(s.minimumZ)};
    snapshot.settings.maximum    = {static_cast<float>(s.maximumX), static_cast<float>(s.maximumY),
                                    static_cast<float>(s.maximumZ)};
    snapshot.settings.iterations = s.iterations;
    auto restored                = simulation->restore(snapshot);
    if (!restored.ok()) {
        return eve::editing::failed<void>(
            EditorStatus::Conflict, RuleId("editor.volume-fluid.runtime-rejected"),
            restored.error() ? restored.error()->message() : "Volume fluid runtime rejected authored settings");
    }
    return eve::editing::applied<void>();
}
}  // namespace eve::fluids_editing
