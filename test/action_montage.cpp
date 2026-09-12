#include "action/Action.h"
#include "action/ActionTimeline.h"
#include "action/editor/ActionTimelineEditor.h"
#include "action/editor/ActionTimelineWidget.h"
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/MontageCoordinator.h"
#include "animation/MontagePlayer.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>
#include <string_view>

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::action::ActionTimeline montageTimeline() {
    eve::action::ActionTimeline timeline;
    timeline.actionId          = id("combat:montage-test");
    timeline.duration          = eve::Duration::fromSeconds(2.0).takeValue();
    timeline.animationSections = {
        {id("montage-section:anticipation"), "memory://clips/anticipation", eve::Duration::zero(),
         eve::Duration::fromSeconds(1.0).takeValue(), eve::Duration::zero()},
        {id("montage-section:strike"), "memory://clips/strike", eve::Duration::fromSeconds(1.0).takeValue(),
         eve::Duration::fromSeconds(2.0).takeValue(), eve::Duration::fromSeconds(0.2).takeValue()},
    };
    timeline.splitTimestamps = {eve::Duration::fromSeconds(0.8).takeValue(),
                                eve::Duration::fromSeconds(1.2).takeValue()};
    eve::action::ActionTrack gameplay;
    gameplay.id    = id("combat-track:gameplay");
    gameplay.label = "Gameplay";
    gameplay.kind  = eve::action::ActionTrackKind::Gameplay;
    gameplay.states.push_back({id("combat-state:hit"),
                               id("combat:hitbox"),
                               eve::Duration::fromSeconds(0.8).takeValue(),
                               eve::Duration::fromSeconds(1.2).takeValue(),
                               {}});
    timeline.tracks.push_back(std::move(gameplay));
    return timeline;
}

std::unique_ptr<eve::animation::AnimClip> rootClip(std::string name, float distance) {
    auto clip = std::make_unique<eve::animation::AnimClip>(std::move(name));
    clip->setDuration(1.0f);
    clip->setLoop(false);
    clip->addPositionKey(0, 0.0f, 0.0f, 0.0f, 0.0f);
    clip->addPositionKey(0, 1.0f, distance, 0.0f, 0.0f);
    return clip;
}

std::vector<eve::animation::MontageClipAsset> montageClips() {
    std::vector<eve::animation::MontageClipAsset> clips;
    clips.push_back({"memory://clips/anticipation", rootClip("anticipation", 1.0f)});
    clips.push_back({"memory://clips/strike", rootClip("strike", 2.0f)});
    return clips;
}

class RootReceiver final : public eve::animation::IMontageRootMotionReceiver {
public:
    void applyMontageRootMotion(const eve::animation::TransformTRS& delta) noexcept override {
        x += delta.px;
        y += delta.py;
        ++calls;
    }

    float x     = 0.0f;
    float y     = 0.0f;
    int   calls = 0;
};

}  // namespace

TEST_CASE("actionMontage.schemaV2RoundTripAndV1Migration") {
    auto timeline                       = montageTimeline();
    timeline.montage.basePlayRate       = 1.25;
    timeline.montage.looping            = true;
    timeline.montage.footIk             = true;
    timeline.montage.animationLayer     = 2;
    timeline.montage.rootMotionVertical = true;
    auto encoded                        = timeline.toValue();
    REQUIRE(encoded.ok());
    auto decoded = eve::action::ActionTimeline::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().animationSections.size(), 2U);
    CHECK_EQ(decoded.value().animationSections[1].blendIn, eve::Duration::fromSeconds(0.2).takeValue());
    CHECK_EQ(decoded.value().sectionCount(), 3U);
    CHECK_EQ(decoded.value().montage.basePlayRate, 1.25);
    CHECK(decoded.value().montage.looping);
    CHECK(decoded.value().montage.footIk);
    CHECK_EQ(decoded.value().montage.animationLayer, 2U);
    auto middleRange = decoded.value().sectionRange(1);
    REQUIRE(middleRange.ok());
    CHECK_EQ(middleRange.value().first, eve::Duration::fromSeconds(0.8).takeValue());
    CHECK_EQ(middleRange.value().second, eve::Duration::fromSeconds(1.2).takeValue());

    auto  versionTwo       = encoded.value();
    auto* versionTwoObject = versionTwo.getIf<eve::Value::Object>();
    REQUIRE(versionTwoObject != nullptr);
    (*versionTwoObject)["schemaVersion"] = static_cast<std::int64_t>(2);
    versionTwoObject->erase("splitTimestampsNs");
    versionTwoObject->erase("montage");
    auto* sections = (*versionTwoObject)["animationSections"].getIf<eve::Value::Array>();
    REQUIRE(sections != nullptr);
    for (auto& section : *sections) {
        auto* object = section.getIf<eve::Value::Object>();
        REQUIRE(object != nullptr);
        object->erase("sourceStartNs");
        object->erase("sourceEndNs");
        object->erase("blendCurve");
    }
    auto migratedV2 = eve::action::ActionTimeline::fromValue(versionTwo);
    REQUIRE(migratedV2.ok());
    CHECK(migratedV2.value().splitTimestamps.empty());
    CHECK_EQ(migratedV2.value().animationSections[0].blendCurve, eve::action::ActionBlendCurve::EaseInOut);

    auto  legacy       = encoded.value();
    auto* legacyObject = legacy.getIf<eve::Value::Object>();
    REQUIRE(legacyObject != nullptr);
    (*legacyObject)["schemaVersion"] = static_cast<std::int64_t>(1);
    legacyObject->erase("animationSections");
    (*legacyObject)["animationUri"] = "memory://clips/legacy";
    auto migrated                   = eve::action::ActionTimeline::fromValue(legacy);
    REQUIRE(migrated.ok());
    REQUIRE_EQ(migrated.value().animationSections.size(), 1U);
    CHECK_EQ(migrated.value().animationSections[0].animationUri, "memory://clips/legacy");
}

TEST_CASE("actionMontage.clipTrimAndBlendCurveUseAuthoredSectionDuration") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    auto timeline                             = montageTimeline();
    timeline.animationSections[0].sourceStart = eve::Duration::fromSeconds(0.25).takeValue();
    timeline.animationSections[0].sourceEnd   = eve::Duration::fromSeconds(0.75).takeValue();
    timeline.animationSections[0].blendCurve  = eve::action::ActionBlendCurve::Linear;
    eve::animation::MontagePlayer                 player(skeleton);
    std::vector<eve::animation::MontageClipAsset> clips;
    clips.push_back({"memory://clips/anticipation", rootClip("anticipation", 1.0f)});
    clips.push_back({"memory://clips/strike", rootClip("strike", 2.0f)});
    REQUIRE(player.prepare(timeline, std::move(clips)).ok());
    REQUIRE(player.play(eve::action::ActionExecutionId(9)).ok());
    eve::action::ActionAdvance advance;
    advance.id           = eve::action::ActionExecutionId(9);
    advance.phase        = eve::action::ActionPhase::Active;
    advance.totalElapsed = eve::Duration::fromSeconds(0.5).takeValue();
    auto presented       = player.present(advance, eve::SimulationTick(1));
    REQUIRE(presented.ok());
    CHECK(std::fabs(player.pose().local(0).px - 0.5f) < 1e-4f);
}

TEST_CASE("actionMontage.lowFrameRateSweepCrossesEveryBoundaryAndSection") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::MontagePlayer                 player(skeleton);
    auto                                          timeline = montageTimeline();
    std::vector<eve::animation::MontageClipAsset> clips;
    clips.push_back({"memory://clips/anticipation", rootClip("anticipation", 1.0f)});
    clips.push_back({"memory://clips/strike", rootClip("strike", 2.0f)});
    REQUIRE(player.prepare(timeline, std::move(clips)).ok());
    RootReceiver receiver;
    player.setRootMotionReceiver(receiver);
    eve::action::ActionDefinition definition;
    definition.id            = timeline.actionId;
    definition.timing.active = timeline.duration;
    definition.timeline      = timeline;
    eve::action::ActionRequest request;
    request.actionId = definition.id;
    eve::action::ActionRuntime runtime;
    auto                       idResult = runtime.submit(std::move(definition), std::move(request));
    REQUIRE(idResult.ok());
    REQUIRE(player.play(idResult.value()).ok());

    auto actionAdvance =
        runtime.advance(idResult.value(), eve::SimulationTick(1), eve::Duration::fromSeconds(1.5).takeValue());
    REQUIRE(actionAdvance.ok());
    auto advanced = player.present(actionAdvance.value(), eve::SimulationTick(1));
    REQUIRE(advanced.ok());
    REQUIRE_EQ(advanced.value().events.size(), 2U);
    CHECK(static_cast<int>(advanced.value().events[0].kind) ==
          static_cast<int>(eve::action::ActionTimelineEventKind::StateEnter));
    CHECK(static_cast<int>(advanced.value().events[1].kind) ==
          static_cast<int>(eve::action::ActionTimelineEventKind::StateExit));
    CHECK_EQ(advanced.value().sectionId, std::optional<eve::LogicalId>(id("montage-section:strike")));
    CHECK(std::fabs(receiver.x - 2.0f) < 1e-4f);
    CHECK_EQ(receiver.calls, 1);
}

TEST_CASE("actionMontage.jumpSectionProgressAndInterruptedBlendOutKeepBlockPairs") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::MontagePlayer                 player(skeleton);
    auto                                          timeline = montageTimeline();
    std::vector<eve::animation::MontageClipAsset> clips;
    clips.push_back({"memory://clips/anticipation", rootClip("anticipation", 1.0f)});
    clips.push_back({"memory://clips/strike", rootClip("strike", 2.0f)});
    REQUIRE(player.prepare(timeline, std::move(clips)).ok());
    const eve::action::ActionExecutionId execution(5);
    REQUIRE(player.play(execution).ok());

    eve::action::ActionAdvance advance;
    advance.id           = execution;
    advance.phase        = eve::action::ActionPhase::Active;
    advance.totalElapsed = eve::Duration::fromSeconds(0.9).takeValue();
    auto entered         = player.present(advance, eve::SimulationTick(1));
    REQUIRE(entered.ok());
    REQUIRE_EQ(entered.value().activeBlocks.size(), 1U);
    CHECK_EQ(entered.value().activeBlocks[0].itemId, id("combat-state:hit"));

    auto jumped = player.jumpToTime(execution, eve::Duration::fromSeconds(1.5).takeValue(), eve::SimulationTick(2));
    REQUIRE(jumped.ok());
    REQUIRE_EQ(jumped.value().events.size(), 1U);
    CHECK_EQ(jumped.value().events[0].kind, eve::action::ActionTimelineEventKind::StateExit);
    CHECK(jumped.value().activeBlocks.empty());
    auto physical = player.physicalSectionIndex();
    REQUIRE(physical.ok());
    CHECK_EQ(physical.value(), 2U);

    auto evaluated = player.evaluateSectionProgress(execution, 1, 0.5, eve::SimulationTick(3));
    REQUIRE(evaluated.ok());
    CHECK_EQ(evaluated.value().current, eve::Duration::fromSeconds(1.0).takeValue());
    REQUIRE_EQ(evaluated.value().events.size(), 1U);
    CHECK_EQ(evaluated.value().events[0].kind, eve::action::ActionTimelineEventKind::StateEnter);

    auto stopping = player.beginBlendOut(eve::Duration::fromSeconds(0.2).takeValue(), eve::SimulationTick(4));
    REQUIRE(stopping.ok());
    REQUIRE_EQ(stopping.value().events.size(), 1U);
    CHECK_EQ(stopping.value().events[0].kind, eve::action::ActionTimelineEventKind::StateExit);
    auto half = player.advanceBlendOut(eve::Duration::fromSeconds(0.1).takeValue(), eve::SimulationTick(5));
    REQUIRE(half.ok());
    CHECK(std::fabs(half.value().weight - 0.5) < 1e-6);
    CHECK(!half.value().completed);
    auto finished = player.advanceBlendOut(eve::Duration::fromSeconds(0.1).takeValue(), eve::SimulationTick(6));
    REQUIRE(finished.ok());
    CHECK(finished.value().completed);
    CHECK(!player.isPlaying());
}

TEST_CASE("actionMontage.editorSectionsUseCanonicalUndoableTransactions") {
    eve::editor::ActionTimelineEditor editor("asset.combat.montage", montageTimeline());
    REQUIRE(editor.renameTrack(id("combat-track:gameplay"), "Damage Windows").ok());
    REQUIRE(editor.setTrackLocked(id("combat-track:gameplay"), true).ok());
    CHECK(!editor.moveItem(id("combat-state:hit"), eve::Duration::fromSeconds(0.1).takeValue()).ok());
    REQUIRE(editor.setTrackLocked(id("combat-track:gameplay"), false).ok());
    REQUIRE(editor.setTrackMuted(id("combat-track:gameplay"), true).ok());
    REQUIRE(editor.undo().ok());
    CHECK(!editor.target().timeline().tracks[0].muted);
    auto moved = editor.moveAnimationSection(id("montage-section:strike"), eve::Duration::fromSeconds(0.1).takeValue());
    CHECK(!moved.ok());
    CHECK_EQ(editor.target().timeline().animationSections[1].start, eve::Duration::fromSeconds(1.0).takeValue());

    REQUIRE(editor.removeAnimationSection(id("montage-section:strike")).ok());
    REQUIRE_EQ(editor.target().timeline().animationSections.size(), 1U);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.target().timeline().animationSections.size(), 2U);

    REQUIRE(editor.removeAnimationSection(id("montage-section:strike")).ok());
    REQUIRE(editor.selectItem(id("montage-section:anticipation"), false).ok());
    auto copied = editor.copySelection();
    REQUIRE(copied.ok());
    CHECK_EQ(copied.value(), 1U);
    auto pasted = editor.paste(eve::Duration::fromSeconds(1.0).takeValue());
    REQUIRE(pasted.ok());
    CHECK_EQ(pasted.value(), 1U);
    CHECK_EQ(editor.target().timeline().animationSections.size(), 2U);

    eve::action::ActionNotifyRegistry registry;
    eve::editor::ActionTimelineWidget widget(editor, registry);
    REQUIRE(widget.setViewport(800.0f, 24.0f).ok());
    REQUIRE(widget.setSnapInterval(eve::Duration::fromSeconds(0.25).takeValue()).ok());
    REQUIRE(widget.seek(300.0f).ok());
    CHECK_EQ(editor.previewTime(), eve::Duration::fromSeconds(0.5).takeValue());
    const auto layout = widget.layout();
    CHECK_EQ(layout.height, 48.0f);
    CHECK_EQ(layout.items.size(), 3U);
    CHECK_EQ(layout.rulerTicks.size(), 9U);
    REQUIRE(widget.zoom(2.0, 0.5).ok());
    REQUIRE(widget.seek(120.0f).ok());
    CHECK_EQ(editor.previewTime(), eve::Duration::fromSeconds(0.5).takeValue());
    REQUIRE(widget.pan(eve::Duration::fromSeconds(0.25).takeValue()).ok());
    REQUIRE(widget.seek(120.0f).ok());
    CHECK_EQ(editor.previewTime(), eve::Duration::fromSeconds(0.75).takeValue());
}

TEST_CASE("actionMontage.coordinatorPingPongsSlotsAndRejectsStaleHandles") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::MontageCoordinator coordinator(skeleton);
    auto                               firstTimeline = montageTimeline();
    firstTimeline.montage.defaultBlendIn             = eve::Duration::fromSeconds(0.2).takeValue();
    auto first =
        coordinator.play(0, firstTimeline, montageClips(), eve::action::ActionExecutionId(11), eve::SimulationTick(1));
    REQUIRE(first.ok());
    eve::action::ActionAdvance advance;
    advance.id           = eve::action::ActionExecutionId(11);
    advance.phase        = eve::action::ActionPhase::Active;
    advance.totalElapsed = eve::Duration::fromSeconds(0.5).takeValue();
    REQUIRE(coordinator.present(first.value(), advance, eve::SimulationTick(1)).ok());

    auto secondTimeline                   = montageTimeline();
    secondTimeline.montage.defaultBlendIn = eve::Duration::fromSeconds(0.2).takeValue();
    auto second =
        coordinator.play(0, secondTimeline, montageClips(), eve::action::ActionExecutionId(12), eve::SimulationTick(2));
    REQUIRE(second.ok());
    CHECK_NE(first.value().index(), second.value().index());
    REQUIRE(coordinator.resolve(first.value()).ok());
    REQUIRE(coordinator.advanceBlendOuts(eve::Duration::fromSeconds(0.1).takeValue(), eve::SimulationTick(3)).ok());
    auto layerPose = coordinator.pose(0);
    REQUIRE(layerPose.ok());
    REQUIRE(coordinator.advanceBlendOuts(eve::Duration::fromSeconds(0.1).takeValue(), eve::SimulationTick(4)).ok());
    auto stale = coordinator.resolve(first.value());
    CHECK(!stale.ok());

    auto third = coordinator.play(0, montageTimeline(), montageClips(), eve::action::ActionExecutionId(13),
                                  eve::SimulationTick(5));
    REQUIRE(third.ok());
    CHECK_EQ(third.value().index(), first.value().index());
    CHECK_NE(third.value().generation(), first.value().generation());
}

TEST_CASE("actionMontage.hotReloadIsTransactionalAndPreservesCursor") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::MontagePlayer player(skeleton);
    REQUIRE(player.prepare(montageTimeline(), montageClips()).ok());
    const eve::action::ActionExecutionId execution(17);
    REQUIRE(player.play(execution).ok());
    eve::action::ActionAdvance advance;
    advance.id           = execution;
    advance.phase        = eve::action::ActionPhase::Active;
    advance.totalElapsed = eve::Duration::fromSeconds(0.5).takeValue();
    REQUIRE(player.present(advance, eve::SimulationTick(1)).ok());
    CHECK(std::fabs(player.pose().local(0).px - 0.5f) < 1e-4f);

    auto invalidClip = rootClip("invalid", 9.0f);
    invalidClip->setDuration(0.0f);
    CHECK(!player.replaceClip("memory://clips/anticipation", std::move(invalidClip)).ok());
    CHECK(std::fabs(player.pose().local(0).px - 0.5f) < 1e-4f);

    REQUIRE(player.replaceClip("memory://clips/anticipation", rootClip("replacement", 3.0f)).ok());
    CHECK_EQ(player.time(), eve::Duration::fromSeconds(0.5).takeValue());
    CHECK(std::fabs(player.pose().local(0).px - 1.5f) < 1e-4f);
}
