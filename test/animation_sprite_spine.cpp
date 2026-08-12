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
#include "graphics/DrawItem2D.h"
#include "graphics/Quad.h"

#include "common/Exception.h"

#include <cmath>
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
