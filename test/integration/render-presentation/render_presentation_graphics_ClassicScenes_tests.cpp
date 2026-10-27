// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-window",
  "module": "graphics",
  "modules": [
    "graphics",
    "image"
  ],
  "origins": [
    "AmbientOcclusion.cpp",
    "AntiAliasing.cpp",
    "BloomPyramid.cpp",
    "ClassicScenes.cpp",
    "ClusteredLighting.cpp",
    "GlobalIllumination.cpp",
    "IBL.cpp",
    "Lighting2D.cpp",
    "Outline.cpp",
    "Quad.cpp"
  ]
}
*/
// clang-format on

// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: AmbientOcclusion
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include "ManualPreview.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <memory>
#include <string>
#include "Fixtures.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
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
#include "window/Window.h"
#include "zeroerr/unittest.h"
// AmbientOcclusion.cpp
namespace test_AmbientOcclusion {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {


float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

void previewTexture(Graphics *gfx, Texture *tex, int ms = 800) {
    if (!eve::test::manualPreviewEnabled()) return;
    gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.12f, 1.f);
    const int frames = (ms >= 16) ? (ms / 16) : 1;
    for (int i = 0; i < frames; ++i) {
        gfx->clearScreen();
        gfx->drawTexturedRectRGBA(tex, 0, 0, float(gfx->getWidth()), float(gfx->getHeight()), 1, 1, 1, 1);
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
    float wallDepth  = 0.35f;
    float floorDepth = 0.85f;
};

float cornerDepth01(int x, int y, void *userdata) {
    auto       *b = static_cast<CornerDepth *>(userdata);
    const float u = (float(x) + 0.5f) / float(b->w);
    const float v = (float(y) + 0.5f) / float(b->h);
    // Vertical wall strip + horizontal wall strip (L-shaped occluder / crease).
    const bool vertWall  = (u >= 0.45f && u <= 0.55f && v >= 0.25f && v <= 0.75f);
    const bool horizWall = (v >= 0.45f && v <= 0.55f && u >= 0.25f && u <= 0.75f);
    if (vertWall || horizWall) return b->wallDepth;
    return b->floorDepth;
}

}  // namespace

TEST_CASE("ao.qualityAndModePresets") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
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
    Graphics            *gfx = nullptr;
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
    Graphics            *gfx = nullptr;
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
    cd.w           = 128;
    cd.h           = 96;
    // Keep the wall/floor depth gap inside the AO radius; probe the floor beside the walls.
    cd.wallDepth   = 0.10f;
    cd.floorDepth  = 0.12f;
    Texture *depth = ao->newLinearDepthTexture(gfx, cd.w, cd.h, cornerDepth01, &cd);
    REQUIRE(depth != nullptr);

    Canvas *out = gfx->newCanvas(cd.w, cd.h);
    ao->computeTo(gfx, depth, out);

    // Crease center vs open floor corner.
    const float crease = luma(out->getPixel(int(cd.w * 0.56f), int(cd.h * 0.56f)));
    const float open   = luma(out->getPixel(int(cd.w * 0.12f), int(cd.h * 0.12f)));
    CHECK(open > crease);
    CHECK(crease < 0.98f);
    CHECK(open > 0.5f);

    previewTexture(gfx, out->getTexture());
    win->close();
}

TEST_CASE("ao.hbaoAndGtaoProduceOcclusion") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 200, 150);

    CornerDepth cd;
    cd.w = 100;
    cd.h = 75;
    // With a 50-unit far plane, this depth gap is half a world unit and
    // lies inside the one-unit AO radius (the old gap spanned 25 units).
    cd.wallDepth  = 0.10f;
    cd.floorDepth = 0.11f;

    for (const char *mode : {"hbao", "gtao"}) {
        std::unique_ptr<AmbientOcclusion> ao(gfx->newAmbientOcclusion());
        ao->setMode(mode);
        ao->setQuality("medium");
        ao->setIntensity(1.4f);
        ao->setRadius(1.0f);
        ao->setCamera(0.f, 2.f, 5.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 55.f, 200.f / 150.f, 0.1f, 50.f);

        Texture *depth = ao->newLinearDepthTexture(gfx, cd.w, cd.h, cornerDepth01, &cd);
        Canvas  *out   = gfx->newCanvas(cd.w, cd.h);
        ao->computeTo(gfx, depth, out);

        float minL = 1.f;
        float maxL = 0.f;
        for (int y = 0; y < cd.h; y += 3) {
            for (int x = 0; x < cd.w; x += 3) {
                const float L = luma(out->getPixel(x, y));
                minL          = std::min(minL, L);
                maxL          = std::max(maxL, L);
            }
        }
        CHECK(maxL > minL);
        CHECK(minL < 0.95f);
    }
    win->close();
}

TEST_CASE("ao.blurPreservesMeanRoughly") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);

    std::unique_ptr<AmbientOcclusion> ao(gfx->newAmbientOcclusion());
    ao->setMode("ssao");
    ao->setQuality("medium");
    ao->setCamera(0.f, 1.f, 3.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 60.f, 160.f / 120.f, 0.1f, 30.f);

    auto *const sharedAo = ao.get();
    SUB_CASE("data passes preserve AO and depth independently of the destination") {
        // Zero intensity gives a known white AO payload, independent of each algorithm's sampling.
        sharedAo->setIntensity(0.f);
        Texture *flatDepth =
            sharedAo->newLinearDepthTexture(gfx, 8, 8, [](int, int, void *) { return 0.25f; }, nullptr);
        Canvas *computed = gfx->newCanvas(8, 8);
        Canvas *filtered = gfx->newCanvas(8, 8);
        REQUIRE(flatDepth != nullptr);
        REQUIRE(computed != nullptr);
        REQUIRE(filtered != nullptr);
        for (const char *mode : {"ssao", "hbao", "gtao"}) {
            sharedAo->setMode(mode);
            for (const Color clear : {Color(1.f, 0.f, 0.f, 1.f), Color(0.f, 0.f, 1.f, 0.f)}) {
                gfx->setCanvas(computed);
                gfx->clear(clear, std::nullopt, std::nullopt);
                gfx->setCanvas(nullptr);
                sharedAo->computeTo(gfx, flatDepth, computed);
                const Color data = computed->getPixel(4, 4);
                REQUIRE(data.r > 0.98f);
                REQUIRE(data.g > 0.98f);
                REQUIRE(data.b > 0.98f);
                REQUIRE(std::fabs(data.a - 0.25f) < 0.01f);

                gfx->setCanvas(filtered);
                gfx->clear(clear, std::nullopt, std::nullopt);
                gfx->setCanvas(nullptr);
                sharedAo->blurTo(gfx, computed->getTexture(), filtered);
                const Color blurred = filtered->getPixel(4, 4);
                REQUIRE(blurred.r > 0.98f);
                REQUIRE(blurred.g > 0.98f);
                REQUIRE(blurred.b > 0.98f);
                REQUIRE(std::fabs(blurred.a - 0.25f) < 0.01f);
            }
        }
    };
    ao->setMode("ssao");
    ao->setIntensity(1.f);

    CornerDepth cd;
    cd.w           = 80;
    cd.h           = 60;
    Texture *depth = ao->newLinearDepthTexture(gfx, cd.w, cd.h, cornerDepth01, &cd);
    Canvas  *raw   = gfx->newCanvas(cd.w, cd.h);
    ao->computeTo(gfx, depth, raw);

    Canvas *blurred = gfx->newCanvas(cd.w, cd.h);
    ao->blurTo(gfx, raw->getTexture(), blurred);

    float sumRaw = 0.f, sumBlur = 0.f;
    int   n = 0;
    for (int y = 0; y < cd.h; y += 2) {
        for (int x = 0; x < cd.w; x += 2) {
            sumRaw += luma(raw->getPixel(x, y));
            sumBlur += luma(blurred->getPixel(x, y));
            ++n;
        }
    }
    const float meanRaw  = sumRaw / float(std::max(n, 1));
    const float meanBlur = sumBlur / float(std::max(n, 1));
    CHECK(std::fabs(meanRaw - meanBlur) < 0.25f);
    CHECK(meanBlur > 0.05f);
    win->close();
}

TEST_CASE("ao.overlayDarkensLitScene") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<AmbientOcclusion> ao(gfx->newAmbientOcclusion());
    ao->setMode("ssao");
    ao->setQuality("high");
    ao->setIntensity(1.0f);
    ao->setCamera(0.f, 1.2f, 3.5f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 50.f, 160.f / 120.f, 0.1f, 35.f);

    CornerDepth cd;
    cd.w           = 80;
    cd.h           = 60;
    cd.wallDepth   = 0.10f;
    cd.floorDepth  = 0.12f;
    Texture *depth = ao->newLinearDepthTexture(gfx, cd.w, cd.h, cornerDepth01, &cd);
    Canvas  *aoMap = gfx->newCanvas(cd.w, cd.h);
    ao->computeTo(gfx, depth, aoMap);

    Canvas *scene = gfx->newCanvas(cd.w, cd.h);
    gfx->setCanvas(scene);
    gfx->drawSolidRect(0.f, 0.f, float(cd.w), float(cd.h), Color(0.85f, 0.85f, 0.88f, 1.f));
    gfx->setCanvas(nullptr);
    const int   creaseX = int(cd.w * 0.56f), creaseY = int(cd.h * 0.56f);
    const float before = luma(scene->getPixel(creaseX, creaseY));
    REQUIRE(before > 0.8f);
    ao->applyOverlayTo(gfx, aoMap->getTexture(), scene);

    const float after     = luma(scene->getPixel(creaseX, creaseY));
    const float openAfter = luma(scene->getPixel(int(cd.w * 0.1f), int(cd.h * 0.1f)));
    CHECK(after < before);
    CHECK(openAfter + 0.02f >= after);
    previewTexture(gfx, scene->getTexture());
    win->close();
}
}  // namespace test_AmbientOcclusion

// Contract section: AntiAliasing
#include "PrivateTestScope.h"


#include <vector>
// AntiAliasing.cpp
namespace test_AntiAliasing {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {


Texture *makeChecker(Graphics *gfx, int w, int h) {
    std::vector<uint8_t> px(size_t(w * h * 4));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool   on = ((x / 4) + (y / 4)) & 1;
            const size_t i  = size_t((y * w + x) * 4);
            px[i + 0]       = on ? 255 : 16;
            px[i + 1]       = on ? 255 : 16;
            px[i + 2]       = on ? 255 : 16;
            px[i + 3]       = 255;
        }
    }
    return gfx->newTexture(w, h, px.data());
}

/** Hold a textured canvas / texture on the swapchain briefly. */
void previewTexture(Graphics *gfx, Texture *tex, int ms = 400) {
    if (!eve::test::manualPreviewEnabled()) return;
    gfx->setBackgroundColorRGBA(0.05f, 0.05f, 0.08f, 1.f);
    const int frames = (ms >= 16) ? (ms / 16) : 1;
    for (int i = 0; i < frames; ++i) {
        gfx->clearScreen();
        gfx->drawTexturedRectRGBA(tex, 0, 0, float(gfx->getWidth()), float(gfx->getHeight()), 1, 1, 1, 1);
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
    Graphics            *gfx = nullptr;
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
    Graphics            *gfx = nullptr;
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
    gfx->drawTexturedRectRGBA(dest->getTexture(), 0, 0, float(gfx->getWidth()), float(gfx->getHeight()), 1, 1, 1, 1);
    gfx->present();
}

TEST_CASE("aa.allModesSmoke") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);

    std::unique_ptr<AntiAliasing> aa(gfx->newAntiAliasing());
    Texture                      *src  = makeChecker(gfx, 160, 120);
    Canvas                       *dest = gfx->newCanvas(160, 120);
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
    Graphics            *gfx = nullptr;
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
        white               = gfx->newTexture(1, 1, px);
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
    Graphics            *gfx = nullptr;
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
    Graphics            *gfx = nullptr;
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
}  // namespace test_AntiAliasing

// Contract section: BloomPyramid
#include "PrivateTestScope.h"


#include "common/config.h"
#include "graphics/Bloom.h"
#include "graphics/shaders/PostProcessWgsl.h"
#include "image/Image.h"
#include "image/ImageData.h"
// BloomPyramid.cpp
namespace test_BloomPyramid {
#ifndef EVENGINE_WEBGPU
#include "graphics/shaders/textured_frag_spv.inc"
#endif


TEST_CASE("graphics.bloom photographic pyramid preserves weighted DC and threshold suppression") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture fixture(64, 32, true);
    auto      *gfx   = fixture.gfx;
    auto      *input = gfx->newHDRCanvas(1281, 721);
    REQUIRE(input != nullptr);
    gfx->setCanvas(input);
    gfx->clear(Color(1, .5f, .25f, 1), std::nullopt, std::nullopt);
    gfx->drawSolidRect(0, 0, 1281, 721, Color(1, .5f, .25f, 1));
    gfx->setCanvas();
    std::unique_ptr<eve::image::ImageData> sourcePixels(input->newHDRImageData());
    REQUIRE(sourcePixels != nullptr);
    REQUIRE(sourcePixels->getPixel(32, 16).r == 1);
#ifdef EVENGINE_WEBGPU
    auto *copyShader =
        gfx->newShaderFromWgsl({}, std::string(shaders::kPostCommon) +
                                       "@fragment fn fs_main(i:FSIn)->@location(0) vec4f{return tex(i.uv)*i.color;}");
#else
    auto *copyShader = gfx->newShaderFromSpv({}, {textured_frag_spv, textured_frag_spv + textured_frag_spv_count});
#endif
    REQUIRE(copyShader != nullptr);
    Bloom               bloom(gfx);
    BloomFilterSettings settings;
    settings.filter = BloomFilter::GaussianPyramid;
    REQUIRE(bloom.configureFilter(settings).ok());
    const auto sample = [&](float threshold) {
        auto *result = bloom.apply(input->getTexture(), .675f, threshold);
        REQUIRE(result != nullptr);
        auto *output = gfx->newHDRCanvas(64, 32);
        REQUIRE(output != nullptr);
        gfx->setCanvas(output);
        gfx->drawTexturedRectShader(result, copyShader, 0, 0, 64, 32, Color(1));
        gfx->setCanvas();
        return std::unique_ptr<eve::image::ImageData>(output->newHDRImageData());
    };
    auto bright = sample(0);
    REQUIRE(bright != nullptr);
    constexpr float energy = 1 + .675f * (.3465f + .138f + .1176f + .066f + .066f + .061f) / 6;
    const auto      center = bright->getPixel(32, 16);
    REQUIRE(std::abs(center.r - energy) < .004f);
    for (const auto &point : {std::pair{0, 0}, std::pair{63, 31}}) {
        const auto pixel = bright->getPixel(point.first, point.second);
        // The reference blur uses a black border, not edge replication.
        REQUIRE(pixel.r > 1);
        REQUIRE(pixel.r < center.r);
        REQUIRE(std::abs(pixel.g - pixel.r * .5f) < .003f);
        REQUIRE(std::abs(pixel.b - pixel.r * .25f) < .002f);
    }
    auto suppressed = sample(10);
    REQUIRE(suppressed != nullptr);
    REQUIRE(std::abs(suppressed->getPixel(32, 16).r - 1) < .002f);
    // Equal level counts must not reuse fast-blur intermediates with wrong dimensions.
    settings.filter = BloomFilter::GaussianScatter;
    REQUIRE(bloom.configureFilter(settings).ok());
    auto scattered = sample(0);
    REQUIRE(scattered != nullptr);
    REQUIRE(std::abs(scattered->getPixel(32, 16).r - 1.675f) < .008f);
    settings.filter = BloomFilter::GaussianPyramid;
    REQUIRE(bloom.configureFilter(settings).ok());
    auto restored = sample(0);
    REQUIRE(restored != nullptr);
    REQUIRE(std::abs(restored->getPixel(32, 16).r - energy) < .004f);
}
}  // namespace test_BloomPyramid

// Contract section: ClassicScenes
#include "PrivateTestScope.h"


#include <assimp/material.h>
#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/texture.h>
#include <assimp/vector3.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>
#include "PathBesideSource.h"
#include "RenderImageAudit.h"
#include "data/ByteData.h"
#include "filesystem/Filesystem.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "os/OS.h"
#include "timer/Timer.h"
// ClassicScenes.cpp
namespace test_ClassicScenes {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;


using namespace eve::graphics;

namespace {


EVE_DEFINE_PATH_BESIDE_SOURCE()

std::vector<char> readBinaryFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool fileExists(const std::string &path) { return std::filesystem::is_regular_file(path); }

float luma(const Color &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }


void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]        = *it;
            data->active       = false;
            data->envMap       = nullptr;
            data->envIntensity = 1.f;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d]   = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
}

Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

Texture *makeSolidCubemap(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b, int face = 8) {
    std::vector<uint8_t> faces(size_t(face) * size_t(face) * 4u * 6u);
    for (size_t i = 0; i < faces.size(); i += 4) {
        faces[i + 0] = r;
        faces[i + 1] = g;
        faces[i + 2] = b;
        faces[i + 3] = 255;
    }
    return gfx->newCubemap(face, faces.data());
}

/** Studio-like face-colored env so metal reflections read clearly in flight. */
Texture *makeStudioCubemap(Graphics *gfx, int face = 16) {
    const uint8_t faceRgb[6][3] = {
        {220, 180, 140},  // +X warm
        {120, 150, 210},  // -X cool
        {245, 245, 250},  // +Y skylight
        {40, 40, 45},     // -Y ground
        {200, 210, 230},  // +Z
        {180, 160, 140},  // -Z
    };
    const size_t         faceBytes = size_t(face) * size_t(face) * 4u;
    std::vector<uint8_t> faces(faceBytes * 6u);
    for (int f = 0; f < 6; ++f) {
        uint8_t *dst = faces.data() + size_t(f) * faceBytes;
        for (size_t i = 0; i < faceBytes; i += 4) {
            dst[i + 0] = faceRgb[f][0];
            dst[i + 1] = faceRgb[f][1];
            dst[i + 2] = faceRgb[f][2];
            dst[i + 3] = 255;
        }
    }
    return gfx->newCubemap(face, faces.data());
}

Texture *textureFromImageData(Graphics *gfx, eve::image::ImageData *img) {
    if (!img) return nullptr;
    // Classic flythroughs need mipmaps + anisotropy; engine defaults stay off for pixel-art paths.
    return gfx->newTexture(img, TextureCreateInfo::withMipmaps(true));
}

Texture *loadAssimpDiffuseTexture(Graphics *gfx, const aiScene *scene, const aiMaterial *mat,
                                  const std::string &assetDir) {
    if (!scene || !mat) return nullptr;

    aiString path;
    // glTF PBR stores albedo as BASE_COLOR; older assets use DIFFUSE.
    if (mat->GetTexture(aiTextureType_BASE_COLOR, 0, &path) != AI_SUCCESS &&
        mat->GetTexture(aiTextureType_DIFFUSE, 0, &path) != AI_SUCCESS) {
        return nullptr;
    }

    const char *p = path.C_Str();
    if (!p || !p[0]) return nullptr;

    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();

    if (p[0] == '*') {
        int idx = std::atoi(p + 1);
        if (idx < 0 || static_cast<unsigned>(idx) >= scene->mNumTextures) return nullptr;
        const aiTexture *tex = scene->mTextures[idx];
        if (!tex || !tex->pcData) return nullptr;
        if (tex->mHeight == 0) {
            eve::data::ByteData bytes(tex->pcData, static_cast<size_t>(tex->mWidth));
            try {
                eve::image::ImageData *img = eve::image::Image::create()->newImageData(&bytes);
                Texture               *t   = textureFromImageData(gfx, img);
                delete img;
                return t;
            } catch (...) {
                return nullptr;
            }
        }
        return nullptr;
    }

    std::string full = assetDir;
    if (!full.empty() && full.back() != '/' && full.back() != '\\') full.push_back('/');
    full += p;
    // Assimp sometimes returns nested paths with backslashes.
    for (char &c : full) {
        if (c == '\\') c = '/';
    }
    auto raw = readBinaryFile(full);
    if (raw.empty()) {
        // Try basename only under assetDir (glTF often stores URI as filename).
        const char *base = std::strrchr(p, '/');
        if (!base) base = std::strrchr(p, '\\');
        base = base ? base + 1 : p;
        full = assetDir;
        if (!full.empty() && full.back() != '/' && full.back() != '\\') full.push_back('/');
        full += base;
        raw = readBinaryFile(full);
    }
    if (raw.empty()) return nullptr;
    eve::data::ByteData bytes(raw.data(), raw.size());
    try {
        eve::image::ImageData *img = eve::image::Image::create()->newImageData(&bytes);
        Texture               *t   = textureFromImageData(gfx, img);
        delete img;
        return t;
    } catch (...) {
        return nullptr;
    }
}

struct Bounds {
    bool  valid = false;
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;
    void  expand(float x, float y, float z) {
        if (!valid) {
            minX = maxX = x;
            minY = maxY = y;
            minZ = maxZ = z;
            valid       = true;
            return;
        }
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        minZ = std::min(minZ, z);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
        maxZ = std::max(maxZ, z);
    }
    float centerX() const { return 0.5f * (minX + maxX); }
    float centerY() const { return 0.5f * (minY + maxY); }
    float centerZ() const { return 0.5f * (minZ + maxZ); }
    float radius() const {
        if (!valid) return 1.f;
        const float dx = maxX - minX;
        const float dy = maxY - minY;
        const float dz = maxZ - minZ;
        return 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
    }
};

void expandBoundsTransformed(Bounds &b, const aiMesh *mesh, const aiMatrix4x4 &world) {
    if (!mesh) return;
    for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
        const aiVector3D p = world * mesh->mVertices[v];
        b.expand(p.x, p.y, p.z);
    }
}

Bounds boundsOfScene(const aiScene *scene) {
    Bounds b;
    if (!scene || !scene->mRootNode) return b;
    std::function<void(const aiNode *, const aiMatrix4x4 &)> walk = [&](const aiNode *node, const aiMatrix4x4 &parent) {
        const aiMatrix4x4 world = parent * node->mTransformation;
        for (unsigned i = 0; i < node->mNumMeshes; ++i) {
            const unsigned mi = node->mMeshes[i];
            if (mi >= scene->mNumMeshes) continue;
            expandBoundsTransformed(b, scene->mMeshes[mi], world);
        }
        for (unsigned c = 0; c < node->mNumChildren; ++c) walk(node->mChildren[c], world);
    };
    walk(scene->mRootNode, aiMatrix4x4());
    return b;
}

void addHud() {
    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r      = 0.f;
    hud->sprite()->g      = 0.f;
    hud->sprite()->b      = 0.f;
}

struct CamKey {
    float eyeX, eyeY, eyeZ;
    float tgtX, tgtY, tgtZ;
};

CamKey lerpKey(const CamKey &a, const CamKey &b, float t) {
    const float u = std::clamp(t, 0.f, 1.f);
    return CamKey{a.eyeX + (b.eyeX - a.eyeX) * u, a.eyeY + (b.eyeY - a.eyeY) * u, a.eyeZ + (b.eyeZ - a.eyeZ) * u,
                  a.tgtX + (b.tgtX - a.tgtX) * u, a.tgtY + (b.tgtY - a.tgtY) * u, a.tgtZ + (b.tgtZ - a.tgtZ) * u};
}

void applyCam(Camera3D *cam, const CamKey &k) {
    cam->setEye(k.eyeX, k.eyeY, k.eyeZ);
    cam->setTarget(k.tgtX, k.tgtY, k.tgtZ);
}

enum class RenderCfg {
    AmbientOnly,
    DirectionalLit,
    DirectionalShadows,
    MultiPointLit,
    IblReflections,
    DirAndPoint,
    ShadowsAndIbl,
    FullLit,
};

const char *cfgName(RenderCfg c) {
    switch (c) {
        case RenderCfg::AmbientOnly: return "ambient";
        case RenderCfg::DirectionalLit: return "dir_lit";
        case RenderCfg::DirectionalShadows: return "dir_shadows";
        case RenderCfg::MultiPointLit: return "point_lit";
        case RenderCfg::IblReflections: return "ibl_reflect";
        case RenderCfg::DirAndPoint: return "dir_point";
        case RenderCfg::ShadowsAndIbl: return "shadows_ibl";
        case RenderCfg::FullLit: return "full_lit";
    }
    return "unknown";
}

bool cfgUsesIbl(RenderCfg c) {
    return c == RenderCfg::IblReflections || c == RenderCfg::ShadowsAndIbl || c == RenderCfg::FullLit;
}

struct SceneActors {
    Camera3D                   *cam    = nullptr;
    Light3D                    *sun    = nullptr;
    Light3D                    *pointA = nullptr;
    Light3D                    *pointB = nullptr;
    Texture                    *env    = nullptr;
    std::vector<Renderable3D *> ents;
    Bounds                      bounds;
    float                       sunR = 1.f, sunG = 0.97f, sunB = 0.92f;
    float                       sunI = 2.2f;
    struct MatBackup {
        float metallic  = 0.f;
        float roughness = 1.f;
    };
    std::vector<MatBackup> matBackup;
};

void setSunColor(SceneActors &a, float r, float g, float b, float intensity) {
    a.sunR = r;
    a.sunG = g;
    a.sunB = b;
    a.sunI = intensity;
    if (a.sun) a.sun->setColor(r, g, b, intensity);
}

void applySun(SceneActors &a, bool castShadow, float intensityScale) {
    a.sun->setEnabled(true);
    a.sun->setCastShadow(castShadow);
    if (castShadow) a.sun->setShadowStrength(1.f);
    a.sun->setColor(a.sunR, a.sunG, a.sunB, std::max(0.05f, a.sunI * intensityScale));
}

void disableAllLights(SceneActors &a) {
    if (a.sun) a.sun->setEnabled(false);
    if (a.pointA) a.pointA->setEnabled(false);
    if (a.pointB) a.pointB->setEnabled(false);
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);
}

void restoreMaterials(SceneActors &a) {
    if (a.matBackup.empty()) {
        a.matBackup.resize(a.ents.size());
        for (size_t i = 0; i < a.ents.size(); ++i) {
            auto mr        = a.ents[i]->meshRenderer();
            a.matBackup[i] = {mr->metallic, mr->roughness};
        }
        return;
    }
    const size_t n = std::min(a.ents.size(), a.matBackup.size());
    for (size_t i = 0; i < n; ++i) {
        a.ents[i]->setMetallic(a.matBackup[i].metallic);
        a.ents[i]->setRoughness(a.matBackup[i].roughness);
    }
}

void applyConfig(SceneActors &a, RenderCfg cfg, bool polishMetals) {
    disableAllLights(a);
    a.cam->setEnvMap(nullptr);
    a.cam->setEnvIntensity(0.f);
    restoreMaterials(a);

    switch (cfg) {
        case RenderCfg::AmbientOnly: a.cam->setAmbient(0.26f, 0.26f, 0.28f); break;
        case RenderCfg::DirectionalLit:
            a.cam->setAmbient(0.06f, 0.06f, 0.07f);
            applySun(a, false, 1.0f);
            break;
        case RenderCfg::DirectionalShadows:
            a.cam->setAmbient(0.10f, 0.10f, 0.12f);
            applySun(a, true, 1.18f);
            break;
        case RenderCfg::MultiPointLit:
            a.cam->setAmbient(0.03f, 0.03f, 0.04f);
            a.pointA->setEnabled(true);
            a.pointB->setEnabled(true);
            break;
        case RenderCfg::IblReflections:
            a.cam->setAmbient(0.02f, 0.02f, 0.03f);
            applySun(a, false, 0.4f);
            a.cam->setEnvMap(a.env);
            a.cam->setEnvIntensity(1.0f);
            if (polishMetals) {
                for (auto *e : a.ents) {
                    e->setMetallic(std::max(e->meshRenderer()->metallic, 0.85f));
                    e->setRoughness(std::min(e->meshRenderer()->roughness, 0.2f));
                }
            }
            break;
        case RenderCfg::DirAndPoint:
            a.cam->setAmbient(0.04f, 0.04f, 0.05f);
            applySun(a, false, 0.91f);
            a.pointA->setEnabled(true);
            a.pointB->setEnabled(true);
            break;
        case RenderCfg::ShadowsAndIbl:
            a.cam->setAmbient(0.03f, 0.03f, 0.04f);
            applySun(a, true, 1.0f);
            a.cam->setEnvMap(a.env);
            a.cam->setEnvIntensity(1.1f);
            break;
        case RenderCfg::FullLit:
            a.cam->setAmbient(0.03f, 0.03f, 0.04f);
            applySun(a, true, 0.91f);
            a.pointA->setEnabled(true);
            a.pointB->setEnabled(true);
            a.cam->setEnvMap(a.env);
            a.cam->setEnvIntensity(1.0f);
            break;
    }
}

void warmPresent(Graphics *gfx) {
    for (int i = 0; i < 2; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
}

/** Hold the live view at `hz` so lighting/AO/GI can be inspected. Perf benches skip this. */
void paceViewFrame(eve::timer::Timer *timer, float frameStart, float hz) {
    if (!timer) return;
    const float budget = 1.f / std::max(hz, 1.f);
    const float remain = budget - (timer->getTime() - frameStart);
    if (remain < 0.001f) return;
    eve::os::OS::create()->sleepMilliseconds(int(remain * 1000.f + 0.5f));
}

float viewSecondsPerPhase() {
    const char *env = std::getenv("EVENGINE_VIEW_SECONDS");
    if (env && env[0]) {
        const float s = float(std::atof(env));
        return s > 0.2f ? s : 0.2f;
    }
    return 4.f;
}

float viewHz() {
    const char *env = std::getenv("EVENGINE_VIEW_HZ");
    if (env && env[0]) {
        const float h = float(std::atof(env));
        return h > 1.f ? h : 1.f;
    }
    return 30.f;
}

/** Extra per-phase / diagnostic PNGs; regular contracts do not emit hero images. */
bool dumpPhaseFrames() {
    const char *env = std::getenv("EVENGINE_DUMP_PHASES");
    return env && env[0] && env[0] != '0';
}

std::string classicOutDir() { return std::string(EVENGINE_TEST_BINARY_DIR) + "/out/classic_scenes"; }

void saveFramePng(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, Graphics * /*gfx*/, const std::string &name,
                  eve::image::ImageData *frame) {
    const std::string outDir = classicOutDir();
    REQUIRE(saveImagePng(*frame, outDir + "/" + name));
    std::printf("ClassicScenes frame: %s\n", (outDir + "/" + name).c_str());
}

/** CPU-only audit of the already-read-back snapshot. No extra present/readback. */
void auditCapturedFrame(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, eve::image::ImageData *frame, Graphics *gfx,
                        const char *sceneTag, const char *phase) {
    REQUIRE(frame != nullptr);
    const Color       c = gfx->getBackgroundColor();
    RenderAuditConfig cfg{sceneTag, phase, "", 0};
    // Stride 4: one pass over ~1/16 of pixels. Flicker needs a second GPU
    // readback and is left to graphics.imageAudit.pipelineConfigs.
    RenderAuditResult result = auditImage(*frame, cfg, RenderAuditBg{c.r, c.g, c.b}, /*step=*/4);
    if (result.hasDefects()) {
        const std::string outDir = classicOutDir();
        appendAuditReport(outDir, result);
        if (result.hasSevere()) saveAuditOverlay(*frame, result, outDir);
        std::printf("ClassicScenes[%s] phase=%s image defects=%zu severe=%d — %s/image_audit.md\n", sceneTag, phase,
                    result.defects.size(), int(result.hasSevere()), outDir.c_str());
    }
    // Ambient-only product shots are dark-on-dark by design; occupancy < 2% is
    // not a missing-mesh failure.
    if (std::strcmp(phase, "ambient") != 0) REQUIRE(!result.hasSevere());
}

void resetRenderControl(Graphics *gfx) {
    RenderControl *rc = gfx->getRenderControl();
    if (!rc) return;
    rc->enable("depthTest");
    rc->enable("shadow");
    rc->enable("forward");
    rc->enable("hair");
    rc->enable("clustered");
    rc->enable("ao");
    rc->enable("gi");
    rc->enable("aa");
    rc->compile();
}

/** Extra lighting / RenderControl snaps at the current camera. No extra fly frames. */
void snapStaticConfigGrid(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, Graphics *gfx, SceneActors &actors,
                          const char *sceneTag, bool polishMetals) {
    struct Extra {
        RenderCfg   cfg;
        const char *enableFeat;
        const char *disableFeat;
        const char *tag;
        int         msaaSamples = -1;
    };
    const Extra extras[] = {
        {RenderCfg::DirAndPoint, nullptr, nullptr, "dir_point"},
        {RenderCfg::ShadowsAndIbl, nullptr, nullptr, "shadows_ibl"},
        {RenderCfg::FullLit, nullptr, nullptr, "full_lit"},
        {RenderCfg::DirectionalLit, "gbuffer", nullptr, "gbuffer"},
        {RenderCfg::DirectionalLit, "gbufferAlbedo", nullptr, "gbuffer_albedo"},
        {RenderCfg::DirectionalShadows, nullptr, "shadow", "no_shadow_pass"},
        {RenderCfg::DirectionalShadows, nullptr, nullptr, "msaa_2x", 2},
        {RenderCfg::DirectionalShadows, nullptr, nullptr, "msaa_off", 0},
    };
    RenderControl *rc = gfx->getRenderControl();
    gfx->setScreenReadbackEnabled(false);
    for (const auto &ex : extras) {
        applyConfig(actors, ex.cfg, polishMetals && cfgUsesIbl(ex.cfg));
        resetRenderControl(gfx);
        if (ex.enableFeat && rc) {
            rc->enable(ex.enableFeat);
            rc->compile();
        }
        if (ex.disableFeat && rc) {
            rc->disable(ex.disableFeat);
            rc->compile();
        }
        if (ex.msaaSamples >= 0) gfx->setMsaaSamples(ex.msaaSamples);
        std::printf("ClassicScenes[%s] extra=%s\n", sceneTag, ex.tag);
        for (int i = 0; i < 2; ++i) {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
        }
        gfx->setScreenReadbackEnabled(true);
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        eve::image::ImageData *snap = gfx->newImageData();
        REQUIRE(snap != nullptr);
        if (dumpPhaseFrames())
            saveFramePng(_ZEROERR_TEST_CONTEXT, gfx, std::string(sceneTag) + "_" + ex.tag + ".png", snap);
        auditCapturedFrame(_ZEROERR_TEST_CONTEXT, snap, gfx, sceneTag, ex.tag);
        delete snap;
        gfx->setScreenReadbackEnabled(false);
        if (ex.msaaSamples >= 0) gfx->setMsaaSamples(4);
    }
    resetRenderControl(gfx);
    gfx->setScreenReadbackEnabled(true);
}

void snapAndSave(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, Graphics *gfx, const char *name) {
    gfx->setScreenReadbackEnabled(true);
    warmPresent(gfx);
    eve::image::ImageData *snap = gfx->newImageData();
    REQUIRE(snap != nullptr);
    saveFramePng(_ZEROERR_TEST_CONTEXT, gfx, name, snap);
    delete snap;
    gfx->setScreenReadbackEnabled(false);
}

/** Isolate the pbr_chart ground disk: hide spheres, dump lighting vs GBuffer normals. */
void dumpPbrChartGroundDiag(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, Graphics *gfx, SceneActors &actors,
                            const CamKey &cam) {
    REQUIRE(!actors.ents.empty());
    applyCam(actors.cam, cam);

    applyConfig(actors, RenderCfg::DirectionalShadows, false);
    snapAndSave(_ZEROERR_TEST_CONTEXT, gfx, "pbr_chart_diag_shadows.png");
    CamKey side = cam;
    side.eyeX   = 2.f * cam.tgtX - cam.eyeX;
    side.eyeZ   = 2.f * cam.tgtZ - cam.eyeZ;
    applyCam(actors.cam, side);
    snapAndSave(_ZEROERR_TEST_CONTEXT, gfx, "pbr_chart_diag_shadows_side.png");
    applyCam(actors.cam, cam);

    for (size_t i = 1; i < actors.ents.size(); ++i) actors.ents[i]->setVisible(false);
    auto       *ground     = actors.ents[0];
    const float savedRough = ground->meshRenderer()->roughness;

    applyConfig(actors, RenderCfg::DirectionalLit, false);
    snapAndSave(_ZEROERR_TEST_CONTEXT, gfx, "pbr_chart_diag_ground_dir.png");

    applyConfig(actors, RenderCfg::IblReflections, false);
    snapAndSave(_ZEROERR_TEST_CONTEXT, gfx, "pbr_chart_diag_ground_ibl.png");

    applyConfig(actors, RenderCfg::DirectionalLit, false);
    ground->setRoughness(1.f);
    snapAndSave(_ZEROERR_TEST_CONTEXT, gfx, "pbr_chart_diag_ground_matte.png");
    ground->setRoughness(savedRough);

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    applyConfig(actors, RenderCfg::DirectionalLit, false);
    resetRenderControl(gfx);
    rc->enable("gbuffer");
    rc->compile();
    gfx->setScreenReadbackEnabled(false);
    for (int i = 0; i < 2; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    Texture *nTex = rc->getGBuffer()->getNormalTexture();
    REQUIRE(nTex != nullptr);
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(nTex, 0, 0, float(gfx->getWidth()), float(gfx->getHeight()), 1, 1, 1, 1);
    gfx->present();
    eve::image::ImageData *nSnap = gfx->newImageData();
    REQUIRE(nSnap != nullptr);
    saveFramePng(_ZEROERR_TEST_CONTEXT, gfx, "pbr_chart_diag_gbuffer_n.png", nSnap);
    delete nSnap;

    for (auto *e : actors.ents) e->setVisible(true);
    resetRenderControl(gfx);
    gfx->setScreenReadbackEnabled(true);
}

void dumpCornellShadowSweep(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, Graphics *gfx, SceneActors &actors,
                            const std::vector<CamKey> &path) {
    REQUIRE(path.size() >= 2);
    applyConfig(actors, RenderCfg::DirectionalShadows, false);
    for (size_t i = 0; i < path.size(); ++i) {
        applyCam(actors.cam, path[i]);
        char name[80];
        std::snprintf(name, sizeof(name), "cornell_diag_shadow_k%zu.png", i);
        snapAndSave(_ZEROERR_TEST_CONTEXT, gfx, name);
    }
    // Consecutive micro-steps on the first leg: swimming shows up as the umbra
    // crawling across the floor between otherwise-similar frames.
    for (int i = 0; i < 6; ++i) {
        applyCam(actors.cam, lerpKey(path[0], path[1], float(i) * 0.04f));
        char name[80];
        std::snprintf(name, sizeof(name), "cornell_diag_shadow_step%02d.png", i);
        snapAndSave(_ZEROERR_TEST_CONTEXT, gfx, name);
    }
}

float meanLuma(Graphics *gfx, int step = 8) {
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    // Center crop — background clear color otherwise dilutes small/dark assets.
    const int x0  = w / 4;
    const int x1  = (w * 3) / 4;
    const int y0  = h / 4;
    const int y1  = (h * 3) / 4;
    double    sum = 0.0;
    int       n   = 0;
    for (int y = y0; y < y1; y += step) {
        for (int x = x0; x < x1; x += step) {
            sum += luma(gfx->getPixel(x, y));
            ++n;
        }
    }
    return n ? float(sum / n) : 0.f;
}

/** Assimp row-major → glm column-major for Graphics::newMeshFromAssimp. */
glm::mat4 aiToGlm(const aiMatrix4x4& m) {
    return glm::mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
}

/** Upload an Assimp mesh with a baked world transform (copies positions/normals). */
Mesh *uploadMeshWorld(Graphics *gfx, const aiMesh *src, const aiMatrix4x4 &world) {
    REQUIRE(src != nullptr);
    return gfx->newMeshFromAssimp(*src, aiToGlm(world));
}

/** Spawn every Assimp mesh with node transforms baked + material tint/texture.
 *  @param texturesLoaded optional out-counter for successfully decoded maps.
 *  @param useMaterialPbrFactors when false, keep caller metallic/roughness defaults
 *         (glTF's metallicFactor default is 1.0; without sampling the MR texture that
 *         turns dielectrics into black mirrors under direct light). */
int spawnModel(Graphics *gfx, eve::model3d::ModelData *md, const std::string &assetDir, SceneActors &out,
               float defaultMetallic = 0.05f, float defaultRoughness = 0.55f, int *texturesLoaded = nullptr,
               bool useMaterialPbrFactors = true) {
    REQUIRE(md != nullptr);
    const aiScene *scene = md->getScene();
    REQUIRE(scene != nullptr);
    REQUIRE(scene->mRootNode != nullptr);
    Texture *white = makeSolid(gfx, 230, 230, 230);

    struct MatLook {
        Texture *tex = nullptr;
        float    tr = 1.f, tg = 1.f, tb = 1.f;
        float    metallic  = 0.05f;
        float    roughness = 0.55f;
    };
    std::unordered_map<unsigned, MatLook> matCache;

    auto resolveMat = [&](unsigned matIndex) -> MatLook {
        auto cached = matCache.find(matIndex);
        if (cached != matCache.end()) return cached->second;
        MatLook look{white, 1.f, 1.f, 1.f, defaultMetallic, defaultRoughness};
        if (scene->mMaterials && matIndex < scene->mNumMaterials) {
            const aiMaterial *mat = scene->mMaterials[matIndex];
            aiColor3D         kd(1.f, 1.f, 1.f);
            mat->Get(AI_MATKEY_COLOR_DIFFUSE, kd);
            float metallic  = defaultMetallic;
            float roughness = defaultRoughness;
            if (useMaterialPbrFactors) {
                mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
                mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
                // If a metallic-roughness map exists, factors alone are not enough —
                // fall back to dielectrics so architecture stays lit without IBL.
                aiString mrPath;
                if (mat->GetTexture(aiTextureType_UNKNOWN, 0, &mrPath) == AI_SUCCESS ||
                    mat->GetTexture(aiTextureType_METALNESS, 0, &mrPath) == AI_SUCCESS ||
                    mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &mrPath) == AI_SUCCESS) {
                    metallic  = defaultMetallic;
                    roughness = defaultRoughness;
                }
            }
            // Prefer BASE_COLOR factor when present (glTF).
            aiColor4D base(kd.r, kd.g, kd.b, 1.f);
            if (mat->Get(AI_MATKEY_BASE_COLOR, base) == AI_SUCCESS) {
                kd.r = base.r;
                kd.g = base.g;
                kd.b = base.b;
            }
            Texture *fromMat = loadAssimpDiffuseTexture(gfx, scene, mat, assetDir);
            if (fromMat) {
                if (fromMat->getWidth() > 1 || fromMat->getHeight() > 1) {
                    REQUIRE(fromMat->getMipmapCount() > 1);
                    REQUIRE(fromMat->getSampler().maxAnisotropy > 1.f);
                    // Cast enums: zeroerr cannot stream MipmapMode for REQUIRE diagnostics.
                    REQUIRE(static_cast<int>(fromMat->getSampler().mipmap) == static_cast<int>(MipmapMode::Linear));
                }
                look.tex = fromMat;
                look.tr  = kd.r;
                look.tg  = kd.g;
                look.tb  = kd.b;
                if (texturesLoaded) ++(*texturesLoaded);
            } else {
                look.tex = makeSolid(gfx, uint8_t(std::clamp(kd.r, 0.f, 1.f) * 255.f + 0.5f),
                                     uint8_t(std::clamp(kd.g, 0.f, 1.f) * 255.f + 0.5f),
                                     uint8_t(std::clamp(kd.b, 0.f, 1.f) * 255.f + 0.5f));
                look.tr = look.tg = look.tb = 1.f;
            }
            look.metallic  = metallic;
            look.roughness = roughness;
        }
        matCache[matIndex] = look;
        return look;
    };

    int                                                      spawned = 0;
    std::function<void(const aiNode *, const aiMatrix4x4 &)> walk = [&](const aiNode *node, const aiMatrix4x4 &parent) {
        const aiMatrix4x4 world = parent * node->mTransformation;
        for (unsigned i = 0; i < node->mNumMeshes; ++i) {
            const unsigned mi = node->mMeshes[i];
            if (mi >= scene->mNumMeshes) continue;
            const aiMesh *ai = scene->mMeshes[mi];
            if (!ai || ai->mNumFaces == 0) continue;
            Mesh* mesh = gfx->newMeshFromAssimp(*ai, aiToGlm(world));
            REQUIRE(mesh != nullptr);

            MatLook look                 = resolveMat(ai->mMaterialIndex);
            auto   *ent                  = Renderable3D::create();
            ent->meshRenderer()->mesh    = mesh;
            ent->meshRenderer()->texture = look.tex;
            ent->meshRenderer()->visible = true;
            ent->setTint(look.tr, look.tg, look.tb, 1.f);
            ent->setMetallic(look.metallic);
            ent->setRoughness(look.roughness);
            ent->setCastShadow(true);
            ent->setReceiveShadow(true);
            out.ents.push_back(ent);
            ++spawned;
        }
        for (unsigned c = 0; c < node->mNumChildren; ++c) walk(node->mChildren[c], world);
    };
    walk(scene->mRootNode, aiMatrix4x4());
    out.bounds = boundsOfScene(scene);
    return spawned;
}

/** Procedural metal/roughness chart on XZ — classic material-response test. */
void spawnMetalRoughnessChart(Graphics *gfx, SceneActors &out) {
    Mesh *sphere = gfx->newMeshSphere(24, 16);
    REQUIRE(sphere != nullptr);
    // Ground plane so shadows have somewhere to land.
    const float sphereR  = 0.42f;
    const float groundHy = 0.04f;  // cylinder object Y in [-1,1] * scale
    auto       *ground   = Renderable3D::create();
    ground->setMesh(gfx->newMeshCylinder(32, 1, true));
    ground->setTexture(makeSolid(gfx, 60, 62, 68));
    ground->setPosition(0.f, -sphereR - groundHy, 0.f);
    ground->setScale(4.5f, groundHy, 4.5f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.95f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);
    out.ents.push_back(ground);
    out.bounds.expand(-4.f, -0.7f, -4.f);
    out.bounds.expand(4.f, 1.2f, 4.f);

    const int nM = 4;
    const int nR = 4;
    for (int im = 0; im < nM; ++im) {
        for (int ir = 0; ir < nR; ++ir) {
            auto *ent = Renderable3D::create();
            ent->setMesh(sphere);
            ent->setTexture(makeSolid(gfx, 210, 210, 215));
            const float x = (float(im) - 1.5f) * 1.15f;
            const float z = (float(ir) - 1.5f) * 1.15f;
            ent->setPosition(x, 0.f, z);
            ent->setScale(sphereR, sphereR, sphereR);
            ent->setMetallic(float(im) / float(nM - 1));
            ent->setRoughness(0.08f + 0.85f * (float(ir) / float(nR - 1)));
            ent->setCastShadow(true);
            ent->setReceiveShadow(true);
            out.ents.push_back(ent);
            out.bounds.expand(x - 0.5f, -0.5f, z - 0.5f);
            out.bounds.expand(x + 0.5f, 0.6f, z + 0.5f);
        }
    }
}

eve::model3d::ModelData *loadModelFromDir(const std::string &dir, const std::string &relPath) {
    REQUIRE(fileExists(dir + "/" + relPath));
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_classic_scenes", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));
    auto *mod = eve::model3d::Model3D::create();
    return mod->newModelDataFromFile(relPath);
}

void setupLights(SceneActors &a, const Bounds &b) {
    const float cx  = b.centerX();
    const float cy  = b.centerY();
    const float cz  = b.centerZ();
    const float rad = std::max(0.5f, b.radius());

    a.sun = Light3D::createLight("dir");
    a.sun->setDirection(0.45f, 1.f, 0.35f);
    setSunColor(a, 1.f, 0.97f, 0.92f, 2.2f);
    a.sun->setCastShadow(false);
    a.sun->setEnabled(false);

    a.pointA = Light3D::createLight("point");
    a.pointA->setPosition(cx - rad * 0.55f, cy + rad * 0.35f, cz + rad * 0.2f);
    a.pointA->setColor(1.f, 0.55f, 0.35f, 4.5f);
    a.pointA->setRadius(rad * 2.2f);
    a.pointA->setEnabled(false);

    a.pointB = Light3D::createLight("point");
    a.pointB->setPosition(cx + rad * 0.55f, cy + rad * 0.25f, cz - rad * 0.15f);
    a.pointB->setColor(0.35f, 0.55f, 1.f, 4.0f);
    a.pointB->setRadius(rad * 2.2f);
    a.pointB->setEnabled(false);
}

std::vector<CamKey> makeOrbitPath(const Bounds &b, float elev = 0.35f, float distScale = 1.7f) {
    const float         cx     = b.centerX();
    const float         cy     = b.centerY();
    const float         cz     = b.centerZ();
    const float         rad    = std::max(0.5f, b.radius());
    const float         dist   = rad * distScale;
    const float         height = cy + rad * elev;
    std::vector<CamKey> keys;
    const int           n = 5;
    // Start at a 3/4 view (~51° from +X) so path[0] snapshots read as a grid,
    // not a side-on collapse of columns.
    const float a0 = 0.90f;
    for (int i = 0; i < n; ++i) {
        const float a = a0 + float(i) / float(n) * 6.2831853f;
        keys.push_back(CamKey{cx + std::cos(a) * dist, height, cz + std::sin(a) * dist, cx, cy, cz});
    }
    keys.push_back(keys.front());
    return keys;
}

std::vector<CamKey> makeCornellPath(const Bounds &b) {
    const float cx = b.centerX();
    const float cy = b.centerY();
    const float cz = b.centerZ();
    return {
        CamKey{cx, cy + 0.15f, cz + 2.4f, cx, cy + 0.1f, cz},
        CamKey{cx - 0.55f, cy + 0.55f, cz + 1.6f, cx, cy + 0.35f, cz - 0.2f},
        CamKey{cx + 0.55f, cy + 0.35f, cz + 1.5f, cx, cy + 0.2f, cz - 0.1f},
        CamKey{cx, cy + 0.9f, cz + 1.2f, cx, cy + 0.2f, cz},
        CamKey{cx, cy + 0.15f, cz + 2.4f, cx, cy + 0.1f, cz},
    };
}

std::vector<CamKey> makeSponzaPath(const Bounds &b) {
    // Crytek Sponza's open atrium runs along +X; keep the eye inside the courtyard
    // rather than outside the facade (which reads as a black wall fill).
    const float x0    = b.minX + (b.maxX - b.minX) * 0.18f;
    const float x1    = b.minX + (b.maxX - b.minX) * 0.82f;
    const float yEye  = b.minY + (b.maxY - b.minY) * 0.18f;
    const float yTgt  = b.minY + (b.maxY - b.minY) * 0.16f;
    const float z     = b.centerZ();
    const float zSide = (b.maxZ - b.minZ) * 0.08f;
    return {
        CamKey{x0, yEye, z, x1, yTgt, z},
        CamKey{b.centerX(), yEye, z + zSide, b.centerX() + (x1 - x0) * 0.15f, yTgt, z},
        CamKey{x1, yEye * 1.05f, z, x0, yTgt, z},
        CamKey{b.centerX(), b.minY + (b.maxY - b.minY) * 0.42f, z - zSide, b.centerX(), yTgt, z},
        CamKey{x0, yEye, z, x1, yTgt, z},
    };
}

/**
 * Submit representative camera keys while cycling render configs.
 * Manual preview retains the timed flythrough. Hero and per-phase PNGs are
 * exported only for manual preview or EVENGINE_DUMP_PHASES=1 respectively.
 * Returns mean luma sampled at the end of each config phase.
 */
std::vector<float> flyThroughConfigs(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, Graphics *gfx, SceneActors &actors,
                                     const std::vector<CamKey> &path, const char *sceneTag, bool polishMetalsForIbl) {
    REQUIRE(actors.cam != nullptr);
    REQUIRE(path.size() >= 2);
    REQUIRE(actors.env != nullptr);

    const RenderCfg phases[] = {
        RenderCfg::AmbientOnly,   RenderCfg::DirectionalLit, RenderCfg::DirectionalShadows,
        RenderCfg::MultiPointLit, RenderCfg::IblReflections,
    };
    const int          nPhases = int(sizeof(phases) / sizeof(phases[0]));
    std::vector<float> phaseLuma;
    phaseLuma.reserve(size_t(nPhases));

    // Animation frames do not need CPU pixels. Enabling readback here would
    // wait the GPU and copy the whole swapchain every present (~10× slower).
    gfx->setScreenReadbackEnabled(false);
    gfx->setBackgroundColor(Color(0.14f, 0.15f, 0.17f, 1.f));
    addHud();

    const bool  manualPreview = eve::test::manualPreviewEnabled();
    auto       *timer       = eve::timer::Timer::create();
    const float hz          = viewHz();
    const float secPerPhase = viewSecondsPerPhase();
    const float totalSec    = float(nPhases) * secPerPhase;
    const int   legs        = int(path.size()) - 1;
    std::printf("ClassicScenes[%s] view %.1fs/phase @ %.0f Hz (set EVENGINE_VIEW_SECONDS to change)\n", sceneTag,
                secPerPhase, hz);

    auto dumpHero = [&](bool captureForCaller) {
        const bool writePng = manualPreview || dumpPhaseFrames();
        if (!captureForCaller && !writePng) return;
        applyCam(actors.cam, path[0]);
        if (std::string(sceneTag) == "pbr_chart") {
            for (auto *e : actors.ents) e->transform()->yaw = 0.f;
        }
        applyConfig(actors, RenderCfg::FullLit, polishMetalsForIbl);
        resetRenderControl(gfx);
        gfx->setScreenReadbackEnabled(true);
        warmPresent(gfx);
        if (writePng) {
            eve::image::ImageData *hero = gfx->newImageData();
            REQUIRE(hero != nullptr);
            saveFramePng(_ZEROERR_TEST_CONTEXT, gfx, std::string(sceneTag) + ".png", hero);
            delete hero;
        }
        gfx->setScreenReadbackEnabled(false);
    };
    dumpHero(false);
    const float tFly0 = timer->getTime();

    int frame = 0;
    for (int pi = 0; pi < nPhases; ++pi) {
        applyConfig(actors, phases[pi], polishMetalsForIbl && cfgUsesIbl(phases[pi]));
        std::printf("ClassicScenes[%s] phase=%s\n", sceneTag, cfgName(phases[pi]));

        const float phaseEnd = float(pi + 1) * secPerPhase;
        const float tPhase0  = timer->getTime();
        int         nFrames  = 0;
        const int   frameCap = manualPreview ? std::max(8, int(hz * secPerPhase) * 4) : 3;
        while (nFrames < frameCap) {
            const float now     = timer->getTime();
            const float elapsed = now - tFly0;
            if (manualPreview && elapsed >= phaseEnd && nFrames >= 2) break;

            // Fixed path samples exercise real camera submissions without a viewing hold.
            const float tPath = manualPreview ? (totalSec > 1e-4f ? std::min(elapsed / totalSec, 1.f) : 0.f)
                                              : (float(pi) + float(nFrames) / float(frameCap - 1)) / float(nPhases);
            const float legF  = tPath * float(legs);
            const int   leg   = std::min(legs - 1, int(legF));
            const float u     = legF - float(leg);
            applyCam(actors.cam, lerpKey(path[size_t(leg)], path[size_t(leg + 1)], u));

            if (std::string(sceneTag) == "pbr_chart") {
                for (auto *e : actors.ents) e->transform()->yaw = float(frame) * 0.03f;
            }

            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) break;
            }
            if (manualPreview) paceViewFrame(timer, now, hz);
            ++nFrames;
            ++frame;
        }
        const float phaseDt = timer->getTime() - tPhase0;
        std::printf("ClassicScenes[%s] phase=%s  view %d frames in %.2fs (%.1f fps)\n", sceneTag, cfgName(phases[pi]),
                    nFrames, phaseDt, phaseDt > 1e-4f ? float(nFrames) / phaseDt : 0.f);

        // Snapshot a front-on / path-start key so charts and interiors stay
        // readable. The fly still covers the full path for live viewing.
        applyCam(actors.cam, path[0]);
        if (std::string(sceneTag) == "pbr_chart") {
            for (auto *e : actors.ents) e->transform()->yaw = 0.f;
        }

        gfx->setScreenReadbackEnabled(true);
        warmPresent(gfx);
        const float L = meanLuma(gfx);
        phaseLuma.push_back(L);
        eve::image::ImageData *snap = gfx->newImageData();
        REQUIRE(snap != nullptr);
        if (dumpPhaseFrames())
            saveFramePng(_ZEROERR_TEST_CONTEXT, gfx, std::string(sceneTag) + "_" + cfgName(phases[pi]) + ".png", snap);
        auditCapturedFrame(_ZEROERR_TEST_CONTEXT, snap, gfx, sceneTag, cfgName(phases[pi]));
        delete snap;
        std::printf("ClassicScenes[%s] phase=%s meanLuma=%.4f\n", sceneTag, cfgName(phases[pi]), L);
        gfx->setScreenReadbackEnabled(false);
    }

    // Refresh the FullLit/path-start pixels even without PNG output: callers
    // sample this result, not the preceding IBL phase's cached readback.
    dumpHero(true);

    // Callers (cornell wall sampling) may read pixels after we return.
    gfx->setScreenReadbackEnabled(true);
    return phaseLuma;
}

struct FpsResult {
    int    frames  = 0;
    double seconds = 0.0;
    double avgFps  = 0.0;
    double minFps  = 0.0;
    double maxFps  = 0.0;
};

int perfTimedFrames() {
    const char *env = std::getenv("EVENGINE_PERF_FRAMES");
    if (!env || !env[0]) return 120;
    const int n = std::atoi(env);
    return n > 8 ? n : 8;
}

FpsResult measureFps(Graphics *gfx, SceneActors &actors, const std::vector<CamKey> &path, RenderCfg cfg,
                     bool polishMetals, int warmupFrames, int timedFrames) {
    REQUIRE(actors.cam != nullptr);
    REQUIRE(path.size() >= 2);
    applyConfig(actors, cfg, polishMetals && cfgUsesIbl(cfg));
    gfx->setScreenReadbackEnabled(false);

    const int legs      = int(path.size()) - 1;
    auto      renderOne = [&](int f, int total) {
        const float tPath = (total <= 1) ? 0.f : float(f) / float(total - 1);
        const float legF  = tPath * float(legs);
        const int   leg   = std::min(legs - 1, int(legF));
        const float u     = legF - float(leg);
        applyCam(actors.cam, lerpKey(path[size_t(leg)], path[size_t(leg + 1)], u));
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
        }
    };

    for (int i = 0; i < warmupFrames; ++i) renderOne(i, warmupFrames);

    using Clock      = std::chrono::steady_clock;
    double     maxDt = 0.0;
    double     minDt = 1e9;
    const auto t0    = Clock::now();
    auto       prev  = t0;
    for (int i = 0; i < timedFrames; ++i) {
        renderOne(i, timedFrames);
        const auto   now = Clock::now();
        const double dt  = std::chrono::duration<double>(now - prev).count();
        prev             = now;
        if (dt > maxDt) maxDt = dt;
        if (dt > 0.0 && dt < minDt) minDt = dt;
    }
    const double elapsed = std::chrono::duration<double>(Clock::now() - t0).count();
    FpsResult    r;
    r.frames  = timedFrames;
    r.seconds = elapsed;
    r.avgFps  = (elapsed > 0.0) ? double(timedFrames) / elapsed : 0.0;
    r.minFps  = (maxDt > 0.0) ? 1.0 / maxDt : 0.0;
    r.maxFps  = (minDt > 0.0 && minDt < 1e9) ? 1.0 / minDt : r.avgFps;
    return r;
}

void printFps(const char *sceneTag, RenderCfg cfg, const FpsResult &r) {
    std::printf(
        "ClassicScenes.perf[%s] phase=%-12s  avg=%7.1f fps  min=%6.1f  peak=%7.1f  "
        "(%d frames in %.3fs)\n",
        sceneTag, cfgName(cfg), r.avgFps, r.minFps, r.maxFps, r.frames, r.seconds);
}

double benchAllConfigs(Graphics *gfx, SceneActors &actors, const std::vector<CamKey> &path, const char *sceneTag,
                       bool polishMetals) {
    gfx->setVSync(false);
    gfx->setScreenReadbackEnabled(false);
    gfx->setBackgroundColor(Color(0.14f, 0.15f, 0.17f, 1.f));

    const RenderCfg phases[] = {
        RenderCfg::AmbientOnly,   RenderCfg::DirectionalLit, RenderCfg::DirectionalShadows,
        RenderCfg::MultiPointLit, RenderCfg::IblReflections,
    };
    const int warmup  = 20;
    const int timed   = perfTimedFrames();
    double    bestAvg = 0.0;
    for (RenderCfg cfg : phases) {
        const FpsResult r = measureFps(gfx, actors, path, cfg, polishMetals, warmup, timed);
        printFps(sceneTag, cfg, r);
        bestAvg = std::max(bestAvg, r.avgFps);
    }
    std::printf("ClassicScenes.perf[%s] best_avg=%.1f fps  vsync=%d  %dx%d\n", sceneTag, bestAvg,
                gfx->isVSync() ? 1 : 0, gfx->getWidth(), gfx->getHeight());
    return bestAvg;
}

double benchGltfOrbitScene(Graphics *gfx, const char *relDir, const char *gltfFile, const char *tag,
                           float defaultMetallic, float defaultRoughness, bool usePbrFactors, bool polishMetalsForIbl,
                           float elev, float distScale, int minMeshes, int minTextures) {
    const std::string dir = pathBesideThisSource(relDir);
    if (!fileExists(dir + "/" + gltfFile)) {
        std::printf("ClassicScenes.perf[%s]: skip (missing %s/%s)\n", tag, dir.c_str(), gltfFile);
        return 0.0;
    }

    eve::model3d::ModelData *md = nullptr;
    try {
        resetScene3D();
        md = loadModelFromDir(dir, gltfFile);
        REQUIRE(md != nullptr);

        SceneActors actors;
        int         texLoaded = 0;
        REQUIRE(spawnModel(gfx, md, dir, actors, defaultMetallic, defaultRoughness, &texLoaded, usePbrFactors) >=
                minMeshes);
        REQUIRE(actors.bounds.valid);
        if (minTextures > 0) REQUIRE(texLoaded >= minTextures);

        const float rad0      = std::max(1e-4f, actors.bounds.radius());
        const float targetRad = 2.0f;
        if (std::fabs(rad0 - targetRad) > 0.15f) {
            const float s  = targetRad / rad0;
            const float cx = actors.bounds.centerX();
            const float cy = actors.bounds.centerY();
            const float cz = actors.bounds.centerZ();
            for (auto *e : actors.ents) {
                auto xf = e->transform();
                xf->x   = cx + (xf->x - cx) * s;
                xf->y   = cy + (xf->y - cy) * s;
                xf->z   = cz + (xf->z - cz) * s;
                xf->sx *= s;
                xf->sy *= s;
                xf->sz *= s;
            }
            Bounds nb;
            nb.expand(cx - targetRad, cy - targetRad, cz - targetRad);
            nb.expand(cx + targetRad, cy + targetRad, cz + targetRad);
            actors.bounds = nb;
        }

        std::printf("ClassicScenes.perf[%s] meshes=%zu textures=%d radius=%.3f\n", tag, actors.ents.size(), texLoaded,
                    actors.bounds.radius());

        actors.cam                = Camera3D::createCamera();
        const float rad           = std::max(0.5f, actors.bounds.radius());
        actors.cam->data()->nearZ = std::max(0.05f, rad * 0.01f);
        actors.cam->data()->farZ  = std::max(40.f, rad * 25.f);
        actors.env                = makeStudioCubemap(gfx);
        setupLights(actors, actors.bounds);
        setSunColor(actors, 1.f, 0.97f, 0.92f, 3.5f);
        actors.pointA->setColor(1.f, 0.55f, 0.35f, 7.f);
        actors.pointB->setColor(0.35f, 0.55f, 1.f, 6.5f);

        auto         path = makeOrbitPath(actors.bounds, elev, distScale);
        const double best = benchAllConfigs(gfx, actors, path, tag, polishMetalsForIbl);
        return best;
    } catch (const std::exception &ex) {
        std::printf("ClassicScenes.perf[%s]: skip after error: %s\n", tag, ex.what());
        return 0.0;
    }
}

void expectLightingResponse(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const std::vector<float> &L) {
    REQUIRE(L.size() >= 5);
    // Soft metals (DamagedHelmet etc.) can stay dark / slightly darker under
    // directional-only because diffuse is tiny; accept either a directional lift
    // or a clear IBL response over ambient.
    const bool dirResponds = L[1] > L[0] + 0.004f;
    const bool iblResponds = L[4] > L[0] + 0.02f;
    const bool lightingOk  = dirResponds || iblResponds;
    REQUIRE(lightingOk);
    // Multi-point and IBL should produce non-black frames.
    REQUIRE(L[3] > 0.02f);
    REQUIRE(L[4] > 0.02f);
}

}  // namespace

// Defined below; forward-declared so early ClassicScenes cases can soft-skip via it.
bool runGltfOrbitScene(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const char *relDir, const char *gltfFile,
                       const char *tag, float defaultMetallic, float defaultRoughness, bool usePbrFactors,
                       bool polishMetalsForIbl, float elev = 0.35f, float distScale = 1.7f, int minMeshes = 1,
                       int minTextures = 0, int winW = 640, int winH = 480);

TEST_CASE("ClassicScenes.cornell.flythroughConfigs") {
    const std::string dir = pathBesideThisSource("assets/classic/cornell");
    const std::string obj = "CornellBox-Original.obj";
    REQUIRE(fileExists(dir + "/" + obj));

    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    auto *md = loadModelFromDir(dir, obj);
    REQUIRE(md != nullptr);

    SceneActors actors;
    REQUIRE(spawnModel(gfx, md, dir, actors, 0.02f, 0.75f) >= 1);
    REQUIRE(actors.bounds.valid);

    actors.cam                = Camera3D::createCamera();
    actors.cam->data()->nearZ = 0.05f;
    actors.cam->data()->farZ  = 50.f;
    actors.env                = makeStudioCubemap(gfx);
    setupLights(actors, actors.bounds);
    // Ceiling-ish sun for the box.
    actors.sun->setDirection(0.15f, 1.f, 0.05f);
    setSunColor(actors, 1.f, 0.97f, 0.92f, 2.8f);
    actors.pointA->setPosition(actors.bounds.centerX(), actors.bounds.maxY - 0.15f, actors.bounds.centerZ());
    actors.pointA->setColor(1.f, 1.f, 0.95f, 6.f);
    actors.pointA->setRadius(4.f);

    auto path = makeCornellPath(actors.bounds);
    auto L    = flyThroughConfigs(_ZEROERR_TEST_CONTEXT, gfx, actors, path, "cornell", /*polishMetalsForIbl=*/true);
    expectLightingResponse(_ZEROERR_TEST_CONTEXT, L);
    snapStaticConfigGrid(_ZEROERR_TEST_CONTEXT, gfx, actors, "cornell", /*polishMetals=*/true);
    if (dumpPhaseFrames()) dumpCornellShadowSweep(_ZEROERR_TEST_CONTEXT, gfx, actors, path);

    // Red vs green walls should remain distinguishable under directional light.
    applyConfig(actors, RenderCfg::DirectionalLit, false);
    applyCam(actors.cam, path[0]);
    warmPresent(gfx);
    const int   w     = gfx->getWidth();
    const int   h     = gfx->getHeight();
    const Color left  = gfx->getPixel(w / 5, h / 2);
    const Color right = gfx->getPixel((w * 4) / 5, h / 2);
    CHECK(left.r + 0.02f > left.g);    // left wall tends red
    CHECK(right.g + 0.01f > right.r);  // right wall tends green

    // Orientation: short box sits on the floor — lower third of the frame should
    // carry more of the box silhouette than the upper third (ceiling).
    const float lower = meanLuma(gfx);  // reused warm frame
    (void)lower;
    double sumTop = 0.0, sumBot = 0.0;
    int    nTop = 0, nBot = 0;
    for (int y = 0; y < h / 4; y += 4) {
        for (int x = w / 3; x < (w * 2) / 3; x += 4) {
            sumTop += luma(gfx->getPixel(x, y));
            ++nTop;
        }
    }
    for (int y = (h * 3) / 4; y < h; y += 4) {
        for (int x = w / 3; x < (w * 2) / 3; x += 4) {
            sumBot += luma(gfx->getPixel(x, y));
            ++nBot;
        }
    }
    const float topL = nTop ? float(sumTop / nTop) : 0.f;
    const float botL = nBot ? float(sumBot / nBot) : 0.f;
    std::printf("ClassicScenes[cornell] orientation topL=%.3f botL=%.3f\n", topL, botL);
    // Floor region (bottom) must not be empty/black while ceiling (top) is lit alone —
    // both contribute under directional light when the camera looks into the box.
    REQUIRE(botL > 0.02f);
    REQUIRE(topL > 0.01f);

    win->close();
}

TEST_CASE("ClassicScenes.pbrChart.flythroughConfigs") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    SceneActors actors;
    spawnMetalRoughnessChart(gfx, actors);
    REQUIRE(actors.ents.size() == 17);  // 16 spheres + ground
    REQUIRE(actors.bounds.valid);

    actors.cam                = Camera3D::createCamera();
    actors.cam->data()->nearZ = 0.05f;
    actors.cam->data()->farZ  = 40.f;
    actors.env                = makeStudioCubemap(gfx);
    setupLights(actors, actors.bounds);
    setSunColor(actors, 1.f, 0.97f, 0.92f, 3.2f);
    // Glancing sun so sphere umbrae stretch across the disk in path[0] (a high
    // sun hides them behind the spheres).
    actors.sun->setDirection(0.9f, 0.45f, 0.25f);

    auto path = makeOrbitPath(actors.bounds, 0.55f, 1.65f);
    auto L    = flyThroughConfigs(_ZEROERR_TEST_CONTEXT, gfx, actors, path, "pbr_chart", /*polishMetalsForIbl=*/false);
    expectLightingResponse(_ZEROERR_TEST_CONTEXT, L);
    snapStaticConfigGrid(_ZEROERR_TEST_CONTEXT, gfx, actors, "pbr_chart", /*polishMetals=*/false);
    if (dumpPhaseFrames()) dumpPbrChartGroundDiag(_ZEROERR_TEST_CONTEXT, gfx, actors, path[0]);

    // IBL phase should brighten metals relative to ambient-only.
    CHECK(L[4] > L[0] + 0.01f);

    win->close();
}

TEST_CASE("ClassicScenes.sponza.flythroughConfigs") {
    const std::string dir  = pathBesideThisSource("assets/classic/sponza/glTF");
    const std::string gltf = "Sponza.gltf";
    if (!fileExists(dir + "/" + gltf)) {
        std::printf(
            "ClassicScenes.sponza: missing %s — run scripts/download_classic_scenes.sh "
            "(or cmake --build --target download_classic_scenes)\n",
            (dir + "/" + gltf).c_str());
        return;
    }

    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 960, 540);
    resetScene3D();

    auto *md = loadModelFromDir(dir, gltf);
    REQUIRE(md != nullptr);
    REQUIRE(md->getMeshCount() >= 10);

    SceneActors actors;
    int         texLoaded = 0;
    REQUIRE(spawnModel(gfx, md, dir, actors, 0.02f, 0.7f, &texLoaded,
                       /*useMaterialPbrFactors=*/false) >= 10);
    REQUIRE(actors.bounds.valid);
    std::printf("ClassicScenes[sponza] bounds center=(%.2f,%.2f,%.2f) radius=%.2f meshes=%zu textures=%d\n",
                actors.bounds.centerX(), actors.bounds.centerY(), actors.bounds.centerZ(), actors.bounds.radius(),
                actors.ents.size(), texLoaded);
    // Ensure we actually bound albedo maps — otherwise the atrium stays pitch black.
    REQUIRE(texLoaded >= 5);

    actors.cam                = Camera3D::createCamera();
    const float rad           = std::max(1.f, actors.bounds.radius());
    actors.cam->data()->nearZ = std::max(0.05f, rad * 0.002f);
    actors.cam->data()->farZ  = std::max(60.f, rad * 4.f);
    actors.env                = makeStudioCubemap(gfx);
    setupLights(actors, actors.bounds);
    actors.sun->setDirection(0.25f, 1.f, 0.2f);
    setSunColor(actors, 1.f, 0.98f, 0.94f, 3.2f);
    actors.pointA->setColor(1.f, 0.75f, 0.45f, 7.f);
    actors.pointA->setRadius(rad * 1.2f);
    actors.pointB->setColor(0.45f, 0.65f, 1.f, 6.f);
    actors.pointB->setRadius(rad * 1.2f);

    auto path = makeSponzaPath(actors.bounds);
    auto L    = flyThroughConfigs(_ZEROERR_TEST_CONTEXT, gfx, actors, path, "sponza", /*polishMetalsForIbl=*/false);
    expectLightingResponse(_ZEROERR_TEST_CONTEXT, L);

    // Shadows should not black out the whole atrium vs plain directional.
    REQUIRE(L[2] > 0.02f);
    REQUIRE(std::abs(L[2] - L[1]) < 0.45f);

    win->close();
}

TEST_CASE("ClassicScenes.damagedHelmet.flythroughConfigs") {
    // Keep dielectric-friendly defaults: without sampling the MR map, glTF's
    // metallicFactor≈1 turns the helmet into a near-black mirror under dir-only.
    if (!runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/damaged_helmet/glTF", "DamagedHelmet.gltf", "helmet",
                           /*defaultMetallic=*/0.05f, /*defaultRoughness=*/0.4f,
                           /*usePbrFactors=*/false, /*polishMetalsForIbl=*/false, 0.25f, 1.8f)) {
        return;
    }
}

/** Soft-skip helper for downloaded Khronos glTF sample scenes. */
bool runGltfOrbitScene(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const char *relDir, const char *gltfFile,
                       const char *tag, float defaultMetallic, float defaultRoughness, bool usePbrFactors,
                       bool polishMetalsForIbl, float elev, float distScale, int minMeshes, int minTextures, int winW,
                       int winH) {
    const std::string dir = pathBesideThisSource(relDir);
    if (!fileExists(dir + "/" + gltfFile)) {
        std::printf("ClassicScenes.%s: missing %s/%s — run scripts/download_classic_scenes.sh\n", tag, dir.c_str(),
                    gltfFile);
        return false;
    }

    eve::window::Window     *win = nullptr;
    Graphics                *gfx = nullptr;
    eve::model3d::ModelData *md  = nullptr;
    try {
        openGfxWindow(win, gfx, winW, winH);
        resetScene3D();

        md = loadModelFromDir(dir, gltfFile);
        REQUIRE(md != nullptr);
        REQUIRE(md->getMeshCount() >= minMeshes);

        SceneActors actors;
        int         texLoaded = 0;
        REQUIRE(spawnModel(gfx, md, dir, actors, defaultMetallic, defaultRoughness, &texLoaded, usePbrFactors) >=
                minMeshes);
        REQUIRE(actors.bounds.valid);
        if (minTextures > 0) REQUIRE(texLoaded >= minTextures);

        // Normalize asset scale so orbit framing is consistent across cm-scale and
        // meter-scale Khronos samples (BoomBox ~100 units, Avocado ≪ 1, etc.).
        const float rad0      = std::max(1e-4f, actors.bounds.radius());
        const float targetRad = 2.0f;
        if (std::fabs(rad0 - targetRad) > 0.15f) {
            const float s  = targetRad / rad0;
            const float cx = actors.bounds.centerX();
            const float cy = actors.bounds.centerY();
            const float cz = actors.bounds.centerZ();
            for (auto *e : actors.ents) {
                auto xf = e->transform();
                xf->x   = cx + (xf->x - cx) * s;
                xf->y   = cy + (xf->y - cy) * s;
                xf->z   = cz + (xf->z - cz) * s;
                xf->sx *= s;
                xf->sy *= s;
                xf->sz *= s;
            }
            Bounds nb;
            nb.expand(cx - targetRad, cy - targetRad, cz - targetRad);
            nb.expand(cx + targetRad, cy + targetRad, cz + targetRad);
            actors.bounds = nb;
        }

        std::printf("ClassicScenes[%s] meshes=%zu textures=%d radius=%.3f\n", tag, actors.ents.size(), texLoaded,
                    actors.bounds.radius());

        actors.cam                = Camera3D::createCamera();
        const float rad           = std::max(0.5f, actors.bounds.radius());
        actors.cam->data()->nearZ = std::max(0.05f, rad * 0.01f);
        actors.cam->data()->farZ  = std::max(40.f, rad * 25.f);
        actors.env                = makeStudioCubemap(gfx);
        setupLights(actors, actors.bounds);
        // Stronger key light so textured dielectrics clear the ambient→lit delta.
        setSunColor(actors, 1.f, 0.97f, 0.92f, 3.5f);
        actors.pointA->setColor(1.f, 0.55f, 0.35f, 7.f);
        actors.pointB->setColor(0.35f, 0.55f, 1.f, 6.5f);

        auto path = makeOrbitPath(actors.bounds, elev, distScale);
        auto L    = flyThroughConfigs(_ZEROERR_TEST_CONTEXT, gfx, actors, path, tag, polishMetalsForIbl);
        expectLightingResponse(_ZEROERR_TEST_CONTEXT, L);

        md = nullptr;
        win->close();
        win = nullptr;
        return true;
    } catch (...) {
        if (win) win->close();
        throw;
    }
}

TEST_CASE("ClassicScenes.flightHelmet.flythroughConfigs") {
    runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/flight_helmet/glTF", "FlightHelmet.gltf", "flight_helmet",
                      /*defaultMetallic=*/0.05f, /*defaultRoughness=*/0.55f,
                      /*usePbrFactors=*/false, /*polishMetalsForIbl=*/false, 0.35f, 1.8f,
                      /*minMeshes=*/5, /*minTextures=*/3);
}

TEST_CASE("ClassicScenes.boomBox.flythroughConfigs") {
    runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/boom_box/glTF", "BoomBox.gltf", "boom_box", 0.05f, 0.45f,
                      /*usePbrFactors=*/false, /*polishMetalsForIbl=*/false, 0.3f, 1.9f);
}

TEST_CASE("ClassicScenes.scifiHelmet.flythroughConfigs") {
    runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/scifi_helmet/glTF", "SciFiHelmet.gltf", "scifi_helmet",
                      0.05f, 0.4f,
                      /*usePbrFactors=*/false, /*polishMetalsForIbl=*/false, 0.25f, 1.8f);
}

TEST_CASE("ClassicScenes.metalRoughSpheres.flythroughConfigs") {
    runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/metal_rough_spheres/glTF", "MetalRoughSpheres.gltf",
                      "metal_rough_spheres", 0.05f, 0.5f, /*usePbrFactors=*/true,
                      /*polishMetalsForIbl=*/false, 0.45f, 1.7f, /*minMeshes=*/1,
                      /*minTextures=*/0);
}

TEST_CASE("ClassicScenes.suzanne.flythroughConfigs") {
    // Blender monkey — silhouette / orientation / lit response.
    if (!runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/suzanne/glTF", "Suzanne.gltf", "suzanne", 0.05f,
                           0.55f,
                           /*usePbrFactors=*/false, /*polishMetalsForIbl=*/true, 0.35f, 1.9f)) {
        return;
    }
}

TEST_CASE("ClassicScenes.duck.flythroughConfigs") {
    runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/duck/glTF", "Duck.gltf", "duck", 0.02f, 0.65f,
                      /*usePbrFactors=*/false, /*polishMetalsForIbl=*/false, 0.45f, 1.9f);
}

TEST_CASE("ClassicScenes.avocado.flythroughConfigs") {
    runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/avocado/glTF", "Avocado.gltf", "avocado", 0.02f, 0.7f,
                      /*usePbrFactors=*/false, /*polishMetalsForIbl=*/false, 0.35f, 1.9f,
                      /*minMeshes=*/1, /*minTextures=*/1);
}

TEST_CASE("ClassicScenes.waterBottle.flythroughConfigs") {
    runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/water_bottle/glTF", "WaterBottle.gltf", "water_bottle",
                      0.05f, 0.35f,
                      /*usePbrFactors=*/false, /*polishMetalsForIbl=*/false, 0.4f, 1.9f);
}

TEST_CASE("ClassicScenes.lantern.flythroughConfigs") {
    runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/lantern/glTF", "Lantern.gltf", "lantern", 0.05f, 0.55f,
                      /*usePbrFactors=*/false, /*polishMetalsForIbl=*/false, 0.45f, 1.85f,
                      /*minMeshes=*/1, /*minTextures=*/1);
}

TEST_CASE("ClassicScenes.antiqueCamera.flythroughConfigs") {
    runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/antique_camera/glTF", "AntiqueCamera.gltf",
                      "antique_camera", 0.08f, 0.5f,
                      /*usePbrFactors=*/false, /*polishMetalsForIbl=*/false, 0.35f, 1.9f, /*minMeshes=*/1,
                      /*minTextures=*/1);
}

TEST_CASE("ClassicScenes.cesiumMilkTruck.flythroughConfigs") {
    runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/cesium_milk_truck/glTF", "CesiumMilkTruck.gltf",
                      "cesium_milk_truck", 0.05f, 0.6f,
                      /*usePbrFactors=*/false,
                      /*polishMetalsForIbl=*/false, 0.4f, 2.0f, /*minMeshes=*/2,
                      /*minTextures=*/1);
}

TEST_CASE("ClassicScenes.barramundiFish.flythroughConfigs") {
    runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/barramundi_fish/glTF", "BarramundiFish.gltf",
                      "barramundi_fish", 0.02f, 0.55f,
                      /*usePbrFactors=*/false,
                      /*polishMetalsForIbl=*/false, 0.3f, 1.9f, /*minMeshes=*/1,
                      /*minTextures=*/1);
}

TEST_CASE("ClassicScenes.corset.flythroughConfigs") {
    runGltfOrbitScene(_ZEROERR_TEST_CONTEXT, "assets/classic/corset/glTF", "Corset.gltf", "corset", 0.05f, 0.5f,
                      /*usePbrFactors=*/false, /*polishMetalsForIbl=*/false, 0.35f, 1.85f,
                      /*minMeshes=*/1, /*minTextures=*/1);
}

TEST_CASE("ClassicScenes.perf.maxFps") {
    double      globalBest = 0.0;
    const char *bestTag    = "";
    auto        note       = [&](const char *tag, double fps) {
        if (fps > globalBest) {
            globalBest = fps;
            bestTag    = tag;
        }
    };

    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 960, 540);
    gfx->setVSync(false);
    gfx->setScreenReadbackEnabled(false);

    auto gltf = [&](const char *relDir, const char *file, const char *tag, float met, float rough, bool pbr,
                    bool polish, float elev, float dist, int minM, int minT) {
        note(tag, benchGltfOrbitScene(gfx, relDir, file, tag, met, rough, pbr, polish, elev, dist, minM, minT));
    };

    try {
        resetScene3D();
        SceneActors actors;
        spawnMetalRoughnessChart(gfx, actors);
        actors.cam                = Camera3D::createCamera();
        actors.cam->data()->nearZ = 0.05f;
        actors.cam->data()->farZ  = 40.f;
        actors.env                = makeStudioCubemap(gfx);
        setupLights(actors, actors.bounds);
        note("pbr_chart", benchAllConfigs(gfx, actors, makeOrbitPath(actors.bounds, 0.55f, 1.9f), "pbr_chart", false));
    } catch (const std::exception &ex) {
        std::printf("ClassicScenes.perf[pbr_chart]: skip after error: %s\n", ex.what());
    }

    try {
        const std::string dir = pathBesideThisSource("assets/classic/cornell");
        const std::string obj = "CornellBox-Original.obj";
        if (fileExists(dir + "/" + obj)) {
            resetScene3D();
            auto       *md = loadModelFromDir(dir, obj);
            SceneActors actors;
            spawnModel(gfx, md, dir, actors, 0.02f, 0.75f);
            actors.cam                = Camera3D::createCamera();
            actors.cam->data()->nearZ = 0.05f;
            actors.cam->data()->farZ  = 50.f;
            actors.env                = makeStudioCubemap(gfx);
            setupLights(actors, actors.bounds);
            note("cornell", benchAllConfigs(gfx, actors, makeCornellPath(actors.bounds), "cornell", true));
        }
    } catch (const std::exception &ex) {
        std::printf("ClassicScenes.perf[cornell]: skip after error: %s\n", ex.what());
    }

    try {
        const std::string dir = pathBesideThisSource("assets/classic/sponza/glTF");
        if (fileExists(dir + "/Sponza.gltf")) {
            resetScene3D();
            auto       *md = loadModelFromDir(dir, "Sponza.gltf");
            SceneActors actors;
            int         texLoaded = 0;
            spawnModel(gfx, md, dir, actors, 0.02f, 0.7f, &texLoaded, false);
            actors.cam                = Camera3D::createCamera();
            const float rad           = std::max(1.f, actors.bounds.radius());
            actors.cam->data()->nearZ = std::max(0.05f, rad * 0.002f);
            actors.cam->data()->farZ  = std::max(60.f, rad * 4.f);
            actors.env                = makeStudioCubemap(gfx);
            setupLights(actors, actors.bounds);
            actors.sun->setDirection(0.25f, 1.f, 0.2f);
            setSunColor(actors, 1.f, 0.98f, 0.94f, 6.f);
            note("sponza", benchAllConfigs(gfx, actors, makeSponzaPath(actors.bounds), "sponza", false));
        } else {
            std::printf("ClassicScenes.perf[sponza]: skip (asset not downloaded)\n");
        }
    } catch (const std::exception &ex) {
        std::printf("ClassicScenes.perf[sponza]: skip after error: %s\n", ex.what());
    }

    gltf("assets/classic/damaged_helmet/glTF", "DamagedHelmet.gltf", "helmet", 0.05f, 0.4f, false, false, 0.25f, 2.1f,
         1, 0);
    gltf("assets/classic/flight_helmet/glTF", "FlightHelmet.gltf", "flight_helmet", 0.05f, 0.55f, false, false, 0.35f,
         1.8f, 5, 3);
    gltf("assets/classic/boom_box/glTF", "BoomBox.gltf", "boom_box", 0.05f, 0.45f, false, false, 0.3f, 1.9f, 1, 0);
    gltf("assets/classic/scifi_helmet/glTF", "SciFiHelmet.gltf", "scifi_helmet", 0.05f, 0.4f, false, false, 0.25f, 1.8f,
         1, 0);
    gltf("assets/classic/metal_rough_spheres/glTF", "MetalRoughSpheres.gltf", "metal_rough_spheres", 0.05f, 0.5f, true,
         false, 0.45f, 1.7f, 1, 0);
    gltf("assets/classic/suzanne/glTF", "Suzanne.gltf", "suzanne", 0.05f, 0.55f, false, true, 0.35f, 1.9f, 1, 0);
    gltf("assets/classic/duck/glTF", "Duck.gltf", "duck", 0.02f, 0.65f, false, false, 0.45f, 1.9f, 1, 0);
    gltf("assets/classic/avocado/glTF", "Avocado.gltf", "avocado", 0.02f, 0.7f, false, false, 0.35f, 1.9f, 1, 1);
    gltf("assets/classic/water_bottle/glTF", "WaterBottle.gltf", "water_bottle", 0.05f, 0.35f, false, false, 0.4f, 1.9f,
         1, 0);
    gltf("assets/classic/lantern/glTF", "Lantern.gltf", "lantern", 0.05f, 0.55f, false, false, 0.45f, 1.85f, 1, 1);
    gltf("assets/classic/antique_camera/glTF", "AntiqueCamera.gltf", "antique_camera", 0.08f, 0.5f, false, false, 0.35f,
         1.9f, 1, 1);
    gltf("assets/classic/cesium_milk_truck/glTF", "CesiumMilkTruck.gltf", "cesium_milk_truck", 0.05f, 0.6f, false,
         false, 0.4f, 2.0f, 2, 1);
    gltf("assets/classic/barramundi_fish/glTF", "BarramundiFish.gltf", "barramundi_fish", 0.02f, 0.55f, false, false,
         0.3f, 1.9f, 1, 1);
    gltf("assets/classic/corset/glTF", "Corset.gltf", "corset", 0.05f, 0.5f, false, false, 0.35f, 1.85f, 1, 1);

    win->close();
    std::printf("ClassicScenes.perf MAX avg fps = %.1f  (%s)  960x540 vsync=off Debug\n", globalBest, bestTag);
    REQUIRE(globalBest > 1.0);
}
}  // namespace test_ClassicScenes

// Contract section: ClusteredLighting
#include "PrivateTestScope.h"


#include <glm/gtc/matrix_transform.hpp>
#include "graphics/ClusteredLight.h"
// ClusteredLighting.cpp
namespace test_ClusteredLighting {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

[[maybe_unused]] static float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }


[[maybe_unused]] static void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]  = *it;
            data->active = false;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d]   = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
}

[[maybe_unused]] static Texture *makeSolidGray(Graphics *gfx, uint8_t g) {
    uint8_t px[4] = {g, g, g, 255};
    return gfx->newTexture(1, 1, px);
}


TEST_CASE("ClusteredLighting.manyPointLightsBrightenCenter") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 12);
    REQUIRE(mesh != nullptr);

    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.04f, 0.04f, 0.04f);

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(0.05f);
    ent->setRoughness(0.55f);

    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;

    // >8 lights triggers clustered path.
    std::vector<Light3D *> lights;
    for (int i = 0; i < 24; ++i) {
        auto       *L = Light3D::createLight("point");
        const float a = float(i) * (6.2831853f / 24.f);
        L->setPosition(std::cos(a) * 1.2f, 0.3f, std::sin(a) * 1.2f);
        L->setColor(1.f, 1.f, 1.f, 1.8f);
        L->setRadius(3.5f);
        lights.push_back(L);
    }
    auto *dir = Light3D::createLight("dir");
    dir->setDirection(0.f, 0.4f, 1.f);
    dir->setColor(0.15f, 0.15f, 0.15f, 1.f);

    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_on = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    for (auto *L : lights) L->setEnabled(false);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_off = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    REQUIRE(L_on > L_off + 0.04f);

    if (eve::test::manualPreviewEnabled()) {
        // Re-enable the ring of lights and spin so clustered lighting is visible.
        for (auto *L : lights) L->setEnabled(true);
        for (int i = 0; i < 60; ++i) {
            ent->transform()->yaw = float(i) * 0.04f;
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) break;
            }
            SDL_Delay(16);
        }
    }

    dir->setEnabled(false);
    win->close();
}
}  // namespace test_ClusteredLighting

// Contract section: GlobalIllumination
#include "PrivateTestScope.h"


// GlobalIllumination.cpp
namespace test_GlobalIllumination {
using namespace eve::graphics;

namespace {


Texture *makePackedAlbedoDepth(Graphics *gfx, int w, int h) {
    std::vector<uint8_t> px(size_t(w * h * 4));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i    = size_t((y * w + x) * 4);
            const bool   wall = (x > w / 3 && x < w / 2);
            px[i + 0]         = wall ? 220 : 40;
            px[i + 1]         = wall ? 40 : 40;
            px[i + 2]         = wall ? 40 : 40;
            px[i + 3]         = wall ? 80 : 200;
        }
    }
    return gfx->newTexture(w, h, px.data());
}

}  // namespace

TEST_CASE("gi.qualityAndApplyFromDepth") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);

    GlobalIllumination *raw = gfx->newGlobalIllumination();
    REQUIRE(raw != nullptr);
    std::unique_ptr<GlobalIllumination> gi(raw);
    REQUIRE(gi->getShader() != nullptr);
    CHECK(gi->getQuality() == "medium");
    CHECK(gi->getSampleCount() >= 8);

    gi->setQuality("low");
    CHECK(gi->getQuality() == "low");
    CHECK(gi->getSampleCount() == 8);
    gi->setQuality("high");
    CHECK(gi->getSampleCount() == 24);
    gi->setQuality("nope");
    CHECK(gi->getQuality() == "medium");

    gi->setQuality("low");
    gi->setIntensity(0.8f);
    gi->setRadius(1.1f);
    gi->setLightDirection(0.4f, 1.f, 0.3f);
    gi->setLightColor(1.f, 0.9f, 0.7f);
    gi->setCamera(0.f, 1.f, 4.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 60.f, 320.f / 240.f, 0.1f, 40.f);

    Texture *packed = makePackedAlbedoDepth(gfx, 64, 48);
    REQUIRE(packed != nullptr);
    Canvas *dest = gfx->newCanvas(64, 48);
    REQUIRE(dest != nullptr);
    gi->applyFromDepthTo(gfx, packed, dest);
    REQUIRE(dest->getTexture() != nullptr);

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    CHECK(rc->supports("ao"));
    CHECK(rc->supports("gi"));
    CHECK(rc->isEnabled("gi"));
    CHECK(rc->isEnabled("ao"));

    win->close();
}
}  // namespace test_GlobalIllumination

// Contract section: IBL
#include "PrivateTestScope.h"


// IBL.cpp
namespace test_IBL {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;


using namespace eve::graphics;

namespace {


EVE_DEFINE_PATH_BESIDE_SOURCE()

std::vector<char> readBinaryFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}


void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]        = *it;
            data->active       = false;
            data->envMap       = nullptr;
            data->envIntensity = 1.f;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d]   = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
}

Texture *makeSolidGray(Graphics *gfx, uint8_t v) {
    const uint8_t px[4] = {v, v, v, 255};
    return gfx->newTexture(1, 1, px);
}

float luma(const Color &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

/** All faces the same solid RGBA — strong specular tint for metals. */
Texture *makeSolidCubemap(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b, int face = 4) {
    std::vector<uint8_t> faces(size_t(face) * size_t(face) * 4u * 6u);
    for (size_t i = 0; i < faces.size(); i += 4) {
        faces[i + 0] = r;
        faces[i + 1] = g;
        faces[i + 2] = b;
        faces[i + 3] = 255;
    }
    return gfx->newCubemap(face, faces.data());
}

/**
 * Per-face solid colors in +X,-X,+Y,-Y,+Z,-Z order.
 * With the default camera at +Z looking at the origin, the sphere center
 * reflects roughly toward +Z — so that face dominates a smooth metal.
 */
Texture *makeFaceColoredCubemap(Graphics *gfx, const uint8_t faceRgb[6][3], int face = 8) {
    const size_t         faceBytes = size_t(face) * size_t(face) * 4u;
    std::vector<uint8_t> faces(faceBytes * 6u);
    for (int f = 0; f < 6; ++f) {
        uint8_t *dst = faces.data() + size_t(f) * faceBytes;
        for (size_t i = 0; i < faceBytes; i += 4) {
            dst[i + 0] = faceRgb[f][0];
            dst[i + 1] = faceRgb[f][1];
            dst[i + 2] = faceRgb[f][2];
            dst[i + 3] = 255;
        }
    }
    return gfx->newCubemap(face, faces.data());
}

/** Load 128^2 PNG faces (px,nx,py,ny,pz,nz) under test/Textures/env/<name>/. */
Texture *loadEnvCubemap(Graphics *gfx, const char *name) {
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    static const char           *kFaces[6]   = {"px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"};

    int                  faceSize = 0;
    std::vector<uint8_t> packed;
    for (int f = 0; f < 6; ++f) {
        const std::string path = pathBesideThisSource((std::string("Textures/env/") + name + "/" + kFaces[f]).c_str());
        auto              raw  = readBinaryFile(path);
        REQUIRE(!raw.empty());
        eve::data::ByteData                    bytes(raw.data(), raw.size());
        std::unique_ptr<eve::image::ImageData> img(eve::image::Image::create()->newImageData(&bytes));
        REQUIRE(img.get() != nullptr);
        REQUIRE(img->getFormat() == std::string("RGBA8"));
        if (faceSize == 0) {
            faceSize = img->getWidth();
            REQUIRE(faceSize > 0);
            REQUIRE(img->getHeight() == faceSize);
            packed.resize(size_t(faceSize) * size_t(faceSize) * 4u * 6u);
        } else {
            REQUIRE(img->getWidth() == faceSize);
            REQUIRE(img->getHeight() == faceSize);
        }
        const size_t faceBytes = size_t(faceSize) * size_t(faceSize) * 4u;
        std::memcpy(packed.data() + size_t(f) * faceBytes, img->getData(), faceBytes);
    }
    return gfx->newCubemap(faceSize, packed.data());
}

void addHud(Graphics *) {
    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r      = 0.f;
    hud->sprite()->g      = 0.f;
    hud->sprite()->b      = 0.f;
}

void warmPresent(Graphics *gfx) {
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
}

/** Spin the given entity so IBL / env reflections are visibly readable (~1s). */
void previewRotating(Graphics *gfx, Renderable3D *ent, int ms = 1000) {
    if (!eve::test::manualPreviewEnabled()) return;
    const int frames = (ms >= 16) ? (ms / 16) : 1;
    for (int i = 0; i < frames; ++i) {
        if (ent != nullptr) ent->transform()->yaw = float(i) * 0.04f;
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
        }
        SDL_Delay(16);
    }
}

}  // namespace

TEST_CASE("IBL.metallicPicksUpEnvColor") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 16);
    REQUIRE(mesh != nullptr);

    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.03f, 0.03f, 0.03f);

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(1.0f);
    ent->setRoughness(0.08f);

    addHud(gfx);

    // Kill direct light so specular IBL dominates metals.
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);
    warmPresent(gfx);
    const Color c0 = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    Texture *env = makeSolidCubemap(gfx, 240, 40, 40);
    cam->setEnvMap(env);
    cam->setEnvIntensity(1.5f);
    warmPresent(gfx);
    const Color c1 = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    // Red env should lift R more than G/B on a metal sphere.
    REQUIRE(c1.r > c0.r + 0.04f);
    REQUIRE(c1.r > c1.g + 0.03f);
    REQUIRE(c1.r > c1.b + 0.03f);

    // Hold the red-env metal sphere so the tint is visible while debugging.
    previewRotating(gfx, ent);

    cam->setEnvIntensity(0.f);
    warmPresent(gfx);
    const Color c2 = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(std::abs(c2.r - c0.r) < 0.05f);

    win->close();
}

TEST_CASE("IBL.newCubemapCreatesSixLayers") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    Texture *cube = makeSolidCubemap(gfx, 10, 20, 30);
    REQUIRE(cube != nullptr);
    REQUIRE(cube->layers == 6);
    REQUIRE(cube->getWidth() == 4);
    REQUIRE(cube->getHeight() == 4);
    REQUIRE(cube->getPixelWidth() == 4);
    REQUIRE(cube->getPixelHeight() == 4);

    Texture *larger = makeSolidCubemap(gfx, 1, 2, 3, 16);
    REQUIRE(larger != nullptr);
    REQUIRE(larger->layers == 6);
    REQUIRE(larger->getWidth() == 16);
    win->close();
}

TEST_CASE("IBL.envIntensityNegativeClampsToZero") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setEnvIntensity(1.25f);
    REQUIRE(std::abs(cam->data()->envIntensity - 1.25f) < 1e-5f);
    cam->setEnvIntensity(-3.f);
    REQUIRE(cam->data()->envIntensity == 0.f);

    win->close();
}

TEST_CASE("IBL.environmentIntensityAndRemovalContracts") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    struct CloseWindow {
        eve::window::Window *window;
        ~CloseWindow() { window->close(); }
    } cleanup{win};
    SUB_CASE("IBL.envIntensityScalesMetalBrightness") {
        resetScene3D();

        Mesh *mesh = gfx->newMeshSphere(24, 16);
        REQUIRE(mesh != nullptr);

        auto *cam         = Camera3D::createCamera();
        cam->data()->eyeZ = 3.f;
        cam->setAmbient(0.03f, 0.03f, 0.03f);
        cam->setEnvMap(makeSolidCubemap(gfx, 220, 220, 220));

        auto *ent                    = Renderable3D::create();
        ent->meshRenderer()->mesh    = mesh;
        ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
        ent->setMetallic(1.0f);
        ent->setRoughness(0.08f);
        addHud(gfx);
        RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

        gfx->setScreenReadbackEnabled(true);
        cam->setEnvIntensity(0.4f);
        warmPresent(gfx);
        const float L_low = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

        cam->setEnvIntensity(2.0f);
        warmPresent(gfx);
        const float L_high = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

        REQUIRE(L_high > L_low + 0.04f);
    };
    SUB_CASE("IBL.hybridMatchesForwardEnvironmentResponse") {
        resetScene3D();
        auto *mesh = gfx->newMeshSphere(24, 16);
        REQUIRE(mesh != nullptr);
        auto *cam         = Camera3D::createCamera();
        cam->data()->eyeZ = 3.f;
        cam->setAmbient(0.03f, 0.03f, 0.03f);
        cam->setEnvMap(makeSolidCubemap(gfx, 220, 220, 220));
        auto *ent = Renderable3D::create();
        ent->setMesh(mesh);
        ent->setTexture(makeSolidGray(gfx, 220));
        ent->setMetallic(1.f);
        ent->setRoughness(0.08f);
        addHud(gfx);
        RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);
        gfx->setScreenReadbackEnabled(true);
        const LightingMode previousMode = gfx->getRenderControl()->getLightingMode();
        const auto capture = [&](LightingMode mode, float intensity, zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
            auto *control = gfx->getRenderControl();
            REQUIRE(control->setLightingMode(mode).ok());
            control->compile();
            REQUIRE(control->getEffectiveLightingMode() == mode);
            cam->setEnvIntensity(intensity);
            warmPresent(gfx);
            return luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));
        };
        const float forwardLow  = capture(LightingMode::ForwardPlus, .4f, _ZEROERR_TEST_CONTEXT);
        const float forwardHigh = capture(LightingMode::ForwardPlus, 2.f, _ZEROERR_TEST_CONTEXT);
        const float hybridLow   = capture(LightingMode::Hybrid, .4f, _ZEROERR_TEST_CONTEXT);
        const float hybridHigh  = capture(LightingMode::Hybrid, 2.f, _ZEROERR_TEST_CONTEXT);
        // An unlit opaque foreground must overwrite its deferred depth/color,
        // while still hiding the more distant environment-lit surface.
        const uint8_t red[]             = {240, 20, 20, 255};
        auto         *foreground        = Renderable3D::create();
        auto         *foregroundMesh    = gfx->newMeshCube();
        auto         *foregroundTexture = gfx->newTexture(1, 1, red);
        REQUIRE(foregroundMesh != nullptr);
        REQUIRE(foregroundTexture != nullptr);
        foreground->setMesh(foregroundMesh);
        foreground->setTexture(foregroundTexture);
        foreground->setPosition(0.f, 0.f, 1.f);
        foreground->setScale(.4f, .4f, .4f);
        foreground->setReceiveLight(false);
        capture(LightingMode::ForwardPlus, 2.f, _ZEROERR_TEST_CONTEXT);
        const Color forwardFront = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        capture(LightingMode::Hybrid, 2.f, _ZEROERR_TEST_CONTEXT);
        const Color hybridFront = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(gfx->getRenderControl()->setLightingMode(previousMode).ok());
        gfx->getRenderControl()->compile();
        REQUIRE(forwardHigh > forwardLow + .04f);
        REQUIRE(hybridHigh > hybridLow + .04f);
        REQUIRE(std::fabs(hybridLow - forwardLow) < .02f);
        REQUIRE(std::fabs(hybridHigh - forwardHigh) < .02f);
        REQUIRE(forwardFront.r > forwardFront.g + .2f);
        REQUIRE(hybridFront.r > hybridFront.g + .2f);
        REQUIRE(std::fabs(hybridFront.r - forwardFront.r) < .02f);
        REQUIRE(std::fabs(hybridFront.g - forwardFront.g) < .02f);
        REQUIRE(std::fabs(hybridFront.b - forwardFront.b) < .02f);
    };
    SUB_CASE("IBL.clearEnvMapDisablesReflection") {
        resetScene3D();

        Mesh *mesh = gfx->newMeshSphere(24, 16);
        REQUIRE(mesh != nullptr);

        auto *cam         = Camera3D::createCamera();
        cam->data()->eyeZ = 3.f;
        cam->setAmbient(0.03f, 0.03f, 0.03f);

        auto *ent                    = Renderable3D::create();
        ent->meshRenderer()->mesh    = mesh;
        ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
        ent->setMetallic(1.0f);
        ent->setRoughness(0.08f);
        addHud(gfx);
        RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

        gfx->setScreenReadbackEnabled(true);
        warmPresent(gfx);
        const Color c0 = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

        cam->setEnvMap(makeSolidCubemap(gfx, 40, 200, 40));
        cam->setEnvIntensity(1.5f);
        warmPresent(gfx);
        const Color c1 = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(c1.g > c0.g + 0.04f);

        cam->setEnvMap(nullptr);
        warmPresent(gfx);
        const Color c2 = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(std::abs(c2.r - c0.r) < 0.05f);
        REQUIRE(std::abs(c2.g - c0.g) < 0.05f);
        REQUIRE(std::abs(c2.b - c0.b) < 0.05f);
    };
}


TEST_CASE("IBL.dielectricEnvWeakerThanMetal") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 16);
    REQUIRE(mesh != nullptr);

    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.03f, 0.03f, 0.03f);
    cam->setEnvMap(makeSolidCubemap(gfx, 230, 50, 50));
    cam->setEnvIntensity(1.5f);

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(0.0f);
    ent->setRoughness(0.35f);
    addHud(gfx);
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);
    warmPresent(gfx);
    const Color cDielectric = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    ent->setMetallic(1.0f);
    ent->setRoughness(0.08f);
    warmPresent(gfx);
    const Color cMetal = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    // Metals mirror env via specular F≈1; dielectrics only get a weak diffuse IBL term.
    REQUIRE(cMetal.r > cDielectric.r + 0.05f);
    REQUIRE(cMetal.r > cMetal.g + 0.03f);

    previewRotating(gfx, ent);
    win->close();
}

TEST_CASE("IBL.plusZFaceTintsSmoothMetal") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 16);
    REQUIRE(mesh != nullptr);

    // Dark everywhere except +Z (cyan) — the face the default camera reflects into.
    const uint8_t faceRgb[6][3] = {
        {8, 8, 8},       // +X
        {8, 8, 8},       // -X
        {8, 8, 8},       // +Y
        {8, 8, 8},       // -Y
        {40, 220, 230},  // +Z
        {8, 8, 8},       // -Z
    };

    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.03f, 0.03f, 0.03f);
    cam->setEnvMap(makeFaceColoredCubemap(gfx, faceRgb));
    cam->setEnvIntensity(1.5f);

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(1.0f);
    ent->setRoughness(0.06f);
    addHud(gfx);
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);
    warmPresent(gfx);
    const Color c = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    // Cyan +Z reflection should lift G and B above R.
    REQUIRE(c.g > c.r + 0.03f);
    REQUIRE(c.b > c.r + 0.03f);

    previewRotating(gfx, ent);
    win->close();
}

TEST_CASE("IBL.loadsPolyHavenEnvCubemaps") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);

    Texture *studio = loadEnvCubemap(gfx, "studio_small_09");
    REQUIRE(studio != nullptr);
    REQUIRE(studio->layers == 6);
    REQUIRE(studio->getWidth() == 128);
    REQUIRE(studio->getHeight() == 128);

    Texture *sky = loadEnvCubemap(gfx, "kloppenheim_06_puresky");
    REQUIRE(sky != nullptr);
    REQUIRE(sky->layers == 6);
    REQUIRE(sky->getWidth() == 128);

    win->close();
}

TEST_CASE("IBL.studioEnvBrightensMetal") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 16);
    REQUIRE(mesh != nullptr);

    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.03f, 0.03f, 0.03f);

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(1.0f);
    ent->setRoughness(0.08f);
    addHud(gfx);
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);
    warmPresent(gfx);
    const float L0 = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    cam->setEnvMap(loadEnvCubemap(gfx, "studio_small_09"));
    cam->setEnvIntensity(1.8f);
    warmPresent(gfx);
    const float L1 = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    REQUIRE(L1 > L0 + 0.03f);
    previewRotating(gfx, ent);
    win->close();
}

TEST_CASE("IBL.outdoorSkyDiffersFromStudio") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 16);
    REQUIRE(mesh != nullptr);

    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.03f, 0.03f, 0.03f);
    cam->setEnvIntensity(1.6f);

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(1.0f);
    ent->setRoughness(0.08f);
    addHud(gfx);
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);

    cam->setEnvMap(loadEnvCubemap(gfx, "studio_small_09"));
    warmPresent(gfx);
    const Color cStudio = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    cam->setEnvMap(loadEnvCubemap(gfx, "kloppenheim_06_puresky"));
    warmPresent(gfx);
    const Color cSky = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    // Different real environments should not land on the same metal response.
    const float dr = std::abs(cStudio.r - cSky.r);
    const float dg = std::abs(cStudio.g - cSky.g);
    const float db = std::abs(cStudio.b - cSky.b);
    REQUIRE(dr + dg + db > 0.04f);

    // Studio then outdoor sky, each held briefly so the env change is visible.
    cam->setEnvMap(loadEnvCubemap(gfx, "studio_small_09"));
    previewRotating(gfx, ent, 800);
    cam->setEnvMap(loadEnvCubemap(gfx, "kloppenheim_06_puresky"));
    previewRotating(gfx, ent, 800);
    win->close();
}
}  // namespace test_IBL

// Contract section: Lighting2D
#include "PrivateTestScope.h"


#include <initializer_list>
// Lighting2D.cpp
namespace test_Lighting2D {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

[[maybe_unused]] static float luma(const Color &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

[[maybe_unused]] static Texture *makeSolidTexture(Graphics *gfx, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4, 255);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
    }
    eve::image::ImageData imageData(w, h, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

/**
 * Normal map biased toward +X on the right half and -X on the left half so a light
 * from the right brightens the right side more.
 */
[[maybe_unused]] static Texture *makeBiasedNormal(Graphics *gfx, int w, int h) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4;
            // Encode [-1,1] → [0,255]; left leans -X, right leans +X.
            float nx  = (x < w / 2) ? -0.7f : 0.7f;
            float ny  = 0.f;
            float nz  = 0.7f;
            px[i + 0] = uint8_t((nx * 0.5f + 0.5f) * 255.f);
            px[i + 1] = uint8_t((ny * 0.5f + 0.5f) * 255.f);
            px[i + 2] = uint8_t((nz * 0.5f + 0.5f) * 255.f);
            px[i + 3] = 255;
        }
    }
    eve::image::ImageData imageData(w, h, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

/** Live-render the current lit scene to the window for ~1s.
 *  Retargets cam / sprites / lights from the offscreen canvas onto the swapchain
 *  so we don't depend on sampling a stale canvas texture after getPixel/present. */
[[maybe_unused]] static void previewOnWindow(Graphics *gfx, Camera2D *cam,
                                             std::initializer_list<Renderable2D *> sprites,
                                             std::initializer_list<Light2D *> lights, int ms = 1000) {
    if (!eve::test::manualPreviewEnabled()) return;
    cam->data()->canvas = nullptr;
    // 128×64 content → roughly fill a 320×240 window.
    cam->data()->zoom = 2.5f;
    for (auto *sp : sprites) sp->sprite()->canvas = nullptr;
    for (auto *L : lights) L->setCanvas(nullptr);

    gfx->setBackgroundColorRGBA(0.06f, 0.06f, 0.08f, 1.f);
    const int frames = (ms >= 16) ? (ms / 16) : 1;
    for (int i = 0; i < frames; ++i) {
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
        }
        SDL_Delay(16);
    }
}

TEST_CASE("Lighting2D.pointLightBrightensNearbyUnlitSprite") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(128, 64);
    REQUIRE(rt != nullptr);

    auto *cam           = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x      = 64.f;
    cam->data()->y      = 32.f;
    cam->data()->zoom   = 1.f;
    cam->setAmbient(0.05f, 0.05f, 0.05f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    Texture *albedo = makeSolidTexture(gfx, 8, 8, 255, 255, 255);
    REQUIRE(albedo != nullptr);

    auto *nearSp                   = Renderable2D::create();
    nearSp->transform()->x         = 8.f;
    nearSp->transform()->y         = 16.f;
    nearSp->sprite()->width        = 24.f;
    nearSp->sprite()->height       = 24.f;
    nearSp->sprite()->texture      = albedo;
    nearSp->sprite()->receiveLight = true;
    nearSp->sprite()->canvas       = rt;
    nearSp->sprite()->visible      = true;

    auto *farSp                   = Renderable2D::create();
    farSp->transform()->x         = 46.f;
    farSp->transform()->y         = 16.f;
    farSp->sprite()->width        = 24.f;
    farSp->sprite()->height       = 24.f;
    farSp->sprite()->texture      = albedo;
    farSp->sprite()->receiveLight = true;
    farSp->sprite()->canvas       = rt;
    farSp->sprite()->visible      = true;

    auto *light = Light2D::createLight("point");
    light->setCanvas(rt);
    light->setPosition(20.f, 28.f);
    light->setColor(1.f, 1.f, 1.f, 2.5f);
    light->setRadius(50.f);
    light->setEnabled(true);

    RenderSystem::render(*gfx);

    float nearL = luma(rt->getPixel(20, 28));
    float farL  = luma(rt->getPixel(48, 28));
    CHECK(nearL > farL + 0.08f);

    previewOnWindow(gfx, cam, {nearSp, farSp}, {light});

    nearSp->sprite()->visible = false;
    farSp->sprite()->visible  = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}

TEST_CASE("Lighting2D.normalMapLitSideBrighter") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(128, 64);
    REQUIRE(rt != nullptr);

    auto *cam           = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x      = 64.f;
    cam->data()->y      = 32.f;
    cam->data()->zoom   = 1.f;
    cam->setAmbient(0.08f, 0.08f, 0.08f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    Texture *albedo = makeSolidTexture(gfx, 32, 32, 220, 220, 220);
    Texture *normal = makeBiasedNormal(gfx, 32, 32);
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);

    auto *sp             = Renderable2D::create();
    sp->transform()->x   = 32.f;
    sp->transform()->y   = 8.f;
    sp->sprite()->width  = 64.f;
    sp->sprite()->height = 48.f;
    sp->setTexture(albedo);
    sp->setNormalTexture(normal);
    sp->setReceiveLight(true);
    sp->sprite()->canvas  = rt;
    sp->sprite()->visible = true;

    auto *light = Light2D::createLight("point");
    light->setCanvas(rt);
    // Light to the right of the sprite → right-facing normals should be brighter.
    light->setPosition(110.f, 32.f);
    light->setColor(1.f, 1.f, 1.f, 3.f);
    light->setRadius(120.f);
    light->setEnabled(true);

    // Second light (also collected) slightly above — ensures multi-light packing.
    auto *light2 = Light2D::createLight("point");
    light2->setCanvas(rt);
    light2->setPosition(100.f, 10.f);
    light2->setColor(0.4f, 0.4f, 1.f, 1.2f);
    light2->setRadius(80.f);
    light2->setEnabled(true);

    RenderSystem::render(*gfx);

    float leftL  = luma(rt->getPixel(42, 32));
    float rightL = luma(rt->getPixel(86, 32));
    CHECK(rightL > leftL + 0.05f);

    previewOnWindow(gfx, cam, {sp}, {light, light2});

    sp->sprite()->visible = false;
    light->setEnabled(false);
    light2->setEnabled(false);
    cam->data()->active = false;
    win->close();
}


TEST_CASE("Lighting2D.zeroRadiusSpotDoesNotActAsDirectional") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(64, 64);
    REQUIRE(rt != nullptr);

    auto *cam           = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x      = 32.f;
    cam->data()->y      = 32.f;
    cam->data()->zoom   = 1.f;
    cam->setAmbient(0.05f, 0.05f, 0.05f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    Texture *albedo            = makeSolidTexture(gfx, 8, 8, 255, 255, 255);
    auto    *sp                = Renderable2D::create();
    sp->transform()->x         = 20.f;
    sp->transform()->y         = 20.f;
    sp->sprite()->width        = 24.f;
    sp->sprite()->height       = 24.f;
    sp->sprite()->texture      = albedo;
    sp->sprite()->receiveLight = true;
    sp->sprite()->canvas       = rt;
    sp->sprite()->visible      = true;

    auto *light = Light2D::createLight("spot");
    light->setCanvas(rt);
    light->setPosition(8.f, 8.f);
    light->setDirection(1.f, 0.f);
    light->setColor(1.f, 1.f, 1.f, 4.f);
    light->setRadius(0.f);  // must not become a directional light
    light->setSpotAngle(25.f);
    light->setEnabled(true);

    RenderSystem::render(*gfx);
    float L = luma(rt->getPixel(32, 32));
    // Ambient-only: a mispacked directional would push luma well above ambient.
    CHECK(L < 0.15f);

    sp->sprite()->visible = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}

TEST_CASE("Lighting2D.spotLightBrightensAlongBeam") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(128, 64);
    REQUIRE(rt != nullptr);

    auto *cam           = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x      = 64.f;
    cam->data()->y      = 32.f;
    cam->data()->zoom   = 1.f;
    cam->setAmbient(0.04f, 0.04f, 0.04f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    Texture *albedo = makeSolidTexture(gfx, 8, 8, 255, 255, 255);
    REQUIRE(albedo != nullptr);

    auto *onBeam                   = Renderable2D::create();
    onBeam->transform()->x         = 48.f;
    onBeam->transform()->y         = 20.f;
    onBeam->sprite()->width        = 24.f;
    onBeam->sprite()->height       = 24.f;
    onBeam->sprite()->texture      = albedo;
    onBeam->sprite()->receiveLight = true;
    onBeam->sprite()->canvas       = rt;
    onBeam->sprite()->visible      = true;

    auto *offBeam                   = Renderable2D::create();
    offBeam->transform()->x         = 48.f;
    offBeam->transform()->y         = 4.f;
    offBeam->sprite()->width        = 24.f;
    offBeam->sprite()->height       = 24.f;
    offBeam->sprite()->texture      = albedo;
    offBeam->sprite()->receiveLight = true;
    offBeam->sprite()->canvas       = rt;
    offBeam->sprite()->visible      = true;

    auto *light = Light2D::createLight("spot");
    light->setCanvas(rt);
    light->setPosition(20.f, 32.f);
    light->setDirection(1.f, 0.f);  // beam to the right
    light->setColor(1.f, 1.f, 1.f, 3.f);
    light->setRadius(90.f);
    light->setSpotAngle(20.f);
    light->setSpotSoftness(0.2f);
    light->setEnabled(true);

    RenderSystem::render(*gfx);

    float onL  = luma(rt->getPixel(60, 32));
    float offL = luma(rt->getPixel(60, 16));
    CHECK(onL > offL + 0.06f);

    previewOnWindow(gfx, cam, {onBeam, offBeam}, {light});

    onBeam->sprite()->visible  = false;
    offBeam->sprite()->visible = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}

TEST_CASE("Lighting2D.setNormalTextureEnablesLitPathAndRotation") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(128, 64);
    REQUIRE(rt != nullptr);

    auto *cam           = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x      = 64.f;
    cam->data()->y      = 32.f;
    cam->data()->zoom   = 1.f;
    cam->setAmbient(0.08f, 0.08f, 0.08f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    Texture *albedo = makeSolidTexture(gfx, 32, 32, 220, 220, 220);
    Texture *normal = makeBiasedNormal(gfx, 32, 32);
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);

    auto *sp = Renderable2D::create();
    sp->setPosition(64.f, 32.f);
    sp->setSize(48.f, 48.f);
    sp->setAnchor(0.5f, 0.5f);
    sp->setTexture(albedo);
    CHECK(sp->getNormalTexture() == nullptr);
    sp->setNormalTexture(normal);
    CHECK(sp->getNormalTexture() == normal);
    sp->setReceiveLight(true);
    sp->setRotation(90.f);
    sp->sprite()->canvas  = rt;
    sp->sprite()->visible = true;

    // Directional light has no distance falloff, so a brightness gap between
    // equal-distance samples must come from rotated normals — not attenuation.
    auto *light = Light2D::createLight("dir");
    light->setCanvas(rt);
    // After 90° clockwise rotation, the map's +X bias faces screen +Y (down).
    // Light from below → lower half should be brighter.
    light->setDirection(0.f, 1.f);
    light->setColor(1.f, 1.f, 1.f, 1.5f);
    light->setEnabled(true);

    RenderSystem::render(*gfx);

    float topL = luma(rt->getPixel(64, 18));
    float botL = luma(rt->getPixel(64, 46));
    CHECK(botL > topL + 0.04f);

    sp->setNormalTexture(nullptr);
    CHECK(sp->getNormalTexture() == nullptr);
    sp->sprite()->visible = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}

/**
 * Pack a biased normal into a corner of a large atlas so UV derivatives are
 * tiny. The lit2d det singularity must be scale-aware or atlas tiles fall back
 * to the flat-Z path and lose the left/right bias.
 */
[[maybe_unused]] static Texture *makeAtlasBiasedNormal(Graphics *gfx, int atlasW, int atlasH, int tileX, int tileY,
                                                       int tileW, int tileH) {
    std::vector<uint8_t> px(size_t(atlasW) * size_t(atlasH) * 4);
    for (int y = 0; y < atlasH; ++y) {
        for (int x = 0; x < atlasW; ++x) {
            size_t i  = (size_t(y) * size_t(atlasW) + size_t(x)) * 4;
            px[i + 0] = 128;
            px[i + 1] = 128;
            px[i + 2] = 255;
            px[i + 3] = 255;
        }
    }
    for (int y = 0; y < tileH; ++y) {
        for (int x = 0; x < tileW; ++x) {
            size_t i  = (size_t(tileY + y) * size_t(atlasW) + size_t(tileX + x)) * 4;
            float  nx = (x < tileW / 2) ? -0.7f : 0.7f;
            float  ny = 0.f;
            float  nz = 0.7f;
            px[i + 0] = uint8_t((nx * 0.5f + 0.5f) * 255.f);
            px[i + 1] = uint8_t((ny * 0.5f + 0.5f) * 255.f);
            px[i + 2] = uint8_t((nz * 0.5f + 0.5f) * 255.f);
            px[i + 3] = 255;
        }
    }
    eve::image::ImageData imageData(atlasW, atlasH, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

[[maybe_unused]] static Texture *makeAtlasSolid(Graphics *gfx, int atlasW, int atlasH, int tileX, int tileY, int tileW,
                                                int tileH, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(size_t(atlasW) * size_t(atlasH) * 4, 0);
    for (int y = 0; y < tileH; ++y) {
        for (int x = 0; x < tileW; ++x) {
            size_t i  = (size_t(tileY + y) * size_t(atlasW) + size_t(tileX + x)) * 4;
            px[i + 0] = r;
            px[i + 1] = g;
            px[i + 2] = b;
            px[i + 3] = 255;
        }
    }
    eve::image::ImageData imageData(atlasW, atlasH, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

TEST_CASE("Lighting2D.atlasUvNormalMapKeepsSideBias") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(128, 64);
    REQUIRE(rt != nullptr);

    auto *cam           = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x      = 64.f;
    cam->data()->y      = 32.f;
    cam->data()->zoom   = 1.f;
    cam->setAmbient(0.08f, 0.08f, 0.08f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    constexpr int kAtlas = 1024;
    constexpr int kTile  = 32;
    Texture      *albedo = makeAtlasSolid(gfx, kAtlas, kAtlas, 0, 0, kTile, kTile, 220, 220, 220);
    Texture      *normal = makeAtlasBiasedNormal(gfx, kAtlas, kAtlas, 0, 0, kTile, kTile);
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);
    Quad *tile = gfx->newQuad(0, 0, kTile, kTile);
    REQUIRE(tile != nullptr);

    auto *sp             = Renderable2D::create();
    sp->transform()->x   = 32.f;
    sp->transform()->y   = 8.f;
    sp->sprite()->width  = 64.f;
    sp->sprite()->height = 48.f;
    sp->setTexture(albedo);
    sp->setNormalTexture(normal);
    sp->setQuad(tile);  // tiny UV span → exercises scale-aware det
    sp->setReceiveLight(true);
    sp->sprite()->canvas  = rt;
    sp->sprite()->visible = true;

    auto *light = Light2D::createLight("dir");
    light->setCanvas(rt);
    light->setDirection(1.f, 0.f);
    light->setColor(1.f, 1.f, 1.f, 1.5f);
    light->setEnabled(true);

    RenderSystem::render(*gfx);

    float leftL  = luma(rt->getPixel(42, 32));
    float rightL = luma(rt->getPixel(86, 32));
    CHECK(rightL > leftL + 0.05f);

    sp->sprite()->visible = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}

TEST_CASE("Lighting2D.additiveBlendBrightensOverClear") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(64, 64);
    REQUIRE(rt != nullptr);

    auto *cam           = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x      = 32.f;
    cam->data()->y      = 32.f;
    cam->data()->zoom   = 1.f;
    cam->setAmbient(0.f, 0.f, 0.f);
    // Non-black clear so additive and alpha produce distinguishable results.
    cam->data()->r = 0.25f;
    cam->data()->g = 0.1f;
    cam->data()->b = 0.1f;
    cam->data()->a = 1.f;

    Texture              *albedo    = makeSolidTexture(gfx, 8, 8, 255, 255, 255);
    const uint8_t         flatPx[4] = {128, 128, 255, 255};
    eve::image::ImageData flatImage(1, 1, "RGBA8");
    std::memcpy(flatImage.getData(), flatPx, 4);
    Texture *flat = gfx->newTexture(&flatImage);
    REQUIRE(albedo != nullptr);
    REQUIRE(flat != nullptr);

    auto *alphaSp             = Renderable2D::create();
    alphaSp->transform()->x   = 4.f;
    alphaSp->transform()->y   = 20.f;
    alphaSp->sprite()->width  = 24.f;
    alphaSp->sprite()->height = 24.f;
    alphaSp->setTexture(albedo);
    alphaSp->setNormalTexture(flat);
    alphaSp->setReceiveLight(true);
    alphaSp->setBlend("alpha");
    alphaSp->setColor(0.4f, 0.4f, 0.4f, 0.5f);
    alphaSp->sprite()->canvas  = rt;
    alphaSp->sprite()->visible = true;

    auto *addSp             = Renderable2D::create();
    addSp->transform()->x   = 36.f;
    addSp->transform()->y   = 20.f;
    addSp->sprite()->width  = 24.f;
    addSp->sprite()->height = 24.f;
    addSp->setTexture(albedo);
    addSp->setNormalTexture(flat);
    addSp->setReceiveLight(true);
    addSp->setBlend("additive");
    addSp->setColor(0.4f, 0.4f, 0.4f, 0.5f);
    addSp->sprite()->canvas  = rt;
    addSp->sprite()->visible = true;

    auto *light = Light2D::createLight("dir");
    light->setCanvas(rt);
    light->setDirection(0.f, 1.f);
    light->setColor(1.f, 1.f, 1.f, 1.f);
    light->setEnabled(true);

    RenderSystem::render(*gfx);

    Color alphaPx = rt->getPixel(16, 32);
    Color addPx   = rt->getPixel(48, 32);
    // Additive over a non-black clear keeps more of the destination red channel
    // than alpha blend (src*a + dst*(1-a)), which darkens the clear.
    CHECK(addPx.r > alphaPx.r + 0.05f);
    CHECK(luma(addPx) > luma(alphaPx) + 0.03f);

    alphaSp->sprite()->visible = false;
    addSp->sprite()->visible   = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}

TEST_CASE("Lighting2D.hdrCanvasLitBlendDoesNotCrash") {
    // HDR canvases reject pixel readback; this is a pipeline-compat smoke test:
    // lit Alpha + Additive must bind HDR lit pipelines (not RGBA8 offscreen).
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newHDRCanvas(64, 64);
    REQUIRE(rt != nullptr);

    auto *cam           = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x      = 32.f;
    cam->data()->y      = 32.f;
    cam->data()->zoom   = 1.f;
    cam->setAmbient(0.1f, 0.1f, 0.1f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    Texture              *albedo    = makeSolidTexture(gfx, 8, 8, 255, 255, 255);
    const uint8_t         flatPx[4] = {128, 128, 255, 255};
    eve::image::ImageData flatImage(1, 1, "RGBA8");
    std::memcpy(flatImage.getData(), flatPx, 4);
    Texture *flat = gfx->newTexture(&flatImage);
    REQUIRE(albedo != nullptr);
    REQUIRE(flat != nullptr);

    auto *alphaSp             = Renderable2D::create();
    alphaSp->transform()->x   = 4.f;
    alphaSp->transform()->y   = 20.f;
    alphaSp->sprite()->width  = 24.f;
    alphaSp->sprite()->height = 24.f;
    alphaSp->setTexture(albedo);
    alphaSp->setNormalTexture(flat);
    alphaSp->setReceiveLight(true);
    alphaSp->setBlend("alpha");
    alphaSp->sprite()->canvas  = rt;
    alphaSp->sprite()->visible = true;

    auto *addSp             = Renderable2D::create();
    addSp->transform()->x   = 36.f;
    addSp->transform()->y   = 20.f;
    addSp->sprite()->width  = 24.f;
    addSp->sprite()->height = 24.f;
    addSp->setTexture(albedo);
    addSp->setNormalTexture(flat);
    addSp->setReceiveLight(true);
    addSp->setBlend("additive");
    addSp->sprite()->canvas  = rt;
    addSp->sprite()->visible = true;

    auto *light = Light2D::createLight("dir");
    light->setCanvas(rt);
    light->setDirection(0.f, 1.f);
    light->setColor(1.f, 1.f, 1.f, 1.f);
    light->setEnabled(true);

    // Must complete without Vulkan/WebGPU pipeline-format mismatch.
    RenderSystem::render(*gfx);
    CHECK(true);

    alphaSp->sprite()->visible = false;
    addSp->sprite()->visible   = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}
}  // namespace test_Lighting2D

// Contract section: Outline
#include "PrivateTestScope.h"


// Outline.cpp
namespace test_Outline {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {


void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]        = *it;
            data->active       = false;
            data->envMap       = nullptr;
            data->envIntensity = 0.f;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d]   = *it;
            d->enabled = false;
        }
    }
}

Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

void addHud() {
    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;
}

void warmPresent(Graphics *gfx, int frames = 2) {
    for (int i = 0; i < frames; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
}

Mesh *loadCube(Graphics *gfx) {
    Mesh *m = gfx->newMeshCube();
    REQUIRE(m != nullptr);
    return m;
}

void setupCubeScene(Graphics *gfx, Renderable3D *&cube) {
    resetScene3D();
    cube = Renderable3D::create();
    cube->setMesh(loadCube(gfx));
    cube->setTexture(makeSolid(gfx, 200, 190, 170));
    cube->setMetallic(0.f);
    cube->setRoughness(0.85f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(1.6f, 1.2f, 2.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.3f, 0.3f, 0.3f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ  = 30.f;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.2f, 1.f, 0.4f);
    sun->setColor(1.f, 1.f, 1.f, 1.6f);
    sun->setEnabled(true);

    addHud();
}

}  // namespace

TEST_CASE("outline.paramRoundTrip") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    std::unique_ptr<Outline> outline(gfx->newOutline());
    REQUIRE(outline->getShader() != nullptr);

    outline->setColor(0.1f, 0.2f, 0.3f);
    outline->setWidth(2.f);
    outline->setDepthThreshold(0.4f);
    outline->setDepthSensitivity(0.01f);
    outline->setNormalThreshold(0.5f);
    outline->setSoftness(0.3f);
    outline->setClip(0.1f, 50.f);

    CHECK(std::fabs(outline->getColorR() - 0.1f) < 1e-5f);
    CHECK(std::fabs(outline->getColorG() - 0.2f) < 1e-5f);
    CHECK(std::fabs(outline->getColorB() - 0.3f) < 1e-5f);
    CHECK(std::fabs(outline->getWidth() - 2.f) < 1e-5f);
    CHECK(std::fabs(outline->getDepthThreshold() - 0.4f) < 1e-5f);
    CHECK(std::fabs(outline->getDepthSensitivity() - 0.01f) < 1e-5f);
    CHECK(std::fabs(outline->getNormalThreshold() - 0.5f) < 1e-5f);
    CHECK(std::fabs(outline->getSoftness() - 0.3f) < 1e-5f);

    CHECK(outline->hasParam("width") == true);
    CHECK(outline->hasParam("depthThreshold") == true);
    CHECK(outline->hasParam("normalThreshold") == true);
    CHECK(outline->hasParam("softness") == true);
    CHECK(std::fabs(outline->getFloat("width") - 2.f) < 1e-5f);

    outline->setFloat("colorR", 0.9f);
    CHECK(std::fabs(outline->getFloat("colorR") - 0.9f) < 1e-5f);

    win->close();
}

TEST_CASE("outline.renderControlFeatureToggle") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->disable("ao");
    CHECK(rc->supports("outline") == true);
    CHECK(rc->isEnabled("outline") == false);

    rc->enable("outline");
    CHECK(rc->isEnabled("outline") == true);
    // Enabling outline implies the gbuffer depth/normal fill.
    CHECK(rc->isEnabled("gbuffer") == true);

    // Turning the gbuffer off must drop the dependent outline feature.
    rc->disable("gbuffer");
    CHECK(rc->isEnabled("outline") == false);

    rc->enable("gbuffer");
    rc->enable("outline");
    rc->compile();

    Renderable3D *cube = nullptr;
    setupCubeScene(gfx, cube);
    warmPresent(gfx, 3);

    GBuffer *gb = rc->getGBuffer();
    REQUIRE(gb != nullptr);
    CHECK(gb->isValid());
    CHECK(gb->getHwDepthTexture() != nullptr);
    CHECK(gb->getNormalTexture() != nullptr);

    win->close();
}

TEST_CASE("outline.newMeshCubeAndPipelineConfig") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);

    Mesh *cube = gfx->newMeshCube(1.f);
    REQUIRE(cube != nullptr);

    // The auto-applied (render-feature) outline must be reachable and configurable.
    Outline *pipeline = gfx->pipelineOutline();
    REQUIRE(pipeline != nullptr);
    pipeline->setColor(0.2f, 0.1f, 0.05f);
    pipeline->setWidth(2.5f);
    pipeline->setClip(0.05f, 40.f);
    CHECK(std::fabs(pipeline->getWidth() - 2.5f) < 1e-5f);
    CHECK(std::fabs(pipeline->getColorR() - 0.2f) < 1e-5f);

    RenderControl *rc = gfx->getRenderControl();
    rc->disable("ao");
    rc->enable("outline");
    rc->compile();

    Renderable3D *cubeEnt = Renderable3D::create();
    cubeEnt->setMesh(cube);
    cubeEnt->setPosition(0.f, 0.f, 0.f);
    cubeEnt->setRoughness(0.7f);
    auto *cam = Camera3D::createCamera();
    cam->setEye(2.6f, 1.9f, 3.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ  = 40.f;
    addHud();
    warmPresent(gfx, 2);
    CHECK(rc->getGBuffer()->isValid());

    win->close();
}

TEST_CASE("outline.previewCube") {
    // Compare the outline against the same cube without the effect at fixed poses.
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 420, 360);
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.12f, 0.13f, 0.16f, 1.f));

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->disable("ao");
    rc->enable("outline");
    rc->compile();

    Renderable3D *cube = nullptr;
    setupCubeScene(gfx, cube);

    // Slightly thicker, softer dark outline so the silhouette reads clearly.
    Outline *outline = gfx->pipelineOutline();
    REQUIRE(outline != nullptr);
    outline->setWidth(1.5f);
    outline->setColor(0.04f, 0.03f, 0.05f);
    outline->setNormalThreshold(0.5f);
    outline->setSoftness(0.2f);

    for (int pose : {0, 17, 43}) {
        cube->setRotation(0.15f * float(pose), float(pose) * 0.02f, 0.f);
        rc->disable("outline");
        rc->compile();
        warmPresent(gfx);
        std::unique_ptr<eve::image::ImageData> plain(gfx->newImageData());
        REQUIRE(plain != nullptr);
        rc->enable("outline");
        rc->compile();
        warmPresent(gfx);
        std::unique_ptr<eve::image::ImageData> outlined(gfx->newImageData());
        REQUIRE(outlined != nullptr);
        REQUIRE(plain->getWidth() == outlined->getWidth());
        REQUIRE(plain->getHeight() == outlined->getHeight());
        int darkenedPixels = 0;
        for (int y = 0; y < plain->getHeight(); ++y) {
            for (int x = 0; x < plain->getWidth(); ++x) {
                const auto before = plain->getPixel(x, y);
                const auto after  = outlined->getPixel(x, y);
                if (before.r + before.g + before.b > after.r + after.g + after.b + 0.09f) ++darkenedPixels;
            }
        }
        REQUIRE(darkenedPixels > 8);
    }

    const int frames = eve::test::manualPreviewEnabled() ? 150 : 0;
    for (int i = 0; i < frames; ++i) {
        cube->setRotation(0.15f * float(i), float(i) * 0.02f, 0.f);
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) goto done;
        }
        SDL_Delay(33);
    }
done:
    win->close();
}
}  // namespace test_Outline

// Contract section: Quad
#include "PrivateTestScope.h"


// Quad.cpp
namespace test_Quad {
using namespace eve::graphics;


TEST_CASE("Quad.spriteAtlasSamplesSubRect") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    // Atlas: left half red, right half green.
    constexpr int        TW = 32, TH = 16;
    std::vector<uint8_t> px(size_t(TW) * size_t(TH) * 4);
    for (int y = 0; y < TH; ++y) {
        for (int x = 0; x < TW; ++x) {
            size_t i    = (size_t(y) * size_t(TW) + size_t(x)) * 4;
            bool   left = x < TW / 2;
            px[i + 0]   = left ? 255 : 0;
            px[i + 1]   = left ? 0 : 255;
            px[i + 2]   = 0;
            px[i + 3]   = 255;
        }
    }
    eve::image::ImageData imageData(TW, TH, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    Texture *atlas = gfx->newTexture(&imageData);
    REQUIRE(atlas != nullptr);

    Quad *right = gfx->newQuad(TW / 2, 0, TW / 2, TH);
    REQUIRE(right != nullptr);

    Canvas *rt = gfx->newCanvas(64, 64);
    REQUIRE(rt != nullptr);

    auto *cam           = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x      = 32.f;
    cam->data()->y      = 32.f;
    cam->data()->zoom   = 1.f;
    cam->data()->r      = 0.f;
    cam->data()->g      = 0.f;
    cam->data()->b      = 0.f;
    cam->data()->a      = 1.f;

    auto *sp                   = Renderable2D::create();
    sp->transform()->x         = 16.f;
    sp->transform()->y         = 16.f;
    sp->sprite()->width        = 32.f;
    sp->sprite()->height       = 32.f;
    sp->sprite()->texture      = atlas;
    sp->sprite()->quad         = right;
    sp->sprite()->receiveLight = false;
    sp->sprite()->canvas       = rt;
    sp->sprite()->visible      = true;

    RenderSystem::render(*gfx);

    Color mid = rt->getPixel(32, 32);
    CHECK(mid.g > 0.7f);
    CHECK(mid.r < 0.2f);

    sp->sprite()->visible = false;
    cam->data()->active   = false;
    win->close();
}
}  // namespace test_Quad
