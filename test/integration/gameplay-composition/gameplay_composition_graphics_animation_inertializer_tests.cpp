// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "cases": {
    "platform-file": [
      "animClip.reloadPathRefreshesRegisteredClips",
      "animClip.reloadUnknownOrBrokenPathIsNoop"
    ],
    "concurrency": [
      "audio.streamSource.pump",
      "audio.source.seekTell",
      "audio.modplug.midi.play"
    ],
    "script-binding": [
      "audio.pcgZone.squirrelContract"
    ]
  },
  "module": "action_editor",
  "modules": [
    "action_editor",
    "action_game_event",
    "agent",
    "agent_tensor",
    "animation",
    "animation_editing",
    "animation_tensor",
    "attributes",
    "audio_editing",
    "card"
  ],
  "origins": [
    "action_editor_adapter.cpp",
    "action_game_event.cpp",
    "action_montage.cpp",
    "action_preview_controller.cpp",
    "action_timeline.cpp",
    "action_timeline_payload_editor.cpp",
    "action_timeline_widget.cpp",
    "agent_composition.cpp",
    "agent_tensor.cpp",
    "animation_action_preview.cpp",
    "animation_inertializer.cpp",
    "animation_smr_neural_retarget.cpp",
    "attribute_domain_adapters.cpp",
    "attributes_rpg_unification.cpp",
    "audio.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_editor_adapter)
#include "PrivateTestScope.h"
#include "action/editor/ActionEditorModule.h"
#include "action/editor/ActionTimelineEditor.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// action_editor_adapter.cpp
namespace test_action_editor_adapter {
using namespace eve::editor;

namespace {

SelectionSnapshot documentSelection(const ActionTimelineTarget& target) {
    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    item.type   = "action.timeline";
    SelectionSnapshot selection;
    selection.channel = "asset";
    selection.items   = {item};
    selection.primary = item;
    return selection;
}

}  // namespace

TEST_CASE("actionEditor.seedTargetExposesCanonicalProperties") {
    ActionTimelineTarget target("combat.light-attack");
    CHECK_EQ(target.describe().type, std::string("action-timeline"));
    CHECK_EQ(target.timeline().actionId.format(), std::string("action:combat.light-attack"));
    CHECK_EQ(target.timeline().duration.nanoseconds(), 1'000'000'000);

    const auto selection = documentSelection(target);
    auto       operation = target.makeSet(selection, PropertyPath("timeline.animationUri"),
                                          EditorValue("asset://animations/light-attack.eva"), PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(target.timeline().animationUri, std::string("asset://animations/light-attack.eva"));
    CHECK_EQ(target.revision(), 1u);

    auto        snapshot = target.snapshotValue();
    const auto* root     = snapshot.getIf<EditorValue::Object>();
    REQUIRE(root != nullptr);
    CHECK_EQ(root->at("schema").getIf<std::string>() ? *root->at("schema").getIf<std::string>() : std::string{},
             std::string("eve.action.timeline"));
}

TEST_CASE("actionEditor.module_registers_automation_target_and_property_command") {
    Editor                                 editor;
    eve::action_editor::ActionEditorModule adapter;
    auto*                                  automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("action.property.set.v1") != std::string::npos);

    const std::string created = automation->invoke(
        "target-create",
        R"({"target":"agent.action","type":"action","animationUri":"asset://test/attack.glb#Attack"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string changed = automation->invoke(
        "execute",
        R"({"target":"agent.action","command":"action.property.set.v1","payload":{"path":"timeline.animationUri","value":"asset://test/idle.glb#Idle"}})");
    CHECK(changed.find("\"status\":\"applied\"") != std::string::npos);

    const std::string closed = automation->invoke("target-close", R"({"target":"agent.action"})");
    CHECK(closed.find("\"status\":\"applied\"") != std::string::npos);
}
}  // namespace test_action_editor_adapter
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_game_event)
#include "PrivateTestScope.h"
#include <chrono>
#include <cstdint>
#include <span>
#include "action/ActionGameplayEventBlock.h"
#include "action/ActionNotifyRegistry.h"
#include "action/game_event/ActionGameEventSink.h"
#include "game_event/GameEvent.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// action_game_event.cpp
namespace test_action_game_event {
namespace {

eve::SubjectRef subject(const char* text) {
    auto parsed = eve::PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

eve::LogicalId id(const char* text) {
    auto parsed = eve::LogicalId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

}  // namespace

TEST_CASE("actionGameEvent.validatesAndAppendsToPersistentLog") {
    eve::Value::Object invalidPayload{{"tag", eve::Value("not canonical")}};
    CHECK(!eve::action::ActionGameplayEventBinding::fromPayload(invalidPayload).ok());

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionTimelineEvent event;
    event.kind    = eve::action::ActionTimelineEventKind::Notify;
    event.trackId = id("action-track:gameplay");
    event.itemId  = id("action-item:event");
    event.type    = id("gameplay:event");
    event.payload = {{"tag", eve::Value("Combat.Action.Hit")},
                     {"targetIndex", eve::Value(std::int64_t{0})},
                     {"data", eve::Value(eve::Value::Object{{"power", eve::Value(std::int64_t{7})}})}};

    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId(71);
    ecs::EntityHandle sourceHandle{nullptr, typeid(void), 3, 1};
    ecs::EntityHandle targetHandle{nullptr, typeid(void), 5, 2};
    context.source = sourceHandle;
    context.targets.push_back(targetHandle);
    context.tick = eve::SimulationTick(42);
    auto absent  = registry.value().dispatch(event, context);
    CHECK(!absent.ok());
    CHECK_EQ(absent.status().code(), eve::StatusCode::NotFound);

    const auto timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(1710000000123));
    eve::game_event::GameEventLog log(
        [](std::span<std::uint8_t> bytes) {
            for (std::size_t index = 0; index < bytes.size(); ++index)
                bytes[index] = static_cast<std::uint8_t>(index + 1);
            return true;
        },
        [timestamp] { return timestamp; });
    const auto sourceSubject = subject("018f0f4e-6b3c-7ab1-8def-0123456789ab");
    const auto targetSubject = subject("018f0f4e-6b3c-7ab2-8def-0123456789ab");
    eve::action::game_event_adapter::ActionGameEventSink sink(
        log, [=](ecs::EntityHandle handle) -> eve::Result<eve::SubjectRef> {
            if (handle.id == sourceHandle.id) return eve::Result<eve::SubjectRef>::success(sourceSubject);
            if (handle.id == targetHandle.id) return eve::Result<eve::SubjectRef>::success(targetSubject);
            return eve::Result<eve::SubjectRef>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "entity is stale", "entity"));
        });
    sink.setEnabled(true);
    REQUIRE(registry.value().dispatch(event, context).ok());
    REQUIRE(sink.lastSequence().has_value());
    const auto* stored = log.find(*sink.lastSequence());
    REQUIRE(stored != nullptr);
    CHECK_EQ(stored->type, std::string("Combat.Action.Hit"));
    CHECK_EQ(stored->source, sourceSubject.format());
    CHECK_EQ(stored->subject, targetSubject.format());
    CHECK_EQ(stored->schemaId, id("action:gameplay-event-v1"));
    CHECK_EQ(stored->schemaVersion.value(), 1U);
    CHECK_EQ(stored->tick, eve::SimulationTick(42));
    CHECK_EQ(stored->payload, std::string("{\"power\":7}"));

    sink.setEnabled(false);
    CHECK(!registry.value().dispatch(event, context).ok());
    CHECK_EQ(log.size(), 1);
}
}  // namespace test_action_game_event
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_montage)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include <memory>
#include <string_view>
#include "action/Action.h"
#include "action/ActionTimeline.h"
#include "action/editor/ActionTimelineEditor.h"
#include "action/editor/ActionTimelineWidget.h"
#include "animation/AnimClip.h"
#include "animation/AnimLayerMixer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/MontageCoordinator.h"
#include "animation/MontagePlayer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// action_montage.cpp
namespace test_action_montage {
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

std::vector<eve::animation::MontageClipAsset> maskedMontageClips() {
    auto clip = std::make_unique<eve::animation::AnimClip>("masked");
    clip->setDuration(1.0f);
    clip->setLoop(false);
    clip->addPositionKey(0, 0.0f, 0.0f, 0.0f, 0.0f);
    clip->addPositionKey(0, 1.0f, 2.0f, 0.0f, 0.0f);
    clip->addPositionKey(1, 0.0f, 0.0f, 1.0f, 0.0f);
    clip->addPositionKey(1, 1.0f, 0.0f, 3.0f, 0.0f);
    std::vector<eve::animation::MontageClipAsset> clips;
    clips.push_back({"memory://clips/anticipation", std::move(clip)});
    clips.push_back({"memory://clips/strike", rootClip("unused", 0.0f)});
    return clips;
}

class RootReceiver final : public eve::animation::IMontageRootMotionReceiver {
public:
    void applyMontageRootMotion(const eve::animation::TransformTRS& delta) noexcept override {
        x += delta.px;
        y += delta.py;
        z += delta.pz;
        ++calls;
    }

    float x     = 0.0f;
    float y     = 0.0f;
    float z     = 0.0f;
    int   calls = 0;
};

}  // namespace

TEST_CASE("actionMontage.schemaV5RoundTripAndLegacyMigration") {
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
    CHECK_EQ(decoded.value().animationSections[1].blendOut, eve::Duration::zero());
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
        object->erase("blendOutNs");
        object->erase("blendInCurve");
        object->erase("blendOutCurve");
    }
    auto* tracks = (*versionTwoObject)["tracks"].getIf<eve::Value::Array>();
    REQUIRE(tracks != nullptr);
    for (auto& track : *tracks) {
        auto* trackObject = track.getIf<eve::Value::Object>();
        REQUIRE(trackObject != nullptr);
        for (const char* collection : {"notifies", "states"}) {
            auto* items = (*trackObject)[collection].getIf<eve::Value::Array>();
            REQUIRE(items != nullptr);
            for (auto& item : *items) {
                auto* itemObject = item.getIf<eve::Value::Object>();
                REQUIRE(itemObject != nullptr);
                itemObject->erase("enabled");
            }
        }
    }
    auto migratedV2 = eve::action::ActionTimeline::fromValue(versionTwo);
    REQUIRE(migratedV2.ok());
    CHECK(migratedV2.value().splitTimestamps.empty());
    CHECK_EQ(migratedV2.value().animationSections[0].blendInCurve, eve::action::ActionBlendCurve::EaseInOut);
    CHECK(migratedV2.value().tracks[0].states[0].enabled);

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

    auto  versionFour       = encoded.value();
    auto* versionFourObject = versionFour.getIf<eve::Value::Object>();
    REQUIRE(versionFourObject != nullptr);
    (*versionFourObject)["schemaVersion"] = static_cast<std::int64_t>(4);
    auto* versionFourSections = (*versionFourObject)["animationSections"].getIf<eve::Value::Array>();
    REQUIRE(versionFourSections != nullptr);
    for (auto& section : *versionFourSections) {
        auto* object = section.getIf<eve::Value::Object>();
        REQUIRE(object != nullptr);
        (*object)["blendCurve"] = (*object)["blendInCurve"];
        object->erase("blendOutNs");
        object->erase("blendInCurve");
        object->erase("blendOutCurve");
    }
    auto migratedV4 = eve::action::ActionTimeline::fromValue(versionFour);
    REQUIRE(migratedV4.ok());
    CHECK_EQ(migratedV4.value().animationSections[0].blendOut,
             migratedV4.value().montage.defaultBlendOut);
    CHECK_EQ(migratedV4.value().animationSections[0].blendInCurve,
             eve::action::ActionBlendCurve::EaseInOut);
    CHECK_EQ(migratedV4.value().animationSections[0].blendOutCurve,
             eve::action::ActionBlendCurve::EaseInOut);
}

TEST_CASE("actionMontage.settingsUpdateIsValidatedAndPreservesPreparedPlayback") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::MontagePlayer player(skeleton);
    REQUIRE(player.prepare(montageTimeline(), montageClips()).ok());
    REQUIRE(player.play(eve::action::ActionExecutionId(77)).ok());

    auto settings               = montageTimeline().montage;
    settings.basePlayRate       = 1.5;
    settings.animationLayer     = 3;
    settings.footIk             = true;
    settings.rootMotionVertical = true;
    REQUIRE(player.setSettings(settings).ok());
    CHECK(player.isPlaying());
    CHECK_EQ(player.animationLayer(), 3U);
    CHECK(player.footIkEnabled());

    settings.basePlayRate = 0.0;
    CHECK(!player.setSettings(settings).ok());
    CHECK_EQ(player.animationLayer(), 3U);
    CHECK(player.footIkEnabled());
}

TEST_CASE("actionMontage.sectionSplitUpdateIsAtomicAndPreservesPreparedPlayback") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::MontagePlayer player(skeleton);
    REQUIRE(player.prepare(montageTimeline(), montageClips()).ok());
    REQUIRE(player.play(eve::action::ActionExecutionId(78)).ok());

    REQUIRE(player
                .setSectionSplits(
                    {eve::Duration::fromSeconds(0.25).takeValue(), eve::Duration::fromSeconds(1.5).takeValue()})
                .ok());
    REQUIRE(player.jumpToSection(eve::action::ActionExecutionId(78), 1, eve::SimulationTick(1)).ok());
    REQUIRE(player.physicalSectionIndex().ok());
    CHECK_EQ(player.physicalSectionIndex().value(), 1U);

    CHECK(!player.setSectionSplits({eve::Duration::zero()}).ok());
    REQUIRE(player.jumpToSection(eve::action::ActionExecutionId(78), 2, eve::SimulationTick(2)).ok());
    REQUIRE(player.physicalSectionIndex().ok());
    CHECK_EQ(player.physicalSectionIndex().value(), 2U);
    CHECK(player.isPlaying());
}

TEST_CASE("actionMontage.clipTrimAndBlendCurveUseAuthoredSectionDuration") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    auto timeline                             = montageTimeline();
    timeline.animationSections[0].sourceStart = eve::Duration::fromSeconds(0.25).takeValue();
    timeline.animationSections[0].sourceEnd   = eve::Duration::fromSeconds(0.75).takeValue();
    timeline.animationSections[0].blendInCurve = eve::action::ActionBlendCurve::Linear;
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

TEST_CASE("actionMontage.sectionEnvelopesFadeAcrossGapsWithoutDippingAtContiguousBoundaries") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    auto timeline = montageTimeline();
    timeline.animationSections[0].blendOut = eve::Duration::fromSeconds(0.4).takeValue();
    timeline.animationSections[0].blendOutCurve = eve::action::ActionBlendCurve::Linear;
    timeline.animationSections[1].blendIn = eve::Duration::fromSeconds(0.4).takeValue();
    timeline.animationSections[1].blendInCurve = eve::action::ActionBlendCurve::Linear;

    eve::animation::MontagePlayer contiguous(skeleton);
    REQUIRE(contiguous.prepare(timeline, montageClips()).ok());
    REQUIRE(contiguous.play(eve::action::ActionExecutionId(90)).ok());
    eve::action::ActionAdvance advance;
    advance.id = eve::action::ActionExecutionId(90);
    advance.phase = eve::action::ActionPhase::Active;
    advance.totalElapsed = eve::Duration::fromSeconds(0.9).takeValue();
    auto atBoundary = contiguous.present(advance, eve::SimulationTick(1));
    REQUIRE(atBoundary.ok());
    CHECK(std::fabs(atBoundary.value().weight - 1.0) < 1e-6);

    timeline.animationSections[1].start = eve::Duration::fromSeconds(1.2).takeValue();
    eve::animation::MontagePlayer gapped(skeleton);
    REQUIRE(gapped.prepare(timeline, montageClips()).ok());
    REQUIRE(gapped.play(eve::action::ActionExecutionId(91)).ok());
    advance.id = eve::action::ActionExecutionId(91);
    advance.totalElapsed = eve::Duration::fromSeconds(0.8).takeValue();
    auto beforeGap = gapped.present(advance, eve::SimulationTick(1));
    REQUIRE(beforeGap.ok());
    CHECK(std::fabs(beforeGap.value().weight - 0.5) < 1e-6);
}

TEST_CASE("actionMontage.blendCurvesAreStableAndClamped") {
    using eve::action::ActionBlendCurve;
    using eve::action::evaluateActionBlendCurve;
    REQUIRE_EQ(evaluateActionBlendCurve(ActionBlendCurve::Linear, -1.0), 0.0);
    REQUIRE_EQ(evaluateActionBlendCurve(ActionBlendCurve::Linear, 2.0), 1.0);
    REQUIRE(std::fabs(evaluateActionBlendCurve(ActionBlendCurve::EaseIn, 0.5) - 0.25) < 1e-9);
    REQUIRE(std::fabs(evaluateActionBlendCurve(ActionBlendCurve::EaseOut, 0.5) - 0.75) < 1e-9);
    REQUIRE(std::fabs(evaluateActionBlendCurve(ActionBlendCurve::EaseInOut, 0.5) - 0.5) < 1e-9);
    REQUIRE(std::fabs(evaluateActionBlendCurve(ActionBlendCurve::SmoothStep, 0.5) - 0.5) < 1e-9);
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
    auto activeBlocks    = timeline.activeBlocks(advance.totalElapsed);
    REQUIRE(activeBlocks.ok());
    advance.activeBlocks = std::move(activeBlocks).takeValue();
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
    eve::action::ActionAdvance secondAdvance;
    secondAdvance.id           = eve::action::ActionExecutionId(12);
    secondAdvance.phase        = eve::action::ActionPhase::Active;
    secondAdvance.totalElapsed = eve::Duration::fromSeconds(0.1).takeValue();
    REQUIRE(coordinator.present(second.value(), secondAdvance, eve::SimulationTick(3)).ok());
    REQUIRE(coordinator.advanceBlendOuts(eve::Duration::fromSeconds(0.1).takeValue(), eve::SimulationTick(3)).ok());
    auto layerPose = coordinator.pose(0);
    REQUIRE(layerPose.ok());
    CHECK(std::fabs(layerPose.value().get().local(0).px - 0.3f) < 1e-5f);
    REQUIRE(coordinator.advanceBlendOuts(eve::Duration::fromSeconds(0.1).takeValue(), eve::SimulationTick(4)).ok());
    auto stale = coordinator.resolve(first.value());
    CHECK(!stale.ok());

    auto third = coordinator.play(0, montageTimeline(), montageClips(), eve::action::ActionExecutionId(13),
                                  eve::SimulationTick(5));
    REQUIRE(third.ok());
    CHECK_EQ(third.value().index(), first.value().index());
    CHECK_NE(third.value().generation(), first.value().generation());
}

TEST_CASE("actionMontage.coordinatorRetainsCompletedPoseUntilNextCrossFade") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::MontageCoordinator coordinator(skeleton);
    auto first = coordinator.play(0, montageTimeline(), montageClips(), eve::action::ActionExecutionId(41),
                                  eve::SimulationTick(1));
    REQUIRE(first.ok());
    eve::action::ActionAdvance completed;
    completed.id           = eve::action::ActionExecutionId(41);
    completed.phase        = eve::action::ActionPhase::Completed;
    completed.totalElapsed = eve::Duration::fromSeconds(2.0).takeValue();
    REQUIRE(coordinator.present(first.value(), completed, eve::SimulationTick(2)).ok());
    REQUIRE(coordinator.advanceBlendOuts(eve::Duration::zero(), eve::SimulationTick(3)).ok());
    auto retained = coordinator.resolve(first.value());
    REQUIRE(retained.ok());
    CHECK(retained.value().get().isFinished());
    auto retainedPose = coordinator.pose(0);
    REQUIRE(retainedPose.ok());
    CHECK(std::fabs(retainedPose.value().get().local(0).px - 2.0f) < 1e-5f);

    auto nextTimeline                   = montageTimeline();
    nextTimeline.montage.defaultBlendIn = eve::Duration::fromSeconds(0.2).takeValue();
    auto second =
        coordinator.play(0, nextTimeline, montageClips(), eve::action::ActionExecutionId(42), eve::SimulationTick(4));
    REQUIRE(second.ok());
    eve::action::ActionAdvance entering;
    entering.id           = eve::action::ActionExecutionId(42);
    entering.phase        = eve::action::ActionPhase::Active;
    entering.totalElapsed = eve::Duration::fromSeconds(0.1).takeValue();
    REQUIRE(coordinator.present(second.value(), entering, eve::SimulationTick(5)).ok());
    REQUIRE(coordinator
                .advanceBlendOuts(eve::Duration::fromSeconds(0.1).takeValue(), eve::SimulationTick(5), second.value())
                .ok());
    auto blendedPose = coordinator.pose(0);
    REQUIRE(blendedPose.ok());
    CHECK(std::fabs(blendedPose.value().get().local(0).px - 1.05f) < 1e-5f);
    REQUIRE(coordinator
                .advanceBlendOuts(eve::Duration::fromSeconds(0.1).takeValue(), eve::SimulationTick(6), second.value())
                .ok());
    CHECK(!coordinator.resolve(first.value()).ok());
}

TEST_CASE("actionMontage.coordinatorLayerMaskRestrictsCompositionPerBone") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("upper", 0);
    skeleton.setBindPosition(1, 0.0f, 1.0f, 0.0f);
    eve::animation::MontageCoordinator coordinator(skeleton);

    CHECK(!coordinator.setLayerBoneMask(0, {1.0f}).ok());
    CHECK(!coordinator.setLayerBoneMask(0, {0.0f, std::numeric_limits<float>::infinity()}).ok());
    eve::animation::AnimBoneMask upperBody(&skeleton);
    REQUIRE(upperBody.setBoneAndChildren("upper", 0.5f));
    REQUIRE(coordinator.setLayerBoneMask(0, upperBody).ok());
    eve::animation::AnimSkeleton otherSkeleton;
    otherSkeleton.addBone("root");
    eve::animation::AnimBoneMask foreignMask(&otherSkeleton);
    CHECK(!coordinator.setLayerBoneMask(0, foreignMask).ok());
    auto configured = coordinator.layerBoneMask(0);
    REQUIRE(configured.ok());
    REQUIRE_EQ(configured.value().size(), 2U);
    CHECK_EQ(configured.value()[1], 0.5f);
    upperBody.setAll(0.0f);
    configured = coordinator.layerBoneMask(0);
    REQUIRE(configured.ok());
    CHECK_EQ(configured.value()[1], 0.5f);

    auto handle = coordinator.play(0, montageTimeline(), maskedMontageClips(), eve::action::ActionExecutionId(21),
                                   eve::SimulationTick(1));
    REQUIRE(handle.ok());
    eve::action::ActionAdvance advance;
    advance.id           = eve::action::ActionExecutionId(21);
    advance.phase        = eve::action::ActionPhase::Active;
    advance.totalElapsed = eve::Duration::fromSeconds(0.5).takeValue();
    REQUIRE(coordinator.present(handle.value(), advance, eve::SimulationTick(2)).ok());
    auto pose = coordinator.pose(0);
    REQUIRE(pose.ok());
    CHECK(std::fabs(pose.value().get().local(0).px) < 1e-5f);
    CHECK(std::fabs(pose.value().get().local(1).py - 1.5f) < 1e-5f);

    REQUIRE(coordinator.clearLayerBoneMask(0).ok());
    REQUIRE(coordinator.pose(0).ok());
    auto unmasked = coordinator.pose(0);
    REQUIRE(unmasked.ok());
    CHECK(std::fabs(unmasked.value().get().local(0).px - 1.0f) < 1e-5f);
    CHECK(std::fabs(unmasked.value().get().local(1).py - 2.0f) < 1e-5f);
}

TEST_CASE("actionMontage.coordinatorComposesWeightedOverrideAndAdditiveLayersOverBasePose") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("upper", 0);
    skeleton.setBindPosition(1, 0.0f, 1.0f, 0.0f);
    eve::animation::MontageCoordinator coordinator(skeleton);

    CHECK(!coordinator.setLayerWeight(0, -0.1f).ok());
    CHECK(!coordinator.setLayerWeight(0, std::numeric_limits<float>::infinity()).ok());
    REQUIRE(coordinator.setLayerWeight(0, 0.5f).ok());
    REQUIRE(coordinator.setLayerBoneMask(0, {0.0f, 1.0f}).ok());
    auto overrideHandle = coordinator.play(0, montageTimeline(), maskedMontageClips(),
                                           eve::action::ActionExecutionId(51), eve::SimulationTick(1));
    REQUIRE(overrideHandle.ok());
    eve::action::ActionAdvance overrideAdvance;
    overrideAdvance.id           = eve::action::ActionExecutionId(51);
    overrideAdvance.phase        = eve::action::ActionPhase::Active;
    overrideAdvance.totalElapsed = eve::Duration::fromSeconds(0.5).takeValue();
    REQUIRE(coordinator.present(overrideHandle.value(), overrideAdvance, eve::SimulationTick(2)).ok());

    REQUIRE(coordinator.setLayerWeight(1, 0.5f).ok());
    REQUIRE(coordinator.setLayerAdditive(1, true).ok());
    auto additiveHandle = coordinator.play(1, montageTimeline(), montageClips(), eve::action::ActionExecutionId(52),
                                           eve::SimulationTick(3));
    REQUIRE(additiveHandle.ok());
    eve::action::ActionAdvance additiveAdvance;
    additiveAdvance.id           = eve::action::ActionExecutionId(52);
    additiveAdvance.phase        = eve::action::ActionPhase::Active;
    additiveAdvance.totalElapsed = eve::Duration::fromSeconds(0.5).takeValue();
    REQUIRE(coordinator.present(additiveHandle.value(), additiveAdvance, eve::SimulationTick(4)).ok());

    eve::animation::AnimPose base(skeleton.getBoneCount());
    skeleton.applyBindPose(&base);
    base.setLocalPosition(0, 10.0f, 0.0f, 0.0f);
    base.setLocalPosition(1, 0.0f, 10.0f, 0.0f);
    auto composed = coordinator.compose(base);
    REQUIRE(composed.ok());
    CHECK(std::fabs(composed.value().get().local(0).px - 10.25f) < 1e-5f);
    CHECK(std::fabs(composed.value().get().local(1).py - 6.0f) < 1e-5f);

    auto weight = coordinator.layerWeight(0);
    REQUIRE(weight.ok());
    CHECK_EQ(weight.value(), 0.5f);
    auto additive = coordinator.layerAdditive(1);
    REQUIRE(additive.ok());
    CHECK(additive.value());
    eve::animation::AnimPose foreignBase(1);
    CHECK(!coordinator.compose(foreignBase).ok());
}

TEST_CASE("actionMontage.coordinatorRootMotionReceiverFollowsLayerSlots") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::MontageCoordinator coordinator(skeleton);
    RootReceiver                       receiver;
    REQUIRE(coordinator.setLayerRootMotionReceiver(0, receiver).ok());

    auto first = coordinator.play(0, montageTimeline(), montageClips(), eve::action::ActionExecutionId(31),
                                  eve::SimulationTick(1));
    REQUIRE(first.ok());
    eve::action::ActionAdvance advance;
    advance.id           = eve::action::ActionExecutionId(31);
    advance.phase        = eve::action::ActionPhase::Active;
    advance.totalElapsed = eve::Duration::fromSeconds(0.5).takeValue();
    REQUIRE(coordinator.present(first.value(), advance, eve::SimulationTick(2)).ok());
    CHECK_EQ(receiver.calls, 1);
    CHECK(std::fabs(receiver.x - 0.5f) < 1e-5f);

    auto second = coordinator.play(0, montageTimeline(), montageClips(), eve::action::ActionExecutionId(32),
                                   eve::SimulationTick(3));
    REQUIRE(second.ok());
    advance.id = eve::action::ActionExecutionId(32);
    REQUIRE(coordinator.present(second.value(), advance, eve::SimulationTick(4)).ok());
    CHECK_EQ(receiver.calls, 2);

    REQUIRE(coordinator.clearLayerRootMotionReceiver(0).ok());
    advance.totalElapsed = eve::Duration::fromSeconds(0.75).takeValue();
    REQUIRE(coordinator.present(second.value(), advance, eve::SimulationTick(5)).ok());
    CHECK_EQ(receiver.calls, 2);
    REQUIRE(coordinator.clearLayerRootMotionReceiver(0).ok());
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

    auto replacement = rootClip("replacement", 3.0f);
    auto cloned      = replacement->clone();
    replacement->setDuration(2.0f);
    CHECK_EQ(cloned->getName(), std::string("replacement"));
    CHECK(std::fabs(cloned->getDuration() - 1.0f) < 1e-5f);
    REQUIRE(player.replaceClip("memory://clips/anticipation", std::move(cloned)).ok());
    CHECK_EQ(player.time(), eve::Duration::fromSeconds(0.5).takeValue());
    CHECK(std::fabs(player.pose().local(0).px - 1.5f) < 1e-4f);
}

TEST_CASE("actionMontage.rootMotionPolicyBakesAndFacesThroughOwnedPlayer") {
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::MontagePlayer    player(skeleton);
    eve::animation::RootMotionPolicy policy;
    policy.bakeTranslationIntoPose = true;
    policy.applySpace              = eve::animation::RootMotionApplySpace::CharacterFacing;
    policy.characterYaw            = static_cast<float>(M_PI * 0.5);  // +X from local +Z
    // Clip moves along +X; facing +90° publishes that as -Z after Ry mapping of X.
    // Use a Z-travelling clip via custom assets below.
    REQUIRE(player.setRootMotionPolicy(policy).ok());

    auto clip = std::make_unique<eve::animation::AnimClip>("forward");
    clip->setDuration(1.0f);
    clip->setLoop(false);
    clip->addPositionKey(0, 0.0f, 0.0f, 0.0f, 0.0f);
    clip->addPositionKey(0, 1.0f, 0.0f, 0.0f, 2.0f);  // +Z travel
    std::vector<eve::animation::MontageClipAsset> clips;
    clips.push_back({"memory://clips/anticipation", std::move(clip)});
    clips.push_back({"memory://clips/strike", rootClip("unused", 0.0f)});

    auto timeline                       = montageTimeline();
    timeline.montage.rootMotionVertical = false;
    REQUIRE(player.prepare(timeline, std::move(clips)).ok());
    // prepare overlays authored locks; bake / facing / yaw must survive.
    CHECK(player.getRootMotionPolicy().bakeTranslationIntoPose);
    CHECK(player.getRootMotionPolicy().applySpace == eve::animation::RootMotionApplySpace::CharacterFacing);
    CHECK(std::fabs(player.getRootMotionPolicy().characterYaw - static_cast<float>(M_PI * 0.5)) < 1e-5f);
    CHECK(!player.getRootMotionMask().translationY);

    RootReceiver receiver;
    player.setRootMotionReceiver(receiver);
    const eve::action::ActionExecutionId execution(41);
    REQUIRE(player.play(execution).ok());
    eve::action::ActionAdvance advance;
    advance.id           = execution;
    advance.phase        = eve::action::ActionPhase::Active;
    advance.totalElapsed = eve::Duration::fromSeconds(0.5).takeValue();
    REQUIRE(player.present(advance, eve::SimulationTick(1)).ok());
    CHECK(std::fabs(receiver.x - 1.0f) < 1e-4f);  // local +Z 1.0 after 0.5s @ 2 u/s, facing → +X
    CHECK(std::fabs(receiver.z) < 1e-4f);
    CHECK(std::fabs(player.pose().local(0).pz) < 1e-4f);  // baked out of pose
}

TEST_CASE("actionMontage.coordinatorLayerRootMotionPolicySurvivesPrepareLocks") {
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::MontageCoordinator coordinator(skeleton);
    RootReceiver                       receiver;
    REQUIRE(coordinator.setLayerRootMotionReceiver(0, receiver).ok());
    eve::animation::RootMotionPolicy policy;
    policy.bakeTranslationIntoPose = true;
    policy.applySpace              = eve::animation::RootMotionApplySpace::CharacterFacing;
    policy.characterYaw            = static_cast<float>(M_PI * 0.5);
    REQUIRE(coordinator.setLayerRootMotionPolicy(0, policy).ok());

    auto clip = std::make_unique<eve::animation::AnimClip>("forward");
    clip->setDuration(1.0f);
    clip->setLoop(false);
    clip->addPositionKey(0, 0.0f, 0.0f, 0.0f, 0.0f);
    clip->addPositionKey(0, 1.0f, 0.0f, 0.0f, 2.0f);
    std::vector<eve::animation::MontageClipAsset> clips;
    clips.push_back({"memory://clips/anticipation", std::move(clip)});
    clips.push_back({"memory://clips/strike", rootClip("unused", 0.0f)});
    auto timeline                       = montageTimeline();
    timeline.montage.rootMotionVertical = false;
    auto handle =
        coordinator.play(0, timeline, std::move(clips), eve::action::ActionExecutionId(51), eve::SimulationTick(1));
    REQUIRE(handle.ok());
    auto resolved = coordinator.resolve(handle.value());
    REQUIRE(resolved.ok());
    CHECK(resolved.value().get().getRootMotionPolicy().bakeTranslationIntoPose);
    CHECK(!resolved.value().get().getRootMotionMask().translationY);  // prepare overlays authored locks

    eve::action::ActionAdvance advance;
    advance.id           = eve::action::ActionExecutionId(51);
    advance.phase        = eve::action::ActionPhase::Active;
    advance.totalElapsed = eve::Duration::fromSeconds(0.5).takeValue();
    REQUIRE(coordinator.present(handle.value(), advance, eve::SimulationTick(2)).ok());
    CHECK(std::fabs(receiver.x - 1.0f) < 1e-4f);
    CHECK(std::fabs(resolved.value().get().pose().local(0).pz) < 1e-4f);
}
}  // namespace test_action_montage
#undef M_PI
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_preview_controller)
#include "PrivateTestScope.h"
#include <string_view>
#include "action/editor/ActionPreviewController.h"
#include "common/Diagnostic.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// action_preview_controller.cpp
namespace test_action_preview_controller {
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
    REQUIRE_EQ(sink.presented->activeBlocks.size(), 1u);
    CHECK_EQ(sink.presented->activeBlocks.front().itemId, id("combat-state:hitbox"));
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
    CHECK(sink.presented->activeBlocks.empty());
}

TEST_CASE("actionPreviewController.stepsFramesAndStopsAtomically") {
    eve::editor::ActionTimelineEditor    editor("asset.combat.preview", timelineFixture());
    RecordingSink                        sink;
    eve::editor::ActionPreviewController preview(editor, sink);

    REQUIRE(preview.stepFrames(1, 20'000'000.0).ok());
    CHECK_EQ(editor.previewTime(), eve::Duration::fromNanoseconds(50));
    CHECK(!editor.playing());
    REQUIRE(preview.stepFrames(-1, 20'000'000.0).ok());
    CHECK_EQ(editor.previewTime(), eve::Duration::zero());
    REQUIRE(preview.jumpToEnd().ok());
    CHECK_EQ(editor.previewTime(), eve::Duration::fromNanoseconds(100));

    editor.play();
    sink.rejectPrepare = true;
    auto rejected      = preview.stop();
    CHECK(!rejected.ok());
    CHECK_EQ(editor.previewTime(), eve::Duration::fromNanoseconds(100));
    CHECK(editor.playing());
    sink.rejectPrepare = false;
    REQUIRE(preview.stop().ok());
    CHECK_EQ(editor.previewTime(), eve::Duration::zero());
    CHECK(!editor.playing());
    CHECK(!preview.stepFrames(1, 0.0).ok());
}
}  // namespace test_action_preview_controller
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_timeline)
#include "PrivateTestScope.h"
#include <string_view>
#include "action/Action.h"
#include "action/ActionTimeline.h"
#include "action/editor/ActionTimelineEditor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// action_timeline.cpp
namespace test_action_timeline {
namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::action::ActionTimeline timelineFixture() {
    eve::action::ActionTimeline timeline;
    timeline.actionId     = id("combat:light-attack");
    timeline.duration     = eve::Duration::fromNanoseconds(100);
    timeline.animationUri = "asset://animations/light-attack.eva";

    eve::action::ActionTrack gameplay;
    gameplay.id    = id("combat-track:gameplay");
    gameplay.label = "Gameplay";
    gameplay.kind  = eve::action::ActionTrackKind::Gameplay;
    gameplay.notifies.push_back({id("combat-notify:begin"), id("combat-event:combo-open"), eve::Duration::zero(), {}});
    gameplay.notifies.push_back({id("combat-notify:hit"),
                                 id("combat-event:deal-damage"),
                                 eve::Duration::fromNanoseconds(20),
                                 {{"damage", eve::Value(25)}}});
    gameplay.states.push_back({id("combat-state:buffer"),
                               id("combat-state-type:input-buffer"),
                               eve::Duration::fromNanoseconds(10),
                               eve::Duration::fromNanoseconds(30),
                               {}});
    timeline.tracks.push_back(std::move(gameplay));
    return timeline;
}

}  // namespace

TEST_CASE("actionTimeline.versionedRoundTripAndDeterministicSampling") {
    auto timeline = timelineFixture();
    auto valid    = timeline.validate();
    REQUIRE(valid.ok());
    auto encoded = timeline.toValue();
    REQUIRE(encoded.ok());
    auto json = encoded.value().toJson();
    REQUIRE(json.ok());
    auto parsedValue = eve::Value::fromJson(json.value());
    REQUIRE(parsedValue.ok());
    auto decoded = eve::action::ActionTimeline::fromValue(parsedValue.value());
    REQUIRE(decoded.ok());
    auto reencoded = decoded.value().toValue();
    REQUIRE(reencoded.ok());
    auto reencodedJson = reencoded.value().toJson();
    REQUIRE(reencodedJson.ok());
    CHECK_EQ(reencodedJson.value(), json.value());

    auto sampled = decoded.value().sample(eve::Duration::zero(), eve::Duration::fromNanoseconds(30), true);
    REQUIRE(sampled.ok());
    REQUIRE_EQ(sampled.value().size(), 4u);
    CHECK_EQ(sampled.value()[0].itemId, id("combat-notify:begin"));
    CHECK_EQ(static_cast<int>(sampled.value()[1].kind),
             static_cast<int>(eve::action::ActionTimelineEventKind::StateEnter));
    CHECK_EQ(sampled.value()[2].itemId, id("combat-notify:hit"));
    CHECK_EQ(static_cast<int>(sampled.value()[3].kind),
             static_cast<int>(eve::action::ActionTimelineEventKind::StateExit));

    auto active = decoded.value().activeBlocks(eve::Duration::fromNanoseconds(20));
    REQUIRE(active.ok());
    REQUIRE_EQ(active.value().size(), 1u);
    CHECK_EQ(active.value()[0].itemId, id("combat-state:buffer"));
    CHECK_EQ(active.value()[0].localTime, eve::Duration::fromNanoseconds(10));
    CHECK_EQ(active.value()[0].duration, eve::Duration::fromNanoseconds(20));
    auto ended = decoded.value().activeBlocks(eve::Duration::fromNanoseconds(30));
    REQUIRE(ended.ok());
    CHECK(ended.value().empty());
    CHECK(!decoded.value().activeBlocks(eve::Duration::fromNanoseconds(101)).ok());
}

TEST_CASE("actionTimeline.disabledItemsPersistAndDoNotProjectRuntimeState") {
    auto timeline                          = timelineFixture();
    timeline.tracks[0].notifies[1].enabled = false;
    timeline.tracks[0].states[0].enabled   = false;

    auto encoded = timeline.toValue();
    REQUIRE(encoded.ok());
    auto decoded = eve::action::ActionTimeline::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(!decoded.value().tracks[0].notifies[1].enabled);
    CHECK(!decoded.value().tracks[0].states[0].enabled);

    auto sampled = decoded.value().sample(eve::Duration::zero(), eve::Duration::fromNanoseconds(30), true);
    REQUIRE(sampled.ok());
    REQUIRE_EQ(sampled.value().size(), 1U);
    CHECK_EQ(sampled.value()[0].itemId, id("combat-notify:begin"));
    auto active = decoded.value().activeBlocks(eve::Duration::fromNanoseconds(20));
    REQUIRE(active.ok());
    CHECK(active.value().empty());
}

TEST_CASE("gameplayAction.advanceProjectsAuthoredTimelineBoundaries") {
    eve::action::ActionDefinition definition;
    definition.id            = id("combat:light-attack");
    definition.timing.windup = eve::Duration::fromNanoseconds(100);
    definition.timeline      = timelineFixture();

    eve::action::ActionRequest request;
    request.actionId = definition.id;
    eve::action::ActionRuntime runtime;
    auto                       submitted = runtime.submit(definition, request);
    REQUIRE(submitted.ok());
    const auto execution = std::move(submitted).takeValue();

    auto started = runtime.advance(execution, eve::SimulationTick{1}, eve::Duration::zero());
    REQUIRE(started.ok());
    REQUIRE_EQ(started.value().timelineEvents.size(), 1u);
    CHECK_EQ(started.value().timelineEvents[0].itemId, id("combat-notify:begin"));

    auto crossed = runtime.advance(execution, eve::SimulationTick{2}, eve::Duration::fromNanoseconds(20));
    REQUIRE(crossed.ok());
    REQUIRE_EQ(crossed.value().timelineEvents.size(), 2u);
    CHECK_EQ(crossed.value().timelineEvents[0].itemId, id("combat-state:buffer"));
    CHECK_EQ(crossed.value().timelineEvents[1].itemId, id("combat-notify:hit"));
    REQUIRE_EQ(crossed.value().activeBlocks.size(), 1u);
    CHECK_EQ(crossed.value().activeBlocks[0].itemId, id("combat-state:buffer"));
    CHECK_EQ(crossed.value().activeBlocks[0].localTime, eve::Duration::fromNanoseconds(10));
}

TEST_CASE("gameplayAction.sectionTimingOverrideKeepsLifecycleEventsAndPresentationOnOneCursor") {
    auto timeline = timelineFixture();
    timeline.splitTimestamps.push_back(eve::Duration::fromNanoseconds(50));

    eve::action::ActionDefinition definition;
    definition.id             = timeline.actionId;
    definition.timing.windup  = eve::Duration::fromNanoseconds(50);
    definition.timing.active  = eve::Duration::fromNanoseconds(25);
    definition.timing.recover = eve::Duration::fromNanoseconds(25);
    definition.timeline       = timeline;
    eve::action::ActionRequest request;
    request.actionId = definition.id;
    eve::action::ActionRuntime runtime;
    auto submitted = runtime.submit(definition, request);
    REQUIRE(submitted.ok());
    const auto execution = submitted.value();

    auto overridden = runtime.setSectionTimingOverride(execution, 0, eve::Duration::fromNanoseconds(100));
    REQUIRE(overridden.ok());
    CHECK_EQ(static_cast<int>(overridden.status().code()), static_cast<int>(eve::StatusCode::Applied));
    REQUIRE(runtime.find(execution) != nullptr);
    REQUIRE(runtime.find(execution)->sectionTimingOverride(0).has_value());

    auto firstHalf = runtime.advance(execution, eve::SimulationTick{1}, eve::Duration::fromNanoseconds(50));
    REQUIRE(firstHalf.ok());
    CHECK_EQ(firstHalf.value().totalElapsed, eve::Duration::fromNanoseconds(25));
    CHECK_EQ(firstHalf.value().inputConsumed, eve::Duration::fromNanoseconds(50));
    CHECK_EQ(firstHalf.value().phase, eve::action::ActionPhase::Windup);
    REQUIRE_EQ(firstHalf.value().timelineEvents.size(), 3U);
    CHECK_EQ(firstHalf.value().timelineEvents[2].itemId, id("combat-notify:hit"));
    auto progress = runtime.sectionProgress(execution, 0);
    REQUIRE(progress.ok());
    CHECK(std::abs(progress.value() - 0.5) < 0.000001);

    auto crossesSection = runtime.advance(execution, eve::SimulationTick{2}, eve::Duration::fromNanoseconds(75));
    REQUIRE(crossesSection.ok());
    CHECK_EQ(crossesSection.value().totalElapsed, eve::Duration::fromNanoseconds(75));
    CHECK_EQ(crossesSection.value().inputConsumed, eve::Duration::fromNanoseconds(75));
    CHECK_EQ(crossesSection.value().phase, eve::action::ActionPhase::Recover);
    CHECK_EQ(runtime.sectionProgress(execution, 0).value(), 1.0);
    CHECK_EQ(runtime.sectionProgress(execution, 1).value(), 0.5);
}

TEST_CASE("gameplayAction.sectionTimingOverrideChangesMidSectionWithoutCursorDiscontinuity") {
    auto timeline = timelineFixture();
    timeline.splitTimestamps.push_back(eve::Duration::fromNanoseconds(50));
    eve::action::ActionDefinition definition;
    definition.id            = timeline.actionId;
    definition.timing.windup = timeline.duration;
    definition.timeline      = timeline;
    eve::action::ActionRequest request;
    request.actionId = definition.id;
    eve::action::ActionRuntime runtime;
    auto submitted = runtime.submit(definition, request);
    REQUIRE(submitted.ok());
    const auto execution = submitted.value();
    REQUIRE(runtime.advance(execution, eve::SimulationTick{1}, eve::Duration::fromNanoseconds(20)).ok());
    REQUIRE(runtime.setSectionTimingOverride(execution, 0, eve::Duration::fromNanoseconds(100)).ok());
    auto slowed = runtime.advance(execution, eve::SimulationTick{2}, eve::Duration::fromNanoseconds(10));
    REQUIRE(slowed.ok());
    CHECK_EQ(slowed.value().totalElapsed, eve::Duration::fromNanoseconds(25));

    auto sameOverride = runtime.setSectionTimingOverride(execution, 0, eve::Duration::fromNanoseconds(100));
    REQUIRE(sameOverride.ok());
    CHECK_EQ(static_cast<int>(sameOverride.status().code()), static_cast<int>(eve::StatusCode::NoOp));
    REQUIRE(runtime.clearSectionTimingOverride(execution, 0).ok());
    auto normal = runtime.advance(execution, eve::SimulationTick{3}, eve::Duration::fromNanoseconds(10));
    REQUIRE(normal.ok());
    CHECK_EQ(normal.value().totalElapsed, eve::Duration::fromNanoseconds(35));

    auto alreadyCleared = runtime.clearSectionTimingOverride(execution, 0);
    REQUIRE(alreadyCleared.ok());
    CHECK_EQ(static_cast<int>(alreadyCleared.status().code()), static_cast<int>(eve::StatusCode::NoOp));
    CHECK(!runtime.setSectionTimingOverride(execution, 2, eve::Duration::fromNanoseconds(10)).ok());
    CHECK(!runtime.setSectionTimingOverride(execution, 0, eve::Duration::zero()).ok());
}

TEST_CASE("actionTimelineEditor.editsPreviewAndUndoThroughCanonicalTarget") {
    eve::editor::ActionTimelineEditor editor("asset.combat.light-attack", timelineFixture());
    eve::editor::EditorWorkspace      workspace("combat", "Combat Editor");
    auto                              configured = editor.configureWorkspace(workspace);
    REQUIRE(configured.ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    CHECK_EQ(workspace.getActivePanel(), "action.timeline");

    eve::action::ActionNotify effect{id("combat-notify:spark"),
                                     id("combat-event:spawn-effect"),
                                     eve::Duration::fromNanoseconds(40),
                                     {{"uri", "asset://vfx/sword-spark"}}};
    auto                      added = editor.addNotify(id("combat-track:gameplay"), effect);
    REQUIRE(added.ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies.size(), 3u);
    CHECK(editor.canUndo());

    auto selected = editor.boxSelect(eve::Duration::fromNanoseconds(15), eve::Duration::fromNanoseconds(45));
    REQUIRE(selected.ok());
    CHECK_EQ(selected.value(), 3u);
    auto copied = editor.copySelection();
    REQUIRE(copied.ok());
    CHECK_EQ(copied.value(), 3u);

    auto undone = editor.undo();
    REQUIRE(undone.ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies.size(), 2u);
    CHECK(editor.canRedo());
    auto redone = editor.redo();
    REQUIRE(redone.ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies.size(), 3u);

    auto seek = editor.seek(eve::Duration::zero());
    REQUIRE(seek.ok());
    editor.play();
    auto previewed = editor.update(eve::Duration::fromNanoseconds(20));
    REQUIRE(previewed.ok());
    CHECK_EQ(previewed.value(), 3u);
    CHECK_EQ(editor.previewEvents()[2].itemId, id("combat-notify:hit"));
}

TEST_CASE("actionTimelineEditor.editsPhysicalSectionSplitsAtomically") {
    eve::editor::ActionTimelineEditor editor("asset.combat.sections", timelineFixture());

    REQUIRE(editor.addSectionSplit(eve::Duration::fromNanoseconds(60)).ok());
    REQUIRE(editor.addSectionSplit(eve::Duration::fromNanoseconds(30)).ok());
    REQUIRE_EQ(editor.target().timeline().splitTimestamps.size(), 2U);
    CHECK_EQ(editor.target().timeline().splitTimestamps[0], eve::Duration::fromNanoseconds(30));
    CHECK_EQ(editor.target().timeline().splitTimestamps[1], eve::Duration::fromNanoseconds(60));

    const auto duplicateRevision = editor.target().revision();
    CHECK(!editor.addSectionSplit(eve::Duration::fromNanoseconds(30)).ok());
    CHECK_EQ(editor.target().revision(), duplicateRevision);
    REQUIRE(editor.setSectionSplit(0, eve::Duration::fromNanoseconds(80)).ok());
    CHECK_EQ(editor.target().timeline().splitTimestamps[0], eve::Duration::fromNanoseconds(60));
    CHECK_EQ(editor.target().timeline().splitTimestamps[1], eve::Duration::fromNanoseconds(80));

    const auto collisionRevision = editor.target().revision();
    CHECK(!editor.setSectionSplit(0, eve::Duration::fromNanoseconds(80)).ok());
    CHECK_EQ(editor.target().revision(), collisionRevision);
    REQUIRE(editor.removeSectionSplit(1).ok());
    REQUIRE_EQ(editor.target().timeline().splitTimestamps.size(), 1U);
    REQUIRE(editor.undo().ok());
    REQUIRE_EQ(editor.target().timeline().splitTimestamps.size(), 2U);
    CHECK_EQ(editor.target().timeline().splitTimestamps[1], eve::Duration::fromNanoseconds(80));
}

TEST_CASE("actionTimelineEditor.movesAlignsAndScalesSelectionAtomically") {
    eve::editor::ActionTimelineEditor editor("asset.combat.selection", timelineFixture());
    REQUIRE(editor.selectItem(id("combat-notify:hit")).ok());
    REQUIRE(editor.selectItem(id("combat-state:buffer"), true).ok());

    auto range = editor.selectionRange();
    REQUIRE(range.ok());
    CHECK_EQ(range.value().start, eve::Duration::fromNanoseconds(10));
    CHECK_EQ(range.value().end, eve::Duration::fromNanoseconds(30));

    REQUIRE(editor.moveSelection(eve::Duration::fromNanoseconds(10)).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[1].time, eve::Duration::fromNanoseconds(30));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, eve::Duration::fromNanoseconds(20));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].end, eve::Duration::fromNanoseconds(40));
    REQUIRE(editor.undo().ok());

    REQUIRE(editor.alignSelectionStart().ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[1].time, eve::Duration::fromNanoseconds(10));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, eve::Duration::fromNanoseconds(10));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].end, eve::Duration::fromNanoseconds(30));
    REQUIRE(editor.undo().ok());

    REQUIRE(editor.alignSelectionEnd().ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[1].time, eve::Duration::fromNanoseconds(30));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, eve::Duration::fromNanoseconds(10));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].end, eve::Duration::fromNanoseconds(30));
    REQUIRE(editor.undo().ok());

    REQUIRE(editor.scaleSelection(eve::Duration::fromNanoseconds(20), eve::Duration::fromNanoseconds(60)).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[1].time, eve::Duration::fromNanoseconds(40));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, eve::Duration::fromNanoseconds(20));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].end, eve::Duration::fromNanoseconds(60));
    REQUIRE(editor.undo().ok());

    REQUIRE(editor.setTrackLocked(id("combat-track:gameplay"), true).ok());
    const auto before = editor.target().timeline();
    CHECK(!editor.moveSelection(eve::Duration::fromNanoseconds(1)).ok());
    CHECK_EQ(editor.target().timeline().toValue().value().toJson().value(), before.toValue().value().toJson().value());
}

TEST_CASE("actionTimelineEditor.sharesDeepClipboardAcrossDocumentsAndPastesTracks") {
    auto                              clipboard = std::make_shared<eve::editor::ActionTimelineClipboard>();
    eve::editor::ActionTimelineEditor source("asset.combat.source", timelineFixture(), clipboard);

    auto destinationTimeline            = timelineFixture();
    destinationTimeline.actionId        = id("combat:alternate-attack");
    destinationTimeline.tracks[0].id    = id("combat-track:alternate");
    destinationTimeline.tracks[0].label = "Alternate";
    destinationTimeline.tracks[0].notifies.clear();
    destinationTimeline.tracks[0].states.clear();
    eve::action::ActionTrack collision;
    collision.id    = id("combat-track:gameplay.copy.1");
    collision.label = "Existing Copy";
    collision.kind  = eve::action::ActionTrackKind::Gameplay;
    destinationTimeline.tracks.push_back(std::move(collision));
    eve::editor::ActionTimelineEditor destination("asset.combat.destination", std::move(destinationTimeline),
                                                  clipboard);

    REQUIRE(source.copyTrack(id("combat-track:gameplay")).ok());
    auto pastedTrack = destination.pasteTrack();
    REQUIRE(pastedTrack.ok());
    CHECK_EQ(pastedTrack.value(), id("combat-track:gameplay.copy.2"));
    REQUIRE_EQ(destination.target().timeline().tracks.size(), 3U);
    const auto& track = destination.target().timeline().tracks.back();
    CHECK_EQ(track.label, "Gameplay Copy");
    REQUIRE_EQ(track.notifies.size(), 2U);
    REQUIRE_EQ(track.states.size(), 1U);
    CHECK(track.notifies[0].id != source.target().timeline().tracks[0].notifies[0].id);
    CHECK(track.states[0].id != source.target().timeline().tracks[0].states[0].id);
    REQUIRE(destination.undo().ok());
    CHECK_EQ(destination.target().timeline().tracks.size(), 2U);

    REQUIRE(source.selectItem(id("combat-notify:hit")).ok());
    auto copied = source.copySelection();
    REQUIRE(copied.ok());
    CHECK_EQ(copied.value(), 1U);
    auto pastedItem = destination.pasteToTrack(id("combat-track:alternate"), eve::Duration::fromNanoseconds(10));
    REQUIRE(pastedItem.ok());
    CHECK_EQ(pastedItem.value(), 1U);
    REQUIRE_EQ(destination.target().timeline().tracks[0].notifies.size(), 1U);
    CHECK_EQ(destination.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(30));

    REQUIRE(destination.setTrackLocked(id("combat-track:alternate"), true).ok());
    const auto before = destination.target().timeline().toValue().value().toJson().value();
    CHECK(!destination.pasteToTrack(id("combat-track:alternate"), eve::Duration::zero()).ok());
    CHECK_EQ(destination.target().timeline().toValue().value().toJson().value(), before);
}

TEST_CASE("actionTimelineDocuments.openSaveReconcileAndProtectDirtyTabs") {
    eve::editor::MemoryAtomicDocumentStore       store;
    eve::editor::DocumentService                 documents(&store);
    eve::editor::ActionTimelineDocumentWorkspace workspace(documents);

    auto first = workspace.open({eve::editor::DocumentKind::Timeline, eve::editor::AssetGuid("light-attack")},
                                "Light Attack", "content://Actions/LightAttack.action", timelineFixture());
    REQUIRE(first.ok());
    auto secondTimeline     = timelineFixture();
    secondTimeline.actionId = id("combat:heavy-attack");
    auto second = workspace.open({eve::editor::DocumentKind::Timeline, eve::editor::AssetGuid("heavy-attack")},
                                 "Heavy Attack", "content://Actions/HeavyAttack.action", secondTimeline);
    REQUIRE(second.ok());
    CHECK_EQ(workspace.activeDocument(), second.value());
    REQUIRE_EQ(workspace.tabs().value().size(), 2U);

    auto* editor = workspace.activeEditor();
    REQUIRE(editor != nullptr);
    REQUIRE(editor->renameTrack(id("combat-track:gameplay"), "Heavy Gameplay").ok());
    auto dirtyTabs = workspace.tabs();
    REQUIRE(dirtyTabs.ok());
    CHECK(dirtyTabs.value()[1].dirty);
    auto protectedClose = workspace.close(second.value(), eve::editor::ActionTimelineCloseMode::ProtectDirty);
    CHECK(!protectedClose.ok());
    CHECK_EQ(protectedClose.code(), eve::editor::EditorStatus::Conflict);

    auto saved = workspace.save(second.value());
    REQUIRE(saved.ok());
    CHECK(!saved.value().dirty());
    CHECK(!workspace.tabs().value()[1].dirty);

    eve::editor::DocumentService external(&store);
    auto externalOpened = external.open({eve::editor::DocumentKind::Timeline, eve::editor::AssetGuid("heavy-attack")},
                                        "Heavy Attack", "content://Actions/HeavyAttack.action");
    REQUIRE(externalOpened.ok());
    auto externallyEdited            = secondTimeline;
    externallyEdited.tracks[0].label = "External Gameplay";
    auto externalValue               = externallyEdited.toValue();
    REQUIRE(externalValue.ok());
    REQUIRE(external.edit(externalOpened.value().id, eve::editor::toEditorValue(externalValue.value())).ok());
    auto externalTicket = external.requestSave(externalOpened.value().id);
    REQUIRE(externalTicket.ok());
    REQUIRE(external.executeSave(externalTicket.value()).ok());

    auto reconciled = workspace.reconcile(second.value());
    REQUIRE(reconciled.ok());
    REQUIRE(workspace.activeEditor() != nullptr);
    CHECK_EQ(workspace.activeEditor()->target().timeline().tracks[0].label, "External Gameplay");
    CHECK(!workspace.activeEditor()->canUndo());

    auto externalSnapshot = external.snapshot(externalOpened.value().id);
    REQUIRE(externalSnapshot.ok());
    eve::editor::EditorValue invalid(eve::editor::EditorValue::Object{{"schema", "not-an-action-timeline"}});
    REQUIRE(external.edit(externalOpened.value().id, invalid, externalSnapshot.value().revision.edit).ok());
    externalTicket = external.requestSave(externalOpened.value().id);
    REQUIRE(externalTicket.ok());
    REQUIRE(external.executeSave(externalTicket.value()).ok());
    auto invalidExternal = workspace.reconcile(second.value());
    CHECK(!invalidExternal.ok());
    CHECK_EQ(invalidExternal.code(), eve::editor::EditorStatus::Conflict);
    CHECK_EQ(workspace.activeEditor()->target().timeline().tracks[0].label, "External Gameplay");
    CHECK_EQ(documents.content(second.value()).value(), workspace.activeEditor()->target().snapshotValue());

    externallyEdited.tracks[0].label = "External Recovered";
    externalValue                    = externallyEdited.toValue();
    REQUIRE(externalValue.ok());
    externalSnapshot = external.snapshot(externalOpened.value().id);
    REQUIRE(externalSnapshot.ok());
    REQUIRE(external
                .edit(externalOpened.value().id, eve::editor::toEditorValue(externalValue.value()),
                      externalSnapshot.value().revision.edit)
                .ok());
    externalTicket = external.requestSave(externalOpened.value().id);
    REQUIRE(externalTicket.ok());
    REQUIRE(external.executeSave(externalTicket.value()).ok());
    REQUIRE(workspace.reconcile(second.value()).ok());
    CHECK_EQ(workspace.activeEditor()->target().timeline().tracks[0].label, "External Recovered");

    REQUIRE(workspace.activeEditor()->renameTrack(id("combat-track:gameplay"), "Unsaved Local").ok());
    externallyEdited.tracks[0].label = "External Again";
    externalValue                    = externallyEdited.toValue();
    REQUIRE(externalValue.ok());
    externalSnapshot = external.snapshot(externalOpened.value().id);
    REQUIRE(externalSnapshot.ok());
    REQUIRE(external
                .edit(externalOpened.value().id, eve::editor::toEditorValue(externalValue.value()),
                      externalSnapshot.value().revision.edit)
                .ok());
    externalTicket = external.requestSave(externalOpened.value().id);
    REQUIRE(externalTicket.ok());
    REQUIRE(external.executeSave(externalTicket.value()).ok());
    auto conflict = workspace.reconcile(second.value());
    CHECK(!conflict.ok());
    CHECK_EQ(conflict.code(), eve::editor::EditorStatus::Conflict);
    CHECK_EQ(workspace.activeEditor()->target().timeline().tracks[0].label, "Unsaved Local");

    REQUIRE(workspace.close(second.value(), eve::editor::ActionTimelineCloseMode::Discard).ok());
    CHECK_EQ(workspace.activeDocument(), first.value());
    REQUIRE(workspace.close(first.value(), eve::editor::ActionTimelineCloseMode::ProtectDirty).ok());
    CHECK(workspace.activeDocument().empty());
}
}  // namespace test_action_timeline
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_timeline_payload_editor)
#include "PrivateTestScope.h"
#include "action/editor/ActionTimelinePayloadEditor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// action_timeline_payload_editor.cpp
namespace test_action_timeline_payload_editor {
namespace {

class FixedVfxDurationProvider final : public eve::action::IActionVfxDurationProvider {
public:
    eve::Result<eve::Duration> naturalDuration(std::string_view uri) const override {
        lastUri = std::string(uri);
        return eve::Result<eve::Duration>::success(eve::Duration::fromSeconds(1.8).value());
    }

    mutable std::string lastUri;
};

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
    track.states.push_back({payloadId("test-state:audio"),
                            payloadId("presentation:audio-state"),
                            eve::Duration::fromNanoseconds(10),
                            eve::Duration::fromNanoseconds(50),
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
    track.states.push_back({payloadId("test-state:vfx"),
                            payloadId("presentation:vfx-state"),
                            eve::Duration::fromSeconds(1.0).value(),
                            eve::Duration::fromSeconds(1.5).value(),
                            {{"uri", "asset://vfx/slash.json"},
                             {"clipStartTime", 0.25},
                             {"clipEndTime", 1.25},
                             {"extensionField", "preserved"}}});
    timeline.tracks.push_back(std::move(track));
    return timeline;
}

}  // namespace

TEST_CASE("actionTimelinePayloadEditor.patchPreservesUnknownFieldsAndRejectsAtomically") {
    eve::editor::ActionTimelineEditor editor("test.payload-editor", payloadTimeline());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::editor::ActionTimelinePayloadEditor payloads(editor, registry.value());
    const auto                               itemId = payloadId("test-state:audio");

    REQUIRE(payloads
                .patch(itemId, {{"volume", 0.5},
                                {"looping", true},
                                {"randomUris", eve::Value::Array{"asset://audio/a.wav", "asset://audio/b.wav"}},
                                {"randomPitchOffset", 0.08},
                                {"spatialBlend", 0.5},
                                {"minDistance", 2.0},
                                {"maxDistance", 24.0},
                                {"fadeOutOnExit", true},
                                {"fadeOutDuration", 0.2}})
                .ok());
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
    const auto                               itemId = payloadId("test-state:vfx");

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

    FixedVfxDurationProvider provider;
    REQUIRE(payloads.fitClipToNaturalDuration(itemId, std::cref(provider)).ok());
    auto naturalClip = payloads.payload(itemId);
    REQUIRE(naturalClip.ok());
    CHECK_EQ(provider.lastUri, std::string("asset://vfx/slash.json"));
    CHECK_EQ(*naturalClip.value().at("clipEndTime").getIf<double>(), 1.8);
    CHECK_EQ(*naturalClip.value().at("extensionField").getIf<std::string>(), "preserved");
    REQUIRE(editor.undo().ok());
    CHECK_EQ(*payloads.payload(itemId).value().at("clipEndTime").getIf<double>(), 1.25);
}

TEST_CASE("actionTimelinePayloadEditor.vfxClipFitRejectsWrongShapeAndOutOfRangeWithoutMutation") {
    auto timeline = vfxPayloadTimeline();
    timeline.tracks.front().notifies.push_back({payloadId("test-notify:vfx"),
                                                payloadId("presentation:vfx"),
                                                eve::Duration::fromSeconds(0.25).value(),
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
    CHECK(!payloads.fitClipToNaturalDuration(payloadId("test-state:vfx"), std::nullopt).ok());
    CHECK_EQ(editor.target().revision(), revision);
    CHECK_EQ(editor.target().timeline().tracks.front().states.front().end.seconds(), 4.75);
}
}  // namespace test_action_timeline_payload_editor
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_timeline_widget)
#include "PrivateTestScope.h"
#include <map>
#include <string_view>
#include "action/ActionAudioWaveform.h"
#include "action/ActionParameterCurve.h"
#include "action/editor/ActionTimelineWidget.h"
#include "common/Capability.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// action_timeline_widget.cpp
namespace test_action_timeline_widget {
namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::action::ActionTimeline timelineFixture() {
    eve::action::ActionTimeline timeline;
    timeline.actionId = id("combat:widget-test");
    timeline.duration = eve::Duration::fromNanoseconds(100);
    eve::action::ActionTrack track;
    track.id    = id("combat-track:gameplay");
    track.label = "Gameplay";
    track.kind  = eve::action::ActionTrackKind::Gameplay;
    track.notifies.push_back({id("combat-notify:damage"),
                              id("combat:damage"),
                              eve::Duration::fromNanoseconds(20),
                              {{"damageType", eve::Value("Damage.Physical")}, {"amount", eve::Value(10)}}});
    track.states.push_back({id("combat-state:hitbox"),
                            id("combat:hitbox-window"),
                            eve::Duration::fromNanoseconds(10),
                            eve::Duration::fromNanoseconds(30),
                            {{"hitbox", eve::Value("weapon.main")}}});
    timeline.tracks.push_back(std::move(track));
    return timeline;
}

class RecordingOverlay final : public eve::editor::IEditorOverlay {
public:
    void line(const eve::editor::OverlayPoint& from, const eve::editor::OverlayPoint& to,
              const eve::editor::OverlayStyle&) override {
        ++lines;
        (void)from;
        (void)to;
    }
    void circle(const eve::editor::OverlayPoint&, float, const eve::editor::OverlayStyle&) override { ++circles; }
    void rectangle(const eve::editor::OverlayPoint&, const eve::editor::OverlayPoint&,
                   const eve::editor::OverlayStyle&) override {
        ++rectangles;
    }
    void text(const eve::editor::OverlayPoint&, const std::string&, const eve::editor::OverlayStyle&) override {
        ++texts;
    }

    int lines      = 0;
    int circles    = 0;
    int rectangles = 0;
    int texts      = 0;
};

class TestWaveformProvider final : public eve::action::IActionAudioWaveformProvider {
public:
    eve::Result<eve::action::ActionAudioWaveform> waveform(
        const eve::action::ActionAudioWaveformRequest& request) override {
        ++calls;
        lastRequest = request;
        eve::action::ActionAudioWaveform result;
        result.clipDurationSeconds = 0.5;
        result.buckets.assign(request.bucketCount, {-0.5f, 0.75f});
        return eve::Result<eve::action::ActionAudioWaveform>::success(std::move(result));
    }

    int                                     calls = 0;
    eve::action::ActionAudioWaveformRequest lastRequest;
};

class EditingInspector final : public eve::editor::IEditorInspector {
public:
    void beginGroup(const std::string&, const std::string&) override {}
    void endGroup() override {}
    bool boolean(const std::string&, const std::string&, bool&) override { return false; }
    bool integer(const std::string&, const std::string&, int&, int, int) override { return false; }
    bool scalar(const std::string& field, const std::string&, float& value, float, float) override {
        auto found = scalarReplacements.find(field);
        if (found == scalarReplacements.end()) return false;
        value = found->second;
        return true;
    }
    bool string(const std::string& field, const std::string&, std::string& value) override {
        auto found = replacements.find(field);
        if (found == replacements.end()) return false;
        value = found->second;
        return true;
    }

    std::map<std::string, std::string> replacements;
    std::map<std::string, float>       scalarReplacements;
};

class CurveEditingInspector final : public eve::editor::IEditorInspector {
public:
    void beginGroup(const std::string& id, const std::string&) override { groups.push_back(id); }
    void endGroup() override { groups.pop_back(); }
    bool boolean(const std::string&, const std::string&, bool&) override { return false; }
    bool integer(const std::string&, const std::string&, int&, int, int) override { return false; }
    bool scalar(const std::string& id, const std::string&, float& value, float, float) override {
        if (!groups.empty() && groups.back() == "action.timeline.parameter-key.1" && id == "value") {
            value = 0.25f;
            return true;
        }
        return false;
    }
    bool string(const std::string&, const std::string&, std::string&) override { return false; }

    std::vector<std::string> groups;
};

eve::editor::ActionTimelineWidget widget(eve::editor::ActionTimelineEditor& editor,
                                         eve::action::ActionNotifyRegistry& registry) {
    eve::editor::ActionTimelineWidget result(editor, registry);
    REQUIRE(result.setViewport(1120.0f, 24.0f, 120.0f).ok());
    return result;
}

}  // namespace

TEST_CASE("actionTimelineWidget.projectsDrawsAndHitTestsSemanticItems") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view   = widget(editor, registry.value());
    auto layout = view.layout();
    REQUIRE_EQ(layout.items.size(), 2u);
    CHECK_EQ(layout.width, 1120.0f);
    CHECK_EQ(layout.height, 24.0f);
    CHECK_EQ(layout.items[0].displayName, std::string("Apply Damage"));
    CHECK_EQ(layout.items[0].detail, std::string("Damage.Physical 10"));
    CHECK(layout.items[0].visual == eve::editor::TimelineItemVisual::Damage);
    CHECK_EQ(layout.items[1].displayName, std::string("Hitbox Window"));
    CHECK_EQ(layout.items[1].detail, std::string("weapon.main"));
    CHECK(layout.items[1].visual == eve::editor::TimelineItemVisual::Hitbox);

    auto notify = view.hitTest(320.0f, 12.0f);
    REQUIRE(notify.has_value());
    CHECK_EQ(notify->itemId, id("combat-notify:damage"));
    auto stateStart = view.hitTest(220.0f, 12.0f);
    REQUIRE(stateStart.has_value());
    CHECK(static_cast<int>(stateStart->part) == static_cast<int>(eve::editor::TimelineHitPart::StartHandle));

    RecordingOverlay overlay;
    view.draw(overlay);
    CHECK(overlay.rectangles >= 3);
    CHECK(overlay.lines >= 4);
    CHECK_EQ(overlay.texts, 2);
}

TEST_CASE("actionTimelineWidget.projectsSemanticResourceThumbnailsWithoutPersistingThem") {
    auto timeline = timelineFixture();
    timeline.tracks[0].notifies.clear();
    timeline.tracks[0].states.clear();
    timeline.tracks[0].states.push_back({id("vfx-state:slash"),
                                         id("presentation:vfx-state"),
                                         eve::Duration::fromNanoseconds(10),
                                         eve::Duration::fromNanoseconds(90),
                                         {{"uri", "effects/combat/slash.effect"}}});
    eve::editor::ActionTimelineEditor editor("asset.combat.widget-thumbnail", std::move(timeline));
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    const auto layout = widget(editor, registry.value()).layout();
    REQUIRE_EQ(layout.items.size(), 1u);
    CHECK_EQ(layout.items[0].displayName, std::string("VFX State"));
    CHECK_EQ(layout.items[0].detail, std::string("slash.effect"));
    CHECK(layout.items[0].visual == eve::editor::TimelineItemVisual::Vfx);

    auto encoded = editor.target().timeline().toValue();
    REQUIRE(encoded.ok());
    const auto json = encoded.value().toJson();
    REQUIRE(json.ok());
    CHECK(json.value().find("displayName") == std::string::npos);
    CHECK(json.value().find("visual") == std::string::npos);
}

TEST_CASE("actionTimelineWidget.usesRegistryLabelAndExplicitCustomVisualFallback") {
    auto timeline = timelineFixture();
    timeline.tracks[0].states.clear();
    timeline.tracks[0].notifies[0].type    = id("project:charge");
    timeline.tracks[0].notifies[0].payload = {{"power", 3}};
    eve::editor::ActionTimelineEditor editor("asset.combat.widget-custom", std::move(timeline));
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    REQUIRE(registry.value()
                .registerDescriptor(
                    {"project:charge", "Charge Meter", "Project", eve::action::ActionNotifyShape::Instant, {"power"}})
                .ok());
    const auto layout = widget(editor, registry.value()).layout();
    REQUIRE_EQ(layout.items.size(), 1u);
    CHECK_EQ(layout.items[0].displayName, std::string("Charge Meter"));
    CHECK(layout.items[0].detail.empty());
    CHECK(layout.items[0].visual == eve::editor::TimelineItemVisual::Custom);
    CHECK_EQ(eve::editor::timelineItemVisualName(layout.items[0].visual), std::string_view("custom"));
}

TEST_CASE("actionTimelineWidget.projectsBoundedAudioWaveformWithoutOwningAudioState") {
    auto timeline           = timelineFixture();
    timeline.tracks[0].kind = eve::action::ActionTrackKind::Audio;
    timeline.tracks[0].notifies.clear();
    timeline.tracks[0].states.clear();
    timeline.tracks[0].states.push_back({id("audio-state:loop"),
                                         id("presentation:audio-state"),
                                         eve::Duration::fromNanoseconds(10),
                                         eve::Duration::fromNanoseconds(90),
                                         {{"uri", "audio/hit.wav"}, {"pitch", 1.5}, {"looping", true}}});
    eve::editor::ActionTimelineEditor editor("asset.combat.widget-waveform", std::move(timeline));
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    CHECK(!view.layout().audioWaveformsAvailable);

    TestWaveformProvider provider;
    eve::cap::provide<eve::action::IActionAudioWaveformProvider>(&provider);
    CHECK(view.layout().audioWaveformsAvailable);
    RecordingOverlay overlay;
    view.draw(overlay);
    eve::cap::revoke<eve::action::IActionAudioWaveformProvider>(&provider);

    REQUIRE_EQ(provider.calls, 1);
    CHECK_EQ(provider.lastRequest.binding.uri, std::string("audio/hit.wav"));
    CHECK_EQ(provider.lastRequest.binding.pitch, 1.5);
    CHECK(provider.lastRequest.binding.looping);
    CHECK_EQ(provider.lastRequest.blockDuration, eve::Duration::fromNanoseconds(80));
    CHECK(provider.lastRequest.bucketCount > 0);
    CHECK(provider.lastRequest.bucketCount <= 512);
    CHECK(overlay.lines >= static_cast<int>(provider.lastRequest.bucketCount));
}

TEST_CASE("actionTimelineWidget.drawsAndTransactionallyInspectsParameterCurveKeys") {
    auto timeline = timelineFixture();
    timeline.tracks[0].states.clear();
    timeline.tracks[0].notifies.clear();
    timeline.tracks[0].states.push_back(
        {id("parameter-curve:fade"),
         id("presentation:parameter-curve"),
         eve::Duration::fromNanoseconds(10),
         eve::Duration::fromNanoseconds(90),
         {{"target", "audio:master-volume"},
          {"operation", "multiply"},
          {"keys",
           eve::Value::Array{eve::Value::Object{{"time", 0.0}, {"value", 1.0}, {"interpolation", "linear"}},
                             eve::Value::Object{{"time", 0.5}, {"value", 0.5}, {"interpolation", "cubic"}},
                             eve::Value::Object{{"time", 1.0}, {"value", 0.0}, {"interpolation", "linear"}}}}}});
    eve::editor::ActionTimelineEditor editor("asset.combat.widget-curve", std::move(timeline));
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto             view = widget(editor, registry.value());
    RecordingOverlay overlay;
    view.draw(overlay);
    CHECK(overlay.lines >= 128);
    CHECK(overlay.circles >= 3);

    REQUIRE(editor.selectItem(id("parameter-curve:fade")).ok());
    CurveEditingInspector inspector;
    REQUIRE(view.inspectSelection(inspector).ok());
    auto binding =
        eve::action::ActionParameterCurveBinding::fromPayload(editor.target().timeline().tracks[0].states[0].payload);
    REQUIRE(binding.ok());
    CHECK_EQ(binding.value().keys[1].value, 0.25);
    REQUIRE(editor.undo().ok());
    binding =
        eve::action::ActionParameterCurveBinding::fromPayload(editor.target().timeline().tracks[0].states[0].payload);
    REQUIRE(binding.ok());
    CHECK_EQ(binding.value().keys[1].value, 0.5);

    REQUIRE(editor
                .addParameterKey(id("parameter-curve:fade"),
                                 {0.25, 0.75, 0.0, 0.0, eve::action::ActionParameterInterpolation::Linear})
                .ok());
    binding =
        eve::action::ActionParameterCurveBinding::fromPayload(editor.target().timeline().tracks[0].states[0].payload);
    REQUIRE(binding.ok());
    REQUIRE_EQ(binding.value().keys.size(), 4u);
    CHECK_EQ(binding.value().keys[1].time, 0.25);
    CHECK(!editor.addParameterKey(id("parameter-curve:fade"), binding.value().keys[1]).ok());
    REQUIRE(editor
                .editParameterKey(id("parameter-curve:fade"), 1,
                                  {0.2, 0.8, 0.0, 0.0, eve::action::ActionParameterInterpolation::Step})
                .ok());
    REQUIRE(editor.removeParameterKey(id("parameter-curve:fade"), 1).ok());
    CHECK(!editor.removeParameterKey(id("parameter-curve:fade"), 0).ok());
    REQUIRE(editor.undo().ok());
    REQUIRE(editor.redo().ok());
    REQUIRE(editor.setTrackLocked(id("combat-track:gameplay"), true).ok());
    CHECK(!editor
               .addParameterKey(id("parameter-curve:fade"),
                                {0.25, 0.75, 0.0, 0.0, eve::action::ActionParameterInterpolation::Linear})
               .ok());
}

TEST_CASE("actionTimelineWidget.dragPreviewCommitsOnceAndUndoRestores") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());

    REQUIRE(view.pointerDown(320.0f, 12.0f).ok());
    REQUIRE(view.pointerMove(520.0f).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(20));
    REQUIRE(view.pointerUp(520.0f).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(40));
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(20));

    REQUIRE(view.pointerDown(220.0f, 12.0f).ok());
    REQUIRE(view.pointerUp(270.0f).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, eve::Duration::fromNanoseconds(15));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].end, eve::Duration::fromNanoseconds(30));
}

TEST_CASE("actionTimelineWidget.magneticSnapUsesCrossTrackBlockEdges") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget-magnetic-snap", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::editor::ActionTimelineWidget view(editor, registry.value());
    REQUIRE(view.setViewport(220.0f, 24.0f, 120.0f).ok());

    REQUIRE(view.pointerDown(140.0f, 12.0f).ok());
    REQUIRE(view.pointerUp(145.0f).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(30));
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(20));
}

TEST_CASE("actionTimelineWidget.draggingSelectedBodyMovesWholeSelectionOnce") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget-selection", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    REQUIRE(editor.selectItem(id("combat-notify:damage")).ok());
    REQUIRE(editor.selectItem(id("combat-state:hitbox"), true).ok());

    REQUIRE(view.pointerDown(320.0f, 12.0f).ok());
    REQUIRE(view.pointerUp(420.0f).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(30));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, eve::Duration::fromNanoseconds(20));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].end, eve::Duration::fromNanoseconds(40));
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(20));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, eve::Duration::fromNanoseconds(10));
}

TEST_CASE("actionTimelineWidget.multiSelectionInspectorScalesCanonicalItems") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget-selection-inspector", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    REQUIRE(editor.selectItem(id("combat-notify:damage")).ok());
    REQUIRE(editor.selectItem(id("combat-state:hitbox"), true).ok());
    EditingInspector inspector;
    inspector.scalarReplacements["start"] = 0.00000002f;
    inspector.scalarReplacements["end"]   = 0.00000006f;
    REQUIRE(view.inspectSelection(inspector).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(40));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, eve::Duration::fromNanoseconds(20));
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].end, eve::Duration::fromNanoseconds(60));
}

TEST_CASE("actionTimelineWidget.exposesUndoableSelectionAlignmentCommands") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget-selection-align", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    REQUIRE(editor.selectItem(id("combat-notify:damage")).ok());
    REQUIRE(editor.selectItem(id("combat-state:hitbox"), true).ok());
    const auto commands = view.commands();
    REQUIRE_EQ(commands.size(), 8u);
    CHECK(commands[3].enabled);
    CHECK(commands[4].enabled);

    REQUIRE(view.handleShortcut("Shift+[").ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(10));
    REQUIRE(editor.undo().ok());
    REQUIRE(view.handleShortcut("Shift+]").ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(30));
}

TEST_CASE("actionTimelineWidget.inspectorUsesRegistryAndCanonicalPayload") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    REQUIRE(editor.selectItem(id("combat-notify:damage")).ok());

    EditingInspector inspector;
    inspector.replacements["type"]    = "gameplay:event";
    inspector.replacements["payload"] = R"({"tag":"Combat.Action.Hit"})";
    REQUIRE(view.inspectSelection(inspector).ok());
    const auto& notify = editor.target().timeline().tracks[0].notifies[0];
    CHECK_EQ(notify.type, id("gameplay:event"));
    CHECK(notify.payload.contains("tag"));

    inspector.replacements["type"]        = "combat:hitbox-window";
    inspector.scalarReplacements["start"] = 0.00000005f;
    CHECK(!view.inspectSelection(inspector).ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].type, id("gameplay:event"));
    CHECK_EQ(editor.target().timeline().tracks[0].notifies[0].time, eve::Duration::fromNanoseconds(20));
}

TEST_CASE("actionTimelineWidget.commandsAndShortcutsAreUndoable") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    REQUIRE(editor.selectItem(id("combat-notify:damage")).ok());
    REQUIRE(view.handleShortcut("Ctrl+C").ok());
    REQUIRE(view.seek(720.0f).ok());
    REQUIRE(view.handleShortcut("Ctrl+V").ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies.size(), 2u);
    REQUIRE(view.handleShortcut("Delete").ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies.size(), 1u);
    REQUIRE(view.handleShortcut("Ctrl+Z").ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies.size(), 2u);
    CHECK(!view.handleShortcut("Ctrl+Unknown").ok());
}

TEST_CASE("actionTimelineWidget.insertsRegisteredNotifyShapesAtCursor") {
    eve::editor::ActionTimelineEditor editor("asset.combat.widget", timelineFixture());
    auto                              registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto view = widget(editor, registry.value());
    REQUIRE(view.seek(620.0f).ok());
    const auto track = id("combat-track:gameplay");
    REQUIRE(view.addNotifyAtCursor(track, "presentation:vfx",
                                   {{"uri", eve::Value("asset://vfx/slash")}, {"lifetimeSeconds", eve::Value(0.5)}})
                .ok());
    REQUIRE(view.addStateAtCursor(track, "input:combo-window", eve::Duration::fromNanoseconds(10),
                                  {{"input", eve::Value("Attack.Heavy")}})
                .ok());
    CHECK_EQ(editor.target().timeline().tracks[0].notifies.size(), 2u);
    CHECK_EQ(editor.target().timeline().tracks[0].states.size(), 2u);
    CHECK_EQ(view.insertableTypes(eve::action::ActionNotifyShape::Instant).size(), 5u);
    CHECK_EQ(view.insertableTypes(eve::action::ActionNotifyShape::State).size(), 7u);
    CHECK(!view.addNotifyAtCursor(track, "combat:hitbox-window", {{"hitbox", eve::Value("weapon")}}).ok());
}
}  // namespace test_action_timeline_widget
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_agent_composition)
#include "PrivateTestScope.h"
#include "../examples/agent/Environments.h"
#include "agent/Agent.h"
#include "grid/GridProjection.h"
#include "ui/Layout.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// agent_composition.cpp
namespace test_agent_composition {
using namespace eve::agent;

TEST_CASE("agent.productionGridGame") {
    agent_examples::GridGame game;
    Config                   c;
    c.featureCount = 2;
    c.actionCount  = 4;
    c.horizon      = 24;
    auto result    = run(c, game);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().failures, 0u);
    REQUIRE(result.value().best.steps.back().observation.outcome == Outcome::Success);
    REQUIRE(replay(result.value().best, game).ok());
    REQUIRE(result.value().trainingSamples > 0);
}

TEST_CASE("agent.productionUiLayout") {
    agent_examples::UiLayout ui;
    Config                   c;
    c.featureCount = 4;
    c.actionCount  = 5;
    c.population   = 12;
    c.elites       = 3;
    c.generations  = 2;
    c.strategy     = Strategy::Random;
    auto result    = run(c, ui);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().failures, 0u);
    REQUIRE(result.value().coverage.size() > 15);
    REQUIRE(replay(result.value().best, ui).ok());
}
}  // namespace test_agent_composition
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_agent_tensor)
#include "PrivateTestScope.h"
#include <cmath>
#include "agent/Agent.h"
#include "agent/tensor/TensorBackend.h"
#include "common/Capability.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// agent_tensor.cpp
namespace test_agent_tensor {
TEST_CASE("agent.tensorParityAndProviderLifetime") {
    using namespace eve::agent;
    Policy p;
    p.featureCount = 2;
    p.actionCount  = 3;
    p.hiddenWidth  = 2;
    p.weights.resize(21);
    for (std::size_t i = 0; i < p.weights.size(); ++i) p.weights[i] = std::sin(double(i)) * 0.5;
    Observation o;
    o.features     = {0.4f, -0.2f};
    o.legalActions = {0, 2};
    auto absent    = infer(p, o, Backend::Tensor);
    REQUIRE(!absent.ok());
    REQUIRE(absent.status().code() == eve::StatusCode::Unsupported);
    auto factory = makeTensorBackend();
    REQUIRE(factory.ok());
    auto provider = std::move(factory).takeValue();
    struct Registration {
        IPolicyBackend& backend;
        explicit Registration(IPolicyBackend& b) : backend(b) { eve::cap::provide<IPolicyBackend>(&b); }
        ~Registration() { eve::cap::revoke<IPolicyBackend>(&backend); }
    };
    {
        Registration registration(*provider);
        for (int sample = 0; sample < 32; ++sample) {
            o.features  = {float(std::sin(sample)), float(std::cos(sample))};
            auto cpu    = infer(p, o);
            auto tensor = infer(p, o, Backend::Tensor);
            REQUIRE(cpu.ok());
            REQUIRE(tensor.ok());
            for (std::size_t i = 0; i < cpu.value().size(); ++i)
                REQUIRE(std::abs(cpu.value()[i] - tensor.value()[i]) < 1e-5);
            REQUIRE_EQ(tensor.value()[1], 0.0);
        }
    }
    REQUIRE(!backendName(Backend::Tensor).ok());
}
}  // namespace test_agent_tensor
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_action_preview)
#include "PrivateTestScope.h"
#include <cmath>
#include "action/editor/AnimationRootMotionPreviewSource.h"
#include "animation/AnimClip.h"
#include "animation/AnimClipRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_action_preview.cpp
namespace test_animation_action_preview {
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
}  // namespace test_animation_action_preview
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_inertializer)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "animation/AnimClip.h"
#include "animation/AnimClipRegistry.h"
#include "animation/AnimImporter.h"
#include "animation/AnimInertializer.h"
#include "animation/AnimLattice.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/Animation.h"
#include "animation/DynamicBoneSolver.h"
#include "animation/FootIKSolver.h"
#include "animation/OrientationWarping.h"
#include "animation/PhysicalBalancePose.h"
#include "animation/ProceduralBoneConfig.h"
#include "animation/editing/ProceduralBoneOverlay.h"
#include "common/Exception.h"
#include "common/Status.h"
#include "data/ByteData.h"
#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_inertializer.cpp
namespace test_animation_inertializer {
// animation_inertializer.cpp
using namespace eve::animation;

TEST_CASE("animation.inertializer.ownsHistoryAndHonorsBoneDurations") {
    AnimInertializer inertia;
    AnimPose         source(3), previous(3), target(3), priorTarget(3);
    for (int i = 0; i < 3; ++i) source.setLocalPosition(i, 2.f, 0.f, 0.f);
    previous.copyFrom(&source);
    std::array<float, 3> factors{0.f, .75f, 1.f};
    REQUIRE(inertia.begin(source, previous, target, priorTarget, .01f, .2f, factors).ok());
    CHECK(inertia.pose().getLocalPositionX(0) == 0.f);
    CHECK(inertia.pose().getLocalPositionX(1) == 2.f);
    source.setLocalPosition(1, 100.f, 0.f, 0.f);
    factors[1] = 1.f;
    REQUIRE(inertia.evaluate(target, .16f).ok());
    CHECK(inertia.pose().getLocalPositionX(1) == 0.f);
    CHECK(inertia.pose().getLocalPositionX(2) > 0.f);
    REQUIRE(inertia.evaluate(target, .2f).ok());
    CHECK(inertia.pose().getLocalPositionX(2) == 0.f);
    // Evaluating absolute time again has no integration/order dependence.
    REQUIRE(inertia.evaluate(target, .16f).ok());
    float atSixteen = inertia.pose().getLocalPositionX(2);
    REQUIRE(inertia.evaluate(target, .03f).ok());
    REQUIRE(inertia.evaluate(target, .16f).ok());
    CHECK(inertia.pose().getLocalPositionX(2) == atSixteen);
}

TEST_CASE("animation.inertializer.invalidInputsLeaveTransitionAndOutputUnchanged") {
    AnimInertializer inertia;
    AnimPose         source(1), previous(1), target(1), priorTarget(1), wrong(2);
    CHECK(!inertia.evaluate(target, 0.f).ok());
    source.setLocalPosition(0, 2.f, 0.f, 0.f);
    previous.copyFrom(&source);
    REQUIRE(inertia.begin(source, previous, target, priorTarget, .01f, .2f).ok());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(!inertia.begin(source, previous, wrong, priorTarget, .01f, .2f).ok());
    CHECK(!inertia.begin(source, previous, target, priorTarget, 0.f, .2f).ok());
    CHECK(!inertia.begin(source, previous, target, priorTarget, .01f, nan).ok());
    std::array<float, 1> factors{-1.f};
    CHECK(!inertia.begin(source, previous, target, priorTarget, .01f, .2f, factors).ok());
    CHECK(!inertia.evaluate(target, -1.f).ok());
    CHECK(!inertia.evaluate(wrong, 0.f).ok());
    target.setLocalPosition(0, nan, 0.f, 0.f);
    CHECK(!inertia.evaluate(target, .1f).ok());
    CHECK(inertia.pose().getLocalPositionX(0) == 2.f);
    target.setLocalPosition(0, 0.f, 0.f, 0.f);
    REQUIRE(inertia.evaluate(target, .2f).ok());
    CHECK(inertia.pose().getLocalPositionX(0) == 0.f);
}

TEST_CASE("animation.inertializer.interruptionPreservesMovingRotationAndVelocity") {
    AnimInertializer inertia;
    AnimPose         source(1), previous(1), target(1), priorTarget(1);
    source.setLocalPosition(0, 2.f, 0.f, 0.f);
    previous.setLocalPosition(0, 1.97f, 0.f, 0.f);
    source.setLocalRotation(0, 0.f, std::sin(.4f), 0.f, std::cos(.4f));
    previous.setLocalRotation(0, 0.f, std::sin(.39f), 0.f, std::cos(.39f));
    target.setLocalRotation(0, 0.f, std::sin(.1f), 0.f, std::cos(.1f));
    priorTarget.setLocalRotation(0, 0.f, std::sin(.095f), 0.f, std::cos(.095f));
    REQUIRE(inertia.begin(source, previous, target, priorTarget, .01f, .2f).ok());
    target.setLocalRotation(0, 0.f, std::sin(.10005f), 0.f, std::cos(.10005f));
    REQUIRE(inertia.evaluate(target, .0001f).ok());
    auto q = inertia.pose().local(0);
    CHECK(std::abs((2.f * std::atan2(q.qy, q.qw) - .8f) / .0001f - 2.f) < .02f);
    CHECK(std::abs((q.px - 2.f) / .0001f - 3.f) < .02f);
    REQUIRE(inertia.evaluate(target, .08f).ok());
    previous.copyFrom(&inertia.pose());
    REQUIRE(inertia.evaluate(target, .09f).ok());
    source.copyFrom(&inertia.pose());
    float expectedVelocity = (source.local(0).px - previous.local(0).px) / .01f;
    target.setLocalPosition(0, -3.f, 0.f, 0.f);
    priorTarget.copyFrom(&target);
    REQUIRE(inertia.begin(source, previous, target, priorTarget, .01f, .15f).ok());
    CHECK(std::abs(inertia.pose().local(0).px - source.local(0).px) < 1e-5f);
    REQUIRE(inertia.evaluate(target, .0001f).ok());
    CHECK(std::abs((inertia.pose().local(0).px - source.local(0).px) / .0001f - expectedVelocity) < .03f);
}

// animation_lattice.cpp
using namespace eve::animation;

namespace {

bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

/** @brief Minimal 4-vertex quad OBJ used to exercise the ModelData bind path. */
const char* kQuadObj =
    "v -1 -1 0\n"
    "v 1 -1 0\n"
    "v 1 1 0\n"
    "v -1 1 0\n"
    "f 1 2 3\n"
    "f 1 3 4\n";

eve::model3d::ModelData* loadQuadModel() {
    auto* fs = eve::filesystem::Filesystem::create();
    if (!fs) return nullptr;
    fs->setIdentity("animation_lattice_test", true);
    fs->setupWriteDirectory();
    eve::data::ByteData bytes(kQuadObj, std::strlen(kQuadObj));
    auto*               mod = eve::model3d::Model3D::create();
    return mod->newModelData(&bytes, ".obj");
}

}  // namespace

TEST_CASE("animation.lattice.identityDefault") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);

    const float bind[9] = {-0.75f, 0.25f, 0.5f, 0.1f, -0.3f, 0.9f, 0.4f, 0.6f, -0.8f};
    lat.bindPositions(bind, 3);
    REQUIRE(lat.getVertexCount() == 3);

    std::vector<float> out;
    REQUIRE(lat.updateDeformedPositions());
    for (int v = 0; v < 3; ++v) {
        CHECK(approx(lat.getDeformedPositionX(v), bind[v * 3 + 0]));
        CHECK(approx(lat.getDeformedPositionY(v), bind[v * 3 + 1]));
        CHECK(approx(lat.getDeformedPositionZ(v), bind[v * 3 + 2]));
    }

    // Default-constructed lattice is a valid identity too.
    AnimLattice def;
    CHECK(def.getPointCount() == 8);
    std::vector<float> defOut;
    REQUIRE(def.deformPositionsTo({0.5f, 0.5f, 0.5f}, defOut));
    CHECK(approx(defOut[0], 0.5f));
    CHECK(approx(defOut[1], 0.5f));
    CHECK(approx(defOut[2], 0.5f));
}

TEST_CASE("animation.lattice.uniformScale") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setScale(2.f, 1.f, 0.5f);

    const float bind[9] = {0.5f, 0.f, 0.f, 0.f, 0.f, 0.f, -0.5f, 0.75f, 1.f};
    lat.bindPositions(bind, 3);
    REQUIRE(lat.updateDeformedPositions());

    // Scaling happens about the lattice origin.
    CHECK(approx(lat.getDeformedPositionX(0), 1.f));
    CHECK(approx(lat.getDeformedPositionY(0), 0.f));
    CHECK(approx(lat.getDeformedPositionZ(0), 0.f));
    CHECK(approx(lat.getDeformedPositionX(1), 0.f));
    CHECK(approx(lat.getDeformedPositionY(1), 0.f));
    CHECK(approx(lat.getDeformedPositionZ(1), 0.f));
    CHECK(approx(lat.getDeformedPositionX(2), -1.f));
    CHECK(approx(lat.getDeformedPositionY(2), 0.75f));
    CHECK(approx(lat.getDeformedPositionZ(2), 0.5f));
}

TEST_CASE("animation.lattice.cornerScaleTrilinear") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setPointScale(1, 1, 1, 2.f, 1.f, 1.f);

    // Corner vertex: exactly the scaled control point.
    std::vector<float> corner = {1.f, 1.f, 1.f};
    std::vector<float> out;
    REQUIRE(lat.deformPositionsTo(corner, out));
    CHECK(approx(out[0], 2.f));
    CHECK(approx(out[1], 1.f));
    CHECK(approx(out[2], 1.f));

    // The box spans [-1,1]; at (0.5,0.5,0.5) the upper-corner weight is 0.75^3.
    std::vector<float> center = {0.5f, 0.5f, 0.5f};
    REQUIRE(lat.deformPositionsTo(center, out));
    CHECK(approx(out[0], 0.5f * (1.f + 0.75f * 0.75f * 0.75f)));
    CHECK(approx(out[1], 0.5f));
    CHECK(approx(out[2], 0.5f));
}

TEST_CASE("animation.lattice.offsetBulge") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setPointOffset(1, 1, 1, 0.4f, -0.2f, 0.f);

    std::vector<float> corner = {1.f, 1.f, 1.f};
    std::vector<float> out;
    REQUIRE(lat.deformPositionsTo(corner, out));
    CHECK(approx(out[0], 1.4f));
    CHECK(approx(out[1], 0.8f));
    CHECK(approx(out[2], 1.f));

    // Interior point: upper-corner offset weighted by 0.75^3.
    std::vector<float> center = {0.5f, 0.5f, 0.5f};
    REQUIRE(lat.deformPositionsTo(center, out));
    CHECK(approx(out[0], 0.5f + 0.4f * 0.75f * 0.75f * 0.75f));
    CHECK(approx(out[1], 0.5f - 0.2f * 0.75f * 0.75f * 0.75f));
    CHECK(approx(out[2], 0.5f));
}

TEST_CASE("animation.lattice.clampVsExtrapolate") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    for (int y = 0; y < 2; ++y)
        for (int z = 0; z < 2; ++z) lat.setPointScale(1, y, z, 2.f, 1.f, 1.f);

    // Vertex outside the box on +x: u = 1.5.
    std::vector<float> p = {2.f, 0.f, 0.f};
    std::vector<float> out;

    lat.setClamp(true);
    REQUIRE(lat.deformPositionsTo(p, out));
    CHECK(approx(out[0], 4.f));  // clamped to the corner cell scale

    lat.setClamp(false);
    REQUIRE(lat.deformPositionsTo(p, out));
    CHECK(approx(out[0], 5.f));  // linear extrapolation keeps growing
    p[0] = -2.f;
    REQUIRE(lat.deformPositionsTo(p, out));
    CHECK(approx(out[0], -1.f));  // extrapolate from the lower boundary too
}

TEST_CASE("animation.lattice.normals") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setScale(2.f, 1.f, 1.f);

    const float pos[9] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    const float nrm[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f};
    float       out[9];
    REQUIRE(lat.deformNormals(pos, nrm, out, 3));

    CHECK(approx(out[0], 1.f));
    CHECK(approx(out[1], 0.f));
    CHECK(approx(out[2], 0.f));
    CHECK(approx(out[3], 0.f));
    CHECK(approx(out[4], 1.f));
    CHECK(approx(out[5], 0.f));
    // (2,1,0) normalized.
    const float inv = 1.f / std::sqrt(5.f);
    CHECK(approx(out[6], 2.f * inv));
    CHECK(approx(out[7], 1.f * inv));
    CHECK(approx(out[8], 0.f));
}

TEST_CASE("animation.lattice.bindModelAndFromModel") {
    std::unique_ptr<eve::model3d::ModelData> model(loadQuadModel());
    REQUIRE(model.get() != nullptr);
    REQUIRE(model->getMeshCount() >= 1);
    REQUIRE(model->getVertexCount(0) == 4);

    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.bindModel(model.get(), 0);
    REQUIRE(lat.getVertexCount() == 4);

    // Bind positions match the OBJ vertices.
    CHECK(approx(lat.getBindPositionX(0), -1.f));
    CHECK(approx(lat.getBindPositionY(1), -1.f));
    CHECK(approx(lat.getBindPositionZ(2), 0.f));

    // fromModel factory binds the same vertices with identity deformation.
    std::unique_ptr<AnimLattice> fromModel(AnimLattice::fromModel(model.get(), 0, 2, 2, 2));
    REQUIRE(fromModel.get() != nullptr);
    REQUIRE(fromModel->getVertexCount() == 4);
    REQUIRE(fromModel->updateDeformedPositions());
    CHECK(approx(fromModel->getDeformedPositionX(0), -1.f));
    CHECK(approx(fromModel->getDeformedPositionY(3), 1.f));
}

TEST_CASE("animation.lattice.validation") {
    AnimLattice lat(2, 2, 2);

    CHECK_THROWS((lat.setDivisions(1, 2, 2), false));
    CHECK_THROWS((lat.setDivisions(2, 0, 2), false));
    CHECK_THROWS((lat.setSize(0.f, 1.f, 1.f), false));
    CHECK_THROWS((lat.setPointScale(2, 0, 0, 1.f, 1.f, 1.f), false));
    CHECK_THROWS((lat.setPointOffset(0, -1, 0, 0.f, 0.f, 0.f), false));
    CHECK_THROWS((lat.getPointScaleX(0, 0, 2), false));
    CHECK_THROWS((lat.bindPositions(nullptr, 3), false));
    CHECK_THROWS((lat.getBindPositionX(0), false));
    CHECK_THROWS((lat.getDeformedPositionX(0), false));

    const float pos[3] = {0.f, 0.f, 0.f};
    lat.bindPositions(pos, 1);
    CHECK_THROWS((lat.getBindPositionY(1), false));
}

TEST_CASE("animation.lattice.cacheMatchesArray") {
    AnimLattice lat(3, 3, 3);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);

    std::vector<float> pos = {-0.8f, 0.2f, 0.1f, 0.7f, -0.6f, 0.9f, 0.f, 0.f, 0.f};
    std::vector<float> nrm = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    lat.bindPositions(pos.data(), 3);
    lat.setPointScale(1, 1, 1, 1.5f, 1.f, 1.f);

    REQUIRE(lat.updateDeformedPositions(pos));
    REQUIRE(lat.updateDeformedNormals(pos, nrm));

    const std::vector<float> outPos = lat.getDeformedPositions();
    const std::vector<float> outNrm = lat.getDeformedNormals();
    REQUIRE(outPos.size() == 9u);
    REQUIRE(outNrm.size() == 9u);
    for (int v = 0; v < 3; ++v) {
        CHECK(approx(lat.getDeformedPositionX(v), outPos[v * 3 + 0]));
        CHECK(approx(lat.getDeformedPositionY(v), outPos[v * 3 + 1]));
        CHECK(approx(lat.getDeformedPositionZ(v), outPos[v * 3 + 2]));
    }
    CHECK(approx(outNrm[0], 0.f));
    CHECK(approx(outNrm[1], 0.f));
    CHECK(approx(outNrm[2], 1.f));

    lat.clearBind();
    CHECK(lat.getVertexCount() == 0);
    CHECK(lat.getDeformedPositions().empty());
}

// animation_orientation_warping.cpp
using namespace eve::animation;

namespace {
constexpr float kPi = 3.14159265359f;

float yawOf(const AnimPose& pose, int bone) {
    const auto& t = pose.world(bone);
    return std::atan2(2.f * (t.qw * t.qy + t.qx * t.qz), 1.f - 2.f * (t.qx * t.qx + t.qy * t.qy));
}

void makeWarpSkeleton(AnimSkeleton& skeleton) {
    skeleton.addBone("root");
    skeleton.addBone("spine", 0);
    skeleton.addBone("head", 1);
    skeleton.addBone("foot", 0);
}

AnimPose identityPose(int bones) { return AnimPose(bones); }
}  // namespace

TEST_CASE("animation.orientationWarping.rotatesRootTowardLocomotionAndLeavesMatcherUntouched") {
    AnimSkeleton skeleton;
    makeWarpSkeleton(skeleton);
    OrientationWarping warp;
    REQUIRE(warp.configure(skeleton, 0).ok());
    REQUIRE(warp.setRotationInterpSpeed(0.f).ok());
    AnimPose matcherPose = identityPose(4);
    matcherPose.setLocalPosition(0, 0.f, 0.f, 1.f);
    AnimPose display = identityPose(4);
    display.copyFrom(&matcherPose);
    REQUIRE(warp.apply(display, 1.f, 0.f, 0.f, 2.f, 1.f / 60.f).ok());
    display.computeWorld(&skeleton);
    CHECK(std::fabs(yawOf(display, 0) - kPi * 0.5f) < 0.02f);
    CHECK(std::fabs(warp.getAppliedAngle() - kPi * 0.5f) < 0.02f);
    CHECK_EQ(matcherPose.getLocalPositionZ(0), 1.f);
    CHECK_EQ(matcherPose.getLocalRotationY(0), 0.f);
    CHECK_EQ(matcherPose.getLocalRotationW(0), 1.f);
}

TEST_CASE("animation.orientationWarping.distributesSpineShareAndRestoresIkWorldYaw") {
    AnimSkeleton skeleton;
    makeWarpSkeleton(skeleton);
    OrientationWarping       warp;
    const std::array<int, 2> spine{1, 2};
    const std::array<int, 1> ik{3};
    REQUIRE(warp.configure(skeleton, 0, spine, ik).ok());
    REQUIRE(warp.setDistributedAlpha(0.5f).ok());
    REQUIRE(warp.setRotationInterpSpeed(0.f).ok());
    AnimPose pose = identityPose(4);
    REQUIRE(warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.016f).ok());
    pose.computeWorld(&skeleton);
    CHECK(std::fabs(yawOf(pose, 0) - kPi * 0.25f) < 0.02f);
    CHECK(std::fabs(yawOf(pose, 2) - kPi * 0.5f) < 0.03f);
    CHECK(std::fabs(yawOf(pose, 3)) < 0.02f);
}

TEST_CASE("animation.orientationWarping.thresholdAndLowSpeedTargetZero") {
    AnimSkeleton skeleton;
    makeWarpSkeleton(skeleton);
    OrientationWarping warp;
    REQUIRE(warp.configure(skeleton, 0).ok());
    REQUIRE(warp.setRotationInterpSpeed(0.f).ok());
    REQUIRE(warp.setAngleThreshold(kPi * 0.5f).ok());
    AnimPose pose = identityPose(4);
    REQUIRE(warp.apply(pose, -1.f, 0.f, 0.f, 1.f, 0.016f).ok());
    CHECK(std::fabs(warp.getTargetAngle()) < 1e-5f);
    CHECK(std::fabs(warp.getAppliedAngle()) < 1e-5f);
    REQUIRE(warp.setAngleThreshold(kPi).ok());
    REQUIRE(warp.setMinRootMotionSpeed(1.f).ok());
    REQUIRE(warp.apply(pose, 0.2f, 0.f, 0.f, 0.2f, 0.016f).ok());
    CHECK(std::fabs(warp.getTargetAngle()) < 1e-5f);
}

TEST_CASE("animation.orientationWarping.invalidInputsAndDisabledApplyLeavePoseUnchanged") {
    AnimSkeleton skeleton;
    makeWarpSkeleton(skeleton);
    OrientationWarping warp;
    AnimPose           pose = identityPose(4);
    pose.setLocalPosition(0, 3.f, 0.f, 0.f);
    CHECK(!warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.016f).ok());
    CHECK_EQ(pose.getLocalPositionX(0), 3.f);
    const std::array<int, 1> bad{9};
    CHECK(!warp.configure(skeleton, 0, bad).ok());
    REQUIRE(warp.configure(skeleton, 0).ok());
    REQUIRE(warp.setRotationInterpSpeed(0.f).ok());
    REQUIRE(warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.016f).ok());
    const float warped = pose.getLocalRotationY(0);
    CHECK(std::fabs(warped) > 0.1f);
    warp.setEnabled(false);
    AnimPose disabled = identityPose(4);
    disabled.setLocalPosition(0, 3.f, 0.f, 0.f);
    auto skipped = warp.apply(disabled, 1.f, 0.f, 0.f, 1.f, 0.016f);
    REQUIRE(skipped.ok());
    CHECK_EQ(disabled.getLocalPositionX(0), 3.f);
    CHECK_EQ(disabled.getLocalRotationY(0), 0.f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(!warp.setDistributedAlpha(nan).ok());
    CHECK(!warp.setAngleThreshold(0.f).ok());
    CHECK(!warp.apply(pose, nan, 0.f, 0.f, 1.f, 0.016f).ok());
    CHECK_EQ(pose.getLocalRotationY(0), warped);
}

TEST_CASE("animation.orientationWarping.interpolationUsesInjectedTime") {
    AnimSkeleton skeleton;
    makeWarpSkeleton(skeleton);
    OrientationWarping warp;
    REQUIRE(warp.configure(skeleton, 0).ok());
    REQUIRE(warp.setRotationInterpSpeed(8.f).ok());
    AnimPose pose = identityPose(4);
    REQUIRE(warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.05f).ok());
    const float first = warp.getAppliedAngle();
    CHECK(first > 0.f);
    CHECK(first < kPi * 0.5f);
    REQUIRE(warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.05f).ok());
    CHECK(warp.getAppliedAngle() > first);
}

// animation_physical_balance.cpp
using namespace eve::animation;

namespace {

std::unique_ptr<AnimSkeleton> makeBalanceSkeleton() {
    auto sk   = std::make_unique<AnimSkeleton>();
    int  root = sk->addBone("root", -1);
    sk->setBindPosition(root, 0.f, 0.f, 0.f);
    int spine = sk->addBone("spine", root);
    sk->setBindPosition(spine, 0.f, 1.f, 0.f);
    int chest = sk->addBone("chest", spine);
    sk->setBindPosition(chest, 0.f, 0.8f, 0.f);
    return sk;
}

}  // namespace

TEST_CASE("animation.physicalBalance.matchesTargetWithoutImpulse") {
    auto                sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    REQUIRE(balance.setBalanceBone(1).ok());
    REQUIRE(balance.setSupportBone(0).ok());
    AnimPose target(sk->getBoneCount());
    sk->applyBindPose(&target);
    target.setLocalRotation(1, 0.f, 0.f, 0.f, 1.f);
    REQUIRE(balance.setTargetPose(&target).ok());
    balance.update(1.f / 60.f);
    CHECK(std::fabs(balance.getLeanX()) < 1e-5f);
    CHECK(std::fabs(balance.getLeanZ()) < 1e-5f);
    CHECK(std::fabs(balance.getPose()->getLocalRotationW(1) - 1.f) < 1e-4f);
}

TEST_CASE("animation.physicalBalance.impulseLeansThenRecovers") {
    auto                sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    REQUIRE(balance.setBalanceBone(1).ok());
    REQUIRE(balance.setSupportBone(0).ok());
    REQUIRE(balance.setRecovery(2.f, 0.45f).ok());
    REQUIRE(balance.setInertia(1.f).ok());
    AnimPose target(sk->getBoneCount());
    sk->applyBindPose(&target);
    REQUIRE(balance.setTargetPose(&target).ok());
    REQUIRE(balance.applyImpulse(1, 8.f, 0.f, 0.f, 0.f, 1.2f, 0.f).ok());
    balance.update(1.f / 60.f);
    const float firstLean = balance.getLeanZ();
    CHECK(std::fabs(firstLean) > 1e-4f);
    // +X impulse at a point above the support moves COM toward +X.
    CHECK(balance.getCenterOfMassX() > 0.f);

    float maxAbs  = std::fabs(firstLean);
    bool  crossed = false;
    float prevVel = balance.getLeanVelocityZ();
    for (int i = 0; i < 180; ++i) {
        balance.update(1.f / 60.f);
        maxAbs = std::max(maxAbs, std::fabs(balance.getLeanZ()));
        if (prevVel * balance.getLeanVelocityZ() < 0.f) crossed = true;
        prevVel = balance.getLeanVelocityZ();
    }
    CHECK(std::fabs(balance.getLeanZ()) < 0.05f);
    CHECK(crossed);
    CHECK(maxAbs >= std::fabs(firstLean));
}

TEST_CASE("animation.physicalBalance.recoilDeflectsHitBone") {
    auto                sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    REQUIRE(balance.setBalanceBone(1).ok());
    REQUIRE(balance.setRecoil(3.f, 0.5f).ok());
    AnimPose target(sk->getBoneCount());
    sk->applyBindPose(&target);
    REQUIRE(balance.setTargetPose(&target).ok());
    REQUIRE(balance.applyImpulse(2, 0.f, 0.f, 4.f, 0.f, 1.8f, 0.1f).ok());
    balance.update(1.f / 60.f);
    const float hitW = balance.getPose()->getLocalRotationW(2);
    CHECK(hitW < 0.999f);
    for (int i = 0; i < 180; ++i) balance.update(1.f / 60.f);
    CHECK(std::fabs(balance.getPose()->getLocalRotationW(2) - 1.f) < 0.02f);
}

TEST_CASE("animation.physicalBalance.rejectsUnrecoverableGravity") {
    auto                sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    CHECK(!balance.setRecovery(0.2f, 0.5f).ok());
    CHECK(!balance.setGravity(500.f).ok());
    CHECK(std::fabs(balance.getRecoveryFrequency() - 2.f) < 1e-6f);
    CHECK(std::fabs(balance.getGravity() - 9.81f) < 1e-5f);
}

TEST_CASE("animation.physicalBalance.rejectsBadImpulse") {
    auto                sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    CHECK(!balance.applyImpulse(-1, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f).ok());
    CHECK(!balance.applyImpulse(0, std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f, 0.f, 0.f, 0.f).ok());
    auto zero = balance.applyImpulse(0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    REQUIRE(zero.ok());
    CHECK(zero.status().code() == eve::StatusCode::NoOp);
}

TEST_CASE("animation.physicalBalance.followsAnimPlayerTarget") {
    auto sk   = makeBalanceSkeleton();
    auto clip = std::make_unique<AnimClip>("idle");
    clip->setDuration(1.f);
    clip->addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    clip->addPositionKey(1, 1.f, 0.f, 1.f, 0.f);
    AnimPlayer player(sk.get());
    player.play(clip.get());
    player.update(0.1f);

    PhysicalBalancePose balance(sk.get());
    REQUIRE(balance.setBalanceBone(1).ok());
    REQUIRE(balance.setTargetPose(player.getPose()).ok());
    REQUIRE(balance.applyImpulse(1, 5.f, 0.f, 0.f, 0.f, 1.f, 0.f).ok());
    balance.update(1.f / 60.f);
    CHECK(std::fabs(balance.getLeanZ()) > 1e-4f);
    CHECK(std::fabs(balance.getPose()->getLocalPositionY(1) - 1.f) < 0.05f);
}

// animation_procedural_bones.cpp
using namespace eve::animation;

namespace {
class TestForceField final : public DynamicBoneForceField {
public:
    void sampleForce(float x, float, float, float, float& outX, float& outY, float& outZ) const override {
        outX = x >= 0.f ? 8.f : -8.f;
        outY = 0.f;
        outZ = 0.f;
    }
};

class TestGroundProvider final : public FootIKGroundProvider {
public:
    mutable bool            hit = true;
    FootIKGroundQueryStatus queryGround(float x, float, float z, float, float& hitX, float& hitY, float& hitZ,
                                        float& nx, float& ny, float& nz) const override {
        if (!hit) return FootIKGroundQueryStatus::NoHit;
        hitX = x;
        hitY = -2.25f;
        hitZ = z;
        nx   = 0.f;
        ny   = 1.f;
        nz   = 0.f;
        return FootIKGroundQueryStatus::Hit;
    }
};

float distance(const AnimPose& pose, int a, int b) {
    const float x = pose.getWorldPositionX(a) - pose.getWorldPositionX(b);
    const float y = pose.getWorldPositionY(a) - pose.getWorldPositionY(b);
    const float z = pose.getWorldPositionZ(a) - pose.getWorldPositionZ(b);
    return std::sqrt(x * x + y * y + z * z);
}
}  // namespace

TEST_CASE("animation.procedural.dynamicBoneRotatesAndPreservesBoneLengths") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("hair_root");
    const int    mid  = skeleton.addBone("hair_mid", root);
    const int    tip  = skeleton.addBone("hair_tip", mid);
    skeleton.setBindPosition(mid, 0.f, -1.f, 0.f);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    pose.computeWorld(&skeleton);

    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(10.f, 0.f, 0.f);
    CHECK(solver.addChain(root, tip, 0.f, 0.f, 1.f, 1.f, 0.f, 6) == 0);
    solver.update(&pose, 0.1f);

    CHECK(pose.getWorldPositionX(tip) > 0.01f);
    CHECK(std::fabs(distance(pose, root, mid) - 1.f) < 1e-3f);
    CHECK(std::fabs(distance(pose, mid, tip) - 1.f) < 1e-3f);
    CHECK(std::fabs(pose.getLocalPositionX(mid)) < 1e-6f);
    CHECK(std::fabs(pose.getLocalPositionY(mid) + 1.f) < 1e-6f);
}

TEST_CASE("animation.procedural.dynamicBoneSameSkeletonRebindPreservesConfiguration") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, 1.f, 0.f);
    DynamicBoneSolver solver(&skeleton);
    CHECK(solver.addChain(root, tip, 0.2f, 0.3f, 0.8f) == 0);
    solver.setSkeleton(&skeleton);
    CHECK(solver.getChainCount() == 1);
}

TEST_CASE("animation.procedural.dynamicBoneFreezeAxisAndCapsuleCollision") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(10.f, 0.f, 0.f);
    const int chain = solver.addChain(root, tip, 0.f, 0.f, 1.f, 1.f, 0.1f, 8);
    REQUIRE(chain == 0);
    solver.setChainFreezeAxis(chain, 3);
    solver.addColliderCapsule(0.15f, -1.5f, -0.5f, 0.15f, -0.5f, 0.5f, 0.25f);
    CHECK(solver.getColliderCount() == 1);
    solver.update(&pose, 0.1f);

    CHECK(pose.getWorldPositionX(tip) < -0.01f);
    CHECK(std::fabs(pose.getWorldPositionZ(tip)) < 1e-4f);
    CHECK(std::fabs(distance(pose, root, tip) - 1.f) < 1e-3f);
}

TEST_CASE("animation.procedural.dynamicBoneColliderModesAreRuntimeConfigurable") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(10.f, 0.f, 0.f);
    REQUIRE(solver.addChain(root, tip, 0.f, 0.f, 1.f, 1.f, 0.1f, 8) == 0);
    solver.addColliderSphere(0.f, -1.f, 0.f, 0.5f);
    solver.setColliderInside(0, true);
    solver.setColliderRadius(0, 0.25f);
    solver.update(&pose, 0.1f);
    CHECK(pose.getWorldPositionX(tip) <= 0.151f);

    skeleton.applyBindPose(&pose);
    solver.reset();
    solver.setColliderEnabled(0, false);
    solver.update(&pose, 0.1f);
    CHECK(pose.getWorldPositionX(tip) > 0.151f);
}

TEST_CASE("animation.procedural.dynamicBoneSupportsParticleProfilesAndVirtualEnds") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(10.f, 0.f, 0.f);
    const int chain = solver.addChain(root, tip, 0.f, 0.f, 1.f, 1.f, 0.f, 8);
    REQUIRE(chain == 0);
    solver.setChainEndLength(chain, 0.5f);
    solver.setChainParticleParameters(chain, 1, 1.f, 1.f, 0.f, 0.f, 0.f);
    solver.setChainParticleParameters(chain, 2, 0.f, 0.f, 1.f, 1.f, 0.f);
    solver.update(&pose, 0.1f);

    CHECK(std::fabs(pose.getWorldPositionX(tip)) < 1e-4f);
    const float localRotationMagnitude = std::fabs(pose.getLocalRotationX(tip)) +
                                         std::fabs(pose.getLocalRotationY(tip)) +
                                         std::fabs(pose.getLocalRotationZ(tip));
    CHECK(localRotationMagnitude > 1e-4f);
}

TEST_CASE("animation.procedural.dynamicBoneHandlesObjectMotionAndDistanceSleep") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(0.f, 0.f, 0.f);
    const int chain = solver.addChain(root, tip, 0.f, 1.f, 0.f, 0.f, 0.f, 4);
    REQUIRE(chain == 0);
    solver.update(&pose, 1.f / 60.f);
    solver.setObjectMoveResponse(0.f);
    pose.setLocalPosition(root, 1.f, 0.f, 0.f);
    solver.update(&pose, 1.f / 60.f);
    pose.computeWorld(&skeleton);
    CHECK(pose.getWorldPositionX(tip) < 0.9f);

    solver.setDistanceReference(100.f, 0.f, 0.f);
    solver.setDistanceLimit(1.f);
    solver.update(&pose, 1.f / 60.f);
    CHECK(solver.isChainSleeping(chain));
    solver.setDistanceReference(1.f, 0.f, 0.f);
    solver.update(&pose, 1.f / 60.f);
    CHECK(!solver.isChainSleeping(chain));
}

TEST_CASE("animation.procedural.dynamicBoneSamplesSpatialForceField") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    TestForceField    field;
    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(0.f, 0.f, 0.f);
    solver.setForceField(&field);
    REQUIRE(solver.getForceField() == &field);
    REQUIRE(solver.addChain(root, tip, 0.f, 0.f, 1.f, 1.f, 0.f, 6) == 0);
    solver.update(&pose, 0.1f);
    CHECK(pose.getWorldPositionX(tip) > 0.01f);
    solver.setForceField(nullptr);
    CHECK(solver.getForceField() == nullptr);
}

TEST_CASE("animation.procedural.footIkCompensatesPelvisAndRejectsSteepGround") {
    AnimSkeleton skeleton;
    const int    pelvis = skeleton.addBone("pelvis");
    const int    hip    = skeleton.addBone("hip", pelvis);
    const int    knee   = skeleton.addBone("knee", hip);
    const int    foot   = skeleton.addBone("foot", knee);
    skeleton.setBindPosition(knee, 0.f, -1.f, 0.f);
    skeleton.setBindPosition(foot, 0.f, -1.f, 0.f);

    AnimPose pose;
    skeleton.applyBindPose(&pose);
    FootIKSolver solver(&skeleton);
    solver.setPelvisBone(pelvis);
    solver.configureLeftLeg(hip, knee, foot);
    solver.setMaxPelvisOffset(0.25f);
    solver.setLeftContact(true, 0.f, -2.5f, 0.f, 0.f, 1.f, 0.f, 1.f);
    solver.apply(&pose, 1.f / 60.f);
    CHECK(std::fabs(pose.getLocalPositionY(pelvis) + 0.25f) < 1e-4f);

    skeleton.applyBindPose(&pose);
    solver.reset();
    solver.setLeftContact(true, 0.f, -2.5f, 0.f, 1.f, 0.f, 0.f, 1.f);
    solver.apply(&pose, 1.f / 60.f);
    CHECK(std::fabs(pose.getLocalPositionY(pelvis)) < 1e-6f);
}

TEST_CASE("animation.procedural.footIkQueriesGroundLocksAndUsesGracePeriod") {
    AnimSkeleton skeleton;
    const int    pelvis = skeleton.addBone("pelvis");
    const int    hip    = skeleton.addBone("hip", pelvis);
    const int    knee   = skeleton.addBone("knee", hip);
    const int    foot   = skeleton.addBone("foot", knee);
    const int    toe    = skeleton.addBone("toe", foot);
    skeleton.setBindPosition(knee, 0.f, -1.f, 0.f);
    skeleton.setBindPosition(foot, 0.f, -1.f, 0.f);
    skeleton.setBindPosition(toe, 0.f, 0.f, 0.25f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    TestGroundProvider ground;
    FootIKSolver       solver(&skeleton);
    solver.setPelvisBone(pelvis);
    solver.configureLeftLeg(hip, knee, foot, 0.05f);
    solver.configureLeftToe(toe, 0.02f);
    solver.setGroundProvider(&ground);
    solver.setFootLockEnabled(true);
    solver.setFootLockThresholds(0.8f, 0.2f);
    solver.setContactGraceTime(0.1f);
    solver.apply(&pose, 1.f / 60.f);
    CHECK(solver.isLeftFootLocked());
    CHECK(pose.getLocalPositionY(pelvis) < 0.f);

    ground.hit = false;
    solver.apply(&pose, 0.05f);
    CHECK(solver.isLeftFootLocked());
    solver.apply(&pose, 0.1f);
    CHECK(!solver.isLeftFootLocked());
    solver.setGroundProvider(nullptr);
    CHECK(solver.getGroundProvider() == nullptr);
}

TEST_CASE("animation.procedural.dynamicBoneConfigRoundTripsUnknownFieldsAndAppliesAtomically") {
    DynamicBoneConfig      config;
    DynamicBoneChainConfig chain;
    chain.rootBone  = "root";
    chain.endBone   = "tip";
    chain.endMode   = 1;
    chain.endLength = 0.25f;
    chain.particles.resize(3);
    config.chains.push_back(chain);
    config.unknownFields["futureEditorData"] = "preserved";
    config.footIK.enabled                    = true;
    config.footIK.pelvisBone                 = "root";
    config.footIK.left                       = {"hip", "knee", "foot", "toe", 0.05f, 0.02f};
    config.footIK.footLockEnabled            = true;
    auto encoded                             = config.toValue();
    REQUIRE(encoded.ok());
    auto decoded = DynamicBoneConfig::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().unknownFields.contains("futureEditorData"));
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    const int    hip  = skeleton.addBone("hip", root);
    const int    knee = skeleton.addBone("knee", hip);
    const int    foot = skeleton.addBone("foot", knee);
    const int    toe  = skeleton.addBone("toe", foot);
    REQUIRE(toe >= 0);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    DynamicBoneSolver solver(&skeleton);
    FootIKSolver      footSolver(&skeleton);
    REQUIRE(decoded.value().apply(skeleton, solver, footSolver).ok());
    CHECK(solver.getChainCount() == 1);
    DynamicBoneConfig invalidConfig = decoded.value();
    invalidConfig.chains[0].endBone = "missing";
    CHECK(!invalidConfig.apply(skeleton, solver).ok());
    CHECK(solver.getChainCount() == 1);
    eve::Value future                                      = encoded.value();
    (*future.getIf<eve::Value::Object>())["schemaVersion"] = static_cast<std::int64_t>(2);
    CHECK(!DynamicBoneConfig::fromValue(future).ok());
}

TEST_CASE("animation.procedural.overlayBuildsRendererNeutralRuntimeDiagnostics") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    DynamicBoneSolver dynamic(&skeleton);
    REQUIRE(dynamic.addChain(root, tip, 0.f, 0.f, 1.f) == 0);
    dynamic.addColliderSphere(0.f, -1.f, 0.f, 0.2f);
    dynamic.update(&pose, 1.f / 60.f);
    eve::animation_editing::ProceduralBoneOverlayBuilder builder;
    auto                                                 overlay = builder.build("avatar", 1, &dynamic, nullptr);
    CHECK(overlay.status == eve::editing::Status::Applied);
    CHECK(overlay.primitives.size() >= 3);
    CHECK(overlay.primitives[0].kind == "dynamic-bone-particle");
}

TEST_CASE("animation.procedural.dynamicBoneRejectsInvalidFramesAndReportsWork") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    mid  = skeleton.addBone("mid", root);
    const int    tip  = skeleton.addBone("tip", mid);
    skeleton.setBindPosition(mid, 0.f, -1.f, 0.f);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    DynamicBoneSolver solver(&skeleton);
    const int         chain = solver.addChain(root, tip, 0.f, 0.f, 1.f, 1.f, 0.2f, 4);
    REQUIRE(chain == 0);
    solver.setChainEndLength(chain, 0.5f);
    solver.setChainSelfCollision(chain, true);
    solver.update(&pose, std::numeric_limits<float>::quiet_NaN());
    CHECK(solver.getLastUpdateStats().activeChains == 0);
    solver.update(&pose, 1.f / 60.f);
    const auto stats = solver.getLastUpdateStats();
    CHECK(stats.activeChains == 1);
    CHECK(stats.particles == 4);
    CHECK(stats.substeps == 1);
    CHECK(stats.selfCollisionTests > 0);
    AnimSkeleton replacement;
    replacement.addBone("other");
    solver.setSkeleton(&replacement);
    CHECK(solver.getChainCount() == 0);
}

TEST_CASE("animation.procedural.footIkRejectsNonFiniteDeltaTime") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    FootIKSolver solver(&skeleton);
    bool         threw = false;
    try {
        solver.apply(&pose, std::numeric_limits<float>::infinity());
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}

// animation_reload.cpp
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
}  // namespace test_animation_inertializer
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_animation_smr_neural_retarget)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSmr.h"
#include "animation/AnimSmrNeural.h"
#include "animation/tensor/AnimationTensor.h"
#include "animation/tensor/SmrFeatures.h"
#include "common/Capability.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// animation_smr_neural_retarget.cpp
namespace test_animation_smr_neural_retarget {
using namespace eve::animation;

namespace {

void buildArmature(AnimSkeleton& skel, float torsoHeight, float upperArm, float lowerArm) {
    const int root     = skel.addBone("Hips");
    const int chest    = skel.addBone("Spine", root);
    const int shoulder = skel.addBone("LeftShoulder", chest);
    const int elbow    = skel.addBone("LeftElbow", shoulder);
    const int hand     = skel.addBone("LeftHand", elbow);
    skel.setBindPosition(chest, 0.f, torsoHeight, 0.f);
    skel.setBindPosition(shoulder, 0.15f, 0.f, 0.f);
    skel.setBindPosition(elbow, upperArm, 0.f, 0.f);
    skel.setBindPosition(hand, lowerArm, 0.f, 0.f);
}

}  // namespace

TEST_CASE("animation.smr.neural.rot6dRoundTrip") {
    float out[6] = {};
    quatToRot6d(0.f, 0.f, 0.70710678f, 0.70710678f, out);
    float qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
    rot6dToQuat(out, qx, qy, qz, qw);
    const float dot = std::fabs(qx * 0.f + qy * 0.f + qz * 0.70710678f + qw * 0.70710678f);
    CHECK(dot > 0.999f);
}

TEST_CASE("animation.smr.neural.featureBatchShapes") {
    AnimSkeleton source;
    AnimSkeleton target;
    buildArmature(source, 1.0f, 0.28f, 0.28f);
    buildArmature(target, 1.8f, 0.70f, 0.70f);

    AnimClip clip("reach");
    clip.setDuration(0.1f);
    clip.setSampleRate(10.f);
    clip.addRotationKey(source.findBone("LeftShoulder"), 0.f, 0.f, 0.f, 0.70710678f, 0.70710678f);
    clip.addRotationKey(source.findBone("LeftElbow"), 0.05f, 0.f, 0.f, 0.70710678f, 0.70710678f);

    const SmrFeatureBatch batch = buildSmrFeatures(clip, &source, &target, nullptr, nullptr, 2, 4);
    CHECK(batch.frames >= 1);
    CHECK(batch.joints == target.getBoneCount());
    CHECK(batch.sensors >= 1);
    CHECK(static_cast<int>(batch.sourceRot6d.size()) == batch.frames * batch.joints * 6);
    CHECK(static_cast<int>(batch.sourceGeom.size()) == batch.sensors * 7);
    CHECK(static_cast<int>(batch.targetGeom.size()) == batch.sensors * 7);
    CHECK(static_cast<int>(batch.sourceDmi.size()) == batch.frames * std::max(batch.pairs, 1) * 10);
}

TEST_CASE("animation.smr.neural.tensorBackendRewritesClip") {
    AnimationTensor* module = AnimationTensor::create();
    CHECK(module != nullptr);
    CHECK(eve::cap::query<ISmrNeuralRetarget>() != nullptr);

    AnimSkeleton source;
    AnimSkeleton target;
    buildArmature(source, 1.0f, 0.28f, 0.28f);
    buildArmature(target, 1.8f, 0.70f, 0.70f);

    AnimClip clip("reach");
    clip.setDuration(0.f);
    clip.setSampleRate(30.f);
    clip.addRotationKey(source.findBone("LeftShoulder"), 0.f, 0.f, 0.f, 0.70710678f, 0.70710678f);
    clip.addRotationKey(source.findBone("LeftElbow"), 0.f, 0.f, 0.f, 0.70710678f, 0.70710678f);

    AnimRetargetProfile neural;
    neural.setSkinnedInteractionPreserve(true);
    neural.setNeuralRetargetEnabled(true);
    neural.setNeuralBackend("tensor");
    neural.addInteractionIkChain("LeftShoulder", "LeftElbow", "LeftHand");

    std::unique_ptr<AnimClip> out(clip.retargetWithProfile(&source, &target, &neural));
    CHECK(out != nullptr);
    CHECK(neural.getNeuralInferenceCount() >= 1);
    CHECK(neural.getInteractionCorrectionCount() >= 1);
    CHECK(out->getRotationKeyCount(target.findBone("LeftElbow")) >= 1);
}

TEST_CASE("animation.smr.neural.fallsBackToClassicalWhenDisabled") {
    AnimationTensor* module = AnimationTensor::create();
    CHECK(module != nullptr);

    AnimSkeleton source;
    AnimSkeleton target;
    buildArmature(source, 1.0f, 0.28f, 0.28f);
    buildArmature(target, 1.8f, 0.70f, 0.70f);

    AnimClip clip("reach");
    clip.setDuration(0.f);
    clip.setSampleRate(30.f);
    clip.addRotationKey(source.findBone("LeftShoulder"), 0.f, 0.f, 0.f, 0.70710678f, 0.70710678f);
    clip.addRotationKey(source.findBone("LeftElbow"), 0.f, 0.f, 0.f, 0.70710678f, 0.70710678f);

    AnimRetargetProfile classical;
    classical.setSkinnedInteractionPreserve(true);
    classical.setNeuralRetargetEnabled(false);
    classical.setInteractionContactThreshold(0.4f);
    classical.setInteractionCorrectionWeight(1.f);
    classical.addInteractionIkChain("LeftShoulder", "LeftElbow", "LeftHand");

    std::unique_ptr<AnimClip> out(clip.retargetWithProfile(&source, &target, &classical));
    CHECK(out != nullptr);
    CHECK(classical.getNeuralInferenceCount() == 0);
    CHECK(classical.getInteractionCorrectionCount() >= 1);
}
}  // namespace test_animation_smr_neural_retarget
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_attribute_domain_adapters)
#include "PrivateTestScope.h"
#include <string>
#include "attributes/AttributeProjection.h"
#include "card/CardAttributes.h"
#include "card/CardTypes.h"
#include "common/ECS.h"
#include "rts/RTSAttributes.h"
#include "rts/RTSTypes.h"
#include "vehicle/VehicleAttributes.h"
#include "vehicle/VehicleTypes.h"
#include "weapon/WeaponAttributes.h"
#include "weapon/WeaponTypes.h"
#include "zeroerr/unittest.h"
// attribute_domain_adapters.cpp
namespace test_attribute_domain_adapters {
namespace {

using eve::attributes::AttributeModifier;
using eve::attributes::AttributeOperation;
using eve::attributes::ModifierPriority;

void checkSharedModifierContract(const AttributeModifier* modifier) {
    REQUIRE(modifier != nullptr);
    CHECK_EQ(modifier->source, std::string(eve::attributes::source::equipment));
    CHECK_EQ(modifier->priority, static_cast<ModifierPriority>(eve::attributes::priority::equipment));
    CHECK_EQ(static_cast<int>(modifier->operation), static_cast<int>(AttributeOperation::Add));
    CHECK_GT(modifier->sequence, 0u);
}

}  // namespace

TEST_CASE("attributes.domainAdapters.projectSelectedStats") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    auto* card = eve::card::CardData::createCard();
    REQUIRE(card != nullptr);
    card->stats()->attack = 3;
    card->stats()->health = 5;
    auto cardEnsure       = eve::card::CardAttributeAdapter::ensure(*card);
    REQUIRE(cardEnsure.ok());
    auto cardModifier = eve::card::CardAttributeAdapter::addModifier(
        *card, "card.equipment.attack", eve::card::CardAttributeAdapter::attackAttribute,
        std::string(eve::attributes::source::equipment), AttributeOperation::Add, 2.0,
        eve::attributes::priority::equipment);
    REQUIRE(cardModifier.ok());
    checkSharedModifierContract(card->attributes()->values.modifierAt(0));
    CHECK_EQ(card->stats()->attack, 3);
    auto cardProject = eve::card::CardAttributeAdapter::project(*card);
    REQUIRE(cardProject.ok());
    CHECK_EQ(card->stats()->attack, 5);

    auto cardSnapshot = eve::card::CardAttributeAdapter::snapshot(*card);
    REQUIRE(cardSnapshot.ok());
    auto snapshot = std::move(cardSnapshot).takeValue();
    auto cardSet =
        eve::card::CardAttributeAdapter::setBase(*card, eve::card::CardAttributeAdapter::attackAttribute, 9.0);
    REQUIRE(cardSet.ok());
    CHECK_EQ(card->stats()->attack, 9);
    auto cardRestore = eve::card::CardAttributeAdapter::restore(*card, snapshot, card->attributes()->values.revision());
    REQUIRE(cardRestore.ok());
    CHECK_EQ(card->stats()->attack, 5);
    snapshot.owner.generation += 1;
    auto cardStale = eve::card::CardAttributeAdapter::restore(*card, snapshot, card->attributes()->values.revision());
    CHECK(!cardStale.ok());
    cardStale.ignore("stale-owner assertion");
    card->release();

    auto* vehicle = eve::vehicle::VehicleEntity::createVehicle();
    REQUIRE(vehicle != nullptr);
    vehicle->health()->hp    = 80.0f;
    vehicle->health()->maxHp = 100.0f;
    auto vehicleEnsure       = eve::vehicle::VehicleAttributeAdapter::ensure(*vehicle);
    REQUIRE(vehicleEnsure.ok());
    auto vehicleModifier = eve::vehicle::VehicleAttributeAdapter::addModifier(
        *vehicle, "vehicle.equipment.armor", eve::vehicle::VehicleAttributeAdapter::armorAttribute,
        std::string(eve::attributes::source::equipment), AttributeOperation::Add, 25.0,
        eve::attributes::priority::equipment);
    REQUIRE(vehicleModifier.ok());
    checkSharedModifierContract(vehicle->attributes()->values.modifierAt(0));
    auto vehicleProject = eve::vehicle::VehicleAttributeAdapter::project(*vehicle);
    REQUIRE(vehicleProject.ok());
    CHECK_EQ(vehicle->health()->hp, 80.0f);
    auto vehicleArmor =
        eve::vehicle::VehicleAttributeAdapter::read(*vehicle, eve::vehicle::VehicleAttributeAdapter::armorAttribute);
    REQUIRE(vehicleArmor.ok());
    CHECK_EQ(vehicleArmor.value(), 25.0);
    vehicle->release();

    auto* weapon = eve::weapon::WeaponEntity::createWeapon();
    REQUIRE(weapon != nullptr);
    weapon->state()->resource.kind  = eve::weapon::ResourceKind::Mana;
    weapon->state()->resource.value = 10.0f;
    weapon->state()->resource.max   = 20.0f;
    weapon->state()->resource.cost  = 3.0f;
    auto weaponEnsure               = eve::weapon::WeaponAttributeAdapter::ensure(*weapon);
    REQUIRE(weaponEnsure.ok());
    auto weaponModifier = eve::weapon::WeaponAttributeAdapter::addModifier(
        *weapon, "weapon.equipment.mana", eve::weapon::WeaponAttributeAdapter::manaAttribute,
        std::string(eve::attributes::source::equipment), AttributeOperation::Add, 5.0,
        eve::attributes::priority::equipment);
    REQUIRE(weaponModifier.ok());
    checkSharedModifierContract(weapon->attributes()->values.modifierAt(0));
    auto weaponProject = eve::weapon::WeaponAttributeAdapter::project(*weapon);
    REQUIRE(weaponProject.ok());
    CHECK_EQ(weapon->state()->resource.value, 15.0f);
    auto consume = eve::weapon::WeaponAttributeAdapter::consumeTriggerResource(*weapon);
    REQUIRE(consume.ok());
    CHECK_EQ(weapon->state()->resource.value, 12.0f);
    weapon->release();

    auto* unit = eve::rts::Unit::createUnit();
    REQUIRE(unit != nullptr);
    unit->motion()->speed = 7.0f;
    auto unitEnsure       = eve::rts::RTSUnitAttributeAdapter::ensure(*unit);
    REQUIRE(unitEnsure.ok());
    auto unitModifier = eve::rts::RTSUnitAttributeAdapter::addModifier(
        *unit, "rts.equipment.attack", eve::rts::RTSUnitAttributeAdapter::attackAttribute,
        std::string(eve::attributes::source::equipment), AttributeOperation::Add, 4.0,
        eve::attributes::priority::equipment);
    REQUIRE(unitModifier.ok());
    checkSharedModifierContract(unit->attributes()->values.modifierAt(0));
    auto unitAttack =
        eve::rts::RTSUnitAttributeAdapter::read(*unit, eve::rts::RTSUnitAttributeAdapter::attackAttribute);
    REQUIRE(unitAttack.ok());
    CHECK_EQ(unitAttack.value(), 4.0);
    auto unsupported = eve::rts::RTSUnitAttributeAdapter::read(*unit, "speed");
    CHECK(!unsupported.ok());
    unsupported.ignore("motion remains domain-owned");
    CHECK_EQ(unit->motion()->speed, 7.0f);
    unit->release();
}
}  // namespace test_attribute_domain_adapters
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_attributes_rpg_unification)
#include "PrivateTestScope.h"
#include <type_traits>
#include "attributes/AttributeSet.h"
#include "rpg/AttributeSystem.h"
#include "rpg/RPGActor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// attributes_rpg_unification.cpp
namespace test_attributes_rpg_unification {
using eve::attributes::AttributeModifier;
using eve::attributes::AttributeOperation;
using eve::attributes::AttributeSet;
using eve::rpg::RPGActor;

TEST_CASE("attributes.canonicalOperationOrderAndMetadata") {
    AttributeSet set("actor:one");
    set.setBase("power", 100.0);

    auto add = set.addModifier(AttributeModifier{"", "power", "gear", AttributeOperation::Add, 10.0, 0});
    auto additive =
        set.addModifier(AttributeModifier{"", "power", "talent", AttributeOperation::AdditivePercent, 0.2, 0});
    auto multiplicative =
        set.addModifier(AttributeModifier{"", "power", "aura", AttributeOperation::MultiplicativePercent, 0.1, 0});
    auto overrideValue =
        set.addModifier(AttributeModifier{"", "power", "scenario", AttributeOperation::Override, 50.0, 100});
    auto clamp = set.addModifier(AttributeModifier{"", "power", "rules", AttributeOperation::ClampMin, 60.0, 200});

    REQUIRE(add.ok());
    REQUIRE(additive.ok());
    REQUIRE(multiplicative.ok());
    REQUIRE(overrideValue.ok());
    REQUIRE(clamp.ok());
    CHECK_EQ(set.getFinal("power"), 60.0);
    CHECK_EQ(set.modifierCount(), 5);
    REQUIRE(set.modifierAt(0) != nullptr);
    REQUIRE(set.modifierAt(4) != nullptr);
    CHECK_EQ(set.modifierAt(0)->sequence, 1u);
    CHECK_EQ(set.modifierAt(4)->sequence, 5u);
    CHECK_EQ(set.modifierAt(0)->attribute, "power");
    CHECK_EQ(set.modifierAt(0)->source, "gear");
}

TEST_CASE("attributes.generatedIdsAreSetLocalAndCopyDeterministic") {
    AttributeSet first("actor:first");
    AttributeSet second("actor:second");

    auto firstId  = first.addModifier(AttributeModifier{"", "power", "source", AttributeOperation::Add, 1.0});
    auto secondId = second.addModifier(AttributeModifier{"", "power", "source", AttributeOperation::Add, 1.0});
    REQUIRE(firstId.ok());
    REQUIRE(secondId.ok());
    CHECK_EQ(firstId.value(), "attribute:modifier:actor:first:1");
    CHECK_EQ(secondId.value(), "attribute:modifier:actor:second:1");
    CHECK_EQ(first.modifierAt(0)->sequence, 1u);
    CHECK_EQ(second.modifierAt(0)->sequence, 1u);

    AttributeSet restored = first;
    auto restoredNext = restored.addModifier(AttributeModifier{"", "power", "restore", AttributeOperation::Add, 2.0});
    auto firstNext    = first.addModifier(AttributeModifier{"", "power", "next", AttributeOperation::Add, 2.0});
    REQUIRE(restoredNext.ok());
    REQUIRE(firstNext.ok());
    CHECK_EQ(restoredNext.value(), "attribute:modifier:actor:first:2");
    CHECK_EQ(firstNext.value(), "attribute:modifier:actor:first:2");
    CHECK_EQ(restored.modifierAt(1)->sequence, 2u);
    CHECK_EQ(first.modifierAt(1)->sequence, 2u);
}

TEST_CASE("attributes.rpgFacadeUsesTheSameAttributeSetTruth") {
    static_assert(std::is_same_v<decltype(RPGActor::Attributes{}.values), AttributeSet>);

    AttributeSet direct("actor:parity");
    direct.setBase("damage", 100.0);
    auto directModifier =
        direct.addModifier(AttributeModifier{"direct", "damage", "shared", AttributeOperation::AdditivePercent, 0.2});
    REQUIRE(directModifier.ok());

    RPGActor* actor = RPGActor::createActor();
    actor->attributes()->values.setBase("damage", 100.0);
    auto actorModifier = actor->addAttributeModifier(
        AttributeModifier{"direct", "damage", "shared", AttributeOperation::AdditivePercent, 0.2});
    REQUIRE(actorModifier.ok());
    CHECK_EQ(direct.getFinal("damage"), actor->getFinalAttribute("damage"));

    auto bad = actor->addAttributeModifier(AttributeModifier{"bad", "", "source", AttributeOperation::Add, 1.0});
    CHECK(!bad.ok());
    CHECK_EQ(bad.code(), eve::StatusCode::Rejected);

    CHECK(actor->removeAttributeModifier(actorModifier.value()).ok());
    CHECK_EQ(actor->getFinalAttribute("damage"), 100.0);
    actor->release();
}

TEST_CASE("attributes.customPolicyParityUsesCanonicalOperationRegistry") {
    auto& policies = eve::rpg::AttributeSystem::customOps();
    policies.registerOperation("square", [](double, double value) { return value * value; });

    AttributeSet direct("actor:custom");
    direct.setBase("power", 1.0);
    auto directModifier = direct.addModifier(
        AttributeModifier{"square", "power", "policy", AttributeOperation::Custom, 4.0, 0, 0, "square"});
    REQUIRE(directModifier.ok());

    RPGActor* actor = RPGActor::createActor();
    actor->attributes()->values.setBase("power", 1.0);
    auto actorModifier = actor->addAttributeModifier(
        AttributeModifier{"square", "power", "policy", AttributeOperation::Custom, 4.0, 0, 0, "square"});
    REQUIRE(actorModifier.ok());
    CHECK_EQ(direct.getFinal("power", 0.0, &policies), actor->getFinalAttribute("power"));

    actor->release();
    policies.unregisterOperation("square");
}
}  // namespace test_attributes_rpg_unification
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_audio)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <thread>
#include <vector>
#include "PathBesideSource.h"
#include "action/ActionAudioBlock.h"
#include "action/ActionAudioWaveform.h"
#include "action/ActionBlockRuntime.h"
#include "action/ActionNotifyRegistry.h"
#include "action/ActionParameterCurve.h"
#include "action/ActionPreview.h"
#include "audio/Audio.h"
#include "audio/AudioZone.h"
#include "audio/Source.h"
#include "audio/UnderwaterAudio.h"
#include "audio/editing/AudioTarget.h"
#include "common/Capability.h"
#include "common/Exception.h"
#include "common/SquirrelBinding.h"
#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneObject.h"
#include "sound/Decoder.h"
#include "sound/Sound.h"
#include "sound/SoundData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// audio.cpp
namespace test_audio {
// audio.cpp
namespace {

EVE_DEFINE_PATH_BESIDE_SOURCE()

std::vector<char> readBinaryFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

class TestAudioAttachmentSource final : public eve::IAttachmentPointSource {
public:
    eve::Result<eve::AttachmentPoint> sampleAttachmentPoint(std::string_view     name,
                                                            eve::AttachmentPoint localOffset) const override {
        ++calls;
        if (name != "hand_r")
            return eve::Result<eve::AttachmentPoint>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "missing test bone", "bone"));
        return eve::Result<eve::AttachmentPoint>::success(
            {20.f + localOffset.x, 30.f + localOffset.y, 40.f + localOffset.z});
    }
    mutable int calls = 0;
};

}  // namespace

static eve::LogicalId actionLogicalId(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

static eve::audio::Audio* tryCreateAudio() {
    try {
        return eve::audio::Audio::create();
    } catch (const eve::Exception&) {
        std::cerr << "N/A: OpenAL device not available\n";
        return nullptr;
    }
}

static std::vector<char> makeSilentWav(int samples = 441) {
    const int         dataBytes = samples * 2;
    std::vector<char> buf(44 + dataBytes, 0);
    auto              w32 = [&](int off, uint32_t v) { std::memcpy(buf.data() + off, &v, 4); };
    auto              w16 = [&](int off, uint16_t v) { std::memcpy(buf.data() + off, &v, 2); };
    std::memcpy(buf.data(), "RIFF", 4);
    w32(4, 36 + dataBytes);
    std::memcpy(buf.data() + 8, "WAVEfmt ", 8);
    w32(16, 16);
    w16(20, 1);
    w16(22, 1);
    w32(24, 44100);
    w32(28, 44100 * 2);
    w16(32, 2);
    w16(34, 16);
    std::memcpy(buf.data() + 36, "data", 4);
    w32(40, dataBytes);
    return buf;
}

TEST_CASE("audio.device") {
    auto* audio = tryCreateAudio();
    if (!audio) return;
    audio->setVolume(0.5f);
    CHECK(audio->getVolume() == 0.5f);
    audio->setPosition(1.f, 2.f, 3.f);
    audio->pump();
}

TEST_CASE("audio.staticSource.playStop") {
    auto* audio = tryCreateAudio();
    if (!audio) return;
    auto*               sound = eve::sound::Sound::create();
    auto                wav   = makeSilentWav(4410);
    eve::data::ByteData data(wav.data(), wav.size());
    auto*               sd  = sound->newSoundData(&data);
    auto*               src = audio->newSource(sd);
    REQUIRE(src != nullptr);
    src->setVolume(0.2f);
    src->setPitch(1.0f);
    src->setLooping(false);
    src->play();
    CHECK(src->isPlaying());
    src->stop();
    CHECK(!src->isPlaying());
    delete src;
    delete sd;
}

TEST_CASE("audio.actionBlockProviderOwnsRealSourceEnterExit") {
    auto* audio = tryCreateAudio();
    if (!audio) return;
    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    auto* scene   = eve::scene::Scene::create();
    auto  mounted = scene->mountAs("action-audio", eve::scene::node("source").withPosition(8.f, 9.f, 10.f));
    REQUIRE(mounted.ok());
    auto source = eve::scene::SceneObject::createObject("action-audio", "source");
    REQUIRE(source.ok());
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionBlockRuntime runtime(registry.value());
    eve::action::ActionAdvance      advance;
    advance.id = eve::action::ActionExecutionId{92};
    eve::Value::Object payload{{"uri", "test/fixtures/resource_formats/tone.wav"},
                               {"bone", "hand_r"},
                               {"randomUris", eve::Value::Array{"test/fixtures/resource_formats/tone.wav"}},
                               {"volume", 0.25},
                               {"pitch", 1.0},
                               {"randomPitchOffset", 0.05},
                               {"spatialBlend", 1.0},
                               {"minDistance", 2.0},
                               {"maxDistance", 30.0},
                               {"looping", true}};
    advance.timelineEvents.push_back({eve::action::ActionTimelineEventKind::StateEnter,
                                      actionLogicalId("presentation-track:audio"),
                                      actionLogicalId("presentation-audio:loop"),
                                      actionLogicalId("presentation:audio-state"), eve::Duration::zero(), payload});
    advance.activeBlocks.push_back({actionLogicalId("presentation-track:audio"),
                                    actionLogicalId("presentation-audio:loop"),
                                    actionLogicalId("presentation:audio-state"), eve::Duration::zero(),
                                    eve::Duration::fromNanoseconds(100), payload});
    eve::action::ActionNotifyContext context;
    context.executionId = advance.id;
    context.source      = ecs::handle_of(source.value());
    TestAudioAttachmentSource attachmentSource;
    context.sourceAttachment = std::cref(attachmentSource);
    REQUIRE(runtime.apply(advance, context).ok());
    CHECK_EQ(attachmentSource.calls, 2);
    const int sourceCount = audio->getSourceCount();
    REQUIRE(runtime.interrupt(context).ok());
    CHECK_EQ(audio->getSourceCount(), sourceCount - 1);
}

TEST_CASE("audio.instantActionNotifyRetainsThenDeterministicallyReleasesSource") {
    auto* audio = tryCreateAudio();
    if (!audio) return;
    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionBlockRuntime runtime(registry.value());
    const int                       before = audio->getSourceCount();
    eve::action::ActionAdvance      trigger;
    trigger.id = eve::action::ActionExecutionId{93};
    trigger.timelineEvents.push_back({eve::action::ActionTimelineEventKind::Notify,
                                      actionLogicalId("presentation-track:audio"),
                                      actionLogicalId("presentation-audio:hit"),
                                      actionLogicalId("presentation:audio"),
                                      eve::Duration::zero(),
                                      {{"uri", "test/fixtures/resource_formats/tone.wav"}, {"volume", 0.5}}});
    eve::action::ActionNotifyContext context;
    context.executionId = trigger.id;
    REQUIRE(runtime.apply(trigger, context).ok());
    CHECK_EQ(audio->getSourceCount(), before + 1);

    eve::action::ActionAdvance expired;
    expired.id           = trigger.id;
    expired.totalElapsed = eve::Duration::fromNanoseconds(10'000'000'000LL);
    REQUIRE(runtime.apply(expired, context).ok());
    CHECK_EQ(audio->getSourceCount(), before);
}

TEST_CASE("audio.actionStateFadeOutUsesInjectedActionTimeAndReleasesSource") {
    auto* audio = tryCreateAudio();
    if (!audio) return;
    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionBlockRuntime  runtime(registry.value());
    const int                        before    = audio->getSourceCount();
    const auto                       execution = eve::action::ActionExecutionId{94};
    const auto                       track     = actionLogicalId("presentation-track:audio");
    const auto                       item      = actionLogicalId("presentation-audio:fade");
    const auto                       type      = actionLogicalId("presentation:audio-state");
    eve::Value::Object               payload{{"uri", "test/fixtures/resource_formats/tone.wav"},
                                             {"volume", 0.8},
                                             {"looping", true},
                                             {"fadeOutOnExit", true},
                                             {"fadeOutDuration", 0.2}};
    eve::action::ActionNotifyContext context;
    context.executionId = execution;

    eve::action::ActionAdvance entered;
    entered.id = execution;
    entered.timelineEvents.push_back(
        {eve::action::ActionTimelineEventKind::StateEnter, track, item, type, eve::Duration::zero(), payload});
    entered.activeBlocks.push_back(
        {track, item, type, eve::Duration::zero(), eve::Duration::fromSeconds(1.0).takeValue(), payload});
    REQUIRE(runtime.apply(entered, context).ok());
    CHECK_EQ(audio->getSourceCount(), before + 1);

    eve::action::ActionAdvance exited;
    exited.id           = execution;
    exited.totalElapsed = eve::Duration::fromSeconds(1.0).takeValue();
    exited.timelineEvents.push_back(
        {eve::action::ActionTimelineEventKind::StateExit, track, item, type, exited.totalElapsed, payload});
    REQUIRE(runtime.apply(exited, context).ok());
    CHECK_EQ(audio->getSourceCount(), before + 1);

    exited.timelineEvents.clear();
    exited.totalElapsed = eve::Duration::fromSeconds(1.1).takeValue();
    REQUIRE(runtime.apply(exited, context).ok());
    CHECK_EQ(audio->getSourceCount(), before + 1);
    exited.totalElapsed = eve::Duration::fromSeconds(1.2).takeValue();
    REQUIRE(runtime.apply(exited, context).ok());
    CHECK_EQ(audio->getSourceCount(), before);
}

TEST_CASE("audio.actionPreviewRetainsAdvanceSeeksRefreshAndStopsOnExit") {
    auto* audio = tryCreateAudio();
    if (!audio) return;
    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    REQUIRE_EQ(eve::cap::listenerCount<eve::action::IActionPreviewSinkProvider>(), 1u);
    auto* provider = eve::cap::listenerAt<eve::action::IActionPreviewSinkProvider>(0);
    REQUIRE(provider != nullptr);
    auto sink = provider->createActionPreviewSink();
    REQUIRE(sink.ok());
    const int before = audio->getSourceCount();

    eve::Value::Object payload{
        {"uri", "test/fixtures/resource_formats/tone.wav"}, {"volume", 0.35}, {"pitch", 1.25}, {"looping", true}};
    eve::action::ActionPreviewFrame frame;
    frame.reason = eve::action::ActionPreviewReason::Seek;
    frame.activeBlocks.push_back({actionLogicalId("presentation-track:audio"),
                                  actionLogicalId("presentation-audio:preview"),
                                  actionLogicalId("presentation:audio-state"), eve::Duration::fromNanoseconds(5000000),
                                  eve::Duration::fromNanoseconds(100000000), payload});
    REQUIRE(sink.value()->prepare(frame).ok());
    CHECK_EQ(audio->getSourceCount(), before + 1);
    sink.value()->present(frame);

    frame.reason = eve::action::ActionPreviewReason::Advance;
    REQUIRE(sink.value()->prepare(frame).ok());
    sink.value()->present(frame);
    CHECK_EQ(audio->getSourceCount(), before + 1);

    frame.reason = eve::action::ActionPreviewReason::Refresh;
    REQUIRE(sink.value()->prepare(frame).ok());
    CHECK_EQ(audio->getSourceCount(), before + 2);
    sink.value()->present(frame);
    CHECK_EQ(audio->getSourceCount(), before + 1);

    auto invalid                                  = frame;
    invalid.activeBlocks.front().payload["pitch"] = 0.0;
    CHECK(!sink.value()->prepare(invalid).ok());
    CHECK_EQ(audio->getSourceCount(), before + 1);

    eve::action::ActionPreviewFrame empty;
    REQUIRE(sink.value()->prepare(empty).ok());
    sink.value()->present(empty);
    CHECK_EQ(audio->getSourceCount(), before);

    eve::action::ActionPreviewCue instant;
    instant.kind    = eve::action::ActionPreviewCueKind::Audio;
    instant.itemId  = actionLogicalId("presentation-audio:instant-preview");
    instant.type    = actionLogicalId("presentation:audio");
    instant.payload = {{"uri", "test/fixtures/resource_formats/tone.wav"}, {"volume", 0.5}};
    empty.cues.push_back(std::move(instant));
    REQUIRE(sink.value()->prepare(empty).ok());
    sink.value()->present(empty);
    CHECK_EQ(audio->getSourceCount(), before + 1);
    sink.value().reset();
    CHECK_EQ(audio->getSourceCount(), before);
}

TEST_CASE("audio.actionWaveformProjectsRealPcmWithPitchLoopAndBoundedBuckets") {
    auto* audio = tryCreateAudio();
    if (!audio) return;
    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    auto* provider = eve::cap::query<eve::action::IActionAudioWaveformProvider>();
    REQUIRE(provider != nullptr);

    eve::action::ActionAudioWaveformRequest request;
    request.binding.uri     = "test/fixtures/resource_formats/tone.wav";
    request.binding.pitch   = 2.0;
    request.binding.looping = true;
    request.blockDuration   = eve::Duration::fromNanoseconds(200'000'000);
    request.bucketCount     = 64;
    auto waveform           = provider->waveform(request);
    REQUIRE(waveform.ok());
    CHECK(waveform.value().clipDurationSeconds > 0.0);
    REQUIRE_EQ(waveform.value().buckets.size(), 64u);
    CHECK(std::any_of(waveform.value().buckets.begin(), waveform.value().buckets.end(),
                      [](const auto& bucket) { return bucket.minimum < -0.01f || bucket.maximum > 0.01f; }));

    request.bucketCount = 5000;
    CHECK(!provider->waveform(request).ok());
}

TEST_CASE("audio.actionParameterCurveDrivesAndRestoresMasterVolume") {
    auto* audio = tryCreateAudio();
    if (!audio) return;
    audio->setVolume(0.8f);
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionBlockRuntime runtime(registry.value());
    eve::Value::Object              payload{
                     {"target", "audio:master-volume"},
                     {"operation", "multiply"},
                     {"keys", eve::Value::Array{eve::Value::Object{{"time", 0.0}, {"value", 1.0}, {"interpolation", "linear"}},
                                   eve::Value::Object{{"time", 1.0}, {"value", 0.0}, {"interpolation", "linear"}}}}};
    eve::action::ActionAdvance advance;
    advance.id = eve::action::ActionExecutionId{94};
    advance.timelineEvents.push_back({eve::action::ActionTimelineEventKind::StateEnter,
                                      actionLogicalId("presentation-track:parameter"),
                                      actionLogicalId("presentation-parameter:master-fade"),
                                      actionLogicalId("presentation:parameter-curve"), eve::Duration::zero(), payload});
    advance.activeBlocks.push_back(
        {actionLogicalId("presentation-track:parameter"), actionLogicalId("presentation-parameter:master-fade"),
         actionLogicalId("presentation:parameter-curve"), eve::Duration::fromNanoseconds(500'000'000),
         eve::Duration::fromNanoseconds(1'000'000'000), payload});
    eve::action::ActionNotifyContext context;
    context.executionId = advance.id;
    REQUIRE(runtime.apply(advance, context).ok());
    CHECK(std::abs(audio->getVolume() - 0.4f) < 0.0001f);
    REQUIRE(runtime.interrupt(context).ok());
    CHECK(std::abs(audio->getVolume() - 0.8f) < 0.0001f);
}

TEST_CASE("audio.underwater.transitionsAndAtomicValidation") {
    auto* audio = tryCreateAudio();
    if (!audio) return;
    auto* sd       = new eve::sound::SoundData(std::vector<uint8_t>(44100 * 2, 0), 44100, 16, 1);
    auto* down     = audio->newSource(sd);
    auto* up       = audio->newSource(sd);
    auto* ambience = audio->newSource(sd);

    CHECK(!eve::audio::applyUnderwaterAudio(down, up, ambience, true, false, true, 1.5F));
    CHECK(!down->isPlaying());
    CHECK(!ambience->isPlaying());
    REQUIRE(static_cast<bool>(eve::audio::applyUnderwaterAudio(down, up, ambience, true, false, true, 0.35F)));
    CHECK(down->isPlaying());
    CHECK(ambience->isPlaying());
    CHECK(ambience->isLooping());
    CHECK(ambience->getVolume() == 0.35F);

    REQUIRE(static_cast<bool>(eve::audio::applyUnderwaterAudio(down, up, ambience, false, true, false, 0.35F)));
    CHECK(up->isPlaying());
    CHECK(!ambience->isPlaying());

    delete ambience;
    delete up;
    delete down;
    delete sd;
}

TEST_CASE("audio.pcgZone.outputDrivesRealSource") {
    auto* audio = tryCreateAudio();
    if (!audio) return;
    auto*                       sd     = new eve::sound::SoundData(std::vector<uint8_t>(44100 * 2, 0), 44100, 16, 1);
    auto*                       source = audio->newSource(sd);
    eve::audio::AudioZoneOutput command;
    command.play   = true;
    command.volume = .35f;
    REQUIRE(eve::audio::applyAudioZoneOutput(source, command).ok());
    CHECK(source->isPlaying());
    CHECK(source->getVolume() == .35f);
    command.play   = false;
    command.stop   = true;
    command.volume = 0.f;
    REQUIRE(eve::audio::applyAudioZoneOutput(source, command).ok());
    CHECK(!source->isPlaying());
    command.volume = std::numeric_limits<float>::quiet_NaN();
    CHECK(!eve::audio::applyAudioZoneOutput(source, command).ok());
    delete source;
    delete sd;
}

TEST_CASE("audio.streamSource.pump") {
    auto* audio = tryCreateAudio();
    if (!audio) return;
    auto                wav = makeSilentWav(44100);
    eve::data::ByteData data(wav.data(), wav.size());
    auto*               src = audio->newSourceFromData(&data, "stream");
    REQUIRE(src != nullptr);
    src->setLooping(true);
    src->play();
    for (int i = 0; i < 30; ++i) {
        audio->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(src->isPlaying());
    src->stop();
    audio->pump();
    delete src;
}

TEST_CASE("audio.spatial.api") {
    auto* audio = tryCreateAudio();
    if (!audio) return;
    auto*               sound = eve::sound::Sound::create();
    auto                wav   = makeSilentWav();
    eve::data::ByteData data(wav.data(), wav.size());
    auto*               sd  = sound->newSoundData(&data);
    auto*               src = audio->newSource(sd);
    audio->setPosition(0, 0, 0);
    src->setRelative(false);
    src->setPosition(5.f, 0.f, 0.f);
    src->setAttenuationDistances(1.f, 50.f);
    src->play();
    audio->pump();
    src->stop();
    delete src;
    delete sd;
}

TEST_CASE("audio.stopAll") {
    auto* audio = tryCreateAudio();
    if (!audio) return;

    auto*               sound = eve::sound::Sound::create();
    auto                wav   = makeSilentWav(44100);
    eve::data::ByteData data(wav.data(), wav.size());
    auto*               sd   = sound->newSoundData(&data);
    auto*               src1 = audio->newSource(sd);
    auto*               src2 = audio->newSource(sd);
    src1->play();
    src2->play();
    audio->pump();
    CHECK(src1->isPlaying());
    CHECK(src2->isPlaying());
    audio->stopAll();
    audio->pump();
    CHECK(!src1->isPlaying());
    CHECK(!src2->isPlaying());
    delete src1;
    delete src2;
    delete sd;
}

TEST_CASE("audio.pause") {
    auto* audio = tryCreateAudio();
    if (!audio) return;

    auto*               sound = eve::sound::Sound::create();
    auto                wav   = makeSilentWav(44100);
    eve::data::ByteData data(wav.data(), wav.size());
    auto*               sd  = sound->newSoundData(&data);
    auto*               src = audio->newSource(sd);
    src->play();
    audio->pump();
    CHECK(src->isPlaying());
    audio->pause(src);
    CHECK(!src->isPlaying());
    delete src;
    delete sd;
}

TEST_CASE("audio.source.seekTell") {
    auto* audio = tryCreateAudio();
    if (!audio) return;

    auto*               sound = eve::sound::Sound::create();
    auto                wav   = makeSilentWav(44100);
    eve::data::ByteData data(wav.data(), wav.size());
    auto*               sd  = sound->newSoundData(&data);
    auto*               src = audio->newSource(sd);

    CHECK(src->getDuration() > 0.9);
    src->play();
    audio->pump();
    CHECK(src->seek(0.5));
    audio->pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    double pos = src->tell();
    // OpenAL tell() can drift after pump/sleep; keep a loose window around seek(0.5).
    CHECK(pos >= 0.40);
    CHECK(pos <= 0.65);
    src->stop();

    delete src;
    delete sd;
}

TEST_CASE("audio.source.pitch") {
    auto* audio = tryCreateAudio();
    if (!audio) return;

    auto*               sound = eve::sound::Sound::create();
    auto                wav   = makeSilentWav();
    eve::data::ByteData data(wav.data(), wav.size());
    auto*               sd  = sound->newSoundData(&data);
    auto*               src = audio->newSource(sd);

    src->setPitch(1.5f);
    CHECK(src->getPitch() == 1.5f);
    src->setPitch(0.5f);
    CHECK(src->getPitch() == 0.5f);

    delete src;
    delete sd;
}

TEST_CASE("audio.volumeRoundTrip") {
    auto* audio = tryCreateAudio();
    if (!audio) return;

    audio->setVolume(0.75f);
    CHECK(audio->getVolume() == 0.75f);

    auto*               sound = eve::sound::Sound::create();
    auto                wav   = makeSilentWav();
    eve::data::ByteData data(wav.data(), wav.size());
    auto*               sd  = sound->newSoundData(&data);
    auto*               src = audio->newSource(sd);

    src->setVolume(0.3f);
    CHECK(src->getVolume() == 0.3f);

    delete src;
    delete sd;
}

TEST_CASE("audio.modplug.midi.play") {
    auto* audio = tryCreateAudio();
    if (!audio) return;

    const std::string path  = pathBesideThisSource("angel.mid");
    auto              bytes = readBinaryFile(path);
    REQUIRE(!bytes.empty());

    eve::filesystem::FileData fd("angel.mid", bytes.size());
    std::memcpy(fd.getData(), bytes.data(), bytes.size());

    // Prefill stream buffers before play — empty AL sources leave AL_INITIAL.
    eve::audio::Source* src = nullptr;
    try {
        src = audio->newSourceFromData(&fd, "stream");
    } catch (const eve::Exception& e) {
        std::cerr << "MIDI stream source failed: " << e.what() << "\n";
        CHECK(false);
        return;
    }
    REQUIRE(src != nullptr);
    std::cerr << "Checking angel.mid stream (4s deadline) (duration=" << src->getDuration() << "s)\n";
    std::cerr << "(ModPlug may warn about missing timidity.cfg / instrument patches)\n";

    src->setVolume(1.0f);
    for (int i = 0; i < 20; ++i) {
        audio->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    src->play();
    bool       heardPlaying   = false;
    double     maxTell        = 0.0;
    const auto streamDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < streamDeadline) {
        audio->pump();
        if (src->isPlaying()) heardPlaying = true;
        maxTell = std::max(maxTell, src->tell());
        if (heardPlaying && maxTell > 0.05) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cerr << "stream: isPlayingSeen=" << (heardPlaying ? "yes" : "no") << " maxTell=" << maxTell << "s\n";
    REQUIRE(heardPlaying);
    REQUIRE(maxTell > 0.05);  // stream path currently underfills; static path is the reliable play test
    src->stop();
    audio->pump();
    delete src;

    // Also verify static path (full decode → OpenAL buffer) briefly.
    eve::filesystem::FileData fd2("angel.mid", bytes.size());
    std::memcpy(fd2.getData(), bytes.data(), bytes.size());
    auto* sound = eve::sound::Sound::create();
    auto* sd    = sound->newSoundData(&fd2);
    REQUIRE(sd != nullptr);
    CHECK(sd->getSampleCount() > 0);
    auto* staticSrc = audio->newSource(sd);
    std::cerr << "Checking angel.mid static (2s deadline) (samples=" << sd->getSampleCount() << ")\n";
    staticSrc->setVolume(1.0f);
    staticSrc->play();
    REQUIRE(staticSrc->isPlaying());
    const auto staticDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (staticSrc->tell() <= 0.5 && std::chrono::steady_clock::now() < staticDeadline) {
        audio->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(staticSrc->tell() > 0.5);
    staticSrc->stop();
    delete staticSrc;
    delete sd;
}

// audio_editing_contracts.cpp
using namespace eve::audio_editing;

namespace {

SelectionSnapshot select(const AudioSourceTarget& target) {
    eve::editing::SelectionItem item;
    item.domain = eve::editing::SelectionDomain::Asset;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    item.type   = "audio.source";

    SelectionSnapshot selection;
    selection.channel = "audio";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

class RecordingSink final : public IAudioSourceRuntimeSink {
public:
    Result<void> publish(const AudioSourceTarget& candidate) override {
        ++calls;
        observedRevision = candidate.revision();
        if (reject)
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("test.audio.publish"),
                                              "injected publication failure");
        return eve::editing::applied<void>();
    }

    bool     reject           = false;
    int      calls            = 0;
    Revision observedRevision = 0;
};

DomainOperation setClip(const AudioSourceTarget& target, std::string uri) {
    auto operation = target.makeSet(select(target), PropertyPath("clip.asset"), EditorValue(std::move(uri)),
                                    PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    return std::move(operation.value());
}

}  // namespace

TEST_CASE("audio_editing.provider_absence_is_explicit_and_preserves_authoring_state") {
    AudioSourcePublishingTarget target("voice", nullptr);
    const Revision              before = target.revision();
    auto applied = target.applyDomainOperation(setClip(target.authoringTarget(), "asset://audio/voice.ogg"));
    CHECK(!applied.ok());
    CHECK_EQ(static_cast<int>(applied.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.revision(), before);
}

TEST_CASE("audio_editing.publication_failure_is_atomic_and_success_commits_candidate") {
    RecordingSink               sink;
    AudioSourcePublishingTarget target("voice", &sink);
    const Revision              before    = target.revision();
    const DomainOperation       operation = setClip(target.authoringTarget(), "asset://audio/voice.ogg");

    sink.reject   = true;
    auto rejected = target.applyDomainOperation(operation);
    CHECK(!rejected.ok());
    CHECK_EQ(target.revision(), before);
    CHECK_EQ(sink.calls, 1);

    sink.reject    = false;
    auto committed = target.applyDomainOperation(operation);
    CHECK(committed.ok());
    CHECK_EQ(target.revision(), before + 1);
    CHECK_EQ(sink.calls, 2);
    CHECK_EQ(sink.observedRevision, before + 1);
}

// audio_zone.cpp
using namespace eve::audio;

TEST_CASE("audio.pcgZone.activationFadeTrackAndDelayedExit") {
    AudioZoneProfile profile;
    profile.radius           = 10.f;
    profile.minimumBreakTime = 2.f;
    profile.maximumBreakTime = 2.f;
    AudioZoneItem item;
    item.volume      = .8f;
    item.fadeInTime  = 2.f;
    item.fadeOutTime = 2.f;
    item.duration    = 10.f;
    REQUIRE(profile.addItem(item).ok());
    AudioZoneState state;
    auto           start = evaluateAudioZone(profile, state, .1f, 0.f, 0.f, 0.f, .5f, 1u);
    REQUIRE(start.ok());
    CHECK(start.value().play);
    CHECK(state.phase == AudioZonePhase::Active);
    auto fade = evaluateAudioZone(profile, state, 1.1f, 0.f, 0.f, 0.f, .5f, 1u);
    REQUIRE(fade.ok());
    CHECK(fade.value().volume > .39f);
    CHECK(fade.value().volume < .41f);
    auto leave = evaluateAudioZone(profile, state, 2.f, 20.f, 0.f, 0.f, 1.f, 1u);
    REQUIRE(leave.ok());
    CHECK(state.phase == AudioZonePhase::BecomingInactive);
    CHECK(!leave.value().stop);
    REQUIRE(evaluateAudioZone(profile, state, 11.9f, 20.f, 0.f, 0.f, 1.f, 1u).ok());
    CHECK(state.phase == AudioZonePhase::BecomingInactive);
    auto stopped = evaluateAudioZone(profile, state, 12.1f, 20.f, 0.f, 0.f, 1.f, 1u);
    REQUIRE(stopped.ok());
    CHECK(stopped.value().stop);
    CHECK(state.phase == AudioZonePhase::Inactive);
}

TEST_CASE("audio.pcgZone.avoidsImmediateRepeatAndFailsAtomically") {
    AudioZoneProfile profile;
    profile.global           = true;
    profile.minimumBreakTime = 0;
    profile.maximumBreakTime = 0;
    AudioZoneItem item;
    item.duration    = .5f;
    item.fadeInTime  = 0;
    item.fadeOutTime = 0;
    REQUIRE(profile.addItem(item).ok());
    REQUIRE(profile.addItem(item).ok());
    AudioZoneState state;
    auto           first = evaluateAudioZone(profile, state, .1f, 0, 0, 0, 1, 55u);
    REQUIRE(first.ok());
    const int selected = state.selectedTrack;
    REQUIRE(evaluateAudioZone(profile, state, .7f, 0, 0, 0, 1, 55u).ok());
    auto second = evaluateAudioZone(profile, state, .8f, 0, 0, 0, 1, 55u);
    REQUIRE(second.ok());
    CHECK(second.value().play);
    CHECK(state.selectedTrack != selected);
    const auto before = state;
    CHECK(!evaluateAudioZone(profile, state, std::numeric_limits<float>::quiet_NaN(), 0, 0, 0, 1, 1u).ok());
    CHECK(state.selectedTrack == before.selectedTrack);
    CHECK(state.playing == before.playing);
}

TEST_CASE("audio.pcgZone.squirrelContract") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    eve::script::exposeResultBindings(table);
    Audio::expose(table);
    vm.run(vm.compileSource(R"(
        local profile=eve.AudioZoneProfile(); profile.radius=12.0;
        profile.minimumBreakTime=1.0; profile.maximumBreakTime=1.0;
        local item=eve.AudioZoneItem(); item.volume=0.8; item.fadeInTime=2.0;
        item.fadeOutTime=2.0; item.duration=10.0;
        assert(profile.addItem(item).value==0 && profile.getItemCount()==1);
        local state=eve.AudioZoneState();
        local start=eve.evaluateAudioZone(profile,state,0.1,0.0,0.0,0.0,0.5,17);
        assert(start.ok && start.value.play && start.value.trackIndex==0);
        local fade=eve.evaluateAudioZone(profile,state,1.1,0.0,0.0,0.0,0.5,17);
        assert(fade.ok && fade.value.volume>0.39 && fade.value.volume<0.41);
    )"));
}
}  // namespace test_audio
#endif
