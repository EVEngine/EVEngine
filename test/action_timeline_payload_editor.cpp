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
