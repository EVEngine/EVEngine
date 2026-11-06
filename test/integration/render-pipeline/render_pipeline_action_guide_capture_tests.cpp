// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"graphics","default":"render-window"}
#include <chrono>
#include <sstream>
#include <thread>

#include "common/ActionGuidePack.h"
#include "common/Capability.h"
#include "common/FrameCapture.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
std::string makePackJson(uint64_t entityId, uint64_t generation, int width, int height) {
    std::ostringstream oss;
    oss << R"({
      "schema":"eve.action.guide-pack","schemaVersion":1,"revision":7,
      "subject":{"domain":"renderable3d","id":)"
        << entityId << R"(,"generation":)" << generation << R"(},
      "skeletonRef":"s","clipRef":"c","rootMode":"inPlace",
      "canvas":{"width":)"
        << width << R"(,"height":)" << height << R"(,"pixelsPerUnit":32,"backgroundRgba":[0,0,0,0],"transparent":true},
      "camera":{"projection":"orthographic","fovYDegrees":45,"orthoSize":2.5,"nearPlane":0.1,"farPlane":50,
                "eye":[0,0,8],"target":[0,0,0],"up":[0,1,0]},
      "shots":[{"shotId":"s0","timeSeconds":0,"view":"front","aovs":["silhouette","depth","bone"],"tags":["p2"]}],
      "actionBinding":{"abilityId":"a","clipName":"c","fps":12,"loop":false,"phaseMarks":[]},
      "qc":{"minSilhouetteIoU":0.5,"maxJointErrorPx":8,"maxRetries":1}
    })";
    return oss.str();
}
}  // namespace

TEST_CASE("graphics.actionGuideCapture.silhouetteDepthAndCameraIndependence") {
    using namespace eve;
    auto*                  window   = window::Window::create();
    auto*                  graphics = graphics::Graphics::create();
    window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    graphics->present();

    auto* capture = cap::query<IActionGuideCapture>();
    if (graphics->frameCaptures().capabilities().backend != "vulkan") {
        REQUIRE(capture == nullptr);
        return;
    }
    REQUIRE(capture != nullptr);

    auto* camera = graphics::Camera3D::createCamera();
    camera->setEye(4, 3, 6);
    camera->setTarget(0, 0, 0);

    const float    positions[] = {-1, -2, -.5f, 1, -2, -.5f, 1, 2, -.5f, -1, 2, -.5f,
                                  -1, -2, .5f,  1, -2, .5f,  1, 2, .5f,  -1, 2, .5f};
    const float    normals[]   = {-1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1, -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1};
    const float    uv[16]{};
    const uint32_t indices[] = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
                                3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2};
    auto*          mesh      = graphics->newMeshFromArrays(positions, normals, uv, 8, indices, 36);
    auto*          entity    = graphics::Renderable3D::create();
    entity->setMesh(mesh);

    const auto packJson = makePackJson(entity->getEntityId(), entity->getEntityGeneration(), 64, 64);
    auto       queued   = capture->requestCapture(packJson, {});
    REQUIRE(queued.ok());
    const auto token = queued.value();

    camera->setEye(400, 300, 600);

    ActionGuideCaptureSnapshot snap;
    for (int i = 0; i < 120; ++i) {
        graphics->present();
        auto status = capture->captureSnapshot(token);
        REQUIRE(status.ok());
        snap = std::move(status.value());
        if (snap.phase == ActionGuideCapturePhase::Ready || snap.phase == ActionGuideCapturePhase::Failed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(snap.phase == ActionGuideCapturePhase::Ready);
    REQUIRE(snap.shots.size() == 1);
    REQUIRE(snap.shots[0].aovs.size() == 3);
    ASSERT(camera->getEyeX() == 400.f);

    const ActionGuideAovBuffer* silhouette = nullptr;
    const ActionGuideAovBuffer* depth      = nullptr;
    const ActionGuideAovBuffer* bone       = nullptr;
    for (const auto& aov : snap.shots[0].aovs) {
        if (aov.aov == "silhouette") silhouette = &aov;
        if (aov.aov == "depth") depth = &aov;
        if (aov.aov == "bone") bone = &aov;
    }
    REQUIRE(silhouette != nullptr);
    REQUIRE(depth != nullptr);
    REQUIRE(bone != nullptr);
    REQUIRE(silhouette->rgba8 != nullptr);
    REQUIRE(depth->rgba8 != nullptr);
    REQUIRE(bone->rgba8 != nullptr);
    CHECK_EQ(silhouette->width, 64u);
    CHECK_EQ(silhouette->height, 64u);

    size_t white = 0, depthLit = 0;
    for (size_t i = 0; i + 3 < silhouette->rgba8->size(); i += 4) {
        if ((*silhouette->rgba8)[i] > 200 && (*silhouette->rgba8)[i + 1] > 200 && (*silhouette->rgba8)[i + 2] > 200)
            ++white;
        if ((*depth->rgba8)[i] > 8) ++depthLit;
    }
    ASSERT(white > 40);
    ASSERT(depthLit > 40);

    auto released = capture->releaseCapture(token);
    REQUIRE(released.ok());
    ecs::DestroyEntity(entity);
    REQUIRE(graphics->releaseMesh(mesh));
    delete mesh;
    ecs::DestroyEntity(camera);
}
