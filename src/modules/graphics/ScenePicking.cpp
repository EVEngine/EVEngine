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

Result<int> Scene::applyPcgTerrainCullingAt(const std::string &hostName,graphics::Camera3D *cam,
                                             float viewW,float viewH,const std::string &tag) {
    SceneHost *host=resolveHost(hostName);
    if(!host||!cam||!std::isfinite(viewW)||!std::isfinite(viewH)||viewW<=0.f||viewH<=0.f||tag.empty())
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "Pcg terrain culling requires host, camera, positive viewport and tag","scene.pcgTerrainCulling"));
    auto d=cam->data();
    const glm::mat4 view=glm::lookAtRH(glm::vec3(d->eyeX,d->eyeY,d->eyeZ),
        glm::vec3(d->targetX,d->targetY,d->targetZ),glm::vec3(d->upX,d->upY,d->upZ));
    const glm::mat4 projection=graphics::cameraProjectionVulkanRH_ZO(d->orthographic,
        glm::radians(d->fovYDeg),d->orthoHeight,viewW/viewH,d->nearZ,d->farZ);
    const glm::mat4 clip=projection*view, inverse=glm::inverse(clip);
    std::vector<std::pair<std::string,bool>> changes;
    host->walkDepthFirst([&](SceneHost*,int,SceneNode& node){
        if(!node.hasBounds||std::find(node.tags.begin(),node.tags.end(),tag)==node.tags.end()) return;
        const bool visible=aabbIntersectsFrustum(clip,inverse,worldBoundsOf(node));
        if(node.visible!=visible) changes.emplace_back(node.id,visible);
    });
    for(const auto& [id,visible]:changes) {
        auto node=host->findById(id); if(!node) continue;
        node.value()->visible=visible; host->markSubtreeDirtyById(id);
    }
    return Result<int>::success(static_cast<int>(changes.size()));
}

}  // namespace eve::scene
