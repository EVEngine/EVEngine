#include "domain_gizmo_editing/DomainGizmoPreview.h"

#include "audio_editing/AudioTarget.h"
#include "lighting_editing/LightingTarget.h"
#include "physics_editing/PhysicsTarget.h"

#include <algorithm>
#include <cmath>

namespace eve::domain_gizmo_editing {
namespace {

const EditorValue* property(const EditorValue& snapshot, const char* path) {
    const auto* root = snapshot.getIf<EditorValue::Object>();
    if (!root) return nullptr;
    const auto properties = root->find("properties");
    if (properties == root->end()) return nullptr;
    const auto* object = properties->second.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(path);
    return found == object->end() ? nullptr : &found->second;
}

bool vector3(const EditorValue* value, std::array<double, 3>& output) {
    const auto* array = value ? value->getIf<EditorValue::Array>() : nullptr;
    if (!array || (array->size() != 2 && array->size() != 3)) return false;
    output = {0.0, 0.0, 0.0};
    for (std::size_t index = 0; index < array->size(); ++index) {
        const auto* number = (*array)[index].getIf<double>();
        if (!number || !std::isfinite(*number)) return false;
        output[index] = *number;
    }
    return true;
}

EditorGizmoSnapshot failure(const std::string& target, Revision revision,
                            const char* rule, std::string message) {
    EditorGizmoSnapshot result;
    result.target = target; result.targetRevision = revision;
    result.diagnostics.push_back({RuleId(rule), DiagnosticSeverity::Error, std::move(message)});
    return result;
}

}  // namespace

EditorGizmoSnapshot EditorGizmoPreviewBuilder::collider(const PhysicsColliderTarget& target) const {
    EditorGizmoSnapshot result;
    result.target = target.targetId(); result.targetRevision = target.revision();
    const EditorValue snapshot = target.snapshotValue();
    const auto* kindValue = property(snapshot, "shape.kind");
    const auto* kind = kindValue ? kindValue->getIf<std::string>() : nullptr;
    EditorGizmoPrimitive primitive;
    primitive.id = "collider";
    if (!kind || !vector3(property(snapshot, "shape.offset"), primitive.position) ||
        !vector3(property(snapshot, "shape.size"), primitive.size))
        return failure(result.target, result.targetRevision, "editor.gizmo.invalid-collider",
                       "Collider snapshot lacks kind, offset or size");
    const auto* sensorValue = property(snapshot, "shape.sensor");
    const auto* sensor = sensorValue ? sensorValue->getIf<bool>() : nullptr;
    primitive.color = sensor && *sensor ? std::array<double, 4>{0.2, 0.8, 1.0, 1.0}
                                        : std::array<double, 4>{0.2, 1.0, 0.35, 1.0};
    primitive.dashed = sensor && *sensor;
    if (*kind == "box") primitive.kind = primitive.size[2] == 0.0 ? "wire-rect" : "wire-box";
    else if (*kind == "circle" || *kind == "sphere") {
        primitive.kind = *kind == "circle" ? "wire-circle" : "wire-sphere";
        const auto* radius = property(snapshot, "shape.radius");
        const auto* number = radius ? radius->getIf<double>() : nullptr;
        if (!number) return failure(result.target, result.targetRevision, "editor.gizmo.invalid-radius", "Collider radius is invalid");
        primitive.radius = *number;
    } else if (*kind == "capsule") {
        primitive.kind = "wire-capsule";
        const auto* radiusValue = property(snapshot, "shape.radius"); const auto* heightValue = property(snapshot, "shape.capsule-height");
        const auto* radius = radiusValue ? radiusValue->getIf<double>() : nullptr; const auto* height = heightValue ? heightValue->getIf<double>() : nullptr;
        if (!radius || !height) return failure(result.target, result.targetRevision, "editor.gizmo.invalid-capsule", "Capsule dimensions are invalid");
        primitive.radius = *radius; primitive.length = *height;
    } else {
        result.status = EditorStatus::Unsupported;
        result.diagnostics.push_back({RuleId("editor.gizmo.complex-collider-asset"), DiagnosticSeverity::Warning,
                                      "Complex collider gizmo requires resolved asset geometry: " + *kind});
        return result;
    }
    result.primitives.push_back(std::move(primitive)); result.status = EditorStatus::Applied;
    return result;
}

EditorGizmoSnapshot EditorGizmoPreviewBuilder::joint(const PhysicsJointTarget& target,
                                                     const ObjectPositionResolver& bodies) const {
    EditorGizmoSnapshot result; result.target = target.targetId(); result.targetRevision = target.revision();
    const EditorValue snapshot = target.snapshotValue();
    const auto* bodyAValue = property(snapshot, "body.a"); const auto* bodyBValue = property(snapshot, "body.b");
    const auto* bodyA = bodyAValue ? bodyAValue->getIf<std::string>() : nullptr;
    const auto* bodyB = bodyBValue ? bodyBValue->getIf<std::string>() : nullptr;
    std::array<double, 3> localA, localB, axis;
    if (!bodyA || bodyA->empty() || !bodyB || bodyB->empty() || !bodies ||
        !vector3(property(snapshot, "anchor.a"), localA) ||
        !vector3(property(snapshot, "anchor.b"), localB) ||
        !vector3(property(snapshot, "joint.axis"), axis))
        return failure(result.target, result.targetRevision, "editor.gizmo.invalid-joint",
                       "Joint gizmo requires body references, anchors, axis and a position resolver");
    auto originA = bodies(*bodyA); auto originB = bodies(*bodyB);
    if (!originA.value || !originB.value)
        return failure(result.target, result.targetRevision, "editor.gizmo.joint-body-not-found",
                       "Joint gizmo could not resolve both body positions");
    std::array<double, 3> anchorA, anchorB, line;
    for (std::size_t i = 0; i < 3; ++i) {
        anchorA[i] = originA.value->at(i) + localA[i];
        anchorB[i] = originB.value->at(i) + localB[i];
        line[i] = anchorB[i] - anchorA[i];
    }
    const double length = std::sqrt(line[0] * line[0] + line[1] * line[1] + line[2] * line[2]);
    if (length > 1e-12) for (double& component : line) component /= length;
    result.primitives.push_back({"anchor-a", "wire-sphere", anchorA, {}, {}, {1.0, 0.3, 0.2, 1.0}, 0.12, 0.0, false});
    result.primitives.push_back({"anchor-b", "wire-sphere", anchorB, {}, {}, {0.2, 0.6, 1.0, 1.0}, 0.12, 0.0, false});
    result.primitives.push_back({"joint-line", "line", anchorA, {}, line, {1.0, 0.85, 0.2, 1.0}, 0.0, length, false});
    result.primitives.push_back({"joint-axis", "arrow", anchorA, {}, axis, {0.4, 1.0, 0.4, 1.0}, 0.0, 1.0, false});
    const auto* enabledValue = property(snapshot, "limit.enabled"); const auto* enabled = enabledValue ? enabledValue->getIf<bool>() : nullptr;
    if (enabled && *enabled) {
        const auto* minimumValue = property(snapshot, "limit.minimum"); const auto* maximumValue = property(snapshot, "limit.maximum");
        const auto* minimum = minimumValue ? minimumValue->getIf<double>() : nullptr; const auto* maximum = maximumValue ? maximumValue->getIf<double>() : nullptr;
        if (minimum && maximum) result.primitives.push_back({"joint-limit", "arc", anchorA,
            {*minimum, *maximum, 0.0}, axis, {1.0, 0.45, 0.1, 1.0}, 1.0, 0.0, true});
    }
    result.status = EditorStatus::Applied; return result;
}

EditorGizmoSnapshot EditorGizmoPreviewBuilder::audioSource(const AudioSourceTarget& target) const {
    EditorGizmoSnapshot result; result.target = target.targetId(); result.targetRevision = target.revision();
    const EditorValue snapshot = target.snapshotValue(); std::array<double, 3> position, direction;
    const auto* referenceValue = property(snapshot, "spatial.reference-distance");
    const auto* maximumValue = property(snapshot, "spatial.maximum-distance");
    const auto* reference = referenceValue ? referenceValue->getIf<double>() : nullptr;
    const auto* maximum = maximumValue ? maximumValue->getIf<double>() : nullptr;
    if (!reference || !maximum || !vector3(property(snapshot, "spatial.position"), position) ||
        !vector3(property(snapshot, "spatial.direction"), direction))
        return failure(result.target, result.targetRevision, "editor.gizmo.invalid-audio-source",
                       "Audio source attenuation or transform is invalid");
    result.primitives.push_back({"reference-distance", "wire-sphere", position, {}, {},
                                 {0.2, 1.0, 0.5, 1.0}, *reference, 0.0, false});
    result.primitives.push_back({"maximum-distance", "wire-sphere", position, {}, {},
                                 {1.0, 0.55, 0.15, 0.75}, *maximum, 0.0, true});
    result.primitives.push_back({"direction", "arrow", position, {}, direction,
                                 {0.3, 0.7, 1.0, 1.0}, 0.0, std::max(1.0, *reference), false});
    result.status = EditorStatus::Applied; return result;
}

EditorGizmoSnapshot EditorGizmoPreviewBuilder::light(const Light3DDocumentTarget& target) const {
    EditorGizmoSnapshot result; result.target = target.targetId(); result.targetRevision = target.revision();
    const EditorValue snapshot = target.snapshotValue(); std::array<double, 3> position, direction;
    const auto* typeValue = property(snapshot, "light.type"); const auto* type = typeValue ? typeValue->getIf<std::string>() : nullptr;
    const auto* colorValue = property(snapshot, "light.color"); const auto* color = colorValue ? colorValue->getIf<EditorValue::Array>() : nullptr;
    if (!type || !color || color->size() != 4 || !vector3(property(snapshot, "transform.position"), position) ||
        !vector3(property(snapshot, "transform.direction"), direction))
        return failure(result.target, result.targetRevision, "editor.gizmo.invalid-light", "Light type, color or transform is invalid");
    std::array<double, 4> rgba;
    for (std::size_t i = 0; i < 4; ++i) { const auto* number = (*color)[i].getIf<double>(); if (!number) return failure(result.target, result.targetRevision, "editor.gizmo.invalid-light-color", "Light color is invalid"); rgba[i] = *number; }
    if (*type == "point") {
        const auto* radiusValue = property(snapshot, "light.radius"); const auto* radius = radiusValue ? radiusValue->getIf<double>() : nullptr;
        if (!radius) return failure(result.target, result.targetRevision, "editor.gizmo.invalid-light-radius", "Point light radius is invalid");
        result.primitives.push_back({"light-volume", "wire-sphere", position, {}, {}, rgba, *radius, 0.0, false});
    } else {
        result.primitives.push_back({"light-direction", "arrow", position, {}, direction, rgba, 0.0, 4.0, false});
    }
    result.status = EditorStatus::Applied; return result;
}

}  // namespace eve::domain_gizmo_editing
