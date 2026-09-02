#include "action_editor/ActionPreviewController.h"

#include "common/Diagnostic.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string_view>

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::action::ActionTimeline timelineFixture() {
    eve::action::ActionTimeline timeline;
    timeline.actionId     = id("combat:preview-test");
    timeline.duration     = eve::Duration::fromNanoseconds(100);
    timeline.animationUri = "asset://animations/preview-test.eva";

    eve::action::ActionTrack track;
    track.id    = id("combat-track:presentation");
    track.label = "Presentation";
    track.kind  = eve::action::ActionTrackKind::Effect;
    track.notifies.push_back({id("combat-notify:vfx"), id("presentation:vfx"), eve::Duration::fromNanoseconds(20), {}});
    track.notifies.push_back(
        {id("combat-notify:audio"), id("presentation:audio"), eve::Duration::fromNanoseconds(30), {}});
    track.notifies.push_back(
        {id("combat-notify:camera"), id("presentation:camera"), eve::Duration::fromNanoseconds(40), {}});
    track.states.push_back({id("combat-state:hitbox"),
                            id("combat:hitbox-window"),
                            eve::Duration::fromNanoseconds(10),
                            eve::Duration::fromNanoseconds(50),
                            {}});
    timeline.tracks.push_back(std::move(track));
    return timeline;
}

class RecordingSink final : public eve::action::IActionPreviewSink {
public:
    [[nodiscard]] eve::Result<void> prepare(const eve::action::ActionPreviewFrame& frame) override {
        ++prepareCount;
        prepared = frame;
        if (rejectPrepare)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "preview resources unavailable"));
        return eve::Result<void>::success();
    }

    void present(const eve::action::ActionPreviewFrame& frame) noexcept override {
        ++presentCount;
        presented = frame;
    }

    void discardPrepared() noexcept override { ++discardCount; }

    bool                                           rejectPrepare = false;
    int                                            prepareCount  = 0;
    int                                            presentCount  = 0;
    int                                            discardCount  = 0;
    std::optional<eve::action::ActionPreviewFrame> prepared;
    std::optional<eve::action::ActionPreviewFrame> presented;
};

class RecordingRootMotion final : public eve::action::IActionRootMotionSource {
public:
    [[nodiscard]] eve::Result<std::vector<eve::action::ActionPreviewPoint3>> sampleRootMotion(
        std::string_view animationUri, eve::Duration duration, std::uint32_t sampleCount) const override {
        ++calls;
        lastAnimationUri = animationUri;
        lastDuration     = duration;
        lastSampleCount  = sampleCount;
        if (reject)
            return eve::Result<std::vector<eve::action::ActionPreviewPoint3>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "animation cannot be sampled"));
        return eve::Result<std::vector<eve::action::ActionPreviewPoint3>>::success(
            {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 2.0f}, {3.0f, 1.0f, 2.0f}});
    }

    mutable bool          reject = false;
    mutable int           calls  = 0;
    mutable std::string   lastAnimationUri;
    mutable eve::Duration lastDuration;
    mutable std::uint32_t lastSampleCount = 0;
};

class RecordingOverlay final : public eve::editor::IEditorOverlay {
public:
    void line(const eve::editor::OverlayPoint& from, const eve::editor::OverlayPoint& to,
              const eve::editor::OverlayStyle&) override {
        ++lines;
        first = from;
        last  = to;
    }
    void circle(const eve::editor::OverlayPoint&, float, const eve::editor::OverlayStyle&) override {}
    void rectangle(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
                   const eve::editor::OverlayStyle&) override {}
    void text(const eve::editor::OverlayPoint&, const std::string&, const eve::editor::OverlayStyle&) override {}

    int                       lines = 0;
    eve::editor::OverlayPoint first;
    eve::editor::OverlayPoint last;
};

}  // namespace

TEST_CASE("actionPreviewController.explicitlyReportsMissingRootMotionProvider") {
    eve::editor::ActionTimelineEditor    editor("asset.combat.preview", timelineFixture());
    RecordingSink                        sink;
    eve::editor::ActionPreviewController preview(editor, sink);

    REQUIRE(preview.refresh().ok());
    REQUIRE(sink.presented.has_value());
    CHECK(static_cast<int>(sink.presented->rootMotionState) ==
          static_cast<int>(eve::action::RootMotionPreviewState::Unavailable));
    CHECK(sink.presented->rootMotionPath.empty());
}

TEST_CASE("actionPreviewController.samplesAndDrawsOwningRootMotionPath") {
    eve::editor::ActionTimelineEditor    editor("asset.combat.preview", timelineFixture());
    RecordingSink                        sink;
    RecordingRootMotion                  rootMotion;
    eve::editor::ActionPreviewController preview(editor, sink, &rootMotion);
    REQUIRE(preview.setRootMotionSampleCount(32).ok());

    REQUIRE(preview.refresh().ok());
    CHECK_EQ(rootMotion.calls, 1);
    CHECK_EQ(rootMotion.lastAnimationUri, "asset://animations/preview-test.eva");
    CHECK_EQ(rootMotion.lastDuration, eve::Duration::fromNanoseconds(100));
    CHECK_EQ(rootMotion.lastSampleCount, 32u);
    REQUIRE(sink.presented.has_value());
    CHECK(static_cast<int>(sink.presented->rootMotionState) ==
          static_cast<int>(eve::action::RootMotionPreviewState::Available));

    RecordingOverlay overlay;
    preview.drawRootMotion(overlay, {10.0f, 20.0f, 30.0f}, 2.0f);
    CHECK_EQ(overlay.lines, 2);
    CHECK_EQ(overlay.first.x, 12.0f);
    CHECK_EQ(overlay.last.x, 16.0f);
    CHECK_EQ(overlay.last.y, 22.0f);
}

TEST_CASE("actionPreviewController.routesPresentationAndStateBoundaryCues") {
    eve::editor::ActionTimelineEditor    editor("asset.combat.preview", timelineFixture());
    RecordingSink                        sink;
    eve::editor::ActionPreviewController preview(editor, sink);
    editor.play();

    auto advanced = preview.update(eve::Duration::fromNanoseconds(40));
    REQUIRE(advanced.ok());
    REQUIRE_EQ(advanced.value(), 4u);
    REQUIRE(sink.presented.has_value());
    CHECK(static_cast<int>(sink.presented->reason) == static_cast<int>(eve::action::ActionPreviewReason::Advance));
    REQUIRE_EQ(sink.presented->cues.size(), 4u);
    CHECK(static_cast<int>(sink.presented->cues[0].kind) ==
          static_cast<int>(eve::action::ActionPreviewCueKind::StateEnter));
    CHECK(static_cast<int>(sink.presented->cues[1].kind) == static_cast<int>(eve::action::ActionPreviewCueKind::Vfx));
    CHECK(static_cast<int>(sink.presented->cues[2].kind) == static_cast<int>(eve::action::ActionPreviewCueKind::Audio));
    CHECK(static_cast<int>(sink.presented->cues[3].kind) ==
          static_cast<int>(eve::action::ActionPreviewCueKind::Camera));
    CHECK_EQ(editor.previewTime(), eve::Duration::fromNanoseconds(40));
}

TEST_CASE("actionPreviewController.prepareFailureLeavesTransportUnchanged") {
    eve::editor::ActionTimelineEditor editor("asset.combat.preview", timelineFixture());
    RecordingSink                     sink;
    sink.rejectPrepare = true;
    eve::editor::ActionPreviewController preview(editor, sink);
    editor.play();

    auto advanced = preview.update(eve::Duration::fromNanoseconds(40));
    CHECK(!advanced.ok());
    CHECK_EQ(editor.previewTime(), eve::Duration::zero());
    CHECK_EQ(sink.prepareCount, 1);
    CHECK_EQ(sink.presentCount, 0);
}

TEST_CASE("actionTimelineEditor.rejectsStaleAndForgedPreviewPlans") {
    eve::editor::ActionTimelineEditor editor("asset.combat.preview", timelineFixture());
    editor.play();
    auto stale = editor.planPreview(eve::Duration::fromNanoseconds(40));
    REQUIRE(stale.ok());
    REQUIRE(editor
                .addNotify(id("combat-track:presentation"), {id("combat-notify:late"),
                                                             id("gameplay:event"),
                                                             eve::Duration::fromNanoseconds(60),
                                                             {{"tag", eve::Value("Combat.Late")}}})
                .ok());
    auto staleCommit = editor.applyPreviewPlan(std::move(stale.value()));
    CHECK(!staleCommit.ok());
    CHECK_EQ(editor.previewTime(), eve::Duration::zero());

    auto forged = editor.planPreview(eve::Duration::fromNanoseconds(40));
    REQUIRE(forged.ok());
    forged.value().events.clear();
    auto forgedCommit = editor.applyPreviewPlan(std::move(forged.value()));
    CHECK(!forgedCommit.ok());
    CHECK_EQ(editor.previewTime(), eve::Duration::zero());
}

TEST_CASE("actionPreviewController.seekValidatesBeforePreparing") {
    eve::editor::ActionTimelineEditor    editor("asset.combat.preview", timelineFixture());
    RecordingSink                        sink;
    eve::editor::ActionPreviewController preview(editor, sink);

    CHECK(!preview.seek(eve::Duration::fromNanoseconds(101)).ok());
    CHECK_EQ(sink.prepareCount, 0);
    REQUIRE(preview.seek(eve::Duration::fromNanoseconds(60)).ok());
    CHECK_EQ(sink.prepareCount, 1);
    CHECK_EQ(sink.presentCount, 1);
    CHECK_EQ(editor.previewTime(), eve::Duration::fromNanoseconds(60));
    REQUIRE(sink.presented.has_value());
    CHECK(sink.presented->cues.empty());
}
