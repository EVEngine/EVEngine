#include "editor/EditorSceneTarget.h"

#include "editor/EditorSceneComponentPayload.h"

#include <algorithm>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> sceneError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* name) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    auto found = object->find(name);
    return found == object->end() ? nullptr : &found->second;
}

}  // namespace

SceneTargetBase::SceneTargetBase(std::string id, std::string type) : id_(std::move(id)), type_(std::move(type)) {}

TargetDescriptor SceneTargetBase::describe() const {
    TargetDescriptor descriptor;
    descriptor.id           = TargetId(id_);
    descriptor.type         = type_;
    descriptor.revision     = revision_;
    descriptor.capabilities = {ISceneHierarchyEditTarget::editorCapabilityId(),
                               ITransformEditTarget::editingCapabilityId(),
                               eve::editing::IEditingSnapshotProvider::editingCapabilityId()};
    if (componentPayloads_)
        descriptor.capabilities.push_back(ISceneComponentPayloadTarget::editorCapabilityId());
    return descriptor;
}

void* SceneTargetBase::queryCapability(const CapabilityId& capability) {
    if (capability == ISceneHierarchyEditTarget::editorCapabilityId())
        return static_cast<ISceneHierarchyEditTarget*>(this);
    if (capability == ITransformEditTarget::editingCapabilityId()) return static_cast<ITransformEditTarget*>(this);
    if (capability == eve::editing::IEditingSnapshotProvider::editingCapabilityId())
        return static_cast<eve::editing::IEditingSnapshotProvider*>(this);
    if (componentPayloads_ && capability == ISceneComponentPayloadTarget::editorCapabilityId())
        return static_cast<ISceneComponentPayloadTarget*>(componentPayloads_);
    return nullptr;
}

EditorResult<void> SceneTargetBase::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_))
        return sceneError<void>(EditorStatus::Rejected, "editor.scene.target-mismatch",
                                "Scene operation targets another backend");
    if (operation.type == "scene.object.create.v1") {
        EditorResult<SceneObjectSnapshot> parsed = parseObject(operation.payload);
        if (!parsed.accepted() || !parsed.value) {
            EditorResult<void> result;
            result.status      = parsed.status;
            result.diagnostics = std::move(parsed.diagnostics);
            return result;
        }
        const SceneObjectSnapshot& object = *parsed.value;
        if (object.id.empty() || objects_.contains(object.id))
            return sceneError<void>(EditorStatus::Conflict, "editor.scene.object-exists",
                                    "Scene object id is empty or already exists");
        if (!object.parent.empty() && !objects_.contains(object.parent))
            return sceneError<void>(EditorStatus::NotFound, "editor.scene.parent-not-found",
                                    "Scene object parent does not exist");
        objects_.emplace(object.id, object);
    } else if (operation.type == "scene.object.delete.v1") {
        EditorResult<SceneObjectSnapshot> parsed = parseObject(operation.payload);
        if (!parsed.accepted() || !parsed.value) {
            EditorResult<void> result;
            result.status      = parsed.status;
            result.diagnostics = std::move(parsed.diagnostics);
            return result;
        }
        const ObjectId& id = parsed.value->id;
        if (!objects_.contains(id))
            return sceneError<void>(EditorStatus::NotFound, "editor.scene.object-not-found",
                                    "Scene object does not exist");
        for (const auto& [childId, child] : objects_) {
            (void)childId;
            if (child.parent == id)
                return sceneError<void>(EditorStatus::Rejected, "editor.scene.object-has-children",
                                        "Scene object with children cannot be deleted by this operation");
        }
        objects_.erase(id);
    } else if (operation.type == "scene.transform.set.v1") {
        const EditorValue* idValue   = field(operation.payload, "id");
        const EditorValue* transform = field(operation.payload, "transform");
        const auto*        id        = idValue ? idValue->getIf<std::string>() : nullptr;
        if (!id || !transform)
            return sceneError<void>(EditorStatus::Rejected, "editor.scene.transform-payload",
                                    "Transform operation requires id and transform");
        auto object = objects_.find(ObjectId(*id));
        if (object == objects_.end())
            return sceneError<void>(EditorStatus::NotFound, "editor.scene.object-not-found",
                                    "Scene object does not exist");
        EditorResult<SceneTransformValue> parsed = parseTransform(*transform);
        if (!parsed.accepted() || !parsed.value) {
            EditorResult<void> result;
            result.status      = parsed.status;
            result.diagnostics = std::move(parsed.diagnostics);
            return result;
        }
        object->second.transform = *parsed.value;
    } else if (operation.type == "scene.transform.multi.set.v1") {
        const auto* entries = operation.payload.getIf<EditorValue::Array>();
        if (!entries)
            return sceneError<void>(EditorStatus::Rejected, "editor.scene.multi-transform-payload",
                                    "Multi-transform operation requires an array payload");
        std::vector<std::pair<ObjectId, SceneTransformValue>> updates;
        updates.reserve(entries->size());
        for (const EditorValue& entry : *entries) {
            const EditorValue* idValue   = field(entry, "id");
            const EditorValue* transform = field(entry, "transform");
            const auto* id = idValue ? idValue->getIf<std::string>() : nullptr;
            if (!id || !transform || !objects_.contains(ObjectId(*id)))
                return sceneError<void>(EditorStatus::NotFound, "editor.scene.multi-transform-object",
                                        "Multi-transform entry references an invalid scene object");
            auto parsed = parseTransform(*transform);
            if (!parsed.accepted() || !parsed.value)
                return sceneError<void>(EditorStatus::Rejected, "editor.scene.multi-transform-value",
                                        "Multi-transform entry contains an invalid transform");
            updates.emplace_back(ObjectId(*id), *parsed.value);
        }
        for (const auto& [id, transform] : updates) objects_.at(id).transform = transform;
    } else if (operation.type == "scene.object.rename.v1") {
        const EditorValue* idValue   = field(operation.payload, "id");
        const EditorValue* nameValue = field(operation.payload, "name");
        const auto*        id        = idValue ? idValue->getIf<std::string>() : nullptr;
        const auto*        name      = nameValue ? nameValue->getIf<std::string>() : nullptr;
        if (!id || !name || name->empty())
            return sceneError<void>(EditorStatus::Rejected, "editor.scene.rename-payload",
                                    "Rename operation requires id and a non-empty name");
        auto object = objects_.find(ObjectId(*id));
        if (object == objects_.end())
            return sceneError<void>(EditorStatus::NotFound, "editor.scene.object-not-found",
                                    "Scene object does not exist");
        object->second.name = *name;
    } else if (operation.type == "scene.object.reparent.v1") {
        const EditorValue* idValue     = field(operation.payload, "id");
        const EditorValue* parentValue = field(operation.payload, "parent");
        const auto*        id          = idValue ? idValue->getIf<std::string>() : nullptr;
        const auto*        parent      = parentValue ? parentValue->getIf<std::string>() : nullptr;
        if (!id || !parent)
            return sceneError<void>(EditorStatus::Rejected, "editor.scene.reparent-payload",
                                    "Reparent operation requires id and parent");
        auto object = objects_.find(ObjectId(*id));
        if (object == objects_.end())
            return sceneError<void>(EditorStatus::NotFound, "editor.scene.object-not-found",
                                    "Scene object does not exist");
        if (!parent->empty() && !objects_.contains(ObjectId(*parent)))
            return sceneError<void>(EditorStatus::NotFound, "editor.scene.parent-not-found",
                                    "Scene object parent does not exist");
        ObjectId ancestor(*parent);
        while (!ancestor.empty()) {
            if (ancestor == object->first)
                return sceneError<void>(EditorStatus::Rejected, "editor.scene.hierarchy-cycle",
                                        "Reparenting would create a scene hierarchy cycle");
            auto node = objects_.find(ancestor);
            if (node == objects_.end()) break;
            ancestor = node->second.parent;
        }
        object->second.parent = ObjectId(*parent);
    } else {
        return sceneError<void>(EditorStatus::Unsupported, "editor.scene.operation-unsupported",
                                "Scene target does not support operation: " + operation.type);
    }
    ++revision_;
    dirty_.include(0, 0);
    return EditorResult<void>::applied();
}

std::unique_ptr<IDomainOperationTarget> SceneTargetBase::cloneDomainState() const {
    return std::make_unique<SceneTargetBase>(*this);
}

EditorResult<void> SceneTargetBase::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* staged = dynamic_cast<SceneTargetBase*>(candidate.get());
    if (!staged || staged->id_ != id_ || staged->type_ != type_)
        return sceneError<void>(EditorStatus::Conflict, "editor.scene.candidate-mismatch",
                                "Scene candidate does not belong to this target");
    objects_.swap(staged->objects_);
    revision_ = staged->revision_;
    dirty_    = staged->dirty_;
    return EditorResult<void>::applied();
}

EditorResult<SceneObjectSnapshot> SceneTargetBase::sceneObject(const ObjectId& id) const {
    auto object = objects_.find(id);
    if (object == objects_.end())
        return sceneError<SceneObjectSnapshot>(EditorStatus::NotFound, "editor.scene.object-not-found",
                                               "Scene object does not exist: " + id.value());
    return EditorResult<SceneObjectSnapshot>::applied(object->second);
}

std::vector<ObjectId> SceneTargetBase::sceneChildren(const ObjectId& parent) const {
    std::vector<ObjectId> children;
    for (const auto& [id, object] : objects_)
        if (object.parent == parent) children.push_back(id);
    return children;
}

EditorResult<DomainOperation> SceneTargetBase::makeCreate(const CreateSceneObjectRequest& request) const {
    if (request.id.empty())
        return sceneError<DomainOperation>(EditorStatus::Rejected, "editor.scene.missing-object-id",
                                           "Scene object id is required");
    if (objects_.contains(request.id))
        return sceneError<DomainOperation>(EditorStatus::Conflict, "editor.scene.object-exists",
                                           "Scene object already exists: " + request.id.value());
    if (!request.parent.empty() && !objects_.contains(request.parent))
        return sceneError<DomainOperation>(EditorStatus::NotFound, "editor.scene.parent-not-found",
                                           "Scene object parent does not exist");
    SceneObjectSnapshot object{request.id, request.parent, request.name, request.transform};
    DomainOperation     operation;
    operation.type        = "scene.object.create.v1";
    operation.inverseType = "scene.object.delete.v1";
    operation.target      = TargetId(id_);
    operation.payload     = objectValue(object);
    operation.inverse     = objectValue(object);
    operation.hasInverse  = true;
    operation.affectedObjects.push_back({TargetId(id_), request.id.value(), 0});
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorResult<DomainOperation> SceneTargetBase::makeDelete(const ObjectId& id) const {
    auto object = objects_.find(id);
    if (object == objects_.end())
        return sceneError<DomainOperation>(EditorStatus::NotFound, "editor.scene.object-not-found",
                                           "Scene object does not exist: " + id.value());
    if (!sceneChildren(id).empty())
        return sceneError<DomainOperation>(EditorStatus::Rejected, "editor.scene.object-has-children",
                                           "Delete or reparent child objects before deleting their parent");
    DomainOperation operation;
    operation.type        = "scene.object.delete.v1";
    operation.inverseType = "scene.object.create.v1";
    operation.target      = TargetId(id_);
    operation.payload     = objectValue(object->second);
    operation.inverse     = operation.payload;
    operation.hasInverse  = true;
    operation.affectedObjects.push_back({TargetId(id_), id.value(), 0});
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorResult<DomainOperation> SceneTargetBase::makeRename(const ObjectId& id, const std::string& name) const {
    auto object = objects_.find(id);
    if (object == objects_.end())
        return sceneError<DomainOperation>(EditorStatus::NotFound, "editor.scene.object-not-found",
                                           "Scene object does not exist: " + id.value());
    if (name.empty())
        return sceneError<DomainOperation>(EditorStatus::Rejected, "editor.scene.empty-name",
                                           "Scene object name must not be empty");
    auto value = [&](const std::string& valueName) {
        EditorValue::Object payload;
        payload["id"]   = id.value();
        payload["name"] = valueName;
        return EditorValue(std::move(payload));
    };
    DomainOperation operation;
    operation.type       = "scene.object.rename.v1";
    operation.target     = TargetId(id_);
    operation.payload    = value(name);
    operation.inverse    = value(object->second.name);
    operation.hasInverse = true;
    operation.affectedObjects.push_back({TargetId(id_), id.value(), 0});
    operation.affectedProperties.push_back("name");
    operation.mergeKey = "scene.name:" + id.value();
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorResult<DomainOperation> SceneTargetBase::makeReparent(const ObjectId& id, const ObjectId& parent) const {
    auto object = objects_.find(id);
    if (object == objects_.end())
        return sceneError<DomainOperation>(EditorStatus::NotFound, "editor.scene.object-not-found",
                                           "Scene object does not exist: " + id.value());
    if (!parent.empty() && !objects_.contains(parent))
        return sceneError<DomainOperation>(EditorStatus::NotFound, "editor.scene.parent-not-found",
                                           "Scene object parent does not exist: " + parent.value());
    ObjectId ancestor = parent;
    while (!ancestor.empty()) {
        if (ancestor == id)
            return sceneError<DomainOperation>(EditorStatus::Rejected, "editor.scene.hierarchy-cycle",
                                               "Reparenting would create a scene hierarchy cycle");
        auto node = objects_.find(ancestor);
        if (node == objects_.end()) break;
        ancestor = node->second.parent;
    }
    auto value = [&](const ObjectId& valueParent) {
        EditorValue::Object payload;
        payload["id"]     = id.value();
        payload["parent"] = valueParent.value();
        return EditorValue(std::move(payload));
    };
    DomainOperation operation;
    operation.type       = "scene.object.reparent.v1";
    operation.target     = TargetId(id_);
    operation.payload    = value(parent);
    operation.inverse    = value(object->second.parent);
    operation.hasInverse = true;
    operation.affectedObjects.push_back({TargetId(id_), id.value(), 0});
    operation.affectedProperties.push_back("parent");
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorResult<SceneTransformValue> SceneTargetBase::readTransform(const ObjectId& id) const {
    auto object = sceneObject(id);
    if (!object.accepted() || !object.value) {
        EditorResult<SceneTransformValue> result;
        result.status      = object.status;
        result.diagnostics = std::move(object.diagnostics);
        return result;
    }
    return EditorResult<SceneTransformValue>::applied(object.value->transform);
}

EditorResult<DomainOperation> SceneTargetBase::makeSetTransform(const ObjectId&            id,
                                                                const SceneTransformValue& transform) const {
    auto object = objects_.find(id);
    if (object == objects_.end())
        return sceneError<DomainOperation>(EditorStatus::NotFound, "editor.scene.object-not-found",
                                           "Scene object does not exist: " + id.value());
    EditorValue::Object payload;
    payload["id"]        = id.value();
    payload["transform"] = transformValue(transform);
    EditorValue::Object inverse;
    inverse["id"]        = id.value();
    inverse["transform"] = transformValue(object->second.transform);
    DomainOperation operation;
    operation.type       = "scene.transform.set.v1";
    operation.target     = TargetId(id_);
    operation.payload    = EditorValue(std::move(payload));
    operation.inverse    = EditorValue(std::move(inverse));
    operation.hasInverse = true;
    operation.affectedObjects.push_back({TargetId(id_), id.value(), 0});
    operation.affectedProperties.push_back("transform");
    operation.mergeKey = "scene.transform:" + id.value();
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorValue SceneTargetBase::snapshotValue() const {
    EditorValue::Array objects;
    objects.reserve(objects_.size());
    for (const auto& [id, object] : objects_) {
        (void)id;
        objects.push_back(objectValue(object));
    }
    EditorValue::Object scene;
    scene["schemaVersion"] = 1;
    scene["objects"]       = EditorValue(std::move(objects));
    return EditorValue(std::move(scene));
}

EditorValue SceneTargetBase::transformValue(const SceneTransformValue& transform) {
    EditorValue::Object value;
    value["x"]         = transform.x;
    value["y"]         = transform.y;
    value["z"]         = transform.z;
    value["rotationX"] = transform.rotationX;
    value["rotationY"] = transform.rotationY;
    value["rotationZ"] = transform.rotationZ;
    value["scaleX"]    = transform.scaleX;
    value["scaleY"]    = transform.scaleY;
    value["scaleZ"]    = transform.scaleZ;
    return EditorValue(std::move(value));
}

EditorResult<SceneTransformValue> SceneTargetBase::parseTransform(const EditorValue& value) {
    const EditorValue* xValue = field(value, "x");
    const EditorValue* yValue = field(value, "y");
    const EditorValue* zValue = field(value, "z");
    const auto*        x      = xValue ? xValue->getIf<double>() : nullptr;
    const auto*        y      = yValue ? yValue->getIf<double>() : nullptr;
    const auto*        z      = zValue ? zValue->getIf<double>() : nullptr;
    if (!x || !y || !z)
        return sceneError<SceneTransformValue>(EditorStatus::Rejected, "editor.scene.transform-value",
                                               "Transform requires numeric x, y and z fields");
    SceneTransformValue result;
    result.x = *x;
    result.y = *y;
    result.z = *z;
    auto optionalNumber = [&](const char* name, double fallback) {
        const EditorValue* entry = field(value, name);
        if (!entry) return fallback;
        const auto* number = entry->getIf<double>();
        return number ? *number : fallback;
    };
    result.rotationX = optionalNumber("rotationX", 0.0);
    result.rotationY = optionalNumber("rotationY", 0.0);
    result.rotationZ = optionalNumber("rotationZ", 0.0);
    result.scaleX    = optionalNumber("scaleX", 1.0);
    result.scaleY    = optionalNumber("scaleY", 1.0);
    result.scaleZ    = optionalNumber("scaleZ", 1.0);
    return EditorResult<SceneTransformValue>::applied(result);
}

EditorResult<SceneObjectSnapshot> SceneTargetBase::parseObject(const EditorValue& value) {
    const EditorValue* idValue     = field(value, "id");
    const EditorValue* parentValue = field(value, "parent");
    const EditorValue* nameValue   = field(value, "name");
    const EditorValue* transform   = field(value, "transform");
    const auto*        id          = idValue ? idValue->getIf<std::string>() : nullptr;
    const auto*        parent      = parentValue ? parentValue->getIf<std::string>() : nullptr;
    const auto*        name        = nameValue ? nameValue->getIf<std::string>() : nullptr;
    if (!id || !parent || !name || !transform)
        return sceneError<SceneObjectSnapshot>(EditorStatus::Rejected, "editor.scene.object-value",
                                               "Scene object payload is incomplete");
    EditorResult<SceneTransformValue> parsedTransform = parseTransform(*transform);
    if (!parsedTransform.accepted() || !parsedTransform.value) {
        EditorResult<SceneObjectSnapshot> result;
        result.status      = parsedTransform.status;
        result.diagnostics = std::move(parsedTransform.diagnostics);
        return result;
    }
    return EditorResult<SceneObjectSnapshot>::applied(
        {ObjectId(*id), ObjectId(*parent), *name, *parsedTransform.value});
}

EditorValue SceneTargetBase::objectValue(const SceneObjectSnapshot& object) {
    EditorValue::Object value;
    value["id"]        = object.id.value();
    value["parent"]    = object.parent.value();
    value["name"]      = object.name;
    value["transform"] = transformValue(object.transform);
    return EditorValue(std::move(value));
}

EditorResult<DomainOperation> ScenePlacementToolLogic::plan(IEditableTarget&              target,
                                                            const CreateSceneObjectRequest& request) const {
    auto* hierarchy = static_cast<ISceneHierarchyEditTarget*>(
        target.queryCapability(ISceneHierarchyEditTarget::editorCapabilityId()));
    if (!hierarchy)
        return sceneError<DomainOperation>(EditorStatus::Unsupported, "editor.scene.hierarchy-unavailable",
                                           "Target does not expose scene hierarchy editing");
    return hierarchy->makeCreate(request);
}

EditorResult<DomainOperation> SceneTransformToolLogic::plan(IEditableTarget& target, const ObjectId& object,
                                                            const SceneTransformValue& transform) const {
    auto* transforms =
        static_cast<ITransformEditTarget*>(target.queryCapability(ITransformEditTarget::editingCapabilityId()));
    if (!transforms)
        return sceneError<DomainOperation>(EditorStatus::Unsupported, "editor.scene.transform-unavailable",
                                           "Target does not expose transform editing");
    return transforms->makeSetTransform(object, transform);
}

EditorResult<DomainOperation> SceneHierarchyToolLogic::planDelete(IEditableTarget& target,
                                                                  const ObjectId& object) const {
    auto* hierarchy = static_cast<ISceneHierarchyEditTarget*>(
        target.queryCapability(ISceneHierarchyEditTarget::editorCapabilityId()));
    if (!hierarchy)
        return sceneError<DomainOperation>(EditorStatus::Unsupported, "editor.scene.hierarchy-unavailable",
                                           "Target does not expose scene hierarchy editing");
    return hierarchy->makeDelete(object);
}

EditorResult<DomainOperation> SceneHierarchyToolLogic::planRename(IEditableTarget& target,
                                                                  const ObjectId& object,
                                                                  const std::string& name) const {
    auto* hierarchy = static_cast<ISceneHierarchyEditTarget*>(
        target.queryCapability(ISceneHierarchyEditTarget::editorCapabilityId()));
    if (!hierarchy)
        return sceneError<DomainOperation>(EditorStatus::Unsupported, "editor.scene.hierarchy-unavailable",
                                           "Target does not expose scene hierarchy editing");
    return hierarchy->makeRename(object, name);
}

EditorResult<DomainOperation> SceneHierarchyToolLogic::planReparent(IEditableTarget& target,
                                                                    const ObjectId& object,
                                                                    const ObjectId& parent) const {
    auto* hierarchy = static_cast<ISceneHierarchyEditTarget*>(
        target.queryCapability(ISceneHierarchyEditTarget::editorCapabilityId()));
    if (!hierarchy)
        return sceneError<DomainOperation>(EditorStatus::Unsupported, "editor.scene.hierarchy-unavailable",
                                           "Target does not expose scene hierarchy editing");
    return hierarchy->makeReparent(object, parent);
}

eve::Result<eve::Revision> ScenePropertyProvider::currentRevision(const SelectionSnapshot&) const {
    if (!target_)
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Scene property target is unavailable",
            "editor.scene.property.target", {}, "editor.ScenePropertyProvider"));
    return eve::Result<eve::Revision>::success(eve::Revision(target_->revision()));
}

PropertySchema ScenePropertyProvider::schema(const SelectionSnapshot&) const {
    PropertySchema result;
    result.typeId = "scene.object";
    auto addTrs = [&](const char* path, const char* name, const EditorValue& defaultValue) {
        PropertyDescriptor descriptor;
        descriptor.path           = PropertyPath(path);
        descriptor.displayNameKey = name;
        descriptor.category       = "transform";
        descriptor.type           = PropertyType::Vec3;
        descriptor.flags          = PropertyFlag::Runtime | PropertyFlag::MultiEdit;
        descriptor.defaultValue   = defaultValue;
        descriptor.numeric.step   = 0.01;
        result.properties.push_back(std::move(descriptor));
    };
    addTrs("transform.position", "editor.transform.position", EditorValue::Array{0.0, 0.0, 0.0});
    addTrs("transform.rotation", "editor.transform.rotation", EditorValue::Array{0.0, 0.0, 0.0});
    addTrs("transform.scale", "editor.transform.scale", EditorValue::Array{1.0, 1.0, 1.0});
    return result;
}

PropertyReadResult ScenePropertyProvider::read(const SelectionSnapshot& selection,
                                                const PropertyPath& path) const {
    if (!target_ || selection.items.empty()) return {};
    auto component = [&](const SceneTransformValue& value) -> EditorValue {
        if (path == PropertyPath("transform.position")) return EditorValue::Array{value.x, value.y, value.z};
        if (path == PropertyPath("transform.rotation"))
            return EditorValue::Array{value.rotationX, value.rotationY, value.rotationZ};
        if (path == PropertyPath("transform.scale"))
            return EditorValue::Array{value.scaleX, value.scaleY, value.scaleZ};
        return {};
    };
    std::optional<EditorValue> common;
    for (const SelectionItem& item : selection.items) {
        if (item.target != TargetId(target_->targetId())) return {};
        auto transform = target_->readTransform(ObjectId(item.item.value()));
        if (!transform.accepted() || !transform.value) return {PropertyReadState::Error, {}, transform.diagnostics};
        EditorValue value = component(*transform.value);
        if (value.type() == EditorValue::Type::Null) return {};
        if (!common) common = value;
        else if (*common != value) return {PropertyReadState::Mixed, {}, {}};
    }
    return common ? PropertyReadResult{PropertyReadState::Value, *common, {}} : PropertyReadResult{};
}

EditorResult<DomainOperation> ScenePropertyProvider::makeSet(const SelectionSnapshot& selection,
                                                              const PropertyPath& path,
                                                              const EditorValue& value,
                                                              PropertySetMode mode) const {
    if (!target_ || mode != PropertySetMode::Absolute)
        return sceneError<DomainOperation>(EditorStatus::Unsupported, "editor.scene.property-mode",
                                           "Scene properties currently require absolute assignment");
    const auto* tuple = value.getIf<EditorValue::Array>();
    if (!tuple || tuple->size() != 3)
        return sceneError<DomainOperation>(EditorStatus::Rejected, "editor.scene.property-value",
                                           "Scene TRS property requires three numeric values");
    const auto* a = (*tuple)[0].getIf<double>();
    const auto* b = (*tuple)[1].getIf<double>();
    const auto* c = (*tuple)[2].getIf<double>();
    if (!a || !b || !c)
        return sceneError<DomainOperation>(EditorStatus::Rejected, "editor.scene.property-value",
                                           "Scene TRS property requires three numeric values");
    EditorValue::Array payload;
    EditorValue::Array inverse;
    DomainOperation operation;
    for (const SelectionItem& item : selection.items) {
        if (item.target != TargetId(target_->targetId()))
            return sceneError<DomainOperation>(EditorStatus::Rejected, "editor.scene.property-target",
                                               "Selection contains objects from another target");
        auto current = target_->readTransform(ObjectId(item.item.value()));
        if (!current.accepted() || !current.value)
            return sceneError<DomainOperation>(EditorStatus::NotFound, "editor.scene.property-object",
                                               "Selected scene object does not exist");
        SceneTransformValue changed = *current.value;
        if (path == PropertyPath("transform.position")) {
            changed.x = *a; changed.y = *b; changed.z = *c;
        } else if (path == PropertyPath("transform.rotation")) {
            changed.rotationX = *a; changed.rotationY = *b; changed.rotationZ = *c;
        } else if (path == PropertyPath("transform.scale")) {
            changed.scaleX = *a; changed.scaleY = *b; changed.scaleZ = *c;
        } else {
            return sceneError<DomainOperation>(EditorStatus::Unsupported, "editor.scene.property-path",
                                               "Scene property path is unsupported: " + path.value());
        }
        EditorValue::Object nextEntry;
        nextEntry["id"] = item.item.value();
        nextEntry["transform"] = SceneTargetBase::transformValue(changed);
        payload.emplace_back(std::move(nextEntry));
        EditorValue::Object previousEntry;
        previousEntry["id"] = item.item.value();
        previousEntry["transform"] = SceneTargetBase::transformValue(*current.value);
        inverse.emplace_back(std::move(previousEntry));
        operation.affectedObjects.push_back({TargetId(target_->targetId()), item.item.value(), 0});
    }
    operation.type       = "scene.transform.multi.set.v1";
    operation.target     = TargetId(target_->targetId());
    operation.payload    = EditorValue(std::move(payload));
    operation.inverse    = EditorValue(std::move(inverse));
    operation.hasInverse = true;
    operation.affectedProperties.push_back(path.value());
    operation.mergeKey = "scene.selection:" + path.value();
    return EditorResult<DomainOperation>::applied(std::move(operation));
}

EditorResult<DomainOperation> ScenePropertyProvider::makeReset(const SelectionSnapshot& selection,
                                                                const PropertyPath& path) const {
    if (path == PropertyPath("transform.scale"))
        return makeSet(selection, path, EditorValue::Array{1.0, 1.0, 1.0}, PropertySetMode::Absolute);
    return makeSet(selection, path, EditorValue::Array{0.0, 0.0, 0.0}, PropertySetMode::Absolute);
}

}  // namespace eve::editor
