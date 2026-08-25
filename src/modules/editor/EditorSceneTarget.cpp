#include "editor/EditorSceneTarget.h"

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
                               ITransformEditTarget::editorCapabilityId()};
    return descriptor;
}

void* SceneTargetBase::queryCapability(const CapabilityId& capability) {
    if (capability == ISceneHierarchyEditTarget::editorCapabilityId())
        return static_cast<ISceneHierarchyEditTarget*>(this);
    if (capability == ITransformEditTarget::editorCapabilityId()) return static_cast<ITransformEditTarget*>(this);
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
    } else {
        return sceneError<void>(EditorStatus::Unsupported, "editor.scene.operation-unsupported",
                                "Scene target does not support operation: " + operation.type);
    }
    ++revision_;
    dirty_.include(0, 0);
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
    value["x"] = transform.x;
    value["y"] = transform.y;
    value["z"] = transform.z;
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
    return EditorResult<SceneTransformValue>::applied({*x, *y, *z});
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

EditorResult<DomainOperation> ScenePlacementToolLogic::plan(IEditableTargetV2&              target,
                                                            const CreateSceneObjectRequest& request) const {
    auto* hierarchy = static_cast<ISceneHierarchyEditTarget*>(
        target.queryCapability(ISceneHierarchyEditTarget::editorCapabilityId()));
    if (!hierarchy)
        return sceneError<DomainOperation>(EditorStatus::Unsupported, "editor.scene.hierarchy-unavailable",
                                           "Target does not expose scene hierarchy editing");
    return hierarchy->makeCreate(request);
}

EditorResult<DomainOperation> SceneTransformToolLogic::plan(IEditableTargetV2& target, const ObjectId& object,
                                                            const SceneTransformValue& transform) const {
    auto* transforms =
        static_cast<ITransformEditTarget*>(target.queryCapability(ITransformEditTarget::editorCapabilityId()));
    if (!transforms)
        return sceneError<DomainOperation>(EditorStatus::Unsupported, "editor.scene.transform-unavailable",
                                           "Target does not expose transform editing");
    return transforms->makeSetTransform(object, transform);
}

}  // namespace eve::editor
