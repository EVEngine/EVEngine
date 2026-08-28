#pragma once

#include "editor/EditorProtocol.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace eve::editor {

class PhysicsColliderTarget;
class PhysicsJointTarget;
class AudioSourceTarget;
class Light3DDocumentTarget;

/** @brief Renderer-neutral wire primitive for editor viewport overlays. */
struct EditorGizmoPrimitive {
    std::string id;
    std::string kind;
    std::array<double, 3> position{0.0, 0.0, 0.0};
    std::array<double, 3> size{0.0, 0.0, 0.0};
    std::array<double, 3> direction{0.0, 0.0, -1.0};
    std::array<double, 4> color{1.0, 1.0, 1.0, 1.0};
    double radius = 0.0;
    double length = 0.0;
    bool dashed = false;
};

/** @brief Immutable revision-tagged overlay snapshot consumed by any viewport renderer. */
struct EditorGizmoSnapshot {
    EditorStatus status = EditorStatus::Failed;
    std::string target;
    Revision targetRevision = 0;
    std::vector<EditorGizmoPrimitive> primitives;
    std::vector<EditorDiagnostic> diagnostics;
};

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

}  // namespace eve::editor
