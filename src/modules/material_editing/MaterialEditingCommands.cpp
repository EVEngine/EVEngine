#include "material_editing/MaterialEditingCommands.h"

#include "editing/EditingProperty.h"

namespace eve::material_editing {
namespace {

const editing::Value* field(const editing::Value& value, const char* key) {
    const auto* object = value.getIf<editing::Value::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

template <class T>
editing::Result<T> error(editing::Status status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, editing::RuleId(rule), std::move(message));
}

editing::SelectionSnapshot selectionFor(editing::IEditableTarget& target) {
    editing::SelectionSnapshot selection;
    selection.channel = "asset";
    editing::SelectionItem item;
    item.domain = editing::SelectionDomain::Asset;
    item.target = editing::TargetId(target.targetId());
    item.item   = editing::StableId(target.targetId().value());
    item.type   = "graphics.material";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

}  // namespace

editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry) {
    editing::EditingCommandDescriptor descriptor;
    descriptor.id                = editing::CommandId("material.property.set.v1");
    descriptor.ownerModule       = "material_editing";
    descriptor.displayName       = "Set material property";
    descriptor.category          = "Material";
    descriptor.automationAllowed = true;
    return registry.registerPlannedCommand(
        std::move(descriptor), [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* properties = static_cast<editing::IPropertyProvider*>(
                target.queryCapability(editing::IPropertyProvider::editingCapabilityId()));
            const editing::Value* pathValue = field(request.payload, "path");
            const auto* path = pathValue ? pathValue->getIf<std::string>() : nullptr;
            const editing::Value* value = field(request.payload, "value");
            if (!properties || !path || !value)
                return error<editing::CommandPlan>(editing::Status::Rejected, "material.editing.property-payload",
                                                   "Material property requires a property target, path and value");
            auto operation = properties->makeSet(selectionFor(target), editing::PropertyPath(*path), *value,
                                                 editing::PropertySetMode::Absolute);
            if (!operation.ok())
                return error<editing::CommandPlan>(operation.code(), "material.editing.property-operation",
                                                   "Material target rejected the property value");
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(operation.value()));
            plan.summary = editing::Value::Object{{"path", *path}};
            return eve::editing::applied<editing::CommandPlan>(std::move(plan));
        });
}

}  // namespace eve::material_editing
