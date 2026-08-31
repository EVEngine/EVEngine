#pragma once

#include "editing/EditingGizmo.h"
#include "audio_editing/AudioTarget.h"
#include "lighting_editing/LightingTarget.h"

#include "editing/EditingProtocol.h"
#include "physics_editing/PhysicsTarget.h"

#include <functional>
#include <string>
#include <vector>

namespace eve::domain_gizmo_editing {
using AudioSourceTarget=audio_editing::AudioSourceTarget;
using Light3DDocumentTarget=lighting_editing::Light3DDocumentTarget;
using PhysicsColliderTarget=physics_editing::PhysicsColliderTarget;
using PhysicsJointTarget=physics_editing::PhysicsJointTarget;
using DiagnosticSeverity=editing::DiagnosticSeverity; using EditorStatus=editing::Status;
using EditorValue=editing::Value; using Revision=editing::Revision; using RuleId=editing::RuleId;
template<class T>using EditorResult=editing::Result<T>;

using EditorGizmoPrimitive = editing::GizmoPrimitive;
using EditorGizmoSnapshot  = editing::GizmoSnapshot;

/** @brief Builds collider, attenuation and light-volume overlays from editor documents. */
class EditorGizmoPreviewBuilder {
public:
    using ObjectPositionResolver = std::function<EditorResult<std::array<double, 3>>(const std::string&)>;
    /** @brief Build a collider wire shape and sensor/body-state coloring. */
    EditorGizmoSnapshot collider(const PhysicsColliderTarget& target) const;
    /** @brief Build body-anchor, connecting-line, axis and limit overlays for a joint. */
    EditorGizmoSnapshot joint(const PhysicsJointTarget& target,
                              const ObjectPositionResolver& bodies) const;
    /** @brief Build reference/max-distance spheres and source direction arrow. */
    EditorGizmoSnapshot audioSource(const AudioSourceTarget& target) const;
    /** @brief Build point-light radius or directional-light arrow overlay. */
    EditorGizmoSnapshot light(const Light3DDocumentTarget& target) const;
};

}  // namespace eve::domain_gizmo_editing
