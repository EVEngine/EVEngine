#include "scene_editing/SceneEditingCommands.h"

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
    return editing::Result<T>::error(status, editing::RuleId(rule), std::move(message));
}

}  // namespace

editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry) {
    editing::EditingCommandDescriptor descriptor;
    descriptor.id                = editing::CommandId("scene.transform.set.v1");
    descriptor.ownerModule       = "scene_editing";
    descriptor.displayName       = "Set scene transform";
    descriptor.category          = "Scene";
    descriptor.automationAllowed = true;
    return registry.registerPlannedCommand(
        std::move(descriptor), [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* capability = static_cast<ITransformEditTarget*>(
                target.queryCapability(ITransformEditTarget::editingCapabilityId()));
            const editing::Value* objectValue = field(request.payload, "object");
            const auto* object = objectValue ? objectValue->getIf<std::string>() : nullptr;
            if (!capability || !object)
                return error<editing::CommandPlan>(editing::Status::Rejected, "scene.editing.transform-payload",
                                                   "Scene transform requires a transform target and object id");
            auto current = capability->readTransform(editing::ObjectId(*object));
            if (!current.accepted() || !current.value)
                return error<editing::CommandPlan>(current.status, "scene.editing.transform-object",
                                                   "Scene object transform is unavailable");
            SceneTransformValue transform = *current.value;
            if (!assignVector3(request.payload, "position", transform.x, transform.y, transform.z) ||
                !assignVector3(request.payload, "rotation", transform.rotationX, transform.rotationY,
                               transform.rotationZ) ||
                !assignVector3(request.payload, "scale", transform.scaleX, transform.scaleY, transform.scaleZ))
                return error<editing::CommandPlan>(editing::Status::Rejected, "scene.editing.transform-vector",
                                                   "Position, rotation and scale must contain three numbers");
            auto operation = capability->makeSetTransform(editing::ObjectId(*object), transform);
            if (!operation.accepted() || !operation.value)
                return error<editing::CommandPlan>(operation.status, "scene.editing.transform-operation",
                                                   "Scene target rejected the transform");
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(*operation.value));
            plan.summary = editing::Value::Object{{"object", *object}};
            return editing::Result<editing::CommandPlan>::applied(std::move(plan));
        });
}

}  // namespace eve::scene_editing
