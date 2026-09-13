#include "action/editor/ActionTimelinePayloadEditor.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::LogicalId payloadId(const char* value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::action::ActionTimeline payloadTimeline() {
    eve::action::ActionTimeline timeline;
    timeline.actionId = payloadId("test:payload-editor");
    timeline.duration = eve::Duration::fromNanoseconds(100);
    eve::action::ActionTrack track;
    track.id    = payloadId("test-track:audio");
    track.label = "Audio";
    track.kind  = eve::action::ActionTrackKind::Audio;
    track.states.push_back({payloadId("test-state:audio"), payloadId("presentation:audio-state"),
                            eve::Duration::fromNanoseconds(10), eve::Duration::fromNanoseconds(50),
                            {{"uri", "asset://audio/swing.wav"}, {"extensionField", "preserved"}}});
    timeline.tracks.push_back(std::move(track));
    return timeline;
}

eve::action::ActionTimeline vfxPayloadTimeline() {
    eve::action::ActionTimeline timeline;
    timeline.actionId = payloadId("test:vfx-clip-fit");
    timeline.duration = eve::Duration::fromSeconds(5.0).value();
    eve::action::ActionTrack track;
    track.id    = payloadId("test-track:vfx");
    track.label = "VFX";
    track.kind  = eve::action::ActionTrackKind::Effect;
    track.states.push_back({payloadId("test-state:vfx"), payloadId("presentation:vfx-state"),
                            eve::Duration::fromSeconds(1.0).value(), eve::Duration::fromSeconds(1.5).value(),
                            {{"uri", "asset://vfx/slash.json"}, {"clipStartTime", 0.25},
                             {"clipEndTime", 1.25}, {"extensionField", "preserved"}}});
    timeline.tracks.push_back(std::move(track));
    return timeline;
}

}  // namespace

TEST_CASE("actionTimelinePayloadEditor.patchPreservesUnknownFieldsAndRejectsAtomically") {
    eve::editor::ActionTimelineEditor editor("test.payload-editor", payloadTimeline());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::editor::ActionTimelinePayloadEditor payloads(editor, registry.value());
    const auto itemId = payloadId("test-state:audio");

    REQUIRE(payloads.patch(itemId, {{"volume", 0.5}, {"looping", true},
                                    {"randomUris", eve::Value::Array{"asset://audio/a.wav",
                                                                      "asset://audio/b.wav"}},
                                    {"randomPitchOffset", 0.08}, {"spatialBlend", 0.5},
                                    {"minDistance", 2.0}, {"maxDistance", 24.0},
                                    {"fadeOutOnExit", true}, {"fadeOutDuration", 0.2}}).ok());
    auto edited = payloads.payload(itemId);
    REQUIRE(edited.ok());
    CHECK_EQ(*edited.value().at("extensionField").getIf<std::string>(), "preserved");
    CHECK_EQ(*edited.value().at("volume").getIf<double>(), 0.5);
    CHECK(*edited.value().at("looping").getIf<bool>());
    CHECK_EQ(edited.value().at("randomUris").getIf<eve::Value::Array>()->size(), 2U);

    const auto revision = editor.target().revision();
    CHECK(!payloads.patch(itemId, {{"pitch", 0.0}}).ok());
    CHECK_EQ(editor.target().revision(), revision);
    auto unchanged = payloads.payload(itemId);
    REQUIRE(unchanged.ok());
    CHECK(!unchanged.value().contains("pitch"));
    CHECK_EQ(*unchanged.value().at("maxDistance").getIf<double>(), 24.0);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(*payloads.payload(itemId).value().at("extensionField").getIf<std::string>(), "preserved");
}

TEST_CASE("actionTimelinePayloadEditor.vfxClipFitCommandsAreAtomicAndUndoable") {
    eve::editor::ActionTimelineEditor editor("test.vfx-clip-fit", vfxPayloadTimeline());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::editor::ActionTimelinePayloadEditor payloads(editor, registry.value());
    const auto itemId = payloadId("test-state:vfx");

    REQUIRE(payloads.fitBlockToClip(itemId).ok());
    const auto& fittedBlock = editor.target().timeline().tracks.front().states.front();
    CHECK_EQ(fittedBlock.start.seconds(), 1.0);
    CHECK_EQ(fittedBlock.end.seconds(), 2.0);
    CHECK_EQ(editor.target().revision(), 1ULL);

    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.target().timeline().tracks.front().states.front().end.seconds(), 1.5);
    REQUIRE(payloads.fitClipToBlock(itemId).ok());
    auto fittedClip = payloads.payload(itemId);
    REQUIRE(fittedClip.ok());
    CHECK_EQ(*fittedClip.value().at("clipStartTime").getIf<double>(), 0.25);
    CHECK_EQ(*fittedClip.value().at("clipEndTime").getIf<double>(), 0.75);
    CHECK_EQ(*fittedClip.value().at("extensionField").getIf<std::string>(), "preserved");
    REQUIRE(editor.undo().ok());
    CHECK_EQ(*payloads.payload(itemId).value().at("clipEndTime").getIf<double>(), 1.25);
}

TEST_CASE("actionTimelinePayloadEditor.vfxClipFitRejectsWrongShapeAndOutOfRangeWithoutMutation") {
    auto timeline = vfxPayloadTimeline();
    timeline.tracks.front().notifies.push_back(
        {payloadId("test-notify:vfx"), payloadId("presentation:vfx"), eve::Duration::fromSeconds(0.25).value(),
         {{"uri", "asset://vfx/slash.json"}, {"lifetimeSeconds", 1.0}}});
    timeline.tracks.front().states.front().start = eve::Duration::fromSeconds(4.5).value();
    timeline.tracks.front().states.front().end   = eve::Duration::fromSeconds(4.75).value();
    eve::editor::ActionTimelineEditor editor("test.vfx-clip-fit-reject", std::move(timeline));
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::editor::ActionTimelinePayloadEditor payloads(editor, registry.value());

    const auto revision = editor.target().revision();
    CHECK(!payloads.fitBlockToClip(payloadId("test-state:vfx")).ok());
    CHECK(!payloads.fitClipToBlock(payloadId("test-notify:vfx")).ok());
    CHECK_EQ(editor.target().revision(), revision);
    CHECK_EQ(editor.target().timeline().tracks.front().states.front().end.seconds(), 4.75);
}
