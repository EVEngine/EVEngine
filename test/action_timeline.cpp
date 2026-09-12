#include "action/Action.h"
#include "action/ActionTimeline.h"
#include "action/editor/ActionTimelineEditor.h"

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
    auto clipboard = std::make_shared<eve::editor::ActionTimelineClipboard>();
    eve::editor::ActionTimelineEditor source("asset.combat.source", timelineFixture(), clipboard);

    auto destinationTimeline              = timelineFixture();
    destinationTimeline.actionId          = id("combat:alternate-attack");
    destinationTimeline.tracks[0].id      = id("combat-track:alternate");
    destinationTimeline.tracks[0].label   = "Alternate";
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
    auto externalOpened = external.open(
        {eve::editor::DocumentKind::Timeline, eve::editor::AssetGuid("heavy-attack")}, "Heavy Attack",
        "content://Actions/HeavyAttack.action");
    REQUIRE(externalOpened.ok());
    auto externallyEdited = secondTimeline;
    externallyEdited.tracks[0].label = "External Gameplay";
    auto externalValue = externallyEdited.toValue();
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
    externalValue = externallyEdited.toValue();
    REQUIRE(externalValue.ok());
    externalSnapshot = external.snapshot(externalOpened.value().id);
    REQUIRE(externalSnapshot.ok());
    REQUIRE(external.edit(externalOpened.value().id, eve::editor::toEditorValue(externalValue.value()),
                          externalSnapshot.value().revision.edit)
                .ok());
    externalTicket = external.requestSave(externalOpened.value().id);
    REQUIRE(externalTicket.ok());
    REQUIRE(external.executeSave(externalTicket.value()).ok());
    REQUIRE(workspace.reconcile(second.value()).ok());
    CHECK_EQ(workspace.activeEditor()->target().timeline().tracks[0].label, "External Recovered");

    REQUIRE(workspace.activeEditor()->renameTrack(id("combat-track:gameplay"), "Unsaved Local").ok());
    externallyEdited.tracks[0].label = "External Again";
    externalValue = externallyEdited.toValue();
    REQUIRE(externalValue.ok());
    externalSnapshot = external.snapshot(externalOpened.value().id);
    REQUIRE(externalSnapshot.ok());
    REQUIRE(external.edit(externalOpened.value().id, eve::editor::toEditorValue(externalValue.value()),
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
