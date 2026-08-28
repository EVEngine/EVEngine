#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <string>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;
using namespace eve::ui;

/**
 * Real window + Vulkan: world sprites (RenderSystem) and ImGui UI overlay in one present.
 * Frame order: UI NewFrame/widgets → RenderSystem (draws + present with overlay) → dispatch.
 */
TEST_CASE("UI.smoke.RenderSystemSameFrameVulkanOverlay") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    auto *ui = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);

    eve::window::WindowSettings s;
    s.width = 640;
    s.height = 480;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    REQUIRE_GT(gfx->getWidth(), 0);
    REQUIRE_GT(gfx->getPixelWidth(), 0);

    REQUIRE(ui->initBackend());
    CHECK(ui->isBackendReady());
    const bool overlayHooked = gfx->getPresentOverlay() != nullptr && gfx->getPresentOverlayUser() != nullptr;
    CHECK(overlayHooked);

    gfx->setBackgroundColor(Color(0.10f, 0.12f, 0.18f, 1.0f));
    gfx->setScreenReadbackEnabled(true);

    auto *cam = Camera2D::createCamera();
    cam->data()->r = 0.10f;
    cam->data()->g = 0.12f;
    cam->data()->b = 0.18f;

    auto *solid = Renderable2D::create();
    solid->transform()->x = 80;
    solid->transform()->y = 100;
    solid->sprite()->width = 160;
    solid->sprite()->height = 100;
    solid->sprite()->r = 0.2f;
    solid->sprite()->g = 0.85f;
    solid->sprite()->b = 0.35f;
    solid->sprite()->visible = true;

    const UIHostHandle smokeHost = ui->mountAs("smoke", window("Overlay",
                                                               {
                                                                   text("UI+RenderSystem", "label"),
                                                                   button("Ping", "ping"),
                                                               },
                                                               "root"));
    REQUIRE(UIHost::resolve(smokeHost).has_value());

    int framesOk = 0;
    for (int frame = 0; frame < 30; ++frame) {
        solid->transform()->x = 80.0f + float(frame) * 2.0f;

        // ImGui frame must be built before present (RenderSystem ends with present).
        ui->beginFrameAndRender();
        RenderSystem::render(*gfx);
        ui->dispatchEvents();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ui->processEvent(&e);
            if (e.type == SDL_QUIT) break;
        }

        // After present+readback, swapchain content should be readable.
        Color p = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        CHECK(std::isfinite(p.r));
        CHECK(std::isfinite(p.g));
        CHECK(std::isfinite(p.b));
        ++framesOk;

        SDL_Delay(8);
    }

    CHECK_EQ(framesOk, 30);
    CHECK(UIHost::resolve(UISystem::findHost("smoke")).has_value());
    auto currentHost = UIHost::resolve(ui->current());
    REQUIRE(currentHost.has_value());
    CHECK(currentHost->get().findById("label").has_value());

    gfx->setScreenReadbackEnabled(false);
    win->close();
}

TEST_CASE("UI.smoke.panelGalleryPreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    auto *ui = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);

    eve::window::WindowSettings s;
    s.width = 720;
    s.height = 480;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    REQUIRE(ui->initBackend());
    gfx->setScreenReadbackEnabled(true);

    gfx->setBackgroundColor(Color(0.09f, 0.10f, 0.14f, 1.0f));

    auto *cam = Camera2D::createCamera();
    cam->data()->r = 0.09f;
    cam->data()->g = 0.10f;
    cam->data()->b = 0.14f;

    // Soft backdrop sprites so the world + overlay composition is obvious.
    auto *bgA = Renderable2D::create();
    bgA->transform()->x = 40;
    bgA->transform()->y = 60;
    bgA->sprite()->width = 220;
    bgA->sprite()->height = 140;
    bgA->sprite()->r = 0.25f;
    bgA->sprite()->g = 0.45f;
    bgA->sprite()->b = 0.75f;
    bgA->sprite()->visible = true;

    auto *bgB = Renderable2D::create();
    bgB->transform()->x = 420;
    bgB->transform()->y = 220;
    bgB->sprite()->width = 180;
    bgB->sprite()->height = 160;
    bgB->sprite()->r = 0.75f;
    bgB->sprite()->g = 0.35f;
    bgB->sprite()->b = 0.45f;
    bgB->sprite()->visible = true;

    float volume = 0.55f;
    bool muted = false;
    int clicks = 0;

    const UIHostHandle galleryHost =
        ui->mountAs("gallery", window("EVEngine Preview",
                                      {
                                          text("UI overlay + world sprites", "title"),
                                          separator("sep0"),
                                          checkbox("Mute", muted, "mute", [&](bool v) { muted = v; }),
                                          slider("Volume", volume, 0.f, 1.f, "vol", [&](float v) { volume = v; }),
                                          progress(volume, "prog", "level"),
                                          button("Ping", "ping", [&]() { ++clicks; }),
                                          text("clicks: 0", "clicks"),
                                      },
                                      "root"));
    REQUIRE(UIHost::resolve(galleryHost).has_value());

    for (int frame = 0; frame < 90; ++frame) {
        bgA->transform()->x = 40.f + float(frame) * 1.2f;
        bgB->transform()->y = 220.f + std::sin(float(frame) * 0.08f) * 24.f;
        volume = 0.35f + 0.3f * (0.5f + 0.5f * std::sin(float(frame) * 0.06f));

        const UIHostHandle remounted = ui->remountAs(
            "gallery",
            window("EVEngine Preview",
                   {
                       text("UI overlay + world sprites", "title"),
                       separator("sep0"),
                       checkbox("Mute", muted, "mute", [&](bool v) { muted = v; }),
                       slider("Volume", volume, 0.f, 1.f, "vol", [&](float v) { volume = v; }),
                       progress(volume, "prog", muted ? "muted" : "level"),
                       button("Ping", "ping", [&]() { ++clicks; }),
                       text("clicks: " + std::to_string(clicks) + "  vol: " + std::to_string(int(volume * 100.f)),
                            "clicks"),
                   },
                   "root"));
        REQUIRE(UIHost::resolve(remounted).has_value());

        ui->beginFrameAndRender();
        RenderSystem::render(*gfx);
        ui->dispatchEvents();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ui->processEvent(&e);
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK(UIHost::resolve(UISystem::findHost("gallery")).has_value());
    bgA->sprite()->visible = false;
    bgB->sprite()->visible = false;
    win->close();
}

TEST_CASE("UI.smoke.viewportEmbeddedRenderTarget") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    auto *ui = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);

    eve::window::WindowSettings s;
    s.width = 800;
    s.height = 600;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    REQUIRE(ui->initBackend());
    gfx->setScreenReadbackEnabled(true);

    const UIHostHandle viewportHost =
        ui->mountAs("vpsmoke", window("Viewport", {viewport("vp", 420.f, 260.f)}, "root"));
    REQUIRE(UIHost::resolve(viewportHost).has_value());

    for (int frame = 0; frame < 4; ++frame) {
        // UI pass first: ensures the offscreen canvas exists at the widget rect.
        ui->beginFrameAndRender();
        Canvas *canvas = ui->viewportCanvas("vp");
        REQUIRE(canvas != nullptr);
        REQUIRE(canvas->getWidth() > 0);

        // 2D: draw into the viewport canvas with the immediate-mode path.
        gfx->setCanvas(canvas);
        gfx->clearScreen();
        gfx->drawSolidRectRGBA(10.f, 10.f, 120.f, 80.f, 0.2f, 0.6f, 0.9f, 1.f);
        gfx->setCanvas(nullptr);

        RenderSystem::render(*gfx);  // present + UI overlay
        ui->dispatchEvents();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ui->processEvent(&e);
            if (e.type == SDL_QUIT) break;
        }
    }

    auto vs = UISystem::viewportState("vpsmoke", "vp");
    REQUIRE(vs.has_value());
    ViewportState &viewportState = vs->get();
    CHECK(viewportState.canvas != nullptr);
    CHECK(viewportState.textureId != 0);
    CHECK(viewportState.width == 420);
    CHECK(viewportState.height == 260);

    // 3D: render a cube scene into the same canvas (preview-quality pass).
    auto *cam = Camera3D::createCamera();
    cam->setEye(3.f, 2.f, 4.f);
    cam->setTarget(0.f, 0.f, 0.f);
    auto *cube = Renderable3D::create();
    cube->setMesh(gfx->newMeshCube(1.f));
    cube->setVisible(true);
    cube->setTint(1.f, 0.f, 0.f, 1.f);
    auto *previewMaterial = new Material();
    previewMaterial->setShadingModel("unlit");
    previewMaterial->setTint(0.f, 1.f, 0.f, 1.f);
    cube->setMaterial(previewMaterial);
    gfx->setDirectionalLight(-0.4f, 0.9f, 0.3f, 1.8f, 1.6f, 1.3f);
    gfx->renderScene3DToCanvas(viewportState.canvas, cam);
    CHECK(viewportState.canvas->getTexture() != nullptr);
    const Color previewCenter = viewportState.canvas->getPixel(viewportState.canvas->getWidth() / 2,
                                                               viewportState.canvas->getHeight() / 2);
    CHECK_GT(previewCenter.g, previewCenter.r);

    // Composite the viewport through the real UI target. Opaque viewport draws
    // must not leak the mesh shader's depth-in-alpha channel into the final UI
    // resolve or the preview becomes an almost-black silhouette.
    ui->beginFrameAndRender();
    RenderSystem::render(*gfx);
    float maxGreen = 0.f;
    for (int y = 0; y < gfx->getHeight(); y += 4)
        for (int x = 0; x < gfx->getWidth(); x += 4)
            maxGreen = std::max(maxGreen, gfx->getPixel(x, y).g);
    CHECK_GT(maxGreen, 0.5f);

    gfx->setScreenReadbackEnabled(false);
    win->close();
}
