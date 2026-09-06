#include "voxel_editing/VoxelEditingCommands.h"

#include "voxel_editing/VoxelCatalog.h"

#include <cstdint>
#include <string>

namespace eve::voxel_editing {
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

bool readInt(const editing::Value& value, const char* key, int& output, bool required) {
    const editing::Value* entry = field(value, key);
    if (!entry) return !required;
    if (const auto* integer = entry->getIf<int64_t>()) {
        output = static_cast<int>(*integer);
        return true;
    }
    if (const auto* real = entry->getIf<double>()) {
        output = static_cast<int>(*real);
        return true;
    }
    return false;
}

bool readBool(const editing::Value& value, const char* key, bool& output, bool required) {
    const editing::Value* entry = field(value, key);
    if (!entry) return !required;
    const auto* flag = entry->getIf<bool>();
    if (!flag) return false;
    output = *flag;
    return true;
}

template <class T>
editing::Result<T> error(editing::Status status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, editing::RuleId(rule), std::move(message));
}

editing::Result<void> addCommand(editing::IEditingCommandRegistry& registry, const char* id, const char* displayName,
                                 editing::EditingCommandPlanner planner) {
    editing::EditingCommandDescriptor descriptor;
    descriptor.id                = editing::CommandId(id);
    descriptor.ownerModule       = "voxel_editing";
    descriptor.displayName       = displayName;
    descriptor.category          = "Voxel";
    descriptor.automationAllowed = true;
    return registry.registerPlannedCommand(std::move(descriptor), std::move(planner));
}

VoxelCatalogTarget* asCatalog(editing::IEditableTarget& target) {
    return dynamic_cast<VoxelCatalogTarget*>(&target);
}

editing::SelectionSnapshot selectionFor(const VoxelCatalogTarget& target, const std::string& item) {
    editing::SelectionSnapshot selection;
    selection.channel = "voxel-catalog";
    editing::SelectionItem row;
    row.domain = editing::SelectionDomain::Asset;
    row.target = editing::TargetId(target.targetId());
    row.item   = editing::StableId(item);
    row.type   = "voxel.model";
    selection.items.push_back(row);
    selection.primary = row;
    return selection;
}

editing::Result<editing::CommandPlan> planFrom(editing::Result<editing::DomainOperation> operation,
                                               editing::Value summary) {
    if (!operation.ok())
        return error<editing::CommandPlan>(operation.code(), "voxel.editing.operation",
                                           "Voxel catalog rejected the planned edit");
    editing::CommandPlan plan;
    plan.operations.push_back(std::move(operation).takeValue());
    plan.summary = std::move(summary);
    return eve::editing::applied<editing::CommandPlan>(std::move(plan));
}

}  // namespace

editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry) {
    auto registered = addCommand(
        registry, "voxel.model.create.v1", "Create voxel model",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* catalog = asCatalog(target);
            const std::string* id = stringField(request.payload, "id");
            if (!catalog || !id || id->empty())
                return error<editing::CommandPlan>(editing::Status::Rejected, "voxel.editing.model-payload",
                                                   "Voxel model create requires id");
            const std::string* name = stringField(request.payload, "name");
            VoxelModelValue model;
            model.id   = editing::ObjectId(*id);
            model.name = name && !name->empty() ? *name : *id;
            if (!readInt(request.payload, "sizeX", model.sizeX, false) ||
                !readInt(request.payload, "sizeY", model.sizeY, false) ||
                !readInt(request.payload, "sizeZ", model.sizeZ, false))
                return error<editing::CommandPlan>(editing::Status::Rejected, "voxel.editing.size",
                                                   "Voxel model size must be integers");
            return planFrom(catalog->makeCreateModel(model), editing::Value::Object{{"id", *id}});
        });
    if (!registered.ok()) return registered;

    registered = addCommand(
        registry, "voxel.model.delete.v1", "Delete voxel model",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* catalog = asCatalog(target);
            const std::string* id = stringField(request.payload, "id");
            if (!catalog || !id || id->empty())
                return error<editing::CommandPlan>(editing::Status::Rejected, "voxel.editing.model-payload",
                                                   "Voxel model delete requires id");
            return planFrom(catalog->makeDeleteModel(editing::ObjectId(*id)), editing::Value::Object{{"id", *id}});
        });
    if (!registered.ok()) return registered;

    registered = addCommand(
        registry, "voxel.model.voxel.set.v1", "Set voxel occupancy",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* catalog = asCatalog(target);
            const std::string* item = stringField(request.payload, "item");
            int x = 0, y = 0, z = 0;
            bool occupied = true;
            if (!catalog || !item || item->empty() || !readInt(request.payload, "x", x, true) ||
                !readInt(request.payload, "y", y, true) || !readInt(request.payload, "z", z, true) ||
                !readBool(request.payload, "occupied", occupied, true))
                return error<editing::CommandPlan>(editing::Status::Rejected, "voxel.editing.voxel-payload",
                                                   "Voxel occupancy requires item, x, y, z and occupied");
            return planFrom(catalog->makeSetVoxel(editing::ObjectId(*item), x, y, z, occupied),
                            editing::Value::Object{{"item", *item}});
        });
    if (!registered.ok()) return registered;

    return addCommand(
        registry, "voxel.property.set.v1", "Set voxel catalog property",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* catalog = asCatalog(target);
            auto* properties = static_cast<editing::IPropertyProvider*>(
                target.queryCapability(VoxelCatalogTarget::propertyCapabilityId()));
            const std::string* item = stringField(request.payload, "item");
            const std::string* path = stringField(request.payload, "path");
            const editing::Value* value = field(request.payload, "value");
            if (!catalog || !properties || !item || item->empty() || !path || !value)
                return error<editing::CommandPlan>(editing::Status::Rejected, "voxel.editing.property-payload",
                                                   "Voxel property requires item, path and value");
            return planFrom(properties->makeSet(selectionFor(*catalog, *item), editing::PropertyPath(*path), *value,
                                                editing::PropertySetMode::Absolute),
                            editing::Value::Object{{"item", *item}, {"path", *path}});
        });
}

}  // namespace eve::voxel_editing
