// Scene picking entry points that need a complete Camera3D.
//
// scene/Scene.cpp used to implement pickScreenAt / collectFrustumIdsAt, which
// forced scene (L1) to include graphics headers (L3) — an upward dependency.
// The two entry points still belong to the Scene script API, but the camera
// projection they need is graphics' business, so the implementations live
// here, in the graphics module (graphics -> scene is a legal downward edge).
// The file is excluded from the build when scene is disabled (same rule as
// SceneLinks.cpp), so graphics keeps working without scene.

#include "graphics/ClipSpace.h"
#include "graphics/RenderSystem3D.h"
#include "scene/Scene.h"
#include "scene/SceneBounds.h"

#include <glm/gtc/matrix_transform.hpp>

#include <limits>
#include <string>
#include <vector>

namespace eve::scene {

std::string Scene::pickScreenAt(const std::string &hostName, graphics::Camera3D *cam,
                                float screenX, float screenY, float viewW,
                                float viewH) const {
    if (!cam) return {};
    cam->screenToRay(screenX, screenY, viewW, viewH);
    return pickRayAt(hostName, cam->getScreenRayOriginX(), cam->getScreenRayOriginY(),
                     cam->getScreenRayOriginZ(), cam->getScreenRayDirX(),
                     cam->getScreenRayDirY(), cam->getScreenRayDirZ());
}

std::vector<std::string> Scene::collectFrustumIdsAt(const std::string &hostName,
                                                    graphics::Camera3D *cam, float viewW,
                                                    float viewH) const {
    SceneHost *h = resolveHost(hostName);
    std::vector<std::string> out;
    if (!h || !cam || viewW <= 0.f || viewH <= 0.f) return out;
    auto d = cam->data();
    const glm::vec3 eye(d->eyeX, d->eyeY, d->eyeZ);
    const glm::vec3 target(d->targetX, d->targetY, d->targetZ);
    const glm::vec3 up(d->upX, d->upY, d->upZ);
    const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
    const glm::mat4 projM = graphics::cameraProjectionVulkanRH_ZO(
        d->orthographic, glm::radians(d->fovYDeg), d->orthoHeight,
        viewW / viewH, d->nearZ, d->farZ);
    const glm::mat4 clip = projM * viewM;
    const glm::mat4 invClip = glm::inverse(clip);

    h->walkDepthFirst([&](SceneHost *, int, SceneNode &n) {
        if (!n.hasBounds) return;
        if (aabbIntersectsFrustum(clip, invClip, worldBoundsOf(n))) {
            out.push_back(n.id);
        }
    });
    return out;
}

}  // namespace eve::scene
