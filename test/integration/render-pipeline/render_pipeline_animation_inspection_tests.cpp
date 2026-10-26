// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"animation","default":"render-window"}
#include <chrono>
#include <thread>
#include "animation/AnimClip.h"
#include "animation/AnimInspection.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "common/Capability.h"
#include "common/EntityInspection.h"
#include "devtools/FrameContactSheet.hpp"
#include "graphics/Graphics.h"
#include "graphics/InspectionPlan.h"
#include "graphics/RenderSystem3D.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("animation.inspection.sampledPosesRenderWithoutChangingMesh") {
    using namespace eve;
    auto*                  window   = window::Window::create();
    auto*                  graphics = graphics::Graphics::create();
    window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    graphics->present();
    auto* inspection = cap::query<IEntityInspection>();
    if (graphics->frameCaptures().capabilities().backend != "vulkan") {
        REQUIRE(inspection == nullptr);
        return;
    }
    REQUIRE(inspection != nullptr);
    auto* camera = graphics::Camera3D::createCamera();
    camera->setEye(3, 3, 10);
    camera->setTarget(3, 0, 0);
    animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    animation::AnimClip clip("translate");
    clip.setDuration(1);
    clip.setLoop(true);
    clip.addPositionKey(0, 0, 0, 0, 0);
    clip.addPositionKey(0, 1, 4, 0, 0);
    animation::AnimSkinStreamData data;
    data.vertexCount         = 3;
    data.bindPositions       = {0, 0, 0, 2, 0, 0, 0, 2, 0};
    data.bindNormals         = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    data.skeletonBones       = {0};
    data.boneNames           = {"root"};
    data.inverseBindMatrices = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};
    data.vertexSkinJoints    = std::vector<int>(12, 0);
    data.vertexWeights       = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const float    uv[6]{};
    const uint32_t indices[] = {0, 1, 2};
    auto* mesh  = graphics->newMeshFromArrays(data.bindPositions.data(), data.bindNormals.data(), uv, 3, indices, 3);
    auto  built = animation::AnimSkin::fromStreams(std::move(data));
    REQUIRE(built.ok());
    auto skin = std::move(built.value());
    REQUIRE(skin->bindGpuMesh(graphics, mesh));
    const float livePalette[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 99, 0, 0, 1};
    REQUIRE(mesh->setSkinPalette(livePalette, 1).ok());
    auto planned = graphics::planInspectionSamples(0, 1, 2, 1, graphics::InspectionLoop::Once);
    REQUIRE(planned.ok());
    std::vector<animation::AnimInspectionTime> times;
    for (const auto& time : planned.value()) times.push_back({time.timelineTime, time.clipTime});
    auto sampled = animation::AnimInspectionBuilder(times).build(clip, skeleton, *skin);
    REQUIRE(sampled.ok());
    skin.reset();
    auto* entity = graphics::Renderable3D::create();
    entity->setMesh(mesh);
    std::vector<FrameCaptureBuffer> images;
    for (size_t frame = 0; frame < sampled.value().size(); ++frame) {
        EntityViewRequest request{entity->getEntityId(), entity->getEntityGeneration(), 64};
        request.skinPalettes = {sampled.value()[frame].palette};
        auto queued          = inspection->request(std::move(request));
        REQUIRE(queued.ok());
        EntityViewSnapshot result;
        for (int attempt = 0; attempt < 100; ++attempt) {
            graphics->present();
            auto status = inspection->snapshot(queued.value());
            REQUIRE(status.ok());
            result = std::move(status.value());
            if (result.capture.phase == FrameCapturePhase::Ready || result.capture.phase == FrameCapturePhase::Failed)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(result.capture.phase == FrameCapturePhase::Ready);
        ASSERT(result.bounds[0] == float(frame) * 2.f);
        ASSERT(result.bounds[3] == float(frame) * 2.f + 2.f);
        ASSERT(mesh->skinPalette()[12] == 99.f);
        REQUIRE(result.capture.buffers.size() == 1);
        images.push_back(result.capture.buffers[0]);
        REQUIRE(inspection->release(queued.value()).ok());
    }
    REQUIRE(images.size() == 3);
    ASSERT(*images[0].bytes != *images[2].bytes);
    dev::FrameContactSheetOptions options;
    options.columns   = 3;
    options.cellWidth = options.cellHeight = 128;
    auto sheet                             = dev::frameContactSheet(images, options);
    REQUIRE(sheet.ok());
    ASSERT(sheet.value().width == 384);
    ASSERT(sheet.value().height == 128);
    ASSERT(clip.getLoop());
    ecs::DestroyEntity(entity);
    ecs::DestroyEntity(camera);
}
