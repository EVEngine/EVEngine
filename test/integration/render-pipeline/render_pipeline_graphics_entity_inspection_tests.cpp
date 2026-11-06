// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"graphics","default":"render-window"}
#include <chrono>
#include <thread>
#include "common/Capability.h"
#include "common/EntityInspection.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.entityInspection.frozenGeometryAndCancellation") {
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
    EntityViewRequest request{entity->getEntityId(), entity->getEntityGeneration(), 64};
    auto              invalid = request;
    ++invalid.generation;
    auto stale = inspection->request(invalid);
    REQUIRE(!stale.ok());
    auto queued = inspection->request(request);
    REQUIRE(queued.ok());
    const auto token = queued.value();
    auto       busy  = inspection->request(request);
    REQUIRE(!busy.ok());
    // Destroy both source lifetimes before any inspection GPU commands are recorded.
    ecs::DestroyEntity(entity);
    REQUIRE(graphics->releaseMesh(mesh));
    delete mesh;
    camera->setEye(400, 300, 600);
    EntityViewSnapshot result;
    for (int i = 0; i < 100; ++i) {
        graphics->present();
        auto status = inspection->snapshot(token);
        REQUIRE(status.ok());
        result = std::move(status.value());
        if (result.capture.phase == FrameCapturePhase::Ready || result.capture.phase == FrameCapturePhase::Failed)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(result.capture.phase == FrameCapturePhase::Ready);
    REQUIRE(result.capture.buffers.size() == 1);
    const auto pixels = result.capture.buffers[0].bytes;
    REQUIRE(pixels != nullptr);
    ASSERT(result.bounds[0] == -1.f);
    ASSERT(result.bounds[4] == 2.f);
    ASSERT(camera->getEyeX() == 400.f);
    std::array<uint32_t, 4> widths{}, heights{};
    for (uint32_t panel = 0; panel < 4; ++panel) {
        size_t   colored = 0;
        uint32_t minX = 64, minY = 64, maxX = 0, maxY = 0;
        for (uint32_t y = 0; y < 64; ++y)
            for (uint32_t x = 0; x < 64; ++x) {
                const size_t offset = ((panel / 2 * 64 + y) * 128 + panel % 2 * 64 + x) * 4;
                if ((*pixels)[offset] > 30 || (*pixels)[offset + 1] > 30 || (*pixels)[offset + 2] > 30) {
                    ++colored;
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
        ASSERT(colored > 20);
        widths[panel]  = maxX - minX + 1;
        heights[panel] = maxY - minY + 1;
    }
    ASSERT(heights[0] == heights[1]);
    ASSERT(widths[0] == widths[2]);
    ASSERT(widths[1] == heights[2]);
    ASSERT(widths[1] < widths[0]);
    auto released = inspection->release(token);
    REQUIRE(released.ok());
    auto after = inspection->snapshot(token);
    REQUIRE(after.ok());
    ASSERT(after.value().capture.buffers.empty());
    ASSERT(pixels->size() == 128 * 128 * 4);
    auto* secondMesh   = graphics->newMeshFromArrays(positions, normals, uv, 8, indices, 36);
    auto* secondEntity = graphics::Renderable3D::create();
    secondEntity->setMesh(secondMesh);
    request.entityId   = secondEntity->getEntityId();
    request.generation = secondEntity->getEntityGeneration();
    auto cancelled     = inspection->request(request);
    REQUIRE(cancelled.ok());
    graphics->present();
    auto cancel = inspection->release(cancelled.value());
    REQUIRE(cancel.ok());
    for (int i = 0; i < 5; ++i) graphics->present();
    auto finalState = inspection->snapshot(cancelled.value());
    REQUIRE(finalState.ok());
    ASSERT(finalState.value().capture.phase != FrameCapturePhase::Ready);
    ecs::DestroyEntity(secondEntity);
    ecs::DestroyEntity(camera);
}

TEST_CASE("graphics.entityInspection.skinPoseMatchesBakedGeometry") {
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
    camera->setEye(4, 3, 8);
    camera->setTarget(3, 1, 0);
    const float    positions[]    = {0, 0, 0, 2, 0, 0, 0, 2, 0};
    const float    baked[]        = {3, 0, 0, 3.5f, 0, -1.5f, 3, 2, 0};
    const float    bakedNormals[] = {.75f, 0, .25f, .75f, 0, .25f, .75f, 0, .25f};
    const float    normals[]      = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[6]{};
    const uint32_t indices[] = {0, 1, 2};
    uint16_t       joints[]  = {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0};
    float          weights[] = {1, 3, 0, 0, 1, 3, 0, 0, 1, 3, 0, 0};
    float palette[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 4, 0, 0, 1};
    auto* mesh      = graphics->newMeshFromArrays(positions, normals, uv, 3, indices, 3);
    auto* entity    = graphics::Renderable3D::create();
    entity->setMesh(mesh);
    REQUIRE(graphics->setMeshSkinningData(mesh, joints, weights, 3));
    EntityViewRequest request{entity->getEntityId(), entity->getEntityGeneration(), 64};
    auto              empty = inspection->request(request);
    REQUIRE(!empty.ok());
    palette[19] = .5f;
    REQUIRE(mesh->setSkinPalette(palette, 2).ok());
    REQUIRE(!inspection->request(request).ok());
    palette[19] = 0;
    REQUIRE(mesh->setSkinPalette(palette, 2).ok());
    joints[3] = 2;  // Even a zero-weight lane must be in range for the shader.
    REQUIRE(graphics->setMeshSkinningData(mesh, joints, weights, 3));
    REQUIRE(!inspection->request(request).ok());
    joints[3]  = 0;
    weights[0] = -1;
    REQUIRE(graphics->setMeshSkinningData(mesh, joints, weights, 3));
    REQUIRE(!inspection->request(request).ok());
    weights[0] = weights[1] = 0;
    REQUIRE(graphics->setMeshSkinningData(mesh, joints, weights, 3));
    REQUIRE(!inspection->request(request).ok());
    weights[0] = 1;
    weights[1] = 3;
    REQUIRE(graphics->setMeshSkinningData(mesh, joints, weights, 3));
    request.skinPalettes = {std::vector<float>(palette, palette + 32)};
    palette[28]          = 40;
    REQUIRE(mesh->setSkinPalette(palette, 2).ok());
    auto queued = inspection->request(request);
    REQUIRE(queued.ok());
    ASSERT(mesh->skinPalette()[28] == 40.f);
    // The copied pose must survive a new animation pose and destruction of its owner.
    palette[28] = 400;
    REQUIRE(mesh->setSkinPalette(palette, 2).ok());
    ecs::DestroyEntity(entity);
    REQUIRE(graphics->releaseMesh(mesh));
    delete mesh;
    auto awaitImage = [&](const std::string& token) {
        EntityViewSnapshot result;
        for (int i = 0; i < 100; ++i) {
            graphics->present();
            auto status = inspection->snapshot(token);
            REQUIRE(status.ok());
            result = std::move(status.value());
            if (result.capture.phase == FrameCapturePhase::Ready || result.capture.phase == FrameCapturePhase::Failed)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(result.capture.phase == FrameCapturePhase::Ready);
        REQUIRE(result.capture.buffers.size() == 1);
        return result;
    };
    const auto posed = awaitImage(queued.value());
    ASSERT(posed.request.skinPalettes.empty());
    ASSERT(posed.bounds[0] == 3.f);
    ASSERT(posed.bounds[3] == 3.5f);
    ASSERT(posed.bounds[2] == -1.5f);
    REQUIRE(inspection->release(queued.value()).ok());
    auto* bakedMesh   = graphics->newMeshFromArrays(baked, bakedNormals, uv, 3, indices, 3);
    auto* bakedEntity = graphics::Renderable3D::create();
    bakedEntity->setMesh(bakedMesh);
    request.entityId   = bakedEntity->getEntityId();
    request.generation = bakedEntity->getEntityGeneration();
    request.skinPalettes.clear();
    auto comparison = inspection->request(request);
    REQUIRE(comparison.ok());
    const auto expected = awaitImage(comparison.value());
    ASSERT(posed.bounds == expected.bounds);
    ASSERT(*posed.capture.buffers[0].bytes == *expected.capture.buffers[0].bytes);
    REQUIRE(inspection->release(comparison.value()).ok());
    ecs::DestroyEntity(bakedEntity);
    ecs::DestroyEntity(camera);
}
