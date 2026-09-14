#include "archspace/editing/ArchSpaceEditingCommands.h"

#include "archspace/editing/ArchSpaceTarget.h"
#include "editing/EditingProperty.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace eve::archspace_editing {
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

template <class T>
editing::Result<T> error(editing::Status status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, editing::RuleId(rule), std::move(message));
}

editing::Result<void> addCommand(editing::IEditingCommandRegistry& registry, const char* id, const char* displayName,
                                 editing::EditingCommandPlanner planner) {
    editing::EditingCommandDescriptor descriptor;
    descriptor.id                = editing::CommandId(id);
    descriptor.ownerModule       = "archspace_editing";
    descriptor.displayName       = displayName;
    descriptor.category          = "ArchSpace";
    descriptor.automationAllowed = true;
    return registry.registerPlannedCommand(std::move(descriptor), std::move(planner));
}

ArchSpaceDocumentTarget* asArchSpace(editing::IEditableTarget& target) {
    return dynamic_cast<ArchSpaceDocumentTarget*>(&target);
}

editing::Result<editing::CommandPlan> planFrom(editing::Result<editing::DomainOperation> operation,
                                               editing::Value                            summary) {
    if (!operation.ok())
        return error<editing::CommandPlan>(operation.code(), "archspace.editing.operation",
                                           "ArchSpace target rejected the planned edit");
    editing::CommandPlan plan;
    plan.operations.push_back(std::move(operation).value());
    plan.summary = std::move(summary);
    return eve::editing::applied<editing::CommandPlan>(std::move(plan));
}

}  // namespace

editing::Result<void> registerEditingCommands(editing::IEditingCommandRegistry& registry) {
    auto registered = addCommand(registry, "archspace.bootstrap.v1", "Bootstrap ArchSpace site",
                                 [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
                                     auto*              doc      = asArchSpace(target);
                                     const std::string* site     = stringField(request.payload, "siteId");
                                     const std::string* building = stringField(request.payload, "buildingId");
                                     const std::string* level    = stringField(request.payload, "levelId");
                                     double             height   = 3.0;
                                     if (!doc || !site || site->empty() || !building || building->empty() || !level ||
                                         level->empty() || !readNumber(request.payload, "levelHeight", height, false))
                                         return error<editing::CommandPlan>(
                                             editing::Status::Rejected, "archspace.editing.bootstrap",
                                             "ArchSpace bootstrap requires siteId, buildingId and levelId");
                                     return planFrom(doc->makeBootstrap(*site, *building, *level, height),
                                                     editing::Value::Object{{"levelId", *level}});
                                 });
    if (!registered.ok()) return registered;

    registered = addCommand(
        registry, "archspace.room.create.v1", "Create ArchSpace rectangular room",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto*              doc     = asArchSpace(target);
            const std::string* level   = stringField(request.payload, "levelId");
            const std::string* room    = stringField(request.payload, "roomId");
            const std::string* name    = stringField(request.payload, "name");
            double             originX = 0, originZ = 0, sizeX = 0, sizeZ = 0, wallHeight = 3, wallThickness = 0.2,
                   slabThickness = 0.2;
            if (!doc || !level || level->empty() || !room || room->empty() ||
                !readNumber(request.payload, "originX", originX, true) ||
                !readNumber(request.payload, "originZ", originZ, true) ||
                !readNumber(request.payload, "sizeX", sizeX, true) ||
                !readNumber(request.payload, "sizeZ", sizeZ, true) ||
                !readNumber(request.payload, "wallHeight", wallHeight, false) ||
                !readNumber(request.payload, "wallThickness", wallThickness, false) ||
                !readNumber(request.payload, "slabThickness", slabThickness, false))
                return error<editing::CommandPlan>(editing::Status::Rejected, "archspace.editing.room",
                                                   "ArchSpace room create requires levelId, roomId and size");
            return planFrom(doc->makeCreateRectRoom(*level, *room, name ? *name : *room, originX, originZ, sizeX, sizeZ,
                                                    wallHeight, wallThickness, slabThickness),
                            editing::Value::Object{{"roomId", *room}});
        });
    if (!registered.ok()) return registered;

    registered = addCommand(
        registry, "archspace.wall.create.v1", "Create ArchSpace wall",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto*              doc   = asArchSpace(target);
            const std::string* level = stringField(request.payload, "levelId");
            const std::string* wall  = stringField(request.payload, "wallId");
            const std::string* name  = stringField(request.payload, "name");
            double             x0 = 0, z0 = 0, x1 = 0, z1 = 0, height = 3.0, thickness = 0.2;
            if (!doc || !level || level->empty() || !wall || wall->empty() ||
                !readNumber(request.payload, "startX", x0, true) ||
                !readNumber(request.payload, "startZ", z0, true) ||
                !readNumber(request.payload, "endX", x1, true) || !readNumber(request.payload, "endZ", z1, true) ||
                !readNumber(request.payload, "height", height, false) ||
                !readNumber(request.payload, "thickness", thickness, false))
                return error<editing::CommandPlan>(editing::Status::Rejected, "archspace.editing.wall",
                                                   "ArchSpace wall create requires levelId, wallId and endpoints");
            return planFrom(doc->makeCreateWall(*level, *wall, name ? *name : *wall, archspace::Vec2{x0, z0},
                                                archspace::Vec2{x1, z1}, height, thickness),
                            editing::Value::Object{{"wallId", *wall}});
        });
    if (!registered.ok()) return registered;

    registered = addCommand(
        registry, "archspace.opening.create.v1", "Create ArchSpace opening",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto*              doc      = asArchSpace(target);
            const std::string* wall     = stringField(request.payload, "wallId");
            const std::string* opening  = stringField(request.payload, "openingId");
            const std::string* kindText = stringField(request.payload, "kind");
            double             t = 0.5, width = 0.9, height = 2.1, sill = 0.0;
            if (!doc || !wall || wall->empty() || !opening || opening->empty() || !kindText ||
                !readNumber(request.payload, "t", t, false) || !readNumber(request.payload, "width", width, false) ||
                !readNumber(request.payload, "height", height, false) ||
                !readNumber(request.payload, "sill", sill, false))
                return error<editing::CommandPlan>(editing::Status::Rejected, "archspace.editing.opening",
                                                   "ArchSpace opening requires wallId, openingId and kind");
            auto kind = archspace::parseOpeningKind(*kindText);
            if (!kind.ok())
                return error<editing::CommandPlan>(editing::Status::Rejected, "archspace.editing.opening",
                                                   "ArchSpace opening requires wallId, openingId and kind");
            return planFrom(doc->makeCreateOpening(*wall, *opening, kind.value(), t, width, height, sill),
                            editing::Value::Object{{"openingId", *opening}});
        });
    if (!registered.ok()) return registered;

    registered = addCommand(
        registry, "archspace.item.place.v1", "Place ArchSpace item",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto*              doc     = asArchSpace(target);
            const std::string* level   = stringField(request.payload, "levelId");
            const std::string* item    = stringField(request.payload, "itemId");
            const std::string* catalog = stringField(request.payload, "catalogId");
            double             x = 0, y = 0, z = 0, yaw = 0;
            if (!doc || !level || level->empty() || !item || item->empty() || !catalog || catalog->empty() ||
                !readNumber(request.payload, "x", x, true) || !readNumber(request.payload, "y", y, false) ||
                !readNumber(request.payload, "z", z, true) || !readNumber(request.payload, "yawDegrees", yaw, false))
                return error<editing::CommandPlan>(editing::Status::Rejected, "archspace.editing.item",
                                                   "ArchSpace item place requires levelId, itemId, catalogId, x, z");
            return planFrom(doc->makePlaceItem(*level, *item, *catalog, archspace::Vec3{x, y, z}, yaw),
                            editing::Value::Object{{"itemId", *item}});
        });
    if (!registered.ok()) return registered;

    registered = addCommand(
        registry, "archspace.property.set.v1", "Set ArchSpace property",
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
            auto*              doc    = asArchSpace(target);
            const std::string* object = stringField(request.payload, "objectId");
            const std::string* path   = stringField(request.payload, "path");
            const editing::Value* value = field(request.payload, "value");
            if (!doc || !object || object->empty() || !path || path->empty() || !value)
                return error<editing::CommandPlan>(editing::Status::Rejected, "archspace.editing.property",
                                                   "ArchSpace property set requires objectId, path and value");
            editing::SelectionSnapshot selection;
            selection.channel = "archspace";
            selection.items.push_back({editing::SelectionDomain::Asset, doc->targetId(), editing::StableId(*object),
                                       std::string("archspace.node")});
            return planFrom(doc->makeSet(selection, editing::PropertyPath(*path), *value,
                                         editing::PropertySetMode::Absolute),
                            editing::Value::Object{{"objectId", *object}, {"path", *path}});
        });
    if (!registered.ok()) return registered;

    return addCommand(registry, "archspace.node.delete.v1", "Delete ArchSpace node",
                      [](editing::IEditableTarget& target, const editing::CommandRequest& request) {
                          auto*              doc = asArchSpace(target);
                          const std::string* id  = stringField(request.payload, "id");
                          if (!doc || !id || id->empty())
                              return error<editing::CommandPlan>(editing::Status::Rejected, "archspace.editing.delete",
                                                                 "ArchSpace delete requires id");
                          return planFrom(doc->makeDeleteNode(editing::ObjectId(*id)),
                                          editing::Value::Object{{"id", *id}});
                      });
}

}  // namespace eve::archspace_editing
