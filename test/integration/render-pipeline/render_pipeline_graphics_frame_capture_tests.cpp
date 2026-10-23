#include "common/config.h"
#ifndef EVENGINE_WEBGPU
#include "graphics/vulkan/Graphics.h"
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include "common/FrameCaptureImage.h"
#include "graphics/Graphics.h"
#include "graphics/PbrSurface.h"
#include "graphics/Shadow.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.frameCapture.sameFrameNativeAttachmentsAndFrozenSnapshots") {
    auto*                       window = eve::window::Window::create();
    auto*                       gfx    = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    auto&                    capture = gfx->frameCaptures();
    eve::FrameCaptureRequest request;
    request.timeout     = std::chrono::milliseconds(30000);
    request.attachments = {
        eve::FrameAttachment::Final,    eve::FrameAttachment::Depth,    eve::FrameAttachment::HwDepth,
        eve::FrameAttachment::Normal,   eve::FrameAttachment::Albedo,   eve::FrameAttachment::PbrParams,
        eve::FrameAttachment::Emissive, eve::FrameAttachment::Velocity, eve::FrameAttachment::Shadow0,
        eve::FrameAttachment::Shadow1,  eve::FrameAttachment::Shadow2};
    if (capture.capabilities().backend != "vulkan") {
        auto unsupported = capture.request(request);
        REQUIRE(!unsupported.ok());
        return;
    }
    const float    positions[] = {-1, -1, 0.5f, 1, -1, 0.5f, 1, 1, 0.5f, -1, 1, 0.5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]        = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 2, 0, 3};
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t white[] = {255, 255, 255, 255};
    auto*         texture = gfx->newTexture(1, 1, white);
    REQUIRE(texture != nullptr);
    uint64_t                    previousFrame = 0;
    eve::graphics::ShadowUpload shadows;
    shadows.active = true;
    for (auto& matrix : shadows.ubo.lightVP) matrix = glm::mat4(1.f);
    gfx->setMesh3DShadows(shadows);
    for (int frame = 0; frame < 4; ++frame) {
        const int color = frame % 2;
#ifndef EVENGINE_WEBGPU
        auto*      backend      = static_cast<eve::graphics::vulkan::Graphics*>(gfx);
        const auto producerSlot = backend->getSwapchain().current_frame;
#endif
        auto queued = capture.request(request);
        REQUIRE(queued.ok());
        const auto  token = queued.value();
        const float red   = color == 0 ? 1.f : 0.f;
        const float green = color == 1 ? 1.f : 0.f;
        gfx->setBackgroundColor(eve::graphics::Color(red, green, 0.f, 1.f));
        gfx->clear(eve::graphics::Color(red, green, 0.f, 1.f), std::nullopt, std::nullopt);
        for (int cascade = 0; cascade < 3; ++cascade) {
            gfx->beginShadowPass(cascade);
            gfx->drawMeshShadow(mesh, glm::mat4(1.f));
            gfx->endShadowPass();
        }
        gfx->beginGBufferPass(64, 64);
        gfx->drawMeshGBuffer(mesh, glm::mat4(1.f), glm::mat4(1.f), 0.1f, 100.f, texture, red, green, 0.f, 0.25f, -0.5f,
                             0.25f, 0.75f);
        gfx->endGBufferPass();
        gfx->setMesh3DViewProj(glm::mat4(1.f));
        gfx->setMesh3DView(glm::mat4(1.f));
        gfx->begin3DFrame();
        eve::graphics::PbrSurface surface;
        auto                      material = gfx->setMesh3DPbrSurface(&surface);
        REQUIRE(material.ok());
        glm::mat4 corner(1.f);
        corner[0][0] = corner[1][1] = 0.1f;
        corner[3][0] = corner[3][1] = -0.8f;
        gfx->drawMesh(mesh, corner, texture, eve::graphics::Color(1.f));
        auto cleared = gfx->setMesh3DPbrSurface(nullptr);
        REQUIRE(cleared.ok());
        gfx->present();
        eve::FrameCaptureSnapshot snapshot;
        for (int attempt = 0; attempt < 20; ++attempt) {
            auto state = capture.snapshot(token);
            REQUIRE(state.ok());
            snapshot = state.value();
            if (snapshot.phase == eve::FrameCapturePhase::Ready || snapshot.phase == eve::FrameCapturePhase::Failed)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            gfx->present();
        }
        REQUIRE(snapshot.phase == eve::FrameCapturePhase::Ready);
        REQUIRE(snapshot.frameId > previousFrame);
        previousFrame = snapshot.frameId;
        REQUIRE(snapshot.buffers.size() == request.attachments.size());
        auto find = [&](eve::FrameAttachment a) -> const eve::FrameCaptureBuffer& {
            return *std::find_if(snapshot.buffers.begin(), snapshot.buffers.end(),
                                 [&](const auto& b) { return b.attachment == a; });
        };
        for (const auto& buffer : snapshot.buffers) {
            REQUIRE(buffer.bytes != nullptr);
            REQUIRE(buffer.producerFrameId == snapshot.frameId);
        }
        auto albedo = eve::frameCapturePixel(find(eve::FrameAttachment::Albedo), 32, 32);
        REQUIRE(albedo.ok());
        REQUIRE(albedo.value().stored[static_cast<size_t>(color)] > 247);
        REQUIRE(albedo.value().stored[static_cast<size_t>(1 - color)] < 8);
        auto pbr = eve::frameCapturePixel(find(eve::FrameAttachment::PbrParams), 32, 32);
        REQUIRE(pbr.ok());
        REQUIRE((pbr.value().decoded[0] > 0.7 && pbr.value().decoded[0] < 0.8));
        REQUIRE((pbr.value().decoded[1] > 0.2 && pbr.value().decoded[1] < 0.3));
        auto final = eve::frameCapturePixel(find(eve::FrameAttachment::Final), 80, 60);
        REQUIRE(final.ok());
        REQUIRE(final.value().decoded[static_cast<size_t>(color)] >
                final.value().decoded[static_cast<size_t>(1 - color)] + 0.1);
        for (auto shadow :
             {eve::FrameAttachment::Shadow0, eve::FrameAttachment::Shadow1, eve::FrameAttachment::Shadow2}) {
            auto center = eve::frameCapturePixel(find(shadow), 1024, 1024);
            REQUIRE(center.ok());
            REQUIRE(std::abs(center.value().stored[0] - 0.5) < 1e-6);
        }
        auto depth = eve::frameCapturePixel(find(eve::FrameAttachment::HwDepth), 32, 32);
        REQUIRE(depth.ok());
        REQUIRE(std::abs(depth.value().stored[0] - 0.5) < 1e-6);
        auto velocity = eve::frameCapturePixel(find(eve::FrameAttachment::Velocity), 32, 32);
        REQUIRE(velocity.ok());
        REQUIRE(std::abs(velocity.value().decoded[1] - 0.25) < 0.01);
        REQUIRE(std::abs(velocity.value().decoded[2] + 0.5) < 0.01);
        REQUIRE(find(eve::FrameAttachment::Velocity).bytes == find(eve::FrameAttachment::Depth).bytes);
        gfx->clear(eve::graphics::Color(0.f, 0.f, 1.f, 1.f), std::nullopt, std::nullopt);
        gfx->present();
#ifndef EVENGINE_WEBGPU
        // Revisit precisely the producer slot after empty frames. A slot number
        // must never make a later frame reuse this frame's recorded graph.
        for (int idle = 0; idle < 3 && backend->getSwapchain().current_frame != producerSlot; ++idle) gfx->present();
        REQUIRE_EQ(backend->getSwapchain().current_frame, producerSlot);
#endif
        auto still = eve::frameCapturePixel(find(eve::FrameAttachment::Albedo), 32, 32);
        REQUIRE(still.ok());
        REQUIRE(still.value().stored[static_cast<size_t>(color)] > 247);
        auto released = capture.release(token);
        REQUIRE(released.ok());
    }
}
