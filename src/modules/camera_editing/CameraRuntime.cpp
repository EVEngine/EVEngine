#include "camera_editing/CameraTarget.h"

#include "camera/CameraController.h"

#include <utility>

namespace eve::camera_editing {
namespace {
template<class T> EditorResult<T> fail(EditorStatus status,const char* rule,std::string message){return EditorResult<T>::error(status,RuleId(rule),std::move(message));}
}

CameraDocumentRuntime::CameraDocumentRuntime() = default;
CameraDocumentRuntime::~CameraDocumentRuntime() = default;

EditorResult<void> CameraDocumentRuntime::publish(const CameraDocumentTarget& document,
                                                  graphics::Camera3D* camera) {
    const auto diagnostics = document.validate();
    for (const auto& diagnostic : diagnostics)
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            EditorResult<void> result;
            result.status = EditorStatus::Rejected;
            result.diagnostics = diagnostics;
            return result;
        }
    auto candidate = std::make_unique<camera::CameraController>();
    candidate->setCamera(camera);
    for (const auto& rig : document.rigs()) {
        candidate->setMode(rig.mode);
        candidate->setTarget(rig.target.x, rig.target.y, rig.target.z);
        candidate->setOffset(rig.offset.x, rig.offset.y, rig.offset.z);
        candidate->setLookAhead(rig.lookAhead.x, rig.lookAhead.y, rig.lookAhead.z);
        candidate->setComposition(rig.composition.x, rig.composition.y);
        candidate->setRadius(rig.radius);
        candidate->setAzimuth(rig.azimuth);
        candidate->setElevation(rig.elevation);
        candidate->setYaw(rig.yaw);
        candidate->setPitch(rig.pitch);
        candidate->setFov(rig.fov);
        candidate->setSmooth(rig.smooth);
        candidate->setMaxSpeed(rig.maxSpeed);
        if (!candidate->addRig(rig.id.value(), rig.mode, rig.priority) ||
            !candidate->saveRigState(rig.id.value()) ||
            !candidate->setRigEnabled(rig.id.value(), rig.enabled))
            return fail<void>(EditorStatus::Failed, "editor.camera.runtime-rig",
                              "Camera runtime rejected a validated rig");
    }
    for (const auto& key : document.timeline()) {
        bool accepted = false;
        if (key.kind == "cut") accepted = candidate->addTimelineCut(key.time, key.rig.value(), key.blend);
        else if (key.kind == "float") accepted = candidate->addTimelineFloat(key.time, key.property, key.value);
        else if (key.kind == "event") accepted = candidate->addTimelineEvent(key.time, key.name, key.data);
        if (!accepted)
            return fail<void>(EditorStatus::Failed, "editor.camera.runtime-key",
                              "Camera runtime rejected a validated timeline key");
    }
    controller_ = std::move(candidate);
    revision_ = document.revision();
    EditorResult<void> result = EditorResult<void>::applied();
    result.diagnostics = diagnostics;
    return result;
}

} // namespace eve::camera_editing
