#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "window/Window.h"

using namespace eve::graphics;

namespace {

void openGfxWindow(eve::window::Window *&win, Graphics *&gfx, int w = 320, int h = 240) {
    win = eve::window::Window::create();
    gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = w;
    s.height = h;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
}

Texture *makeChecker(Graphics *gfx, int w, int h) {
    std::vector<uint8_t> px(size_t(w * h * 4));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool on = ((x / 4) + (y / 4)) & 1;
            const size_t i = size_t((y * w + x) * 4);
            px[i + 0] = on ? 255 : 16;
            px[i + 1] = on ? 255 : 16;
            px[i + 2] = on ? 255 : 16;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(w, h, px.data());
}

/** Hold a textured canvas / texture on the swapchain briefly. */
void previewTexture(Graphics *gfx, Texture *tex, int ms = 400) {
    gfx->setBackgroundColorRGBA(0.05f, 0.05f, 0.08f, 1.f);
    const int frames = (ms >= 16) ? (ms / 16) : 1;
    for (int i = 0; i < frames; ++i) {
        gfx->clearScreen();
        gfx->drawTexturedRectRGBA(tex, 0, 0, float(gfx->getWidth()), float(gfx->getHeight()), 1, 1, 1,
                                  1);
        gfx->present();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
        }
        SDL_Delay(16);
    }
}

}  // namespace

TEST_CASE("aa.qualityAndModes") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    AntiAliasing *raw = gfx->newAntiAliasing();
    REQUIRE(raw != nullptr);
    REQUIRE(raw->getFxaaShader() != nullptr);
    REQUIRE(raw->getSmaaShader() != nullptr);
    REQUIRE(raw->getSsaaShader() != nullptr);
    REQUIRE(raw->getNfaaShader() != nullptr);
    std::unique_ptr<AntiAliasing> aa(raw);

    CHECK(aa->getMode() == "fxaa");
    CHECK(aa->getQuality() == "medium");
    CHECK(aa->getShader() == aa->getFxaaShader());

    aa->setMode("smaa");
    CHECK(aa->getMode() == "smaa");
    CHECK(aa->getShader() == aa->getSmaaShader());
    CHECK(aa->hasParam("threshold"));
    CHECK(aa->hasParam("maxSearch"));

    aa->setMode("nfaa");
    CHECK(aa->getShader() == aa->getNfaaShader());
    CHECK(aa->hasParam("strength"));

    aa->setMode("ssaa");
    CHECK(aa->getShader() == aa->getSsaaShader());
    CHECK(aa->suggestScale() == 2.f);
    CHECK(aa->resolutionFor(100) == 200);

    aa->setQuality("high");
    CHECK(aa->getQuality() == "high");
    CHECK(aa->suggestScale() == 4.f);
    CHECK(aa->resolutionFor(100) == 400);
    CHECK(aa->getFloat("scale") == 4.f);

    aa->setQuality("low");
    CHECK(aa->suggestScale() == 2.f);
    CHECK(aa->getFloat("kernel") == 0.f);

    aa->setMode("unknown");
    CHECK(aa->getMode() == "fxaa");
    aa->setQuality("bogus");
    CHECK(aa->getQuality() == "medium");
}

TEST_CASE("aa.fxaaApplyToCanvas") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 256, 192);

    std::unique_ptr<AntiAliasing> aa(gfx->newAntiAliasing());
    aa->setMode("fxaa");
    aa->setQuality("medium");

    Texture *src = makeChecker(gfx, 128, 96);
    REQUIRE(src != nullptr);
    Canvas *dest = gfx->newCanvas(128, 96);
    REQUIRE(dest != nullptr);
    REQUIRE(dest->getTexture() != nullptr);

    gfx->setBackgroundColorRGBA(0, 0, 0, 1);
    aa->applyTo(gfx, src, dest);
    previewTexture(gfx, dest->getTexture(), 300);

    // Flat region should stay near black/white extremes after AA (no crash / blackout).
    gfx->setCanvas(dest);
    // Just ensure we can draw the result to screen without throwing.
    gfx->setCanvas(nullptr);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(dest->getTexture(), 0, 0, float(gfx->getWidth()),
                              float(gfx->getHeight()), 1, 1, 1, 1);
    gfx->present();
}

TEST_CASE("aa.allModesSmoke") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);

    std::unique_ptr<AntiAliasing> aa(gfx->newAntiAliasing());
    Texture *src = makeChecker(gfx, 160, 120);
    Canvas *dest = gfx->newCanvas(160, 120);
    REQUIRE(src != nullptr);
    REQUIRE(dest != nullptr);

    const char *modes[] = {"fxaa", "smaa", "ssaa", "nfaa"};
    for (const char *mode : modes) {
        aa->setMode(mode);
        aa->setQuality("medium");
        aa->applyTo(gfx, src, dest);
        CHECK(aa->getMode() == mode);
        previewTexture(gfx, dest->getTexture(), 200);
    }
}

TEST_CASE("aa.ssaaFromHiResCanvas") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 256, 192);

    std::unique_ptr<AntiAliasing> aa(gfx->newAntiAliasing());
    aa->setMode("ssaa");
    aa->setQuality("medium");

    const int dw = 128;
    const int dh = 96;
    const int sw = aa->resolutionFor(dw);
    const int sh = aa->resolutionFor(dh);
    CHECK(sw == 256);
    CHECK(sh == 192);

    Canvas *hi = gfx->newCanvas(sw, sh);
    Canvas *lo = gfx->newCanvas(dw, dh);
    REQUIRE(hi != nullptr);
    REQUIRE(lo != nullptr);

    // Draw a high-contrast diagonal into the supersampled canvas.
    gfx->setCanvas(hi);
    gfx->setBackgroundColorRGBA(0.05f, 0.05f, 0.08f, 1.f);
    gfx->clearScreen();
    Texture *white = nullptr;
    {
        const uint8_t px[4] = {240, 240, 240, 255};
        white = gfx->newTexture(1, 1, px);
    }
    // Staircase of thin rectangles approximating a diagonal.
    for (int i = 0; i < 32; ++i) {
        const float x = float(i * (sw / 32));
        const float y = float(i * (sh / 32));
        gfx->drawTexturedRectRGBA(white, x, y, 6.f, 6.f, 1, 1, 1, 1);
    }
    gfx->setCanvas(nullptr);

    aa->applyCanvasTo(gfx, hi, lo);
    previewTexture(gfx, lo->getTexture(), 400);
}

TEST_CASE("aa.paramOverrides") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    std::unique_ptr<AntiAliasing> aa(gfx->newAntiAliasing());
    aa->setMode("fxaa");
    aa->setFloat("subpix", 0.25f);
    CHECK(std::fabs(aa->getFloat("subpix") - 0.25f) < 1e-5f);

    aa->setMode("smaa");
    aa->setFloat("threshold", 0.2f);
    CHECK(std::fabs(aa->getFloat("threshold") - 0.2f) < 1e-5f);

    aa->setMode("nfaa");
    aa->setFloat("strength", 1.5f);
    CHECK(std::fabs(aa->getFloat("strength") - 1.5f) < 1e-5f);
}

TEST_CASE("aa.msaaSamplesApi") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 256, 192);

    // Defaults: hardware MSAA on at 4 samples via the render control feature.
    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    CHECK(rc->isEnabled("msaa"));
    CHECK(gfx->getMsaaSamples() == 4);

    gfx->setMsaaSamples(8);
    CHECK(gfx->getMsaaSamples() == 8);
    gfx->setMsaaSamples(2);
    CHECK(gfx->getMsaaSamples() == 2);
    gfx->setMsaaSamples(0);
    CHECK(gfx->getMsaaSamples() == 0);
    gfx->setMsaaSamples(1);  // below the MSAA threshold → treated as off
    CHECK(gfx->getMsaaSamples() == 0);
    gfx->setMsaaSamples(-2);
    CHECK(gfx->getMsaaSamples() == 0);
    gfx->setMsaaSamples(4);
    CHECK(gfx->getMsaaSamples() == 4);

    // 3D smoke frame with MSAA enabled: draw a mesh + resolve, no crash/blackout.
    gfx->setBackgroundColorRGBA(0.05f, 0.05f, 0.08f, 1.f);
    Mesh *mesh = gfx->newMeshSphere(24, 16);
    REQUIRE(mesh != nullptr);
    gfx->begin3DFrame();
    CHECK(gfx->had3DThisFrame());
    gfx->drawMesh(mesh, glm::mat4(1.f), nullptr, Color(0.9f, 0.6f, 0.3f, 1.f));
    gfx->present();
}

