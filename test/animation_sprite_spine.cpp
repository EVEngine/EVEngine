#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/Animation.h"
#include "animation/SpriteAnim.h"
#include "animation/SpriteClip.h"
#include "animation/SpriteSheet.h"
#include "animation/SpineAnim.h"
#include "animation/SpineAtlas.h"
#include "animation/SpineSkeleton.h"
#include "animation/SpineSkeletonData.h"
#include "filesystem/Filesystem.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Graphics.h"
#include "graphics/Quad.h"
#include "graphics/RenderSystem.h"
#include "window/Window.h"

#include <SDL2/SDL.h>
#include "common/Exception.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace eve::animation;

TEST_CASE("animation.sprite.sheetGridAndNamed") {
    std::unique_ptr<SpriteSheet> sheet(new SpriteSheet());
    int added = sheet->setGrid(4, 2, 32, 48, 0, 0, 0, 0);
    CHECK_EQ(added, 8);
    CHECK_EQ(sheet->getFrameCount(), 8);
    CHECK_EQ(sheet->getFrameX(0), 0);
    CHECK_EQ(sheet->getFrameY(0), 0);
    CHECK_EQ(sheet->getFrameWidth(0), 32);
    CHECK_EQ(sheet->getFrameHeight(0), 48);
    CHECK_EQ(sheet->getFrameX(5), 32);  // row1 col1
    CHECK_EQ(sheet->getFrameY(5), 48);

    sheet->clear();
    int i0 = sheet->addFrame("idle0", 10, 20, 16, 16);
    int i1 = sheet->addFrame("idle1", 26, 20, 16, 16);
    CHECK_EQ(i0, 0);
    CHECK_EQ(i1, 1);
    CHECK_EQ(sheet->findFrame("idle1"), 1);
    CHECK_EQ(sheet->findFrame("missing"), -1);

    eve::graphics::Quad quad;
    sheet->applyToQuad(&quad, 1);
    CHECK_EQ(quad.getX(), 26);
    CHECK_EQ(quad.getY(), 20);
    CHECK_EQ(quad.getWidth(), 16);
    CHECK_EQ(quad.getHeight(), 16);
}

TEST_CASE("animation.sprite.clipAndPlayer") {
    auto *anim = Animation::create();
    std::unique_ptr<SpriteSheet> sheet(anim->newSpriteSheet());
    sheet->setGrid(4, 1, 16, 16);

    std::unique_ptr<SpriteClip> clip(anim->newSpriteClip("walk"));
    clip->setLoop(true);
    clip->addFrame(0, 0.1f);
    clip->addFrame(1, 0.1f);
    clip->addFrame(2, 0.1f);
    clip->addFrame(3, 0.1f);
    CHECK(std::fabs(clip->getDuration() - 0.4f) < 1e-5f);
    CHECK_EQ(clip->frameAtTime(0.05f), 0);
    CHECK_EQ(clip->frameAtTime(0.15f), 1);
    CHECK_EQ(clip->frameAtTime(0.45f), 0);  // loop

    std::unique_ptr<SpriteAnim> player(anim->newSpriteAnim());
    player->setSheet(sheet.get());
    eve::graphics::Quad quad;
    player->bindQuad(&quad);
    player->play(clip.get());
    CHECK(player->isPlaying());
    CHECK_EQ(player->getSheetFrame(), 0);
    CHECK_EQ(quad.getX(), 0);

    anim->update(0.15f);
    CHECK_EQ(player->getClipFrame(), 1);
    CHECK_EQ(player->getSheetFrame(), 1);
    CHECK_EQ(quad.getX(), 16);

    player->setLoop(false);
    player->setTime(0.f);
    player->play(clip.get());
    player->setLoop(false);
    // finish
    for (int i = 0; i < 10; ++i) player->update(0.1f);
    CHECK(player->isFinished());
    CHECK(!player->isPlaying());
    CHECK_EQ(player->getSheetFrame(), 3);
}

TEST_CASE("animation.spine.atlasParse") {
    const char *atlasText =
        "hero.png\n"
        "size: 128,64\n"
        "format: RGBA8888\n"
        "filter: Linear,Linear\n"
        "repeat: none\n"
        "body\n"
        "  rotate: false\n"
        "  xy: 2, 2\n"
        "  size: 40, 50\n"
        "  orig: 40, 50\n"
        "  offset: 0, 0\n"
        "  index: -1\n"
        "head\n"
        "  rotate: false\n"
        "  xy: 50, 4\n"
        "  size: 24, 24\n"
        "  orig: 24, 24\n"
        "  offset: 0, 0\n"
        "  index: -1\n";

    std::unique_ptr<SpineAtlas> atlas(new SpineAtlas());
    std::string err;
    CHECK(atlas->loadFromText(atlasText, &err));
    CHECK_EQ(atlas->getPageCount(), 1);
    CHECK_EQ(atlas->getPageName(0), std::string("hero.png"));
    CHECK_EQ(atlas->getPageWidth(0), 128);
    CHECK_EQ(atlas->getPageHeight(0), 64);
    CHECK_EQ(atlas->getRegionCount(), 2);
    CHECK_EQ(atlas->findRegion("body"), 0);
    CHECK_EQ(atlas->getRegionWidth(0), 40);
    CHECK_EQ(atlas->getRegionHeight(0), 50);
    CHECK_EQ(atlas->getRegionX(1), 50);

    float u0, v0, u1, v1;
    atlas->getRegionUV(0, 128, 64, u0, v0, u1, v1);
    CHECK(std::fabs(u0 - 2.f / 128.f) < 1e-5f);
    CHECK(std::fabs(v0 - 2.f / 64.f) < 1e-5f);
}

TEST_CASE("animation.spine.skeletonAndAnim") {
    const char *json = R"JSON({
  "skeleton": { "spine": "4.1.00" },
  "bones": [
    { "name": "root" },
    { "name": "body", "parent": "root", "y": 40 },
    { "name": "head", "parent": "body", "y": 30 }
  ],
  "slots": [
    { "name": "body", "bone": "body", "attachment": "body" },
    { "name": "head", "bone": "head", "attachment": "head" }
  ],
  "skins": [
    {
      "name": "default",
      "attachments": {
        "body": { "body": { "width": 40, "height": 50 } },
        "head": { "head": { "width": 24, "height": 24, "y": 5 } }
      }
    }
  ],
  "animations": {
    "idle": {
      "bones": {
        "body": {
          "rotate": [
            { "time": 0, "value": 0 },
            { "time": 0.5, "value": 20 },
            { "time": 1.0, "value": 0 }
          ]
        }
      }
    },
    "nod": {
      "bones": {
        "head": {
          "translate": [
            { "time": 0, "x": 0, "y": 0 },
            { "time": 0.25, "x": 0, "y": -8 }
          ]
        }
      }
    }
  }
})JSON";

    const char *atlasText =
        "hero.png\n"
        "size: 128,64\n"
        "body\n"
        "  xy: 2, 2\n"
        "  size: 40, 50\n"
        "head\n"
        "  xy: 50, 4\n"
        "  size: 24, 24\n";

    auto *animMod = Animation::create();
    SpineSkeletonData *dataRaw = animMod->newSpineSkeletonDataFromJson(json);
    std::unique_ptr<SpineSkeletonData> data(dataRaw);
    CHECK(dataRaw != nullptr);
    if (!dataRaw) return;
    CHECK_EQ(data->getBoneCount(), 3);
    CHECK_EQ(data->findBone("head"), 2);
    CHECK_EQ(data->getSlotCount(), 2);
    CHECK_EQ(data->getAnimationCount(), 2);
    CHECK(std::fabs(data->getAnimationDuration(data->findAnimation("idle")) - 1.f) < 1e-4f);

    SpineAtlas *atlasRaw = animMod->newSpineAtlasFromText(atlasText);
    std::unique_ptr<SpineAtlas> atlas(atlasRaw);
    CHECK(atlasRaw != nullptr);
    if (!atlasRaw) return;

    std::unique_ptr<SpineSkeleton> sk(animMod->newSpineSkeleton(data.get()));
    sk->updateWorldTransform();
    CHECK(std::fabs(sk->getBoneWorldY(1) - 40.f) < 1e-3f);
    CHECK(std::fabs(sk->getBoneWorldY(2) - 70.f) < 1e-3f);

    std::unique_ptr<SpineAnim> player(animMod->newSpineAnim(sk.get()));
    player->setAtlas(atlas.get());
    player->setFlipY(false);  // keep Spine Y-up for assertions
    player->setLoop(true);
    CHECK(player->play("idle"));
    player->setTime(0.5f);
    player->apply();
    // body setup rot 0 + key 20
    CHECK(std::fabs(sk->getBoneLocalRotation(1) - 20.f) < 1e-3f);
    CHECK_EQ(player->getDrawSlotCount(), 2);

    CHECK(player->play("nod"));
    player->setLoop(false);
    player->setTime(0.25f);
    player->apply();
    // head setup y=30 + translate -8
    CHECK(std::fabs(sk->getBoneLocalY(2) - 22.f) < 1e-3f);

    std::vector<eve::graphics::DrawItem2D> items;
    player->setFlipY(true);
    player->setPosition(100, 200);
    player->collectDrawItems(items);
    CHECK_EQ(static_cast<int>(items.size()), 2);
    CHECK_EQ(items[0].layer, 0);
}

TEST_CASE("animation.spine.modulePump") {
    auto *anim = Animation::create();
    const char *json = R"JSON({
  "bones": [ { "name": "root" } ],
  "slots": [ { "name": "slot", "bone": "root", "attachment": "a" } ],
  "skins": [ { "name": "default", "attachments": {
    "slot": { "a": { "width": 10, "height": 10 } }
  }}],
  "animations": {
    "spin": { "bones": { "root": { "rotate": [
      { "time": 0, "value": 0 }, { "time": 1, "value": 90 }
    ]}}}
  }
})JSON";
    SpineSkeletonData *dataRaw = anim->newSpineSkeletonDataFromJson(json);
    std::unique_ptr<SpineSkeletonData> data(dataRaw);
    CHECK(dataRaw != nullptr);
    if (!dataRaw) return;
    std::unique_ptr<SpineSkeleton> sk(anim->newSpineSkeleton(data.get()));
    std::unique_ptr<SpineAnim> player(anim->newSpineAnim(sk.get()));
    player->play("spin");
    CHECK_EQ(anim->getSpineAnimCount(), 1);
    anim->update(0.5f);
    CHECK(std::fabs(sk->getBoneLocalRotation(0) - 45.f) < 1e-2f);
}

namespace {

std::string spineAssetsDir() {
    std::string here = __FILE__;
    const auto slash = here.find_last_of("/\\");
    const std::string dir = (slash == std::string::npos) ? std::string(".") : here.substr(0, slash);
    return dir + "/assets/spine";
}

bool spineAssetsReady() {
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
void measurePoseCenter(SpineAnim *anim, float &cx, float &cy) {
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
    win->setGraphics(gfx);
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
