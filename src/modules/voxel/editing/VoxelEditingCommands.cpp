#include "voxel/editing/VoxelEditingCommands.h"

#include "editing/PropertySchemaJson.h"
#include "voxel/editing/VoxelCatalog.h"

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

editing::PropertyDescriptor stringProp(const char* path, const char* title) {
    editing::PropertyDescriptor descriptor;
    descriptor.path           = editing::PropertyPath(path);
    descriptor.displayNameKey = title;
    descriptor.type           = editing::PropertyType::String;
    return descriptor;
}

editing::PropertyDescriptor intProp(const char* path, const char* title) {
    editing::PropertyDescriptor descriptor;
    descriptor.path           = editing::PropertyPath(path);
    descriptor.displayNameKey = title;
    descriptor.type           = editing::PropertyType::Int;
    return descriptor;
}

editing::PropertyDescriptor boolProp(const char* path, const char* title) {
    editing::PropertyDescriptor descriptor;
    descriptor.path           = editing::PropertyPath(path);
    descriptor.displayNameKey = title;
    descriptor.type           = editing::PropertyType::Bool;
    return descriptor;
}

editing::EditingCommandDescriptor makeDescriptor(const char* id, const char* displayName,
                                                 editing::PropertySchema schema) {
    editing::EditingCommandDescriptor descriptor;
    descriptor.id                = editing::CommandId(id);
    descriptor.ownerModule       = "voxel_editing";
    descriptor.displayName       = displayName;
    descriptor.category          = "Voxel";
    descriptor.automationAllowed = true;
    descriptor.targetKinds       = {"voxel-catalog", "voxel-model"};
    descriptor.payloadSchema     = std::move(schema);
    descriptor.panelCapability   = "voxel.sculpt";
    descriptor.panelContext      = "inspector";
    return descriptor;
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
        return eve::editing::failed<editing::CommandPlan>(operation.code(), editing::RuleId("voxel.editing.operation"),
                                                          "Voxel catalog rejected the planned edit");
    editing::CommandPlan plan;
    plan.operations.push_back(std::move(operation).takeValue());
    plan.summary = std::move(summary);
    return eve::editing::applied<editing::CommandPlan>(std::move(plan));
}

}  // namespace

std::vector<editing::EditingCommandDescriptor> voxelEditingCommandDescriptors() {
    std::vector<editing::EditingCommandDescriptor> descriptors;
    {
        editing::PropertySchema schema;
        schema.typeId = "voxel.model.create.v1";
        schema.properties.push_back(stringProp("id", "Id"));
        schema.properties.push_back(stringProp("name", "Name"));
        schema.properties.push_back(intProp("sizeX", "Size X"));
        schema.properties.push_back(intProp("sizeY", "Size Y"));
        schema.properties.push_back(intProp("sizeZ", "Size Z"));
        descriptors.push_back(makeDescriptor("voxel.model.create.v1", "Create voxel model", std::move(schema)));
    }
    {
        editing::PropertySchema schema;
        schema.typeId = "voxel.model.delete.v1";
        schema.properties.push_back(stringProp("id", "Id"));
        descriptors.push_back(makeDescriptor("voxel.model.delete.v1", "Delete voxel model", std::move(schema)));
    }
    {
        editing::PropertySchema schema;
        schema.typeId = "voxel.model.voxel.set.v1";
        schema.properties.push_back(stringProp("item", "Item"));
        schema.properties.push_back(intProp("x", "X"));
        schema.properties.push_back(intProp("y", "Y"));
        schema.properties.push_back(intProp("z", "Z"));
        schema.properties.push_back(boolProp("occupied", "Occupied"));
        descriptors.push_back(makeDescriptor("voxel.model.voxel.set.v1", "Set voxel occupancy", std::move(schema)));
    }
    {
        editing::PropertySchema schema;
        schema.typeId = "voxel.property.set.v1";
        schema.properties.push_back(stringProp("item", "Item"));
        schema.properties.push_back(stringProp("path", "Path"));
        editing::PropertyDescriptor value = stringProp("value", "Value");
        value.type                        = editing::PropertyType::Struct;
        schema.properties.push_back(std::move(value));
        descriptors.push_back(makeDescriptor("voxel.property.set.v1", "Set voxel catalog property", std::move(schema)));
    }
    return descriptors;
}

editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry) {
    auto descriptors = voxelEditingCommandDescriptors();
    auto registered  = registry.registerPlannedCommand(
        descriptors[0], [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* catalog = asCatalog(target);
            const std::string* id = stringField(request.payload, "id");
            if (!catalog || !id || id->empty())
                return eve::editing::failed<editing::CommandPlan>(editing::Status::Rejected,
                                                                  editing::RuleId("voxel.editing.model-payload"),
                                                                  "Voxel model create requires id");
            const std::string* name = stringField(request.payload, "name");
            VoxelModelValue model;
            model.id   = editing::ObjectId(*id);
            model.name = name && !name->empty() ? *name : *id;
            if (!readInt(request.payload, "sizeX", model.sizeX, false) ||
                !readInt(request.payload, "sizeY", model.sizeY, false) ||
                !readInt(request.payload, "sizeZ", model.sizeZ, false))
                return eve::editing::failed<editing::CommandPlan>(editing::Status::Rejected,
                                                                  editing::RuleId("voxel.editing.size"),
                                                                  "Voxel model size must be integers");
            return planFrom(catalog->makeCreateModel(model), editing::Value::Object{{"id", *id}});
        });
    if (!registered.ok()) return registered;

    registered = registry.registerPlannedCommand(
        descriptors[1], [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* catalog = asCatalog(target);
            const std::string* id = stringField(request.payload, "id");
            if (!catalog || !id || id->empty())
                return eve::editing::failed<editing::CommandPlan>(editing::Status::Rejected,
                                                                  editing::RuleId("voxel.editing.model-payload"),
                                                                  "Voxel model delete requires id");
            return planFrom(catalog->makeDeleteModel(editing::ObjectId(*id)), editing::Value::Object{{"id", *id}});
        });
    if (!registered.ok()) return registered;

    registered = registry.registerPlannedCommand(
        descriptors[2], [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* catalog = asCatalog(target);
            const std::string* item = stringField(request.payload, "item");
            int x = 0, y = 0, z = 0;
            bool occupied = true;
            if (!catalog || !item || item->empty() || !readInt(request.payload, "x", x, true) ||
                !readInt(request.payload, "y", y, true) || !readInt(request.payload, "z", z, true) ||
                !readBool(request.payload, "occupied", occupied, true))
                return eve::editing::failed<editing::CommandPlan>(
                    editing::Status::Rejected, editing::RuleId("voxel.editing.voxel-payload"),
                    "Voxel occupancy requires item, x, y, z and occupied");
            return planFrom(catalog->makeSetVoxel(editing::ObjectId(*item), x, y, z, occupied),
                            editing::Value::Object{{"item", *item}});
        });
    if (!registered.ok()) return registered;

    return registry.registerPlannedCommand(
        descriptors[3], [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* catalog = asCatalog(target);
            auto* properties = static_cast<editing::IPropertyProvider*>(
                target.queryCapability(VoxelCatalogTarget::propertyCapabilityId()));
            const std::string* item = stringField(request.payload, "item");
            const std::string* path = stringField(request.payload, "path");
            const editing::Value* value = field(request.payload, "value");
            if (!catalog || !properties || !item || item->empty() || !path || !value)
                return eve::editing::failed<editing::CommandPlan>(editing::Status::Rejected,
                                                                  editing::RuleId("voxel.editing.property-payload"),
                                                                  "Voxel property requires item, path and value");
            return planFrom(properties->makeSet(selectionFor(*catalog, *item), editing::PropertyPath(*path), *value,
                                                editing::PropertySetMode::Absolute),
                            editing::Value::Object{{"item", *item}, {"path", *path}});
        });
}

}  // namespace eve::voxel_editing
