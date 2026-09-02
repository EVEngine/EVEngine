#include "action/Action.h"
#include "action/ActionTimeline.h"
#include "action_editor/ActionTimelineEditor.h"

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
