#include "avatar_editing/AvatarEditingCommands.h"

#include "avatar_editing/AvatarTarget.h"

#include "editing/EditingProperty.h"

namespace eve::avatar_editing {
namespace {

const editing::Value* field(const editing::Value& value, const char* key) {
    const auto* object = value.getIf<editing::Value::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

const std::string* stringField(const editing::Value& value, const char* key) {
    const editing::Value* entry = field(value, key);
    return entry ? entry->getIf<std::string>() : nullptr;
}

template <class T>
editing::Result<T> error(editing::Status status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, editing::RuleId(rule), std::move(message));
}

editing::SelectionSnapshot selectionFor(editing::IEditableTarget& target, const std::string& item,
                                        const std::string& type) {
    editing::SelectionSnapshot selection;
    selection.channel = "avatar";
    editing::SelectionItem     row;
    row.domain = editing::SelectionDomain::Asset;
    row.target = editing::TargetId(target.targetId());
    row.item   = editing::StableId(item);
    row.type   = type;
    selection.items.push_back(row);
    selection.primary = row;
    return selection;
}

AvatarDocumentTarget* asAvatar(editing::IEditableTarget& target) {
    return dynamic_cast<AvatarDocumentTarget*>(&target);
}

editing::Result<editing::CommandPlan> propertySet(editing::IEditableTarget&      target,
                                                  const editing::CommandRequest& request) {
    auto* properties = static_cast<editing::IPropertyProvider*>(
        target.queryCapability(editing::CapabilityId("eve.editor.target.avatar-properties")));
    if (!properties)
        properties = static_cast<editing::IPropertyProvider*>(
            target.queryCapability(editing::IPropertyProvider::editingCapabilityId()));
    const auto* item = stringField(request.payload, "item");
    const auto* type = stringField(request.payload, "type");
    const auto* path = stringField(request.payload, "path");
    const editing::Value* value = field(request.payload, "value");
    if (!properties || !item || !type || !path || !value)
        return error<editing::CommandPlan>(editing::Status::Rejected, "avatar.editing.property-payload",
                                           "Avatar property requires a property target, item, type, path and value");
    auto operation = properties->makeSet(selectionFor(target, *item, *type), editing::PropertyPath(*path), *value,
                                         editing::PropertySetMode::Absolute);
    if (!operation.ok())
        return error<editing::CommandPlan>(operation.code(), "avatar.editing.property-operation",
                                           "Avatar target rejected the property value");
    editing::CommandPlan plan;
    plan.operations.push_back(std::move(operation.value()));
    plan.summary = editing::Value::Object{{"item", *item}, {"path", *path}};
    return eve::editing::applied<editing::CommandPlan>(std::move(plan));
}

editing::Result<editing::CommandPlan> layerCreate(editing::IEditableTarget&      target,
                                                  const editing::CommandRequest& request) {
    auto* avatar = asAvatar(target);
    const auto* id = stringField(request.payload, "id");
    const auto* name = stringField(request.payload, "name");
    const auto* texture = stringField(request.payload, "texture");
    if (!avatar || !id || !name || !texture)
        return error<editing::CommandPlan>(editing::Status::Rejected, "avatar.editing.layer-payload",
                                           "Avatar layer create requires a document target, id, name and texture");
    AvatarLayerValue layer;
    layer.id           = editing::ObjectId(*id);
    layer.name         = *name;
    layer.textureAsset = *texture;
    auto operation     = avatar->makeCreateLayer(layer);
    if (!operation.ok())
        return error<editing::CommandPlan>(operation.code(), "avatar.editing.layer-operation",
                                           "Avatar target rejected the layer");
    editing::CommandPlan plan;
    plan.operations.push_back(std::move(operation.value()));
    plan.summary = editing::Value::Object{{"id", *id}, {"name", *name}};
    return eve::editing::applied<editing::CommandPlan>(std::move(plan));
}

editing::Result<editing::CommandPlan> parameterCreate(editing::IEditableTarget&      target,
                                                      const editing::CommandRequest& request) {
    auto* avatar = asAvatar(target);
    const auto* id = stringField(request.payload, "id");
    const auto* name = stringField(request.payload, "name");
    if (!avatar || !id || !name)
        return error<editing::CommandPlan>(editing::Status::Rejected, "avatar.editing.parameter-payload",
                                           "Avatar parameter create requires a document target, id and name");
    AvatarParameterValue parameter;
    parameter.id   = editing::ObjectId(*id);
    parameter.name = *name;
    auto operation = avatar->makeCreateParameter(parameter);
    if (!operation.ok())
        return error<editing::CommandPlan>(operation.code(), "avatar.editing.parameter-operation",
                                           "Avatar target rejected the parameter");
    editing::CommandPlan plan;
    plan.operations.push_back(std::move(operation.value()));
    plan.summary = editing::Value::Object{{"id", *id}, {"name", *name}};
    return eve::editing::applied<editing::CommandPlan>(std::move(plan));
}

editing::Result<void> registerOne(editing::IEditingCommandRegistry& registry, const char* id, const char* display,
                                  editing::EditingCommandPlanner planner) {
    editing::EditingCommandDescriptor descriptor;
    descriptor.id                = editing::CommandId(id);
    descriptor.ownerModule       = "avatar_editing";
    descriptor.displayName       = display;
    descriptor.category          = "Avatar";
    descriptor.automationAllowed = true;
    return registry.registerPlannedCommand(std::move(descriptor), std::move(planner));
}

}  // namespace

editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry) {
    auto registered = registerOne(registry, "avatar.property.set.v1", "Set avatar property", propertySet);
    if (!registered.ok()) return registered;
    registered = registerOne(registry, "avatar.layer.create.v1", "Create avatar layer", layerCreate);
    if (!registered.ok()) {
        registry.unregisterOwner("avatar_editing").ignore("avatar command registration rollback");
        return registered;
    }
    registered = registerOne(registry, "avatar.parameter.create.v1", "Create avatar parameter", parameterCreate);
    if (!registered.ok())
        registry.unregisterOwner("avatar_editing").ignore("avatar command registration rollback");
    return registered;
}

}  // namespace eve::avatar_editing
