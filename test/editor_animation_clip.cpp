#include "animation_editing/AnimationClip.h"

#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::animation_editing;
using namespace eve::editing;

namespace {
AnimationTransformKey key(const char* id, double time, double x) {
    AnimationTransformKey result;
    result.id = StableId(id); result.time = time; result.positionX = x;
    return result;
}
}

TEST_CASE("editor.animation.clip_timeline_supports_stable_reversible_tracks_events_and_masks") {
    AnimationClipDocumentTarget clip("walk");
    auto settings = clip.makeSetSettings(2.0, 60.0, true); REQUIRE(settings.ok());
    CHECK(clip.applyDomainOperation(settings.value()).ok());
    AnimationBoneTrack track{StableId("hips-track"), "mixamorig:Hips", {key("start", 0.0, 0.0), key("end", 2.0, 4.0)}};
    auto setTrack = clip.makeSetTrack(track); REQUIRE(setTrack.ok()); CHECK(clip.applyDomainOperation(setTrack.value()).ok());
    auto event = clip.makeSetEvent({StableId("footstep"), 0.5, "footstep", "left"}); REQUIRE(event.ok()); CHECK(clip.applyDomainOperation(event.value()).ok());
    auto mask = clip.makeSetMask({"mixamorig:Hips", 0.25}); REQUIRE(mask.ok()); CHECK(clip.applyDomainOperation(mask.value()).ok());
    const auto preview = clip.preview(1.0, {"mixamorig:Hips"});
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied)); REQUIRE_EQ(preview.bones.size(), 1U);
    CHECK_EQ(preview.bones[0].positionX, 2.0); CHECK_EQ(preview.bones[0].maskWeight, 0.25);
    auto remove = clip.makeDeleteTrack(StableId("hips-track")); REQUIRE(remove.ok()); CHECK(clip.applyDomainOperation(remove.value()).ok());
    DomainOperation undo = remove.value(); undo.type = remove.value().inverseType; undo.payload = remove.value().inverse;
    CHECK(clip.applyDomainOperation(undo).ok()); CHECK_EQ(clip.tracks().size(), 1U);
}

TEST_CASE("editor.animation.clip_snapshot_and_retarget_preview_are_non_destructive") {
    AnimationClipDocumentTarget clip("source");
    auto track = clip.makeSetTrack({StableId("hips"), "mixamorig:Hips", {key("key", 0.0, 1.0)}}); REQUIRE(track.ok()); CHECK(clip.applyDomainOperation(track.value()).ok());
    const auto mapping = clip.previewRetarget({"hips", "spine"});
    CHECK_EQ(static_cast<int>(mapping.status), static_cast<int>(EditorStatus::Applied)); CHECK_EQ(mapping.mapping.at("mixamorig:Hips"), "hips");
    AnimationClipDocumentTarget restored("source"); CHECK(restored.loadSnapshot(clip.snapshotValue()).ok()); CHECK_EQ(restored.tracks().size(), 1U);
}

TEST_CASE("editor.animation.clip_builds_real_runtime_asset_and_rejects_missing_bones") {
    AnimationClipDocumentTarget document("run");
    auto track = document.makeSetTrack({StableId("root-track"), "root", {key("a", 0.0, 0.0), key("b", 1.0, 2.0)}}); REQUIRE(track.ok()); CHECK(document.applyDomainOperation(track.value()).ok());
    auto event = document.makeSetEvent({StableId("event"), 0.25, "step", "right"}); REQUIRE(event.ok()); CHECK(document.applyDomainOperation(event.value()).ok());
    eve::animation::AnimSkeleton skeleton; skeleton.addBone("root"); AnimationClipRuntimeBuilder builder;
    auto built = builder.build(document, &skeleton); REQUIRE(built.ok()); CHECK_EQ(built.value()->getPositionKeyCount(0), 2); CHECK_EQ(built.value()->getEventCount(), 1); delete built.value();
    eve::animation::AnimSkeleton wrong; wrong.addBone("pelvis"); CHECK_EQ(static_cast<int>(builder.build(document, &wrong).code()), static_cast<int>(EditorStatus::Rejected));
}
