#include "action_editor/AnimationRootMotionPreviewSource.h"

#include "animation/AnimClip.h"
#include "animation/AnimClipRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

namespace {

void configureClip(eve::animation::AnimClip& clip) {
    clip.setDuration(1.0f);
    clip.setLoop(false);
    clip.addPositionKey(0, 0.0f, 0.0f, 0.0f, 0.0f);
    clip.addPositionKey(0, 1.0f, 4.0f, 2.0f, 8.0f);
}

}  // namespace

TEST_CASE("animationActionPreview.samplesRegisteredClipWithoutRetainingIt") {
    const std::string        uri = "asset://animations/root-motion-preview.eva";
    eve::animation::AnimClip clip("root-motion-preview");
    configureClip(clip);
    eve::animation::AnimClipRegistry::registerPath(uri, &clip);
    eve::editor::AnimationRootMotionPreviewSource source;

    auto path = source.sampleRootMotion(uri, eve::Duration::fromNanoseconds(1000000000), 3);
    REQUIRE(path.ok());
    REQUIRE_EQ(path.value().size(), 3u);
    CHECK(std::fabs(path.value()[1].x - 2.0f) < 1e-5f);
    CHECK(std::fabs(path.value()[1].y - 1.0f) < 1e-5f);
    CHECK(std::fabs(path.value()[2].z - 8.0f) < 1e-5f);
}

TEST_CASE("animationActionPreview.rejectsMissingAmbiguousAndInvalidRootClips") {
    const std::string                             uri = "asset://animations/ambiguous-preview.eva";
    eve::editor::AnimationRootMotionPreviewSource source;
    auto missing = source.sampleRootMotion(uri, eve::Duration::fromNanoseconds(1000000000), 3);
    CHECK(!missing.ok());

    eve::animation::AnimClip first("first");
    eve::animation::AnimClip second("second");
    configureClip(first);
    configureClip(second);
    eve::animation::AnimClipRegistry::registerPath(uri, &first);
    eve::animation::AnimClipRegistry::registerPath(uri, &second);
    auto ambiguous = source.sampleRootMotion(uri, eve::Duration::fromNanoseconds(1000000000), 3);
    CHECK(!ambiguous.ok());

    const std::string uniqueUri = "asset://animations/invalid-root-preview.eva";
    eve::animation::AnimClipRegistry::registerPath(uniqueUri, &first);
    eve::editor::AnimationRootMotionPreviewSource invalidRoot(1);
    auto invalid = invalidRoot.sampleRootMotion(uniqueUri, eve::Duration::fromNanoseconds(1000000000), 3);
    CHECK(!invalid.ok());
}

TEST_CASE("animationActionPreview.clipDestructionBecomesObservableNotStale") {
    const std::string                             uri = "asset://animations/transient-preview.eva";
    eve::editor::AnimationRootMotionPreviewSource source;
    {
        eve::animation::AnimClip clip("transient");
        configureClip(clip);
        eve::animation::AnimClipRegistry::registerPath(uri, &clip);
        auto present = source.sampleRootMotion(uri, eve::Duration::fromNanoseconds(1000000000), 2);
        REQUIRE(present.ok());
    }
    auto destroyed = source.sampleRootMotion(uri, eve::Duration::fromNanoseconds(1000000000), 2);
    CHECK(!destroyed.ok());
}
