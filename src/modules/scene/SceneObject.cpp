#include "scene/SceneObject.h"

#include <utility>

namespace eve::scene {

eve::Result<SceneObject *> SceneObject::createObject(const std::string &hostName, const std::string &nodeId,
                                                     eve::SceneObjectId persistentId) {
    SceneObject *o = SceneObject::create();
    if (!o)
        return eve::Result<SceneObject *>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "scene object creation returned null", "scene.object"));
    o->meta()->entity = o;
    o->meta()->hostName = hostName;
    o->meta()->nodeId = nodeId;
    o->meta()->persistentId = persistentId;
    return eve::Result<SceneObject *>::success(o, eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::scene
