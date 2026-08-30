#include "editor/EditorFluidTarget.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> fluidError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>(); if (!object) return nullptr;
    const auto found = object->find(key); return found == object->end() ? nullptr : &found->second;
}

EditorValue settingsValue(const FluidSimulationSettings& s) {
    return EditorValue::Object{{"maxParticles", int64_t{s.maxParticles}}, {"previewParticles", int64_t{s.previewParticles}},
        {"particleRadius", s.particleRadius}, {"supportRadius", s.supportRadius}, {"restDensity", s.restDensity},
        {"gravity", EditorValue::Array{s.gravityX, s.gravityY, s.gravityZ}}, {"viscosity", s.viscosity},
        {"yieldStress", s.yieldStress}, {"cohesion", s.cohesion}, {"adhesion", s.adhesion},
        {"damping", s.damping}, {"maximumVelocity", s.maximumVelocity}, {"iterations", int64_t{s.iterations}},
        {"pbfIterations", int64_t{s.pbfIterations}}};
}

EditorResult<FluidSimulationSettings> parseSettings(const EditorValue& value) {
    const auto integer = [&](const char* key) { const auto* entry = field(value, key); return entry ? entry->getIf<int64_t>() : nullptr; };
    const auto number = [&](const char* key) { const auto* entry = field(value, key); return entry ? entry->getIf<double>() : nullptr; };
    const auto* max = integer("maxParticles"); const auto* preview = integer("previewParticles");
    const auto* radius = number("particleRadius"); const auto* support = number("supportRadius"); const auto* density = number("restDensity");
    const auto* gravityValue = field(value, "gravity"); const auto* gravity = gravityValue ? gravityValue->getIf<EditorValue::Array>() : nullptr;
    const auto* viscosity = number("viscosity"); const auto* yield = number("yieldStress"); const auto* cohesion = number("cohesion");
    const auto* adhesion = number("adhesion"); const auto* damping = number("damping"); const auto* velocity = number("maximumVelocity");
    const auto* iterations = integer("iterations"); const auto* pbf = integer("pbfIterations");
    if (!max || !preview || !radius || !support || !density || !gravity || gravity->size() != 3 || !viscosity || !yield || !cohesion || !adhesion || !damping || !velocity || !iterations || !pbf)
        return fluidError<FluidSimulationSettings>(EditorStatus::Rejected, "editor.fluid.invalid-settings", "Fluid settings are incomplete");
    double gravityComponents[3];
    for (int i = 0; i < 3; ++i) { const auto* component = (*gravity)[i].getIf<double>(); if (!component || !std::isfinite(*component)) return fluidError<FluidSimulationSettings>(EditorStatus::Rejected, "editor.fluid.invalid-gravity", "Fluid gravity must be finite"); gravityComponents[i] = *component; }
    const double values[]{*radius, *support, *density, *viscosity, *yield, *cohesion, *adhesion, *damping, *velocity};
    if (std::any_of(std::begin(values), std::end(values), [](double component) { return !std::isfinite(component); }) ||
        *max <= 0 || *max > 10000000 || *preview < 0 || *preview > *max || *radius <= 0.0 || *support <= 0.0 ||
        *density <= 0.0 || *viscosity < 0.0 || *yield < 0.0 || *cohesion < 0.0 || *adhesion < 0.0 ||
        *damping < 0.0 || *velocity <= 0.0 || *iterations <= 0 || *iterations > 1024 || *pbf <= 0 || *pbf > 1024)
        return fluidError<FluidSimulationSettings>(EditorStatus::Rejected, "editor.fluid.settings-range", "Fluid settings are outside safe solver ranges");
    return EditorResult<FluidSimulationSettings>::applied({static_cast<int>(*max), static_cast<int>(*preview), *radius, *support, *density,
        gravityComponents[0], gravityComponents[1], gravityComponents[2], *viscosity, *yield, *cohesion, *adhesion, *damping, *velocity,
        static_cast<int>(*iterations), static_cast<int>(*pbf)});
}

PropertyDescriptor descriptor(const char* path, PropertyType type, EditorValue value, double minimum = 0.0,
                              double maximum = 0.0, bool range = false) {
    PropertyDescriptor result; result.path = PropertyPath(path); result.displayNameKey = std::string("editor.fluid.") + path;
    result.category = "fluid"; result.type = type; result.flags = PropertyFlag::Runtime; result.defaultValue = std::move(value);
    if (range) { result.numeric.minimum = minimum; result.numeric.maximum = maximum; }
    return result;
}

EditorValue setting(const FluidSimulationSettings& s, const std::string& path) {
    const EditorValue values = settingsValue(s);
    const auto* object = values.getIf<EditorValue::Object>();
    return object->at(path);
}

}  // namespace

FluidSimulationTarget::FluidSimulationTarget(std::string id) : id_(std::move(id)) {}
TargetDescriptor FluidSimulationTarget::describe() const { return {TargetId(id_), "fluid-simulation", revision_, false, {CapabilityId("eve.editor.target.fluid-properties")}}; }
void* FluidSimulationTarget::queryCapability(const CapabilityId& capability) { return capability == CapabilityId("eve.editor.target.fluid-properties") ? static_cast<IPropertyProvider*>(this) : nullptr; }

EditorResult<void> FluidSimulationTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_) || operation.type != "fluid.settings.replace.v1") return fluidError<void>(EditorStatus::Rejected, "editor.fluid.operation-mismatch", "Fluid operation targets another document or type");
    auto parsed = parseSettings(operation.payload); if (!parsed.value) return fluidError<void>(parsed.status, "editor.fluid.invalid-settings", "Fluid settings payload is invalid");
    settings_ = *parsed.value; ++revision_; dirty_.include(0, 0); return EditorResult<void>::applied();
}

bool FluidSimulationTarget::matches(const SelectionSnapshot& selection) const { return selection.items.size() == 1 && selection.items.front().target == TargetId(id_); }
eve::Result<eve::Revision> FluidSimulationTarget::currentRevision(const SelectionSnapshot& selection) const {
    if (!matches(selection)) return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Fluid selection does not match its target", "editor.fluid.selection"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

PropertySchema FluidSimulationTarget::schema(const SelectionSnapshot&) const {
    FluidSimulationSettings d; PropertySchema result; result.typeId = "fluids.simulation";
    result.properties = {
        descriptor("maxParticles", PropertyType::Int, int64_t{d.maxParticles}, 1, 10000000, true),
        descriptor("previewParticles", PropertyType::Int, int64_t{d.previewParticles}, 0, 10000000, true),
        descriptor("particleRadius", PropertyType::Float, d.particleRadius, 0.000001, 1000, true),
        descriptor("supportRadius", PropertyType::Float, d.supportRadius, 0.000001, 1000, true),
        descriptor("restDensity", PropertyType::Float, d.restDensity, 0.000001, 1000000, true),
        descriptor("gravity", PropertyType::Vec3, EditorValue::Array{d.gravityX, d.gravityY, d.gravityZ}),
        descriptor("viscosity", PropertyType::Float, d.viscosity, 0, 1000000, true),
        descriptor("yieldStress", PropertyType::Float, d.yieldStress, 0, 1000000, true),
        descriptor("cohesion", PropertyType::Float, d.cohesion, 0, 1000000, true),
        descriptor("adhesion", PropertyType::Float, d.adhesion, 0, 1000000, true),
        descriptor("damping", PropertyType::Float, d.damping, 0, 1000000, true),
        descriptor("maximumVelocity", PropertyType::Float, d.maximumVelocity, 0.000001, 1000000, true),
        descriptor("iterations", PropertyType::Int, int64_t{d.iterations}, 1, 1024, true),
        descriptor("pbfIterations", PropertyType::Int, int64_t{d.pbfIterations}, 1, 1024, true)};
    return result;
}

PropertyReadResult FluidSimulationTarget::read(const SelectionSnapshot& selection, const PropertyPath& path) const {
    if (!matches(selection) || !schema(selection).find(path)) return {};
    return {PropertyReadState::Value, setting(settings_, path.value()), {}};
}

EditorResult<DomainOperation> FluidSimulationTarget::makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                                              const EditorValue& value, PropertySetMode mode) const {
    if (mode == PropertySetMode::Reset) return makeReset(selection, path);
    auto property = schema(selection).find(path);
    if (!matches(selection) || !property || mode != PropertySetMode::Absolute) return fluidError<DomainOperation>(EditorStatus::Rejected, "editor.fluid.invalid-property-set", "Fluid property requires matching selection and absolute assignment");
    auto valid = validatePropertyValue(*property, value); if (!valid.isAccepted()) { EditorResult<DomainOperation> failed; failed.status = valid.status; failed.diagnostics = std::move(valid.diagnostics); return failed; }
    EditorValue candidateValue = settingsValue(settings_); auto* object = candidateValue.getIf<EditorValue::Object>(); (*object)[path.value()] = value;
    auto candidate = parseSettings(candidateValue); if (!candidate.value) return fluidError<DomainOperation>(candidate.status, "editor.fluid.invalid-settings", "Fluid property conflicts with other settings");
    DomainOperation operation; operation.type = "fluid.settings.replace.v1"; operation.inverseType = operation.type; operation.target = TargetId(id_);
    operation.payload = settingsValue(*candidate.value); operation.inverse = settingsValue(settings_); operation.hasInverse = true;
    operation.affectedProperties.push_back(path.value()); operation.mergeKey = "fluid:" + id_ + ":" + path.value(); return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorResult<DomainOperation> FluidSimulationTarget::makeReset(const SelectionSnapshot& selection, const PropertyPath& path) const {
    auto property = schema(selection).find(path); if (!property) return fluidError<DomainOperation>(EditorStatus::Unsupported, "editor.fluid.property-not-found", "Fluid property is unknown");
    return makeSet(selection, path, property->defaultValue, PropertySetMode::Absolute);
}

std::vector<EditorDiagnostic> FluidSimulationTarget::validate() const {
    std::vector<EditorDiagnostic> result;
    if (settings_.supportRadius < settings_.particleRadius * 2.0) result.push_back({RuleId("editor.fluid.support-radius-small"), DiagnosticSeverity::Error, "Fluid support radius should be at least twice particle radius"});
    if (settings_.supportRadius > settings_.particleRadius * 8.0) result.push_back({RuleId("editor.fluid.support-radius-large"), DiagnosticSeverity::Warning, "Large support radius can make neighbor search prohibitively expensive"});
    return result;
}

FluidSimulationPreview FluidSimulationTarget::previewBudget(std::uint64_t byteBudget, std::uint64_t neighborBudget) const {
    FluidSimulationPreview result; result.documentRevision = revision_;
    const std::uint64_t particles = static_cast<std::uint64_t>(settings_.previewParticles);
    result.estimatedBytes = particles * 80ULL;
    const double ratio = settings_.supportRadius / settings_.particleRadius;
    result.estimatedNeighborChecks = static_cast<std::uint64_t>(particles * std::min(512.0, ratio * ratio * ratio * 2.0) * settings_.iterations * settings_.pbfIterations);
    result.diagnostics = validate();
    if (result.estimatedBytes > byteBudget) result.diagnostics.push_back({RuleId("editor.fluid.memory-budget"), DiagnosticSeverity::Error, "Fluid preview exceeds its memory budget"});
    if (result.estimatedNeighborChecks > neighborBudget) result.diagnostics.push_back({RuleId("editor.fluid.compute-budget"), DiagnosticSeverity::Error, "Fluid preview exceeds its estimated neighbor-work budget"});
    const bool error = std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& d) { return d.severity == DiagnosticSeverity::Error; }); result.status = error ? EditorStatus::Rejected : EditorStatus::Applied; return result;
}

EditorValue FluidSimulationTarget::snapshotValue() const { return EditorValue::Object{{"schemaVersion", int64_t{1}}, {"settings", settingsValue(settings_)}}; }
EditorResult<void> FluidSimulationTarget::loadSnapshot(const EditorValue& snapshot) {
    const auto* versionValue = field(snapshot, "schemaVersion"); const auto* settings = field(snapshot, "settings"); const auto* version = versionValue ? versionValue->getIf<int64_t>() : nullptr;
    if (!version || *version != 1 || !settings) return fluidError<void>(EditorStatus::Unsupported, "editor.fluid.invalid-snapshot", "Fluid snapshot schema is unsupported");
    auto parsed = parseSettings(*settings); if (!parsed.value) return fluidError<void>(parsed.status, "editor.fluid.invalid-settings", "Fluid snapshot settings are invalid");
    settings_ = *parsed.value; ++revision_; dirty_.clear(); return EditorResult<void>::applied();
}

}  // namespace eve::editor
