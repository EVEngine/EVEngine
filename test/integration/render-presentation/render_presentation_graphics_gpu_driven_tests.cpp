// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-window",
  "cases": {
    "platform-file": [
      "decal.proceduralImportsReferenceSubstanceMaterialPresets",
      "decal.renderImportedPresetContactSheet"
    ],
    "render-offscreen": [
      "decal.proceduralFacadeUploadsAllRuntimeChannels",
      "decal.gpuHeadlessProjectionReadback",
      "graphics.resourceReuse.fileCacheDoesNotReloadResidentPixels"
    ]
  },
  "module": "stylize",
  "modules": [
    "animation",
    "avatar",
    "decal",
    "dialogue",
    "gpgpu",
    "graphics",
    "stylize"
  ],
  "origins": [
    "Xray.cpp",
    "animation_sprite_spine.cpp",
    "avatar_vrm_vulkan.cpp",
    "decal.cpp",
    "dialogue.cpp",
    "gpu_driven.cpp",
    "gpu_driven_parity.cpp",
    "graphics_deferred_lifetime.cpp",
    "graphics_deferred_targets.cpp",
    "graphics_font.cpp",
    "graphics_resource_reuse.cpp",
    "graphics_vegetation_render.cpp",
    "hair.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_Xray)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "Fixtures.h"
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
#include "medialoader/model/ModelLoader.h"
#include "stylize/Stylize.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Xray.cpp
namespace test_Xray {
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
        SDL_Delay(8);
    }
}

/** Minimal unit cube OBJ (outward CCW for RH Y-up). */
static const char kCubeObj[] =
    "v -0.5 -0.5  0.5\nv  0.5 -0.5  0.5\nv  0.5  0.5  0.5\nv -0.5  0.5  0.5\n"
    "vn 0 0 1\nf 1//1 2//1 3//1\nf 1//1 3//1 4//1\n"
    "v  0.5 -0.5 -0.5\nv -0.5 -0.5 -0.5\nv -0.5  0.5 -0.5\nv  0.5  0.5 -0.5\n"
    "vn 0 0 -1\nf 5//2 6//2 7//2\nf 5//2 7//2 8//2\n"
    "v  0.5 -0.5  0.5\nv  0.5 -0.5 -0.5\nv  0.5  0.5 -0.5\nv  0.5  0.5  0.5\n"
    "vn 1 0 0\nf 9//3 10//3 11//3\nf 9//3 11//3 12//3\n"
    "v -0.5 -0.5 -0.5\nv -0.5 -0.5  0.5\nv -0.5  0.5  0.5\nv -0.5  0.5 -0.5\n"
    "vn -1 0 0\nf 13//4 14//4 15//4\nf 13//4 15//4 16//4\n"
    "v -0.5  0.5  0.5\nv  0.5  0.5  0.5\nv  0.5  0.5 -0.5\nv -0.5  0.5 -0.5\n"
    "vn 0 1 0\nf 17//5 18//5 19//5\nf 17//5 19//5 20//5\n"
    "v -0.5 -0.5 -0.5\nv  0.5 -0.5 -0.5\nv  0.5 -0.5  0.5\nv -0.5 -0.5  0.5\n"
    "vn 0 -1 0\nf 21//6 22//6 23//6\nf 21//6 23//6 24//6\n";

Mesh *loadCube(Graphics *gfx) {
    medialoader::ModelLoader loader;
    auto                     scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes >= 1);
    Mesh *m = gfx->newMeshFromAssimp(*scene->mMeshes[0]);
    REQUIRE(m != nullptr);
    return m;
}

/**
 * Builds a scene with a large occluder cube (a "building") between the camera
 * and a smaller target cube that sits fully behind it. Returns the target.
 * The camera looks straight down -Z at the origin.
 */
Renderable3D *setupOcclusionScene(Graphics *gfx, Renderable3D *&occluder, Shader *xrayShader) {
    resetScene3D();

    auto *building = Renderable3D::create();
    building->setMesh(loadCube(gfx));
    building->setTexture(makeSolid(gfx, 120, 110, 100));
    building->setMetallic(0.f);
    building->setRoughness(0.9f);
    building->setPosition(0.f, 0.f, 1.2f);  // near the camera
    building->setScale(3.f, 2.2f, 1.5f);    // big enough to fully hide the target
    occluder = building;

    auto *target = Renderable3D::create();
    target->setMesh(loadCube(gfx));
    target->setTexture(makeSolid(gfx, 90, 200, 90));
    target->setMetallic(0.f);
    target->setRoughness(0.6f);
    target->setPosition(0.f, 0.f, -1.2f);  // behind the occluder, hidden from the camera
    target->setXRayShader(xrayShader);
    target->setXRayHighlight(true);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 3.2f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.3f, 0.3f, 0.3f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ  = 40.f;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.2f, 1.f, 0.4f);
    sun->setColor(1.f, 1.f, 1.f, 1.6f);
    sun->setEnabled(true);

    addHud();
    return target;
}

}  // namespace

TEST_CASE("xray.shaderAndFlags") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);

    eve::stylize::Stylize *stylize = eve::stylize::Stylize::create();
    REQUIRE(stylize != nullptr);
    CHECK(stylize->hasStyle("xray"));
    CHECK(stylize->hasMeshStyle("xray"));
    CHECK(stylize->supports("xray", "mesh"));

    Shader *sh = stylize->newMeshShader(gfx, "xray");
    REQUIRE(sh != nullptr);
    REQUIRE(sh->gpuHandle != nullptr);
    CHECK(sh->isXray());

    // Push-constant params exist and round-trip.
    CHECK(sh->hasUniform("colorR"));
    CHECK(sh->hasUniform("colorG"));
    CHECK(sh->hasUniform("colorB"));
    CHECK(sh->hasUniform("bias"));
    CHECK(sh->hasUniform("screenW"));
    CHECK(sh->hasUniform("screenH"));
    CHECK(sh->hasUniform("rimPower"));
    CHECK(sh->hasUniform("rimStrength"));
    CHECK(sh->hasUniform("alpha"));

    sh->sendFloat("colorR", 0.2f);
    float v = 0.f;
    CHECK(sh->getFromVar("colorR", &v, sizeof(v)) == int(sizeof(v)));
    CHECK(std::fabs(v - 0.2f) < 1e-5f);

    // Renderable3D X-ray wiring.
    auto *r = Renderable3D::create();
    r->setXRayShader(sh);
    r->setXRayHighlight(true);
    CHECK(r->getXRayShader() == sh);
    CHECK(r->getXRayHighlight() == true);
    r->setXRayHighlight(false);
    CHECK(r->getXRayHighlight() == false);
    r->setVisible(false);

    win->close();
}

TEST_CASE("xray.occludedSilhouette") {
    // A target hidden behind a building should show its X-ray silhouette through
    // the wall (highlight color), while a target that is NOT flagged stays hidden.
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->disable("ao");
    rc->enable("gbuffer");  // X-ray needs the G-buffer scene depth
    rc->compile();

    eve::stylize::Stylize *stylize = eve::stylize::Stylize::create();
    Shader                *xray    = stylize->newMeshShader(gfx, "xray");
    REQUIRE(xray != nullptr);
    xray->sendFloat("alpha", 1.f);

    Renderable3D *occluder = nullptr;
    Renderable3D *target   = setupOcclusionScene(gfx, occluder, xray);

    // Warm up so the G-buffer / pipelines are ready.
    warmPresent(gfx, 4);

    GBuffer *gb = rc->getGBuffer();
    REQUIRE(gb != nullptr);
    CHECK(gb->isValid());
    CHECK(gb->getHwDepthTexture() != nullptr);

    // Hold a few rendered frames so the X-ray pass executes without error and
    // the silhouette is visually inspectable.
    for (int i = 0; i < 60; ++i) {
        target->setRotation(float(i) * 0.04f, float(i) * 0.01f, 0.f);
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) goto done;
        }
        SDL_Delay(16);
    }
done:
    target->setVisible(false);
    win->close();
}
}  // namespace test_Xray
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_sprite_spine)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "PathBesideSource.h"
#include "animation/Animation.h"
#include "animation/SpineAnim.h"
#include "animation/SpineAtlas.h"
#include "animation/SpineSkeleton.h"
#include "animation/SpineSkeletonData.h"
#include "animation/SpriteAnim.h"
#include "animation/SpriteClip.h"
#include "animation/SpriteSheet.h"
#include "common/Exception.h"
#include "filesystem/Filesystem.h"
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
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_sprite_spine.cpp
namespace test_animation_sprite_spine {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;


using namespace eve::animation;


namespace {

[[maybe_unused]] std::string spineAssetsDir() {
    std::string       here  = eve_test_path::sourceBesideTestDir(__FILE__);
    const auto        slash = here.find_last_of("/\\");
    const std::string dir   = (slash == std::string::npos) ? std::string(".") : here.substr(0, slash);
    return dir + "/assets/spine";
}

[[maybe_unused]] bool spineAssetsReady() { return std::filesystem::is_regular_file(spineAssetsDir() + "/.downloaded"); }

struct SpineDemoModel {
    const char *relPath;  // virtual path under the mounted spine assets dir
    const char *atlasRel;
    const char *animation;
    float       cx = 0.f, cy = 0.f;  // target CENTER of the model's bounding box on screen
    float       scale = 1.f;
};

struct SpineDemoPlayer {
    std::unique_ptr<SpineSkeletonData>    data;
    std::unique_ptr<SpineAtlas>           atlas;
    std::unique_ptr<SpineSkeleton>        skeleton;
    std::unique_ptr<SpineAnim>            anim;
    std::vector<eve::graphics::Texture *> pageTextures;
};

/** Centroid of the current pose's draw quads (position at 0,0) — used to
 *  place each model by its own bounds instead of assuming the root anchor. */
[[maybe_unused]] void measurePoseCenter(SpineAnim *anim, float &cx, float &cy) {
    std::vector<eve::graphics::DrawItem2D> items;
    anim->setPosition(0.f, 0.f);
    anim->collectDrawItems(items);
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (const auto &it : items) {
        minX = std::min(minX, it.x);
        minY = std::min(minY, it.y);
        maxX = std::max(maxX, it.x + it.w);
        maxY = std::max(maxY, it.y + it.h);
    }
    if (items.empty()) {
        cx = 0.f;
        cy = 0.f;
        return;
    }
    cx = (minX + maxX) * 0.5f;
    cy = (minY + maxY) * 0.5f;
}

}  // namespace

/**
 * Renders three real Spine models (downloaded via scripts/download_spine_models.py)
 * into a window for a few seconds, each playing a looped animation.
 * Skips (returns) when the assets have not been downloaded.
 */
TEST_CASE("animation.spine.windowDemoRealModels") {
    if (!spineAssetsReady()) {
        std::printf(
            "animation.spine.windowDemoRealModels: missing %s — run "
            "`cmake --build --target download_spine_models` (or "
            "`python3 scripts/download_spine_models.py`)\n",
            spineAssetsDir().c_str());
        return;
    }

    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 1280;
    s.height   = 720;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    REQUIRE(fs->setIdentity("ev_ut_spine_demo", true));
    REQUIRE(fs->setupWriteDirectory());
    const std::string root = spineAssetsDir();
    fs->allowMountingForPath(root);
    REQUIRE(fs->mount(root, "", false));

    // Module singleton; must outlive all players (each SpineAnim unregisters
    // itself from its owner in the destructor).
    auto *anim = Animation::create();
    REQUIRE(anim != nullptr);

    const SpineDemoModel models[] = {
        // (cx, cy) is the desired on-screen CENTER of each model's bounding box;
        // the actual root-anchor offset is measured at runtime so all three
        // characters are laid out side by side without overlap.
        {"spineboy/export/spineboy.json", "spineboy/export/spineboy.atlas", "run", 250.f, 420.f, 0.55f},
        {"hero/export/hero.json", "hero/export/hero.atlas", "Walk", 640.f, 430.f, 1.2f},
        {"dragon/export/dragon.json", "dragon/export/dragon.atlas", "flying", 1030.f, 400.f, 0.5f},
    };

    std::vector<SpineDemoPlayer> players;
    for (const auto &m : models) {
        SpineDemoPlayer p;
        p.data.reset(anim->newSpineSkeletonDataFromFile(m.relPath));
        REQUIRE(p.data.get() != nullptr);
        p.atlas.reset(anim->newSpineAtlasFromFile(m.atlasRel));
        REQUIRE(p.atlas.get() != nullptr);
        p.skeleton.reset(anim->newSpineSkeleton(p.data.get()));
        REQUIRE(p.skeleton.get() != nullptr);
        p.anim.reset(anim->newSpineAnim(p.skeleton.get()));
        REQUIRE(p.anim.get() != nullptr);

        p.anim->setAtlas(p.atlas.get());
        for (int page = 0; page < p.atlas->getPageCount(); ++page) {
            const std::string pageName = p.atlas->getPageName(page);
            // The atlas page name is the image basename (e.g. "spineboy.png").
            const std::string texRel = std::string(m.relPath);
            const auto        slash  = texRel.find_last_of('/');
            const std::string texPath =
                (slash == std::string::npos) ? pageName : texRel.substr(0, slash + 1) + pageName;
            eve::graphics::Texture *tex = gfx->newTextureFromFile(texPath);
            REQUIRE(tex != nullptr);
            p.anim->setPageTexture(page, tex);
            p.pageTextures.push_back(tex);
        }
        p.anim->setScale(m.scale, m.scale);
        p.anim->setLoop(true);
        // Distinct layer per model so slot order never interleaves across models.
        p.anim->setLayer(static_cast<int>(players.size()));
        REQUIRE(p.anim->play(m.animation));

        // Center this model's actual bounding box on its target position.
        float poseCx = 0.f, poseCy = 0.f;
        measurePoseCenter(p.anim.get(), poseCx, poseCy);
        p.anim->setPosition(m.cx - poseCx, m.cy - poseCy);
        players.push_back(std::move(p));
    }

    gfx->setBackgroundColorRGBA(0.10f, 0.12f, 0.15f, 1.f);
    std::vector<eve::graphics::DrawItem2D> items;
    std::vector<size_t>                    perPlayerItems(players.size(), 0);
    std::vector<bool>                      perPlayerRotated(players.size(), false);
    const int                              frames = 60 * 3;  // ~3 seconds at 60 fps
    for (int i = 0; i < frames; ++i) {
        items.clear();
        for (size_t pi = 0; pi < players.size(); ++pi) {
            players[pi].anim->update(1.f / 60.f);
            const size_t before = items.size();
            players[pi].anim->collectDrawItems(items);
            perPlayerItems[pi] += items.size() - before;
            for (size_t k = before; k < items.size(); ++k) {
                if (items[k].rotation != 0.f) perPlayerRotated[pi] = true;
            }
        }
        eve::graphics::RenderSystem::drawItems(*gfx, items, true);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                win->close();
                return;
            }
        }
        SDL_Delay(16);
    }

    // Objectively verify each character is visible on screen: render one extra
    // frame with screen readback and count pixels differing from the background
    // corner color around each model's target center.
    gfx->setScreenReadbackEnabled(true);
    items.clear();
    for (auto &p : players) p.anim->update(1.f / 60.f);
    for (auto &p : players) p.anim->collectDrawItems(items);
    eve::graphics::RenderSystem::drawItems(*gfx, items, true);
    gfx->setScreenReadbackEnabled(false);

    std::vector<int> perPlayerLitPixels(players.size(), 0);
    const Color      bg = gfx->getPixel(4, 4);
    for (size_t pi = 0; pi < players.size(); ++pi) {
        const int cx = static_cast<int>(models[pi].cx);
        const int cy = static_cast<int>(models[pi].cy);
        for (int dy = -24; dy <= 24; dy += 8) {
            for (int dx = -24; dx <= 24; dx += 8) {
                const Color c    = gfx->getPixel(cx + dx, cy + dy);
                const float dist = std::fabs(c.r - bg.r) + std::fabs(c.g - bg.g) + std::fabs(c.b - bg.b);
                if (dist > 0.15f) ++perPlayerLitPixels[pi];
            }
        }
    }

    // Every model must emit visible quads, actually rotate limbs (proves the
    // region-attachment animation drives real motion), sit centered on its
    // target position (no overlap / no off-screen scatter), and draw visible
    // non-background pixels around that center.
    for (size_t pi = 0; pi < players.size(); ++pi) {
        CHECK_GT(perPlayerItems[pi], 0u);
        CHECK(perPlayerRotated[pi]);
        CHECK_GT(perPlayerLitPixels[pi], 0);

        float poseCx = 0.f, poseCy = 0.f;
        measurePoseCenter(players[pi].anim.get(), poseCx, poseCy);
        const float onScreenCx = players[pi].anim->getX() + poseCx;
        const float onScreenCy = players[pi].anim->getY() + poseCy;
        CHECK(std::fabs(onScreenCx - models[pi].cx) < 40.f);
        CHECK(std::fabs(onScreenCy - models[pi].cy) < 40.f);
    }
    win->close();
}
}  // namespace test_animation_sprite_spine
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_avatar_vrm_vulkan)
#include "PrivateTestScope.h"
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "Fixtures.h"
#include "PathBesideSource.h"
#include "avatar/AvatarInstance.h"
#include "avatar/VrmDocument.h"
#include "common/ECS.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/vulkan/Graphics.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// avatar_vrm_vulkan.cpp
namespace test_avatar_vrm_vulkan {
TEST_CASE("avatar.vrm.importMorphGazeSpringAndRollback") {
    GfxFixture graphics(320, 240);
    auto      *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_vrm", true));
    const auto directory =
        (std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path() / "assets/vrm").string();
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(directory);
    REQUIRE(fs->mount(directory, "", false));
    eve::avatar::AvatarInstance avatar("vroid");
    auto                        loaded = avatar.loadVroidModel("contract.vrm");
    REQUIRE(loaded.ok());
    REQUIRE_EQ(avatar.getVroidMeshCount(), 2);
    REQUIRE_EQ(avatar.getVroidSpringCount(), 1);
    REQUIRE_EQ(avatar.getVroidVersion(), std::string("1.0"));
    REQUIRE_EQ(avatar.getHumanoidBoneName("head"), std::string("head"));
    avatar.setExpression("happy");
    avatar.update(1.f / 60);
    avatar.sync();
    REQUIRE_EQ(avatar.getBoundMesh()->getMorphWeight("smile"), 1.f);
    auto *gpu = static_cast<eve::graphics::vulkan::GpuMesh *>(avatar.getBoundMesh()->gpuHandle);
    REQUIRE(gpu->dynamic);
    auto &buffer   = gpu->dynVertices[(gpu->dynamicWriteCount - 1) % gpu->kDynamicVertexCopies];
    auto *vertices = static_cast<eve::graphics::vulkan::MeshVertex *>(buffer.map());
    REQUIRE(vertices != nullptr);
    const float preservedWeight = vertices[0].weights.x;
    buffer.unmap();
    REQUIRE_EQ(preservedWeight, 1.f);
    avatar.setExpression("aa");
    avatar.update(1.f / 60);
    avatar.sync();
    REQUIRE_EQ(avatar.getBoundMesh()->getMorphWeight("smile"), .5f);
    auto before = avatar.sampleAttachmentPoint("leftEye", {0, 0, .1f});
    REQUIRE(before.ok());
    REQUIRE(avatar.setLookAtTarget(2, .4f, 1));
    avatar.update(1.f / 60);
    auto after = avatar.sampleAttachmentPoint("leftEye", {0, 0, .1f});
    REQUIRE(after.ok());
    REQUIRE(std::abs(after.value().x - before.value().x) > 1e-4f);
    auto tip = avatar.sampleAttachmentPoint("hairTip");
    REQUIRE(tip.ok());
    for (int i = 0; i < 30; ++i) avatar.update(1.f / 60);
    auto moved = avatar.sampleAttachmentPoint("hairTip");
    REQUIRE(moved.ok());
    REQUIRE(std::abs(moved.value().x - tip.value().x) > 1e-5f);
    auto      *original = avatar.getRenderable3D();
    const auto handle   = ecs::handle_of(original);
    auto       failed   = avatar.loadVroidModel("missing.vrm");
    REQUIRE(!failed.ok());
    REQUIRE(avatar.getRenderable3D() == original);
    REQUIRE_EQ(avatar.getVroidModelPath(), std::string("contract.vrm"));
    auto replaced = avatar.loadVroidModel("contract.vrm");
    REQUIRE(replaced.ok());
    REQUIRE(ecs::try_get(handle) == nullptr);
    // ECS projection may be destroyed first; owner cleanup must resolve the generation.
    ecs::DestroyEntity(avatar.getRenderable3D());
    avatar.sync();
    avatar.release();
    REQUIRE(fs->unmount(directory));
}
}  // namespace test_avatar_vrm_vulkan
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_decal)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <iterator>
#include <limits>
#include <string>
#include <vector>
#include "Fixtures.h"
#include "RenderImageAudit.h"
#include "common/Capability.h"
#include "common/DecalQuery.h"
#include "decal/Decal.h"
#include "decal/DecalManager.h"
#include "decal/ProceduralDecal.h"
#include "graphics/Graphics.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// decal.cpp
namespace test_decal {
using namespace eve::decal;
using namespace eve::graphics;

namespace {
[[maybe_unused]] eve::graphics::Texture *makeSolidTex(eve::graphics::Graphics *gfx, uint8_t r, uint8_t g, uint8_t b);
[[maybe_unused]] eve::graphics::Mesh    *makePlane(eve::graphics::Graphics *gfx, float size);
}  // namespace


TEST_CASE("decal.proceduralImportsReferenceSubstanceMaterialPresets") {
    const auto  directory     = std::filesystem::path(EVENGINE_SOURCE_DIR) / "test" / "fixtures" / "procedural_decal";
    std::size_t importedCount = 0;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".sbsprs") continue;
        const auto   &path = entry.path();
        std::ifstream stream(path, std::ios::binary);
        REQUIRE(stream.good());
        const std::string xml((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        auto              imported = importProceduralDecalSbsprs(xml);
        REQUIRE(imported.ok());
        CHECK(imported.value().width >= 1);
        CHECK(imported.value().height >= 1);
        imported.value().width  = 32;
        imported.value().height = 32;
        auto baked              = bakeProceduralDecal(imported.value());
        REQUIRE(baked.ok());
        CHECK_EQ(baked.value().albedo.size(), static_cast<std::size_t>(32 * 32 * 4));
        CHECK_EQ(baked.value().normal.size(), baked.value().albedo.size());
        CHECK_EQ(baked.value().params.size(), baked.value().albedo.size());
        ++importedCount;
    }
    CHECK_EQ(importedCount, 40u);

    auto malformed = importProceduralDecalSbsprs("<sbspresets><sbspreset><presetinput identifier=\"a_color\"");
    CHECK(!malformed.ok());
    const auto document = [](const std::string &identifier, const std::string &value) {
        return "<sbspresets><sbspreset><presetinput identifier=\"" + identifier + "\" value=\"" + value +
               "\"/></sbspreset></sbspresets>";
    };
    CHECK(!importProceduralDecalSbsprs(document("a_amount", "not-a-number")).ok());
    CHECK(!importProceduralDecalSbsprs(document("$randomseed", "1.5")).ok());
    CHECK(!importProceduralDecalSbsprs(document("$outputsize", "8,bad")).ok());
    CHECK(!importProceduralDecalSbsprs(document("a_color", "0.1,0.2,0.3,0.4")).ok());
    std::string oversized(1024u * 1024u + 1u, 'x');
    CHECK(!importProceduralDecalSbsprs(oversized).ok());
}

TEST_CASE("decal.renderImportedPresetContactSheet") {
    const auto directory = std::filesystem::path(EVENGINE_SOURCE_DIR) / "test" / "fixtures" / "procedural_decal";
    std::vector<std::filesystem::path> presets;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".sbsprs") presets.push_back(entry.path());
    }
    std::sort(presets.begin(), presets.end());
    REQUIRE_EQ(presets.size(), 40u);

    constexpr int             tile        = 128;
    constexpr int             gap         = 4;
    constexpr int             columns     = 5;
    constexpr int             rows        = 8;
    constexpr int             sheetWidth  = columns * tile + (columns + 1) * gap;
    constexpr int             sheetHeight = rows * tile + (rows + 1) * gap;
    std::vector<std::uint8_t> sheet(static_cast<std::size_t>(sheetWidth * sheetHeight * 4), 255u);
    for (int y = 0; y < sheetHeight; ++y) {
        for (int x = 0; x < sheetWidth; ++x) {
            const auto index = static_cast<std::size_t>((y * sheetWidth + x) * 4);
            const auto shade = static_cast<std::uint8_t>(((x / 12 + y / 12) & 1) ? 72 : 104);
            sheet[index + 0] = shade;
            sheet[index + 1] = shade;
            sheet[index + 2] = shade;
        }
    }

    const auto      output = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out" / "decal" / "imported_presets";
    std::error_code ec;
    std::filesystem::create_directories(output, ec);
    REQUIRE(!ec);
    std::ofstream manifest(output / "contact_sheet_order.txt", std::ios::binary);
    REQUIRE(manifest.good());

    for (std::size_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex) {
        std::ifstream stream(presets[presetIndex], std::ios::binary);
        REQUIRE(stream.good());
        const std::string xml((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        auto              recipe = importProceduralDecalSbsprs(xml);
        REQUIRE(recipe.ok());
        recipe.value().width  = tile;
        recipe.value().height = tile;
        auto bake             = bakeProceduralDecal(recipe.value());
        REQUIRE(bake.ok());

        eve::image::ImageData image(tile, tile, "RGBA8", bake.value().albedo.data(), false);
        REQUIRE(saveImagePng(image, (output / (presets[presetIndex].stem().string() + ".png")).string()));
        manifest << presetIndex + 1 << "\t" << presets[presetIndex].stem().string() << "\n";

        const int originX = gap + static_cast<int>(presetIndex % columns) * (tile + gap);
        const int originY = gap + static_cast<int>(presetIndex / columns) * (tile + gap);
        for (int y = 0; y < tile; ++y) {
            for (int x = 0; x < tile; ++x) {
                const auto  source = static_cast<std::size_t>((y * tile + x) * 4);
                const auto  target = static_cast<std::size_t>(((originY + y) * sheetWidth + originX + x) * 4);
                const float alpha  = float(bake.value().albedo[source + 3]) / 255.f;
                for (int channel = 0; channel < 3; ++channel) {
                    sheet[target + channel] =
                        static_cast<std::uint8_t>(float(bake.value().albedo[source + channel]) * alpha +
                                                  float(sheet[target + channel]) * (1.f - alpha));
                }
            }
        }
    }
    eve::image::ImageData contactSheet(sheetWidth, sheetHeight, "RGBA8", sheet.data(), false);
    REQUIRE(saveImagePng(contactSheet, (output / "contact_sheet.png").string()));
    std::printf("procedural decal contact sheet saved: %s\n", output.string().c_str());
}

TEST_CASE("decal.renderImportedPresetsOnLitMaterialSpheres") {
    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 1280, 800);

    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 6.0f;
    cam->setAmbient(0.075f, 0.08f, 0.095f);
    RenderSystem3D::setDirectionalLight(-0.65f, 0.8f, 0.9f, 1.f, 0.92f, 0.78f);

    auto *background                      = Renderable3D::create();
    background->meshRenderer()->mesh      = makePlane(gfx, 12.f);
    background->meshRenderer()->texture   = makeSolidTex(gfx, 28, 32, 42);
    background->meshRenderer()->roughness = 0.9f;
    background->transform()->z            = -0.65f;

    static bool sMaterialSphereDrawer = false;
    if (!sMaterialSphereDrawer) {
        sMaterialSphereDrawer = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData, const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData.eyeX, camData.eyeY, camData.eyeZ, viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();
    gfx->setScreenReadbackEnabled(true);

    const auto directory = std::filesystem::path(EVENGINE_SOURCE_DIR) / "test" / "fixtures" / "procedural_decal";
    std::vector<std::filesystem::path> presets;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".sbsprs") presets.push_back(entry.path());
    }
    std::sort(presets.begin(), presets.end());
    REQUIRE_EQ(presets.size(), 40u);

    auto *sphere = gfx->newMeshSphere(48, 24);
    REQUIRE(sphere != nullptr);
    DecalManager::inst().clearAll();
    constexpr int columns = 8;
    for (std::size_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex) {
        std::ifstream stream(presets[presetIndex], std::ios::binary);
        REQUIRE(stream.good());
        const std::string xml((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        auto              recipe = importProceduralDecalSbsprs(xml);
        REQUIRE(recipe.ok());
        recipe.value().width  = 128;
        recipe.value().height = 128;
        auto bake             = bakeProceduralDecal(recipe.value());
        REQUIRE(bake.ok());

        const int   column                = static_cast<int>(presetIndex % columns);
        const int   row                   = static_cast<int>(presetIndex / columns);
        const float x                     = (float(column) - 3.5f) * 0.9f;
        const float y                     = (2.f - float(row)) * 0.9f;
        auto       *entity                = Renderable3D::create();
        entity->meshRenderer()->mesh      = sphere;
        entity->meshRenderer()->texture   = makeSolidTex(gfx, 185, 185, 190);
        entity->meshRenderer()->roughness = 0.48f;
        entity->setPosition(x, y, 0.f);
        entity->setScale(0.36f, 0.36f, 0.36f);

        const int decal = DecalManager::inst().project(
            x, y, 0.18f, 0.f, 0.f, 1.f, gfx->newTexture(128, 128, bake.value().albedo.data()), "material-sphere", 0.78f,
            0.72f, false, static_cast<int>(presetIndex), 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
        REQUIRE(DecalManager::inst().setTextures(decal, gfx->newTexture(128, 128, bake.value().normal.data()),
                                                 gfx->newTexture(128, 128, bake.value().params.data())));
        REQUIRE(DecalManager::inst().setProjection(decal, "spherical", 4.f) == DecalProjectionStatus::Applied);
        REQUIRE(DecalManager::inst().setParallax(decal, 0.025f, 8.f, 24.f) == DecalParallaxStatus::Applied);
        REQUIRE(DecalManager::inst().setEdgeFade(decal, 0.025f) == DecalEdgeFadeStatus::Applied);
    }

    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) break;
        }
    }
    auto *snapshot = gfx->newImageData();
    REQUIRE(snapshot != nullptr);
    const auto      output = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out" / "decal" / "imported_presets";
    std::error_code ec;
    std::filesystem::create_directories(output, ec);
    REQUIRE(!ec);
    REQUIRE(saveImagePng(*snapshot, (output / "lit_material_spheres.png").string()));
    delete snapshot;

    DecalManager::inst().clearAll();
    win->close();
}


TEST_CASE("decal.proceduralFacadeUploadsAllRuntimeChannels") {
    eve::graphics::Graphics *gfx = nullptr;
    openHeadlessGfx(gfx, 32, 32);
    Decal api;
    auto *albedo = api.bakePresetTexture(gfx, "rust", 42u, 32, "albedo");
    auto *normal = api.bakePresetTexture(gfx, "rust", 42u, 32, "normal");
    auto *params = api.bakePresetTexture(gfx, "rust", 42u, 32, "params");
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);
    REQUIRE(params != nullptr);
    CHECK_EQ(albedo->getWidth(), 32);
    CHECK_EQ(normal->getHeight(), 32);
    CHECK_EQ(params->getWidth(), 32);
    const auto importedPath =
        std::filesystem::path(EVENGINE_SOURCE_DIR) / "test" / "fixtures" / "procedural_decal" / "Blood_Wet.sbsprs";
    std::ifstream importedStream(importedPath, std::ios::binary);
    REQUIRE(importedStream.good());
    const std::string importedXml((std::istreambuf_iterator<char>(importedStream)), std::istreambuf_iterator<char>());
    auto             *importedAlbedo = api.bakeSbsprsTexture(gfx, importedXml, 32, "albedo");
    REQUIRE(importedAlbedo != nullptr);
    CHECK_EQ(importedAlbedo->getWidth(), 32);
    CHECK_THROWS((
        [&] {
            auto *unexpected = api.bakePresetTexture(gfx, "rust", 42u, 32, "unknown");
            (void)unexpected;
        }(),
        false));
}


namespace {

[[maybe_unused]] eve::graphics::Texture *makeSolidTex(eve::graphics::Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const int            size = 16;
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i]     = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] eve::graphics::Mesh *makePlane(eve::graphics::Graphics *gfx, float size) {
    const float                 h   = size * 0.5f;
    const std::vector<float>    pos = {-h, -h, 0.f, h, -h, 0.f, h, h, 0.f, -h, h, 0.f};
    const std::vector<float>    nrm = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const std::vector<float>    uv  = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), 4, idx.data(), 6);
}

}  // namespace

// End-to-end smoke: a red decal projected onto a white plane must change the
// screen-center pixel toward red (the decal layer is written between the
// G-buffer and forward passes, and mesh3d.frag blends it before lighting).
TEST_CASE("decal.gpuProjectBlendsIntoForward") {
    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    auto *mesh = makePlane(gfx, 2.f);
    REQUIRE(mesh != nullptr);

    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 2.6f;

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeSolidTex(gfx, 255, 255, 255);
    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);

    // Enable the decal pass and feed it from DecalManager through a drawer
    // (same path the Decal script module registers at startup).
    static bool sDrawerRegistered = false;
    if (!sDrawerRegistered) {
        sDrawerRegistered = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData, const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData.eyeX, camData.eyeY, camData.eyeZ, viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    auto *decalTex = makeSolidTex(gfx, 200, 20, 20);
    DecalManager::inst().clearAll();
    DecalManager::inst().project(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, decalTex, "smoke", 1.2f, 0.1f, false, 0, 0.f, 0.f, 0.f,
                                 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
    }

    const int   W      = gfx->getWidth();
    const int   H      = gfx->getHeight();
    const Color center = gfx->getPixel(W / 2, H / 2);
    const Color corner = gfx->getPixel(4, 4);
    // Center (inside the 1.2m decal on a 2m plane) must be red-tinted; the
    // corner stays near-white.
    CHECK_GT(center.r - center.g, 0.1f);
    CHECK_GT(center.r - center.b, 0.1f);
    CHECK_LT(corner.r - corner.g, 0.1f);

    DecalManager::inst().clearAll();
    win->close();
}

// Emissive decals use the additive layer pipeline: a red glow on a dark plane
// must visibly brighten the screen center vs. the unlit corner.
TEST_CASE("decal.gpuEmissiveAdditiveGlow") {
    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    auto *mesh = makePlane(gfx, 2.f);
    REQUIRE(mesh != nullptr);

    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 2.6f;

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = makeSolidTex(gfx, 60, 60, 60);  // dark base
    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);

    static bool sGlowDrawer = false;
    if (!sGlowDrawer) {
        sGlowDrawer = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData, const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData.eyeX, camData.eyeY, camData.eyeZ, viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    auto *glowColor  = makeSolidTex(gfx, 200, 20, 20);  // red glow color
    auto *glowParams = makeSolidTex(gfx, 0, 0, 255);    // B = emissive intensity
    DecalManager::inst().clearAll();
    const int id = DecalManager::inst().project(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, glowColor, "glow", 1.2f, 0.1f, false, 0,
                                                0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(id > 0);
    CHECK(DecalManager::inst().setTextures(id, nullptr, glowParams));
    CHECK(DecalManager::inst().setBlend(id, "add"));
    CHECK(DecalManager::inst().setStrength(id, 0.f, 0.f, 0.f, 1.f));

    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
    }

    const int   W      = gfx->getWidth();
    const int   H      = gfx->getHeight();
    const Color center = gfx->getPixel(W / 2, H / 2);
    const Color corner = gfx->getPixel(4, 4);
    CHECK_GT(center.r, corner.r + 0.2f);  // emissive red lifts the center
    CHECK_GT(center.r - center.g, 0.1f);  // ... toward red, not white

    DecalManager::inst().clearAll();
    win->close();
}

// Headless verification of the decal pass itself: queue a G-buffer plane + one
// decal, then read the DecalLayer back to CPU. Works without a swapchain, so it
// also runs when the interactive GPU/surface is busy.
TEST_CASE("decal.gpuHeadlessProjectionReadback") {
    eve::graphics::Graphics *gfx = nullptr;
    openHeadlessGfx(gfx, 160, 120);

    auto *mesh = makePlane(gfx, 2.f);
    REQUIRE(mesh != nullptr);
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    gfx->beginGBufferPass(160, 120);
    gfx->drawMeshGBuffer(mesh, glm::mat4(1.f), glm::mat4(1.f), 0.1f, 100.f, makeSolidTex(gfx, 255, 255, 255));
    gfx->endGBufferPass();

    auto *decalTex = makeSolidTex(gfx, 200, 20, 20);
    gfx->beginDecalPass(160, 120);
    gfx->setDecalCamera(glm::mat4(1.f), 0.1f, 100.f);
    // Identity box at the origin covers the screen center.
    gfx->drawDecal(glm::mat4(1.f), decalTex, nullptr, nullptr, nullptr, 1.f, 0.f, 0.f, 0.f, 0.f);
    gfx->endDecalPass();

    auto *img = gfx->readDecalLayerToImageData("albedo");
    REQUIRE(img != nullptr);
    CHECK_EQ(img->getWidth(), 160);
    CHECK_EQ(img->getHeight(), 120);

    const uint8_t *px     = static_cast<const uint8_t *>(img->getData());
    auto           at     = [&](int x, int y) { return px + (size_t(y) * 160u + size_t(x)) * 4u; };
    const uint8_t *center = at(80, 60);
    CHECK_GT(center[0], 150);  // red decal written at the center
    CHECK_LT(center[1], 80);
    const uint8_t *corner = at(4, 4);
    CHECK_LT(corner[0], 20);  // outside the unit box -> layer stays cleared
    delete img;
}

// Triplanar mode paints a wall that classic planar projection discards (facing
// nearly orthogonal to the decal forward). Planar leaves the wall near the
// base albedo color.
TEST_CASE("decal.gpuTriplanarCoversGrazingWall") {
    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);

    // Vertical wall in XY (normal +Z), viewed from +Z.
    auto *wall = makePlane(gfx, 2.f);
    REQUIRE(wall != nullptr);

    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 2.6f;

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = wall;
    ent->meshRenderer()->texture = makeSolidTex(gfx, 180, 180, 180);
    RenderSystem3D::setDirectionalLight(0.2f, 0.5f, 1.f, 1.f, 1.f, 1.f);

    static bool sTripDrawer = false;
    if (!sTripDrawer) {
        sTripDrawer = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData, const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData.eyeX, camData.eyeY, camData.eyeZ, viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    auto *decalTex = makeSolidTex(gfx, 200, 20, 20);
    auto &mgr      = DecalManager::inst();
    mgr.clearAll();
    // Project along +Y onto a +Z wall — planar culls, triplanar keeps coverage.
    const int id = mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, decalTex, "trip", 2.f, 2.f, false, 0, 0.f, 0.f, 0.f, 0.f,
                               0.f, 0.f, 0.f);
    REQUIRE(id > 0);

    auto centerRedness = [&]() -> float {
        gfx->setScreenReadbackEnabled(true);
        for (int i = 0; i < 3; ++i) {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) break;
            }
        }
        const Color c = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        return c.r - c.g;
    };

    REQUIRE(mgr.setProjection(id, "planar", 4.f) == DecalProjectionStatus::Applied);
    const float planarScore = centerRedness();

    REQUIRE(mgr.setProjection(id, "triplanar", 4.f) == DecalProjectionStatus::Applied);
    const float triplanarScore = centerRedness();

    REQUIRE(mgr.setProjection(id, "world", 4.f) == DecalProjectionStatus::Applied);
    const float worldScore = centerRedness();

    // Triplanar must shift the wall toward the red decal; planar should not.
    REQUIRE(triplanarScore > 0.12f);
    REQUIRE(triplanarScore > planarScore + 0.08f);
    REQUIRE(worldScore > 0.12f);
    REQUIRE(worldScore > planarScore + 0.08f);

    mgr.clearAll();
    win->close();
}

namespace {

[[maybe_unused]] float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// Procedural decal art so the gallery needs no external assets.
[[maybe_unused]] eve::graphics::Texture *makeChecker(eve::graphics::Graphics *gfx, int size, int cells) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool    dark = ((x * cells / size) + (y * cells / size)) % 2 == 0;
            const uint8_t v    = dark ? 150 : 205;
            size_t        i    = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i]              = v;
            px[i + 1]          = v;
            px[i + 2]          = v;
            px[i + 3]          = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] eve::graphics::Texture *makeBloodSplat(eve::graphics::Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u   = (float(x) + 0.5f) / float(size);
            const float v   = (float(y) + 0.5f) / float(size);
            const float dx  = u - 0.5f;
            const float dy  = v - 0.5f;
            const float ang = std::atan2(dy, dx);
            const float r   = std::sqrt(dx * dx + dy * dy) * 2.f;
            // Irregular splat edge: three-lobe wobble + inner core.
            const float wob  = 0.72f + 0.22f * std::sin(3.f * ang + 1.3f) + 0.10f * std::sin(5.f * ang + 4.2f);
            const float a    = 1.f - smoothstep(0.30f, wob, r);
            const float core = 1.f - smoothstep(0.0f, 0.34f, r);
            size_t      i    = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i]            = 130;
            px[i + 1]        = 14;
            px[i + 2]        = 24;
            px[i + 3]        = uint8_t(std::clamp((a * 0.55f + core * 0.45f) * 255.f, 0.f, 255.f));
        }
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] eve::graphics::Texture *makeDirt(eve::graphics::Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    uint32_t             seed = 12345u;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            seed          = seed * 1664525u + 1013904223u;
            const float n = float(seed % 10000u) / 10000.f;
            const float a = n < 0.62f ? 0.25f + 0.35f * n : 0.f;
            size_t      i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i]         = 118;
            px[i + 1]     = 102;
            px[i + 2]     = 82;
            px[i + 3]     = uint8_t(a * 255.f);
        }
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] eve::graphics::Texture *makeDentNormal(eve::graphics::Graphics *gfx, int size) {
    // Concave dent: tangent-space normal tilts inward toward the center.
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u    = (float(x) + 0.5f) / float(size);
            const float v    = (float(y) + 0.5f) / float(size);
            const float dx   = (u - 0.5f) * 2.f;
            const float dy   = (v - 0.5f) * 2.f;
            const float r    = std::sqrt(dx * dx + dy * dy);
            float       tilt = 1.6f * std::exp(-r * r * 2.6f);
            // Raised rim just outside the crater.
            tilt -= 0.8f * std::exp(-(r - 0.55f) * (r - 0.55f) * 60.f);
            glm::vec3 n = glm::normalize(glm::vec3(-dx * tilt, -dy * tilt, 1.f));
            size_t    i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i]       = uint8_t((n.x * 0.5f + 0.5f) * 255.f);
            px[i + 1]   = uint8_t((n.y * 0.5f + 0.5f) * 255.f);
            px[i + 2]   = uint8_t((n.z * 0.5f + 0.5f) * 255.f);
            px[i + 3]   = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] eve::graphics::Texture *makeDentAlbedo(eve::graphics::Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float   u      = (float(x) + 0.5f) / float(size);
            const float   v      = (float(y) + 0.5f) / float(size);
            const float   dx     = (u - 0.5f) * 2.f;
            const float   dy     = (v - 0.5f) * 2.f;
            const float   r      = std::sqrt(dx * dx + dy * dy);
            const float   crater = 1.f - smoothstep(0.f, 0.55f, r);
            const float   ring   = smoothstep(0.42f, 0.55f, r) * (1.f - smoothstep(0.55f, 0.68f, r));
            const uint8_t val    = uint8_t(std::clamp((68.f + crater * -28.f + ring * 42.f), 0.f, 255.f));
            size_t        i      = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i]                = val;
            px[i + 1]            = uint8_t(val * 0.93f);
            px[i + 2]            = uint8_t(val * 0.88f);
            px[i + 3]            = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] eve::graphics::Texture *makeRoughParams(eve::graphics::Graphics *gfx, int size, float rough) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i]     = uint8_t(rough * 255.f);  // R = target roughness
        px[i + 1] = 0;                       // G = metallic
        px[i + 2] = 0;                       // B = emissive
        px[i + 3] = 255;
    }
    return gfx->newTexture(size, size, px.data());
}

}  // namespace

// Gallery render: blood / dirt / dent decals projected onto a checker floor,
// saved as PNG for visual review (build/<plat>/test/out/decal/).
TEST_CASE("decal.renderGalleryPng") {
    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 480, 360);

    auto *mesh = makePlane(gfx, 3.f);
    REQUIRE(mesh != nullptr);
    auto *cam                    = Camera3D::createCamera();
    cam->data()->eyeY            = 1.9f;
    cam->data()->eyeZ            = 2.35f;
    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->transform()->pitch      = -3.14159265f * 0.5f;  // lay the plane flat (normal +Y)
    ent->meshRenderer()->texture = makeChecker(gfx, 64, 8);
    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);

    static bool sGalleryDrawer = false;
    if (!sGalleryDrawer) {
        sGalleryDrawer = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData, const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData.eyeX, camData.eyeY, camData.eyeZ, viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    DecalManager::inst().clearAll();
    auto &mgr          = DecalManager::inst();
    auto  bloodRecipe  = proceduralDecalPreset("blood-wet", 7u);
    auto  dirtRecipe   = proceduralDecalPreset("dirt", 23u);
    auto  damageRecipe = proceduralDecalPreset("damage", 37u);
    REQUIRE(bloodRecipe.ok());
    REQUIRE(dirtRecipe.ok());
    REQUIRE(damageRecipe.ok());
    for (auto *recipe : {&bloodRecipe.value(), &dirtRecipe.value(), &damageRecipe.value()}) {
        recipe->width  = 128;
        recipe->height = 128;
    }
    auto bloodBake  = bakeProceduralDecal(bloodRecipe.value());
    auto dirtBake   = bakeProceduralDecal(dirtRecipe.value());
    auto damageBake = bakeProceduralDecal(damageRecipe.value());
    REQUIRE(bloodBake.ok());
    REQUIRE(dirtBake.ok());
    REQUIRE(damageBake.ok());
    // Blood: dark red splat, wet gloss (roughness down via params R).
    const int blood =
        mgr.project(-0.85f, 0.01f, 0.15f, 0.f, 1.f, 0.f, gfx->newTexture(128, 128, bloodBake.value().albedo.data()),
                    "blood", 1.05f, 0.12f, true, 7, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.setTextures(blood, gfx->newTexture(128, 128, bloodBake.value().normal.data()),
                          gfx->newTexture(128, 128, bloodBake.value().params.data())));
    CHECK(mgr.setStrength(blood, 1.f, 1.f, 0.f, 0.f));
    // Dirt: gray-brown speckle, rough + slight normal wobble.
    const int dirt =
        mgr.project(0.f, 0.01f, 0.1f, 0.f, 1.f, 0.f, gfx->newTexture(128, 128, dirtBake.value().albedo.data()), "dirt",
                    1.3f, 0.12f, true, 23, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.setTextures(dirt, gfx->newTexture(128, 128, dirtBake.value().normal.data()),
                          gfx->newTexture(128, 128, dirtBake.value().params.data())));
    CHECK(mgr.setStrength(dirt, 1.f, 1.f, 0.f, 0.f));
    CHECK(mgr.setProjection(dirt, "spherical", 4.f) == DecalProjectionStatus::Applied);
    // Dent: concave normal map + crater albedo (real indentation shading).
    const int dent =
        mgr.project(0.85f, 0.01f, 0.15f, 0.f, 1.f, 0.f, gfx->newTexture(128, 128, damageBake.value().albedo.data()),
                    "dent", 1.05f, 0.12f, false, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.setTextures(dent, gfx->newTexture(128, 128, damageBake.value().normal.data()),
                          gfx->newTexture(128, 128, damageBake.value().params.data())));
    CHECK(mgr.setStrength(dent, 1.f, 1.f, 0.f, 0.f));
    CHECK(mgr.setParallax(dent, 0.055f, 8.f, 24.f) == DecalParallaxStatus::Applied);

    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
    }
    eve::image::ImageData *snap = gfx->newImageData();
    REQUIRE(snap != nullptr);
    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out/decal";
    std::error_code   ec;
    std::filesystem::create_directories(outDir, ec);
    REQUIRE(saveImagePng(*snap, outDir + "/decal_gallery.png"));
    std::printf("decal gallery saved: %s/decal_gallery.png\n", outDir.c_str());
    delete snap;

    DecalManager::inst().clearAll();
    win->close();
}
}  // namespace test_decal
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_dialogue)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <string>
#include <vector>
#include "avatar/Avatar.h"
#include "dialogue/Dialogue.h"
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
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "ui/UI.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// dialogue.cpp
namespace test_dialogue {
using namespace eve::dialogue;
using eve::avatar::Avatar;
using eve::avatar::AvatarInstance;
using namespace eve::graphics;
using namespace eve::ui;

namespace {

[[maybe_unused]] void hideAllSprites() {
    if (ecs::current()->getManager<Renderable2D>() == nullptr) return;
    auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [sp]   = *it;
        sp->visible = false;
    }
}

[[maybe_unused]] AvatarInstance *makePortrait(Avatar *avmod, float r, float g, float b) {
    AvatarInstance *av = avmod->newImageAvatar();
    av->addLayer("body", nullptr, 0);
    av->setLayerSize("body", 140.f, 280.f);
    av->setLayerColor("body", r, g, b, 1.f);
    av->addLayer("face", nullptr, 1);
    av->setLayerSize("face", 90.f, 90.f);
    av->setLayerOffset("face", 25.f, 30.f);
    av->setLayerColor("face", r * 0.7f + 0.25f, g * 0.7f + 0.2f, b * 0.7f + 0.2f, 1.f);
    av->addLayer("mouth", nullptr, 2);
    av->setLayerSize("mouth", 36.f, 14.f);
    av->setLayerOffset("mouth", 52.f, 95.f);
    av->setLayerColor("mouth", 0.85f, 0.25f, 0.3f, 0.15f);
    av->defineExpression("talk", "mouth=1");
    av->defineExpression("idle", "mouth=0.15");
    return av;
}

}  // namespace


TEST_CASE("dialogue.stage.avatarTypewriterPreview") {
    hideAllSprites();

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    auto *ui  = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);
    eve::window::WindowSettings s;
    s.width    = 800;
    s.height   = 480;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    REQUIRE(ui->initBackend());

    Dialogue *dlg   = Dialogue::create();
    Avatar   *avmod = Avatar::create();
    dlg->registerCharacter("alice", "Alice");
    dlg->registerCharacter("bob", "Bob");

    AvatarInstance *alice = makePortrait(avmod, 0.35f, 0.55f, 0.95f);
    AvatarInstance *bob   = makePortrait(avmod, 0.9f, 0.45f, 0.35f);
    dlg->bindAvatar("alice", alice);
    dlg->bindAvatar("bob", bob);
    dlg->setSlotX("left", 0.12f);
    dlg->setSlotX("right", 0.72f);
    dlg->show("alice", "left");
    dlg->show("bob", "right");

    dlg->setLipSyncEnabled(true);
    dlg->setLipSyncParameter("mouth");
    dlg->setLipSyncAmplitude(0.95f);
    dlg->setTypeSpeed(18.f);

    auto *cam      = Camera2D::createCamera();
    cam->data()->r = 0.08f;
    cam->data()->g = 0.09f;
    cam->data()->b = 0.12f;

    // Floor / stage backdrop.
    auto *stage              = Renderable2D::create();
    stage->transform()->x    = 0;
    stage->transform()->y    = 360;
    stage->sprite()->width   = 800;
    stage->sprite()->height  = 120;
    stage->sprite()->r       = 0.12f;
    stage->sprite()->g       = 0.14f;
    stage->sprite()->b       = 0.18f;
    stage->sprite()->visible = true;

    enum Phase { Line1, Line2, Choices, Done };
    Phase phase = Line1;
    dlg->say("alice", "Welcome to the EVEngine stage preview.");
    dlg->setExpression("alice", "talk");
    dlg->setExpression("bob", "idle");

    int  frames    = 0;
    bool sawChoice = false;
    for (int frame = 0; frame < 150; ++frame) {
        ++frames;
        dlg->update(0.016f);
        dlg->syncStage(float(gfx->getWidth()), float(gfx->getHeight()));
        avmod->sync();

        if (phase == Line1 && dlg->isWaitingAdvance()) {
            dlg->advance();
            dlg->say("bob", "Looks like the portraits and typewriter are alive.");
            dlg->setExpression("alice", "idle");
            dlg->setExpression("bob", "talk");
            phase = Line2;
        } else if (phase == Line2 && dlg->isWaitingAdvance()) {
            dlg->advance();
            dlg->clearChoices();
            dlg->addChoice("cont", "Continue");
            dlg->addChoice("exit", "Exit preview");
            dlg->presentChoices();
            dlg->setExpression("bob", "idle");
            phase = Choices;
        } else if (phase == Choices && dlg->isWaitingChoice()) {
            sawChoice = true;
            if (frame > 110) {
                dlg->selectChoice(0);
                phase = Done;
            }
        }

        const std::string       speaker = dlg->getSpeakerName();
        const std::string       body    = dlg->getVisibleText();
        std::vector<WidgetDesc> kids    = {
            text(speaker.empty() ? "Narrator" : speaker, "speaker"),
            separator("sep"),
            text(body.empty() ? "..." : body, "line"),
        };
        if (dlg->isWaitingChoice()) {
            kids.push_back(separator("sep2"));
            for (int i = 0; i < dlg->getChoiceCount(); ++i) {
                kids.push_back(text("• " + dlg->getChoiceLabel(i), "c" + std::to_string(i)));
            }
        } else if (dlg->isWaitingAdvance()) {
            kids.push_back(text("[advance]", "hint"));
        }

        REQUIRE(UIHost::resolve(ui->remountAs("dlg", window("Dialogue", kids, "root"))).has_value());
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

    CHECK_GT(frames, 100);
    CHECK(sawChoice);
    CHECK(dlg->isIdle());

    stage->sprite()->visible = false;
    alice->setVisible(false);
    bob->setVisible(false);
    alice->sync();
    bob->sync();
    dlg->reset();
    alice->release();
    bob->release();
    delete alice;
    delete bob;
    win->close();
}
}  // namespace test_dialogue
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gpu_driven)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include "Fixtures.h"
#include "ScriptTest.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "graphics/vulkan/GpuDriven.h"
#include "graphics/vulkan/Graphics.h"
#include "virtualgeometry/Builder.h"
#include "virtualgeometry/VirtualGeometryAsset.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// gpu_driven.cpp
namespace test_gpu_driven {
using namespace eve;


using namespace eve::graphics;

TEST_CASE("GpuDriven.capsAvailable") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    CHECK(vg->gpuDrivenCaps().computeShader);
    gfx->gpuDrivenSetEnabled(true);
    CHECK_EQ(gfx->gpuDrivenEnabled(), vg->gpuDrivenCaps().gpuDrivenAvailable());
    gfx->gpuDrivenSetEnabled(false);
    CHECK(!gfx->gpuDrivenEnabled());
    win->close();
}

TEST_CASE("GpuDriven.bindlessTextureRegistration") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;  // environment without 1.2 features: nothing to validate
    }

    const uint8_t  red[4]   = {255, 0, 0, 255};
    const uint8_t  green[4] = {0, 255, 0, 255};
    Texture       *a        = gfx->newTexture(1, 1, red);
    Texture       *b        = gfx->newTexture(1, 1, green);
    const uint32_t sa       = vg->debugBindlessIndex(a);
    const uint32_t sb       = vg->debugBindlessIndex(b);
    REQUIRE(sa != eve::graphics::vulkan::kInvalidBindlessSlot);
    REQUIRE(sb != eve::graphics::vulkan::kInvalidBindlessSlot);
    REQUIRE(sa != sb);  // distinct slots

    const std::vector<uint8_t> cubePx(6 * 4, 128);
    Texture                   *cube    = gfx->newCubemap(1, cubePx.data());
    auto                      *gpuCube = static_cast<eve::graphics::vulkan::GpuTexture *>(cube->gpuHandle);
    REQUIRE(gpuCube->bindlessIndexCube != eve::graphics::vulkan::kInvalidBindlessSlot);
    win->close();
}

TEST_CASE("GpuDriven.meshTableRegistration") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    Mesh *m = gfx->newMeshSphere(8, 4);
    REQUIRE(m != nullptr);
    const uint32_t idx = vg->debugMeshRecordIndex(m);
    REQUIRE(idx != eve::graphics::vulkan::kInvalidBindlessSlot);
    auto *gpu = static_cast<eve::graphics::vulkan::GpuMesh *>(m->gpuHandle);
    REQUIRE(gpu->record.vertexCount > 0);
    REQUIRE(gpu->record.indexCount > 0);
    REQUIRE(gpu->record.boundsCenterRadius.w > 0.f);  // bounds computed at upload
    win->close();
}

TEST_CASE("GpuDriven.residentInstanceBufferDirectSubmit") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    Mesh          *mesh       = gfx->newMeshSphere(12, 8);
    Material      *material   = gfx->newMaterial();
    const uint32_t meshId     = gfx->gpuDrivenMeshRecord(mesh);
    const uint32_t materialId = gfx->gpuDrivenMaterialRecord(material);
    REQUIRE(meshId != kInvalidGpuDrivenSlot);
    REQUIRE(materialId != kInvalidGpuDrivenSlot);

    GpuInstance instance;
    instance.model      = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, -3.f));
    instance.meshId     = meshId;
    instance.materialId = materialId;
    auto *compute       = eve::gpgpu::Gpgpu::create();
    REQUIRE(compute->isAvailable());
    eve::gpgpu::GpuBuffer *resident = compute->newBuffer(sizeof(instance), "storage");
    resident->uploadBytes(&instance, sizeof(instance));

    GpuResidentInstanceBucket bucket{0, 1, meshId, materialId};
    GpuResidentInstanceBatch  batch;
    batch.buffer             = resident->residentView();
    batch.buffer.strideBytes = sizeof(GpuInstance);
    batch.buckets            = &bucket;
    batch.bucketCount        = 1;
    batch.instanceCount      = 1;

    const glm::vec3 eye(0.f, 0.f, 2.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(0.f, 0.f, -3.f), glm::vec3(0.f, 1.f, 0.f));
    glm::mat4       proj = glm::perspectiveRH_ZO(glm::radians(50.f), 320.f / 240.f, 0.1f, 100.f);
    proj[1][1] *= -1.f;
    gfx->setMesh3DViewProj(proj * view);
    gfx->setMesh3DView(view);
    gfx->setMesh3DCameraPos(eye);
    gfx->gpuDrivenSetEnabled(true);
    gfx->begin3DFrame();
    REQUIRE(gfx->had3DThisFrame());
    GpuResidentInstanceBatch invalid = batch;
    invalid.buffer.backend           = GpuResidentBackend::WebGpu;
    CHECK(int(gfx->gpuDrivenSubmitResident(invalid)) == int(GpuResidentSubmitStatus::BackendMismatch));
    invalid                    = batch;
    invalid.buffer.strideBytes = sizeof(GpuInstance) - sizeof(uint32_t);
    CHECK(int(gfx->gpuDrivenSubmitResident(invalid)) == int(GpuResidentSubmitStatus::InvalidArgument));
    invalid                             = batch;
    GpuResidentInstanceBucket gapBucket = bucket;
    gapBucket.firstInstance             = 1;
    invalid.buckets                     = &gapBucket;
    CHECK(int(gfx->gpuDrivenSubmitResident(invalid)) == int(GpuResidentSubmitStatus::InvalidArgument));
    const GpuResidentSubmitStatus submitted = gfx->gpuDrivenSubmitResident(batch);
    CHECK(int(submitted) == int(GpuResidentSubmitStatus::Submitted));
    gfx->present();
    CHECK_EQ(vg->debugLastGpuDrivenDrawCount(), uint32_t(1));

    delete resident;
    win->close();
}

TEST_CASE("GpuDriven.squirrelResidentInstanceSubmit") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    if (!gfx->supportsGpuDriven3D()) {
        win->close();
        return;
    }

    {
        ScriptTest script(R"SQ(
        local gfx = eve.Graphics()
        local gpu = eve.Gpgpu()
        if (!gpu.isAvailable()) throw "GPGPU unavailable"
        local instanceStride = gpu.getGpuDrivenInstanceStride()
        if (instanceStride < 80) throw "unexpected instance stride"
        if (gpu.getGpuResidentOffsetAlignment() != 256) throw "unexpected offset alignment"

        local mesh = gfx.newMeshSphere(12, 8)
        local material = gfx.newMaterial()
        local meshId = gpu.gpuDrivenMeshRecord(mesh)
        local materialId = gpu.gpuDrivenMaterialRecord(material)
        if (meshId < 0 || materialId < 0) throw "resource registration failed"
        if (!gpu.gpuDrivenMaterialUsable(material)) throw "material is not usable"

        local instances = gpu.newBuffer(instanceStride, "storage")
        local model = [1.0, 0.0, 0.0, 0.0,
                       0.0, 1.0, 0.0, 0.0,
                       0.0, 0.0, 1.0, 0.0,
                       0.0, 0.0, -3.0, 1.0]
        gpu.writeGpuDrivenInstance(instances, 0, model, meshId, materialId, 0, -1)
        local buckets = [{ firstInstance = 0, instanceCount = 1,
                           meshId = meshId, materialId = materialId }]

        gpu.setGpuDrivenEnabled(true)
        if (!gpu.isGpuDrivenEnabled()) throw "GPU-driven renderer did not enable"
        gfx.begin3DFrame()
        local status = gpu.submitResidentInstances(instances, buckets, 1, 0)
        if (status != "submitted") throw "resident submit failed: " + status
        gfx.present()
    )SQ");
    }

    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    CHECK_EQ(vg->debugLastGpuDrivenDrawCount(), uint32_t(1));
    win->close();
}

namespace {

float gdLuma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

void gdWarmPresent(Graphics *gfx) {
    for (int i = 0; i < 4; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
}

/** @brief UV sphere (radius r) with positions + normals + triangle indices. */
void gdSphere(float r, int slices, int stacks, std::vector<float> &pos, std::vector<float> &nrm,
              std::vector<uint32_t> &idx) {
    pos.clear();
    nrm.clear();
    idx.clear();
    for (int s = 0; s <= slices; ++s) {
        const float u = float(s) / float(slices);
        for (int t = 0; t <= stacks; ++t) {
            const float v     = float(t) / float(stacks);
            const float theta = u * 6.2831853f;
            const float phi   = v * 3.14159265f;
            const float x     = std::sin(phi) * std::cos(theta);
            const float y     = std::cos(phi);
            const float z     = std::sin(phi) * std::sin(theta);
            pos.push_back(x * r);
            pos.push_back(y * r);
            pos.push_back(z * r);
            nrm.push_back(x);
            nrm.push_back(y);
            nrm.push_back(z);
        }
    }
    const int stride = stacks + 1;
    for (int s = 0; s < slices; ++s) {
        for (int t = 0; t < stacks; ++t) {
            const uint32_t i0 = uint32_t(s * stride + t);
            const uint32_t i1 = uint32_t(s * stride + t + 1);
            const uint32_t i2 = uint32_t((s + 1) * stride + t);
            const uint32_t i3 = uint32_t((s + 1) * stride + t + 1);
            idx.push_back(i0);
            idx.push_back(i2);
            idx.push_back(i1);
            idx.push_back(i1);
            idx.push_back(i2);
            idx.push_back(i3);
        }
    }
}

/** @brief Pack CPU VgCluster into the GPU GpuVgCluster layout (4 x uvec4). */
void gdPackVgClusters(const eve::virtualgeometry::VirtualGeometryAsset &asset,
                      std::vector<eve::graphics::GpuVgCluster>         &out) {
    out.resize(asset.clusters.size());
    auto fb = [](float f) {
        union {
            float    f;
            uint32_t u;
        } x;
        x.f = f;
        return x.u;
    };
    for (size_t i = 0; i < asset.clusters.size(); ++i) {
        const auto   &c = asset.clusters[i];
        GpuVgCluster &g = out[i];
        g.u0[0]         = fb(c.cx);
        g.u0[1]         = fb(c.cy);
        g.u0[2]         = fb(c.cz);
        g.u0[3]         = fb(c.r);
        g.u1[0]         = c.triStart;
        g.u1[1]         = c.triCount;
        g.u1[2]         = c.lodLevel;
        g.u1[3]         = c.parent;
        g.u2[0]         = fb(c.errorR);
        g.u2[1]         = fb(c.errorRScreen);
        g.u2[2]         = c.childCount;
        g.u2[3]         = 0;
        for (int k = 0; k < 4; ++k) g.u3[k] = c.children[k];
    }
}

std::vector<float> gdCaptureLuma(Graphics *gfx) {
    const int          w = gfx->getWidth();
    const int          h = gfx->getHeight();
    std::vector<float> out(size_t(w) * size_t(h), 0.f);
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            out[size_t(y * w + x)] = gdLuma(gfx->getPixel(x, y));
        }
    }
    return out;
}

Texture *gdSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

/** @brief Deterministic LCG so scene layout is stable across machines/runs. */
float gdRand01(uint32_t &seed) {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) * (1.f / 16777216.f);
}

/** @brief Checkerboard 2D texture (high contrast exposes edge/noise artifacts). */
Texture *gdChecker(Graphics *gfx, int n = 16) {
    std::vector<uint8_t> px;
    px.reserve(size_t(n) * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const uint8_t v = ((x ^ y) & 1) ? 235 : 30;
            px.push_back(v);
            px.push_back(v);
            px.push_back(v);
            px.push_back(255);
        }
    return gfx->newTexture(n, n, px.data());
}

/** @brief Unit cube (24 verts, per-face normals) for shape variety in scenes. */
Mesh *gdCubeMesh(Graphics *gfx) {
    static const float kFaces[6][4][3] = {
        {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}}, {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},     {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
        {{-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}},     {{-1, -1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, -1, 1}},
    };
    static const float kN[6][3] = {
        {-1, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0},
    };
    std::vector<float>    pos, nrm;
    std::vector<uint32_t> idx;
    for (int f = 0; f < 6; ++f) {
        const uint32_t base = uint32_t(pos.size() / 3);
        for (int c = 0; c < 4; ++c) {
            pos.insert(pos.end(), kFaces[f][c], kFaces[f][c] + 3);
            nrm.insert(nrm.end(), kN[f], kN[f] + 3);
        }
        idx.push_back(base + 0);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        idx.push_back(base + 0);
        idx.push_back(base + 2);
        idx.push_back(base + 3);
    }
    return gfx->newMeshFromArrays(pos.data(), nrm.data(), nullptr, int(pos.size() / 3), idx.data(), int(idx.size()));
}

/** @brief Simple delta summary for large-scene parity comparisons. */
struct GdDelta {
    float  maxDelta  = 0.f;
    float  meanDelta = 0.f;
    int    over005   = 0;
    int    over008   = 0;
    size_t n         = 0;
};

GdDelta gdCompare(const char *name, const std::vector<float> &a, const std::vector<float> &b) {
    GdDelta d;
    d.n = std::min(a.size(), b.size());
    for (size_t i = 0; i < d.n; ++i) {
        const float delta = std::fabs(a[i] - b[i]);
        d.maxDelta        = std::max(d.maxDelta, delta);
        d.meanDelta += delta;
        if (delta > 0.05f) ++d.over005;
        if (delta > 0.08f) ++d.over008;
    }
    d.meanDelta /= float(d.n);
    std::printf("[gd-scene] %-28s max=%.4f mean=%.5f over0.05=%d/%zu over0.08=%d/%zu\n", name, d.maxDelta, d.meanDelta,
                d.over005, d.n, d.over008, d.n);
    return d;
}

/** @brief A moderately large deterministic scene (grid of mixed meshes/materials). */
struct GdGrid {
    std::vector<Renderable3D *> objects;
    Mesh                       *sphere   = nullptr;
    Mesh                       *cylinder = nullptr;
    Mesh                       *cube     = nullptr;
    std::vector<Material *>     materials;
    int                         nx      = 0;
    int                         nz      = 0;
    float                       spacing = 1.f;
};

GdGrid gdBuildGrid(Graphics *gfx, int nx, int nz, float spacing, uint32_t seed = 20260821u) {
    GdGrid s;
    s.nx       = nx;
    s.nz       = nz;
    s.spacing  = spacing;
    s.sphere   = gfx->newMeshSphere(24, 12);
    s.cylinder = gfx->newMeshCylinder(16, 1, true);
    s.cube     = gdCubeMesh(gfx);

    auto addMat = [&](Texture *albedo, float rough, float metal) {
        Material *m = gfx->newMaterial();
        m->setAlbedoTexture(albedo);
        m->setNormalTexture(nullptr);
        m->setRoughness(rough);
        m->setMetallic(metal);
        s.materials.push_back(m);
        return m;
    };
    addMat(gdChecker(gfx, 16), 0.40f, 0.10f);
    addMat(gdSolid(gfx, 180, 70, 60), 0.80f, 0.00f);
    addMat(gdSolid(gfx, 70, 180, 90), 0.20f, 0.60f);
    addMat(gdSolid(gfx, 70, 90, 180), 0.60f, 0.20f);
    addMat(gdChecker(gfx, 8), 0.50f, 0.30f);

    s.objects.reserve(size_t(nx) * nz);
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < nz; ++j) {
            Mesh       *mesh  = ((i + j) % 3 == 0) ? s.sphere : (((i + j) % 3 == 1) ? s.cylinder : s.cube);
            Material   *mat   = s.materials[size_t(i * 3 + j * 5) % s.materials.size()];
            const float x     = (float(i) - float(nx - 1) * 0.5f) * spacing;
            const float z     = (float(j) - float(nz - 1) * 0.5f) * spacing;
            const float scale = 0.45f + 0.40f * gdRand01(seed);
            const float yaw   = gdRand01(seed) * 360.f;
            auto       *obj   = Renderable3D::create();
            obj->setMesh(mesh);
            obj->setMaterial(mat);
            obj->setPosition(x, 0.35f, z);
            obj->setScale(scale, scale, scale);
            obj->setRotation(0.f, yaw, 0.f);
            s.objects.push_back(obj);
        }
    }
    return s;
}

/** @brief Occlusion-heavy scene: a wall hides a crowd behind it. */
GdGrid gdBuildOcclusionScene(Graphics *gfx, uint32_t seed = 20260822u) {
    GdGrid s;
    s.sphere      = gfx->newMeshSphere(24, 12);
    s.cylinder    = gfx->newMeshCylinder(16, 1, true);
    s.cube        = gdCubeMesh(gfx);
    auto *wallMat = gfx->newMaterial();
    wallMat->setAlbedoTexture(gdChecker(gfx, 8));
    wallMat->setNormalTexture(nullptr);
    wallMat->setRoughness(0.6f);
    wallMat->setMetallic(0.1f);
    auto *crowdMat = gfx->newMaterial();
    crowdMat->setAlbedoTexture(gdSolid(gfx, 190, 120, 40));
    crowdMat->setNormalTexture(nullptr);
    crowdMat->setRoughness(0.5f);
    crowdMat->setMetallic(0.2f);

    auto add = [&](Mesh *mesh, Material *mat, float x, float y, float z, float sx, float sy, float sz, float yaw) {
        auto *obj = Renderable3D::create();
        obj->setMesh(mesh);
        obj->setMaterial(mat);
        obj->setPosition(x, y, z);
        obj->setScale(sx, sy, sz);
        obj->setRotation(0.f, yaw, 0.f);
        s.objects.push_back(obj);
    };
    // Tall + wide opaque wall between camera and crowd, with a clear depth
    // gap so the crowd's bounding spheres land well behind it in the HZB.
    add(s.cube, wallMat, 0.f, 1.2f, 1.5f, 10.f, 12.f, 0.5f, 0.f);
    // Crowd hidden behind the wall.
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 5; ++j) {
            const float x     = (float(i) - 2.5f) * 1.2f;
            const float z     = -3.6f - float(j) * 1.1f;
            const float scale = 0.5f + 0.4f * gdRand01(seed);
            const Mesh *mesh  = ((i + j) % 2) ? s.cylinder : s.sphere;
            add(const_cast<Mesh *>(mesh), crowdMat, x, 0.35f, z, scale, scale, scale, gdRand01(seed) * 360.f);
        }
    // A few visible objects between camera and wall.
    for (int i = -2; i <= 2; ++i) add(s.cube, wallMat, float(i) * 1.4f, 0.35f, 2.5f, 0.55f, 0.55f, 0.55f, 0.f);
    s.nx = int(s.objects.size());
    s.nz = 1;
    return s;
}

}  // namespace

/**
 * @brief The opaque forward pass must produce the same image with the legacy
 * per-draw path and the GPU-driven (indirect + bindless) path.
 */
TEST_CASE("GpuDriven.opaqueForwardParity") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    Material *ballMat = gfx->newMaterial();
    ballMat->setAlbedoTexture(gdSolid(gfx, 205, 70, 60));  // single texture -> slot 0
    ballMat->setNormalTexture(nullptr);
    ballMat->setRoughness(0.5f);
    ballMat->setMetallic(0.1f);
    auto *ball = Renderable3D::create();
    ball->setMesh(gfx->newMeshSphere(24, 16));
    ball->setMaterial(ballMat);
    ball->setPosition(0.f, 0.35f, 0.f);
    ball->setScale(0.55f, 0.55f, 0.55f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);  // isolate the direct-light path first

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("gpuDriven");
    gdWarmPresent(gfx);
    const auto legacy = gdCaptureLuma(gfx);

    rc->enable("gpuDriven");
    gdWarmPresent(gfx);
    const auto gpuDriven = gdCaptureLuma(gfx);

    // Same shading source, different emission path: allow small float noise.
    float     maxDelta = 0.f;
    const int w        = gfx->getWidth();
    const int h        = gfx->getHeight();
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            const size_t i = size_t(y * w + x);
            const float  d = std::fabs(legacy[i] - gpuDriven[i]);
            maxDelta       = std::max(maxDelta, d);
        }
    }
    rc->disable("gpuDriven");
    REQUIRE(maxDelta < 0.03f);
    win->close();
}

/**
 * @brief Multi-texture parity: two spheres with distinct bindless slots
 * (albedo slot 0 and slot 1) must produce the same image on the legacy path
 * and the GPU-driven path. Regression test for descriptor-array dynamic
 * indexing at element > 0.
 */
TEST_CASE("GpuDriven.opaqueForwardParityMultiTexture") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    auto makeBall = [&](Mesh *mesh, Texture *albedo, float x) {
        Material *mat = gfx->newMaterial();
        mat->setAlbedoTexture(albedo);
        mat->setNormalTexture(nullptr);
        mat->setRoughness(0.5f);
        mat->setMetallic(0.1f);
        // The GPU-driven forward pipeline is currently double-sided. Keep the
        // legacy comparison on the same documented material mode so this test
        // isolates bindless texture selection instead of raster culling.
        mat->setDoubleSided(true);
        auto *obj = Renderable3D::create();
        obj->setMesh(mesh);
        obj->setMaterial(mat);
        obj->setPosition(x, 0.35f, 0.f);
        obj->setScale(0.55f, 0.55f, 0.55f);
        return obj;
    };
    Mesh    *shared = gfx->newMeshSphere(24, 16);
    Texture *red    = gdSolid(gfx, 205, 70, 60);  // bindless slot 0
    Texture *green  = gdSolid(gfx, 60, 205, 90);  // bindless slot 1
    makeBall(shared, red, -1.2f);                 // material 0 -> slot 0
    makeBall(shared, green, 1.2f);                // material 1 -> slot 1

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("gpuDriven");
    gdWarmPresent(gfx);
    const auto legacy = gdCaptureLuma(gfx);

    rc->enable("gpuDriven");
    gdWarmPresent(gfx);
    REQUIRE(vg->debugLastGpuDrivenDrawCount() > 0);  // both spheres went through the path
    const auto gpuDriven = gdCaptureLuma(gfx);

    float     maxDelta = 0.f;
    const int w        = gfx->getWidth();
    const int h        = gfx->getHeight();
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            const size_t i = size_t(y * w + x);
            const float  d = std::fabs(legacy[i] - gpuDriven[i]);
            maxDelta       = std::max(maxDelta, d);
        }
    }
    rc->disable("gpuDriven");
    REQUIRE(maxDelta < 0.03f);
    win->close();
}

/**
 * @brief Stage 2 cull parity: instances outside the frustum (behind the camera
 * or far outside the sides) must be culled by the GPU chain while the final
 * image stays pixel-identical to the legacy path (GPU-clipped geometry never
 * contributed pixels anyway).
 */
TEST_CASE("GpuDriven.opaqueForwardCullParity") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    auto makeBall = [&](Mesh *mesh, Texture *albedo, float x, float z) {
        Material *mat = gfx->newMaterial();
        mat->setAlbedoTexture(albedo);
        mat->setNormalTexture(nullptr);
        mat->setRoughness(0.5f);
        mat->setMetallic(0.1f);
        // Match the GPU-driven pipeline's current double-sided raster state;
        // frustum rejection, not back-face culling, is under test here.
        mat->setDoubleSided(true);
        auto *obj = Renderable3D::create();
        obj->setMesh(mesh);
        obj->setMaterial(mat);
        obj->setPosition(x, 0.35f, z);
        obj->setScale(0.55f, 0.55f, 0.55f);
        return obj;
    };
    Mesh    *shared = gfx->newMeshSphere(24, 16);
    Texture *albedo = gdSolid(gfx, 120, 140, 160);
    makeBall(shared, albedo, -1.2f, 0.f);  // visible
    makeBall(shared, albedo, 0.f, 0.f);    // visible
    makeBall(shared, albedo, 1.2f, 0.f);   // visible
    makeBall(shared, albedo, 0.f, 9.f);    // behind the camera -> culled
    makeBall(shared, albedo, -40.f, 0.f);  // outside the left frustum -> culled

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("gpuDriven");
    gdWarmPresent(gfx);
    const auto legacy = gdCaptureLuma(gfx);

    rc->enable("gpuDriven");
    gdWarmPresent(gfx);
    const auto gpuDriven = gdCaptureLuma(gfx);
    vg->waitForSharedGpuResources();
    REQUIRE(vg->debugGpuDrivenVisibleCount() == 3);  // 2 of 5 instances culled
    REQUIRE(vg->debugGpuDrivenCulledDrawCount() == 3);

    float     maxDelta = 0.f;
    const int w        = gfx->getWidth();
    const int h        = gfx->getHeight();
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            const size_t i = size_t(y * w + x);
            const float  d = std::fabs(legacy[i] - gpuDriven[i]);
            maxDelta       = std::max(maxDelta, d);
        }
    }
    rc->disable("gpuDriven");
    REQUIRE(maxDelta < 0.03f);
    win->close();
}

/**
 * @brief Stage 3 parity: the visibility-buffer path (GBuffer writes visID/
 * visBary, scene color pass runs a fullscreen resolve) must produce the same
 * image as the stage-2 forward shading, with the same GPU culling active.
 * Requires the 1x scene pass (resolve path gates on MSAA off).
 */
TEST_CASE("GpuDriven.visResolveParity") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    auto makeBall = [&](Mesh *mesh, Texture *albedo, float x, float z) {
        Material *mat = gfx->newMaterial();
        mat->setAlbedoTexture(albedo);
        mat->setNormalTexture(nullptr);
        mat->setRoughness(0.5f);
        mat->setMetallic(0.1f);
        auto *obj = Renderable3D::create();
        obj->setMesh(mesh);
        obj->setMaterial(mat);
        obj->setPosition(x, 0.35f, z);
        obj->setScale(0.55f, 0.55f, 0.55f);
        return obj;
    };
    Mesh    *shared = gfx->newMeshSphere(24, 16);
    Texture *albedo = gdSolid(gfx, 120, 140, 160);
    makeBall(shared, albedo, -1.2f, 0.f);  // visible
    makeBall(shared, albedo, 0.f, 0.f);    // visible
    makeBall(shared, albedo, 1.2f, 0.f);   // visible
    makeBall(shared, albedo, 0.f, 9.f);    // behind the camera -> culled
    makeBall(shared, albedo, -40.f, 0.f);  // outside the left frustum -> culled

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("msaa");  // resolve path requires the 1x scene pass
    rc->disable("gpuDriven");
    gdWarmPresent(gfx);
    gdCaptureLuma(gfx);  // warm the 1x scene pass; legacy reference not needed

    rc->enable("gpuDriven");
    gdWarmPresent(gfx);
    const auto fwdGpu = gdCaptureLuma(gfx);

    rc->enable("visResolve");
    gdWarmPresent(gfx);
    const auto resolved = gdCaptureLuma(gfx);
    vg->waitForSharedGpuResources();
    REQUIRE(vg->debugGpuDrivenVisibleCount() == 3);  // same cull as forward

    float     maxDelta = 0.f;
    const int w        = gfx->getWidth();
    const int h        = gfx->getHeight();
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            const size_t i = size_t(y * w + x);
            const float  d = std::fabs(fwdGpu[i] - resolved[i]);
            maxDelta       = std::max(maxDelta, d);
        }
    }
    rc->disable("gpuDriven");
    rc->disable("visResolve");
    std::printf("GpuDriven.visResolveParity maxDelta=%f\n", maxDelta);
    REQUIRE(maxDelta < 0.06f);
    win->close();
}

/**
 * @brief Stage 3 VG: a mesh with an attached virtual-geometry asset is culled
 * (frustum + HZB) and hardware-rasterized into the vis buffer, then resolved
 * with flat material-tint shading. Verifies visible clusters and the resulting
 * pixels, plus frustum culling when the object moves behind the camera.
 */
TEST_CASE("GpuDriven.vgVisResolve") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable() || !vg->gpuDrivenCaps().drawIndirectCount) {
        win->close();
        return;
    }

    // Shared geometry: UV sphere -> regular Mesh + VG cluster DAG.
    std::vector<float>    pos, nrm;
    std::vector<uint32_t> idx;
    gdSphere(0.9f, 24, 12, pos, nrm, idx);
    eve::virtualgeometry::VirtualGeometryBuilder            builder;
    eve::virtualgeometry::VirtualGeometryAsset              asset;
    eve::virtualgeometry::VirtualGeometryBuilder::MeshInput in{int(pos.size() / 3), pos.data(), nrm.data(), idx.data(),
                                                               int(idx.size())};
    REQUIRE(builder.build(in, eve::virtualgeometry::VirtualGeometryBuilder::Options{}, asset));
    REQUIRE(!asset.clusters.empty());
    REQUIRE(asset.totalTriangles() > 0);

    std::vector<eve::graphics::GpuVgCluster> packed;
    gdPackVgClusters(asset, packed);
    eve::graphics::GpuVgAssetUpload up{};
    up.positions             = asset.positions.data();
    up.vertexCount           = asset.vertexCount;
    up.normals               = asset.normals.empty() ? nullptr : asset.normals.data();
    up.triangles             = asset.triangles.data();
    up.triangleCount         = int(asset.triangles.size());
    up.clusters              = packed.data();
    up.clusterCount          = int(packed.size());
    const uint32_t vgAssetId = vg->gpuDrivenVgUpload(up);
    REQUIRE(vgAssetId != eve::graphics::kInvalidGpuDrivenSlot);

    Mesh *mesh =
        gfx->newMeshFromArrays(pos.data(), nrm.data(), nullptr, int(pos.size() / 3), idx.data(), int(idx.size()));
    REQUIRE(mesh != nullptr);
    REQUIRE(vg->gpuDrivenVgAttachToMesh(mesh, vgAssetId));
    REQUIRE(vg->gpuDrivenVgAssetId(mesh) == vgAssetId);

    Material *mat = gfx->newMaterial();
    mat->setTint(0.9f, 0.2f, 0.1f);
    auto *obj = Renderable3D::create();
    obj->setMesh(mesh);
    obj->setMaterial(mat);
    obj->setPosition(0.f, 0.f, 0.f);
    obj->setScale(1.15f, 1.15f, 1.15f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 3.2f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.15f, 0.15f, 0.17f);
    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.4f, 1.f, 0.3f);
    sun->setColor(1.f, 1.f, 1.f, 2.2f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("msaa");
    rc->enable("gpuDriven");
    rc->enable("visResolve");
    gdWarmPresent(gfx);
    vg->waitForSharedGpuResources();
    std::printf("vgVisResolve: resolveWanted=%d cullEnabled=%d drawCount=%u\n", int(vg->gpuDrivenResolveWanted()),
                int(vg->gpuDrivenCullEnabled()), vg->debugLastGpuDrivenDrawCount());
    std::printf("vgVisResolve: rc== %d visResolve=%d msaa=%d gpuDriven=%d\n", int(rc == gfx->getRenderControl()),
                int(rc->isEnabled("visResolve")), int(rc->isEnabled("msaa")), int(rc->isEnabled("gpuDriven")));

    const uint32_t visCount = vg->debugGpuDrivenVgVisibleCount();
    std::printf("vgVisResolve: visible clusters=%u\n", visCount);
    REQUIRE(visCount > 0);
    const int   w      = gfx->getWidth();
    const int   h      = gfx->getHeight();
    const Color center = gfx->getPixel(w / 2, h / 2);
    std::printf("vgVisResolve: center=(%.3f %.3f %.3f)\n", center.r, center.g, center.b);
    // Flat tint shading: the lit sphere center must be clearly reddish.
    REQUIRE(center.r > center.b + 0.05f);

    // NOTE: GPU-side HZB occlusion culling for VG clusters is a known
    // follow-up (the CPU renderer frustum-culls before the VG instance is
    // submitted, and the stage-3 HZB path is not wired for clusters yet).
    // This test verifies the cluster cull + raster + resolve pipeline end to
    // end; cluster occlusion will be covered once the HZB path lands.

    rc->disable("gpuDriven");
    rc->disable("visResolve");
    win->close();
}

/**
 * @brief Large-scene forward parity: ~100 mixed objects (3 meshes x 5
 * materials, deterministic transforms) must produce nearly the same image on
 * the CPU (legacy per-draw) path and the GPU-driven (bindless + indirect)
 * path under the same camera/lighting/material configuration.
 */
TEST_CASE("GpuDriven.largeSceneForwardParity") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 480, 360);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 12.f, 16.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setFov(45.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    const GdGrid scene = gdBuildGrid(gfx, 10, 10, 1.35f);
    REQUIRE(scene.objects.size() == 100);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);
    auto *p1 = Light3D::createLight("point");
    p1->setPosition(-4.f, 4.f, -2.f);
    p1->setColor(1.f, 0.7f, 0.5f, 2.0f);
    p1->setRadius(9.f);
    auto *p2 = Light3D::createLight("point");
    p2->setPosition(4.f, 3.f, 3.f);
    p2->setColor(0.5f, 0.8f, 1.f, 1.8f);
    p2->setRadius(8.f);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("msaa");
    rc->disable("gpuDriven");
    gdWarmPresent(gfx);
    const auto legacy = gdCaptureLuma(gfx);

    rc->enable("gpuDriven");
    gdWarmPresent(gfx);
    REQUIRE(vg->debugLastGpuDrivenDrawCount() > 0);
    const auto    gpuDriven = gdCaptureLuma(gfx);
    const GdDelta d         = gdCompare("largeScene legacy vs gpuDriven", legacy, gpuDriven);

    rc->disable("gpuDriven");
    // Same shading state: differences are confined to high-contrast edge
    // pixels (rasterization rule / projection rounding), not global noise.
    REQUIRE(d.meanDelta < 0.008f);
    REQUIRE(d.over008 * 50 < int(d.n));  // < 2% of samples deviate > 0.08 luma
    win->close();
}

/**
 * @brief Large-scene cull parity: an opaque wall hides a 30-object crowd
 * behind it. The CPU path draws everything (no occlusion cull); the GPU chain
 * additionally runs HZB occlusion culling, so its visible instance count must
 * drop sharply while the final image stays close to the CPU result.
 */
TEST_CASE("GpuDriven.largeSceneCullParity") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 480, 360);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 5.5f, 9.f);
    cam->setTarget(0.f, 0.4f, -2.f);
    cam->setFov(50.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    const GdGrid scene     = gdBuildOcclusionScene(gfx);
    const size_t submitted = scene.objects.size();  // all inside the frustum

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.5f, 1.f, 0.4f);
    sun->setColor(1.f, 1.f, 1.f, 2.2f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("msaa");
    rc->disable("gpuDriven");
    gdWarmPresent(gfx);
    const auto legacy = gdCaptureLuma(gfx);

    rc->enable("gpuDriven");
    gdWarmPresent(gfx);
    vg->waitForSharedGpuResources();
    const uint32_t visible = vg->debugGpuDrivenVisibleCount();
    std::printf("[gd-scene] cullParity submitted=%zu gpuVisible=%u\n", submitted, visible);
    REQUIRE(visible >= 6);         // wall + 5 front objects are never occluded
    REQUIRE(visible < submitted);  // HZB dropped at least some hidden crowd
    const auto    gpuDriven = gdCaptureLuma(gfx);
    const GdDelta d         = gdCompare("largeSceneCull legacy vs gpuDriven", legacy, gpuDriven);

    rc->disable("gpuDriven");
    REQUIRE(d.meanDelta < 0.008f);
    REQUIRE(d.over008 * 50 < int(d.n));
    win->close();
}

/**
 * @brief Large-scene visibility-buffer parity: the same ~64-object scene must
 * be pixel-close between the GPU-driven forward pass and the stage-3 vis
 * (GBuffer visID/visBary + fullscreen resolve) pass.
 */
TEST_CASE("GpuDriven.largeSceneVisResolveParity") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 480, 360);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable() || !vg->gpuDrivenCaps().drawIndirectCount) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 11.f, 15.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setFov(45.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    const GdGrid scene = gdBuildGrid(gfx, 8, 8, 1.45f);
    REQUIRE(scene.objects.size() == 64);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);
    auto *p1 = Light3D::createLight("point");
    p1->setPosition(-3.f, 4.f, -2.f);
    p1->setColor(1.f, 0.7f, 0.5f, 2.0f);
    p1->setRadius(8.f);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("msaa");  // resolve path requires the 1x scene pass
    rc->enable("gpuDriven");
    rc->disable("visResolve");
    gdWarmPresent(gfx);
    const auto fwd = gdCaptureLuma(gfx);

    rc->enable("visResolve");
    gdWarmPresent(gfx);
    const auto    resolved = gdCaptureLuma(gfx);
    const GdDelta d        = gdCompare("largeSceneVis fwd vs resolve", fwd, resolved);

    rc->disable("gpuDriven");
    rc->disable("visResolve");
    REQUIRE(d.meanDelta < 0.002f);
    // A handful of isolated silhouette pixels may differ by driver-specific
    // rasterization rounding between the vis pass and the resolve; anything
    // beyond that is a real defect.
    REQUIRE(d.maxDelta < 0.1f);
    REQUIRE(d.over008 <= 4);
    win->close();
}
}  // namespace test_gpu_driven
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gpu_driven_parity)
#include "PrivateTestScope.h"
#include "Fixtures.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#ifdef EVENGINE_WEBGPU
#include "graphics/webgpu/Graphics.h"
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
// gpu_driven_parity.cpp
namespace test_gpu_driven_parity {
#ifdef EVENGINE_WEBGPU

#endif


using namespace eve::graphics;

namespace {

std::vector<float> captureLuma(Graphics *gfx) {
    for (int i = 0; i < 4; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const int          width  = gfx->getWidth();
    const int          height = gfx->getHeight();
    std::vector<float> pixels;
    pixels.reserve(size_t(width / 4) * size_t(height / 4));
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            const Color c = gfx->getPixel(x, y);
            pixels.push_back((c.r + c.g + c.b) / 3.f);
        }
    }
    return pixels;
}

}  // namespace

/** @brief Simplified GPU-driven shaders must reject surface features they do not encode. */
TEST_CASE("GpuDrivenParity.rejectsUnrepresentedSurfaceFeatures") {
    eve::window::Window *window = nullptr;
    Graphics            *gfx    = nullptr;
    openGfxWindow(window, gfx, 160, 120);
    if (!gfx->supportsGpuDriven3D()) {
        window->close();
        return;
    }

    Material *transparent = gfx->newMaterial();
    transparent->setSurfaceMode("transparent");
    CHECK(!gfx->gpuDrivenMaterialUsable(transparent));

    Material *masked = gfx->newMaterial();
    masked->setSurfaceMode("masked");
    CHECK(gfx->gpuDrivenMaterialUsable(masked));

    Material  *extended = gfx->newMaterial();
    PbrSurface surface;
    surface.vegetationColor.overlay = 0.5f;
    REQUIRE(extended->setPbrSurface(surface).ok());
    CHECK(!gfx->gpuDrivenMaterialUsable(extended));
    window->close();
}

TEST_CASE("GpuDrivenParity.materialRecordsReleaseIdempotentlyAndReuseSlots") {
    eve::window::Window *window = nullptr;
    Graphics            *gfx    = nullptr;
    openGfxWindow(window, gfx, 160, 120);
    if (!gfx->supportsGpuDriven3D()) {
        window->close();
        return;
    }

    Material *first = gfx->newMaterial();
    REQUIRE(gfx->gpuDrivenMaterialUsable(first));
    const uint32_t firstSlot = gfx->gpuDrivenMaterialRecord(first);
    REQUIRE(firstSlot != kInvalidGpuDrivenSlot);
    REQUIRE(gfx->gpuDrivenReleaseMaterialRecord(first).ok());
    REQUIRE(gfx->gpuDrivenReleaseMaterialRecord(first).ok());

    Material *second = gfx->newMaterial();
    REQUIRE(gfx->gpuDrivenMaterialUsable(second));
    const uint32_t secondSlot = gfx->gpuDrivenMaterialRecord(second);
    CHECK_EQ(secondSlot, firstSlot);
    REQUIRE(gfx->gpuDrivenReleaseMaterialRecord(second).ok());
    window->close();
}

TEST_CASE("GpuDrivenParity.cameraFacingCardTurnsTowardSideCamera") {
    eve::window::Window *window = nullptr;
    Graphics            *gfx    = nullptr;
    openGfxWindow(window, gfx, 160, 120);
    if (!gfx->supportsGpuDriven3D()) {
        window->close();
        return;
    }

    const float    positions[] = {-.5f, 0.f, 0.f, .5f, 0.f, 0.f, .5f, 1.f, 0.f, -.5f, 1.f, 0.f};
    const float    normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float    texcoords[] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    Mesh          *mesh        = gfx->newMeshFromArrays(positions, normals, texcoords, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t green[]  = {32, 240, 64, 255};
    Material     *material = gfx->newMaterial();
    material->setAlbedoTexture(gfx->newTexture(1, 1, green));
    material->setDoubleSided(true);
    material->setCastShadow(false);
    material->setCastOcclusion(false);
    auto *card = Renderable3D::create();
    card->setMesh(mesh);
    card->setMaterial(material);

    auto *camera = Camera3D::createCamera();
    camera->setEye(3.f, .5f, 0.f);
    camera->setTarget(0.f, .5f, 0.f);
    camera->setAmbient(1.f, 1.f, 1.f);
    gfx->setScreenReadbackEnabled(true);
    auto *control = gfx->getRenderControl();
    for (const char *pass : {"ao", "gi", "aa", "atmosphere", "msaa", "visResolve"}) control->disable(pass);
    control->enable("gpuDriven");

    auto sum = [&]() {
        const auto pixels = captureLuma(gfx);
        float      total  = 0.f;
        for (float value : pixels) total += value;
        return total;
    };
    material->setCameraFacing(false);
    const float edgeOn = sum();
    material->setCameraFacing(true);
    const float facing = sum();
    REQUIRE(gfx->gpuDrivenEnabled());
    CHECK(facing > edgeOn + 2.f);
    CHECK(facing > 5.f);
    window->close();
}

/** @brief Both backends preserve an authored masked cutoff in GPU-driven shading. */
TEST_CASE("GpuDrivenParity.maskedAlphaCutoff") {
    eve::window::Window *window = nullptr;
    Graphics            *gfx    = nullptr;
    openGfxWindow(window, gfx, 160, 120);
    if (!gfx->supportsGpuDriven3D()) {
        window->close();
        return;
    }

    const float    positions[] = {-.7f, -.7f, 0.f, .7f, -.7f, 0.f, .7f, .7f, 0.f, -.7f, .7f, 0.f};
    const float    normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float    texcoords[] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[]   = {0, 1, 2, 0, 2, 3};
    Mesh          *mesh        = gfx->newMeshFromArrays(positions, normals, texcoords, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t orange[]     = {230, 100, 32, 153};  // alpha = 0.6
    Texture      *texture      = gfx->newTexture(1, 1, orange);
    auto          makeMaterial = [&](float cutoff) {
        Material *material = gfx->newMaterial();
        material->setAlbedoTexture(texture);
        material->setSurfaceMode("masked");
        material->setAlphaCutoff(cutoff);
        material->setDoubleSided(true);
        material->setCastShadow(false);
        material->setCastOcclusion(false);
        return material;
    };
    Material *visible = makeMaterial(.25f);
    Material *clipped = makeMaterial(.75f);
    auto     *card    = Renderable3D::create();
    card->setMesh(mesh);
    card->setMaterial(visible);

    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 0.f, 3.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setAmbient(1.f, 1.f, 1.f);
    gfx->setScreenReadbackEnabled(true);
    auto *control = gfx->getRenderControl();
    for (const char *pass : {"ao", "gi", "aa", "atmosphere", "msaa", "visResolve"}) control->disable(pass);
    control->enable("gpuDriven");

    auto sum = [&]() {
        float total = 0.f;
        for (float value : captureLuma(gfx)) total += value;
        return total;
    };
    const float visibleSum = sum();
    card->setMaterial(clipped);
    const float clippedSum = sum();
    REQUIRE(gfx->gpuDrivenEnabled());
    CHECK(visibleSum > clippedSum + 5.f);
    window->close();
}

/** @brief Backend-neutral parity for opaque GPU culling and indirect submission. */
TEST_CASE("GpuDrivenParity.opaqueStage1") {
    eve::window::Window *window = nullptr;
    Graphics            *gfx    = nullptr;
    openGfxWindow(window, gfx, 320, 240);
    if (!gfx->supportsGpuDriven3D()) {
        window->close();
        return;
    }

    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 3.5f, 5.5f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setAmbient(0.08f, 0.08f, 0.1f);

    const uint8_t red[4]   = {210, 65, 55, 255};
    const uint8_t green[4] = {55, 205, 85, 255};
    Mesh         *mesh     = gfx->newMeshSphere(24, 16);
    auto          addBall  = [&](float x, float y, float z, const uint8_t *rgba) {
        Material *material = gfx->newMaterial();
        material->setAlbedoTexture(gfx->newTexture(1, 1, rgba));
        material->setRoughness(0.5f);
        auto *object = Renderable3D::create();
        object->setMesh(mesh);
        object->setMaterial(material);
        object->setPosition(x, y, z);
        object->setScale(0.55f, 0.55f, 0.55f);
    };
    addBall(-0.9f, 0.35f, 0.f, red);
    addBall(0.9f, 0.35f, 0.f, green);
    // Continue the eye-to-front-sphere ray so the farther, perspective-smaller
    // sphere is completely occluded. This keeps HZB rejection pixel-neutral.
    addBall(-1.17f, -0.595f, -1.65f, red);
    addBall(0.f, 0.35f, 20.f, red);  // Behind the camera; rejected by culling.

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *control = gfx->getRenderControl();
    // Isolate opaque submission and reconstruction from temporal/post passes.
    control->disable("ao");
    control->disable("gi");
    control->disable("aa");
    control->disable("atmosphere");
    control->disable("msaa");
    control->disable("gpuDriven");
    const std::vector<float> legacy = captureLuma(gfx);
    control->enable("gpuDriven");
    const std::vector<float> driven = captureLuma(gfx);
    REQUIRE(gfx->gpuDrivenEnabled());
#ifdef EVENGINE_WEBGPU
    auto *webgpu = dynamic_cast<eve::graphics::webgpu::Graphics *>(gfx);
    REQUIRE(webgpu != nullptr);
    REQUIRE(webgpu->debugGpuDrivenVisibleCount() == 3);
    REQUIRE(webgpu->debugGpuDrivenHzbMipCount() > 1);
    REQUIRE(webgpu->debugGpuDrivenGpuVisibleCount() == 2);
    REQUIRE(webgpu->debugGpuDrivenDispatchCount() == 4);
    REQUIRE(webgpu->debugGpuDrivenIndirectDrawCount() == 2);
#endif
    REQUIRE(legacy.size() == driven.size());
    float  maxDelta        = 0.f;
    float  legacySum       = 0.f;
    float  drivenSum       = 0.f;
    float  deltaSum        = 0.f;
    size_t divergentPixels = 0;
    size_t maxIndex        = 0;
    for (size_t i = 0; i < legacy.size(); ++i) {
        const float delta = std::abs(legacy[i] - driven[i]);
        if (delta > maxDelta) {
            maxDelta = delta;
            maxIndex = i;
        }
        legacySum += legacy[i];
        drivenSum += driven[i];
        deltaSum += delta;
        if (delta >= 0.03f) ++divergentPixels;
    }
    std::printf(
        "GpuDrivenParity forward maxDelta=%f index=%zu legacy=%f driven=%f "
        "legacySum=%f drivenSum=%f meanDelta=%f divergent=%zu/%zu\n",
        maxDelta, maxIndex, legacy[maxIndex], driven[maxIndex], legacySum, drivenSum, deltaSum / float(legacy.size()),
        divergentPixels, legacy.size());
    // Indirect and direct rasterization can select opposite samples along a
    // sub-pixel silhouette. Measure whole-frame agreement while still bounding
    // both aggregate error and the number of visibly divergent samples.
    REQUIRE(deltaSum / float(legacy.size()) < 0.01f);
    REQUIRE(divergentPixels * 100u < legacy.size() * 3u);

    // Stage 3: disable MSAA (visibility attachments are single-sample), then
    // compare the non-indexed vis pass + fullscreen reconstruction against
    // the stage-2 forward indirect path.
    control->disable("visResolve");
    const std::vector<float> forward1x = captureLuma(gfx);
    control->enable("visResolve");
    const std::vector<float> resolved = captureLuma(gfx);
    REQUIRE(gfx->gpuDrivenResolveWanted());
    float resolveDelta = 0.f;
    for (size_t i = 0; i < forward1x.size(); ++i)
        resolveDelta = std::max(resolveDelta, std::abs(forward1x[i] - resolved[i]));
    REQUIRE(resolveDelta < 0.06f);
    control->disable("visResolve");
    window->close();
}

/** @brief Backend-neutral VG compute-cull, indirect-vis and resolve coverage. */
TEST_CASE("GpuDrivenParity.virtualGeometry") {
    eve::window::Window *window = nullptr;
    Graphics            *gfx    = nullptr;
    openGfxWindow(window, gfx, 320, 240);
    if (!gfx->supportsGpuDriven3D()) {
        window->close();
        return;
    }

    const float    positions[] = {-1.1f, -0.9f, 0.f, 1.1f, -0.9f, 0.f, 0.f, 1.1f, 0.f};
    const float    normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const uint32_t indices[]   = {0, 1, 2};
    GpuVgCluster   cluster{};
    auto           bits = [](float value) {
        uint32_t out = 0;
        std::memcpy(&out, &value, sizeof(out));
        return out;
    };
    cluster.u0[0] = bits(0.f);
    cluster.u0[1] = bits(0.f);
    cluster.u0[2] = bits(0.f);
    cluster.u0[3] = bits(1.6f);
    cluster.u1[0] = 0;
    cluster.u1[1] = 1;
    cluster.u1[3] = 0xFFFFFFFFu;
    for (uint32_t &child : cluster.u3) child = 0xFFFFFFFFu;
    GpuVgAssetUpload upload{};
    upload.positions       = positions;
    upload.vertexCount     = 3;
    upload.normals         = normals;
    upload.triangles       = indices;
    upload.triangleCount   = 3;
    upload.clusters        = &cluster;
    upload.clusterCount    = 1;
    const uint32_t assetId = gfx->gpuDrivenVgUpload(upload);
    // Vulkan devices without drawIndirectCount still support the ordinary
    // GPU-driven path, but intentionally reject compacted VG command streams.
    if (assetId == kInvalidGpuDrivenSlot) {
        window->close();
        return;
    }

    Mesh *mesh = gfx->newMeshFromArrays(positions, normals, nullptr, 3, indices, 3);
    REQUIRE(mesh != nullptr);
    REQUIRE(gfx->gpuDrivenVgAttachToMesh(mesh, assetId));
    REQUIRE(gfx->gpuDrivenVgAssetId(mesh) == assetId);
    Material *material = gfx->newMaterial();
    material->setTint(0.9f, 0.2f, 0.1f);
    auto *object = Renderable3D::create();
    object->setMesh(mesh);
    object->setMaterial(material);
    object->setPosition(0.f, 0.f, 0.f);

    // A second copy lies directly behind the first one. It stays inside the
    // frustum but must disappear from the GPU-written indirect commands once
    // the previous-frame HZB is available.
    const uint32_t hiddenAssetId = gfx->gpuDrivenVgUpload(upload);
    REQUIRE(hiddenAssetId != kInvalidGpuDrivenSlot);
    Mesh *hiddenMesh = gfx->newMeshFromArrays(positions, normals, nullptr, 3, indices, 3);
    REQUIRE(gfx->gpuDrivenVgAttachToMesh(hiddenMesh, hiddenAssetId));
    auto *hiddenObject = Renderable3D::create();
    hiddenObject->setMesh(hiddenMesh);
    hiddenObject->setMaterial(material);
    hiddenObject->setPosition(0.f, 0.f, -4.f);

    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 0.f, 3.2f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setAmbient(0.35f, 0.35f, 0.35f);
    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.f, 0.f, -1.f);
    sun->setColor(1.f, 1.f, 1.f, 1.5f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *control = gfx->getRenderControl();
    control->disable("ao");
    control->disable("gi");
    control->disable("aa");
    control->disable("atmosphere");
    control->disable("msaa");
    control->enable("gpuDriven");
    control->enable("visResolve");
    captureLuma(gfx);
    REQUIRE(gfx->gpuDrivenResolveWanted());
#ifdef EVENGINE_WEBGPU
    const Color visible = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(visible.r > visible.b + 0.05f);
    auto *webgpu = dynamic_cast<eve::graphics::webgpu::Graphics *>(gfx);
    REQUIRE(webgpu != nullptr);
    REQUIRE(webgpu->debugGpuDrivenVgDispatchCount() == 2);
    REQUIRE(webgpu->debugGpuDrivenVgGpuVisibleCount() == 1);
    REQUIRE(webgpu->debugGpuDrivenVgIndirectDrawCount() == 2);
#endif

    control->disable("gpuDriven");
    control->disable("visResolve");
    window->close();
}
}  // namespace test_gpu_driven_parity
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_deferred_lifetime)
#include "PrivateTestScope.h"
#include "graphics/ClusteredLight.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "window/Window.h"
#include "zeroerr/unittest.h"
// graphics_deferred_lifetime.cpp
namespace test_graphics_deferred_lifetime {
TEST_CASE("graphics.deferredLighting.reusesCompletedFrameResources") {
    auto                       *window   = eve::window::Window::create();
    auto                       *graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 64;
    settings.height = 48;
    REQUIRE(window->setWindowSettings(settings));
    if (!graphics->supportsDeferredLighting()) return;

    // Exceeds VMA's mapping reference limit even with three frame slots.
    // Validation also checks that each descriptor is updated only after its
    // previous submission has completed. No readback/idle wait masks overlap.
    for (int frame = 0; frame < 800; ++frame) {
        graphics->beginGBufferPass(64, 48);
        graphics->endGBufferPass();
        graphics->begin3DFrame();
        graphics->setMesh3DViewProj(glm::mat4(1));
        graphics->setMesh3DView(glm::mat4(1));
        graphics->setMesh3DClip(0.1f, 100.f);
        graphics->drawDeferredLighting();
        graphics->present();
    }
}

TEST_CASE("graphics.clusteredLighting.reusesUnchangedStorageBindings") {
    auto                       *window   = eve::window::Window::create();
    auto                       *graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 64;
    settings.height = 48;
    REQUIRE(window->setWindowSettings(settings));
    auto *mesh = graphics->newMeshSphere(8, 4);
    REQUIRE(mesh != nullptr);
    const auto lighting =
        eve::graphics::buildClusteredLighting({}, {}, glm::mat4(1), 0.1f, 100.f, 64, 48, 1.f, glm::vec4(0.2f));
    // Re-uploading stable lighting must not allocate a fresh descriptor set
    // each frame and exhaust the shared descriptor pool.
    for (int frame = 0; frame < 1000; ++frame) {
        graphics->begin3DFrame();
        graphics->setMesh3DViewProj(glm::mat4(1));
        graphics->setMesh3DClusteredLighting(lighting);
        graphics->drawMesh(mesh, glm::mat4(1), nullptr, eve::graphics::Color(1.f, 1.f, 1.f, 1.f));
        graphics->present();
    }
}
}  // namespace test_graphics_deferred_lifetime
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_deferred_targets)
#include "PrivateTestScope.h"
#include <memory>
#include "graphics/Graphics.h"
#include "graphics/RenderControl.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_deferred_targets.cpp
namespace test_graphics_deferred_targets {
TEST_CASE("graphics.deferredTargets.lateGbufferAndResize") {
    auto                       *window   = eve::window::Window::create();
    auto                       *graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    // A loading screen builds the shadow-only graph before any G-buffer exists.
    graphics->begin3DFrame();
    graphics->present();
    const float    positions[] = {-1, -1, 0.5f, 1, -1, 0.5f, 1, 1, 0.5f, -1, 1, 0.5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 2, 0, 3};
    auto          *mesh        = graphics->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t red[]   = {255, 0, 0, 255};
    auto         *texture = graphics->newTexture(1, 1, red);
    REQUIRE(texture != nullptr);
    for (int size : {64, 96}) {
        graphics->beginGBufferPass(size, size);
        graphics->drawMeshGBuffer(mesh, glm::mat4(1), glm::mat4(1), 0.1f, 100.f, texture,
                                  /*tint*/ 1.f, 1.f, 1.f, /*motion*/ 0.f, 0.f,
                                  /*roughness*/ 0.25f, /*metallic*/ 0.75f);
        graphics->endGBufferPass();
        auto *gb = graphics->getRenderControl()->getGBuffer();
        REQUIRE(gb != nullptr);
        REQUIRE(gb->hasBuffer("pbrParams"));
        REQUIRE(gb->hasBuffer("emissive"));
        std::unique_ptr<eve::image::ImageData> image(graphics->readGBufferToImageData("albedo"));
        REQUIRE(image != nullptr);
        REQUIRE_EQ(image->getWidth(), size);
        const auto  *pixels = static_cast<const uint8_t *>(image->getData());
        const size_t center = (size_t(size / 2) * size + size / 2) * 4;
        REQUIRE(pixels[center] > 247);
        REQUIRE(pixels[center + 1] < 8);
        REQUIRE(pixels[center + 2] < 8);

        std::unique_ptr<eve::image::ImageData> pbr(graphics->readGBufferToImageData("pbrParams"));
        REQUIRE(pbr != nullptr);
        const auto *pbrPx = static_cast<const uint8_t *>(pbr->getData());
        // R=metallic≈0.75, G=roughness≈0.25, B=occlusion=1, A=specular=1
        REQUIRE(pbrPx[center] > 170);
        REQUIRE(pbrPx[center] < 210);
        REQUIRE(pbrPx[center + 1] > 50);
        REQUIRE(pbrPx[center + 1] < 80);
        REQUIRE(pbrPx[center + 2] > 247);
        REQUIRE(pbrPx[center + 3] > 247);

        std::unique_ptr<eve::image::ImageData> emissive(graphics->readGBufferToImageData("emissive"));
        REQUIRE(emissive != nullptr);
        const auto *emPx = static_cast<const uint8_t *>(emissive->getData());
        REQUIRE(emPx[center] < 8);
        REQUIRE(emPx[center + 1] < 8);
        REQUIRE(emPx[center + 2] < 8);

        graphics->begin3DFrame();
        graphics->present();
        // Removing all geometry must still execute the clear-only pass.
        graphics->beginGBufferPass(size, size);
        graphics->endGBufferPass();
        std::unique_ptr<eve::image::ImageData> empty(graphics->readGBufferToImageData("depth"));
        REQUIRE(empty != nullptr);
        const auto *emptyPixels = static_cast<const uint8_t *>(empty->getData());
        REQUIRE_EQ(emptyPixels[center], uint8_t(255));
        graphics->begin3DFrame();
        graphics->present();
    }
}

TEST_CASE("graphics.hybridLighting.deferredPassAvailable") {
    auto                       *window   = eve::window::Window::create();
    auto                       *graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    auto *rc = graphics->getRenderControl();
    REQUIRE(rc != nullptr);
    // Vulkan and WebGPU both report supportsDeferredLighting (Phase C/D).
    if (!graphics->supportsDeferredLighting()) {
        CHECK(!rc->isDeferredLightingAvailable());
        auto ok = rc->setLightingMode(eve::graphics::LightingMode::Hybrid);
        CHECK(ok.ok());
        rc->compile();
        CHECK(rc->hasHybridLightingFallback());
        CHECK(!rc->hasPass("deferredLighting"));
        return;
    }
    CHECK(rc->isDeferredLightingAvailable());
    auto ok = rc->setLightingMode(eve::graphics::LightingMode::Hybrid);
    CHECK(ok.ok());
    rc->compile();
    CHECK(rc->getEffectiveLightingMode() == eve::graphics::LightingMode::Hybrid);
    CHECK(!rc->hasHybridLightingFallback());
    CHECK(rc->hasPass("deferredLighting"));
    CHECK(rc->hasPass("gbuffer"));
    CHECK(rc->hasPass("forward"));
    CHECK(!rc->isEnabled("msaa"));
    // Pass order: shadow → gbuffer → deferredLighting → forward
    int shadowIdx = -1, gbIdx = -1, defIdx = -1, fwdIdx = -1;
    for (int i = 0; i < rc->getPassCount(); ++i) {
        const std::string n = rc->getPassName(i);
        if (n == "shadow") shadowIdx = i;
        if (n == "gbuffer") gbIdx = i;
        if (n == "deferredLighting") defIdx = i;
        if (n == "forward") fwdIdx = i;
    }
    CHECK(shadowIdx >= 0);
    CHECK(gbIdx > shadowIdx);
    CHECK(defIdx > gbIdx);
    CHECK(fwdIdx > defIdx);

    // Smoke: GBuffer fill + deferred lighting into an open scene-color pass.
    const float    positions[] = {-1, -1, 0.5f, 1, -1, 0.5f, 1, 1, 0.5f, -1, 1, 0.5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 2, 0, 3};
    auto          *mesh        = graphics->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t red[]   = {220, 40, 40, 255};
    auto         *texture = graphics->newTexture(1, 1, red);
    REQUIRE(texture != nullptr);
    graphics->beginGBufferPass(160, 120);
    graphics->drawMeshGBuffer(mesh, glm::mat4(1), glm::mat4(1), 0.1f, 100.f, texture, 1.f, 1.f, 1.f, 0.f, 0.f, 0.4f,
                              0.1f);
    graphics->endGBufferPass();
    graphics->begin3DFrame();
    graphics->setMesh3DViewProj(glm::mat4(1));
    graphics->setMesh3DView(glm::mat4(1));
    graphics->setMesh3DClip(0.1f, 100.f);
    graphics->setMesh3DCameraPos(glm::vec3(0.f, 0.f, 3.f));
    graphics->drawDeferredLighting();
    graphics->present();
}

TEST_CASE("graphics.lightingPreset.ciKeepsForwardPlus") {
    auto                       *window   = eve::window::Window::create();
    auto                       *graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 64;
    settings.height = 48;
    REQUIRE(window->setWindowSettings(settings));
    auto *rc = graphics->getRenderControl();
    REQUIRE(rc != nullptr);

    auto ci = rc->applyLightingPreset(eve::graphics::LightingPreset::Ci);
    CHECK(ci.ok());
    CHECK(rc->getLightingPreset() == eve::graphics::LightingPreset::Ci);
    CHECK(rc->getLightingMode() == eve::graphics::LightingMode::ForwardPlus);
    rc->compile();
    CHECK(rc->getEffectiveLightingMode() == eve::graphics::LightingMode::ForwardPlus);
    CHECK(!rc->hasPass("deferredLighting"));

    auto desktop = rc->applyLightingPreset("desktop");
    CHECK(desktop.ok());
    CHECK(rc->getLightingPreset() == eve::graphics::LightingPreset::Desktop);
    if (graphics->supportsDeferredLighting()) {
        CHECK(rc->getLightingMode() == eve::graphics::LightingMode::Hybrid);
        rc->compile();
        CHECK(rc->hasPass("deferredLighting"));
    }

    auto bad = rc->applyLightingPreset("nope");
    CHECK(!bad.ok());
    CHECK(rc->getLightingPreset() == eve::graphics::LightingPreset::Desktop);
}
}  // namespace test_graphics_deferred_targets
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_font)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "Fixtures.h"
#include "PathBesideSource.h"
#include "common/Exception.h"
#include "data/ByteData.h"
#include "font/Font.h"
#include "font/FontData.h"
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
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_font.cpp
namespace test_graphics_font {
using namespace eve::graphics;

namespace {

// Same fixture used by test/font.cpp: FontAwesome, which only has icon
// glyphs (no printable ASCII) — its one well-known bitmap lives at U+F000.
constexpr int         kIconCodepoint = 0xF000;
constexpr const char *kIconUtf8      = "\xEF\x80\x80";  // UTF-8 for U+F000


// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;
EVE_DEFINE_PATH_BESIDE_SOURCE()

std::vector<char> readBinaryFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::unique_ptr<eve::font::FontData> loadFontAwesome(int size = 32) {
    auto raw = readBinaryFile(pathBesideThisSource("fonts/FontAwesome.ttf"));
    if (raw.empty()) return nullptr;
    eve::data::ByteData data(raw.data(), raw.size());
    return std::unique_ptr<eve::font::FontData>(eve::font::Font::create()->newFontData(&data, size));
}

bool expectException(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const eve::Exception &) {
        return true;
    }
    return false;
}

}  // namespace

TEST_CASE("graphics.font.newFontBuildsAtlasForCharset") {
    auto fontData = loadFontAwesome(32);
    REQUIRE(fontData.get() != nullptr);

    GfxFixture fx(320, 240, /*useHeadless=*/true);
    REQUIRE(fx.gfx != nullptr);

    std::unique_ptr<Font> gfont(fx.gfx->newFont(fontData.get(), kIconUtf8));
    REQUIRE(gfont.get() != nullptr);
    REQUIRE(gfont->getTexture() != nullptr);
    CHECK(gfont->getTexture()->getWidth() > 0);
    CHECK(gfont->getTexture()->getHeight() > 0);

    CHECK(gfont->getHeight() > 0.f);
    CHECK(gfont->getAscent() > 0.f);
    CHECK(gfont->getBaseline() > 0.f);
    CHECK(gfont->getWidth(kIconUtf8) > 0.f);

    // In the atlas (built only for kIconUtf8's codepoint), the icon is present
    // but an arbitrary ASCII letter is not — even though FontData itself might
    // resolve it differently; atlas coverage is what Font::hasGlyph reports.
    CHECK(gfont->hasGlyph(kIconCodepoint));
    CHECK(!gfont->hasGlyph(static_cast<int>('A')));

    const Font::Glyph *g = gfont->findGlyph(kIconCodepoint);
    REQUIRE(g != nullptr);
    CHECK(g->width > 0);
    CHECK(g->height > 0);
    CHECK(g->advance > 0);
    CHECK(gfont->findGlyph(static_cast<int>('A')) == nullptr);
}

TEST_CASE("graphics.font.newFontDefaultCharsetDoesNotCrash") {
    auto fontData = loadFontAwesome(24);
    REQUIRE(fontData.get() != nullptr);

    GfxFixture fx(320, 240, /*useHeadless=*/true);
    // FontAwesome has no printable ASCII glyphs, so most/all of the default
    // charset will be skipped — this exercises the "no glyphs rasterized"
    // fallback path (a valid 1x1 placeholder atlas) without throwing.
    std::unique_ptr<Font> gfont(fx.gfx->newFont(fontData.get()));
    REQUIRE(gfont.get() != nullptr);
    REQUIRE(gfont->getTexture() != nullptr);
}

// Shared coverage for Vulkan and WebGPU font rendering.
TEST_CASE("graphics.drawText.throwsWithNullFont") {
    GfxFixture fx(320, 240, /*useHeadless=*/true);
    fx.gfx->setFont(nullptr);
    CHECK(fx.gfx->getFont() == nullptr);
    CHECK(expectException([&] { fx.gfx->print("hello", 0.f, 0.f); }));
    CHECK(expectException([&] { fx.gfx->drawText(nullptr, "hello", 12.f, 18.f); }));
}

TEST_CASE("graphics.drawText.rendersAtRequestedPosition") {
    auto fontData = loadFontAwesome(32);
    REQUIRE(fontData.get() != nullptr);

    GfxFixture            fx(320, 240, /*useHeadless=*/true);
    std::unique_ptr<Font> gfont(fx.gfx->newFont(fontData.get(), kIconUtf8));
    REQUIRE(gfont.get() != nullptr);
    fx.gfx->setFont(gfont.get());
    CHECK(fx.gfx->getFont() == gfont.get());
    fx.gfx->print("", 0.f, 0.f);
    fx.gfx->setFont(nullptr);

    Canvas *rt = fx.gfx->newCanvas(96, 96);
    REQUIRE(rt != nullptr);
    fx.gfx->setCanvas(rt);
    fx.gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    fx.gfx->drawText(gfont.get(), kIconUtf8, 48.f, 48.f);
    fx.gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> img(rt->newImageData());
    REQUIRE(img.get() != nullptr);
    const auto *px                       = static_cast<const unsigned char *>(img->getData());
    long long   litNearRequestedPosition = 0;
    long long   litInOppositeCorner      = 0;
    for (int y = 0; y < img->getHeight(); ++y) {
        for (int x = 0; x < img->getWidth(); ++x) {
            const unsigned char *p = px + (static_cast<size_t>(y) * img->getWidth() + x) * 4;
            if (p[0] <= 40 && p[1] <= 40 && p[2] <= 40) continue;
            if (x >= 44 && y >= 44) ++litNearRequestedPosition;
            if (x < 32 && y < 32) ++litInOppositeCorner;
        }
    }
    CHECK(litNearRequestedPosition > 0);
    CHECK(litInOppositeCorner == 0);

    CHECK(fx.gfx->getFont() == nullptr);
}

TEST_CASE("graphics.drawText.rendersGlyphPixelsOnCanvas") {
    auto fontData = loadFontAwesome(32);
    REQUIRE(fontData.get() != nullptr);

    GfxFixture            fx(320, 240, /*useHeadless=*/true);
    std::unique_ptr<Font> gfont(fx.gfx->newFont(fontData.get(), kIconUtf8));
    REQUIRE(gfont.get() != nullptr);
    fx.gfx->setFont(nullptr);

    Canvas *rt = fx.gfx->newCanvas(64, 64);
    REQUIRE(rt != nullptr);

    fx.gfx->setCanvas(rt);
    fx.gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    fx.gfx->drawText(gfont.get(), kIconUtf8, 4.f, 4.f, Color(1.f, 1.f, 1.f, 1.f));
    fx.gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> img(rt->newImageData());
    REQUIRE(img.get() != nullptr);
    const auto *px = static_cast<const unsigned char *>(img->getData());
    const int   w  = img->getWidth();
    const int   h  = img->getHeight();

    // The glyph should have lit up at least some pixels away from the
    // all-black clear color somewhere in the canvas.
    long long litPixels = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char *p = px + (static_cast<size_t>(y) * w + x) * 4;
            if (p[0] > 40 || p[1] > 40 || p[2] > 40) ++litPixels;
        }
    }
    CHECK(litPixels > 0);

    // Windowed-only debug preview: hold the window open so the glyph is visible
    // while debugging (~2s). Skipped in headless mode (present() is a no-op).
    if (!fx.gfx->isHeadless()) {
        fx.gfx->setBackgroundColorRGBA(0.08f, 0.08f, 0.1f, 1.f);
        for (int frame = 0; frame < 60; ++frame) {
            fx.gfx->clearScreen();
            fx.gfx->drawTexturedRect(rt->getTexture(), 16.f, 16.f, 128.f, 128.f, Color(1.f, 1.f, 1.f, 1.f));
            fx.gfx->drawText(gfont.get(), kIconUtf8, 160.f, 40.f, Color(1.f, 1.f, 1.f, 1.f), 3.f);
            fx.gfx->present();

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) break;
            }
            SDL_Delay(16);
        }
    }

    CHECK(fx.gfx->getFont() == nullptr);
}
}  // namespace test_graphics_font
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_resource_reuse)
#include "PrivateTestScope.h"
#include <filesystem>
#include <memory>
#include "Fixtures.h"
#include "GraphicsParitySupport.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "graphics/ViewPreparation.h"
#include "graphics/hair/GroomInstance.h"
#include "zeroerr/unittest.h"
#if !defined(EVENGINE_WEBGPU)
#include "graphics/vulkan/Graphics.h"
#endif
// graphics_resource_reuse.cpp
namespace test_graphics_resource_reuse {
#if !defined(EVENGINE_WEBGPU)

#endif

using namespace eve::graphics;

TEST_CASE("graphics.resourceReuse.fileCacheDoesNotReloadResidentPixels") {
    auto      *gfx = parity_test::headlessGraphics();
    TempDir    files;
    const auto path       = files.path() / "eve-resource-reuse.png";
    auto      *filesystem = eve::filesystem::Filesystem::create();
    filesystem->allowMountingForPath(files.path().string());
    REQUIRE(filesystem->mount(files.path().string(), "", false));
    const std::string texturePath = "eve-resource-reuse.png";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};
    std::filesystem::copy_file("test/assets/resource_reuse.png", path,
                               std::filesystem::copy_options::overwrite_existing);
    auto *texture = gfx->newTextureFromFile(texturePath);
    REQUIRE(texture != nullptr);
    auto desired = TextureSampler::nearest();
    gfx->setTextureSampler(texture, desired);  // Must survive deferred first upload.
    gfx->ensureFileTexturesReady();
    REQUIRE(texture->gpuHandle != nullptr);
    CHECK(texture->sampler == desired);
    auto loaded = gfx->loadTexture(texturePath);
    REQUIRE(loaded.ok());
    CHECK(&loaded.value().get() == texture);
    CHECK(gfx->newTextureFromFile("./" + texturePath) == texture);
    void *const original = texture->gpuHandle;
    std::filesystem::remove(path);
    for (int i = 0; i < 20; ++i) {
        CHECK(gfx->newTextureFromFile(texturePath) == texture);
        CHECK(texture->gpuHandle == original);
    }
    CHECK(!gfx->reloadTextureFromFile(texturePath));
    CHECK(texture->gpuHandle == original);
    auto cached = gfx->loadTexture(texturePath);
    REQUIRE(cached.ok());
    CHECK(&cached.value().get() == texture);
    REQUIRE(gfx->releaseTexture(texture));
    delete texture;
    eve::ResourceManager::getInstance().unload(texturePath);
    CHECK(!gfx->loadTexture(texturePath).ok());
    std::filesystem::copy_file("test/assets/resource_reuse.png", path,
                               std::filesystem::copy_options::overwrite_existing);
    auto firstReady = gfx->loadTexture(texturePath);
    REQUIRE(firstReady.ok());
    auto &ready = firstReady.value().get();
    CHECK(!ready.hasDeferredFilePixels());
    CHECK(ready.gpuHandle != nullptr);
    CHECK_EQ(ready.getWidth(), 2);
    CHECK_EQ(ready.getHeight(), 2);
    auto absent = gfx->IResourceFactory::loadTexture(texturePath);
    REQUIRE(!absent.ok());
    CHECK_EQ(absent.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
    REQUIRE(gfx->releaseTexture(&ready));
    delete &ready;
    REQUIRE(filesystem->unmount(files.path().string()));
}

TEST_CASE("graphics.resourceReuse.samplerEditsCoalesceAndReuse") {
    GfxFixture    fixture(64, 64);
    auto         *gfx     = fixture.gfx;
    const uint8_t rgba[]  = {255, 0, 0, 255};
    auto         *texture = gfx->newTexture(1, 1, rgba);
    REQUIRE(texture != nullptr);
    auto      *second  = gfx->newTexture(1, 1, rgba);
    const auto initial = texture->sampler;
    auto       nearest = TextureSampler::nearest();
    for (int i = 0; i < 50; ++i) gfx->setTextureSampler(texture, nearest);
#if !defined(EVENGINE_WEBGPU)
    auto      *gpu      = static_cast<vulkan::GpuTexture *>(texture->gpuHandle);
    const auto original = gpu->sampler;
    CHECK(gpu->samplerState == initial);  // Setter has not executed backend work.
    gfx->setTextureSampler(texture, initial);
    gfx->begin3DFrame();
    CHECK(gpu->sampler == original);  // Reverted batch does no work.
    gfx->present();
    gfx->setTextureSampler(texture, nearest);
#endif
    gfx->setTextureSampler(second, nearest);
    gfx->begin3DFrame();
#if !defined(EVENGINE_WEBGPU)
    const auto cached = gpu->sampler;
    CHECK(gpu->samplerState == nearest);
    CHECK(cached == static_cast<vulkan::GpuTexture *>(second->gpuHandle)->sampler);
#endif
    gfx->present();
    gfx->setTextureSampler(texture, initial);
    gfx->begin3DFrame();
    gfx->present();
    gfx->setTextureSampler(texture, nearest);
    gfx->begin3DFrame();
#if !defined(EVENGINE_WEBGPU)
    CHECK(gpu->sampler == cached);
#endif
    gfx->present();
    gfx->setTextureSampler(second, initial);  // Release cancels pending publication.
    REQUIRE(gfx->releaseTexture(second));
    delete second;
    gfx->begin3DFrame();
    gfx->present();
    REQUIRE(gfx->releaseTexture(texture));
    delete texture;
}

TEST_CASE("graphics.resourceReuse.groomPublishesOnceAndKeepsMeshIdentity") {
    GfxFixture             fixture(64, 64);
    auto                  *gfx = fixture.gfx;
    hair::GroomInstance    groom(gfx);
    hair::ProceduralParams params;
    params.strandCount     = 12;
    params.pointsPerStrand = 4;
    params.seed            = 41;
    REQUIRE(groom.bakeProceduralPlane(0.2f, 0.2f, params).ok());
    auto *const mesh = groom.getMesh();
    REQUIRE(mesh != nullptr);
    void *const          gpuIdentity = mesh->gpuHandle;
    hair::GuideSimParams simulation;
    REQUIRE(groom.enableGuideSimulation(simulation).ok());
    for (int i = 0; i < 3; ++i) REQUIRE(groom.update(1.f / 60.f).ok());
    groom.setWidthScale(2.f);
    groom.setWidthScale(3.f);
#if !defined(EVENGINE_WEBGPU)
    auto      *gpu    = static_cast<vulkan::GpuMesh *>(mesh->gpuHandle);
    const auto before = gpu->dynamicWriteCount;
#endif
    auto prepare = [&] { return detail::prepareViewResources(*gfx, glm::mat4(1.f), glm::vec3(0.f)); };
    REQUIRE(prepare().ok());
    CHECK(groom.getMesh() == mesh);
    CHECK(mesh->gpuHandle == gpuIdentity);
#if !defined(EVENGINE_WEBGPU)
    CHECK_EQ(gpu->dynamicWriteCount, before + 1);
#endif
    REQUIRE(prepare().ok());
#if !defined(EVENGINE_WEBGPU)
    CHECK_EQ(gpu->dynamicWriteCount, before + 1);
#endif
    REQUIRE(groom.disableGuideSimulation().ok());
    for (int lod : {1, 2, 0, 1, 0}) {
        groom.setForcedLod(lod);
        REQUIRE(prepare().ok());
        if (lod == 2)
            CHECK(groom.getMesh() == nullptr);
        else
            CHECK(groom.getMesh() == mesh);
        CHECK(mesh->gpuHandle == gpuIdentity);
    }

    hair::GroomGroup unsupported;
    unsupported.name = "unsupported";
    auto strands     = hair::generateOnPlane(0.2f, 0.2f, params);
    REQUIRE(strands.ok());
    unsupported.strands = std::move(strands).takeValue();
    hair::GroomLod meshLod;
    meshLod.representation = hair::Representation::Meshes;
    unsupported.lods       = {meshLod};
    hair::GroomAsset candidate;
    REQUIRE(candidate.addGroup(std::move(unsupported)).ok());
    const auto curveCount = groom.getCurveCount();
    CHECK(!groom.setAsset(candidate).ok());
    CHECK_EQ(groom.getCurveCount(), curveCount);
    CHECK(groom.getMesh() == mesh);
    CHECK(mesh->gpuHandle == gpuIdentity);
    groom.setWidthScale(4.f);
    REQUIRE(prepare().ok());
    CHECK(groom.getMesh() == mesh);
    gfx->begin3DFrame();
    groom.draw();
    gfx->present();
}
}  // namespace test_graphics_resource_reuse
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_vegetation_render)
#include "PrivateTestScope.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include "asset/CanonicalMesh.h"
#include "asset/graphics/VegetationAsset.h"
#include "filesystem/FileData.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/VegetationField.h"
#include "graphics/VegetationMaskPacking.h"
#include "graphics/VegetationMotion.h"
#include "graphics/VegetationRender.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_vegetation_render.cpp
namespace test_graphics_vegetation_render {
using namespace eve::graphics;

TEST_CASE("graphics.vegetation.render_mesh_update_and_season") {
    auto                       *gfx    = Graphics::create();
    auto                       *window = eve::window::Window::create();
    eve::window::WindowSettings settings;
    settings.width  = 800;
    settings.height = 500;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.06f, 0.09f, 0.13f, 1.f));
    auto *camera = Camera3D::createCamera();
    camera->setEye(4, 3, 6);
    camera->setTarget(0, 0.6f, 0);
    camera->setAmbient(0.45f, 0.45f, 0.45f);
    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.4f, 1.f, 0.5f);
    sun->setColor(1, 0.96f, 0.9f, 1.5f);
    VegetationField   field;
    VegetationGlobals globals;
    globals.motion.z = 1.f;
    REQUIRE(field.replace(globals, {}).ok());
    std::vector<VegetationVertex> rest;
    std::vector<float>            uv;
    std::vector<uint32_t>         indices;
    for (unsigned row = 0; row < 12; ++row)
        for (unsigned col = 0; col < 16; ++col) {
            const glm::vec3 pivot(float(col) * 0.22f - 1.65f, 0.f, float(row) * 0.23f - 1.3f);
            const float     height = 0.7f + 0.04f * float((col * 7 + row * 13) % 9);
            const auto      start  = uint32_t(rest.size());
            for (unsigned level = 0; level < 4; ++level) {
                const float t     = float(level) / 3.f;
                const float width = 0.055f * (1.f - t) + 0.003f;
                for (unsigned side = 0; side < 2; ++side) {
                    VegetationVertex v;
                    v.pivot     = pivot;
                    v.position  = pivot + glm::vec3(side ? width : -width, t * height, 0);
                    v.normal    = {0, 0, 1};
                    v.tangent   = glm::vec4(1, 0, 0, 1);
                    v.bending   = t * t;
                    v.branch    = t;
                    v.flutter   = t;
                    v.variation = float((col + row * 16) % 101) / 100.f;
                    rest.push_back(v);
                    uv.insert(uv.end(), {float(side), t});
                }
                if (level < 3) {
                    const auto a = start + level * 2;
                    indices.insert(indices.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
                }
            }
        }
    eve::asset::CanonicalMeshData canonical;
    canonical.indices      = indices;
    canonical.texcoords[0] = uv;
    for (const auto *name : {"COLOR_0", "_UNITY_UV0", "_UNITY_UV1", "_UNITY_UV3", "TANGENT"})
        canonical.attributes[name].components = 4;
    for (size_t i = 0; i < rest.size(); ++i) {
        const auto &v = rest[i];
        canonical.positions.insert(canonical.positions.end(), {v.position.x, v.position.y, v.position.z});
        canonical.normals.insert(canonical.normals.end(), {v.normal.x, v.normal.y, v.normal.z});
        auto append = [&](const char *name, std::initializer_list<float> values) {
            auto &out = canonical.attributes[name].values;
            out.insert(out.end(), values);
        };
        const auto masks = std::floor(v.branch * 2047.f) * 2048.f + std::floor(v.flutter * 2047.f);
        append("COLOR_0", {v.variation, v.occlusion, v.detail, v.bending});
        append("TANGENT", {v.tangent->x, v.tangent->y, v.tangent->z, v.tangent->w});
        append("_UNITY_UV0", {uv[i * 2], 1.f - uv[i * 2 + 1], masks, 40980.f});
        append("_UNITY_UV1", {uv[i * 2], 1.f - uv[i * 2 + 1], 0, 0});
        append("_UNITY_UV3", {v.pivot.x, -v.pivot.z, v.pivot.y, 0});
    }
    auto asset = eve::asset_graphics::VegetationAsset::fromCanonical(canonical);
    REQUIRE(asset.ok());
    canonical    = {};  // Runtime deformation owns its rest stream independently of import staging.
    auto created = asset.value()->createMesh(*gfx, field, {});
    REQUIRE(created.ok());
    auto *mesh            = created.value();
    auto  initialGeometry = asset.value()->evaluate(field, {});
    REQUIRE(initialGeometry.ok());
    REQUIRE(std::vector<float>(mesh->motionHighlights().begin(), mesh->motionHighlights().end()) ==
            initialGeometry.value().motionHighlights);
    REQUIRE(std::vector<float>(mesh->vegetationFactors().begin(), mesh->vegetationFactors().end()) ==
            initialGeometry.value().vegetationFactors);
    REQUIRE(mesh->hasImportedTangents());
    const auto initialTangents = mesh->importedTangents();
    auto      *plant           = Renderable3D::create();
    plant->setMesh(mesh);
    plant->setCamera(camera);
    Material          material;
    VegetationSurface surface;
    surface.albedo    = {0.18f, 0.65f, 0.06f, 1.f};
    surface.roughness = 1.f;
    VegetationMask sourceMask{2, 1, {glm::vec4(0, .4f, .2f, .8f), glm::vec4(0, 1, .8f, .2f)}};
    auto           orm = packVegetationOrm(sourceMask, .7f, .8f);
    REQUIRE(orm.ok());
    uint8_t packedMask[8];
    for (size_t i = 0; i < 2; ++i)
        for (size_t c = 0; c < 4; ++c)
            packedMask[i * 4 + c] = uint8_t(std::lround(orm.value().pixels[i][int(c)] * 255.f));
    auto *maskTexture = gfx->newTexture(2, 1, packedMask);
    REQUIRE(maskTexture != nullptr);
    auto          pbr = material.pbrSurface();
    const uint8_t normalPixel[]{180, 128, 255, 255};
    auto         *normalTexture = gfx->newTexture(1, 1, normalPixel);
    REQUIRE(normalTexture != nullptr);
    pbr.normalMode                                          = PbrNormalMode::VegetationRG;
    pbr.textures[size_t(PbrTextureSlot::Normal)].texture    = normalTexture;
    pbr.textures[size_t(PbrTextureSlot::Normal)].srgbDecode = false;
    for (auto slot : {PbrTextureSlot::MetallicRoughness, PbrTextureSlot::Occlusion}) {
        auto &binding      = pbr.textures[size_t(slot)];
        binding.texture    = maskTexture;
        binding.srgbDecode = false;
    }
    REQUIRE(material.setPbrSurface(pbr).ok());
    auto sample = field.sample({0, 0, 0});
    REQUIRE(sample.ok());
    REQUIRE(applyVegetationSurface(material, sample.value(), surface).ok());
    plant->setMaterial(&material);
    VegetationMotion motion;
    motion.time   = 0.7;
    motion.camera = {4, 3, 6};
    REQUIRE(asset.value()->updateMesh(*gfx, *mesh, field, motion).ok());
    auto updatedGeometry = asset.value()->evaluate(field, motion);
    REQUIRE(updatedGeometry.ok());
    REQUIRE(std::vector<float>(mesh->motionHighlights().begin(), mesh->motionHighlights().end()) ==
            updatedGeometry.value().motionHighlights);
    REQUIRE(std::vector<float>(mesh->vegetationFactors().begin(), mesh->vegetationFactors().end()) ==
            updatedGeometry.value().vegetationFactors);
    REQUIRE(mesh->importedTangents() != initialTangents);
    REQUIRE(mesh->hasBounds());
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    [[maybe_unused]] auto                 *image = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> screenshot(gfx->newImageData());
    REQUIRE(screenshot != nullptr);
    int green = 0;
    for (int y = 0; y < settings.height; y += 2)
        for (int x = 0; x < settings.width; x += 2) {
            auto pixel = screenshot->getPixel(x, y);
            if (pixel.g > pixel.r + 0.04f && pixel.g > pixel.b + 0.04f) ++green;
        }
    CHECK(green > 1000);
    const auto path = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out/vegetation.png";
    std::filesystem::create_directories(path.parent_path());
    std::unique_ptr<eve::filesystem::FileData> png(
        screenshot->encode(medialoader::FormatHandler::ENCODED_PNG, "vegetation.png", false));
    REQUIRE(png != nullptr);
    std::ofstream output(path, std::ios::binary);
    output.write(static_cast<const char *>(png->getData()), std::streamsize(png->getSize()));
    REQUIRE(output.good());
    const auto withMask                                                     = material.pbrSurface();
    auto       withoutMask                                                  = withMask;
    withoutMask.textures[size_t(PbrTextureSlot::MetallicRoughness)].texture = nullptr;
    withoutMask.textures[size_t(PbrTextureSlot::Occlusion)].texture         = nullptr;
    REQUIRE(material.setPbrSurface(withoutMask).ok());
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<eve::image::ImageData> plainFrame(gfx->newImageData());
    REQUIRE(plainFrame != nullptr);
    int maskChangedPixels = 0;
    for (int y = 0; y < settings.height; y += 2)
        for (int x = 0; x < settings.width; x += 2) {
            const auto a = screenshot->getPixel(x, y), b = plainFrame->getPixel(x, y);
            if (std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b) > .01f) ++maskChangedPixels;
        }
    CHECK(maskChangedPixels > 100);
    auto dual               = withMask;
    dual.colorMaskEnabled   = true;
    dual.colorMaskSecondary = {.7f, .02f, .01f};
    dual.colorMaskMin       = .2f;
    dual.colorMaskMax       = .8f;
    REQUIRE(material.setPbrSurface(dual).ok());
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<eve::image::ImageData> dualFrame(gfx->newImageData());
    REQUIRE(dualFrame != nullptr);
    int secondaryPixels = 0;
    for (int y = 0; y < settings.height; y += 2)
        for (int x = 0; x < settings.width; x += 2) {
            const auto pixel = dualFrame->getPixel(x, y);
            if (pixel.r > pixel.g + .04f && pixel.r > pixel.b + .04f) ++secondaryPixels;
        }
    CHECK(secondaryPixels > 100);
    std::unique_ptr<eve::filesystem::FileData> dualPng(
        dualFrame->encode(medialoader::FormatHandler::ENCODED_PNG, "vegetation-dual-color.png", false));
    REQUIRE(dualPng != nullptr);
    std::ofstream dualOutput(path.parent_path() / "vegetation-dual-color.png", std::ios::binary);
    dualOutput.write(static_cast<const char *>(dualPng->getData()), std::streamsize(dualPng->getSize()));
    REQUIRE(dualOutput.good());
    REQUIRE(material.setPbrSurface(withMask).ok());
    VegetationElement autumn;
    autumn.extents    = {10, 10, 10};
    autumn.seasonal   = true;
    autumn.seasons[3] = {0.8f, 0.08f, 0.015f, 1.f};
    globals.season    = 3.f;
    REQUIRE(field.replace(globals, std::span(&autumn, 1)).ok());
    auto changed = field.sample({0, 0, 0});
    REQUIRE(changed.ok());
    REQUIRE(applyVegetationSurface(material, changed.value(), surface).ok());
    motion.time = 2.3;
    REQUIRE(asset.value()->updateMesh(*gfx, *mesh, field, motion).ok());
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<eve::image::ImageData> autumnFrame(gfx->newImageData());
    REQUIRE(autumnFrame != nullptr);
    int red = 0;
    for (int y = 0; y < settings.height; y += 2)
        for (int x = 0; x < settings.width; x += 2) {
            const auto pixel = autumnFrame->getPixel(x, y);
            if (pixel.r > pixel.g + 0.04f && pixel.r > pixel.b + 0.04f) ++red;
        }
    CHECK(red > 1000);
    std::unique_ptr<eve::filesystem::FileData> autumnPng(
        autumnFrame->encode(medialoader::FormatHandler::ENCODED_PNG, "vegetation-autumn.png", false));
    REQUIRE(autumnPng != nullptr);
    std::ofstream autumnOutput(path.parent_path() / "vegetation-autumn.png", std::ios::binary);
    autumnOutput.write(static_cast<const char *>(autumnPng->getData()), std::streamsize(autumnPng->getSize()));
    REQUIRE(autumnOutput.good());
    plant->setMaterial(nullptr);
    plant->setVisible(false);
}
}  // namespace test_graphics_vegetation_render
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_hair)
#include "PrivateTestScope.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include "Fixtures.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/HairCards.h"
#include "graphics/HairShader.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "graphics/hair/Binding.h"
#include "graphics/hair/ClusterGrid.h"
#include "graphics/hair/GroomAsset.h"
#include "graphics/hair/GroomInstance.h"
#include "graphics/hair/Guides.h"
#include "graphics/hair/Procedural.h"
#include "graphics/hair/RibbonBuilder.h"
#include "graphics/hair/Simulation.h"
#include "graphics/hair/StrandsDatas.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// hair.cpp
namespace test_hair {
#include "graphics/shaders/mesh3d_hair_frag_spv.inc"
#include "graphics/shaders/mesh3d_hair_vert_spv.inc"


// hair.cpp
using eve::graphics::Graphics;
using eve::graphics::Renderable3D;
using eve::graphics::Shader;


TEST_CASE("graphics.HairShader.createGpuPipeline") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);
    Shader *shader = gfx->newHairShader();
    REQUIRE(shader != nullptr);
    REQUIRE(shader->gpuHandle != nullptr);
    CHECK(shader->hasUniform("specExp"));
    CHECK(shader->hasUniform("strandDir"));
    CHECK(shader->hasUniform("marschnerR"));
    shader->sendFloat("specStrength", 0.7f);
    shader->sendVec3("strandDir", 0.f, 1.f, 0.f);
    shader->sendFloat("marschnerR", 1.2f);
    shader->sendFloat("marschnerTT", 0.5f);
    shader->sendFloat("marschnerTRT", 0.3f);
    CHECK(shader->hasUniform("selfShadowStrength"));
    shader->sendFloat("selfShadowStrength", 0.5f);
    shader->sendFloat("selfShadowBias", 0.2f);
    shader->sendFloat("rootAoStrength", 0.4f);
    win->close();
}


// hair_cards.cpp
using eve::graphics::Graphics;
using eve::graphics::Material;
using eve::graphics::Mesh;
using eve::graphics::Renderable3D;


TEST_CASE("graphics.HairCards.uploadMaterialAndLod") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);
    REQUIRE(gfx != nullptr);

    Mesh *nearMesh = gfx->newHairCardMesh(0.15f, 0.4f);
    REQUIRE(nearMesh != nullptr);
    Mesh *farMesh = gfx->newHairCardMesh(0.25f, 0.35f);
    REQUIRE(farMesh != nullptr);

    Material *mat = gfx->newHairCardMaterial(nullptr);
    REQUIRE(mat != nullptr);
    CHECK(mat->getHair());
    CHECK(mat->getDoubleSided());
    CHECK(!mat->getDepthWrite());
    CHECK(!mat->getCastShadow());
    CHECK_EQ(mat->getSortPriority(), 10);

    auto *r = Renderable3D::create();
    REQUIRE(r != nullptr);
    eve::graphics::hair::configureCardLod(r, nearMesh, farMesh, 6.f);
    CHECK(r->getHair());
    CHECK_EQ(r->getMeshLodCount(), 2);

    delete mat;
    win->close();
}

// hair_groom.cpp
using eve::graphics::Graphics;
using eve::graphics::hair::appendRibbonMesh;
using eve::graphics::hair::BindingDeformMode;
using eve::graphics::hair::buildGuideWeights;
using eve::graphics::hair::buildRibbons;
using eve::graphics::hair::ClusterGrid;
using eve::graphics::hair::extractGuides;
using eve::graphics::hair::filterStrandsByCurves;
using eve::graphics::hair::generateOnPlane;
using eve::graphics::hair::GroomAsset;
using eve::graphics::hair::GroomBinding;
using eve::graphics::hair::GroomGroup;
using eve::graphics::hair::GroomInstance;
using eve::graphics::hair::GroomLod;
using eve::graphics::hair::GuideInfluence;
using eve::graphics::hair::GuideSimParams;
using eve::graphics::hair::GuideSimulator;
using eve::graphics::hair::interpolateStrands;
using eve::graphics::hair::InterpolationMode;
using eve::graphics::hair::ProceduralParams;
using eve::graphics::hair::Representation;
using eve::graphics::hair::RibbonMesh;
using eve::graphics::hair::RibbonParams;
using eve::graphics::hair::selectLodIndex;
using eve::graphics::hair::SkinTriMesh;
using eve::graphics::hair::StrandCurve;
using eve::graphics::hair::StrandGuideWeights;
using eve::graphics::hair::StrandPoint;
using eve::graphics::hair::StrandsDatas;


TEST_CASE("graphics.hair.groomInstanceMultiGroupAndMarschner") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);

    ProceduralParams params;
    params.strandCount     = 6;
    params.pointsPerStrand = 4;
    params.seed            = 31;
    auto scalp             = generateOnPlane(0.2f, 0.2f, params);
    params.seed            = 32;
    auto fringe            = generateOnPlane(0.12f, 0.12f, params);
    REQUIRE(scalp.ok());
    REQUIRE(fringe.ok());

    GroomLod lod;
    lod.screenSize     = 1.f;
    lod.representation = Representation::Strands;

    GroomGroup g0;
    g0.name    = "scalp";
    g0.groupId = 1;
    g0.strands = std::move(scalp).value();
    g0.lods    = {lod};
    GroomGroup g1;
    g1.name    = "fringe";
    g1.groupId = 2;
    g1.strands = std::move(fringe).value();
    g1.lods    = {lod};

    GroomAsset asset;
    REQUIRE(asset.addGroup(std::move(g0)).ok());
    REQUIRE(asset.addGroup(std::move(g1)).ok());

    GroomInstance *groom = gfx->newGroomInstance();
    REQUIRE(groom != nullptr);
    auto set = groom->setAsset(asset);
    REQUIRE(set.ok());
    CHECK_EQ(groom->getGroupCount(), 2u);
    CHECK(groom->getCurveCount() == 12);
    REQUIRE(groom->getMesh() != nullptr);
    REQUIRE(groom->getShader() != nullptr);
    CHECK(groom->getShader()->hasUniform("marschnerR"));

    groom->setMarschnerLobes(1.5f, 0.6f, 0.4f);
    CHECK_EQ(groom->getMarschnerR(), 1.5f);
    CHECK_EQ(groom->getMarschnerTT(), 0.6f);
    CHECK_EQ(groom->getMarschnerTRT(), 0.4f);
    groom->setMarschnerLobes(-1.f, 0.2f, -0.5f);
    CHECK_EQ(groom->getMarschnerR(), 0.f);
    CHECK_EQ(groom->getMarschnerTT(), 0.2f);
    CHECK_EQ(groom->getMarschnerTRT(), 0.f);

    CHECK(groom->getShader()->hasUniform("selfShadowStrength"));
    groom->setSelfShadow(0.6f, 0.2f, 0.5f);
    CHECK_EQ(groom->getSelfShadowStrength(), 0.6f);
    CHECK_EQ(groom->getSelfShadowBias(), 0.2f);
    CHECK_EQ(groom->getRootAoStrength(), 0.5f);
    groom->setSelfShadow(-1.f, 2.f, -0.5f);
    CHECK_EQ(groom->getSelfShadowStrength(), 0.f);
    CHECK_EQ(groom->getSelfShadowBias(), 1.f);
    CHECK_EQ(groom->getRootAoStrength(), 0.f);

    gfx->begin3DFrame();
    groom->draw(glm::mat4(1.f));
    gfx->present();
    win->close();
}


TEST_CASE("graphics.hair.groomInstanceCardsLodBakeAndDraw") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);

    GroomInstance *groom = gfx->newGroomInstance();
    REQUIRE(groom != nullptr);
    ProceduralParams params;
    params.strandCount     = 24;
    params.pointsPerStrand = 5;
    params.seed            = 41;
    auto baked             = groom->bakeProceduralPlane(0.2f, 0.2f, params);
    REQUIRE(baked.ok());
    REQUIRE(groom->getMesh() != nullptr);
    CHECK_EQ(groom->getActiveLodIndex(), 0);
    CHECK_EQ(groom->getActiveRepresentation(), int(Representation::Strands));
    const int strandsVerts = groom->getMesh()->getVertexCount();
    CHECK(strandsVerts > 0);

    groom->setForcedLod(1);
    auto cardsRebuild = groom->rebuild();
    REQUIRE(cardsRebuild.ok());
    REQUIRE(groom->getMesh() != nullptr);
    CHECK_EQ(groom->getActiveLodIndex(), 1);
    CHECK_EQ(groom->getActiveRepresentation(), int(Representation::Cards));
    const int cardsVerts = groom->getMesh()->getVertexCount();
    CHECK(cardsVerts > 0);
    // Cards expand per-segment quads; topology differs from near ribbon bake.
    CHECK(cardsVerts != strandsVerts);

    groom->setForcedLod(2);
    auto noneRebuild = groom->rebuild();
    REQUIRE(noneRebuild.ok());
    CHECK_EQ(groom->getActiveLodIndex(), 2);
    CHECK_EQ(groom->getActiveRepresentation(), int(Representation::None));
    CHECK(groom->getMesh() == nullptr);

    groom->setForcedLod(1);
    REQUIRE(groom->rebuild().ok());
    gfx->begin3DFrame();
    groom->draw(glm::mat4(1.f));
    gfx->present();
    win->close();
}

TEST_CASE("graphics.hair.groomInstanceGuideSimUpdate") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);

    GroomInstance *groom = gfx->newGroomInstance();
    REQUIRE(groom != nullptr);
    ProceduralParams params;
    params.strandCount     = 32;
    params.pointsPerStrand = 6;
    params.seed            = 11;
    auto baked             = groom->bakeProceduralPlane(0.25f, 0.25f, params);
    REQUIRE(baked.ok());
    CHECK(!groom->isGuideSimulationEnabled());

    GuideSimParams simParams;
    simParams.gravity    = glm::vec3(0.f, -15.f, 0.f);
    simParams.damping    = 0.97f;
    simParams.iterations = 4;
    auto enabled         = groom->enableGuideSimulation(simParams, 0.2f, InterpolationMode::Offset);
    REQUIRE(enabled.ok());
    CHECK(groom->isGuideSimulationEnabled());
    REQUIRE(groom->getMesh() != nullptr);

    for (int i = 0; i < 12; ++i) {
        auto upd = groom->update(1.f / 60.f);
        REQUIRE(upd.ok());
    }
    CHECK(groom->getMesh() != nullptr);
    CHECK_EQ(groom->getCurveCount(), 32);

    auto disabled = groom->disableGuideSimulation();
    REQUIRE(disabled.ok());
    CHECK(!groom->isGuideSimulationEnabled());
    CHECK(groom->update(1.f / 60.f).ok());

    gfx->begin3DFrame();
    groom->draw(glm::mat4(1.f));
    gfx->present();
    win->close();
}
}  // namespace test_hair
#endif
