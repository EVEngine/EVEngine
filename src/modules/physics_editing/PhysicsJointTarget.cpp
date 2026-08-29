#include "physics_editing/PhysicsTarget.h"

#include <cmath>
#include <tuple>
#include <utility>

namespace eve::physics_editing {
namespace {

template <class T>
EditorResult<T> jointError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

PropertyDescriptor jointProperty(const char* path, const char* label, const char* category, PropertyType type,
                                 EditorValue value) {
    PropertyDescriptor result;
    result.path           = PropertyPath(path);
    result.displayNameKey = label;
    result.category       = category;
    result.type           = type;
    result.flags          = PropertyFlag::Runtime;
    result.defaultValue   = std::move(value);
    return result;
}

}  // namespace

PhysicsJointTarget::PhysicsJointTarget(std::string id) : id_(std::move(id)), values_(defaults()) {}

TargetDescriptor PhysicsJointTarget::describe() const {
    return {TargetId(id_), "physics-joint-3d", revision_, false, {CapabilityId("eve.editor.target.physics-joint")}};
}

void* PhysicsJointTarget::queryCapability(const CapabilityId& capability) {
    return capability == CapabilityId("eve.editor.target.physics-joint") ? static_cast<IPropertyProvider*>(this)
                                                                         : nullptr;
}

EditorResult<void> PhysicsJointTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_) || operation.type != "physics.joint.property.set.v1")
        return jointError<void>(EditorStatus::Rejected, "editor.physics.joint-operation",
                                "Operation is not valid for this physics joint");
    const EditorValue* pathValue = field(operation.payload, "path");
    const EditorValue* value     = field(operation.payload, "value");
    const auto*        path      = pathValue ? pathValue->getIf<std::string>() : nullptr;
    if (!path || !value)
        return jointError<void>(EditorStatus::Rejected, "editor.physics.joint-payload",
                                "Joint operation requires path and value");
    auto descriptor = jointSchema().find(PropertyPath(*path));
    if (!descriptor)
        return jointError<void>(EditorStatus::Unsupported, "editor.physics.joint-property",
                                "Unknown joint property: " + *path);
    auto valid = validatePropertyValue(*descriptor, *value);
    if (!valid.accepted()) return valid;
    values_[*path] = *value;
    ++revision_;
    dirty_.include(0, 0);
    return EditorResult<void>::applied();
}

std::unique_ptr<IDomainOperationTarget> PhysicsJointTarget::cloneDomainState() const {
    return std::make_unique<PhysicsJointTarget>(*this);
}

EditorResult<void> PhysicsJointTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<PhysicsJointTarget*>(candidate.get());
    if (!typed || typed->id_ != id_)
        return jointError<void>(EditorStatus::Conflict, "editor.physics.joint-candidate-mismatch",
                                "Joint candidate belongs to another target");
    *this = *typed;
    return EditorResult<void>::applied();
}

eve::Result<eve::Revision> PhysicsJointTarget::currentRevision(const SelectionSnapshot& selection) const {
    if (!selectionMatches(selection))
        return eve::Result<eve::Revision>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Selection does not belong to this joint",
                                   "editor.physics.joint-selection", {}, "editor.PhysicsJointTarget"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

PropertySchema PhysicsJointTarget::schema(const SelectionSnapshot&) const { return jointSchema(); }

PropertyReadResult PhysicsJointTarget::read(const SelectionSnapshot& selection, const PropertyPath& path) const {
    if (!selectionMatches(selection)) return {};
    const auto found = values_.find(path.value());
    return found == values_.end() ? PropertyReadResult{}
                                  : PropertyReadResult{PropertyReadState::Value, found->second, {}};
}

EditorResult<DomainOperation> PhysicsJointTarget::makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                                          const EditorValue& value, PropertySetMode mode) const {
    if (!selectionMatches(selection) || mode != PropertySetMode::Absolute)
        return jointError<DomainOperation>(EditorStatus::Rejected, "editor.physics.joint-set",
                                           "Joint property requires its own selection and absolute assignment");
    auto descriptor = jointSchema().find(path);
    if (!descriptor)
        return jointError<DomainOperation>(EditorStatus::Unsupported, "editor.physics.joint-property",
                                           "Unknown joint property: " + path.value());
    auto valid = validatePropertyValue(*descriptor, value);
    if (!valid.accepted()) {
        EditorResult<DomainOperation> failed;
        failed.status      = valid.status;
        failed.diagnostics = std::move(valid.diagnostics);
        return failed;
    }
    auto payload = [&](const EditorValue& assigned) {
        EditorValue::Object object;
        object["path"]  = path.value();
        object["value"] = assigned;
        return EditorValue(std::move(object));
    };
    DomainOperation operation;
    operation.type       = "physics.joint.property.set.v1";
    operation.target     = TargetId(id_);
    operation.payload    = payload(value);
    operation.inverse    = payload(values_.at(path.value()));
    operation.hasInverse = true;
    operation.affectedProperties.push_back(path.value());
    operation.mergeKey = "physics.joint:" + id_ + ":" + path.value();
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorResult<DomainOperation> PhysicsJointTarget::makeReset(const SelectionSnapshot& selection,
                                                            const PropertyPath&      path) const {
    auto descriptor = jointSchema().find(path);
    if (!descriptor)
        return jointError<DomainOperation>(EditorStatus::Unsupported, "editor.physics.joint-property",
                                           "Unknown joint property: " + path.value());
    return makeSet(selection, path, descriptor->defaultValue, PropertySetMode::Absolute);
}

EditorValue PhysicsJointTarget::snapshotValue() const {
    EditorValue::Object properties;
    for (const auto& [path, value] : values_) properties[path] = value;
    EditorValue::Object root;
    root["schemaVersion"] = 1;
    root["properties"]    = EditorValue(std::move(properties));
    return EditorValue(std::move(root));
}

std::vector<EditorDiagnostic> PhysicsJointTarget::validate() const {
    std::vector<EditorDiagnostic> diagnostics;
    const auto                    text   = [&](const char* path) { return *values_.at(path).getIf<std::string>(); };
    const auto                    number = [&](const char* path) { return *values_.at(path).getIf<double>(); };
    if (text("body.a").empty() || text("body.b").empty() || text("body.a") == text("body.b"))
        diagnostics.push_back({RuleId("editor.physics.joint-bodies"), DiagnosticSeverity::Error,
                               "Joint requires two distinct body references"});
    const std::string kind = text("joint.kind");
    if (kind == "prismatic" || kind == "wheel") {
        const auto& axis          = *values_.at("joint.axis").getIf<EditorValue::Array>();
        double      lengthSquared = 0.0;
        for (const EditorValue& component : axis) {
            const double value = *component.getIf<double>();
            lengthSquared += value * value;
        }
        if (lengthSquared < 1e-12)
            diagnostics.push_back({RuleId("editor.physics.joint-axis"), DiagnosticSeverity::Error,
                                   "Prismatic and wheel joints require a non-zero axis"});
    }
    if (*values_.at("limit.enabled").getIf<bool>() && number("limit.minimum") > number("limit.maximum"))
        diagnostics.push_back(
            {RuleId("editor.physics.joint-limit"), DiagnosticSeverity::Error, "Joint limit minimum exceeds maximum"});
    return diagnostics;
}

PropertySchema PhysicsJointTarget::jointSchema() {
    PropertySchema result;
    result.typeId  = "physics.joint3d";
    auto kind      = jointProperty("joint.kind", "editor.physics.joint-kind", "joint", PropertyType::Enum, "distance");
    kind.enumItems = {"distance", "revolute", "prismatic", "spherical", "wheel"};
    result.properties.push_back(std::move(kind));
    result.properties.push_back(jointProperty("body.a", "editor.physics.body-a", "joint", PropertyType::ObjectRef, ""));
    result.properties.push_back(jointProperty("body.b", "editor.physics.body-b", "joint", PropertyType::ObjectRef, ""));
    for (const auto& [path, label, value] :
         {std::tuple{"anchor.a", "editor.physics.anchor-a", EditorValue::Array{0.0, 0.0, 0.0}},
          {"anchor.b", "editor.physics.anchor-b", EditorValue::Array{0.0, 0.0, 0.0}},
          {"joint.axis", "editor.physics.axis", EditorValue::Array{1.0, 0.0, 0.0}}})
        result.properties.push_back(jointProperty(path, label, "joint", PropertyType::Vec3, value));
    result.properties.push_back(
        jointProperty("limit.enabled", "editor.physics.limit-enabled", "limit", PropertyType::Bool, false));
    result.properties.push_back(
        jointProperty("motor.enabled", "editor.physics.motor-enabled", "motor", PropertyType::Bool, false));
    result.properties.push_back(
        jointProperty("collision.connected", "editor.physics.collide-connected", "joint", PropertyType::Bool, false));
    auto numeric = [&](const char* path, const char* label, const char* category, double value, double minimum,
                       double maximum) {
        auto descriptor            = jointProperty(path, label, category, PropertyType::Float, value);
        descriptor.numeric.minimum = minimum;
        descriptor.numeric.maximum = maximum;
        descriptor.numeric.step    = 0.01;
        result.properties.push_back(std::move(descriptor));
    };
    numeric("limit.minimum", "editor.physics.limit-minimum", "limit", 0.0, -100000.0, 100000.0);
    numeric("limit.maximum", "editor.physics.limit-maximum", "limit", 0.0, -100000.0, 100000.0);
    numeric("motor.speed", "editor.physics.motor-speed", "motor", 0.0, -100000.0, 100000.0);
    numeric("motor.force", "editor.physics.motor-force", "motor", 0.0, 0.0, 1000000000.0);
    numeric("break.force", "editor.physics.break-force", "break", 0.0, 0.0, 1000000000.0);
    numeric("break.torque", "editor.physics.break-torque", "break", 0.0, 0.0, 1000000000.0);
    return result;
}

std::map<std::string, EditorValue> PhysicsJointTarget::defaults() {
    std::map<std::string, EditorValue> result;
    for (const PropertyDescriptor& descriptor : jointSchema().properties)
        result.emplace(descriptor.path.value(), descriptor.defaultValue);
    return result;
}

bool PhysicsJointTarget::selectionMatches(const SelectionSnapshot& selection) const {
    return selection.items.size() == 1 && selection.items.front().target == TargetId(id_);
}

}  // namespace eve::physics_editing
