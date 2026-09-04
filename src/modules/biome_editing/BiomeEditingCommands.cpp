#include "biome_editing/BiomeEditingCommands.h"

#include "biome_editing/BiomeTarget.h"
#include "editing/EditingProperty.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace eve::biome_editing {
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

bool readNumber(const editing::Value& value, const char* key, double& output, bool required) {
    const editing::Value* entry = field(value, key);
    if (!entry) return !required;
    if (const auto* real = entry->getIf<double>()) {
        output = *real;
        return std::isfinite(output);
    }
    if (const auto* integer = entry->getIf<std::int64_t>()) {
        output = static_cast<double>(*integer);
        return true;
    }
    return false;
}

bool readInt(const editing::Value& value, const char* key, int& output, bool required) {
    if (!field(value, key)) return !required;
    double parsed = 0.0;
    if (!readNumber(value, key, parsed, true) || parsed < -100000.0 || parsed > 100000.0) return false;
    output = static_cast<int>(parsed);
    return true;
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
    descriptor.ownerModule       = "biome_editing";
    descriptor.displayName       = displayName;
    descriptor.category          = "Biome";
    descriptor.automationAllowed = true;
    return registry.registerPlannedCommand(std::move(descriptor), std::move(planner));
}

BiomeDocumentTarget* asBiome(editing::IEditableTarget& target) {
    return dynamic_cast<BiomeDocumentTarget*>(&target);
}

editing::SelectionSnapshot selectionFor(const BiomeDocumentTarget& target, const std::string& item,
                                        const std::string& type) {
    editing::SelectionSnapshot selection;
    selection.channel = "biome";
    editing::SelectionItem row;
    row.domain = editing::SelectionDomain::Asset;
    row.target = editing::TargetId(target.targetId());
    row.item   = editing::StableId(item);
    row.type   = type;
    selection.items.push_back(row);
    selection.primary = row;
    return selection;
}

editing::Result<editing::CommandPlan> planFrom(editing::Result<editing::DomainOperation> operation,
                                               editing::Value summary) {
    if (!operation.ok())
        return error<editing::CommandPlan>(operation.code(), "biome.editing.operation",
                                           "Biome target rejected the planned edit");
    editing::CommandPlan plan;
    plan.operations.push_back(std::move(operation).takeValue());
    plan.summary = std::move(summary);
    return eve::editing::applied<editing::CommandPlan>(std::move(plan));
}

}  // namespace

editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry) {
    auto registered = addCommand(
        registry, "biome.layer.create.v1", "Create biome layer",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* biome = asBiome(target);
            const std::string* id = stringField(request.payload, "id");
            const std::string* spatial = stringField(request.payload, "spatial");
            if (!biome || !id || id->empty() || !spatial || spatial->empty())
                return error<editing::CommandPlan>(editing::Status::Rejected, "biome.editing.layer-payload",
                                                   "Biome layer create requires id and spatial asset");
            const std::string* name = stringField(request.payload, "name");
            BiomeLayerValue layer;
            layer.id           = editing::ObjectId(*id);
            layer.name         = name && !name->empty() ? *name : *id;
            layer.spatialAsset = *spatial;
            layer.priority     = 0;
            layer.density      = 1.f;
            double density     = 1.0;
            if (!readInt(request.payload, "priority", layer.priority, false) ||
                !readNumber(request.payload, "density", density, false))
                return error<editing::CommandPlan>(editing::Status::Rejected, "biome.editing.layer-number",
                                                   "Biome layer priority and density must be numbers");
            layer.density = static_cast<float>(density);
            return planFrom(biome->makeCreateLayer(layer), editing::Value::Object{{"id", *id}});
        });
    if (!registered.ok()) return registered;

    registered = addCommand(
        registry, "biome.asset.create.v1", "Create biome asset",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* biome = asBiome(target);
            const std::string* layer = stringField(request.payload, "layer");
            const std::string* id = stringField(request.payload, "id");
            const std::string* asset = stringField(request.payload, "asset");
            if (!biome || !layer || layer->empty() || !id || id->empty() || !asset || asset->empty())
                return error<editing::CommandPlan>(editing::Status::Rejected, "biome.editing.asset-payload",
                                                   "Biome asset create requires layer, id and asset");
            BiomeAssetValue entry;
            entry.id    = editing::ObjectId(*id);
            entry.asset = *asset;
            double weight = 1.0, minScale = 1.0, maxScale = 1.0;
            if (!readNumber(request.payload, "weight", weight, false) ||
                !readNumber(request.payload, "minScale", minScale, false) ||
                !readNumber(request.payload, "maxScale", maxScale, false) ||
                !readBool(request.payload, "randomYaw", entry.randomYaw, false))
                return error<editing::CommandPlan>(editing::Status::Rejected, "biome.editing.asset-number",
                                                   "Biome asset weight, scale and yaw must be valid");
            entry.weight   = static_cast<float>(weight);
            entry.minScale = static_cast<float>(minScale);
            entry.maxScale = static_cast<float>(maxScale);
            return planFrom(biome->makeCreateAsset(editing::ObjectId(*layer), entry),
                            editing::Value::Object{{"id", *id}, {"layer", *layer}});
        });
    if (!registered.ok()) return registered;

    return addCommand(
        registry, "biome.property.set.v1", "Set biome property",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto* biome = asBiome(target);
            auto* properties = static_cast<editing::IPropertyProvider*>(
                target.queryCapability(BiomeDocumentTarget::propertyCapabilityId()));
            const std::string* item = stringField(request.payload, "item");
            const std::string* type = stringField(request.payload, "type");
            const std::string* path = stringField(request.payload, "path");
            const editing::Value* value = field(request.payload, "value");
            if (!biome || !properties || !item || item->empty() || !type || !path || !value)
                return error<editing::CommandPlan>(editing::Status::Rejected, "biome.editing.property-payload",
                                                   "Biome property requires item, type, path and value");
            return planFrom(properties->makeSet(selectionFor(*biome, *item, *type), editing::PropertyPath(*path),
                                                *value, editing::PropertySetMode::Absolute),
                            editing::Value::Object{{"item", *item}, {"path", *path}});
        });
}

}  // namespace eve::biome_editing
