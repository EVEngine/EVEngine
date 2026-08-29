#include "physics_editing/PhysicsTarget.h"

#include <utility>

namespace eve::physics_editing {
namespace {

template <class T>
EditorResult<T> physicsError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

PropertyDescriptor property(const char* path, const char* label, const char* category, PropertyType type,
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

PhysicsColliderTarget::PhysicsColliderTarget(std::string id, int dimensions)
    : id_(std::move(id)), dimensions_(dimensions == 2 ? 2 : 3), values_(defaults()) {}

TargetDescriptor PhysicsColliderTarget::describe() const {
    return {TargetId(id_),
            dimensions_ == 2 ? "physics-collider-2d" : "physics-collider-3d",
            revision_,
            false,
            {CapabilityId("eve.editor.target.physics-collider")}};
}

void* PhysicsColliderTarget::queryCapability(const CapabilityId& capability) {
    return capability == CapabilityId("eve.editor.target.physics-collider") ? static_cast<IPropertyProvider*>(this)
                                                                            : nullptr;
}

EditorResult<void> PhysicsColliderTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_))
        return physicsError<void>(EditorStatus::Rejected, "editor.physics.target-mismatch",
                                  "Physics operation targets another collider");
    if (operation.type != "physics.collider.property.set.v1")
        return physicsError<void>(EditorStatus::Unsupported, "editor.physics.operation-unsupported",
                                  "Unsupported physics collider operation: " + operation.type);
    const EditorValue* pathValue = field(operation.payload, "path");
    const EditorValue* value     = field(operation.payload, "value");
    const auto*        path      = pathValue ? pathValue->getIf<std::string>() : nullptr;
    if (!path || !value)
        return physicsError<void>(EditorStatus::Rejected, "editor.physics.operation-payload",
                                  "Physics collider operation requires path and value");
    auto descriptor = colliderSchema().find(PropertyPath(*path));
    if (!descriptor)
        return physicsError<void>(EditorStatus::Unsupported, "editor.physics.property-unsupported",
                                  "Unknown physics collider property: " + *path);
    auto valid = validateAssignment(*descriptor, *value);
    if (!valid.accepted()) return valid;
    values_[*path] = *value;
    ++revision_;
    dirty_.include(0, 0);
    return EditorResult<void>::applied();
}

std::unique_ptr<IDomainOperationTarget> PhysicsColliderTarget::cloneDomainState() const {
    return std::make_unique<PhysicsColliderTarget>(*this);
}

EditorResult<void> PhysicsColliderTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<PhysicsColliderTarget*>(candidate.get());
    if (!typed || typed->id_ != id_ || typed->dimensions_ != dimensions_)
        return physicsError<void>(EditorStatus::Conflict, "editor.physics.candidate-mismatch",
                                  "Collider candidate belongs to another target");
    *this = *typed;
    return EditorResult<void>::applied();
}

eve::Result<eve::Revision> PhysicsColliderTarget::currentRevision(const SelectionSnapshot& selection) const {
    if (!selectionMatches(selection))
        return eve::Result<eve::Revision>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Selection does not belong to this collider",
                                   "editor.physics.selection", {}, "editor.PhysicsColliderTarget"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

PropertySchema PhysicsColliderTarget::schema(const SelectionSnapshot&) const { return colliderSchema(); }

PropertyReadResult PhysicsColliderTarget::read(const SelectionSnapshot& selection, const PropertyPath& path) const {
    if (!selectionMatches(selection)) return {};
    const auto found = values_.find(path.value());
    return found == values_.end() ? PropertyReadResult{}
                                  : PropertyReadResult{PropertyReadState::Value, found->second, {}};
}

EditorResult<DomainOperation> PhysicsColliderTarget::makeSet(const SelectionSnapshot& selection,
                                                             const PropertyPath& path, const EditorValue& value,
                                                             PropertySetMode mode) const {
    if (!selectionMatches(selection))
        return physicsError<DomainOperation>(EditorStatus::Rejected, "editor.physics.selection",
                                             "Selection does not belong to this collider");
    if (mode == PropertySetMode::Reset) return makeReset(selection, path);
    if (mode != PropertySetMode::Absolute)
        return physicsError<DomainOperation>(EditorStatus::Unsupported, "editor.physics.property-mode",
                                             "Collider properties require absolute assignment");
    auto descriptor = colliderSchema().find(path);
    if (!descriptor)
        return physicsError<DomainOperation>(EditorStatus::Unsupported, "editor.physics.property-unsupported",
                                             "Unknown collider property: " + path.value());
    auto valid = validateAssignment(*descriptor, value);
    if (!valid.accepted()) {
        EditorResult<DomainOperation> failed;
        failed.status      = valid.status;
        failed.diagnostics = std::move(valid.diagnostics);
        return failed;
    }
    const auto previous = values_.find(path.value());
    auto       payload  = [&](const EditorValue& assigned) {
        EditorValue::Object object;
        object["path"]  = path.value();
        object["value"] = assigned;
        return EditorValue(std::move(object));
    };
    DomainOperation operation;
    operation.type       = "physics.collider.property.set.v1";
    operation.target     = TargetId(id_);
    operation.payload    = payload(value);
    operation.inverse    = payload(previous->second);
    operation.hasInverse = true;
    operation.affectedProperties.push_back(path.value());
    operation.mergeKey = "physics.collider:" + id_ + ":" + path.value();
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorResult<DomainOperation> PhysicsColliderTarget::makeReset(const SelectionSnapshot& selection,
                                                               const PropertyPath&      path) const {
    auto descriptor = colliderSchema().find(path);
    if (!descriptor)
        return physicsError<DomainOperation>(EditorStatus::Unsupported, "editor.physics.property-unsupported",
                                             "Unknown collider property: " + path.value());
    return makeSet(selection, path, descriptor->defaultValue, PropertySetMode::Absolute);
}

PropertySchema PhysicsColliderTarget::colliderSchema() const {
    PropertySchema result;
    result.typeId  = dimensions_ == 2 ? "physics.collider2d" : "physics.collider3d";
    auto body      = property("body.type", "editor.physics.body-type", "body", PropertyType::Enum, "static");
    body.enumItems = {"static", "kinematic", "dynamic"};
    result.properties.push_back(std::move(body));
    auto shape      = property("shape.kind", "editor.physics.shape", "shape", PropertyType::Enum,
                               dimensions_ == 2 ? EditorValue("box") : EditorValue("box"));
    shape.enumItems = dimensions_ == 2 ? std::vector<std::string>{"box", "circle", "polygon", "chain"}
                                       : std::vector<std::string>{"box",         "sphere",        "capsule",
                                                                  "convex-hull", "triangle-mesh", "height-field"};
    result.properties.push_back(std::move(shape));
    auto vector = [&](const char* path, const char* label, EditorValue value) {
        result.properties.push_back(property(
            path, label, "shape", dimensions_ == 2 ? PropertyType::Vec2 : PropertyType::Vec3, std::move(value)));
    };
    vector(
        "shape.offset", "editor.physics.offset",
        dimensions_ == 2 ? EditorValue(EditorValue::Array{0.0, 0.0}) : EditorValue(EditorValue::Array{0.0, 0.0, 0.0}));
    vector(
        "shape.size", "editor.physics.size",
        dimensions_ == 2 ? EditorValue(EditorValue::Array{1.0, 1.0}) : EditorValue(EditorValue::Array{1.0, 1.0, 1.0}));
    auto numeric = [&](const char* path, const char* label, const char* category, double value, double minimum,
                       double maximum) {
        auto descriptor            = property(path, label, category, PropertyType::Float, value);
        descriptor.numeric.minimum = minimum;
        descriptor.numeric.maximum = maximum;
        descriptor.numeric.step    = 0.01;
        result.properties.push_back(std::move(descriptor));
    };
    numeric("shape.radius", "editor.physics.radius", "shape", 0.5, 0.001, 100000.0);
    numeric("shape.capsule-height", "editor.physics.capsule-height", "shape", 1.0, 0.0, 100000.0);
    numeric("material.density", "editor.physics.density", "material", 1.0, 0.0, 100000.0);
    numeric("material.friction", "editor.physics.friction", "material", 0.2, 0.0, 1.0);
    numeric("material.restitution", "editor.physics.restitution", "material", 0.0, 0.0, 1.0);
    result.properties.push_back(
        property("shape.sensor", "editor.physics.sensor", "collision", PropertyType::Bool, false));
    auto integer = [&](const char* path, const char* label, int64_t value) {
        auto descriptor            = property(path, label, "collision", PropertyType::Int, value);
        descriptor.numeric.minimum = 0.0;
        descriptor.numeric.maximum = 2147483647.0;
        result.properties.push_back(std::move(descriptor));
    };
    integer("collision.category", "editor.physics.category", 1);
    integer("collision.mask", "editor.physics.mask", 2147483647);
    auto asset             = property("shape.asset", "editor.physics.shape-asset", "shape", PropertyType::AssetRef, "");
    asset.assetTypeFilters = {"mesh", "height-field", "polygon"};
    result.properties.push_back(std::move(asset));
    return result;
}

std::map<std::string, EditorValue> PhysicsColliderTarget::defaults() const {
    std::map<std::string, EditorValue> result;
    for (const PropertyDescriptor& descriptor : colliderSchema().properties)
        result.emplace(descriptor.path.value(), descriptor.defaultValue);
    return result;
}

EditorResult<void> PhysicsColliderTarget::validateAssignment(const PropertyDescriptor& descriptor,
                                                             const EditorValue&        value) const {
    auto result = validatePropertyValue(descriptor, value);
    if (!result.accepted()) return result;
    if (descriptor.path == PropertyPath("shape.size") || descriptor.path == PropertyPath("shape.offset")) {
        const auto*       tuple    = value.getIf<EditorValue::Array>();
        const std::size_t expected = dimensions_ == 2 ? 2 : 3;
        if (!tuple || tuple->size() != expected)
            return physicsError<void>(EditorStatus::Rejected, "editor.physics.vector-size",
                                      "Physics vector has the wrong dimensionality");
        for (const EditorValue& component : *tuple) {
            const auto* number = component.getIf<double>();
            if (!number || (descriptor.path == PropertyPath("shape.size") && *number <= 0.0))
                return physicsError<void>(EditorStatus::Rejected, "editor.physics.vector-value",
                                          "Physics size components must be positive numbers");
        }
    }
    return EditorResult<void>::applied();
}

bool PhysicsColliderTarget::selectionMatches(const SelectionSnapshot& selection) const {
    return selection.items.size() == 1 && selection.items.front().target == TargetId(id_);
}

}  // namespace eve::physics_editing
