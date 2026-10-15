#include "PathBesideSource.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/Animation.h"
#include "animation/SpineAnim.h"
#include "animation/SpineAtlas.h"
#include "animation/SpineSkeleton.h"
#include "animation/SpineSkeletonData.h"
#include "animation/SpriteAnim.h"
#include "animation/SpriteClip.h"
#include "animation/SpriteSheet.h"
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

// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

#include <SDL2/SDL.h>
#include "common/Exception.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace eve::animation;


namespace {

[[maybe_unused]] std::string spineAssetsDir() {
    std::string here = eve_test_path::sourceBesideTestDir(__FILE__);
    const auto slash = here.find_last_of("/\\");
    const std::string dir = (slash == std::string::npos) ? std::string(".") : here.substr(0, slash);
    return dir + "/assets/spine";
}

[[maybe_unused]] bool spineAssetsReady() {
    return std::filesystem::is_regular_file(spineAssetsDir() + "/.downloaded");
}

struct SpineDemoModel {
    const char *relPath;  // virtual path under the mounted spine assets dir
    const char *atlasRel;
    const char *animation;
    float cx = 0.f, cy = 0.f;  // target CENTER of the model's bounding box on screen
    float scale = 1.f;
};

struct SpineDemoPlayer {
    std::unique_ptr<SpineSkeletonData> data;
    std::unique_ptr<SpineAtlas> atlas;
    std::unique_ptr<SpineSkeleton> skeleton;
    std::unique_ptr<SpineAnim> anim;
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
        std::printf("animation.spine.windowDemoRealModels: missing %s — run "
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
        {"spineboy/export/spineboy.json", "spineboy/export/spineboy.atlas", "run",
         250.f, 420.f, 0.55f},
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
            const auto slash = texRel.find_last_of('/');
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
    std::vector<size_t> perPlayerItems(players.size(), 0);
    std::vector<bool>   perPlayerRotated(players.size(), false);
    const int frames = 60 * 3;  // ~3 seconds at 60 fps
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
    const Color bg = gfx->getPixel(4, 4);
    for (size_t pi = 0; pi < players.size(); ++pi) {
        const int cx = static_cast<int>(models[pi].cx);
        const int cy = static_cast<int>(models[pi].cy);
        for (int dy = -24; dy <= 24; dy += 8) {
            for (int dx = -24; dx <= 24; dx += 8) {
                const Color c = gfx->getPixel(cx + dx, cy + dy);
                const float dist =
                    std::fabs(c.r - bg.r) + std::fabs(c.g - bg.g) + std::fabs(c.b - bg.b);
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
