#include "common/config.h"
#if !defined(EVENGINE_WEBGPU)
#include "common/Profile.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/vulkan/FrameGraphJobs.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <chrono>
#include <cstdio>
#include <memory>

TEST_CASE("graphics.frameRecording.inlineBudgetAndPeriodicReprobe") {
    using namespace eve::graphics::vulkan;
    FrameGraphRecordingState state;
    REQUIRE(!state.isParallelWorthwhile());
    state.serialCost = std::chrono::microseconds(100);
    REQUIRE(!state.isParallelWorthwhile());
    state.serialCost = std::chrono::milliseconds(2);
    REQUIRE(state.isParallelWorthwhile());
    state.parallelFrames = 60;
    REQUIRE(!state.isParallelWorthwhile());
    state = {};
    REQUIRE(!state.isParallelWorthwhile());
    auto executor = jobSystemPassExecutor(nullptr);
    int  next     = 0;
    executor({0, 1, 2}, [&](uint32_t index) {
        REQUIRE_EQ(index, uint32_t(next));
        ++next;
    });
    REQUIRE_EQ(next, 3);
}

TEST_CASE("graphics.frameRecording.fxaaSlotReuseResizeAndPersistentCanvas") {
    using namespace eve::graphics;
    auto                       *win = eve::window::Window::create();
    auto                       *gfx = Graphics::create();
    eve::window::WindowSettings settings;
    settings.width    = 128;
    settings.height   = 128;
    settings.high_dpi = false;
    settings.vsync    = 0;
    REQUIRE(win->setWindowSettings(settings));
    struct Cleanup {
        eve::window::Window *win;
        ~Cleanup() {
            eve::prof::Profiler::setEnabled(false);
            win->close();
        }
    } cleanup{win};
    // Keep the window visible: resizing a hidden Cocoa window can leave its
    // Vulkan drawable at 1x1 even though SDL reports the requested logical size.
    gfx->setScreenReadbackEnabled(true);
    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 0.f, 4.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->data()->ambientR = camera->data()->ambientG = camera->data()->ambientB = 0.8f;
    auto *entity                                                                   = Renderable3D::create();
    entity->setMesh(gfx->newMeshCube(1.f));
    const uint8_t red[4]       = {255, 0, 0, 255};
    const uint8_t green[4]     = {0, 255, 0, 255};
    auto         *redTexture   = gfx->newTexture(1, 1, red);
    auto         *greenTexture = gfx->newTexture(1, 1, green);
    for (int frame = 0; frame < 12; ++frame) {
        if (frame == 6) {
            settings.width  = 160;
            settings.height = 96;
            auto *native    = static_cast<SDL_Window *>(win->getHandle());
            SDL_SetWindowSize(native, settings.width, settings.height);
            // Cocoa applies the content-view / Metal-layer resize while pumping
            // native events. A logical SDL size alone does not prove the Vulkan
            // drawable is ready. Keep the first-frame assertion below, but only
            // render after the real drawable has reached the requested 1x size.
            int        logicalWidth = 0, logicalHeight = 0, pixelWidth = 0, pixelHeight = 0;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            do {
                SDL_PumpEvents();
                SDL_GetWindowSize(native, &logicalWidth, &logicalHeight);
                SDL_Vulkan_GetDrawableSize(native, &pixelWidth, &pixelHeight);
                if (logicalWidth == settings.width && logicalHeight == settings.height &&
                    pixelWidth == settings.width && pixelHeight == settings.height)
                    break;
                SDL_Delay(10);
            } while (std::chrono::steady_clock::now() < deadline);
            REQUIRE_EQ(logicalWidth, settings.width);
            REQUIRE_EQ(logicalHeight, settings.height);
            REQUIRE_EQ(pixelWidth, settings.width);
            REQUIRE_EQ(pixelHeight, settings.height);
            gfx->setViewportSize(logicalWidth, logicalHeight, pixelWidth, pixelHeight);
        }
        entity->setTexture(frame % 2 ? greenTexture : redTexture);
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        std::unique_ptr<eve::image::ImageData> image(gfx->newImageData());
        REQUIRE(image != nullptr);
        REQUIRE_EQ(image->getSize(), size_t(gfx->getWidth() * gfx->getHeight() * 4));
        const auto  *pixels = static_cast<const uint8_t *>(image->getData());
        const size_t center = size_t((gfx->getHeight() / 2) * gfx->getWidth() + gfx->getWidth() / 2) * 4;
        REQUIRE_GT(pixels[center + (frame % 2 ? 1 : 0)], pixels[center + (frame % 2 ? 0 : 1)]);
    }
    // Steady-state default post path must no longer drain every in-flight frame.
    eve::prof::Profiler::setEnabled(true);
    RenderSystem3D::render(*gfx);
    RenderSystem::render(*gfx);
    (void)eve::prof::Profiler::frameMark();
    std::fprintf(stderr, "%s\n", eve::prof::Profiler::textReport().c_str());
    bool sawFrameLocal = false;
    for (const auto &sample : eve::prof::Profiler::lastFrame()) {
        REQUIRE_NE(sample.name, std::string("GpuResources.WaitAllFrames"));
        if (sample.name == "Offscreen.FrameLocalRecord") sawFrameLocal = true;
    }
    REQUIRE(sawFrameLocal);
    eve::prof::Profiler::setEnabled(false);
    // A public canvas still preserves pixels across frames and partial updates.
    auto *canvas   = gfx->newCanvas(8, 8);
    auto *identity = canvas->getTexture();
    gfx->setCanvas(canvas);
    gfx->drawSolidRectRGBA(0, 0, 8, 8, 1, 0, 0, 1);
    gfx->setCanvas(nullptr);
    RenderSystem::render(*gfx);
    gfx->setCanvas(canvas);
    gfx->drawSolidRectRGBA(0, 0, 2, 2, 0, 1, 0, 1);
    gfx->setCanvas(nullptr);
    RenderSystem::render(*gfx);
    REQUIRE_EQ(canvas->getTexture(), identity);
    REQUIRE_GT(canvas->getPixel(6, 6).r, 0.9f);
    REQUIRE_GT(canvas->getPixel(0, 0).g, 0.9f);
    // History-dependent effects keep their existing shared-image synchronization.
    gfx->getRenderControl()->enable("taa");
    gfx->setSceneAutoExposure(true, -4.f, 4.f);
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        std::unique_ptr<eve::image::ImageData> image(gfx->newImageData());
        REQUIRE(image != nullptr);
        REQUIRE_EQ(image->getSize(), size_t(gfx->getWidth() * gfx->getHeight() * 4));
    }
}

#endif
