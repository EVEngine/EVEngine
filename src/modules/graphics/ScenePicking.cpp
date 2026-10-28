// Provides the scene camera projection capability.
//
// scene/Scene.cpp used to implement pickScreenAt / collectFrustumIdsAt, which
// forced scene (L1) to include graphics headers (L3) -- an upward dependency.
// The camera math those entry points need (screen ray, clip matrix) is still
// graphics' business, but defining scene's members here also made scene's own
// object files reference a symbol only graphics defines, which a per-layer DLL
// split cannot link. So scene keeps the entry points and this file provides
// ISceneCameraProjection instead; scene queries it through common/Capability.h.
//
// The file is excluded from the build when scene is disabled (same rule as
// SceneLinks.cpp), so graphics keeps working without scene.

#include "graphics/ClipSpace.h"
#include "graphics/RenderSystem3D.h"
#include "scene/SceneCameraProjection.h"

#include "common/Capability.h"

#include <glm/gtc/matrix_transform.hpp>

#include <optional>

namespace eve::graphics {
namespace {

class CameraProjection final : public eve::scene::ISceneCameraProjection {
public:
    std::optional<eve::scene::ScreenRay> screenRay(Camera3D &cam, float screenX, float screenY, float viewW,
                                                   float viewH) const override {
        cam.screenToRay(screenX, screenY, viewW, viewH);
        eve::scene::ScreenRay ray;
        ray.origin    = {cam.getScreenRayOriginX(), cam.getScreenRayOriginY(), cam.getScreenRayOriginZ()};
        ray.direction = {cam.getScreenRayDirX(), cam.getScreenRayDirY(), cam.getScreenRayDirZ()};
        return ray;
    }

    std::optional<eve::scene::CameraClip> clipForViewport(Camera3D &cam, float viewW, float viewH) const override {
        if (!(viewW > 0.f) || !(viewH > 0.f)) return std::nullopt;
        auto            d = cam.data();
        const glm::mat4 view =
            glm::lookAtRH(glm::vec3(d->eyeX, d->eyeY, d->eyeZ), glm::vec3(d->targetX, d->targetY, d->targetZ),
                          glm::vec3(d->upX, d->upY, d->upZ));
        const glm::mat4 projection = graphics::cameraProjectionVulkanRH_ZO(
            d->orthographic, glm::radians(d->fovYDeg), d->orthoHeight, viewW / viewH, d->nearZ, d->farZ);
        eve::scene::CameraClip clip;
        clip.clip    = projection * view;
        clip.inverse = glm::inverse(clip.clip);
        return clip;
    }
};

CameraProjection g_cameraProjection;

// A static registrar, like the link-kind registration right next door in
// SceneLinks.cpp: the picking entry points must work for callers that build a
// Camera3D without constructing the Graphics module (test/scene.cpp does), and
// the adapter holds no state, so there is nothing to tear down.
struct RegisterCameraProjection {
    RegisterCameraProjection() { eve::cap::provide<eve::scene::ISceneCameraProjection>(&g_cameraProjection); }
} g_registerCameraProjection;

}  // namespace
}  // namespace eve::graphics
