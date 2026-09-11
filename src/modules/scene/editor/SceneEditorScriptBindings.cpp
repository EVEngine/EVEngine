#include "scene/editor/SceneEditorScriptBindings.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "scene/Scene.h"
#include "scene/editing/SceneTarget.h"
#include "scene/editor/SceneEditorModule.h"
#include "scene/editor/SceneEditorSession.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::scene_editor {
namespace {
const editing::Value* field(const editing::Value& value, const char* key) {
    const auto* object = value.getIf<editing::Value::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

bool number(const editing::Value& value, double& output) {
    if (const auto* real = value.getIf<double>())
        output = *real;
    else if (const auto* integer = value.getIf<std::int64_t>())
        output = static_cast<double>(*integer);
    else
        return false;
    return true;
}

editing::Result<PhysicsPlacementCollider> placementCollider(const editing::Value& value) {
    PhysicsPlacementCollider result;
    const auto*              shapeValue = field(value, "shape");
    const auto*              shape      = shapeValue ? shapeValue->getIf<std::string>() : nullptr;
    if (!shape || (*shape != "box" && *shape != "sphere" && *shape != "capsule" && *shape != "convex"))
        return editing::failed<PhysicsPlacementCollider>(editing::Status::Rejected,
                                                         editing::RuleId("scene.physics-placement.script-part-shape"),
                                                         "Collider part shape is unsupported");
    if (*shape == "sphere")
        result.shape = PhysicsPlacementShape::Sphere;
    else if (*shape == "capsule")
        result.shape = PhysicsPlacementShape::Capsule;
    else if (*shape == "convex")
        result.shape = PhysicsPlacementShape::ConvexHull;
    if (const auto* sourceValue = field(value, "source")) {
        const auto* source = sourceValue->getIf<std::string>();
        if (!source || (*source != "existing" && *source != "generated"))
            return editing::failed<PhysicsPlacementCollider>(
                editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-part-source"),
                "Collider part source must be existing or generated");
        result.source = *source == "existing" ? PhysicsPlacementColliderSource::Existing
                                              : PhysicsPlacementColliderSource::Generated;
    }
    const auto parseVector = [&](const char* key, double& x, double& y, double& z, bool required) {
        const auto* candidate = field(value, key);
        if (!candidate) return !required;
        const auto* array = candidate->getIf<editing::Value::Array>();
        return array && array->size() == 3 && number((*array)[0], x) && number((*array)[1], y) &&
               number((*array)[2], z);
    };
    if (!parseVector("halfExtents", result.halfExtentX, result.halfExtentY, result.halfExtentZ, true) ||
        !parseVector("localPosition", result.localX, result.localY, result.localZ, false) ||
        !parseVector("localRotation", result.localRotationX, result.localRotationY, result.localRotationZ, false))
        return editing::failed<PhysicsPlacementCollider>(editing::Status::Rejected,
                                                         editing::RuleId("scene.physics-placement.script-part-vector"),
                                                         "Collider part vectors require three numbers");
    if (const auto* verticesValue = field(value, "vertices")) {
        const auto* vertices = verticesValue->getIf<editing::Value::Array>();
        if (!vertices)
            return editing::failed<PhysicsPlacementCollider>(
                editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-part-vertices"),
                "Collider part vertices must be an array");
        for (const auto& component : *vertices) {
            double numeric = 0.0;
            if (!number(component, numeric))
                return editing::failed<PhysicsPlacementCollider>(
                    editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-part-vertex"),
                    "Collider part vertices must be numeric");
            result.convexVertices.push_back(static_cast<float>(numeric));
        }
    }
    if (const auto* maximumValue = field(value, "maxHullVertices")) {
        const auto* maximum = maximumValue->getIf<std::int64_t>();
        if (!maximum)
            return editing::failed<PhysicsPlacementCollider>(
                editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-part-budget"),
                "Collider part maxHullVertices must be an integer");
        result.convexMaxVertices = static_cast<int>(*maximum);
    }
    return editing::applied<PhysicsPlacementCollider>(std::move(result));
}

editing::Result<PhysicsPlacementRequest> placementRequest(const editing::Value& payload) {
    const auto* objectValues = field(payload, "objects");
    const auto* objects      = objectValues ? objectValues->getIf<editing::Value::Array>() : nullptr;
    if (!objects)
        return editing::failed<PhysicsPlacementRequest>(editing::Status::Rejected,
                                                        editing::RuleId("scene.physics-placement.script-payload"),
                                                        "Physics placement requires an objects array");
    PhysicsPlacementRequest request;
    if (const auto* primaryValue = field(payload, "primary")) {
        const auto* primary = primaryValue->getIf<std::string>();
        if (!primary)
            return editing::failed<PhysicsPlacementRequest>(editing::Status::Rejected,
                                                            editing::RuleId("scene.physics-placement.script-primary"),
                                                            "Physics placement primary must be an object id");
        request.primaryObject = editing::ObjectId(*primary);
    }
    if (const auto* modeValue = field(payload, "mode")) {
        const auto* mode = modeValue->getIf<std::string>();
        if (!mode || (*mode != "drag" && *mode != "place" && *mode != "drop" && *mode != "fall" && *mode != "rotate" &&
                      *mode != "align" && *mode != "point"))
            return editing::failed<PhysicsPlacementRequest>(editing::Status::Rejected,
                                                            editing::RuleId("scene.physics-placement.script-mode"),
                                                            "Physics placement mode is unsupported");
        if (*mode == "drop" || *mode == "fall")
            request.settings.mode = PhysicsPlacementMode::Fall;
        else if (*mode == "rotate")
            request.settings.mode = PhysicsPlacementMode::Rotate;
        else if (*mode == "align")
            request.settings.mode = PhysicsPlacementMode::Align;
        else if (*mode == "point")
            request.settings.mode = PhysicsPlacementMode::Point;
        else
            request.settings.mode = PhysicsPlacementMode::Place;
    }
    if (const auto* settingsValue = field(payload, "settings")) {
        if (!settingsValue->getIf<editing::Value::Object>())
            return editing::failed<PhysicsPlacementRequest>(editing::Status::Rejected,
                                                            editing::RuleId("scene.physics-placement.script-settings"),
                                                            "Physics placement settings must be an object");
        const auto assign = [&](const char* key, double& destination) {
            const auto* value = field(*settingsValue, key);
            return !value || number(*value, destination);
        };
        if (!assign("gravityX", request.settings.gravityX) || !assign("gravityY", request.settings.gravityY) ||
            !assign("gravityZ", request.settings.gravityZ) ||
            !assign("maximumLinearSpeed", request.settings.maximumLinearSpeed) ||
            !assign("linearDamping", request.settings.linearDamping) ||
            !assign("angularDamping", request.settings.angularDamping) ||
            !assign("contactHertz", request.settings.contactHertz) ||
            !assign("contactDampingRatio", request.settings.contactDampingRatio) ||
            !assign("maximumPushOutSpeed", request.settings.maximumPushOutSpeed) ||
            !assign("maximumAngularSpeed", request.settings.maximumAngularSpeed) ||
            !assign("minimumBoundsSpeedFactor", request.settings.minimumBoundsSpeedFactor) ||
            !assign("maximumBoundsSpeedFactor", request.settings.maximumBoundsSpeedFactor) ||
            !assign("teleportDistance", request.settings.teleportDistance))
            return editing::failed<PhysicsPlacementRequest>(
                editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-setting-number"),
                "Physics placement numeric settings must contain numbers");
        if (const auto* subStepsValue = field(*settingsValue, "subStepCount")) {
            const auto* subSteps = subStepsValue->getIf<std::int64_t>();
            if (!subSteps)
                return editing::failed<PhysicsPlacementRequest>(
                    editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-substeps"),
                    "Physics placement subStepCount must be an integer");
            request.settings.subStepCount = static_cast<int>(*subSteps);
        }
        if (const auto* preserveValue = field(*settingsValue, "preserveSelectionLayout")) {
            const auto* preserve = preserveValue->getIf<bool>();
            if (!preserve)
                return editing::failed<PhysicsPlacementRequest>(
                    editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-layout"),
                    "Physics placement preserveSelectionLayout must be boolean");
            request.settings.preserveSelectionLayout = *preserve;
        }
        const auto assignBool = [&](const char* key, bool& destination) {
            const auto* value = field(*settingsValue, key);
            if (!value) return true;
            const auto* boolean = value->getIf<bool>();
            if (!boolean) return false;
            destination = *boolean;
            return true;
        };
        if (!assignBool("freezePositionX", request.settings.freezePositionX) ||
            !assignBool("freezePositionY", request.settings.freezePositionY) ||
            !assignBool("freezePositionZ", request.settings.freezePositionZ) ||
            !assignBool("freezeRotationX", request.settings.freezeRotationX) ||
            !assignBool("freezeRotationY", request.settings.freezeRotationY) ||
            !assignBool("freezeRotationZ", request.settings.freezeRotationZ) ||
            !assignBool("scaleSpeedByBounds", request.settings.scaleSpeedByBounds) ||
            !assignBool("softCollision", request.settings.softCollision))
            return editing::failed<PhysicsPlacementRequest>(
                editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-setting-bool"),
                "Physics placement boolean settings must be boolean");
        if (const auto* policyValue = field(*settingsValue, "colliderPolicy")) {
            const auto* policy = policyValue->getIf<std::string>();
            if (!policy || (*policy != "existingOnly" && *policy != "generatedWhenMissing" &&
                            *policy != "generatedOnly" && *policy != "existingAndGenerated"))
                return editing::failed<PhysicsPlacementRequest>(
                    editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-collider-policy"),
                    "Physics placement colliderPolicy is unsupported");
            if (*policy == "existingOnly")
                request.settings.colliderPolicy = PhysicsPlacementColliderPolicy::ExistingOnly;
            else if (*policy == "generatedOnly")
                request.settings.colliderPolicy = PhysicsPlacementColliderPolicy::GeneratedOnly;
            else if (*policy == "existingAndGenerated")
                request.settings.colliderPolicy = PhysicsPlacementColliderPolicy::ExistingAndGenerated;
            else
                request.settings.colliderPolicy = PhysicsPlacementColliderPolicy::GeneratedWhenMissing;
        }
    }
    if (const auto* admissionValue = field(payload, "admission")) {
        if (!admissionValue->getIf<editing::Value::Object>())
            return editing::failed<PhysicsPlacementRequest>(editing::Status::Rejected,
                                                            editing::RuleId("scene.physics-placement.script-admission"),
                                                            "Physics placement admission must be an object");
        for (const auto& [key, destination] : std::initializer_list<std::pair<const char*, std::uint64_t*>>{
                 {"includedLayerBits", &request.admission.includedLayerBits},
                 {"excludedLayerBits", &request.admission.excludedLayerBits}}) {
            if (const auto* candidate = field(*admissionValue, key)) {
                const auto* integer = candidate->getIf<std::int64_t>();
                if (!integer || *integer < 0)
                    return editing::failed<PhysicsPlacementRequest>(
                        editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-admission-layer"),
                        "Admission layer masks must be non-negative integers");
                *destination = static_cast<std::uint64_t>(*integer);
            }
        }
        for (const auto& [key, destination] : std::initializer_list<std::pair<const char*, bool*>>{
                 {"excludeDisabled", &request.admission.excludeDisabled},
                 {"excludeLocked", &request.admission.excludeLocked}}) {
            if (const auto* candidate = field(*admissionValue, key)) {
                const auto* boolean = candidate->getIf<bool>();
                if (!boolean)
                    return editing::failed<PhysicsPlacementRequest>(
                        editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-admission-flag"),
                        "Admission flags must be boolean");
                *destination = *boolean;
            }
        }
        if (const auto* tagsValue = field(*admissionValue, "requiredTags")) {
            const auto* tags = tagsValue->getIf<editing::Value::Array>();
            if (!tags)
                return editing::failed<PhysicsPlacementRequest>(
                    editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-admission-tags"),
                    "Admission requiredTags must be an array");
            for (const auto& candidate : *tags) {
                const auto* tag = candidate.getIf<std::string>();
                if (!tag)
                    return editing::failed<PhysicsPlacementRequest>(
                        editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-admission-tag"),
                        "Admission requiredTags must contain strings");
                request.admission.requiredTags.push_back(*tag);
            }
        }
        const auto* minimumValue = field(*admissionValue, "minimum");
        const auto* maximumValue = field(*admissionValue, "maximum");
        if (minimumValue || maximumValue) {
            const auto* minimum = minimumValue ? minimumValue->getIf<editing::Value::Array>() : nullptr;
            const auto* maximum = maximumValue ? maximumValue->getIf<editing::Value::Array>() : nullptr;
            if (!minimum || !maximum || minimum->size() != 3 || maximum->size() != 3 ||
                !number((*minimum)[0], request.admission.minimumX) ||
                !number((*minimum)[1], request.admission.minimumY) ||
                !number((*minimum)[2], request.admission.minimumZ) ||
                !number((*maximum)[0], request.admission.maximumX) ||
                !number((*maximum)[1], request.admission.maximumY) ||
                !number((*maximum)[2], request.admission.maximumZ))
                return editing::failed<PhysicsPlacementRequest>(
                    editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-admission-bounds"),
                    "Admission bounds require minimum and maximum numeric vectors");
            request.admission.useBounds = true;
        }
    }
    for (const auto& value : *objects) {
        const auto*            idValue       = field(value, "object");
        const auto*            id            = idValue ? idValue->getIf<std::string>() : nullptr;
        const auto*            selectedValue = field(value, "selected");
        const auto*            selected      = selectedValue ? selectedValue->getIf<bool>() : nullptr;
        const auto*            boundsValue   = field(value, "halfExtents");
        const auto*            bounds        = boundsValue ? boundsValue->getIf<editing::Value::Array>() : nullptr;
        PhysicsPlacementObject entry;
        if (!id || !selected || !bounds || bounds->size() != 3 || !number((*bounds)[0], entry.halfExtentX) ||
            !number((*bounds)[1], entry.halfExtentY) || !number((*bounds)[2], entry.halfExtentZ))
            return editing::failed<PhysicsPlacementRequest>(
                editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-object"),
                "Placement entries require object, selected and three half extents");
        entry.object   = editing::ObjectId(*id);
        entry.selected = *selected;
        if (const auto* shapeValue = field(value, "shape")) {
            const auto* shape = shapeValue->getIf<std::string>();
            if (!shape || (*shape != "box" && *shape != "sphere" && *shape != "capsule" && *shape != "convex" &&
                           *shape != "auto"))
                return editing::failed<PhysicsPlacementRequest>(
                    editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-shape"),
                    "Placement shape must be box, sphere, capsule, convex or auto");
            if (*shape == "sphere") entry.shape = PhysicsPlacementShape::Sphere;
            if (*shape == "capsule") entry.shape = PhysicsPlacementShape::Capsule;
            if (*shape == "convex") entry.shape = PhysicsPlacementShape::ConvexHull;
            if (*shape == "auto") entry.shape = PhysicsPlacementShape::Auto;
        }
        if (const auto* verticesValue = field(value, "vertices")) {
            const auto* vertices = verticesValue->getIf<editing::Value::Array>();
            if (!vertices)
                return editing::failed<PhysicsPlacementRequest>(
                    editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-vertices"),
                    "Convex placement vertices must be a numeric array");
            entry.convexVertices.reserve(vertices->size());
            for (const auto& vertex : *vertices) {
                double component = 0.0;
                if (!number(vertex, component))
                    return editing::failed<PhysicsPlacementRequest>(
                        editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-vertex"),
                        "Convex placement vertices must be numeric");
                entry.convexVertices.push_back(static_cast<float>(component));
            }
        }
        if (const auto* maximumValue = field(value, "maxHullVertices")) {
            const auto* maximum = maximumValue->getIf<std::int64_t>();
            if (!maximum)
                return editing::failed<PhysicsPlacementRequest>(
                    editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-hull-budget"),
                    "Convex hull vertex budget must be an integer");
            entry.convexMaxVertices = static_cast<int>(*maximum);
        }
        if (const auto* keyValue = field(value, "colliderResourceKey")) {
            const auto* key = keyValue->getIf<std::string>();
            if (!key)
                return editing::failed<PhysicsPlacementRequest>(
                    editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-resource-key"),
                    "colliderResourceKey must be a string");
            entry.colliderResourceKey = *key;
        }
        if (const auto* layerValue = field(value, "layerBits")) {
            const auto* layer = layerValue->getIf<std::int64_t>();
            if (!layer || *layer < 0)
                return editing::failed<PhysicsPlacementRequest>(editing::Status::Rejected,
                                                                editing::RuleId("scene.physics-placement.script-layer"),
                                                                "layerBits must be a non-negative integer");
            entry.layerBits = static_cast<std::uint64_t>(*layer);
        }
        for (const auto& [key, destination] : std::initializer_list<std::pair<const char*, bool*>>{
                 {"enabled", &entry.enabled}, {"locked", &entry.locked}}) {
            if (const auto* candidate = field(value, key)) {
                const auto* boolean = candidate->getIf<bool>();
                if (!boolean)
                    return editing::failed<PhysicsPlacementRequest>(
                        editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-object-flag"),
                        "Placement object flags must be boolean");
                *destination = *boolean;
            }
        }
        if (const auto* tagsValue = field(value, "tags")) {
            const auto* tags = tagsValue->getIf<editing::Value::Array>();
            if (!tags)
                return editing::failed<PhysicsPlacementRequest>(editing::Status::Rejected,
                                                                editing::RuleId("scene.physics-placement.script-tags"),
                                                                "Placement tags must be an array");
            for (const auto& candidate : *tags) {
                const auto* tag = candidate.getIf<std::string>();
                if (!tag)
                    return editing::failed<PhysicsPlacementRequest>(
                        editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-tag"),
                        "Placement tags must contain strings");
                entry.tags.push_back(*tag);
            }
        }
        if (const auto* collidersValue = field(value, "colliders")) {
            const auto* colliders = collidersValue->getIf<editing::Value::Array>();
            if (!colliders)
                return editing::failed<PhysicsPlacementRequest>(
                    editing::Status::Rejected, editing::RuleId("scene.physics-placement.script-colliders"),
                    "Placement colliders must be an array");
            for (const auto& colliderValue : *colliders) {
                auto collider = placementCollider(colliderValue);
                if (!collider.ok()) return editing::Result<PhysicsPlacementRequest>::failure(collider.status());
                entry.colliders.push_back(std::move(collider.value()));
            }
        }
        request.objects.push_back(std::move(entry));
    }
    return editing::applied<PhysicsPlacementRequest>(std::move(request));
}

editing::Value placementValue(const PhysicsPlacementFrame& frame) {
    editing::Value::Array objects;
    for (const auto& entry : frame.objects) {
        const auto& value = entry.transform;
        objects.emplace_back(editing::Value::Object{
            {"object", entry.object.value()},
            {"selected", entry.selected},
            {"position", editing::Value::Array{value.x, value.y, value.z}},
            {"rotation", editing::Value::Array{value.rotationX, value.rotationY, value.rotationZ}},
            {"scale", editing::Value::Array{value.scaleX, value.scaleY, value.scaleZ}},
        });
    }
    return editing::Value::Object{
        {"tick", static_cast<std::int64_t>(frame.tick)},
        {"colliding", frame.colliding},
        {"settled", frame.settled},
        {"surfaceAligned", frame.surfaceAligned},
        {"surfaceNormal", editing::Value::Array{frame.surfaceNormalX, frame.surfaceNormalY, frame.surfaceNormalZ}},
        {"objects", std::move(objects)}};
}
}  // namespace

void exposeSceneEditorSessions(ssq::Table& table, ssq::Class& module) {
    const auto vm  = table.getHandle();
    auto       cls = table.addClass<SceneEditorSession>(
        "SceneEditorSession", std::function<SceneEditorSession*()>([]() -> SceneEditorSession* { return nullptr; }),
        true);
    const auto project = [vm](editing::Result<editing::TransactionReceipt> result) {
        return script::projectStatusResult(vm, result.status(), result.ok(), false);
    };
    cls.addFunc("execute", [vm, project](SceneEditorSession* self, std::string command, ssq::Object payload) {
        auto value = script::valueFromSquirrel(payload);
        if (!value.ok()) return script::projectStatusResult(vm, value.status(), false, false);
        return project(self->execute(std::move(command), editing::toEditingValue(value.value())));
    });
    cls.addFunc("undo", [project](SceneEditorSession* self) { return project(self->undo()); });
    cls.addFunc("restrictCommands", [vm](SceneEditorSession* self, ssq::Array commands) {
        std::vector<std::string> ids;
        for (std::size_t i = 0; i < commands.size(); ++i) ids.push_back(commands.get<std::string>(i));
        auto result = self->restrictCommands(ids);
        return script::projectStatusResult(vm, result.status(), result.ok(), false);
    });
    cls.addFunc("redo", [project](SceneEditorSession* self) { return project(self->redo()); });
    cls.addFunc("saveJson", &SceneEditorSession::saveJson);
    cls.addFunc("restoreJson", [project](SceneEditorSession* self, const std::string& json) {
        return project(self->restoreJson(json));
    });
    cls.addFunc("snapshot", [vm](SceneEditorSession* self) {
        return script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, true,
                                           editing::toPresentationValue(self->snapshot()));
    });
    cls.addFunc("getRevision", &SceneEditorSession::revision);
    cls.addFunc("cachePhysicsPlacementHull", [vm](SceneEditorSession* self, const std::string& object,
                                                  ssq::Array vertices, int maxVertices) {
        std::vector<float> copied;
        copied.reserve(vertices.size());
        for (std::size_t index = 0; index < vertices.size(); ++index) copied.push_back(vertices.get<float>(index));
        auto result = self->cachePhysicsPlacementHull(editing::ObjectId(object), std::move(copied), maxVertices);
        return script::projectStatusResult(vm, result.status(), result.ok(), false);
    });
    cls.addFunc("cachePhysicsPlacementCompound", [vm](SceneEditorSession* self, const std::string& object,
                                                      const std::string& resourceKey, ssq::Array parts) {
        auto value = script::valueFromSquirrel(parts);
        if (!value.ok()) return script::projectStatusResult(vm, value.status(), false, false);
        auto                                  editingValue = editing::toEditingValue(value.value());
        const auto*                           values       = editingValue.getIf<editing::Value::Array>();
        std::vector<PhysicsPlacementCollider> colliders;
        if (values) {
            colliders.reserve(values->size());
            for (const auto& partValue : *values) {
                auto part = placementCollider(partValue);
                if (!part.ok()) return script::projectStatusResult(vm, part.status(), false, false);
                colliders.push_back(std::move(part.value()));
            }
        }
        auto result = self->cachePhysicsPlacementCompound(editing::ObjectId(object), resourceKey, std::move(colliders));
        return script::projectStatusResult(vm, result.status(), result.ok(), false);
    });
    cls.addFunc("savePhysicsPlacementColliderCacheJson", &SceneEditorSession::savePhysicsPlacementColliderCacheJson);
    cls.addFunc("restorePhysicsPlacementColliderCacheJson", [vm](SceneEditorSession* self, const std::string& json) {
        auto result = self->restorePhysicsPlacementColliderCacheJson(json);
        return script::projectStatusResult(vm, result.status(), result.ok(), false);
    });
    cls.addFunc("removePhysicsPlacementHull", [vm](SceneEditorSession* self, const std::string& object) {
        auto result = self->removePhysicsPlacementHull(editing::ObjectId(object));
        return script::projectStatusResult(vm, result.status(), result.ok(), false);
    });
    cls.addFunc("beginPhysicsPlacement", [vm](SceneEditorSession* self, ssq::Object payload) {
        auto value = script::valueFromSquirrel(payload);
        if (!value.ok()) return script::projectStatusResult(vm, value.status(), false, false);
        auto request = placementRequest(editing::toEditingValue(value.value()));
        if (!request.ok()) return script::projectStatusResult(vm, request.status(), false, false);
        auto result = self->beginPhysicsPlacement(std::move(request.value()));
        return script::projectStatusResult(vm, result.status(), result.ok(), false);
    });
    cls.addFunc("updatePhysicsPlacement", [vm](SceneEditorSession* self, float x, float y, float z, float dt) {
        auto result = self->updatePhysicsPlacement(x, y, z, dt);
        if (!result.ok()) return script::projectStatusResult(vm, result.status(), false, false);
        return script::projectStatusResult(vm, result.status(), true, true,
                                           editing::toPresentationValue(placementValue(result.value())));
    });
    cls.addFunc("updatePhysicsPlacementPose", [vm](SceneEditorSession* self, float x, float y, float z, float rotationX,
                                                   float rotationY, float rotationZ, float dt) {
        auto result = self->updatePhysicsPlacementPose(x, y, z, rotationX, rotationY, rotationZ, dt);
        if (!result.ok()) return script::projectStatusResult(vm, result.status(), false, false);
        return script::projectStatusResult(vm, result.status(), true, true,
                                           editing::toPresentationValue(placementValue(result.value())));
    });
    cls.addFunc("updatePhysicsPlacementTransform",
                [vm](SceneEditorSession* self, ssq::Array position, ssq::Array rotation, ssq::Array scale, float dt) {
                    if (position.size() != 3 || rotation.size() != 3 || scale.size() != 3)
                        return script::projectStatusResult(
                            vm,
                            Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "Placement transform arrays require three values")),
                            false, false);
                    auto result = self->updatePhysicsPlacementTransform(
                        position.get<float>(0), position.get<float>(1), position.get<float>(2), rotation.get<float>(0),
                        rotation.get<float>(1), rotation.get<float>(2), scale.get<float>(0), scale.get<float>(1),
                        scale.get<float>(2), dt);
                    if (!result.ok()) return script::projectStatusResult(vm, result.status(), false, false);
                    return script::projectStatusResult(vm, result.status(), true, true,
                                                       editing::toPresentationValue(placementValue(result.value())));
                });
    cls.addFunc("alignPhysicsPlacementToSurface",
                [vm](SceneEditorSession* self, ssq::Array from, ssq::Array to, float offset, float dt) {
                    if (from.size() != 3 || to.size() != 3)
                        return script::projectStatusResult(
                            vm,
                            Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "Surface alignment endpoints require three values")),
                            false, false);
                    auto result = self->alignPhysicsPlacementToSurface(from.get<float>(0), from.get<float>(1),
                                                                       from.get<float>(2), to.get<float>(0),
                                                                       to.get<float>(1), to.get<float>(2), offset, dt);
                    if (!result.ok()) return script::projectStatusResult(vm, result.status(), false, false);
                    return script::projectStatusResult(vm, result.status(), true, true,
                                                       editing::toPresentationValue(placementValue(result.value())));
                });
    cls.addFunc("commitPhysicsPlacement",
                [project](SceneEditorSession* self) { return project(self->commitPhysicsPlacement()); });
    cls.addFunc("cancelPhysicsPlacement", [vm](SceneEditorSession* self) {
        auto result = self->cancelPhysicsPlacement();
        return script::projectStatusResult(vm, result.status(), result.ok(), false);
    });
    cls.addFunc("isPhysicsPlacementActive", [](SceneEditorSession* self) { return self->physicsPlacementActive(); });
    module.addFunc("createSession", [vm](SceneEditorModule*, const std::string& id) {
        if (id.empty())
            return script::projectStatusResult(vm,
                                               Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                                 "Scene target id must not be empty")),
                                               false, false);
        auto object =
            script::makeOwnedSquirrelInstance<SceneEditorSession>(vm, std::make_unique<SceneEditorSession>(id));
        if (!object.ok()) return script::projectStatusResult(vm, object.status(), false, false);
        auto result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", std::move(object).takeValue());
        result.set("ownership", std::string("owned"));
        return result;
    });
    module.addFunc("createLiveSession", [vm](SceneEditorModule*, const std::string& id, const std::string& hostName) {
        auto* scene = ModuleManager::getInstance<scene::Scene>("Scene");
        if (!scene || id.empty())
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Live scene editing requires Scene module and a nonempty target id")),
                false, false);
        auto host = scene->findHost(hostName);
        if (!host.ok()) return script::projectStatusResult(vm, host.status(), false, false);
        auto object = script::makeOwnedSquirrelInstance<SceneEditorSession>(
            vm, std::make_unique<SceneEditorSession>(
                    std::make_unique<scene_editing::SceneHostEditorTarget>(id, host.value())));
        if (!object.ok()) return script::projectStatusResult(vm, object.status(), false, false);
        auto result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", std::move(object).takeValue());
        result.set("ownership", std::string("owned"));
        return result;
    });
}
}  // namespace eve::scene_editor
