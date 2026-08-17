#include "scene/SceneObject.h"

namespace eve::scene {

SceneObject *SceneObject::createObject(const std::string &hostName,
                                       const std::string &nodeId) {
    SceneObject *o = SceneObject::create();
    o->meta()->entity = o;
    o->meta()->hostName = hostName;
    o->meta()->nodeId = nodeId;
    return o;
}

}  // namespace eve::scene
