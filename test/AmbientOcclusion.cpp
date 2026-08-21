#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <memory>
#include <string>

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
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "window/Window.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {

void openGfxWindow(eve::window::Window *&win, Graphics *&gfx, int w = 320, int h = 240) {
    win = eve::window::Window::create();
    gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width  = static_cast<uint16_t>(w);
    s.height = static_cast<uint16_t>(h);
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
}

float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

void previewTexture(Graphics *gfx, Texture *tex, int ms = 800) {
    gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.12f, 1.f);
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

struct CornerDepth {
    int w = 0, h = 0;
    // Two walls meeting at a corner near center: closer walls → AO in the crease.
    float wallDepth = 0.35f;
    float floorDepth = 0.85f;
};

float cornerDepth01(int x, int y, void *userdata) {
    auto *b = static_cast<CornerDepth *>(userdata);
    const float u = (float(x) + 0.5f) / float(b->w);
    const float v = (float(y) + 0.5f) / float(b->h);
    // Vertical wall strip + horizontal wall strip (L-shaped occluder / crease).
    const bool vertWall = (u >= 0.45f && u <= 0.55f && v >= 0.25f && v <= 0.75f);
    const bool horizWall = (v >= 0.45f && v <= 0.55f && u >= 0.25f && u <= 0.75f);
    if (vertWall || horizWall) return b->wallDepth;
    return b->floorDepth;
}

}  // namespace

TEST_CASE("ao.qualityAndModePresets") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    AmbientOcclusion *raw = gfx->newAmbientOcclusion();
    REQUIRE(raw != nullptr);
    std::unique_ptr<AmbientOcclusion> ao(raw);
    REQUIRE(ao->getSsaoShader() != nullptr);
    REQUIRE(ao->getHbaoShader() != nullptr);
    REQUIRE(ao->getGtaoShader() != nullptr);
    REQUIRE(ao->getBlurShader() != nullptr);
    REQUIRE(ao->getOverlayShader() != nullptr);

    CHECK(ao->getMode() == "ssao");
    ao->setQuality("low");
    CHECK(ao->getQuality() == "low");
    CHECK(ao->getSampleCount() == 8);
    CHECK(ao->getDownscale() == 4.f);
    CHECK(ao->resolutionFor(400) == 100);

    ao->setQuality("medium");
    CHECK(ao->getSampleCount() == 16);
    CHECK(ao->getDownscale() == 2.f);

    ao->setQuality("high");
    CHECK(ao->getSampleCount() == 24);
    CHECK(ao->getDownscale() == 1.f);

    ao->setMode("hbao");
    CHECK(ao->getMode() == "hbao");
    ao->setQuality("medium");
    CHECK(ao->getSampleCount() == 36);  // 6 dirs * 6 steps

    ao->setMode("gtao");
    ao->setQuality("low");
    CHECK(ao->getSampleCount() == 16);  // 4*4

    ao->setMode("unknown");
    CHECK(ao->getMode() == "ssao");
    ao->setQuality("unknown");
    CHECK(ao->getQuality() == "medium");

    win->close();
}

TEST_CASE("ao.paramRoundTrip") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    std::unique_ptr<AmbientOcclusion> ao(gfx->newAmbientOcclusion());

    ao->setRadius(0.9f);
    ao->setBias(0.04f);
    ao->setIntensity(1.25f);
    ao->setPower(1.8f);
    CHECK(std::fabs(ao->getRadius() - 0.9f) < 1e-5f);
    CHECK(std::fabs(ao->getBias() - 0.04f) < 1e-5f);
    CHECK(std::fabs(ao->getIntensity() - 1.25f) < 1e-5f);
    CHECK(std::fabs(ao->getPower() - 1.8f) < 1e-5f);
    CHECK(ao->hasParam("invViewProj") == true);
    CHECK(ao->hasParam("sampleCount") == true);
    CHECK(ao->hasParam("dirCount") == true);

    ao->setMode("gtao");
    ao->setThickness(0.7f);
    CHECK(std::fabs(ao->getFloat("thickness") - 0.7f) < 1e-5f);
    win->close();
}

TEST_CASE("ao.ssaoDarkensCrease") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 256, 192);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<AmbientOcclusion> ao(gfx->newAmbientOcclusion());
    ao->setMode("ssao");
    ao->setQuality("high");
    ao->setIntensity(1.5f);
    ao->setRadius(1.2f);
    ao->setBias(0.02f);
    ao->setPower(1.2f);
    ao->setCamera(0.f, 1.5f, 4.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 50.f, 256.f / 192.f, 0.1f, 40.f);

    CornerDepth cd;
    cd.w = 128;
    cd.h = 96;
    Texture *depth = ao->newLinearDepthTexture(gfx, cd.w, cd.h, cornerDepth01, &cd);
    REQUIRE(depth != nullptr);

    Canvas *out = gfx->newCanvas(cd.w, cd.h);
    ao->computeTo(gfx, depth, out);

    // Crease center vs open floor corner.
    const float crease = luma(out->getPixel(cd.w / 2, cd.h / 2));
    const float open = luma(out->getPixel(int(cd.w * 0.12f), int(cd.h * 0.12f)));
    CHECK(open > crease);
    CHECK(crease < 0.98f);
    CHECK(open > 0.5f);

    previewTexture(gfx, out->getTexture());
    win->close();
}

TEST_CASE("ao.hbaoAndGtaoProduceOcclusion") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 200, 150);

    CornerDepth cd;
    cd.w = 100;
    cd.h = 75;

    for (const char *mode : {"hbao", "gtao"}) {
        std::unique_ptr<AmbientOcclusion> ao(gfx->newAmbientOcclusion());
        ao->setMode(mode);
        ao->setQuality("medium");
        ao->setIntensity(1.4f);
        ao->setRadius(1.0f);
        ao->setCamera(0.f, 2.f, 5.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 55.f, 200.f / 150.f, 0.1f, 50.f);

        Texture *depth = ao->newLinearDepthTexture(gfx, cd.w, cd.h, cornerDepth01, &cd);
        Canvas *out = gfx->newCanvas(cd.w, cd.h);
        ao->computeTo(gfx, depth, out);

        float minL = 1.f;
        float maxL = 0.f;
        for (int y = 0; y < cd.h; y += 3) {
            for (int x = 0; x < cd.w; x += 3) {
                const float L = luma(out->getPixel(x, y));
                minL = std::min(minL, L);
                maxL = std::max(maxL, L);
            }
        }
        CHECK(maxL > minL);
        CHECK(minL < 0.95f);
    }
    win->close();
}

TEST_CASE("ao.blurPreservesMeanRoughly") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);

    std::unique_ptr<AmbientOcclusion> ao(gfx->newAmbientOcclusion());
    ao->setMode("ssao");
    ao->setQuality("medium");
    ao->setCamera(0.f, 1.f, 3.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 60.f, 160.f / 120.f, 0.1f, 30.f);

    CornerDepth cd;
    cd.w = 80;
    cd.h = 60;
    Texture *depth = ao->newLinearDepthTexture(gfx, cd.w, cd.h, cornerDepth01, &cd);
    Canvas *raw = gfx->newCanvas(cd.w, cd.h);
    ao->computeTo(gfx, depth, raw);

    Canvas *blurred = gfx->newCanvas(cd.w, cd.h);
    ao->blurTo(gfx, raw->getTexture(), blurred);

    float sumRaw = 0.f, sumBlur = 0.f;
    int n = 0;
    for (int y = 0; y < cd.h; y += 2) {
        for (int x = 0; x < cd.w; x += 2) {
            sumRaw += luma(raw->getPixel(x, y));
            sumBlur += luma(blurred->getPixel(x, y));
            ++n;
        }
    }
    const float meanRaw = sumRaw / float(std::max(n, 1));
    const float meanBlur = sumBlur / float(std::max(n, 1));
    CHECK(std::fabs(meanRaw - meanBlur) < 0.25f);
    CHECK(meanBlur > 0.05f);
    win->close();
}

TEST_CASE("ao.overlayDarkensLitScene") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<AmbientOcclusion> ao(gfx->newAmbientOcclusion());
    ao->setMode("ssao");
    ao->setQuality("high");
    ao->setIntensity(1.0f);
    ao->setCamera(0.f, 1.2f, 3.5f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 50.f, 160.f / 120.f, 0.1f, 35.f);

    CornerDepth cd;
    cd.w = 80;
    cd.h = 60;
    Texture *depth = ao->newLinearDepthTexture(gfx, cd.w, cd.h, cornerDepth01, &cd);
    Canvas *aoMap = gfx->newCanvas(cd.w, cd.h);
    ao->computeTo(gfx, depth, aoMap);

    Canvas *scene = gfx->newCanvas(cd.w, cd.h);
    gfx->setCanvas(scene);
    gfx->clear(Color(0.85f, 0.85f, 0.88f, 1.f), std::nullopt, std::nullopt);
    const float before = luma(scene->getPixel(cd.w / 2, cd.h / 2));
    ao->applyOverlay(gfx, aoMap->getTexture());
    gfx->setCanvas(nullptr);

    const float after = luma(scene->getPixel(cd.w / 2, cd.h / 2));
    const float openAfter = luma(scene->getPixel(int(cd.w * 0.1f), int(cd.h * 0.1f)));
    CHECK(after < before);
    CHECK(openAfter + 0.02f >= after);
    previewTexture(gfx, scene->getTexture());
    win->close();
}
