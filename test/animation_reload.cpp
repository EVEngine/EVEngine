#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimClipRegistry.h"
#include "animation/AnimImporter.h"
#include "animation/AnimSkeleton.h"
#include "animation/Animation.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

using namespace eve::animation;

namespace {

const char* kRefreshFixturePath = "eve_anim_reload_refresh.anim.txt";
const char* kBrokenFixturePath  = "eve_anim_reload_broken.anim.txt";

std::unique_ptr<AnimSkeleton> makeSkeleton() {
    auto sk   = std::make_unique<AnimSkeleton>();
    int  root = sk->addBone("root", -1);
    sk->setBindPosition(root, 0.f, 0.f, 0.f);
    int hip = sk->addBone("hip", root);
    sk->setBindPosition(hip, 0.f, 1.f, 0.f);
    return sk;
}

std::string makeAnimationFixture(float duration, int keyCount) {
    auto     sk = makeSkeleton();
    AnimClip clip("walk");
    clip.setDuration(duration);
    clip.setLoop(true);
    clip.setSampleRate(20.f);
    for (int i = 0; i < keyCount; ++i) {
        clip.addPositionKey(0, static_cast<float>(i) * 0.5f, 0.f, 0.f, static_cast<float>(i));
    }
    return AnimImporter::exportAnimationFixtureText(sk.get(), &clip);
}

void writeAnimationFixture(const char* path, const std::string& text) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs << text;
}

}  // namespace

TEST_CASE("animClip.adoptSwapsPayloadInPlace") {
    std::unique_ptr<AnimClip> a(new AnimClip("a"));
    a->setDuration(1.f);

    std::unique_ptr<AnimClip> b(new AnimClip("b"));
    b->setDuration(2.f);
    b->setLoop(false);
    b->addPositionKey(0, 0.25f, 1.f, 2.f, 3.f);

    a->adopt(*b);
    CHECK(a->getName() == "b");
    CHECK(a->getDuration() == 2.f);
    CHECK(!a->getLoop());
    CHECK_EQ(a->getPositionKeyCount(0), 1);
}

TEST_CASE("animClip.registryTracksPathAndUnregistersOnDestroy") {
    AnimClipRegistry::clear();
    {
        std::unique_ptr<AnimClip> c(new AnimClip("c"));
        AnimClipRegistry::registerPath("anim\\walk.anim.txt", c.get());
        CHECK(AnimClipRegistry::hasPath("anim/walk.anim.txt"));
        CHECK_EQ(AnimClipRegistry::count(), 1);

        const std::vector<AnimClip*> found = AnimClipRegistry::findByPath("anim/walk.anim.txt");
        REQUIRE(found.size() == 1u);
        CHECK(found[0] == c.get());
    }
    CHECK_EQ(AnimClipRegistry::count(), 0);
    CHECK(!AnimClipRegistry::hasPath("anim/walk.anim.txt"));
    AnimClipRegistry::clear();
}

TEST_CASE("animClip.reloadPathRefreshesRegisteredClips") {
    writeAnimationFixture(kRefreshFixturePath, makeAnimationFixture(1.f, 2));

    Animation*                anim = Animation::create();
    std::unique_ptr<AnimClip> loaded(anim->newClipFromAnimationFixtureText(kRefreshFixturePath));
    REQUIRE(loaded.get() != nullptr);
    AnimClip* identity = loaded.get();
    CHECK(loaded->getDuration() == 1.f);
    CHECK_EQ(AnimClipRegistry::findByPath(kRefreshFixturePath).size(), 1u);

    // Rewrite the source with different content and hot-reload.
    writeAnimationFixture(kRefreshFixturePath, makeAnimationFixture(2.f, 4));
    auto refreshed = AnimClipRegistry::reloadPath(kRefreshFixturePath);
    REQUIRE(refreshed.ok());
    CHECK_EQ(std::move(refreshed).takeValue(), 1);

    CHECK(loaded.get() == identity);  // instance identity is stable
    CHECK(loaded->getDuration() == 2.f);
    CHECK_EQ(loaded->getPositionKeyCount(0), 4);

    AnimClipRegistry::clear();
    std::remove(kRefreshFixturePath);
}

TEST_CASE("animClip.reloadUnknownOrBrokenPathIsNoop") {
    auto absent = AnimClipRegistry::reloadPath("no_such_file.anim.txt");
    REQUIRE(absent.ok());
    CHECK_EQ(std::move(absent).takeValue(), 0);

    std::unique_ptr<AnimClip> c(new AnimClip("c"));
    AnimClipRegistry::registerPath(kBrokenFixturePath, c.get());
    writeAnimationFixture(kBrokenFixturePath, "not an animation fixture");
    auto broken = AnimClipRegistry::reloadPath(kBrokenFixturePath);
    REQUIRE(!broken.ok());
    AnimClipRegistry::clear();
    std::remove(kBrokenFixturePath);
}
