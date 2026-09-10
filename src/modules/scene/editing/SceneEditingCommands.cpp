#include "scene/editing/SceneEditingCommands.h"
#include "scene/editing/SceneTarget.h"

#include <cstdint>

namespace eve::scene_editing {
namespace {

const editing::Value* field(const editing::Value& value, const char* key) {
    const auto* object = value.getIf<editing::Value::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

bool assignVector3(const editing::Value& payload, const char* key, double& x, double& y, double& z) {
    const editing::Value* entry = field(payload, key);
    if (!entry) return true;
    const auto* values = entry->getIf<editing::Value::Array>();
    if (!values || values->size() != 3) return false;
    const auto number = [](const editing::Value& value, double& output) {
        if (const auto* real = value.getIf<double>()) {
            output = *real;
            return true;
        }
        if (const auto* integer = value.getIf<std::int64_t>()) {
            output = static_cast<double>(*integer);
            return true;
        }
        return false;
    };
    return number((*values)[0], x) && number((*values)[1], y) && number((*values)[2], z);
}

template <class T>
editing::Result<T> error(editing::Status status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, editing::RuleId(rule), std::move(message));
}

}  // namespace

editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry) {
    editing::EditingCommandDescriptor descriptor;
    descriptor.id                = editing::CommandId("scene.transform.set.v1");
    descriptor.ownerModule       = "scene_editing";
    descriptor.displayName       = "Set scene transform";
    descriptor.category          = "Scene";
    descriptor.automationAllowed = true;
    auto transformRegistered     = registry.registerPlannedCommand(
        std::move(descriptor), [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* capability = static_cast<ITransformEditTarget*>(
                target.queryCapability(ITransformEditTarget::editingCapabilityId()));
            const editing::Value* objectValue = field(request.payload, "object");
            const auto* object = objectValue ? objectValue->getIf<std::string>() : nullptr;
            if (!capability || !object)
                return error<editing::CommandPlan>(editing::Status::Rejected, "scene.editing.transform-payload",
                                                   "Scene transform requires a transform target and object id");
            auto current = capability->readTransform(editing::ObjectId(*object));
            if (!current.ok())
                return error<editing::CommandPlan>(current.code(), "scene.editing.transform-object",
                                                   "Scene object transform is unavailable");
            SceneTransformValue transform = current.value();
            if (!assignVector3(request.payload, "position", transform.x, transform.y, transform.z) ||
                !assignVector3(request.payload, "rotation", transform.rotationX, transform.rotationY,
                               transform.rotationZ) ||
                !assignVector3(request.payload, "scale", transform.scaleX, transform.scaleY, transform.scaleZ))
                return error<editing::CommandPlan>(editing::Status::Rejected, "scene.editing.transform-vector",
                                                   "Position, rotation and scale must contain three numbers");
            auto operation = capability->makeSetTransform(editing::ObjectId(*object), transform);
            if (!operation.ok())
                return error<editing::CommandPlan>(operation.code(), "scene.editing.transform-operation",
                                                   "Scene target rejected the transform");
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(operation.value()));
            plan.summary = editing::Value::Object{{"object", *object}};
            return eve::editing::applied<editing::CommandPlan>(std::move(plan));
        });
    if (!transformRegistered.ok()) return transformRegistered;
    for (const std::string action : {"create", "delete", "rename", "reparent"}) {
        editing::EditingCommandDescriptor entry;
        entry.id          = editing::CommandId("scene.object." + action + ".v1");
        entry.ownerModule = "scene_editing";
        entry.displayName = "Scene object " + action;
        entry.category    = "Scene";
        auto registered   = registry.registerPlannedCommand(
            std::move(entry), [action](editing::IEditableTarget& target, const editing::CommandRequest& request) {
                auto hierarchy = target.capability<ISceneHierarchyEditTarget>();
                if (!hierarchy)
                    return error<editing::CommandPlan>(editing::Status::Unsupported,
                                                         "scene.editing.hierarchy-unavailable",
                                                         "Target does not support scene hierarchy editing");
                const auto* idValue = field(request.payload, "object");
                const auto* id      = idValue ? idValue->getIf<std::string>() : nullptr;
                if (!id || id->empty())
                    return error<editing::CommandPlan>(editing::Status::Rejected, "scene.editing.object-required",
                                                         "A nonempty object id is required");
                const auto* nameValue   = field(request.payload, "name");
                const auto* name        = nameValue ? nameValue->getIf<std::string>() : nullptr;
                const auto* parentValue = field(request.payload, "parent");
                const auto* parent      = parentValue ? parentValue->getIf<std::string>() : nullptr;
                if ((nameValue && !name) || (parentValue && !parent) || (action == "rename" && !name) ||
                    (action == "reparent" && !parent))
                    return error<editing::CommandPlan>(editing::Status::Rejected, "scene.editing.hierarchy-payload",
                                                         "Name and parent must be strings");
                auto operation = [&]() -> editing::Result<editing::DomainOperation> {
                    if (action == "delete") return hierarchy->get().makeDelete(editing::ObjectId(*id));
                    if (action == "rename") return hierarchy->get().makeRename(editing::ObjectId(*id), *name);
                    if (action == "reparent")
                        return hierarchy->get().makeReparent(editing::ObjectId(*id), editing::ObjectId(*parent));
                    CreateSceneObjectRequest create;
                    create.id     = editing::ObjectId(*id);
                    create.name   = name ? *name : *id;
                    create.parent = parent ? editing::ObjectId(*parent) : editing::ObjectId{};
                    auto& t       = create.transform;
                    if (!assignVector3(request.payload, "position", t.x, t.y, t.z) ||
                        !assignVector3(request.payload, "rotation", t.rotationX, t.rotationY, t.rotationZ) ||
                        !assignVector3(request.payload, "scale", t.scaleX, t.scaleY, t.scaleZ))
                        return error<editing::DomainOperation>(editing::Status::Rejected, "scene.editing.create-vector",
                                                                 "TRS vectors require three numbers");
                    return hierarchy->get().makeCreate(create);
                }();
                if (!operation.ok()) return editing::Result<editing::CommandPlan>::failure(operation.status());
                editing::CommandPlan plan;
                plan.operations.push_back(std::move(operation.value()));
                return editing::applied<editing::CommandPlan>(std::move(plan));
            });
        if (!registered.ok()) return registered;
    }
    editing::EditingCommandDescriptor update;
    update.id          = editing::CommandId("scene.object.update.v1");
    update.ownerModule = "scene_editing";
    update.displayName = "Edit scene object properties";
    update.category    = "Scene";
    auto updated       = registry.registerPlannedCommand(
        std::move(update),
        [](editing::IEditableTarget&      target,
           const editing::CommandRequest& request) -> editing::Result<editing::CommandPlan> {
            auto        hierarchy  = target.capability<ISceneHierarchyEditTarget>();
            auto        transforms = target.capability<ITransformEditTarget>();
            const auto* idValue    = field(request.payload, "object");
            const auto* id         = idValue ? idValue->getIf<std::string>() : nullptr;
            if (!hierarchy || !transforms || !id)
                return error<editing::CommandPlan>(editing::Status::Rejected, "scene.editing.update-payload",
                                                         "Scene object and editing capabilities are required");
            auto current = transforms->get().readTransform(editing::ObjectId(*id));
            if (!current.ok()) return editing::Result<editing::CommandPlan>::failure(current.status());
            auto t = current.value();
            if (!assignVector3(request.payload, "position", t.x, t.y, t.z) ||
                !assignVector3(request.payload, "rotation", t.rotationX, t.rotationY, t.rotationZ) ||
                !assignVector3(request.payload, "scale", t.scaleX, t.scaleY, t.scaleZ))
                return error<editing::CommandPlan>(editing::Status::Rejected, "scene.editing.update-vector",
                                                         "Invalid TRS vector");
            auto transform = transforms->get().makeSetTransform(editing::ObjectId(*id), t);
            if (!transform.ok()) return editing::Result<editing::CommandPlan>::failure(transform.status());
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(transform.value()));
            for (const auto* key : {"name", "parent"}) {
                const auto* value = field(request.payload, key);
                if (!value) continue;
                const auto* text = value->getIf<std::string>();
                if (!text)
                    return error<editing::CommandPlan>(editing::Status::Rejected, "scene.editing.update-text",
                                                             "Name and parent must be strings");
                auto operation = std::string_view(key) == "name"
                                           ? hierarchy->get().makeRename(editing::ObjectId(*id), *text)
                                           : hierarchy->get().makeReparent(editing::ObjectId(*id), editing::ObjectId(*text));
                if (!operation.ok()) return editing::Result<editing::CommandPlan>::failure(operation.status());
                plan.operations.push_back(std::move(operation.value()));
            }
            return editing::applied<editing::CommandPlan>(std::move(plan));
        });
    if (!updated.ok()) return updated;
    editing::EditingCommandDescriptor restore;
    restore.id          = editing::CommandId("scene.snapshot.restore.v1");
    restore.ownerModule = "scene_editing";
    restore.displayName = "Restore scene hierarchy and transforms";
    restore.category    = "Scene";
    return registry.registerPlannedCommand(
        std::move(restore),
        [](editing::IEditableTarget&      value,
           const editing::CommandRequest& request) -> editing::Result<editing::CommandPlan> {
            auto* scene = dynamic_cast<SceneTargetBase*>(&value);
            if (!scene)
                return error<editing::CommandPlan>(editing::Status::Unsupported, "scene.restore.target",
                                                   "Target does not support scene snapshots");
            auto operation = scene->makeRestore(request.payload);
            if (!operation.ok()) return editing::Result<editing::CommandPlan>::failure(operation.status());
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(operation.value()));
            return editing::applied<editing::CommandPlan>(std::move(plan));
        });
}

}  // namespace eve::scene_editing
