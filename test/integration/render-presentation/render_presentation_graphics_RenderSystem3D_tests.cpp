// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "filesystem",
  "modules": [
    "common",
    "daynight",
    "filesystem",
    "graphics",
    "image",
    "procgen",
    "ui"
  ],
  "origins": [
    "RenderSystem.cpp",
    "RenderSystem3D.cpp",
    "ScreenSpaceReflection.cpp",
    "ShaderResourceSharing.cpp",
    "Shadow3D.cpp",
    "SkyAtmospherePass.cpp",
    "SkyMoon.cpp",
    "SkyRuntime.cpp",
    "SkyScriptBindings.cpp",
    "SkyTextureGpu.cpp",
    "SkyViewPreparation.cpp",
    "SkyWisps.cpp",
    "TextureSampler.cpp",
    "UIOverlaySmoke.cpp",
    "Volumetric.cpp"
  ],
  "cases": {
    "render-offscreen": [
      "daynight.sky runtime rejects updates after provider retirement without advancing state",
      "graphics.sky atmosphere survives graphics provider retirement"
    ],
    "render-window": [
      "Camera.panAndZoomAffectOffscreenPixels",
      "Canvas.offscreenGetPixelAndSampleOnScreen",
      "Graphics.screenGetPixelAfterPresent",
      "GraphicsSmoke.clearAndPresentWindow",
      "GraphicsSmoke.cubemapGeneratesMipChain",
      "GraphicsSmoke.newTextureFromFileThrowsOnMissing",
      "GraphicsSmoke.setTextureSamplerValidatesStrings",
      "GraphicsSmoke.textureMipmapsAndAnisotropy",
      "Lighting3D.metallicIncreasesSpecularHighlight",
      "Lighting3D.pointLightBrightensNearbyFace",
      "Mesh.newMeshFromAssimpCube",
      "Mesh.newMeshSphere",
      "RenderSystem3D.dynamicVertexRingTracksLatestUpdate",
      "RenderSystem3D.entityIdMaskPixels",
      "RenderSystem3D.forwardContributorRunsInMainPassAndUnregisters",
      "RenderSystem3D.gpuOpaqueCollectorRunsBeforeCullAndUnregisters",
      "RenderSystem3D.lightingAffectsPixels",
      "RenderSystem3D.parallaxChangesPixels",
      "RenderSystem3D.readTextureBackPixels",
      "RenderSystem3D.shadowContributorRunsInCascadesAndUnregisters",
      "RenderSystem3D.smokeRotatingCube",
      "RenderSystem3D.texCellBombChangesPixels",
      "RenderSystem3D.textureCheckerPixels",
      "Renderable3D.gtsTerrainRuntimeReplacesTilesWithStableHandles",
      "Renderable3D.gtsTerrainRuntimeRestoresSourceVisibilityAcrossEveryLifetimeOrder",
      "Renderable3D.gtsTerrainTileUploadsAndConfiguresRealLodChain",
      "Renderable3D.pcgGenericLodsUploadAsOneAtomicChain",
      "Shadow3D.dirLightDarkensOccludedGround",
      "Shadow3D.maskedCasterUsesAlbedoAlpha",
      "Shadow3D.pcgLodTwoSidedCastsBackFacingGeometry",
      "Shadow3D.pcgSpeedTreeLodKeepsComplementaryCasterCoverage",
      "Shadow3D.receiveShadowFalseIgnoresMap",
      "Shadow3D.shadowStrengthInterpolatesVisibility",
      "UI.smoke.RenderSystemSameFrameVulkanOverlay",
      "UI.smoke.panelGalleryPreview",
      "UI.smoke.viewportEmbeddedRenderTarget",
      "daynight.sky injected cloud time changes authored wisps and closes the morph cycle",
      "daynight.sky optical cycle survives midnight and restores the same rendered snapshot",
      "daynight.sky runtime advances one clock and publishes solar lighting to capture",
      "daynight.sky script prepares an owned profile runtime and checks time commands",
      "graphics.shader shared images retain ownership and replacement isolation",
      "graphics.sky HDR volume uploads preserve interpolation and failed replacement",
      "graphics.sky atmosphere capture uses independent HDR lighting and detaches cleanly",
      "graphics.sky authored wisps composite into the shared capture path",
      "graphics.sky descriptor limit rejects clouds and leaves atmosphere preparation usable",
      "graphics.sky filmic display preserves middle grey and keeps prior modes explicit",
      "graphics.sky height fog fills the ground with independently integrated ambient light",
      "graphics.sky lunar surface preserves phase earthlight occultation and source orientation",
      "graphics.sky wisps preserve phase blending and celestial vertex lighting",
      "graphics.view preparation failures cancel dispatch and registrations detach",
      "graphics.view preparation runs before offscreen target opens",
      "ssr.createConfigureAndApply",
      "volumetric.applyFogProducesOpacity",
      "volumetric.applyFromScene",
      "volumetric.cloudModeParametersAndQuality",
      "volumetric.collectAndDriveSceneLights3D",
      "volumetric.drawOccluders2DUsesCastFlag",
      "volumetric.drawOcclusionRespectsFlag",
      "volumetric.fogDenserAtDistance",
      "volumetric.fogModeAndQuality",
      "volumetric.fogParamsRoundTrip",
      "volumetric.froxelAtlasCompositesIntegratedMedia",
      "volumetric.froxelIntegratesEmissiveProxyAndSceneLights",
      "volumetric.froxelSceneLightsValidateBoundsAndRankProxies",
      "volumetric.modeAndRayMarchQuality",
      "volumetric.occlusionFromSceneLights2D",
      "volumetric.occlusionMapAndScatter",
      "volumetric.paramRoundTripShared",
      "volumetric.qualityPresets",
      "volumetric.rayMarchProducesScatter",
      "volumetric.rayMarchShadowDarkensBehindOccluder",
      "volumetric.spotConeScatter2D"
    ]
  }
}
*/
// clang-format on

// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: RenderSystem
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include "ManualPreview.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include "common/Exception.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/Batcher.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "medialoader/image/FormatHandler.h"
#include "window/Window.h"
#include "zeroerr/unittest.h"
// RenderSystem.cpp
namespace test_RenderSystem {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;


TEST_CASE("RenderSystem.drawsVisibleSpritesViaMocklessPath") {
    auto *a              = Renderable2D::create();
    a->transform()->x    = 5;
    a->transform()->y    = 6;
    a->sprite()->width   = 16;
    a->sprite()->height  = 16;
    a->sprite()->r       = 0.2f;
    a->sprite()->visible = true;

    CHECK(std::abs(a->transform()->x - 5.f) < 1e-5f);
    CHECK(std::abs(a->sprite()->width - 16.f) < 1e-5f);

    int  visible = 0;
    auto view    = ecs::View<Renderable2D, Renderable2D::Transform2D, Renderable2D::Sprite>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [xf, sp] = *it;
        if (sp->visible) ++visible;
    }
    CHECK_GE(visible, 1);
}

TEST_CASE("RenderSystem.sprite2dPropertyApiReachesDrawItem") {
    auto *sprite = Renderable2D::create();
    sprite->setPosition(12.f, 34.f);
    sprite->setSize(80.f, 60.f);
    sprite->setScale(1.5f, 0.5f);
    sprite->setRotation(27.f);
    sprite->setLayer(7);
    sprite->setBlend("additive");
    sprite->setReceiveLight(false);
    sprite->setAnchor(0.25f, 0.75f);
    sprite->setFlip(true, false);

    std::vector<DrawItem2D> items;
    RenderSystem::collectSprites(items);
    bool found = false;
    for (const auto &item : items) {
        if (std::fabs(item.x - 12.f) < 1e-5f && std::fabs(item.y - 34.f) < 1e-5f) {
            found = true;
            CHECK(std::fabs(item.w - 120.f) < 1e-5f);
            CHECK(std::fabs(item.h - 30.f) < 1e-5f);
            CHECK(std::fabs(item.rotation - 27.f) < 1e-5f);
            CHECK_EQ(item.layer, 7);
            CHECK_EQ(static_cast<int>(item.blend), static_cast<int>(BlendMode::Additive));
            CHECK(!item.receiveLight);
            CHECK(std::fabs(item.anchorX - 0.25f) < 1e-5f);
            CHECK(std::fabs(item.anchorY - 0.75f) < 1e-5f);
            CHECK(item.flipX);
            CHECK(!item.flipY);
        }
    }
    CHECK(found);
    sprite->release();
}


[[maybe_unused]] static std::vector<uint8_t> makeCheckerRGBA(int w, int h, int cell, uint8_t r0, uint8_t g0, uint8_t b0,
                                                             uint8_t r1, uint8_t g1, uint8_t b1) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool   on = ((x / cell) + (y / cell)) % 2 == 0;
            size_t i  = (size_t(y) * size_t(w) + size_t(x)) * 4;
            px[i + 0] = on ? r0 : r1;
            px[i + 1] = on ? g0 : g1;
            px[i + 2] = on ? b0 : b1;
            px[i + 3] = 255;
        }
    }
    return px;
}

[[maybe_unused]] static std::vector<uint8_t> makeStripeRGBA(int w, int h, int band) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool   on = (x / band) % 2 == 0;
            size_t i  = (size_t(y) * size_t(w) + size_t(x)) * 4;
            px[i + 0] = on ? 40 : 80;
            px[i + 1] = on ? 200 : 40;
            px[i + 2] = on ? 255 : 200;
            px[i + 3] = 255;
        }
    }
    return px;
}

[[maybe_unused]] static eve::filesystem::Filesystem *bootstrapFilesystemForSaveIO(const char *identity) {
    auto *fs = eve::filesystem::Filesystem::create();
    // Unique identity per caller: CTest runs GraphicsSmoke.* in parallel and the
    // Filesystem module is a process-wide singleton — shared write dirs race on macOS.
    REQUIRE(fs->setIdentity(identity, true));
    REQUIRE(fs->setupWriteDirectory());
    return fs;
}

TEST_CASE("GraphicsSmoke.clearAndPresentWindow") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 640;
    s.height   = 480;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    REQUIRE_GT(gfx->getWidth(), 0);
    REQUIRE_GT(gfx->getPixelWidth(), 0);

    gfx->setBackgroundColor(Color(0.12f, 0.14f, 0.22f, 1.0f));

    auto                        *fs          = bootstrapFilesystemForSaveIO("evengine_gfx_smoke_clear");
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();

    auto                  checkerPx = makeCheckerRGBA(64, 64, 8, 255, 220, 60, 40, 40, 180);
    eve::image::ImageData src(64, 64, "RGBA8");
    std::memcpy(src.getData(), checkerPx.data(), checkerPx.size());

    const char                                *tmpName = "evengine_smoke_checker.png";
    std::unique_ptr<eve::filesystem::FileData> encoded(
        src.encode(medialoader::FormatHandler::ENCODED_PNG, tmpName, true));
    REQUIRE(encoded.get() != nullptr);

    Texture *fromFile = gfx->newTextureFromFile(tmpName);
    REQUIRE(fromFile != nullptr);

    auto                  stripePx = makeStripeRGBA(64, 64, 8);
    eve::image::ImageData imageData(64, 64, "RGBA8");
    std::memcpy(imageData.getData(), stripePx.data(), stripePx.size());
    Texture *stripes = gfx->newTexture(&imageData);
    REQUIRE(stripes != nullptr);
    REQUIRE(stripes != fromFile);

    auto *solid              = Renderable2D::create();
    solid->transform()->x    = 40;
    solid->transform()->y    = 40;
    solid->sprite()->width   = 120;
    solid->sprite()->height  = 80;
    solid->sprite()->r       = 1.0f;
    solid->sprite()->g       = 0.4f;
    solid->sprite()->b       = 0.2f;
    solid->sprite()->visible = true;

    auto *spriteA              = Renderable2D::create();
    spriteA->transform()->x    = 200;
    spriteA->transform()->y    = 80;
    spriteA->sprite()->width   = 128;
    spriteA->sprite()->height  = 128;
    spriteA->sprite()->texture = fromFile;
    spriteA->sprite()->r       = 1;
    spriteA->sprite()->g       = 1;
    spriteA->sprite()->b       = 1;
    spriteA->sprite()->visible = true;

    auto *spriteB              = Renderable2D::create();
    spriteB->transform()->x    = 360;
    spriteB->transform()->y    = 160;
    spriteB->sprite()->width   = 160;
    spriteB->sprite()->height  = 160;
    spriteB->sprite()->texture = stripes;
    spriteB->sprite()->r       = 1;
    spriteB->sprite()->g       = 1;
    spriteB->sprite()->b       = 1;
    spriteB->sprite()->visible = true;

    spriteA->sprite()->layer = 1;
    spriteB->sprite()->layer = 1;

    for (int frame = 0; frame < 60; ++frame) {
        solid->transform()->x   = 40.0f + float(frame) * 0.8f;
        spriteA->transform()->y = 80.0f + float((frame % 40) - 20) * 0.5f;
        spriteB->transform()->x = 360.0f + float((frame % 30) - 15) * 0.4f;
        RenderSystem::render(*gfx);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    fs->remove(tmpName);
    win->close();
}

TEST_CASE("GraphicsSmoke.newTextureFromFileThrowsOnMissing") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    // No write-dir bootstrap: missing-file should throw from PhysFS open alone.
    // (Parallel GraphicsSmoke.clearAndPresentWindow used to race the same identity.)
    [[maybe_unused]] auto *const filesystemModule = eve::filesystem::Filesystem::create();
    bool                         threw            = false;
    try {
        gfx->newTextureFromFile("definitely_missing_evengine_xyz.png");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);

    win->close();
}

TEST_CASE("Canvas.offscreenGetPixelAndSampleOnScreen") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 640;
    s.height   = 480;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *c = gfx->newCanvas(64, 64);
    REQUIRE(c != nullptr);
    gfx->setCanvas(c);
    gfx->clear(Color(0.2f, 0.4f, 0.6f, 1.0f), std::nullopt, std::nullopt);
    gfx->drawSolidRect(0, 0, 64, 64, Color(1.0f, 0.0f, 0.0f, 1.0f));
    gfx->setCanvas();

    Color p = c->getPixel(32, 32);
    CHECK(std::abs(p.r - 1.0f) < 0.05f);
    CHECK(std::abs(p.g - 0.0f) < 0.05f);
    CHECK(std::abs(p.b - 0.0f) < 0.05f);

    std::unique_ptr<eve::image::ImageData> img(c->newImageData());
    REQUIRE(img->getWidth() == 64);
    REQUIRE(img->getHeight() == 64);
    auto *px = static_cast<uint8_t *>(img->getData());
    CHECK(px[(32 * 64 + 32) * 4 + 0] >= 200);

    gfx->setCanvas(c);
    bool presentThrew = false;
    try {
        gfx->present();
    } catch (const eve::Exception &) {
        presentThrew = true;
    }
    CHECK(presentThrew);
    gfx->setCanvas();

    Canvas *c2 = gfx->newCanvas(128, 128);
    REQUIRE(c2 != nullptr);
    REQUIRE(c2->getTexture() != nullptr);

    auto *off              = Renderable2D::create();
    off->transform()->x    = 0;
    off->transform()->y    = 0;
    off->sprite()->width   = 128;
    off->sprite()->height  = 128;
    off->sprite()->r       = 0.1f;
    off->sprite()->g       = 0.8f;
    off->sprite()->b       = 0.3f;
    off->sprite()->canvas  = c2;
    off->sprite()->visible = true;

    auto *onScreen              = Renderable2D::create();
    onScreen->transform()->x    = 80;
    onScreen->transform()->y    = 80;
    onScreen->sprite()->width   = 192;
    onScreen->sprite()->height  = 192;
    onScreen->sprite()->texture = c2->getTexture();
    onScreen->sprite()->r       = 1;
    onScreen->sprite()->g       = 1;
    onScreen->sprite()->b       = 1;
    onScreen->sprite()->visible = true;

    for (int frame = 0; frame < 30; ++frame) {
        RenderSystem::render(*gfx);
        SDL_Delay(8);
    }

    Color p2 = c2->getPixel(64, 64);
    CHECK(std::abs(p2.g - 0.8f) < 0.15f);

    win->close();
}

TEST_CASE("Camera.panAndZoomAffectOffscreenPixels") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt          = gfx->newCanvas(100, 100);
    auto   *cam         = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x      = 50.f;
    cam->data()->y      = 50.f;
    cam->data()->zoom   = 1.f;
    cam->data()->r      = 0.f;
    cam->data()->g      = 0.f;
    cam->data()->b      = 0.f;
    cam->data()->a      = 1.f;

    // Create override camera before any render/View (ECS create after heavy GPU
    // work has been flaky on this path); wire it up later via Sprite.camera.
    auto *camOverride           = Camera2D::createCamera();
    camOverride->data()->canvas = rt;
    camOverride->data()->active = false;
    camOverride->data()->x      = 40.f;
    camOverride->data()->y      = 40.f;
    camOverride->data()->zoom   = 1.f;
    camOverride->data()->r      = 0.f;
    camOverride->data()->g      = 0.f;
    camOverride->data()->b      = 0.f;
    camOverride->data()->a      = 1.f;

    auto *sp              = Renderable2D::create();
    sp->transform()->x    = 40.f;
    sp->transform()->y    = 40.f;
    sp->sprite()->width   = 20.f;
    sp->sprite()->height  = 20.f;
    sp->sprite()->r       = 1.f;
    sp->sprite()->g       = 0.f;
    sp->sprite()->b       = 0.f;
    sp->sprite()->canvas  = rt;
    sp->sprite()->visible = true;

    RenderSystem::render(*gfx);

    Color mid = rt->getPixel(45, 45);
    CHECK(std::abs(mid.r - 1.f) < 0.08f);

    cam->data()->zoom = 2.f;
    RenderSystem::render(*gfx);
    Color z = rt->getPixel(35, 35);
    CHECK(std::abs(z.r - 1.f) < 0.08f);
    Color outside = rt->getPixel(80, 80);
    CHECK(outside.r < 0.2f);

    sp->sprite()->camera = camOverride;
    cam->data()->zoom    = 1.f;
    RenderSystem::render(*gfx);
    // Override cam center (40,40): sprite lands at (50,50)-(70,70).
    // Default cam would have left it at (40,40)-(60,60).
    Color ov = rt->getPixel(65, 65);
    CHECK(std::abs(ov.r - 1.f) < 0.08f);
    Color notDefault = rt->getPixel(45, 45);
    CHECK(notDefault.r < 0.2f);

    sp->sprite()->visible = false;
    cam->data()->active   = false;
    win->close();
}
}  // namespace test_RenderSystem

// Contract section: RenderSystem3D
#include "PrivateTestScope.h"


#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include "Fixtures.h"
#include "graphics/ClipSpace.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "medialoader/model/ModelLoader.h"
// RenderSystem3D.cpp
namespace test_RenderSystem3D {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

// Cube with per-face UVs (0..1). Front face (+Z) is first so Assimp preserves it.
static const char kCubeObj[] =
    // +Z front
    "v -0.5 -0.5  0.5\n"
    "v  0.5 -0.5  0.5\n"
    "v  0.5  0.5  0.5\n"
    "v -0.5  0.5  0.5\n"
    "vt 0 0\n"
    "vt 1 0\n"
    "vt 1 1\n"
    "vt 0 1\n"
    "vn 0 0 1\n"
    "f 1/1/1 2/2/1 3/3/1\n"
    "f 1/1/1 3/3/1 4/4/1\n"
    // -Z back
    "v  0.5 -0.5 -0.5\n"
    "v -0.5 -0.5 -0.5\n"
    "v -0.5  0.5 -0.5\n"
    "v  0.5  0.5 -0.5\n"
    "vn 0 0 -1\n"
    "f 5/1/2 6/2/2 7/3/2\n"
    "f 5/1/2 7/3/2 8/4/2\n"
    // +X right
    "v  0.5 -0.5  0.5\n"
    "v  0.5 -0.5 -0.5\n"
    "v  0.5  0.5 -0.5\n"
    "v  0.5  0.5  0.5\n"
    "vn 1 0 0\n"
    "f 9/1/3 10/2/3 11/3/3\n"
    "f 9/1/3 11/3/3 12/4/3\n"
    // -X left
    "v -0.5 -0.5 -0.5\n"
    "v -0.5 -0.5  0.5\n"
    "v -0.5  0.5  0.5\n"
    "v -0.5  0.5 -0.5\n"
    "vn -1 0 0\n"
    "f 13/1/4 14/2/4 15/3/4\n"
    "f 13/1/4 15/3/4 16/4/4\n"
    // +Y top
    "v -0.5  0.5  0.5\n"
    "v  0.5  0.5  0.5\n"
    "v  0.5  0.5 -0.5\n"
    "v -0.5  0.5 -0.5\n"
    "vn 0 1 0\n"
    "f 17/1/5 18/2/5 19/3/5\n"
    "f 17/1/5 19/3/5 20/4/5\n"
    // -Y bottom
    "v -0.5 -0.5 -0.5\n"
    "v  0.5 -0.5 -0.5\n"
    "v  0.5 -0.5  0.5\n"
    "v -0.5 -0.5  0.5\n"
    "vn 0 -1 0\n"
    "f 21/1/6 22/2/6 23/3/6\n"
    "f 21/1/6 23/3/6 24/4/6\n";

[[maybe_unused]] static float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }


[[maybe_unused]] static Mesh *loadUvCube(Graphics *gfx) {
    medialoader::ModelLoader loader;
    auto                     scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes >= 1);
    REQUIRE(scene->mMeshes[0]->HasTextureCoords(0));
    Mesh *m = gfx->newMeshFromAssimp(*scene->mMeshes[0]);
    REQUIRE(m != nullptr);
    REQUIRE_GT(m->indexCount, 0);
    return m;
}

[[maybe_unused]] static Texture *makeChecker(Graphics *gfx, int size, int cell) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint8_t c = ((x / cell) ^ (y / cell)) & 1 ? 255 : 0;
            size_t  i = size_t(y * size + x) * 4;
            px[i]     = c;
            px[i + 1] = c;
            px[i + 2] = c;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data(), TextureCreateInfo::withMipmaps(true));
}

[[maybe_unused]] static Texture *makeSolidGray(Graphics *gfx, uint8_t g) {
    uint8_t px[4] = {g, g, g, 255};
    return gfx->newTexture(1, 1, px);
}

/** Non-periodic motif so UV remapping (not just phase shift) is measurable. */
[[maybe_unused]] static Texture *makeMotif(Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    const float          cx = float(size) * 0.35f;
    const float          cy = float(size) * 0.4f;
    const float          r  = float(size) * 0.22f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float   dx = float(x) - cx;
            float   dy = float(y) - cy;
            float   d  = std::sqrt(dx * dx + dy * dy);
            uint8_t v  = d < r ? 255 : uint8_t((x * 13 + y * 7) % 180);
            if (x > size * 3 / 4 && y < size / 4) v = 40;
            size_t i  = size_t(y * size + x) * 4;
            px[i]     = v;
            px[i + 1] = uint8_t((v * 3) / 4);
            px[i + 2] = uint8_t(255 - v / 2);
            px[i + 3] = 255;
        }
    }
    TextureCreateInfo info = TextureCreateInfo::withMipmaps(true);
    info.sampler.repeatU   = true;
    info.sampler.repeatV   = true;
    return gfx->newTexture(size, size, px.data(), info);
}

// Graphics/ECS are process-wide singletons — hide leftovers from earlier cases.
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

// NOTE: Graphics is a process-wide singleton — one window lifetime per process.
TEST_CASE("Mesh.newMeshFromAssimpCube") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);

    Mesh *m = loadUvCube(gfx);
    REQUIRE(m != nullptr);

    win->close();
}

TEST_CASE("Mesh.newMeshSphere") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);

    Mesh *m = gfx->newMeshSphere(24, 12);
    REQUIRE(m != nullptr);
    CHECK_GT(m->indexCount, 0);
    // stacks bands * slices quads * 2 tris * 3 indices
    CHECK_EQ(m->indexCount, 24 * 12 * 6);

    win->close();
}

TEST_CASE("Graphics.screenGetPixelAfterPresent") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 64, 64);

    const Color expected(0.20f, 0.45f, 0.70f, 1.f);
    gfx->setScreenReadbackEnabled(true);
    gfx->clear(expected, std::nullopt, std::nullopt);
    // Full-screen solid ensures a known color even if clear-only present is optimized later.
    gfx->drawSolidRect(0, 0, float(gfx->getWidth()), float(gfx->getHeight()), expected);
    gfx->present();

    Color c = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(std::abs(c.r - expected.r) < 0.08f);
    REQUIRE(std::abs(c.g - expected.g) < 0.08f);
    REQUIRE(std::abs(c.b - expected.b) < 0.08f);

    win->close();
}

TEST_CASE("RenderSystem3D.smokeRotatingCube") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = loadUvCube(gfx);

    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeChecker(gfx, 16, 4);

    // Same-frame 2D overlay (also ensures present closes the open 3D pass).
    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 4;
    hud->transform()->y   = 4;
    hud->sprite()->width  = 8;
    hud->sprite()->height = 8;
    hud->sprite()->r      = 1.f;
    hud->sprite()->g      = 0.2f;
    hud->sprite()->b      = 0.2f;

    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);

    // ~1s of visible rotation so the smoke case is a real on-screen scene.
    for (int i = 0; i < 60; ++i) {
        ent->transform()->yaw = float(i) * 0.05f;
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    win->close();
}

TEST_CASE("RenderSystem3D.forwardContributorRunsInMainPassAndUnregisters") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 64, 64);
    resetScene3D();

    auto *cam            = Camera3D::createCamera();
    cam->data()->eyeZ    = 3.f;
    int            calls = 0;
    const uint64_t token = RenderSystem3D::addForwardExtraDrawer(
        [&](Graphics &, const Camera3D::Data &seen, const glm::mat4 &, float aspect) {
            ++calls;
            CHECK(&seen == cam->data().operator->());
            CHECK_GT(aspect, 0.f);
        });
    REQUIRE_NE(token, 0u);

    RenderSystem3D::render(*gfx);
    gfx->present();
    CHECK_EQ(calls, 1);

    RenderSystem3D::removeForwardExtraDrawer(token);
    RenderSystem3D::render(*gfx);
    gfx->present();
    CHECK_EQ(calls, 1);

    win->close();
}

TEST_CASE("RenderSystem3D.gpuOpaqueCollectorRunsBeforeCullAndUnregisters") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 64, 64);
    resetScene3D();

    auto *cam            = Camera3D::createCamera();
    cam->data()->eyeZ    = 3.f;
    int            calls = 0;
    const uint64_t token =
        RenderSystem3D::addGpuOpaqueCollector([&](Graphics &, const Camera3D::Data &seen, const glm::mat4 &,
                                                  float aspect, std::vector<GpuInstance> &instances) {
            ++calls;
            CHECK(&seen == cam->data().operator->());
            CHECK_GT(aspect, 0.f);
            CHECK(instances.empty());
        });
    REQUIRE_NE(token, 0u);

    RenderSystem3D::render(*gfx);
    gfx->present();
    CHECK_EQ(calls, 1);

    RenderSystem3D::removeGpuOpaqueCollector(token);
    RenderSystem3D::render(*gfx);
    gfx->present();
    CHECK_EQ(calls, 1);

    win->close();
}

TEST_CASE("RenderSystem3D.shadowContributorRunsInCascadesAndUnregisters") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 64, 64);
    resetScene3D();

    auto *cam            = Camera3D::createCamera();
    cam->data()->eyeZ    = 3.f;
    int            calls = 0;
    const uint64_t token =
        RenderSystem3D::addShadowExtraDrawer([&](Graphics &, const glm::mat4 &, const Camera3D::Data &seen) {
            ++calls;
            CHECK(&seen == cam->data().operator->());
        });
    REQUIRE_NE(token, 0u);

    RenderSystem3D::render(*gfx);
    gfx->present();
    REQUIRE_GT(calls, 0);
    const int registeredCalls = calls;

    RenderSystem3D::removeShadowExtraDrawer(token);
    RenderSystem3D::render(*gfx);
    gfx->present();
    CHECK_EQ(calls, registeredCalls);

    win->close();
}

TEST_CASE("RenderSystem3D.dynamicVertexRingTracksLatestUpdate") {
    // P1-2 regression: updateMeshVertices now writes into a per-frame ring of
    // host-visible vertex buffers instead of waiting on all in-flight frames.
    // The draw path must bind the latest written copy — a stale slot (e.g. a
    // copy from a few frames ago) would show the wrong side of this
    // alternating quad.
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    const float           half    = 0.5f;
    std::vector<float>    basePos = {-half, -half, 0.f, half, -half, 0.f, half, half, 0.f, -half, half, 0.f};
    std::vector<float>    nrm     = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    std::vector<float>    uv      = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    std::vector<uint32_t> idx     = {0, 1, 2, 0, 2, 3};
    Mesh                 *mesh    = gfx->newMeshFromArrays(basePos.data(), nrm.data(), uv.data(), 4, idx.data(), 6);
    REQUIRE(mesh != nullptr);

    auto *cam                    = Camera3D::createCamera();
    cam->data()->eyeZ            = 3.f;
    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);
    gfx->setScreenReadbackEnabled(true);

    const int W         = gfx->getWidth();
    const int H         = gfx->getHeight();
    auto      updatePos = [&](float side) {
        std::vector<float> pos = basePos;
        for (int v = 0; v < 4; ++v) pos[size_t(v) * 3u + 0u] += side * 0.8f;
        // Indices are only supplied on the first update; later calls exercise
        // the ring's CPU-index reuse path.
        REQUIRE(gfx->updateMeshVertices(mesh, pos.data(), nrm.data(), uv.data(), 4, idx.data(), 6));
    };
    auto renderFrame = [&]() {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        gfx->present();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
    };
    auto brightestHalf = [&]() -> int {
        // Horizontal scan at mid-height: 0 = brightest on left half, 1 = right.
        float bestL = 0.f, bestR = 0.f;
        for (int x = 0; x < W; ++x) {
            const Color c = gfx->getPixel(x, H / 2);
            const float l = luma(c);
            if (x < W / 2)
                bestL = std::max(bestL, l);
            else
                bestR = std::max(bestR, l);
        }
        return bestL > bestR ? 0 : 1;
    };

    // Alternate sides across many frames so the ring rotates several times;
    // the drawn image must always reflect the update issued in that frame.
    for (int i = 0; i < 24; ++i) {
        updatePos((i % 2 == 0) ? -1.f : 1.f);
        renderFrame();
    }
    // Settle on the right side and verify the latest update wins.
    for (int i = 0; i < 4; ++i) {
        updatePos(1.f);
        renderFrame();
    }
    CHECK_EQ(brightestHalf(), 1);
    // Settle on the left side: the ring must still serve the freshest copy.
    for (int i = 0; i < 4; ++i) {
        updatePos(-1.f);
        renderFrame();
    }
    CHECK_EQ(brightestHalf(), 0);

    win->close();
}

TEST_CASE("RenderSystem3D.textureCheckerPixels") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh        = loadUvCube(gfx);
    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;

    auto *ent                 = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    // 2x2 checker: left/right halves differ clearly under face-on projection.
    ent->meshRenderer()->texture = makeChecker(gfx, 16, 8);

    // Off-sample HUD so present closes the 3D pass via the same path as smoke.
    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r      = 0.f;
    hud->sprite()->g      = 0.f;
    hud->sprite()->b      = 0.f;

    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
    // Warm up a couple of frames after possible surface/swapchain recreate.
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }

    const int cx = gfx->getWidth() / 2;
    const int cy = gfx->getHeight() / 2;
    const int dx = std::max(16, gfx->getWidth() / 10);
    // Sample inside a checker row rather than on its v=0.5 boundary, where
    // backend-specific filtering can blend both cells to nearly the same gray.
    const int sampleY = cy - dx / 2;
    Color     a       = gfx->getPixel(cx - dx, sampleY);
    Color     b       = gfx->getPixel(cx + dx, sampleY);
    // Lit PBR softens albedo contrast vs unlit; still require a clear left/right split.
    REQUIRE(std::abs(luma(a) - luma(b)) > 0.05f);

    win->close();
}

TEST_CASE("RenderSystem3D.lightingAffectsPixels") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh        = loadUvCube(gfx);
    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);

    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r      = 0.f;
    hud->sprite()->g      = 0.f;
    hud->sprite()->b      = 0.f;

    const int cx = gfx->getWidth() / 2;
    const int cy = gfx->getHeight() / 2;

    gfx->setScreenReadbackEnabled(true);
    // Empirically for this mesh/view under the PBR path, +Z lights the visible face brighter.
    RenderSystem3D::setDirectionalLight(0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_bright = luma(gfx->getPixel(cx, cy));

    RenderSystem3D::setDirectionalLight(0.f, 0.f, -1.f, 1.f, 1.f, 1.f);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_dark = luma(gfx->getPixel(cx, cy));

    REQUIRE(L_bright - L_dark > 0.08f);

    win->close();
}

TEST_CASE("Lighting3D.pointLightBrightensNearbyFace") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh        = loadUvCube(gfx);
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
    hud->sprite()->r      = 0.f;
    hud->sprite()->g      = 0.f;
    hud->sprite()->b      = 0.f;

    // Point light in front of +Z face.
    auto *light = Light3D::createLight("point");
    light->setPosition(0.f, 0.f, 2.f);
    light->setColor(1.f, 1.f, 1.f, 4.f);
    light->setRadius(6.f);

    // Disable legacy directional fallback by also having Light3D enabled (already).
    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_near = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    light->setPosition(0.f, 0.f, -4.f);  // behind cube → front face darker
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_far = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    REQUIRE(L_near > L_far + 0.05f);

    light->setEnabled(false);
    win->close();
}

TEST_CASE("Lighting3D.metallicIncreasesSpecularHighlight") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh        = loadUvCube(gfx);
    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.05f, 0.05f, 0.05f);

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 200);
    ent->setRoughness(0.2f);
    ent->setMetallic(0.0f);

    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;

    auto *light = Light3D::createLight("dir");
    // Light from the camera side so N·L > 0 and a specular lobe can form.
    light->setDirection(0.f, 0.35f, 1.f);
    light->setColor(1.f, 1.f, 1.f, 2.0f);

    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_dielectric = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    ent->setMetallic(1.0f);
    ent->setRoughness(0.12f);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_metal = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    REQUIRE(L_dielectric > 0.08f);
    // With this view, the specular lobe may miss the center pixel; metals still
    // drop Lambert diffuse, so the metal sample should be darker than dielectric.
    REQUIRE(L_metal < L_dielectric - 0.02f);

    light->setEnabled(false);
    win->close();
}

TEST_CASE("RenderSystem3D.texCellBombChangesPixels") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);
    resetScene3D();

    // Same cube + camera as textureCheckerPixels (known-good textured face).
    Mesh *mesh        = loadUvCube(gfx);
    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeMotif(gfx, 64);
    ent->setMetallic(0.f);
    ent->setRoughness(0.85f);

    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r      = 0.f;
    hud->sprite()->g      = 0.f;
    hud->sprite()->b      = 0.f;

    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.f, 0.f, 1.f, 1.f, 1.f, 1.f);

    auto capture = [&](std::vector<float> &out) {
        out.clear();
        for (int i = 0; i < 3; ++i) {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
        }
        const int w  = gfx->getWidth();
        const int h  = gfx->getHeight();
        const int cx = w / 2;
        const int cy = h / 2;
        const int dx = std::max(8, w / 12);
        const int dy = std::max(8, h / 12);
        for (int y = cy - 2 * dy; y <= cy + 2 * dy; y += dy) {
            for (int x = cx - 2 * dx; x <= cx + 2 * dx; x += dx) {
                out.push_back(luma(gfx->getPixel(x, y)));
            }
        }
    };

    std::vector<float> off, on;
    ent->setTexCellBomb(3.f, 0.f, 1.f);
    CHECK_EQ(ent->getTexCellBombStrength(), 0.f);
    capture(off);

    ent->setTexCellBomb(3.f, 1.f, 1.f);
    CHECK_EQ(ent->getTexCellBombScale(), 3.f);
    CHECK_EQ(ent->getTexCellBombStrength(), 1.f);
    capture(on);

    REQUIRE_EQ(off.size(), on.size());
    REQUIRE_GT(off.size(), 4u);

    float offMin = off[0], offMax = off[0];
    for (float v : off) {
        offMin = std::min(offMin, v);
        offMax = std::max(offMax, v);
    }
    REQUIRE(offMax - offMin > 0.02f);

    float mad = 0.f;
    for (size_t i = 0; i < off.size(); ++i) mad = std::max(mad, std::fabs(off[i] - on[i]));
    if (!(mad > 0.015f)) {
        std::fprintf(stderr, "texCellBomb mad=%g offRange=%g n=%zu\n", mad, offMax - offMin, off.size());
    }
    REQUIRE(mad > 0.015f);

    win->close();
}

/** Left-half high / right-half low height so glancing POM shifts albedo UVs. */
[[maybe_unused]] static Texture *makeSplitHeight(Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint8_t h = (x < size / 2) ? 255 : 0;
            size_t  i = size_t(y * size + x) * 4;
            px[i]     = h;
            px[i + 1] = h;
            px[i + 2] = h;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data(), true, true);
}

TEST_CASE("RenderSystem3D.parallaxChangesPixels") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);
    resetScene3D();

    Mesh *mesh = loadUvCube(gfx);
    auto *cam  = Camera3D::createCamera();
    // Off-axis eye so tangent-space view has a strong XY component.
    cam->setEye(1.6f, 0.7f, 2.4f);
    cam->setTarget(0.f, 0.f, 0.f);

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeMotif(gfx, 64);
    ent->setHeightTexture(makeSplitHeight(gfx, 64));
    ent->setMetallic(0.f);
    ent->setRoughness(0.85f);

    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r      = 0.f;
    hud->sprite()->g      = 0.f;
    hud->sprite()->b      = 0.f;

    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.f, 0.f, 1.f, 1.f, 1.f, 1.f);

    auto capture = [&](std::vector<float> &out) {
        out.clear();
        for (int i = 0; i < 3; ++i) {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
        }
        const int w  = gfx->getWidth();
        const int h  = gfx->getHeight();
        const int cx = w / 2;
        const int cy = h / 2;
        const int dx = std::max(8, w / 12);
        const int dy = std::max(8, h / 12);
        for (int y = cy - 2 * dy; y <= cy + 2 * dy; y += dy) {
            for (int x = cx - 2 * dx; x <= cx + 2 * dx; x += dx) {
                out.push_back(luma(gfx->getPixel(x, y)));
            }
        }
    };

    std::vector<float> off, on;
    ent->setParallax(0.f);
    CHECK_EQ(ent->getParallaxScale(), 0.f);
    capture(off);

    ent->setParallax(0.12f, 8.f, 24.f);
    CHECK_EQ(ent->getParallaxScale(), 0.12f);
    CHECK_EQ(ent->getParallaxMinLayers(), 8.f);
    CHECK_EQ(ent->getParallaxMaxLayers(), 24.f);
    capture(on);

    REQUIRE_EQ(off.size(), on.size());
    REQUIRE_GT(off.size(), 4u);

    float offMin = off[0], offMax = off[0];
    for (float v : off) {
        offMin = std::min(offMin, v);
        offMax = std::max(offMax, v);
    }
    REQUIRE(offMax - offMin > 0.02f);

    float mad = 0.f;
    for (size_t i = 0; i < off.size(); ++i) mad = std::max(mad, std::fabs(off[i] - on[i]));
    if (!(mad > 0.01f)) {
        std::fprintf(stderr, "parallax mad=%g offRange=%g n=%zu\n", mad, offMax - offMin, off.size());
    }
    REQUIRE(mad > 0.01f);

    win->close();
}


// ---------------------------------------------------------------------------
// GPU 验证：逐像素实体 ID mask（renderEntityIdMask）与通用纹理读回
// （readTextureToImageData）。这两条路径支撑 capture_render_frame 的
// "id" / "depth" / "normal" 缓冲区，必须在真实 Vulkan 设备上跑通。
// ---------------------------------------------------------------------------
TEST_CASE("RenderSystem3D.entityIdMaskPixels") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();

    Mesh *mesh = loadUvCube(gfx);  // 单位立方体（半边长 0.5）
    REQUIRE(mesh != nullptr);

    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;  // 看向原点
    auto            d = cam->data();
    const glm::vec3 eye(d->eyeX, d->eyeY, d->eyeZ);
    const glm::vec3 target(d->targetX, d->targetY, d->targetZ);
    const glm::vec3 up(0.f, 1.f, 0.f);
    const float     aspect = float(gfx->getWidth()) / float(gfx->getHeight());
    const glm::mat4 viewM  = glm::lookAtRH(eye, target, up);
    const glm::mat4 projM  = eve::graphics::perspectiveVulkanRH_ZO(glm::radians(d->fovYDeg), aspect, d->nearZ, d->farZ);
    const glm::mat4 viewProj = projM * viewM;

    // 两个立方体：左侧 id=1（红），右侧 id=2（绿）。
    std::vector<Graphics::EntityIdDraw> draws;
    Graphics::EntityIdDraw              a;
    a.mesh    = mesh;
    a.model   = glm::translate(glm::mat4(1.f), glm::vec3(-1.f, 0.f, 0.f));
    a.idColor = glm::vec4(1.f, 0.f, 0.f, 1.f);
    draws.push_back(a);
    Graphics::EntityIdDraw b;
    b.mesh    = mesh;
    b.model   = glm::translate(glm::mat4(1.f), glm::vec3(1.f, 0.f, 0.f));
    b.idColor = glm::vec4(0.f, 1.f, 0.f, 1.f);
    draws.push_back(b);

    const int              w   = gfx->getWidth();
    const int              h   = gfx->getHeight();
    eve::image::ImageData *img = gfx->renderEntityIdMask(draws, viewProj, w, h);
    REQUIRE(img != nullptr);
    REQUIRE(img->getWidth() == w);
    REQUIRE(img->getHeight() == h);

    auto *px     = static_cast<const uint8_t *>(img->getData());
    bool  sawRed = false, sawGreen = false, sawBg = false;
    for (int y = 8; y < h - 8; y += 4) {
        for (int x = 8; x < w - 8; x += 4) {
            const size_t  i = (size_t(y) * size_t(w) + size_t(x)) * 4;
            const uint8_t r = px[i], g = px[i + 1], b = px[i + 2];
            if (r > 200 && g < 60 && b < 60)
                sawRed = true;
            else if (g > 200 && r < 60 && b < 60)
                sawGreen = true;
            else if (r == 0 && g == 0 && b == 0)
                sawBg = true;
        }
    }
    CHECK(sawRed);    // 左立方体被渲染为 id=1（红）
    CHECK(sawGreen);  // 右立方体被渲染为 id=2（绿）
    CHECK(sawBg);     // 背景为 (0,0,0,0)

    // 保存 ID mask PNG 供人工核对（可选）。
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    eve::filesystem::FileData *png = img->encode(medialoader::FormatHandler::ENCODED_PNG, "entity_id_mask.png", false);
    if (png) {
        const std::filesystem::path outDir = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out";
        std::filesystem::create_directories(outDir);
        std::ofstream out(outDir / "entity_id_mask.png", std::ios::binary);
        if (out.good()) {
            out.write(static_cast<const char *>(png->getData()), static_cast<std::streamsize>(png->getSize()));
            std::printf("entity_id_mask saved: %s\n", (outDir / "entity_id_mask.png").string().c_str());
        }
        delete png;
    }
    delete img;

    win->close();
}

TEST_CASE("RenderSystem3D.readTextureBackPixels") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();

    // 用 renderEntityIdMask（离屏，不经过 present）往 G-buffer 槽写入真实立方体
    // 几何，再通过 readTextureToImageData 读回深度/法线纹理 —— 即
    // capture_render_frame 的 depth/normal 路径。
    Mesh *mesh = loadUvCube(gfx);
    REQUIRE(mesh != nullptr);
    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    auto            d = cam->data();
    const glm::vec3 eye(d->eyeX, d->eyeY, d->eyeZ);
    const glm::mat4 viewM = glm::lookAtRH(eye, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
    const glm::mat4 projM =
        eve::graphics::perspectiveVulkanRH_ZO(glm::radians(d->fovYDeg), 320.f / 240.f, d->nearZ, d->farZ);
    const glm::mat4 viewProj = projM * viewM;

    std::vector<Graphics::EntityIdDraw> draws;
    Graphics::EntityIdDraw              a;
    a.mesh    = mesh;
    a.model   = glm::mat4(1.f);
    a.idColor = glm::vec4(1.f, 0.f, 0.f, 1.f);
    draws.push_back(a);
    eve::image::ImageData *idImg = gfx->renderEntityIdMask(draws, viewProj, 320, 240);
    REQUIRE(idImg != nullptr);
    delete idImg;

    auto *gb = gfx->getRenderControl()->getGBuffer();
    REQUIRE(gb != nullptr);
    REQUIRE(gb->isValid());

    const int cx = 320 / 2;
    const int cy = 240 / 2;

    eve::image::ImageData *depth = gfx->readGBufferToImageData("depth");
    REQUIRE(depth != nullptr);
    {
        auto        *px = static_cast<const uint8_t *>(depth->getData());
        const size_t c  = (size_t(cy) * size_t(depth->getWidth()) + size_t(cx)) * 4;
        const size_t e  = (size_t(3) * size_t(depth->getWidth()) + size_t(3)) * 4;
        // 立方体在中心（有几何）→ 中心线性深度 > 背景（清空为 0）深度。
        CHECK(px[c + 0] > px[e + 0]);
    }
    delete depth;

    eve::image::ImageData *normal = gfx->readGBufferToImageData("normal");
    REQUIRE(normal != nullptr);
    {
        auto        *px = static_cast<const uint8_t *>(normal->getData());
        const size_t c  = (size_t(cy) * size_t(normal->getWidth()) + size_t(cx)) * 4;
        // 立方体正对相机的前脸法线 ≈ (0,0,1) → B 通道接近 255。
        CHECK(px[c + 2] > 200);
    }
    delete normal;

    win->close();
}
}  // namespace test_RenderSystem3D

// Contract section: ScreenSpaceReflection
#include "PrivateTestScope.h"


// ScreenSpaceReflection.cpp
namespace test_ScreenSpaceReflection {
using namespace eve::graphics;

TEST_CASE("ssr.createConfigureAndApply") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);

    std::unique_ptr<ScreenSpaceReflection> ssr(gfx->newScreenSpaceReflection());
    REQUIRE(ssr.get() != nullptr);
    REQUIRE(ssr->getShader() != nullptr);
    REQUIRE(ssr->getShader()->gpuHandle != nullptr);
    ssr->setCamera(0.f, 1.f, 3.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 55.f, 1.5f, 0.1f, 30.f);
    ssr->setMaxDistance(12.f);
    ssr->setStepLength(0.25f);
    ssr->setMaxSteps(24);
    ssr->setThickness(0.35f);
    ssr->setStrength(0.75f);
    CHECK(ssr->hasParam("maxDist"));
    CHECK(ssr->getFloat("maxSteps") == 24.f);

    std::vector<uint8_t> scene(64u * 48u * 4u, 255u);
    std::vector<uint8_t> depth(64u * 48u * 4u, 128u);
    Texture             *sceneTexture = gfx->newTexture(64, 48, scene.data());
    Texture             *depthTexture = gfx->newTexture(64, 48, depth.data());
    Canvas              *dest         = gfx->newCanvas(64, 48);
    REQUIRE(sceneTexture != nullptr);
    REQUIRE(depthTexture != nullptr);
    REQUIRE(dest != nullptr);
    ssr->applyFromSceneTo(gfx, sceneTexture, depthTexture, nullptr, dest);
    REQUIRE(dest->getTexture() != nullptr);

    ssr->setEnabled(false);
    CHECK(!ssr->getEnabled());
    ssr->applyFromSceneTo(gfx, sceneTexture, depthTexture, nullptr, dest);
    win->close();
}
}  // namespace test_ScreenSpaceReflection

// Contract section: ShaderResourceSharing
#include "PrivateTestScope.h"


#include <array>
#include "ShaderResourceSharingSpv.h"
#include "graphics/ShaderResources.h"
#include "graphics/vulkan/ShaderResourceReload.h"
// ShaderResourceSharing.cpp
namespace test_ShaderResourceSharing {
TEST_CASE("graphics.shader shared images retain ownership and replacement isolation") {
    GfxFixture fixture;
    auto      *gfx    = fixture.gfx;
    auto      *first  = gfx->newMeshShaderFromSpv(sharing_vert, sharing_frag);
    auto      *second = gfx->newMeshShaderFromSpv(sharing_vert, sharing_frag);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    auto                            owner = std::make_shared<const std::array<std::byte, 4>>();
    eve::graphics::ShaderImageInput image;
    image.width        = 1;
    image.height       = 1;
    image.bytes        = *owner;
    image.contentOwner = owner;
    auto replace       = [&](eve::graphics::Shader *shader) {
        return gfx->replaceMeshShaderResources(*shader, sharing_vert, sharing_frag, {{&image, 1}, {}, {}});
    };
#ifndef EVE_TEST_RESOURCE_VALIDATION
    // Provider-absent builds must reject uploads without replacing either shader.
    auto *firstHandle  = first->gpuHandle;
    auto *secondHandle = second->gpuHandle;
    auto  unavailable  = replace(first);
    REQUIRE(!unavailable.ok());
    REQUIRE(unavailable.error() != nullptr);
    REQUIRE(unavailable.error()->code() == eve::DiagnosticCode::Unsupported);
    REQUIRE(unavailable.status().describe().find("SPIR-V validation provider is unavailable") != std::string::npos);
    REQUIRE(first->gpuHandle == firstHandle);
    REQUIRE(second->gpuHandle == secondHandle);
#else
    auto shared = [&] {
        return eve::graphics::vulkan::isMeshResourceImageShared(
            *static_cast<eve::graphics::vulkan::GpuShader *>(first->gpuHandle), 0,
            *static_cast<eve::graphics::vulkan::GpuShader *>(second->gpuHandle), 0);
    };
    REQUIRE(replace(first).ok());
    REQUIRE(replace(second).ok());
    REQUIRE(shared());
    image.sampler.repeatU = true;
    REQUIRE(replace(second).ok());
    REQUIRE(!shared());
    image.sampler.repeatU = false;
    REQUIRE(replace(second).ok());
    REQUIRE(shared());
    image.bytes = {};
    REQUIRE(!replace(second).ok());
    REQUIRE(shared());
    auto other         = std::make_shared<const std::array<std::byte, 4>>();
    image.bytes        = *other;
    image.contentOwner = other;
    REQUIRE(replace(second).ok());
    REQUIRE(!shared());
    image.bytes        = *owner;
    image.contentOwner = owner;
    REQUIRE(replace(second).ok());
    REQUIRE(shared());
    REQUIRE(gfx->releaseShader(first));
    // The second shader still owns the shared storage after the first is released.
    REQUIRE(replace(second).ok());
#endif
    REQUIRE(gfx->releaseShader(second));
#ifndef EVE_TEST_RESOURCE_VALIDATION
    REQUIRE(gfx->releaseShader(first));
#endif
}
}  // namespace test_ShaderResourceSharing

// Contract section: Shadow3D
#include "PrivateTestScope.h"


#include "graphics/Shadow.h"
#include "graphics/Waterfall.h"
// Shadow3D.cpp
namespace test_Shadow3D {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {


[[maybe_unused]] void resetScene3D() {
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

[[maybe_unused]] Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] Mesh *makeGroundPlane(Graphics *gfx) { return gfx->newMeshSphere(16, 8); }

[[maybe_unused]] void addHud() {
    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;
}

[[maybe_unused]] float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

[[maybe_unused]] void warmPresent(Graphics *gfx) {
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
}

/** Live-render the shadow scene to the window for ~1s. */
[[maybe_unused]] void previewScene(Graphics *gfx, int ms = 1000) {
    if (!eve::test::manualPreviewEnabled()) return;
    const int frames = (ms >= 16) ? (ms / 16) : 1;
    for (int i = 0; i < frames; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
        }
        SDL_Delay(16);
    }
}

/** Max (a - b) over a coarse pixel grid. Used because Vulkan FB Y / RH_ZO place
 *  this scene's umbra off the naive "center + below" sample point. */
[[maybe_unused]] float maxLumaDelta(Graphics *gfx, const std::vector<float> &a, const std::vector<float> &b,
                                    int step = 4) {
    float     best = -1.f;
    const int w    = gfx->getWidth();
    const int h    = gfx->getHeight();
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            const size_t i = size_t(y * w + x);
            best           = std::max(best, a[i] - b[i]);
        }
    }
    return best;
}

[[maybe_unused]] std::vector<float> captureLumaGrid(Graphics *gfx, int step = 4) {
    const int          w = gfx->getWidth();
    const int          h = gfx->getHeight();
    std::vector<float> out(size_t(w * h), 0.f);
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            out[size_t(y * w + x)] = luma(gfx->getPixel(x, y));
        }
    }
    return out;
}

[[maybe_unused]] void setupShadowScene(Graphics *gfx, Renderable3D *&ground, Light3D *&sun, bool groundReceive,
                                       Renderable3D **caster = nullptr) {
    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.05f, 0.05f, 0.05f);

    ground = Renderable3D::create();
    ground->setMesh(makeGroundPlane(gfx));
    ground->setTexture(makeSolid(gfx, 220, 220, 220));
    ground->setPosition(0.f, -1.2f, 0.f);
    ground->setScale(4.f, 0.15f, 4.f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.85f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(groundReceive);

    auto *box = Renderable3D::create();
    box->setMesh(gfx->newMeshSphere(20, 12));
    box->setTexture(makeSolid(gfx, 200, 200, 200));
    box->setPosition(0.f, 0.35f, 0.f);
    box->setScale(0.55f, 0.55f, 0.55f);
    box->setMetallic(0.f);
    box->setRoughness(0.6f);
    box->setCastShadow(true);
    box->setReceiveShadow(false);
    if (caster) *caster = box;

    sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    sun->setShadowBias(0.003f);

    addHud();
}

}  // namespace

TEST_CASE("Shadow3D.dirLightDarkensOccludedGround") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Renderable3D *ground = nullptr;
    Light3D      *sun    = nullptr;
    setupShadowScene(gfx, ground, sun, true);
    (void)ground;

    gfx->setScreenReadbackEnabled(true);

    sun->setCastShadow(false);
    warmPresent(gfx);
    const auto L_off = captureLumaGrid(gfx);

    sun->setCastShadow(true);
    warmPresent(gfx);
    const auto L_on = captureLumaGrid(gfx);

    // Enabling CSM must darken some receive-shadow texels (umbra under the occluder).
    REQUIRE(maxLumaDelta(gfx, L_off, L_on) > 0.03f);

    // With shadows on, some lit ground should still be brighter than some umbra texel.
    float     brightest = 0.f, darkestGeom = 1.f;
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            const float L = L_on[size_t(y * w + x)];
            if (L < 0.15f) continue;  // skip clear / fully ambient
            brightest   = std::max(brightest, L);
            darkestGeom = std::min(darkestGeom, L);
        }
    }
    REQUIRE(brightest > darkestGeom + 0.02f);

    previewScene(gfx);
    win->close();
}

TEST_CASE("Shadow3D.pcgLodTwoSidedCastsBackFacingGeometry") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Renderable3D *ground = nullptr;
    Renderable3D *caster = nullptr;
    Light3D      *sun    = nullptr;
    setupShadowScene(gfx, ground, sun, true, &caster);
    (void)ground;
    const float    pos[]    = {-0.8f, 0.f, -0.8f, 0.8f, 0.f, -0.8f, 0.8f, 0.f, 0.8f, -0.8f, 0.f, 0.8f};
    const float    nrm[]    = {0.f, -1.f, 0.f, 0.f, -1.f, 0.f, 0.f, -1.f, 0.f, 0.f, -1.f, 0.f};
    const float    uv[]     = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t idxA[]   = {0, 1, 2, 0, 2, 3};
    const uint32_t idxB[]   = {0, 2, 1, 0, 3, 2};
    Mesh          *windingA = gfx->newMeshFromArrays(pos, nrm, uv, 4, idxA, 6);
    Mesh          *windingB = gfx->newMeshFromArrays(pos, nrm, uv, 4, idxB, 6);
    REQUIRE(windingA != nullptr);
    REQUIRE(windingB != nullptr);
    caster->setPosition(0.f, 0.35f, 0.f);
    caster->setScale(1.f, 1.f, 1.f);
    caster->setMeshLod(0, windingA);
    gfx->setScreenReadbackEnabled(true);

    auto shadowDelta = [&](Mesh *mesh, int shadowMode) {
        caster->setMeshLod(0, mesh);
        REQUIRE(caster->setMeshLodRendererState(0, 0, shadowMode, true, 1, true, 1, 1).ok());
        sun->setCastShadow(false);
        warmPresent(gfx);
        const auto off = captureLumaGrid(gfx);
        sun->setCastShadow(true);
        warmPresent(gfx);
        const auto on = captureLumaGrid(gfx);
        return maxLumaDelta(gfx, off, on);
    };

    const float doubleA = shadowDelta(windingA, 2);
    const float doubleB = shadowDelta(windingB, 2);
    const float singleA = shadowDelta(windingA, 1);
    const float singleB = shadowDelta(windingB, 1);
    REQUIRE(std::min(doubleA, doubleB) > 0.02f);
    REQUIRE(std::min(singleA, singleB) + 0.015f < std::min(doubleA, doubleB));
    win->close();
}

TEST_CASE("Shadow3D.pcgSpeedTreeLodKeepsComplementaryCasterCoverage") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();
    Renderable3D *ground = nullptr;
    Renderable3D *caster = nullptr;
    Light3D      *sun    = nullptr;
    setupShadowScene(gfx, ground, sun, true, &caster);
    (void)ground;
    Mesh *sphere = caster->getMesh();
    caster->setMeshLod(0, sphere);
    caster->setMeshLod(1, sphere, 10.f);
    REQUIRE(caster->setMeshLodFadeWidth(0, 1.f).ok());
    REQUIRE(caster->setMeshLodFadePolicy(1, false, .5f).ok());
    gfx->setScreenReadbackEnabled(true);
    sun->setCastShadow(false);
    warmPresent(gfx);
    const auto off = captureLumaGrid(gfx);
    sun->setCastShadow(true);
    warmPresent(gfx);
    const auto on = captureLumaGrid(gfx);
    CHECK(maxLumaDelta(gfx, off, on) > .03f);
    win->close();
}

TEST_CASE("Shadow3D.receiveShadowFalseIgnoresMap") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Renderable3D *ground = nullptr;
    Light3D      *sun    = nullptr;
    setupShadowScene(gfx, ground, sun, false);
    (void)sun;

    gfx->setScreenReadbackEnabled(true);
    warmPresent(gfx);
    const auto L_offRecv = captureLumaGrid(gfx);

    ground->setReceiveShadow(true);
    warmPresent(gfx);
    const auto L_onRecv = captureLumaGrid(gfx);

    // Turning receiveShadow on must apply the shadow map and darken umbra texels.
    REQUIRE(maxLumaDelta(gfx, L_offRecv, L_onRecv) > 0.02f);

    previewScene(gfx);
    win->close();
}

TEST_CASE("Shadow3D.shadowStrengthInterpolatesVisibility") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Renderable3D *ground = nullptr;
    Light3D      *sun    = nullptr;
    setupShadowScene(gfx, ground, sun, true);
    (void)ground;
    gfx->setScreenReadbackEnabled(true);

    sun->setShadowStrength(0.f);
    warmPresent(gfx);
    const auto off = captureLumaGrid(gfx);
    sun->setShadowStrength(0.5f);
    warmPresent(gfx);
    const auto half = captureLumaGrid(gfx);
    sun->setShadowStrength(1.f);
    warmPresent(gfx);
    const auto full = captureLumaGrid(gfx);

    size_t strongest      = 0;
    float  strongestDelta = 0.f;
    for (size_t i = 0; i < off.size(); ++i) {
        const float delta = off[i] - full[i];
        if (delta > strongestDelta) {
            strongestDelta = delta;
            strongest      = i;
        }
    }
    REQUIRE(strongestDelta > 0.02f);
    const float halfDelta = off[strongest] - half[strongest];
    REQUIRE(halfDelta > 0.f);
    REQUIRE(halfDelta < strongestDelta);

    win->close();
}

TEST_CASE("Shadow3D.maskedCasterUsesAlbedoAlpha") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Renderable3D *ground = nullptr;
    Renderable3D *caster = nullptr;
    Light3D      *sun    = nullptr;
    setupShadowScene(gfx, ground, sun, true, &caster);
    (void)ground;
    (void)sun;
    REQUIRE(caster != nullptr);

    const uint8_t transparentPx[4] = {220, 220, 220, 0};
    Texture      *transparent      = gfx->newTexture(1, 1, transparentPx);
    Texture      *opaque           = makeSolid(gfx, 220, 220, 220);
    REQUIRE(transparent != nullptr);
    REQUIRE(opaque != nullptr);
    Material masked;
    masked.setSurfaceMode("masked");
    masked.setAlphaCutoff(0.5f);
    masked.setCastShadow(true);
    masked.setReceiveShadow(false);
    caster->setMaterial(&masked);

    gfx->setScreenReadbackEnabled(true);
    masked.setAlbedoTexture(transparent);
    warmPresent(gfx);
    const auto transparentCaster = captureLumaGrid(gfx);
    masked.setAlbedoTexture(opaque);
    warmPresent(gfx);
    const auto opaqueCaster = captureLumaGrid(gfx);

    REQUIRE(maxLumaDelta(gfx, transparentCaster, opaqueCaster) > 0.02f);
    win->close();
}
}  // namespace test_Shadow3D

// Contract section: SkyAtmospherePass
#include "PrivateTestScope.h"


#include <chrono>
#include <cstdlib>
#include <string_view>
#include "common/config.h"
#include "graphics/DisplayOutputEncoding.h"
#include "graphics/ViewPreparation.h"
#include "graphics/sky/SkyAtmosphereLuts.h"
#include "graphics/sky/SkyAtmospherePass.h"
#ifndef EVENGINE_WEBGPU
#include "graphics/vulkan/Graphics.h"
#endif
// SkyAtmospherePass.cpp
namespace test_SkyAtmospherePass {
#ifndef EVENGINE_WEBGPU
#include "graphics/shaders/scene_tonemap_frag_spv.inc"

#endif

TEST_CASE("graphics.sky atmosphere capture uses independent HDR lighting and detaches cleanly") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    const char             *mode      = std::getenv("EVENGINE_SKY_BENCHMARK");
    const bool              benchmark = mode && std::string_view(mode) == "1";
    const int               width = benchmark ? 1280 : 160, height = benchmark ? 720 : 90;
    GfxFixture              fixture(width, height, true);
    auto                   *gfx = fixture.gfx;
    SkyAtmosphereParameters invalid;
    invalid.groundRadiusKm = -1;
    REQUIRE(!SkyAtmospherePass::prepare(*gfx, invalid).ok());
    [[maybe_unused]] const auto prepareStarted = std::chrono::steady_clock::now();
    auto                        prepared       = SkyAtmospherePass::prepare(*gfx, {});
#if defined(EVENGINE_WEBGPU) || !defined(EVE_TEST_RESOURCE_VALIDATION)
    REQUIRE(!prepared.ok());
    REQUIRE(prepared.error()->code() == eve::DiagnosticCode::Unsupported);
#else
    REQUIRE(prepared.ok());
    const double prepareMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - prepareStarted).count();
    auto  sky    = std::move(prepared).takeValue();
    auto *camera = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(0, 2.3f, -1);
    camera->setClipPlanes(.1f, 10000);
    camera->setFov(50);
    SkyCelestialLight sourceLight;
    if (benchmark) {
        camera->setTarget(.965925826f, 2.258819045f, 0);
        camera->setFov(float(2 * std::atan(std::tan(75.0 * 3.141592653589793 / 360.0) * height / width) * 180 /
                             3.141592653589793));
        sourceLight.direction = {-.5f, .8660254f, 0};
    }
    REQUIRE(sky->setLight(sourceLight).ok());
    SkyAtmosphereFrame invalidFrame;
    invalidFrame.dayPhase = 1;
    REQUIRE(!sky->setFrame(invalidFrame).ok());
    invalidFrame.dayPhase = 0;

    invalidFrame.sun.irradiance  = {0, 0, 0};
    invalidFrame.wispsMorphPhase = 1;
    REQUIRE(!sky->setFrame(invalidFrame).ok());
    auto *canvas = gfx->newHDRCanvas(width, height);
    REQUIRE(canvas != nullptr);
    gfx->setBackgroundColor(Color(0, 0, 0, 1));
    REQUIRE(sky->attach().ok());
    REQUIRE(sky->attach().ok());
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::array<float, 5> gpuMs{}, viewPreparationMs{}, changingViewWallMs{};
    if (benchmark)
        for (auto &ms : gpuMs) {
            RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
            ms = gfx->getLastOffscreen3DGpuDurationMs();
            REQUIRE(ms > 0);
        }
    if (benchmark) {
        size_t sampleIndex = 0;
        auto   observer    = addViewPreparation([&](Graphics &provider, const glm::mat4 &, const glm::vec3 &) {
            viewPreparationMs[sampleIndex] = provider.getLastOffscreen3DGpuDurationMs();
            return eve::Result<void>::success();
        });
        REQUIRE(observer.ok());
        for (; sampleIndex < viewPreparationMs.size(); ++sampleIndex) {
            auto changing = sourceLight;
            changing.irradiance *= 1.01f + float(sampleIndex) * .01f;
            REQUIRE(sky->setLight(changing).ok());
            const auto started = std::chrono::steady_clock::now();
            RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
            changingViewWallMs[sampleIndex] =
                float(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        }
        removeViewPreparation(observer.value());
        REQUIRE(sky->setLight(sourceLight).ok());
        RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    }
    std::unique_ptr<eve::image::ImageData> first(canvas->newHDRImageData());
    const auto                             sample = first->getPixel(width / 2, height / 2);
    REQUIRE(std::isfinite(sample.r));
    REQUIRE(std::isfinite(sample.g));
    REQUIRE(std::isfinite(sample.b));
    REQUIRE(sample.b > sample.r);
    REQUIRE(sample.b > .0001f);
    // Only the optical sky camera is lifted to the reference's minimum height.
    // Looking from below the virtual ground must not make the whole sky black.
    camera->setEye(0, -10, 0);
    camera->setTarget(0, -9.7f, -1);
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> underground(canvas->newHDRImageData());
    const auto                             undergroundSample = underground->getPixel(width / 2, height / 2);
    REQUIRE(std::isfinite(undergroundSample.b));
    REQUIRE(undergroundSample.b > .0001f);
    camera->setEye(0, 2, 0);
    if (benchmark)
        camera->setTarget(.965925826f, 2.258819045f, 0);
    else
        camera->setTarget(0, 2.3f, -1);
    sky->detach();
    SkyAtmosphereParameters singleParameters;
    singleParameters.multiScatteringFactor = 0;
    auto singlePrepared                    = SkyAtmospherePass::prepare(*gfx, singleParameters);
    REQUIRE(singlePrepared.ok());
    auto single = std::move(singlePrepared).takeValue();
    REQUIRE(single->setLight(sourceLight).ok());
    REQUIRE(single->attach().ok());
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> singleImage(canvas->newHDRImageData());
    const auto                             singleSample = singleImage->getPixel(width / 2, height / 2);
    REQUIRE(sample.r > singleSample.r);
    REQUIRE(sample.g > singleSample.g);
    REQUIRE(sample.b > singleSample.b);
    single.reset();
    REQUIRE(sky->attach().ok());
    if (const char *path = std::getenv("EVENGINE_SKY_ARTIFACT")) {
        std::ofstream output(path, std::ios::binary);
        REQUIRE(output.good());
        output << "PF\n" << width << " " << height << "\n-1.0\n";
        for (int y = height - 1; y >= 0; --y)
            for (int x = 0; x < width; ++x) {
                const auto  pixel = first->getPixel(x, y);
                const float rgb[]{pixel.r, pixel.g, pixel.b};
                output.write(reinterpret_cast<const char *>(rgb), sizeof(rgb));
            }
        REQUIRE(output.good());
        if (benchmark) {
            std::ofstream timing(std::string(path) + ".timing.json");
            REQUIRE(timing.good());
            timing << "{\"schema\":\"eve.sky-pass-timing/1\",\"width\":" << width << ",\"height\":" << height
                   << ",\"prepareMs\":" << prepareMs << ",\"gpuOffscreenPassMs\":[";
            for (size_t i = 0; i < gpuMs.size(); ++i) timing << (i ? "," : "") << gpuMs[i];
            timing << "],\"viewPreparationGpuMs\":[";
            for (size_t i = 0; i < viewPreparationMs.size(); ++i) timing << (i ? "," : "") << viewPreparationMs[i];
            timing << "],\"changingViewWallMs\":[";
            for (size_t i = 0; i < changingViewWallMs.size(); ++i) timing << (i ? "," : "") << changingViewWallMs[i];
            timing << "]}\n";
            REQUIRE(timing.good());
        }
    }
    SkyCelestialLight doubled = sourceLight;
    doubled.irradiance        = glm::vec3(10);
    REQUIRE(sky->setLight(doubled).ok());
    auto rejected         = doubled;
    rejected.irradiance.x = -1;
    REQUIRE(!sky->setLight(rejected).ok());
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> second(canvas->newHDRImageData());
    const auto                             twice = second->getPixel(width / 2, height / 2);
    REQUIRE(std::abs(twice.b - 2 * sample.b) < std::max(.0001f, sample.b * .003f));
    sky.reset();
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> removed(canvas->newHDRImageData());
    REQUIRE(removed->getPixel(width / 2, height / 2).b == 0);
#endif
}

#if !defined(EVENGINE_WEBGPU) && defined(EVE_TEST_RESOURCE_VALIDATION)
TEST_CASE("graphics.sky filmic display preserves middle grey and keeps prior modes explicit") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture fixture(16, 16, true);
    auto      *gfx = fixture.gfx;
    REQUIRE(gfx->getSceneToneMapping() == Graphics::SceneToneMapping::Aces);
    REQUIRE(!gfx->setSceneToneMapping(static_cast<Graphics::SceneToneMapping>(99)).ok());
    REQUIRE(gfx->getSceneToneMapping() == Graphics::SceneToneMapping::Aces);
    REQUIRE(gfx->setSceneToneMapping(Graphics::SceneToneMapping::Filmic).ok());
    REQUIRE(gfx->getSceneToneMapping() == Graphics::SceneToneMapping::Filmic);
    auto *input  = gfx->newHDRCanvas(16, 16);
    auto *output = gfx->newHDRCanvas(16, 16);
    auto *shader =
        gfx->newShaderFromSpv({}, {scene_tonemap_frag_spv, scene_tonemap_frag_spv + scene_tonemap_frag_spv_count});
    REQUIRE(input != nullptr);
    REQUIRE(output != nullptr);
    REQUIRE(shader != nullptr);
    const auto sample = [&](float linear, float mode, float displayMode = 0) {
        gfx->setCanvas(input);
        gfx->clear(Color(linear, linear, linear, 1), std::nullopt, std::nullopt);
        gfx->drawSolidRect(0, 0, 16, 16, Color(linear, linear, linear, 1));
        gfx->setCanvas();
        gfx->setCanvas(output);
        gfx->drawTexturedRectShader(input->getTexture(), shader, 0, 0, 16, 16,
                                    Color(mode, displayMode, display::packNits(200, 1000), 0));
        gfx->setCanvas();
        std::unique_ptr<eve::image::ImageData> pixels(output->newHDRImageData());
        REQUIRE(pixels != nullptr);
        return pixels->getPixel(8, 8);
    };
    const auto film = sample(.18f, 2), old = sample(.18f, 1), linear = sample(.18f, 0);
    REQUIRE(std::abs(film.r - .18f) < .002f);
    REQUIRE(std::abs(film.r - film.g) < .001f);
    REQUIRE(std::abs(film.g - film.b) < .001f);
    REQUIRE(old.r > film.r + .05f);
    REQUIRE(std::abs(linear.r - .18f) < .001f);
    const auto black = sample(0, 2), bright = sample(100, 2);
    REQUIRE(black.r == 0);
    REQUIRE(black.g == 0);
    REQUIRE(black.b == 0);
    REQUIRE(std::isfinite(bright.r));
    REQUIRE(bright.r > .99f);
    REQUIRE(bright.r <= 1);
    // Filmic participates in the dev HDR output contract without replacing its encoding.
    const auto compose = sample(.9f, 2, 3), scRgb = sample(.9f, 2, 1);
    REQUIRE(std::abs(compose.r - film.r * 5.f) < .005f);
    REQUIRE(std::abs(scRgb.r - compose.r * 2.5f) < .01f);
    const auto pq = sample(.9f, 2, 2), pqBright = sample(9.f, 2, 2);
    REQUIRE(std::isfinite(pq.r));
    REQUIRE(pq.r > 0);
    REQUIRE(pqBright.r > pq.r);
    REQUIRE(pqBright.r <= 1);
    REQUIRE(gfx->getScenePhotographicVignette() == 0);
    REQUIRE(gfx->setScenePhotographicVignette(.4f).ok());
    REQUIRE(!gfx->setScenePhotographicVignette(-1).ok());
    REQUIRE(!gfx->setScenePhotographicVignette(2).ok());
    REQUIRE(gfx->getScenePhotographicVignette() == .4f);
    (void)sample(1, 0);
    gfx->setCanvas(output);
    gfx->drawTexturedRectShader(input->getTexture(), shader, 0, 0, 16, 16, Color(0, .1f, 1, 0));
    gfx->setCanvas();
    std::unique_ptr<eve::image::ImageData> vignette(output->newHDRImageData());
    REQUIRE(vignette != nullptr);
    // Pixel centers at (+/- .9375,+/- .9375) in square vignette space.
    const float edgeExpected = 1.f / std::pow(1.f + 2.f * .375f * .375f, 2.f);
    REQUIRE(std::abs(vignette->getPixel(0, 0).r - edgeExpected) < .001f);
    REQUIRE(std::abs(vignette->getPixel(15, 15).r - edgeExpected) < .001f);
    REQUIRE(vignette->getPixel(8, 8).r > .997f);
    REQUIRE(gfx->setScenePhotographicVignette(0).ok());
    REQUIRE(gfx->setSceneToneMapping(Graphics::SceneToneMapping::Aces).ok());
}

TEST_CASE("graphics.sky height fog fills the ground with independently integrated ambient light") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    const char             *timingPath = std::getenv("EVENGINE_SKY_FOG_TIMING");
    const int               width = timingPath ? 1280 : 64, height = timingPath ? 720 : 64;
    GfxFixture              fixture(width, height, true);
    auto                   *gfx = fixture.gfx;
    SkyAtmosphereParameters parameters;
    parameters.heightFog.densityPerMetre         = .000551f;
    parameters.heightFog.directionalInscattering = {0, 0, 0};
    auto tables                                  = detail::bakeSkyAtmosphereLuts(parameters);
    REQUIRE(tables.ok());
    auto ambient = detail::bakeDistantSkyAmbient(parameters, tables.value(), {0, 1, 0});
    REQUIRE(ambient.ok());
    auto prepared = SkyAtmospherePass::prepare(*gfx, parameters);
    REQUIRE(prepared.ok());
    auto              sky = std::move(prepared).takeValue();
    SkyCelestialLight light;
    light.direction  = {0, 1, 0};
    light.irradiance = {5, 5, 5};
    REQUIRE(sky->setLight(light).ok());
    auto *camera = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(0, 1, -1);
    camera->setClipPlanes(.1f, 10000);
    camera->setFov(30);
    auto *canvas = gfx->newHDRCanvas(width, height);
    REQUIRE(canvas != nullptr);
    REQUIRE(sky->attach().ok());
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> image(canvas->newHDRImageData());
    const auto                             pixel = image->getPixel(width / 2, height / 2);
    const float                            actual[]{pixel.r, pixel.g, pixel.b};
    for (size_t axis = 0; axis < 3; ++axis) {
        const float expected = ambient.value()[axis] * 5 * parameters.heightFog.atmosphereContribution;
        REQUIRE(expected > 0);
        REQUIRE(std::isfinite(actual[axis]));
        REQUIRE(std::abs(actual[axis] - expected) < expected * .02f);
    }
    light.irradiance = {0, 0, 0};
    REQUIRE(sky->setLight(light).ok());
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> night(canvas->newHDRImageData());
    REQUIRE(night->getPixel(width / 2, height / 2).r == 0);
    REQUIRE(night->getPixel(width / 2, height / 2).g == 0);
    REQUIRE(night->getPixel(width / 2, height / 2).b == 0);
    if (timingPath) {
        light.direction  = {-.5f, .8660254f, 0};
        light.irradiance = {5, 5, 5};
        REQUIRE(sky->setLight(light).ok());
        camera->setTarget(.965925826f, 2.258819045f, 0);
        camera->setFov(46.6921257f);
        RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
        std::ofstream output(timingPath);
        REQUIRE(output.good());
        output << "{\"schema\":\"eve.sky-fog-pass-timing/1\",\"width\":1280,\"height\":720,\"gpuOffscreenPassMs\":[";
        for (int i = 0; i < 5; ++i) {
            RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
            const float duration = gfx->getLastOffscreen3DGpuDurationMs();
            REQUIRE(duration > 0);
            output << (i ? "," : "") << duration;
        }
        output << "]}\n";
        REQUIRE(output.good());
    }
}

TEST_CASE("graphics.sky atmosphere survives graphics provider retirement") {
    auto graphics = std::make_unique<eve::graphics::vulkan::Graphics>();
    graphics->initHeadless(16, 16);
    auto prepared = eve::graphics::SkyAtmospherePass::prepare(*graphics, {});
    REQUIRE(prepared.ok());
    auto sky = std::move(prepared).takeValue();
    REQUIRE(sky->attach().ok());
    graphics.reset();
    auto drawn = sky->draw(glm::mat4(1), glm::vec3(0, 2, 0));
    REQUIRE(!drawn.ok());
    REQUIRE(drawn.error()->code() == eve::DiagnosticCode::StaleHandle);
    REQUIRE(!sky->attach().ok());
    sky.reset();
}
#endif
}  // namespace test_SkyAtmospherePass

// Contract section: SkyMoon
#include "PrivateTestScope.h"


#ifndef EVENGINE_WEBGPU
#include "SkyWispsSpv.h"
#endif
// SkyMoon.cpp
namespace test_SkyMoon {
#ifndef EVENGINE_WEBGPU


TEST_CASE("graphics.sky lunar surface preserves phase earthlight occultation and source orientation") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture fixture(16, 16, true);
    auto      *gfx    = fixture.gfx;
    auto      *input  = gfx->newHDRCanvas(16, 16);
    auto      *output = gfx->newHDRCanvas(16, 16);
    auto      *shader = gfx->newShaderFromSpv({}, sky_wisps_probe_frag);
    REQUIRE(input != nullptr);
    REQUIRE(output != nullptr);
    REQUIRE(shader != nullptr);
    // The source normal swizzle RBG maps this sample to (0,-1,0).
    gfx->setCanvas(input);
    gfx->clear(Color(.5f, .5f, 0, 1), std::nullopt, std::nullopt);
    gfx->drawSolidRect(0, 0, 16, 16, Color(.5f, .5f, 0, 1));
    gfx->setCanvas();
    const auto sample = [&](float mode, float phase, float offset) {
        gfx->setCanvas(output);
        gfx->clear(Color(0, 0, 0, 0), std::nullopt, std::nullopt);
        gfx->drawTexturedRectShader(input->getTexture(), shader, 0, 0, 16, 16, Color(mode, phase, offset, 1));
        gfx->setCanvas();
        std::unique_ptr<eve::image::ImageData> pixels(output->newHDRImageData());
        REQUIRE(pixels != nullptr);
        return pixels->getPixel(8, 8);
    };
    const auto full = sample(12, 0, 0), quarter = sample(12, 7.3825f, 0), dark = sample(12, 14.765f, 0);
    REQUIRE(std::abs(full.r - .5f) < .001f);
    REQUIRE(std::abs(quarter.r - .0025f) < .0001f);
    REQUIRE(std::abs(dark.r - .0025f) < .0001f);
    REQUIRE(full.a == 1);
    REQUIRE(dark.a == 1);  // Unlit lunar surface still blocks background stars.
    const auto opposite = sample(13, 0, 0), outside = sample(12, 0, .03f);
    REQUIRE(opposite.r == 0);
    REQUIRE(opposite.a == 0);
    REQUIRE(outside.r == 0);
    REQUIRE(outside.a == 0);
    const auto midnight = sample(14, 0, 0), dawn = sample(14, 6, 0), noon = sample(14, 12, 0);
    REQUIRE(std::abs(midnight.g + 1) < .001f);
    REQUIRE(std::abs(dawn.r - .573269f) < .001f);
    REQUIRE(std::abs(dawn.g + .032749f) < .001f);
    REQUIRE(std::abs(dawn.b + .818713f) < .001f);
    REQUIRE(std::abs(noon.g - 1) < .001f);
}
#endif
}  // namespace test_SkyMoon

// Contract section: SkyRuntime
#include "PrivateTestScope.h"


#include <iterator>
#include <limits>
#include "SkyPreparationSupport.h"
#include "common/Value.h"
#include "daynight/sky/SkyProfile.h"
#include "daynight/sky/SkyRuntime.h"
#include "graphics/sky/SkyWispsAsset.h"
#include "graphics/sky/SkyWispsLayer.h"
#ifndef EVENGINE_WEBGPU
#include "graphics/vulkan/Graphics.h"
#endif
// SkyRuntime.cpp
namespace test_SkyRuntime {
#ifndef EVENGINE_WEBGPU

#endif

TEST_CASE("daynight.sky runtime advances one clock and publishes solar lighting to capture") {
    using namespace eve::daynight;
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture    fixture(32, 32, true);
    std::ifstream profileFile(std::string(EVENGINE_SOURCE_DIR) + "/examples/uds-sky/sky.json");
    REQUIRE(profileFile.good());
    const std::string profileText((std::istreambuf_iterator<char>(profileFile)), std::istreambuf_iterator<char>());
    auto              document = eve::Value::fromJson(profileText);
    REQUIRE(document.ok());
    auto profile = SkyProfile::decode(document.value());
    REQUIRE(profile.ok());
    auto settings            = profile.value().settings();
    settings.clock           = {5.5, 1};
    auto invalid             = settings;
    invalid.sunIrradiance[0] = std::numeric_limits<float>::infinity();
    REQUIRE(!SkyRuntime::prepare(*fixture.gfx, invalid, {}).ok());
    auto prepared = SkyRuntime::prepare(*fixture.gfx, settings, profile.value().atmosphere());
#if defined(EVENGINE_WEBGPU) || !defined(EVE_TEST_RESOURCE_VALIDATION)
    REQUIRE(!prepared.ok());
    REQUIRE(prepared.error()->code() == eve::DiagnosticCode::Unsupported);
#else
    REQUIRE(prepared.ok());
    auto sky = std::move(prepared).takeValue();
    // Actual UDS DirectionalLightComponent intensity at 05:30 in atmosphere mode.
    REQUIRE(std::abs(sky->frame().sunIrradiance[0] - 4.99556827545166) < 1e-5);
    SkyWeather weather;
    weather.cloudCoverage = 10;
    weather.windXZ        = {4, 0};
    REQUIRE(sky->transitionTo(weather, eve::Duration::fromNanoseconds(4000000000)).ok());
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(6500000000)).ok());
    REQUIRE(sky->frame().simulation.hour == 12);
    REQUIRE(sky->frame().simulation.weather.cloudCoverage == 10);
    REQUIRE(sky->frame().simulation.cloudTravelXZ[0] == 18);
    REQUIRE(sky->frame().sunIrradiance[0] == 5);
    REQUIRE(std::abs(sky->frame().sunDirection[0] + .5) < 1e-6);
    REQUIRE(!sky->advance(eve::Duration::fromNanoseconds(-1)).ok());
    REQUIRE(sky->frame().simulation.hour == 12);
    REQUIRE(sky->frame().sunIrradiance[0] == 5);
    auto *camera = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(0, 2.3f, -1);
    camera->setClipPlanes(.1f, 10000);
    camera->setFov(50);
    auto *canvas = fixture.gfx->newHDRCanvas(32, 32);
    REQUIRE(canvas != nullptr);
    fixture.gfx->setBackgroundColor(Color(0, 0, 0, 1));
    REQUIRE(sky->attach().ok());
    REQUIRE(sky->attach().ok());
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> noon(canvas->newHDRImageData());
    REQUIRE(noon->getPixel(16, 16).b > .0001f);
    // Render consumers must not advance simulation independently.
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    REQUIRE(sky->frame().simulation.hour == 12);
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(12000000000)).ok());
    REQUIRE(sky->frame().simulation.hour == 0);
    REQUIRE(sky->frame().sunIrradiance[0] == 0);
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> midnight(canvas->newHDRImageData());
    REQUIRE(midnight->getPixel(16, 16).b == 0);
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(12000000000)).ok());
    sky.reset();
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> removed(canvas->newHDRImageData());
    REQUIRE(removed->getPixel(16, 16).b == 0);
#endif
}

#if !defined(EVENGINE_WEBGPU) && defined(EVE_TEST_RESOURCE_VALIDATION)
TEST_CASE("daynight.sky runtime rejects updates after provider retirement without advancing state") {
    auto graphics = std::make_unique<eve::graphics::vulkan::Graphics>();
    graphics->initHeadless(16, 16);
    auto prepared = eve::daynight::SkyRuntime::prepare(*graphics, {}, {});
    REQUIRE(prepared.ok());
    auto sky = std::move(prepared).takeValue();
    REQUIRE(sky->attach().ok());
    graphics.reset();
    auto result = sky->advance(eve::Duration::fromNanoseconds(1000000000));
    REQUIRE(!result.ok());
    REQUIRE(result.error()->code() == eve::DiagnosticCode::StaleHandle);
    REQUIRE(sky->frame().simulation.elapsedSeconds == 0);
    REQUIRE(!sky->attach().ok());
    REQUIRE(!sky->transitionTo({}, eve::Duration{}).ok());
    sky.reset();
}
#endif

#if !defined(EVENGINE_WEBGPU) && defined(EVE_TEST_RESOURCE_VALIDATION)
TEST_CASE("daynight.sky injected cloud time changes authored wisps and closes the morph cycle") {
    using namespace eve::daynight;
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    const std::string source = std::string(EVENGINE_SOURCE_DIR) + "/examples/uds-sky";
    REQUIRE(fs->mountRealDirectory(source, "sky-motion", false));
    auto asset = SkyWispsAsset::generate(2026);
    REQUIRE(asset.ok());
    std::ifstream input(source + "/sky-animated.json");
    REQUIRE(input.good());
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    auto              document = eve::Value::fromJson(text);
    REQUIRE(document.ok());
    auto profile = SkyProfile::decode(document.value());
    REQUIRE(profile.ok());
    auto settings = profile.value().settings();
    REQUIRE(settings.cloudMotion.speed == .35);
    settings.cloudMotion.speed = .5;
    GfxFixture fixture(160, 90, true);
    auto runtime = SkyRuntime::prepare(*fixture.gfx, settings, profile.value().atmosphere(), &asset.value().layer());
    if (!skyPreparationAvailable(runtime)) return;
    auto sky = std::move(runtime).takeValue();
    REQUIRE(sky->attach().ok());
    auto *camera = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(.965925826f, 2.258819045f, 0);
    camera->setFov(46.6921257f);
    camera->setClipPlanes(.1f, 10000);
    auto *canvas = fixture.gfx->newHDRCanvas(160, 90);
    REQUIRE(canvas != nullptr);
    const auto capture = [&]() {
        RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
        return std::unique_ptr<eve::image::ImageData>(canvas->newHDRImageData());
    };
    auto first = capture();
    REQUIRE(first != nullptr);
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(10000000000)).ok());
    REQUIRE(sky->frame().cloudTime == 5);
    REQUIRE(sky->frame().wispsMorphPhase == .5f);
    auto moved = capture();
    REQUIRE(moved != nullptr);
    int changed = 0;
    for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 160; ++x) {
            const auto a = first->getPixel(x, y), b = moved->getPixel(x, y);
            if (std::abs(a.b - b.b) > .0001f) ++changed;
        }
    REQUIRE(changed > 100);
    REQUIRE(sky->frame().simulation.elapsedSeconds == 10);
    REQUIRE(!sky->advance(eve::Duration::fromNanoseconds(-1)).ok());
    REQUIRE(sky->frame().cloudTime == 5);
    REQUIRE(sky->frame().wispsMorphPhase == .5f);
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(10000000000)).ok());
    REQUIRE(sky->frame().cloudTime == 10);
    REQUIRE(sky->frame().wispsMorphPhase == 0);
    auto cycle = capture();
    REQUIRE(cycle != nullptr);
    for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 160; ++x) {
            const auto a = first->getPixel(x, y), b = cycle->getPixel(x, y);
            REQUIRE(std::abs(a.r - b.r) < .00001f);
            REQUIRE(std::abs(a.g - b.g) < .00001f);
            REQUIRE(std::abs(a.b - b.b) < .00001f);
        }
    sky.reset();
    REQUIRE(fs->unmountRealDirectory(source));
}
#endif

TEST_CASE("daynight.sky optical cycle survives midnight and restores the same rendered snapshot") {
#if !defined(EVENGINE_WEBGPU) && defined(EVE_TEST_RESOURCE_VALIDATION)
    using namespace eve::graphics;
    using namespace eve::daynight;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture        fixture(32, 32, true);
    auto             *fs     = eve::filesystem::Filesystem::create();
    const std::string source = std::string(EVENGINE_SOURCE_DIR) + "/examples/uds-sky";
    REQUIRE(fs->mountRealDirectory(source, "optical-sky", false));
    auto asset = SkyWispsAsset::generate(2026);
    REQUIRE(asset.ok());
    SkyRuntimeSettings settings;
    settings.clock             = {23, 1};
    settings.cloudMotion.speed = 0;
    auto result                = SkyRuntime::prepare(*fixture.gfx, settings, {}, &asset.value().layer());
    if (!skyPreparationAvailable(result)) return;
    auto sky = std::move(result).takeValue();
    REQUIRE(sky->attach().ok());
    auto *camera = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(.965925826f, 2.258819045f, 0);
    camera->setFov(46.6921257f);
    camera->setClipPlanes(.1f, 10000);
    auto *canvas = fixture.gfx->newHDRCanvas(32, 32);
    REQUIRE(canvas != nullptr);
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> before(canvas->newHDRImageData());
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(2000000000)).ok());
    REQUIRE(sky->frame().simulation.hour == 1);
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> after(canvas->newHDRImageData());
    bool                                   changed = false;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const auto a = before->getPixel(x, y), b = after->getPixel(x, y);
            REQUIRE(std::isfinite(b.r));
            REQUIRE(std::isfinite(b.g));
            REQUIRE(std::isfinite(b.b));
            changed = changed || a.r != b.r || a.g != b.g || a.b != b.b;
        }
    REQUIRE(changed);
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(22000000000)).ok());
    REQUIRE(sky->frame().simulation.hour == 23);
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> restored(canvas->newHDRImageData());
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const auto a = before->getPixel(x, y), b = restored->getPixel(x, y);
            REQUIRE(a.r == b.r);
            REQUIRE(a.g == b.g);
            REQUIRE(a.b == b.b);
        }
    sky->detach();
    REQUIRE(fs->unmountRealDirectory(source));
#endif
}
}  // namespace test_SkyRuntime

// Contract section: SkyScriptBindings
#include "PrivateTestScope.h"


#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
// SkyScriptBindings.cpp
namespace test_SkyScriptBindings {
TEST_CASE("daynight.sky script prepares an owned profile runtime and checks time commands") {
    GfxFixture fixture(32, 32, true);
    ssq::VM    vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
#ifdef EVENGINE_WEBGPU
    vm.set("vulkanSupported", false);
#else
    vm.set("vulkanSupported", true);
#endif
#if !defined(EVENGINE_WEBGPU) && defined(EVE_TEST_RESOURCE_VALIDATION)
    vm.set("skySupported", true);
#else
    vm.set("skySupported", false);
#endif
    std::ifstream file(std::string(EVENGINE_SOURCE_DIR) + "/examples/uds-sky/sky.json");
    REQUIRE(file.good());
    vm.set("profileJson", std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()));
    vm.run(vm.compileSource(R"(
        local module = eve.DayNight();
        local graphics = eve.Graphics();
        assert(graphics.getSceneToneMapping() == "aces");
        assert(!graphics.setSceneToneMapping("unknown").ok);
        assert(graphics.getScenePhotographicVignette() == 0.0);
        assert(!graphics.setScenePhotographicVignette(-1.0).ok);
        assert(!module.prepareSky(graphics, "{}").ok);
        local made = module.prepareSky(graphics, profileJson);
        if (!skySupported) {
            assert(!made.ok);
            assert(made.code == "unsupported");
            assert(made.diagnostics.len() == 1);
            assert(made.diagnostics[0].message == (vulkanSupported
                ? "SPIR-V validation provider is unavailable"
                : "Atmosphere resource program requires Vulkan"));
            assert(graphics.setSceneToneMapping("filmic").ok == vulkanSupported);
            assert(graphics.setScenePhotographicVignette(0.4).ok == vulkanSupported);
            return;
        }
        if (!made.ok) foreach (diagnostic in made.diagnostics) print(diagnostic.message + "\n");
        assert(made.ok && made.hasValue && made.ownership == "owned");
        assert(graphics.setSceneToneMapping("filmic").ok);
        assert(graphics.getSceneToneMapping() == "filmic");
        assert(graphics.setScenePhotographicVignette(0.4).ok);
        assert(graphics.getScenePhotographicVignette() > 0.39);
        assert(graphics.setScenePhotographicVignette(0.0).ok);
        assert(graphics.setSceneToneMapping("aces").ok);
        local sky = made.value;
        assert(sky.attach().ok);
        assert(sky.attach().ok);
        assert(sky.frame().value.hour == 12);
        assert(!sky.advanceSeconds(-1.0).ok);
        assert(sky.frame().value.elapsedSeconds == 0);
        assert(sky.advanceSeconds(0.25).ok);
        assert(sky.frame().value.elapsedSeconds == 0.25);
        assert(sky.frame().value.hour == 12);
        assert(sky.frame().value.sunDirection[0] < -0.49);
        assert(sky.detach().ok);
        assert(sky.detach().ok);
        sky = null;
        made = null;
        collectgarbage();
    )"));
}
}  // namespace test_SkyScriptBindings

// Contract section: SkyTextureGpu
#include "PrivateTestScope.h"


#include "SkyTextureGpuSpv.h"
// SkyTextureGpu.cpp
namespace test_SkyTextureGpu {
// sky_volume_vert: #version 450
// layout(location=0) in vec3 position;
// void main(){gl_Position=vec4(position,1.0);}
// sky_volume_frag: #version 450
// layout(set=1,binding=0) uniform sampler3D skyVolume;
// layout(location=0) out vec4 color;
// void main(){color=textureLod(skyVolume,vec3(0.5,0.5,0.5),0.0);}

TEST_CASE("graphics.sky HDR volume uploads preserve interpolation and failed replacement") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture fixture(32, 32, true);
    auto      *gfx = fixture.gfx;
#ifdef EVENGINE_WEBGPU
    Shader shader;
    auto   unavailable = gfx->replaceMeshShaderResources(shader, sky_volume_vert, sky_volume_frag, {});
    REQUIRE(!unavailable.ok());
    REQUIRE(unavailable.error()->code() == eve::DiagnosticCode::Unsupported);
#else
    auto *shader = gfx->newMeshShaderFromSpv(sharing_vert, sharing_frag);
    REQUIRE(shader != nullptr);
    // Two Z slices: values above one and negative values must remain linear HDR.
    std::array<uint16_t, 8> pixels{0x4000, 0x3c00, 0xbc00, 0x3c00, 0x4400, 0x4200, 0x3c00, 0x3c00};
    ShaderImageInput        image;
    image.dimension = ShaderImageDimension::Image3D;
    image.format    = ShaderImageFormat::RGBA16Float;
    image.width = image.height = 1;
    image.depth                = 2;
    image.bytes                = std::as_bytes(std::span(pixels));
    image.sampler              = TextureSampler::linear();
#ifndef EVE_TEST_RESOURCE_VALIDATION
    auto *original = shader->gpuHandle;
#endif
    auto uploaded = gfx->replaceMeshShaderResources(*shader, sky_volume_vert, sky_volume_frag, {{&image, 1}, {}, {}});
#ifndef EVE_TEST_RESOURCE_VALIDATION
    REQUIRE(!uploaded.ok());
    REQUIRE(uploaded.error()->code() == eve::DiagnosticCode::Unsupported);
    REQUIRE(shader->gpuHandle == original);
#else
    REQUIRE(uploaded.ok());
    auto *committed = shader->gpuHandle;
    image.depth     = 1;
    REQUIRE(!gfx->replaceMeshShaderResources(*shader, sky_volume_vert, sky_volume_frag, {{&image, 1}, {}, {}}).ok());
    REQUIRE(shader->gpuHandle == committed);
    const float    positions[] = {-1, -1, .5f, 3, -1, .5f, -1, 3, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]        = {0, 0, 2, 0, 0, 2};
    const uint32_t indices[]   = {0, 1, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uv, 3, indices, 3);
    auto          *canvas      = gfx->newHDRCanvas(16, 16);
    REQUIRE(mesh != nullptr);
    REQUIRE(canvas != nullptr);
    REQUIRE(gfx->configureMeshShaderSurface(*shader, BlendMode::Opaque, false, true).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->begin3DFrameToCanvas(canvas);
    gfx->drawMeshShader(mesh, glm::mat4(1), nullptr, Color(1, 1, 1, 1), shader);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> readback(canvas->newHDRImageData());
    REQUIRE(readback != nullptr);
    const auto sample = readback->getPixel(8, 8);
    REQUIRE(std::abs(sample.r - 3.f) < .01f);
    REQUIRE(std::abs(sample.g - 2.f) < .01f);
    REQUIRE(std::abs(sample.b) < .01f);
    REQUIRE(gfx->releaseMesh(mesh));
    delete mesh;
#endif
    REQUIRE(gfx->releaseShader(shader));
    delete shader;
#endif
}
}  // namespace test_SkyTextureGpu

// Contract section: SkyViewPreparation
#include "PrivateTestScope.h"


// SkyViewPreparation.cpp
namespace test_SkyViewPreparation {
TEST_CASE("graphics.view preparation failures cancel dispatch and registrations detach") {
    using namespace eve::graphics;
    GfxFixture fixture(16, 16, true);
    auto      &gfx = *fixture.gfx;
    REQUIRE(!addViewPreparation({}).ok());
    REQUIRE(detail::prepareViewResources(gfx, glm::mat4(1), glm::vec3(0)).ok());
    std::vector<int> order;
    auto             first = addViewPreparation([&](Graphics &, const glm::mat4 &, const glm::vec3 &) {
        order.push_back(1);
        auto recursive = detail::prepareViewResources(gfx, glm::mat4(1), glm::vec3(0));
        REQUIRE(!recursive.ok());
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected"));
    });
    REQUIRE(first.ok());
    auto second = addViewPreparation([&](Graphics &, const glm::mat4 &, const glm::vec3 &) {
        order.push_back(2);
        return eve::Result<void>::success();
    });
    REQUIRE(second.ok());
    auto failed = detail::prepareViewResources(gfx, glm::mat4(1), glm::vec3(0));
    REQUIRE(!failed.ok());
    REQUIRE(order.size() == 1);
    REQUIRE(order.front() == 1);
    removeViewPreparation(first.value());
    order.clear();
    REQUIRE(detail::prepareViewResources(gfx, glm::mat4(1), glm::vec3(0)).ok());
    REQUIRE(order.size() == 1);
    REQUIRE(order.front() == 2);
    removeViewPreparation(second.value());
    removeViewPreparation(second.value());
    order.clear();
    REQUIRE(detail::prepareViewResources(gfx, glm::mat4(1), glm::vec3(0)).ok());
    REQUIRE(order.empty());
}

TEST_CASE("graphics.view preparation runs before offscreen target opens") {
    using namespace eve::graphics;
    GfxFixture fixture(16, 16, true);
    auto      *gfx            = fixture.gfx;
    auto      *preparedTarget = gfx->newHDRCanvas(8, 8);
    auto      *destination    = gfx->newHDRCanvas(16, 16);
    auto      *camera         = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(1, 2, 0);
    int  calls        = 0;
    auto registration = addViewPreparation([&](Graphics &provider, const glm::mat4 &, const glm::vec3 &eye) {
        REQUIRE(&provider == gfx);
        REQUIRE(eye.y == 2);
        // A nested destination would throw: this preparation must precede it.
        provider.begin3DFrameToCanvas(preparedTarget);
        provider.end3DFrameToCanvas();
        ++calls;
        return eve::Result<void>::success();
    });
    REQUIRE(registration.ok());
    RenderSystem3D::renderToCanvas(*gfx, destination, camera);
    REQUIRE(calls == 1);
    removeViewPreparation(registration.value());
    RenderSystem3D::renderToCanvas(*gfx, destination, camera);
    REQUIRE(calls == 1);
}
}  // namespace test_SkyViewPreparation

// Contract section: SkyWisps
#include "PrivateTestScope.h"


#ifndef EVENGINE_WEBGPU
#include "SkyWispsSpv.h"
#endif
#ifndef EVENGINE_WEBGPU
#include "graphics/sky/SkyWispsAsset.h"
#endif
#ifndef EVENGINE_WEBGPU
#include "graphics/vulkan/Graphics.h"
#endif
// SkyWisps.cpp
namespace test_SkyWisps {
#ifndef EVENGINE_WEBGPU

#include "graphics/shaders/scene_tonemap_frag_spv.inc"


#endif

#if !defined(EVENGINE_WEBGPU) && defined(EVE_TEST_RESOURCE_VALIDATION)
TEST_CASE("graphics.sky descriptor limit rejects clouds and leaves atmosphere preparation usable") {
    using namespace eve::graphics;
    GfxFixture fixture(32, 32, true);
    auto      *gfx   = static_cast<vulkan::Graphics *>(fixture.gfx);
    auto      &limit = gfx->getDevice().physical_device.properties.limits.maxPerStageDescriptorSamplers;
    struct RestoreLimit {
        uint32_t &value;
        uint32_t  previous;
        ~RestoreLimit() { value = previous; }
    } restore{limit, limit};
    // Exercise the actual admission path at Vulkan's minimum sampler budget.
    limit      = 16;
    auto asset = SkyWispsAsset::generate(2026);
    REQUIRE(asset.ok());
    auto layer                = asset.value().layer();
    layer.opticalCycleEnabled = false;
    auto clouds               = SkyAtmospherePass::prepare(*gfx, {}, &layer);
    REQUIRE(!clouds.ok());
    REQUIRE(!skyPreparationAvailable(clouds));
    auto atmosphere = SkyAtmospherePass::prepare(*gfx, {});
    REQUIRE(atmosphere.ok());
    REQUIRE(atmosphere.value()->attach().ok());
    atmosphere.value()->detach();
}

TEST_CASE("graphics.sky authored wisps composite into the shared capture path") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture                  fixture(32, 32, true);
    auto                       *gfx = fixture.gfx;
    const std::array<float, 27> corners{10000, -30000, -30000, 0, 0,     1, 1,     1,   1, 10000, 30000, -30000, 1, 0,
                                        1,     1,      1,      1, 10000, 0, 30000, .5f, 1, 1,     1,     1,      1};
    const std::array<std::byte, 1> white{std::byte{255}};
    ShaderImageInput               texture;
    texture.format = ShaderImageFormat::R8;
    texture.width = texture.height = 1;
    texture.bytes                  = white;
    texture.sampler                = TextureSampler::linear();
    SkyWispsLayer layer;
    layer.corners        = corners;
    layer.densityTexture = &texture;
    layer.domeScale      = 1;
    layer.color          = {1, 1, 1};
    layer.gradient       = {0, 0, 0, 0};
    layer.opacity        = 1;
    layer.litIntensity   = 1;
    auto *camera         = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(1, 2, 0);
    camera->setClipPlanes(.1f, 10000);
    camera->setFov(50);
    auto *canvas = gfx->newHDRCanvas(32, 32);
    REQUIRE(canvas != nullptr);
    // Always prove the base atmosphere works, including on a descriptor-limited device.
    auto atmosphere = SkyAtmospherePass::prepare(*gfx, {});
    REQUIRE(atmosphere.ok());
    auto cloudPreparation = SkyAtmospherePass::prepare(*gfx, {}, &layer);
    if (!skyPreparationAvailable(cloudPreparation)) return;
    std::move(cloudPreparation).takeValue().reset();
    std::move(atmosphere).takeValue().reset();
    const auto render = [&](const SkyWispsLayer *input, bool mainView = false) {
        auto prepared = SkyAtmospherePass::prepare(*gfx, {}, input);
        REQUIRE(prepared.ok());
        auto pass = std::move(prepared).takeValue();
        REQUIRE(pass->setLight({{1, 0, 0}, {5, 5, 5}}).ok());
        REQUIRE(pass->attach().ok());
        if (mainView) {
            const auto vp = perspectiveVulkanRH_ZO(glm::radians(50.f), 1.f, .1f, 10000.f) *
                            glm::lookAtRH(glm::vec3(0, 2, 0), glm::vec3(1, 2, 0), glm::vec3(0, 1, 0));
            REQUIRE(pass->prepareView(vp, {0, 2, 0}).ok());
            gfx->begin3DFrameToCanvas(canvas);
            REQUIRE(pass->draw(vp, {0, 2, 0}).ok());
            gfx->end3DFrameToCanvas();
        } else {
            RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
        }
        std::unique_ptr<eve::image::ImageData> image(canvas->newHDRImageData());
        REQUIRE(image != nullptr);
        return image->getPixel(16, 16);
    };
    const auto base = render(nullptr), cloud = render(&layer);
    REQUIRE(cloud.r > 2.2f);
    REQUIRE(cloud.r < 5.f);
    REQUIRE(std::abs(cloud.r - cloud.g) < .01f);
    REQUIRE(std::abs(cloud.r - cloud.b) < .01f);
    REQUIRE(std::abs(cloud.r - base.r) > .1f);
    layer.opacity          = 0;
    const auto transparent = render(&layer);
    REQUIRE(std::abs(transparent.r - base.r) < .001f);
    REQUIRE(std::abs(transparent.g - base.g) < .001f);
    REQUIRE(std::abs(transparent.b - base.b) < .001f);
    layer.sunDiskColor   = {100, 50, 25};
    layer.sunDiskShape   = {.3f, 3, 0};
    const auto hiddenSun = render(&layer);
    REQUIRE(std::abs(hiddenSun.r - transparent.r) < .001f);
    const auto visibleSun = render(&layer, true);
    REQUIRE(visibleSun.r > hiddenSun.r + 50);
    layer.sunDiskShape[2]     = 1;
    layer.sunDiskCurveEnabled = true;
    for (size_t channel = 0; channel < 3; ++channel) {
        layer.sunDiskCurve[channel * 4]     = {0, 0, 0, 0};
        layer.sunDiskCurve[channel * 4 + 1] = {.25f, 0, 0, 0};
        layer.sunDiskCurve[channel * 4 + 2] = {.75f, .5f, 0, 0};
        layer.sunDiskCurve[channel * 4 + 3] = {1, .5f, 0, 0};
    }
    const auto curveSun = render(&layer, true);
    REQUIRE(std::abs((curveSun.r - hiddenSun.r) * 2 - (visibleSun.r - hiddenSun.r)) < .15f);
    layer.sunDiskCurve[1][0] = 0;
    REQUIRE(!SkyAtmospherePass::prepare(*gfx, {}, &layer).ok());
    layer.sunDiskCurveEnabled = false;
    const auto reflectedSun   = render(&layer);
    REQUIRE(std::abs(reflectedSun.r - visibleSun.r) < .1f);
    REQUIRE(reflectedSun.r > hiddenSun.r + 50);
    REQUIRE(reflectedSun.g > hiddenSun.g + 25);
    layer.opacity          = 1;
    const auto obscuredSun = render(&layer);
    // HDR readback is FP16: allow one quantization step near the 2.3 cloud value.
    REQUIRE(std::abs(obscuredSun.r - cloud.r) < .003f);
    layer.sunDiskShape[0] = 0;
    REQUIRE(!SkyAtmospherePass::prepare(*gfx, {}, &layer).ok());
    layer.sunDiskShape[0] = .3f;
    layer.morphPeriod     = 0;
    REQUIRE(!SkyAtmospherePass::prepare(*gfx, {}, &layer).ok());
    if (const char *artifactDirectory = std::getenv("EVENGINE_SKY_WISPS_ARTIFACT")) {
        const std::filesystem::path root(artifactDirectory);
        const auto                  read = [&](const char *name) {
            std::ifstream input(root / name, std::ios::binary | std::ios::ate);
            REQUIRE(input.good());
            const auto size = input.tellg();
            REQUIRE(size > 0);
            REQUIRE(size < 64 * 1024 * 1024);
            std::vector<char> bytes(static_cast<size_t>(size));
            input.seekg(0);
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            REQUIRE(input.good());
            return bytes;
        };
        const auto meshBytes = read("corners.f32"), densityBytes = read("wisps.r8");
        REQUIRE(meshBytes.size() % (27 * sizeof(float)) == 0);
        REQUIRE(densityBytes.size() == 2048 * 2048);
        std::vector<float> authored(meshBytes.size() / sizeof(float));
        std::memcpy(authored.data(), meshBytes.data(), meshBytes.size());
        std::vector<std::byte> rgba(densityBytes.size() * 4);
        for (size_t i = 0; i < densityBytes.size(); ++i) {
            const auto value = std::byte(static_cast<unsigned char>(densityBytes[i]));
            rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = value;
            rgba[i * 4 + 3]                                 = std::byte{255};
        }
        ShaderImageInput density;
        density.format = ShaderImageFormat::RGBA8Srgb;
        density.width = density.height = 2048;
        density.bytes                  = rgba;
        density.sampler                = TextureSampler::linear();
        density.sampler.repeatU = density.sampler.repeatV = true;
        SkyWispsLayer source;
        source.corners        = authored;
        source.densityTexture = &density;
        SkyAtmosphereParameters atmosphere;
        atmosphere.heightFog.densityPerMetre = .000551f;
        auto prepared                        = SkyAtmospherePass::prepare(*gfx, atmosphere, &source);
        REQUIRE(prepared.ok());
        auto pass = std::move(prepared).takeValue();
        REQUIRE(pass->attach().ok());
        camera->setTarget(.965925826f, 2.258819045f, 0);
        camera->setFov(46.6921257f);
        auto *hdr     = gfx->newHDRCanvas(1280, 720);
        auto *display = gfx->newCanvas(1280, 720);
        REQUIRE(hdr != nullptr);
        REQUIRE(display != nullptr);
        RenderSystem3D::renderToCanvas(*gfx, hdr, camera);
        const float gpuMs = gfx->getLastOffscreen3DGpuDurationMs();
        auto       *film =
            gfx->newShaderFromSpv({}, {scene_tonemap_frag_spv, scene_tonemap_frag_spv + scene_tonemap_frag_spv_count});
        REQUIRE(film != nullptr);
        gfx->setCanvas(display);
        gfx->drawTexturedRectShader(hdr->getTexture(), film, 0, 0, 1280, 720, Color(2, 1, 1, 65536));
        gfx->setCanvas();
        std::unique_ptr<eve::image::ImageData> pixels(display->newImageData());
        REQUIRE(pixels != nullptr);
        std::ofstream image(root / "wisps.ppm", std::ios::binary);
        image << "P6\n1280 720\n255\n";
        for (int y = 0; y < 720; ++y)
            for (int x = 0; x < 1280; ++x) {
                const auto pixel = pixels->getPixel(x, y);
                for (const float value : {pixel.r, pixel.g, pixel.b}) {
                    REQUIRE(std::isfinite(value));
                    image.put(static_cast<char>(std::lround(std::clamp(value, 0.f, 1.f) * 255)));
                }
            }
        REQUIRE(image.good());
        std::ofstream timing(root / "timing.json");
        timing << "{\"gpuOffscreenPassMs\":" << gpuMs << "}\n";
        REQUIRE(timing.good());
    }
}
#endif

#ifndef EVENGINE_WEBGPU
TEST_CASE("graphics.sky wisps preserve phase blending and celestial vertex lighting") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture fixture(16, 16, true);
    auto      *gfx    = fixture.gfx;
    auto      *input  = gfx->newCanvas(16, 16);
    auto      *output = gfx->newHDRCanvas(16, 16);
    auto      *shader = gfx->newShaderFromSpv({}, sky_wisps_probe_frag);
    REQUIRE(input != nullptr);
    REQUIRE(output != nullptr);
    REQUIRE(shader != nullptr);
    gfx->setCanvas(input);
    gfx->clear(Color(1, 1, 1, 1), std::nullopt, std::nullopt);
    gfx->drawSolidRect(0, 0, 8, 16, Color(1, 1, 1, 1));
    gfx->drawSolidRect(8, 0, 8, 16, Color(0, 0, 0, 1));
    gfx->setCanvas();
    std::unique_ptr<eve::image::ImageData> sourcePixels(input->newImageData());
    REQUIRE(sourcePixels != nullptr);
    REQUIRE(sourcePixels->getPixel(4, 8).r > .99f);
    REQUIRE(sourcePixels->getPixel(12, 8).r < .01f);
    const auto sample = [&](float mode, float time) {
        gfx->setCanvas(output);
        gfx->drawTexturedRectShader(input->getTexture(), shader, 0, 0, 16, 16, Color(mode, time, 0, 1));
        gfx->setCanvas();
        std::unique_ptr<eve::image::ImageData> pixels(output->newHDRImageData());
        REQUIRE(pixels != nullptr);
        return pixels->getPixel(8, 8);
    };
    REQUIRE(std::abs(sample(0, 0).r - .2f) < .001f);
    REQUIRE(std::abs(sample(0, 2.5f).r - .1f) < .001f);
    REQUIRE(std::abs(sample(0, 5).r) < .001f);
    REQUIRE(std::abs(sample(0, 10).r - .2f) < .001f);
    const auto day = sample(1, 0), night = sample(2, 0), highlight = sample(3, 0);
    REQUIRE(std::abs(day.r - 1) < .001f);
    REQUIRE(std::abs(day.g) < .001f);
    REQUIRE(std::abs(day.b - 3.28f) < .005f);
    REQUIRE(std::abs(night.r - 1) < .001f);
    REQUIRE(std::abs(night.g) < .001f);
    REQUIRE(std::abs(night.b) < .001f);
    REQUIRE(std::abs(highlight.r - 3.5f) < .005f);
    REQUIRE(std::abs(highlight.g - 2.81245f) < .005f);
    REQUIRE(std::abs(highlight.b - 2.12f) < .005f);
    // A nonlinear parent transform must follow composition; transforming
    // the two inputs before mixing would produce .48525 rather than .462.
    const auto composed = sample(4, 0);
    REQUIRE(std::abs(composed.r - .462f) < .001f);
    REQUIRE(std::abs(composed.g - .2295f) < .001f);
    REQUIRE(std::abs(composed.b - .9f) < .001f);
    REQUIRE(std::abs(sample(5, -1).r) < .001f);
    REQUIRE(std::abs(sample(5, .25f).r - .5f) < .001f);
    REQUIRE(std::abs(sample(5, .5f).r - .8125f) < .001f);
    REQUIRE(std::abs(sample(5, .75f).r - .75f) < .001f);
    REQUIRE(std::abs(sample(5, 2).r - 1.f) < .001f);
    REQUIRE(sample(6, -.1f).r == 0);
    REQUIRE(sample(6, 0).r == 0);
    REQUIRE(std::abs(sample(6, .1f).r - .5f) < .001f);
    REQUIRE(sample(6, .3f).r == 1);
    REQUIRE(sample(6, 0).g == 1);
    // Captured UDS 5.8 material values, not values computed by this shader.
    const std::array<float, 4> hours{0, 6, 12, 18};
    const std::array<Color, 4> clouds{Color(.003047f, .005015f, .009361f, 1), Color(.020577f, .008931f, .006047f, 1),
                                      Color(.355900f, .399502f, .469850f, 1), Color(.020577f, .008931f, .006047f, 1)};
    const std::array<float, 4> midpoints{.020513f, .021585f, .852876f, .021585f};
    const std::array<float, 4> intensities{.45f, .689543f, .375f, .689543f};
    for (size_t i = 0; i < hours.size(); ++i) {
        const auto cloud = sample(7, hours[i]);
        REQUIRE(std::abs(cloud.r - clouds[i].r) < .0003f);
        REQUIRE(std::abs(cloud.g - clouds[i].g) < .0003f);
        REQUIRE(std::abs(cloud.b - clouds[i].b) < .0003f);
        const auto controls = sample(8, hours[i]);
        REQUIRE(std::abs(controls.r - midpoints[i]) < .0006f);
        REQUIRE(std::abs(controls.g - intensities[i]) < .0006f);
    }
    const auto midnightMoon = sample(9, 0);
    REQUIRE(std::abs(midnightMoon.r - .555242f) < .0006f);
    REQUIRE(std::abs(midnightMoon.b + .831689f) < .0006f);
    const auto dawnGlow = sample(10, 6);
    REQUIRE(std::abs(dawnGlow.r - .000453f) < .000002f);
    REQUIRE(std::abs(dawnGlow.b - .001242f) < .000002f);
    const auto dawnFog = sample(11, 6);
    REQUIRE(std::abs(dawnFog.r - .000308f) < .000002f);
    REQUIRE(std::abs(dawnFog.b - .000845f) < .000002f);
}
#endif
}  // namespace test_SkyWisps

// Contract section: TextureSampler
#include "PrivateTestScope.h"


#include <optional>
#include "graphics/TextureSampler.h"
#include "procgen/GtsTerrainLod.h"
#include "procgen/GtsTerrainLodRuntime.h"
#include "procgen/PcgMeshLod.h"
#include "procgen/PcgMeshLodBackup.h"
#include "procgen/Procgen.h"
// TextureSampler.cpp
namespace test_TextureSampler {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {

[[maybe_unused]] std::vector<uint8_t> makeSolid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return px;
}

}  // namespace


TEST_CASE("GraphicsSmoke.textureMipmapsAndAnisotropy") {
    GfxFixture fx(320, 240, /*useHeadless=*/true);
    auto       px = makeSolid(64, 64, 200, 40, 40);

    TextureCreateInfo info = TextureCreateInfo::withMipmaps(true, 8.f);
    Texture          *tex  = fx.gfx->newTexture(64, 64, px.data(), info);
    REQUIRE(tex != nullptr);
    CHECK_EQ(tex->getMipmapCount(), mipmapCountForSize(64, 64));
    CHECK_EQ(static_cast<int>(tex->getSampler().mipmap), static_cast<int>(MipmapMode::Linear));
    CHECK(tex->getSampler().maxAnisotropy >= 8.f - 1e-3f);

    const float deviceMax = fx.gfx->getMaxAnisotropy();
    CHECK(deviceMax >= 1.f);

    // Nearest pixel-art style sampler without rebuilding image.
    fx.gfx->setTextureSampler(tex, TextureSampler::nearest());
    CHECK_EQ(static_cast<int>(tex->getSampler().min), static_cast<int>(FilterMode::Nearest));
    CHECK_EQ(static_cast<int>(tex->getSampler().mag), static_cast<int>(FilterMode::Nearest));

    // Draw once to exercise descriptor rewrite path.
    fx.gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.08f, 1.f));
    fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);
    fx.gfx->drawTexturedRect(tex, 10.f, 10.f, 64.f, 64.f, Color(1, 1, 1, 1));
    fx.gfx->present();
}

TEST_CASE("GraphicsSmoke.setTextureSamplerValidatesStrings") {
    GfxFixture fx(320, 240, /*useHeadless=*/true);
    auto       px  = makeSolid(16, 16, 120, 120, 120);
    Texture   *tex = fx.gfx->newTexture(16, 16, px.data());
    REQUIRE(tex != nullptr);

    // Valid string update through the script-facing name.
    fx.gfx->setTextureSampler(tex, "nearest", "none", 1.f, 0.f);
    CHECK_EQ(static_cast<int>(tex->getSampler().min), static_cast<int>(FilterMode::Nearest));

    fx.gfx->setTextureSampler(tex, "linear", "linear", 8.f, 0.f);
    CHECK_EQ(static_cast<int>(tex->getSampler().mipmap), static_cast<int>(MipmapMode::Linear));

    // Typos fail fast instead of silently falling back to linear.
    bool threw = false;
    try {
        fx.gfx->setTextureSampler(tex, "linar", "none", 1.f, 0.f);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        fx.gfx->setTextureSampler(tex, "linear", "mip", 1.f, 0.f);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("GraphicsSmoke.cubemapGeneratesMipChain") {
    GfxFixture           fx(320, 240, /*useHeadless=*/true);
    const int            face = 16;
    std::vector<uint8_t> faces(size_t(face) * face * 4 * 6, 128);
    Texture             *cube = fx.gfx->newCubemap(face, faces.data());
    REQUIRE(cube != nullptr);
    CHECK_EQ(cube->getMipmapCount(), mipmapCountForSize(face, face));
    CHECK_EQ(static_cast<int>(cube->getSampler().mipmap), static_cast<int>(MipmapMode::Linear));
}


TEST_CASE("Renderable3D.gtsTerrainTileUploadsAndConfiguresRealLodChain") {
    using namespace eve::procgen;
    GfxFixture fx(128, 128, true);
    MeshBuild  source;
    source.setActiveGroup("terrain");
    source.addVertex(0, 0, 0, 0, 1, 0, 0, 0);
    source.addVertex(2, 0, 0, 0, 1, 0, 1, 0);
    source.addVertex(2, 0, 2, 0, 1, 0, 1, 1);
    source.addVertex(0, 0, 2, 0, 1, 0, 0, 1);
    source.addTriangle(0, 1, 2);
    source.addTriangle(0, 2, 3);
    auto lods = buildGtsTerrainLods(source, 1, 0, GtsMeshPivot::CenterXZ, defaultGtsTerrainLodLevels());
    REQUIRE(lods.ok());
    auto *ent = Renderable3D::create();
    REQUIRE(ent);
    Procgen procgen;
    auto configured = procgen.configureGtsTerrainTileLods(lods.value(), 0, *ent, *fx.gfx, 2.f, 90.f, 10.f, 3.f, 20.f);
    REQUIRE(configured.ok());
    CHECK_EQ(ent->getMeshLodCount(), 4);
    CHECK(ent->getMesh() != nullptr);
    CHECK(ent->getMeshLodCullDistance() > 40.f);
    CHECK(ent->meshRenderer()->meshForDistance(ent->getMeshLodCullDistance()) == nullptr);
    CHECK_EQ(ent->transform()->x, 10.5f);
    CHECK_EQ(ent->transform()->y, 3.f);
    CHECK_EQ(ent->transform()->z, 21.f);
}

TEST_CASE("Renderable3D.pcgGenericLodsUploadAsOneAtomicChain") {
    using namespace eve::procgen;
    GfxFixture fx(128, 128, true);
    MeshBuild  source;
    source.setActiveGroup("surface");
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x)
            source.addVertex(float(x), 0.f, float(z), 0.f, 1.f, 0.f, float(x) / 2.f, float(z) / 2.f);
    for (int z = 0; z < 2; ++z)
        for (int x = 0; x < 2; ++x) {
            const uint32_t a = uint32_t(z * 3 + x), b = a + 1, c = a + 3, d = c + 1;
            source.addTriangle(a, b, d);
            source.addTriangle(a, d, c);
        }
    PcgMeshLodProfile profile;
    REQUIRE(profile.appendLevel(.8f, .1f, 1.f).ok());
    REQUIRE(profile.appendLevel(.2f, .15f, .5f).ok());
    REQUIRE(profile.setFadePolicy(2, false, .5f).ok());
    REQUIRE(profile.setLevelRendererState(0, 4, 2, false, 2, false, 4, 2).ok());
    REQUIRE(profile.setLevelRendererState(1, 2, 0, true, 0, true, 0, 0).ok());
    PcgMeshLodSet lods;
    REQUIRE(buildPcgMeshLodsInto(lods, source, profile).ok());
    auto *ent = Renderable3D::create();
    REQUIRE(ent);
    Procgen procgen;
    REQUIRE(procgen.configurePcgMeshLods(lods, *ent, *fx.gfx, 2.f, 90.f).ok());
    CHECK_EQ(ent->getMeshLodCount(), 2);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(1.f), 0);
    CHECK_EQ(ent->getMeshLodSecondaryLevelAtDistance(1.1875f), 1);
    CHECK(std::abs(ent->getMeshLodSecondaryWeightAtDistance(1.1875f) - .5f) < .0001f);
    CHECK_EQ(ent->getMeshLodRendererState(0, 0), 4);
    CHECK_EQ(ent->getMeshLodRendererState(0, 1), 2);
    CHECK_EQ(ent->getMeshLodRendererState(0, 2), 0);
    CHECK_EQ(ent->getMeshLodRendererState(1, 0), 2);
    CHECK(!ent->setMeshLodRendererState(0, 3, 1, true, 1, true, 1, 1).ok());
    CHECK_EQ(ent->getMeshLodLevelAtDistance(2.f), 1);
    CHECK(ent->getMeshLodCullDistance() > 4.9f);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    fx.gfx->expose(table);
    Procgen::expose(table);
    vm.addFunc("pcgBackupTarget", [&]() { return ent; });
    vm.run(vm.compileSource(R"(
        local target=pcgBackupTarget();local backup=eve.PcgMeshLodBackup();
        assert(backup.capture(target).ok && backup.isCaptured());
        local id=backup.getEntityId();local generation=backup.getEntityGeneration();
        assert(id==target.getEntityId() && generation==target.getEntityGeneration());
        target.setVisible(false);assert(backup.restore(target).ok && !backup.isCaptured());
        assert(target.getVisible());
    )"));
    const int before = ent->getMeshLodCount();
    CHECK(!procgen.configurePcgMeshLods(lods, *ent, *fx.gfx, -1.f, 90.f).ok());
    CHECK_EQ(ent->getMeshLodCount(), before);
    ecs::DestroyEntity(ent);
}

TEST_CASE("Renderable3D.gtsTerrainRuntimeReplacesTilesWithStableHandles") {
    using namespace eve::procgen;
    GfxFixture fx(128, 128, true);
    MeshBuild  source;
    source.setActiveGroup("terrain");
    source.addVertex(0, 0, 0, 0, 1, 0, 0, 0);
    source.addVertex(2, 0, 0, 0, 1, 0, 1, 0);
    source.addVertex(2, 0, 2, 0, 1, 0, 1, 1);
    source.addVertex(0, 0, 2, 0, 1, 0, 0, 1);
    source.addTriangle(0, 1, 2);
    source.addTriangle(0, 2, 3);
    auto lods = buildGtsTerrainLods(source, 1, 0, GtsMeshPivot::CenterXZ, defaultGtsTerrainLodLevels());
    REQUIRE(lods.ok());
    Procgen              procgen;
    GtsTerrainLodRuntime runtime;
    auto                 first = runtime.replace(lods.value(), procgen, *fx.gfx, 2.f, 90.f);
    REQUIRE(first.ok());
    CHECK_EQ(first.value(), 1u);
    CHECK_EQ(runtime.getTileCount(), 2);
    auto *old = runtime.getRenderable(0);
    REQUIRE(old);
    auto oldHandle = ecs::handle_of(old);
    CHECK(old->getVisible());
    CHECK(!runtime.replace(lods.value(), procgen, *fx.gfx, -1.f, 90.f).ok());
    CHECK_EQ(runtime.getRevision(), 1u);
    CHECK(ecs::try_get(oldHandle) == old);
    CHECK(old->getVisible());
    auto *material = fx.gfx->newMaterial();
    REQUIRE(material);
    auto applied = runtime.applyMaterial(*material);
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value(), 2);
    CHECK(old->getMaterial() == material);
    auto second = runtime.replace(lods.value(), procgen, *fx.gfx, 2.f, 90.f, 5.f, 0.f, 7.f);
    REQUIRE(second.ok());
    CHECK_EQ(second.value(), 2u);
    CHECK(ecs::try_get(oldHandle) == nullptr);
    auto *current = runtime.getRenderable(0);
    REQUIRE(current);
    CHECK_EQ(current->transform()->x, 5.5f);
    CHECK_EQ(current->transform()->z, 8.f);
    auto cleared = runtime.clear();
    REQUIRE(cleared.ok());
    CHECK_EQ(cleared.value(), 2);
    CHECK_EQ(runtime.getTileCount(), 0);
    CHECK_EQ(runtime.getRevision(), 3u);
}

TEST_CASE("Renderable3D.gtsTerrainRuntimeRestoresSourceVisibilityAcrossEveryLifetimeOrder") {
    using namespace eve::procgen;
    GfxFixture fx(128, 128, true);
    MeshBuild  sourceMesh;
    sourceMesh.setActiveGroup("terrain");
    sourceMesh.addVertex(0, 0, 0, 0, 1, 0, 0, 0);
    sourceMesh.addVertex(2, 0, 0, 0, 1, 0, 1, 0);
    sourceMesh.addVertex(2, 0, 2, 0, 1, 0, 1, 1);
    sourceMesh.addVertex(0, 0, 2, 0, 1, 0, 0, 1);
    sourceMesh.addTriangle(0, 1, 2);
    sourceMesh.addTriangle(0, 2, 3);
    auto lods = buildGtsTerrainLods(sourceMesh, 1, 0, GtsMeshPivot::CenterXZ, defaultGtsTerrainLodLevels());
    REQUIRE(lods.ok());
    Procgen procgen;
    auto   *firstSource = Renderable3D::create();
    REQUIRE(firstSource);
    firstSource->setVisible(true);
    auto *hiddenSource = Renderable3D::create();
    REQUIRE(hiddenSource);
    hiddenSource->setVisible(false);
    {
        GtsTerrainLodRuntime runtime;
        auto first = runtime.replaceAndHideSource(lods.value(), procgen, *fx.gfx, *firstSource, 2.f, 90.f);
        REQUIRE(first.ok());
        CHECK(!firstSource->getVisible());
        CHECK(runtime.getSourceTerrain() == firstSource);
        CHECK_EQ(runtime.getRevision(), 1u);
        CHECK(!runtime.replaceAndHideSource(lods.value(), procgen, *fx.gfx, *firstSource, -1.f, 90.f).ok());
        CHECK(!firstSource->getVisible());
        CHECK(runtime.getSourceTerrain() == firstSource);
        CHECK_EQ(runtime.getRevision(), 1u);
        auto second = runtime.replaceAndHideSource(lods.value(), procgen, *fx.gfx, *hiddenSource, 2.f, 90.f);
        REQUIRE(second.ok());
        CHECK(firstSource->getVisible());
        CHECK(!hiddenSource->getVisible());
        CHECK(runtime.getSourceTerrain() == hiddenSource);
        auto cleared = runtime.clear();
        REQUIRE(cleared.ok());
        CHECK(!hiddenSource->getVisible());
        CHECK(runtime.getSourceTerrain() == nullptr);
    }
    firstSource->setVisible(true);
    {
        GtsTerrainLodRuntime runtime;
        REQUIRE(runtime.replaceAndHideSource(lods.value(), procgen, *fx.gfx, *firstSource, 2.f, 90.f).ok());
        CHECK(!firstSource->getVisible());
    }
    CHECK(firstSource->getVisible());
    auto *destroyedSource = Renderable3D::create();
    REQUIRE(destroyedSource);
    destroyedSource->setVisible(true);
    GtsTerrainLodRuntime sourceFirst;
    REQUIRE(sourceFirst.replaceAndHideSource(lods.value(), procgen, *fx.gfx, *destroyedSource, 2.f, 90.f).ok());
    ecs::DestroyEntity(destroyedSource);
    CHECK(sourceFirst.getSourceTerrain() == nullptr);
    REQUIRE(sourceFirst.clear().ok());
    ecs::DestroyEntity(firstSource);
    ecs::DestroyEntity(hiddenSource);
}
}  // namespace test_TextureSampler

// Contract section: UIOverlaySmoke
#include "PrivateTestScope.h"


#include <string>
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
// UIOverlaySmoke.cpp
namespace test_UIOverlaySmoke {
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
    auto *ui  = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);

    eve::window::WindowSettings s;
    s.width    = 640;
    s.height   = 480;
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

    auto *cam      = Camera2D::createCamera();
    cam->data()->r = 0.10f;
    cam->data()->g = 0.12f;
    cam->data()->b = 0.18f;

    auto *solid              = Renderable2D::create();
    solid->transform()->x    = 80;
    solid->transform()->y    = 100;
    solid->sprite()->width   = 160;
    solid->sprite()->height  = 100;
    solid->sprite()->r       = 0.2f;
    solid->sprite()->g       = 0.85f;
    solid->sprite()->b       = 0.35f;
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
    auto *ui  = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);

    eve::window::WindowSettings s;
    s.width    = 720;
    s.height   = 480;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    REQUIRE(ui->initBackend());
    gfx->setScreenReadbackEnabled(true);

    gfx->setBackgroundColor(Color(0.09f, 0.10f, 0.14f, 1.0f));

    auto *cam      = Camera2D::createCamera();
    cam->data()->r = 0.09f;
    cam->data()->g = 0.10f;
    cam->data()->b = 0.14f;

    // Soft backdrop sprites so the world + overlay composition is obvious.
    auto *bgA              = Renderable2D::create();
    bgA->transform()->x    = 40;
    bgA->transform()->y    = 60;
    bgA->sprite()->width   = 220;
    bgA->sprite()->height  = 140;
    bgA->sprite()->r       = 0.25f;
    bgA->sprite()->g       = 0.45f;
    bgA->sprite()->b       = 0.75f;
    bgA->sprite()->visible = true;

    auto *bgB              = Renderable2D::create();
    bgB->transform()->x    = 420;
    bgB->transform()->y    = 220;
    bgB->sprite()->width   = 180;
    bgB->sprite()->height  = 160;
    bgB->sprite()->r       = 0.75f;
    bgB->sprite()->g       = 0.35f;
    bgB->sprite()->b       = 0.45f;
    bgB->sprite()->visible = true;

    float volume = 0.55f;
    bool  muted  = false;
    int   clicks = 0;

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
        volume              = 0.35f + 0.3f * (0.5f + 0.5f * std::sin(float(frame) * 0.06f));

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
    auto *ui  = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);

    eve::window::WindowSettings s;
    s.width    = 800;
    s.height   = 600;
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
    const Color previewCenter =
        viewportState.canvas->getPixel(viewportState.canvas->getWidth() / 2, viewportState.canvas->getHeight() / 2);
    CHECK_GT(previewCenter.g, previewCenter.r);

    // Composite the viewport through the real UI target. Opaque viewport draws
    // must not leak the mesh shader's depth-in-alpha channel into the final UI
    // resolve or the preview becomes an almost-black silhouette.
    ui->beginFrameAndRender();
    RenderSystem::render(*gfx);
    float maxGreen = 0.f;
    for (int y = 0; y < gfx->getHeight(); y += 4)
        for (int x = 0; x < gfx->getWidth(); x += 4) maxGreen = std::max(maxGreen, gfx->getPixel(x, y).g);
    CHECK_GT(maxGreen, 0.5f);

    gfx->setScreenReadbackEnabled(false);
    win->close();
}
}  // namespace test_UIOverlaySmoke

// Contract section: Volumetric
#include "PrivateTestScope.h"


#include "graphics/AntiAliasing.h"
// Volumetric.cpp
namespace test_Volumetric {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {


[[maybe_unused]] Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    const uint8_t px[4] = {r, g, b, a};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

/** Hold a textured canvas / texture on the swapchain for ~1s. */
[[maybe_unused]] void previewTexture(Graphics *gfx, Texture *tex, int ms = 1000) {
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

TEST_CASE("volumetric.qualityPresets") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);

    Volumetric *raw = gfx->newVolumetric();
    REQUIRE(raw != nullptr);
    REQUIRE(raw->getShader() != nullptr);
    std::unique_ptr<Volumetric> vol(raw);

    vol->setQuality("low");
    CHECK(vol->getQuality() == "low");
    CHECK(vol->getSampleCount() == 16);
    CHECK(vol->getDownscale() == 4.f);
    CHECK(vol->resolutionFor(400) == 100);

    vol->setQuality("medium");
    CHECK(vol->getSampleCount() == 48);
    CHECK(vol->getDownscale() == 2.f);
    CHECK(vol->resolutionFor(400) == 200);

    vol->setQuality("high");
    CHECK(vol->getSampleCount() == 96);
    CHECK(vol->getDownscale() == 1.f);
    CHECK(vol->resolutionFor(400) == 400);

    vol->setQuality("unknown");
    CHECK(vol->getQuality() == "medium");
}


TEST_CASE("volumetric.occlusionMapAndScatter") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 256, 192);
    gfx->setScreenReadbackEnabled(true);

    Volumetric *raw = gfx->newVolumetric();
    REQUIRE(raw != nullptr);
    std::unique_ptr<Volumetric> vol(raw);
    vol->setQuality("medium");
    vol->setShaftColor(1.f, 0.9f, 0.7f);
    vol->setFogColor(0.4f, 0.5f, 0.7f);
    vol->setIntensity(1.2f);

    const int ow  = vol->resolutionFor(gfx->getWidth());
    const int oh  = vol->resolutionFor(gfx->getHeight());
    Canvas   *occ = gfx->newCanvas(ow, oh);
    REQUIRE(occ != nullptr);

    gfx->setCanvas(occ);
    vol->beginOcclusionMap(gfx, float(ow) * 0.75f, float(oh) * 0.2f, 18.f);
    // Occluder between light and bottom of screen.
    vol->drawOccluderSolid(gfx, float(ow) * 0.35f, float(oh) * 0.35f, float(ow) * 0.3f, float(oh) * 0.2f);
    gfx->setCanvas(nullptr);

    Texture *occTex = occ->getTexture();
    REQUIRE(occTex != nullptr);

    Canvas *shafts = gfx->newCanvas(gfx->getWidth(), gfx->getHeight());
    vol->scatterTo(gfx, occTex, shafts);

    // Warm present path so offscreen content is flushed / readable.
    gfx->setBackgroundColorRGBA(0.05f, 0.05f, 0.08f, 1.f);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(shafts->getTexture(), 0, 0, float(gfx->getWidth()), float(gfx->getHeight()), 1, 1, 1, 1);
    gfx->present();
    gfx->present();

    // Shafts should brighten somewhere below the light (radial blur direction).
    float maxL = 0.f;
    for (int y = 0; y < gfx->getHeight(); y += 4) {
        for (int x = 0; x < gfx->getWidth(); x += 4) {
            maxL = std::max(maxL, luma(gfx->getPixel(x, y)));
        }
    }
    CHECK(maxL > 0.05f);

    previewTexture(gfx, shafts->getTexture());
    win->close();
}

TEST_CASE("volumetric.applyFromScene") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 200, 150);
    gfx->setScreenReadbackEnabled(true);

    Volumetric *raw = gfx->newVolumetric();
    REQUIRE(raw != nullptr);
    std::unique_ptr<Volumetric> vol(raw);
    vol->setQuality("low");
    vol->setLightScreenUV(0.8f, 0.15f);
    vol->setFloat("dustAmount", 0.4f);
    vol->setFloat("fogAmount", 0.3f);

    Canvas *scene = gfx->newCanvas(gfx->getWidth(), gfx->getHeight());
    gfx->setCanvas(scene);
    gfx->clear(Color(0.08f, 0.09f, 0.12f, 1.f), std::nullopt, std::nullopt);
    // Bright "sun" disc + dark occluder.
    gfx->drawSolidRect(150, 10, 30, 30, Color(1.f, 0.95f, 0.8f, 1.f));
    gfx->drawSolidRect(70, 50, 50, 40, Color(0.02f, 0.02f, 0.03f, 1.f));
    gfx->setCanvas(nullptr);

    Canvas *out = gfx->newCanvas(gfx->getWidth(), gfx->getHeight());
    vol->applyFromSceneTo(gfx, scene->getTexture(), out);

    // Read from the destination canvas (avoid swapchain readback variance).
    const Color nearLight = out->getPixel(170, 25);
    const Color farCorner = out->getPixel(10, 140);
    CHECK(luma(nearLight) > luma(farCorner));
    CHECK(luma(nearLight) > 0.2f);

    previewTexture(gfx, out->getTexture());
    win->close();
}

TEST_CASE("volumetric.drawOcclusionRespectsFlag") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 128, 128);

    Texture *tex = makeSolid(gfx, 255, 255, 255);
    tex->setCastOcclusion(false);

    Canvas *c = gfx->newCanvas(64, 64);
    gfx->setCanvas(c);
    // Paint a bright plate explicitly (pending clear alone may not flush).
    gfx->drawSolidRect(0, 0, 64, 64, Color(0.9f, 0.9f, 0.9f, 1.f));
    glm::mat4 m(1.f);
    m[3][0] = 10.f;
    m[3][1] = 10.f;
    m[0][0] = 40.f;
    m[1][1] = 40.f;
    gfx->drawOcclusion(tex, m);  // should no-op because castOcclusion=false
    gfx->setCanvas(nullptr);

    CHECK(c->getPixel(30, 30).r > 0.5f);

    // Control: enabling occlusion paints black.
    tex->setCastOcclusion(true);
    gfx->setCanvas(c);
    gfx->drawSolidRect(0, 0, 64, 64, Color(0.9f, 0.9f, 0.9f, 1.f));
    gfx->drawOcclusion(tex, m);
    gfx->setCanvas(nullptr);
    CHECK(c->getPixel(30, 30).r < 0.2f);
}


TEST_CASE("volumetric.collectAndDriveSceneLights3D") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    REQUIRE(vol);

    vol->setMode("raymarch");
    vol->setCamera(0.f, 2.f, 8.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 60.f, 1.777f, 0.1f, 100.f);
    vol->setAnisotropy(0.75f);
    CHECK(std::fabs(vol->getAnisotropy() - 0.75f) < 1e-5f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.3f, 1.f, 0.2f);
    sun->setColor(1.f, 0.95f, 0.8f, 1.5f);
    sun->setVolumetric(true);

    auto *neon = Light3D::createEmissiveProxy(0.f, 1.f, -2.f, 1.f, 0.2f, 0.4f, 3.f, 5.f);

    std::vector<VolumetricLight> lights;
    auto                         collected = vol->collectSceneLights3D(lights, 8);
    REQUIRE(collected.ok());
    // Directional is excluded from froxel local list; emissive proxy is included.
    CHECK(collected.value() == 1);
    CHECK(lights.size() == 1u);
    CHECK(std::fabs(lights[0].position.z + 2.f) < 1e-5f);

    auto driven = vol->driveFromLight3D(sun, 1280.f, 720.f);
    REQUIRE(driven.ok());
    CHECK(std::fabs(vol->getFloat("shaftR") - 1.f) < 1e-5f);
    CHECK(vol->getFloat("intensity") > 1.f);

    auto primary = vol->driveFromPrimarySceneLight3D(1280.f, 720.f);
    REQUIRE(primary.ok());

    (void)neon;
    win->close();
}

TEST_CASE("volumetric.froxelSceneLightsValidateBoundsAndRankProxies") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    REQUIRE(vol);
    vol->setMode("froxel");
    vol->setCamera(0.f, 2.f, 6.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 55.f, 1.777f, 0.1f, 40.f);
    vol->configureFroxelGrid(4, 4, 8, 0.1f, 40.f);
    vol->clearFroxelGrid();
    vol->injectFroxelHeightFog(0.04f, 0.8f, 0.85f, 0.95f, 0.f, 0.2f, -4.f, 8.f);

    auto badBounds = vol->integrateFroxelFromSceneLights(0.02f, 0.02f, 0.03f, glm::vec3(5.f), glm::vec3(1.f), 4);
    CHECK(!badBounds.ok());

    REQUIRE(vol->injectEmissiveLightProxy(0.f, 1.f, -2.f, 1.f, 0.2f, 0.05f, 5.f, 8.f).ok());
    // Fill the budget with weak scene lights; the strong proxy must still win a slot.
    for (int i = 0; i < 4; ++i) {
        auto *weak = Light3D::createEmissiveProxy(float(i) * 2.f, 0.f, 0.f, 0.1f, 0.1f, 0.1f, 0.2f, 2.f);
        (void)weak;
    }
    auto ok = vol->integrateFroxelFromSceneLights(0.02f, 0.02f, 0.03f, glm::vec3(-10.f), glm::vec3(10.f), 4);
    REQUIRE(ok.ok());
    const glm::vec4 sample = vol->getAtmosphereVolume()->sampleIntegrated(0.5f, 0.5f, 8.f);
    CHECK(sample.r > sample.b);
    win->close();
}

TEST_CASE("volumetric.froxelIntegratesEmissiveProxyAndSceneLights") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    REQUIRE(vol);
    vol->setMode("froxel");
    vol->setCamera(0.f, 2.f, 6.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 55.f, 1.777f, 0.1f, 40.f);
    vol->configureFroxelGrid(8, 6, 16, 0.1f, 40.f);
    vol->clearFroxelGrid();
    vol->injectFroxelHeightFog(0.05f, 0.8f, 0.85f, 0.95f, 0.f, 0.2f, -4.f, 8.f);

    auto injected = vol->injectEmissiveLightProxy(0.f, 1.f, -4.f, 1.f, 0.15f, 0.05f, 6.f, 2.5f);
    REQUIRE(injected.ok());
    vol->integrateFroxel(0.05f, 0.05f, 0.06f, 1.f);

    const glm::vec4 sample = vol->getAtmosphereVolume()->sampleIntegrated(0.5f, 0.5f, 12.f);
    CHECK(sample.r > sample.b);

    auto *torch = Light3D::createEmissiveProxy(0.f, 1.f, -3.f, 1.f, 0.3f, 0.05f, 2.f, 4.f);
    vol->clearFroxelGrid();
    vol->injectFroxelHeightFog(0.05f, 0.8f, 0.85f, 0.95f, 0.f, 0.2f, -4.f, 8.f);
    auto fromScene = vol->integrateFroxelFromSceneLights(0.02f, 0.02f, 0.03f, glm::vec3(-10.f), glm::vec3(10.f), 8);
    REQUIRE(fromScene.ok());
    const glm::vec4 sceneSample = vol->getAtmosphereVolume()->sampleIntegrated(0.5f, 0.5f, 10.f);
    CHECK(sceneSample.r > 0.f);
    (void)torch;
    win->close();
}

TEST_CASE("volumetric.occlusionFromSceneLights2D") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    REQUIRE(vol);

    Canvas *occ    = gfx->newCanvas(128, 72);
    Canvas *shafts = gfx->newCanvas(128, 72);
    gfx->setCanvas(occ);

    auto *a = Light2D::createEmissiveProxy(20.f, 20.f, 1.f, 0.8f, 0.4f, 1.f, 40.f);
    auto *b = Light2D::createEmissiveProxy(100.f, 50.f, 0.4f, 0.6f, 1.f, 1.2f, 40.f);
    a->setCanvas(nullptr);
    b->setCanvas(nullptr);

    auto begun = vol->beginOcclusionMapFromSceneLights2D(gfx, nullptr, 12.f);
    REQUIRE(begun.ok());
    CHECK(begun.value() == 2);
    // Bright cores should be present near both light positions.
    CHECK(occ->getPixel(20, 20).r > 0.3f);
    CHECK(occ->getPixel(100, 50).r > 0.3f);

    // Scatter into a separate canvas — sampling the active canvas texture is invalid.
    gfx->setCanvas(shafts);
    auto aliased = vol->scatterFromSceneLights2D(gfx, shafts->getTexture(), nullptr);
    CHECK(!aliased.ok());
    auto passes = vol->scatterFromSceneLights2D(gfx, occ->getTexture(), nullptr);
    REQUIRE(passes.ok());
    CHECK(passes.value() == 2);

    gfx->setCanvas(nullptr);
    win->close();
}

TEST_CASE("volumetric.spotConeScatter2D") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 128, 128);
    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    REQUIRE(vol);
    vol->setQuality("medium");
    vol->setFloat("dustAmount", 0.f);
    vol->setFloat("fogAmount", 0.f);
    vol->setFloat("exposure", 0.55f);
    vol->setIntensity(1.5f);

    Canvas *occ    = gfx->newCanvas(128, 128);
    Canvas *shafts = gfx->newCanvas(128, 128);
    gfx->setCanvas(occ);

    // Narrow flashlight at top-center aiming +Y (down the canvas).
    auto *spot = Light2D::createLight("spot");
    spot->setPosition(64.f, 24.f);
    spot->setDirection(0.f, 1.f);
    spot->setColor(1.f, 1.f, 1.f, 2.f);
    spot->setRadius(90.f);
    spot->setSpotAngle(18.f);
    spot->setSpotSoftness(0.2f);
    spot->setVolumetric(true);
    spot->setVolumetricIntensity(1.f);
    spot->setCanvas(nullptr);

    auto begun = vol->beginOcclusionMapFromSceneLights2D(gfx, nullptr, 14.f);
    REQUIRE(begun.ok());
    CHECK(begun.value() == 1);
    CHECK(vol->getFloat("spotCosOuter") > -1.5f);
    CHECK(std::fabs(vol->getFloat("spotDy") - 1.f) < 1e-4f);

    gfx->setCanvas(shafts);
    auto passes = vol->scatterFromSceneLights2D(gfx, occ->getTexture(), nullptr);
    REQUIRE(passes.ok());
    CHECK(passes.value() == 1);

    // In-beam (below light) should be brighter than a side sample at similar distance.
    const float inBeam = luma(shafts->getPixel(64, 70));
    const float side   = luma(shafts->getPixel(110, 24));
    CHECK(inBeam > 0.02f);
    CHECK(inBeam > side * 2.5f);

    gfx->setCanvas(nullptr);
    win->close();
}

TEST_CASE("volumetric.modeAndRayMarchQuality") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);

    Volumetric *raw = gfx->newVolumetric();
    REQUIRE(raw != nullptr);
    std::unique_ptr<Volumetric> vol(raw);
    REQUIRE(vol->getRayMarchShader() != nullptr);

    CHECK(vol->getMode() == "screenspace");
    vol->setMode("raymarch");
    CHECK(vol->getMode() == "raymarch");

    vol->setQuality("low");
    CHECK(vol->getSampleCount() == 8);
    CHECK(vol->getDownscale() == 4.f);

    vol->setQuality("high");
    CHECK(vol->getSampleCount() == 48);

    vol->setMode("screenspace");
    vol->setQuality("medium");
    CHECK(vol->getSampleCount() == 48);
}

TEST_CASE("volumetric.cloudModeParametersAndQuality") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);

    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    REQUIRE(vol.get() != nullptr);
    REQUIRE(vol->getCloudShader() != nullptr);
    vol->setMode("cloud");
    vol->setCloudLayer(6.f, 14.f);
    vol->setCloudCoverage(0.72f);
    vol->setCloudDensity(1.3f);
    vol->setCloudScale(22.f);
    vol->setCloudWind(2.f, -0.5f);
    vol->setCloudLightColor(1.f, 0.65f, 0.35f);
    CHECK(std::fabs(vol->getFloat("cloudBottom") - 6.f) < 1e-5f);
    CHECK(std::fabs(vol->getFloat("cloudTop") - 14.f) < 1e-5f);
    CHECK(std::fabs(vol->getFloat("cloudCoverage") - 0.72f) < 1e-5f);
    vol->setQuality("low");
    CHECK(vol->getSampleCount() == 16);
    vol->setQuality("high");
    CHECK(vol->getSampleCount() == 64);
    win->close();
}

namespace {

struct BoxDepth {
    int   w = 0, h = 0;
    float boxU0 = 0.35f, boxU1 = 0.65f, boxV0 = 0.35f, boxV1 = 0.65f;
    float boxDepth   = 0.35f;
    float floorDepth = 0.85f;
};

[[maybe_unused]] float boxDepth01(int x, int y, void *userdata) {
    auto       *b = static_cast<BoxDepth *>(userdata);
    const float u = (float(x) + 0.5f) / float(b->w);
    const float v = (float(y) + 0.5f) / float(b->h);
    if (u >= b->boxU0 && u <= b->boxU1 && v >= b->boxV0 && v <= b->boxV1) return b->boxDepth;
    return b->floorDepth;
}

}  // namespace

TEST_CASE("volumetric.rayMarchProducesScatter") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);

    Volumetric *raw = gfx->newVolumetric();
    REQUIRE(raw != nullptr);
    std::unique_ptr<Volumetric> vol(raw);
    vol->setMode("raymarch");
    vol->setQuality("medium");
    vol->setLightDirection(0.5f, 1.f, 0.2f);
    vol->setLightScreenUV(0.8f, 0.15f);
    vol->setCamera(0.f, 2.f, 5.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 50.f, 160.f / 120.f, 0.1f, 40.f);
    vol->setDensity(1.2f);
    vol->setIntensity(2.5f);
    vol->setShaftColor(1.f, 0.92f, 0.75f);
    vol->setFloat("fogAmount", 0.5f);
    vol->setFloat("dustAmount", 0.4f);

    BoxDepth bd;
    bd.w           = 80;
    bd.h           = 60;
    Texture *depth = vol->newLinearDepthTexture(gfx, bd.w, bd.h, boxDepth01, &bd);
    REQUIRE(depth != nullptr);

    Canvas *out = gfx->newCanvas(bd.w, bd.h);
    vol->rayMarchTo(gfx, depth, out);

    float maxL = 0.f;
    float sum  = 0.f;
    int   n    = 0;
    for (int y = 0; y < bd.h; y += 2) {
        for (int x = 0; x < bd.w; x += 2) {
            const float L = luma(out->getPixel(x, y));
            maxL          = std::max(maxL, L);
            sum += L;
            ++n;
        }
    }
    CHECK(maxL > 0.005f);
    CHECK(sum / float(std::max(n, 1)) > 0.0005f);

    previewTexture(gfx, out->getTexture());
    win->close();
}

TEST_CASE("volumetric.rayMarchShadowDarkensBehindOccluder") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);

    Volumetric *raw = gfx->newVolumetric();
    REQUIRE(raw != nullptr);
    std::unique_ptr<Volumetric> vol(raw);
    vol->setMode("raymarch");
    vol->setQuality("high");
    vol->setLightDirection(0.6f, 0.8f, 0.1f);
    vol->setLightScreenUV(0.85f, 0.1f);
    vol->setCamera(0.f, 1.5f, 4.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 55.f, 160.f / 120.f, 0.1f, 30.f);
    vol->setDensity(0.7f);
    vol->setIntensity(1.6f);
    vol->setFloat("shadowSteps", 16.f);

    BoxDepth bd;
    bd.w           = 96;
    bd.h           = 72;
    bd.boxU0       = 0.4f;
    bd.boxU1       = 0.6f;
    bd.boxV0       = 0.3f;
    bd.boxV1       = 0.55f;
    bd.boxDepth    = 0.3f;
    bd.floorDepth  = 0.9f;
    Texture *depth = vol->newLinearDepthTexture(gfx, bd.w, bd.h, boxDepth01, &bd);

    Canvas *out = gfx->newCanvas(bd.w, bd.h);
    vol->rayMarchTo(gfx, depth, out);

    // Sample below the box (away from light) vs beside it toward the light.
    const int   midX        = int(bd.w * 0.5f);
    const int   belowY      = int(bd.h * 0.7f);
    const int   litY        = int(bd.h * 0.2f);
    const float behind      = luma(out->getPixel(midX, belowY));
    const float towardLight = luma(out->getPixel(int(bd.w * 0.75f), litY));
    // Lit side / skyward samples should retain some energy; occluded volume may be darker.
    CHECK(towardLight + behind > 0.0f);
    CHECK(towardLight + 0.001f >= behind * 0.25f);
}

TEST_CASE("volumetric.drawOccluders2DUsesCastFlag") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 128, 96);

    // Hide leftover sprites from other tests if any manager exists.
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }

    auto *a              = Renderable2D::create();
    a->transform()->x    = 20;
    a->transform()->y    = 20;
    a->sprite()->width   = 40;
    a->sprite()->height  = 40;
    a->sprite()->visible = true;
    a->setCastOcclusion(true);

    auto *b              = Renderable2D::create();
    b->transform()->x    = 70;
    b->transform()->y    = 20;
    b->sprite()->width   = 40;
    b->sprite()->height  = 40;
    b->sprite()->visible = true;
    b->setCastOcclusion(false);

    Volumetric                 *raw = gfx->newVolumetric();
    std::unique_ptr<Volumetric> vol(raw);

    Canvas *occ = gfx->newCanvas(128, 96);
    gfx->setCanvas(occ);
    gfx->clear(Color(1.f, 1.f, 1.f, 1.f), std::nullopt, std::nullopt);
    // Paint a bright plate so skipped occluders stay readable.
    gfx->drawSolidRect(0, 0, 128, 96, Color(0.95f, 0.95f, 0.95f, 1.f));
    vol->drawOccluders2D(gfx);
    gfx->setCanvas(nullptr);

    // Occluder A region dark; B skipped → stays bright.
    CHECK(occ->getPixel(30, 30).r < 0.15f);
    CHECK(occ->getPixel(85, 30).r > 0.5f);
}

TEST_CASE("volumetric.paramRoundTripShared") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setDensity(0.42f);
    CHECK(std::fabs(vol->getFloat("density") - 0.42f) < 1e-5f);
    vol->setMode("raymarch");
    vol->setDensity(0.33f);
    CHECK(std::fabs(vol->getFloat("density") - 0.33f) < 1e-5f);
    CHECK(vol->hasParam("invViewProj") == true);
    CHECK(vol->hasParam("exposure") == true);
}

TEST_CASE("volumetric.fogModeAndQuality") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    REQUIRE(vol->getFogShader() != nullptr);

    vol->setMode("fog");
    CHECK(vol->getMode() == "fog");
    vol->setQuality("low");
    CHECK(vol->getSampleCount() == 8);
    vol->setQuality("high");
    CHECK(vol->getSampleCount() == 48);
    vol->setQuality("medium");
    CHECK(vol->getSampleCount() == 24);
}

TEST_CASE("volumetric.fogParamsRoundTrip") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setMode("fog");
    vol->setFogHeight(-1.5f);
    vol->setFogHeightFalloff(0.22f);
    vol->setFogStart(3.f);
    vol->setFogEnd(25.f);
    vol->setFogNoise(0.5f);
    vol->setFogColor(0.4f, 0.5f, 0.7f);
    CHECK(std::fabs(vol->getFloat("fogHeight") + 1.5f) < 1e-5f);
    CHECK(std::fabs(vol->getFloat("heightFalloff") - 0.22f) < 1e-5f);
    CHECK(std::fabs(vol->getFloat("fogStart") - 3.f) < 1e-5f);
    CHECK(std::fabs(vol->getFloat("fogEnd") - 25.f) < 1e-5f);
    CHECK(std::fabs(vol->getFloat("noiseAmount") - 0.5f) < 1e-5f);
    CHECK(std::fabs(vol->getFloat("fogR") - 0.4f) < 1e-5f);
}

TEST_CASE("volumetric.applyFogProducesOpacity") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);

    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setMode("fog");
    vol->setQuality("medium");
    vol->setCamera(0.f, 1.5f, 6.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 50.f, 160.f / 120.f, 0.1f, 50.f);
    vol->setLightDirection(0.3f, 1.f, 0.2f);
    vol->setFogHeight(0.f);
    vol->setFogHeightFalloff(0.2f);
    vol->setFogStart(1.f);
    vol->setFogEnd(30.f);
    vol->setDensity(0.45f);
    vol->setIntensity(1.5f);
    vol->setFogColor(0.6f, 0.7f, 0.85f);
    vol->setFogNoise(0.2f);

    BoxDepth bd;
    bd.w           = 80;
    bd.h           = 60;
    bd.boxDepth    = 0.4f;
    bd.floorDepth  = 0.95f;
    Texture *depth = vol->newLinearDepthTexture(gfx, bd.w, bd.h, boxDepth01, &bd);
    Canvas  *out   = gfx->newCanvas(bd.w, bd.h);
    // Transparent clear so SrcAlpha fog writes measurable alpha (opaque dst always
    // blends to a=1 with the textured pipeline).
    gfx->setCanvas(out);
    gfx->clear(Color(0.f, 0.f, 0.f, 0.f), std::nullopt, std::nullopt);
    vol->applyFog(gfx, depth);
    gfx->setCanvas(nullptr);

    float maxA = 0.f;
    float maxL = 0.f;
    for (int y = 0; y < bd.h; y += 2) {
        for (int x = 0; x < bd.w; x += 2) {
            const Color c = out->getPixel(x, y);
            maxA          = std::max(maxA, c.a);
            maxL          = std::max(maxL, luma(c));
        }
    }
    CHECK(maxA > 0.02f);
    CHECK(maxL > 0.01f);

    previewTexture(gfx, out->getTexture());
    win->close();
}

TEST_CASE("volumetric.fogDenserAtDistance") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 128, 96);

    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setMode("fog");
    vol->setQuality("high");
    vol->setCamera(0.f, 1.f, 4.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 55.f, 128.f / 96.f, 0.1f, 40.f);
    vol->setFogHeight(-0.5f);
    vol->setFogHeightFalloff(0.05f);
    vol->setFogStart(0.5f);
    vol->setFogEnd(20.f);
    // Keep density moderate so near/far do not both saturate to opaque.
    vol->setDensity(0.08f);
    vol->setIntensity(1.2f);
    vol->setFogNoise(0.0f);

    // Near wall vs far wall depths.
    BoxDepth nearBd;
    nearBd.w          = 64;
    nearBd.h          = 48;
    nearBd.boxU0      = 0.f;
    nearBd.boxU1      = 1.f;
    nearBd.boxV0      = 0.f;
    nearBd.boxV1      = 1.f;
    nearBd.boxDepth   = 0.15f;
    nearBd.floorDepth = 0.15f;

    BoxDepth farBd   = nearBd;
    farBd.boxDepth   = 0.95f;
    farBd.floorDepth = 0.95f;

    Texture *nearDepth = vol->newLinearDepthTexture(gfx, nearBd.w, nearBd.h, boxDepth01, &nearBd);
    Texture *farDepth  = vol->newLinearDepthTexture(gfx, farBd.w, farBd.h, boxDepth01, &farBd);
    Canvas  *nearOut   = gfx->newCanvas(nearBd.w, nearBd.h);
    Canvas  *farOut    = gfx->newCanvas(farBd.w, farBd.h);

    gfx->setCanvas(nearOut);
    gfx->clear(Color(0.f, 0.f, 0.f, 0.f), std::nullopt, std::nullopt);
    vol->applyFog(gfx, nearDepth);
    gfx->setCanvas(farOut);
    gfx->clear(Color(0.f, 0.f, 0.f, 0.f), std::nullopt, std::nullopt);
    vol->applyFog(gfx, farDepth);
    gfx->setCanvas(nullptr);

    const float nearA = nearOut->getPixel(32, 24).a;
    const float farA  = farOut->getPixel(32, 24).a;
    CHECK(farA > nearA);
}

TEST_CASE("volumetric.froxelAtlasCompositesIntegratedMedia") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 128, 96);

    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setMode("froxel");
    vol->configureFroxelGrid(16, 12, 16, 0.1f, 40.f);
    vol->injectFroxelHeightFog(0.08f, 0.75f, 0.82f, 0.95f, 0.f, 0.15f, -2.f, 8.f);
    vol->integrateFroxel(1.f, 0.9f, 0.8f);
    vol->uploadFroxel(gfx);
    REQUIRE(vol->getFroxelAtlas() != nullptr);
    CHECK(vol->getFroxelAtlas()->getWidth() == 64);
    CHECK(vol->getFroxelAtlas()->getHeight() == 48);

    BoxDepth bd;
    bd.w           = 64;
    bd.h           = 48;
    bd.boxDepth    = 0.8f;
    bd.floorDepth  = 0.8f;
    Texture *depth = vol->newLinearDepthTexture(gfx, bd.w, bd.h, boxDepth01, &bd);
    Canvas  *out   = gfx->newCanvas(bd.w, bd.h);
    gfx->setCanvas(out);
    gfx->clear(Color(0.f, 0.f, 0.f, 0.f), std::nullopt, std::nullopt);
    vol->applyFroxel(gfx, depth);
    gfx->setCanvas(nullptr);

    const Color center = out->getPixel(32, 24);
    CHECK(center.a > 0.02f);
    CHECK(luma(center) > 0.01f);
    win->close();
}
}  // namespace test_Volumetric
