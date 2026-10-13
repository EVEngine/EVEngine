#include <algorithm>
#include <cmath>
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimStateMachine.h"
#include "animation/editing/AnimationClip.h"
#include "animation/editing/AnimationGraph.h"
#include "animation/editor/AnimationClipEditor.h"
#include "animation/editor/AnimationEditorModule.h"
#include "animation/editor/EditorAnimationClip.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"
#include "editor/EditorWorkspace.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_animation_clip.cpp
using namespace eve::animation_editing;
using namespace eve::editing;

namespace {
AnimationTransformKey key(const char* id, double time, double x) {
    AnimationTransformKey result;
    result.id        = StableId(id);
    result.time      = time;
    result.positionX = x;
    return result;
}
}  // namespace

TEST_CASE("editor.animation.clip_timeline_supports_stable_reversible_tracks_events_and_masks") {
    AnimationClipDocumentTarget clip("walk");
    auto                        settings = clip.makeSetSettings(2.0, 60.0, true);
    REQUIRE(settings.ok());
    CHECK(clip.applyDomainOperation(settings.value()).ok());
    AnimationBoneTrack track{StableId("hips-track"), "mixamorig:Hips", {key("start", 0.0, 0.0), key("end", 2.0, 4.0)}};
    auto               setTrack = clip.makeSetTrack(track);
    REQUIRE(setTrack.ok());
    CHECK(clip.applyDomainOperation(setTrack.value()).ok());
    auto event = clip.makeSetEvent({StableId("footstep"), 0.5, "footstep", "left"});
    REQUIRE(event.ok());
    CHECK(clip.applyDomainOperation(event.value()).ok());
    auto mask = clip.makeSetMask({"mixamorig:Hips", 0.25});
    REQUIRE(mask.ok());
    CHECK(clip.applyDomainOperation(mask.value()).ok());
    const auto preview = clip.preview(1.0, {"mixamorig:Hips"});
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(preview.bones.size(), 1U);
    CHECK_EQ(preview.bones[0].positionX, 2.0);
    CHECK_EQ(preview.bones[0].maskWeight, 0.25);
    auto remove = clip.makeDeleteTrack(StableId("hips-track"));
    REQUIRE(remove.ok());
    CHECK(clip.applyDomainOperation(remove.value()).ok());
    DomainOperation undo = remove.value();
    undo.type            = remove.value().inverseType;
    undo.payload         = remove.value().inverse;
    CHECK(clip.applyDomainOperation(undo).ok());
    CHECK_EQ(clip.tracks().size(), 1U);
}

TEST_CASE("editor.animation.clip_snapshot_and_retarget_preview_are_non_destructive") {
    AnimationClipDocumentTarget clip("source");
    auto                        track = clip.makeSetTrack({StableId("hips"), "mixamorig:Hips", {key("key", 0.0, 1.0)}});
    REQUIRE(track.ok());
    CHECK(clip.applyDomainOperation(track.value()).ok());
    const auto mapping = clip.previewRetarget({"hips", "spine"});
    CHECK_EQ(static_cast<int>(mapping.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(mapping.mapping.at("mixamorig:Hips"), "hips");
    AnimationClipDocumentTarget restored("source");
    CHECK(restored.loadSnapshot(clip.snapshotValue()).ok());
    CHECK_EQ(restored.tracks().size(), 1U);
}

TEST_CASE("editor.animation.clip_builds_real_runtime_asset_and_rejects_missing_bones") {
    AnimationClipDocumentTarget document("run");
    auto track = document.makeSetTrack({StableId("root-track"), "root", {key("a", 0.0, 0.0), key("b", 1.0, 2.0)}});
    REQUIRE(track.ok());
    CHECK(document.applyDomainOperation(track.value()).ok());
    auto event = document.makeSetEvent({StableId("event"), 0.25, "step", "right"});
    REQUIRE(event.ok());
    CHECK(document.applyDomainOperation(event.value()).ok());
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimationClipRuntimeBuilder builder;
    auto                        built = builder.build(document, &skeleton);
    REQUIRE(built.ok());
    CHECK_EQ(built.value()->getPositionKeyCount(0), 2);
    CHECK_EQ(built.value()->getEventCount(), 1);
    delete built.value();
    eve::animation::AnimSkeleton wrong;
    wrong.addBone("pelvis");
    CHECK_EQ(static_cast<int>(builder.build(document, &wrong).code()), static_cast<int>(EditorStatus::Rejected));
}

// editor_animation_clip_editor.cpp
using eve::animation_editor::AnimationClipEditor;
using eve::editor::EditorWorkspace;

TEST_CASE("editor.animation.clip_editor_installs_workspace_and_reverses_mask") {
    AnimationClipEditor editor("preview.walk");
    EditorWorkspace     workspace("animation.preview", "Animation Clip Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    CHECK_EQ(workspace.getPanelId(0), std::string("animation.skeleton"));
    CHECK_EQ(workspace.getPanelCapability(0), std::string("animation.clip"));
    CHECK_EQ(workspace.getPanelContext(0), std::string("list"));
    CHECK_EQ(workspace.getPanelContext(1), std::string("preview"));
    CHECK_EQ(workspace.getPanelContext(2), std::string("inspector"));
    CHECK_EQ(workspace.getPanelContext(3), std::string("timeline"));

    REQUIRE(editor.setViewport(640.0f, 36.0f, 120.0f).ok());
    CHECK_EQ(editor.trackCount(), 6);
    CHECK_EQ(editor.boneCount(), 6);
    CHECK_EQ(editor.boneParent(3), std::string("Chest"));
    CHECK(editor.keyCount() >= 12);
    CHECK_EQ(editor.eventCount(), 1);
    CHECK(editor.primitiveCount() > 0);
    CHECK_EQ(editor.preview().documentRevision, editor.revision());

    REQUIRE(editor.seekSeconds(1.0).ok());
    CHECK(std::abs(editor.playhead() - 1.0) < 0.0001);
    double hipsX = 0.0;
    for (const auto& bone : editor.preview().bones)
        if (bone.bone == "Hips") hipsX = bone.positionX;
    CHECK(std::abs(hipsX - 1.0) < 0.05);

    const double before = editor.selectedMaskWeight();
    REQUIRE(editor.setMaskWeight(0.25).ok());
    CHECK(std::abs(editor.selectedMaskWeight() - 0.25) < 0.0001);
    REQUIRE(editor.undo().ok());
    CHECK(std::abs(editor.selectedMaskWeight() - before) < 0.0001);
}

TEST_CASE("editor.animation.clip_editor_pointer_selects_bone_and_rejects_invalid_settings") {
    AnimationClipEditor editor("preview.walk");
    REQUIRE(editor.setViewport(800.0f, 36.0f, 120.0f).ok());
    REQUIRE(editor.pointerDown(20.0f, 50.0f).ok());
    CHECK_EQ(editor.selectedBone(), std::string("Spine"));

    editor.play();
    REQUIRE(editor.update(0.2).ok());
    CHECK(editor.playhead() > 0.0);
    CHECK(editor.isPlaying());

    auto rejected = editor.setDuration(0.0);
    CHECK(!rejected.ok());
    CHECK_EQ(editor.duration(), 2.0);
}

TEST_CASE("editor.animation.clip_editor_keys_edits_and_deletes_joint_transform") {
    AnimationClipEditor editor("preview.walk");
    REQUIRE(editor.selectBone("Spine").ok());
    REQUIRE(editor.seekSeconds(1.0).ok());
    const int beforeKeys = editor.keyCount();

    REQUIRE(editor.setSelectedPosition(1.25, 2.5, -0.5).ok());
    CHECK(editor.hasSelectedKey());
    CHECK_EQ(editor.keyCount(), beforeKeys + 1);
    CHECK(std::abs(editor.selectedPositionX() - 1.25) < 0.0001);
    CHECK(std::abs(editor.selectedPositionY() - 2.5) < 0.0001);
    CHECK(std::abs(editor.selectedPositionZ() + 0.5) < 0.0001);

    REQUIRE(editor.setSelectedRotation(15.0, 30.0, -20.0).ok());
    CHECK(std::abs(editor.selectedRotationX() - 15.0) < 0.01);
    CHECK(std::abs(editor.selectedRotationY() - 30.0) < 0.01);
    CHECK(std::abs(editor.selectedRotationZ() + 20.0) < 0.01);
    REQUIRE(editor.setSelectedScale(1.1, 1.2, 1.3).ok());
    CHECK(std::abs(editor.selectedScaleY() - 1.2) < 0.0001);

    REQUIRE(editor.moveSelectedKey(1.25).ok());
    CHECK(std::abs(editor.selectedKeyTime() - 1.25) < 0.0001);
    REQUIRE(editor.deleteSelectedKey().ok());
    CHECK(!editor.hasSelectedKey());
    CHECK_EQ(editor.keyCount(), beforeKeys);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.keyCount(), beforeKeys + 1);
}

TEST_CASE("editor.animation.clip_editor_round_trips_real_runtime_skeleton_and_clip") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("Root");
    const int                    hand = skeleton.addBone("Hand", root);
    skeleton.setBindPosition(hand, 0.0f, 1.0f, 0.0f);

    eve::animation::AnimClip clip("attack");
    clip.setDuration(1.0f);
    clip.setSampleRate(30.0f);
    clip.setLoop(false);
    clip.addPositionKey(root, 0.0f, 0.0f, 0.0f, 0.0f);
    clip.addPositionKey(root, 1.0f, 1.0f, 0.0f, 0.0f);
    clip.addRotationKey(hand, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    clip.addRotationKey(hand, 1.0f, 0.0f, 0.7071067f, 0.0f, 0.7071067f);

    AnimationClipEditor editor("runtime.attack");
    REQUIRE(editor.loadRuntimeClip(skeleton, clip).ok());
    CHECK_EQ(editor.boneCount(), 2);
    CHECK_EQ(editor.boneName(1), std::string("Hand"));
    CHECK_EQ(editor.boneParent(1), std::string("Root"));
    REQUIRE(editor.selectBone("Hand").ok());
    REQUIRE(editor.seekSeconds(0.5).ok());
    REQUIRE(editor.setSelectedPosition(0.25, 1.25, -0.5).ok());
    REQUIRE(editor.writeRuntimeClip(clip, skeleton).ok());

    eve::animation::AnimPose pose(2);
    clip.sample(0.5f, &pose, &skeleton);
    CHECK(std::abs(pose.getLocalPositionX(hand) - 0.25f) < 0.0001f);
    CHECK(std::abs(pose.getLocalPositionY(hand) - 1.25f) < 0.0001f);
    CHECK(std::abs(pose.getLocalPositionZ(hand) + 0.5f) < 0.0001f);
}

// editor_animation_editor.cpp
using namespace eve::editor;

TEST_CASE("editor.animation.clip_automation_owns_headless_targets") {
    Editor                                       editor;
    eve::animation_editor::AnimationEditorModule adapter;
    auto*                                        automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("animation.clip.settings.set.v1") != std::string::npos);
    CHECK(commands.find("animation.clip.track.set.v1") != std::string::npos);

    const std::string created =
        automation->invoke("target-create", R"({"target":"agent.clip","type":"animation-clip"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string settings = automation->invoke(
        "execute",
        R"({"target":"agent.clip","command":"animation.clip.settings.set.v1","payload":{"duration":2,"sampleRate":60,"loop":true}})");
    REQUIRE(settings.find("\"status\":\"applied\"") != std::string::npos);

    const std::string tracked = automation->invoke(
        "execute",
        R"({"target":"agent.clip","command":"animation.clip.track.set.v1","payload":{"id":"hips-track","bone":"Hips","keys":[{"id":"start","time":0,"px":0,"py":0,"pz":0,"rx":0,"ry":0,"rz":0,"rw":1,"sx":1,"sy":1,"sz":1},{"id":"end","time":2,"px":4,"py":0,"pz":0,"rx":0,"ry":0,"rz":0,"rw":1,"sx":1,"sy":1,"sz":1}]}})");
    REQUIRE(tracked.find("\"status\":\"applied\"") != std::string::npos);

    const std::string masked = automation->invoke(
        "execute",
        R"({"target":"agent.clip","command":"animation.clip.mask.set.v1","payload":{"bone":"Hips","weight":0.25}})");
    REQUIRE(masked.find("\"status\":\"applied\"") != std::string::npos);

    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.clip"})");
    CHECK(inspected.find("animation-clip-document") != std::string::npos);
    CHECK(inspected.find("hips-track") != std::string::npos);
    CHECK(inspected.find("Hips") != std::string::npos);

    const std::string undone = automation->invoke("undo", R"({"target":"agent.clip"})");
    CHECK(undone.find("\"status\":\"applied\"") != std::string::npos);
    const std::string afterUndo = automation->invoke("inspect", R"({"target":"agent.clip"})");
    CHECK(afterUndo.find("0.25") == std::string::npos);

    const std::string closed = automation->invoke("target-close", R"({"target":"agent.clip"})");
    CHECK(closed.find("\"status\":\"applied\"") != std::string::npos);
    const std::string afterClose = automation->invoke("inspect", R"({"target":"agent.clip"})");
    CHECK(afterClose.find("not-found") != std::string::npos);
}

// editor_animation_graph.cpp
using namespace eve::animation_editing;
using namespace eve::editing;

namespace {

GraphDocumentData basicGraph(AnimationStateGraphDomain& domain) {
    auto idle       = domain.makeStateNode(GraphNodeId("idle"), "asset://animations/idle.eva");
    auto run        = domain.makeStateNode(GraphNodeId("run"), "asset://animations/run.eva");
    auto transition = domain.makeTransitionNode(GraphNodeId("idle-to-run"));
    REQUIRE(idle.ok());
    REQUIRE(run.ok());
    REQUIRE(transition.ok());

    EditorValue::Object transitionProperties = *transition.value().properties.getIf<EditorValue::Object>();
    transitionProperties["blendSeconds"]     = 0.15;
    transitionProperties["conditionKind"]    = "float";
    transitionProperties["parameter"]        = "speed";
    transitionProperties["operator"]         = ">";
    transitionProperties["threshold"]        = 0.1;
    transition.value().properties            = EditorValue(std::move(transitionProperties));

    eve::editing::GraphDocument graph;
    REQUIRE(graph.createNode(idle.value()).ok());
    REQUIRE(graph.createNode(run.value()).ok());
    REQUIRE(graph.createNode(transition.value()).ok());
    const GraphPinRecord* idleOut       = graph.findPin(GraphPinId("idle.out"));
    const GraphPinRecord* transitionIn  = graph.findPin(GraphPinId("idle-to-run.in"));
    const GraphPinRecord* transitionOut = graph.findPin(GraphPinId("idle-to-run.out"));
    const GraphPinRecord* runIn         = graph.findPin(GraphPinId("run.in"));
    REQUIRE(idleOut);
    REQUIRE(transitionIn);
    REQUIRE(transitionOut);
    REQUIRE(runIn);
    REQUIRE(
        graph.connect({StableId("edge-in"), idleOut->id, transitionIn->id}, domain.canConnect(*idleOut, *transitionIn))
            .ok());
    REQUIRE(
        graph.connect({StableId("edge-out"), transitionOut->id, runIn->id}, domain.canConnect(*transitionOut, *runIn))
            .ok());
    EditorValue::Object parameters;
    parameters["entry"] = "idle";
    REQUIRE(graph.setParameters(EditorValue(std::move(parameters))).ok());
    return graph.snapshot(domain.domain());
}

}  // namespace

TEST_CASE("editor.animation.state_graph_compiles_stable_clip_references_and_conditions") {
    AnimationStateGraphDomain domain;
    const auto                graph    = basicGraph(domain);
    const auto                compiled = domain.compile(graph);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(compiled.documentRevision, graph.revision);
    CHECK(compiled.definition.find("EVANIM_STATE_GRAPH 1\n") == 0);
    CHECK(compiled.definition.find("state \"idle\" \"asset://animations/idle.eva\"") != std::string::npos);
    CHECK(compiled.definition.find("transition \"idle-to-run\" \"idle\" \"run\" 0.15") != std::string::npos);
    CHECK(compiled.definition.find("\"float\" \"speed\" \">\" 0.1") != std::string::npos);
}

TEST_CASE("editor.animation.state_graph_rejects_incomplete_or_invalid_authoring_data") {
    AnimationStateGraphDomain domain;
    GraphDocumentData         graph = basicGraph(domain);
    graph.schemaVersion             = 99;
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Unsupported));

    graph = basicGraph(domain);
    graph.edges.pop_back();
    auto result = domain.compile(graph);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!result.diagnostics.empty());

    graph           = basicGraph(domain);
    auto transition = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                   [](const GraphNodeRecord& node) { return node.type == "animation.transition"; });
    REQUIRE(transition != graph.nodes.end());
    auto* properties = transition->properties.getIf<EditorValue::Object>();
    REQUIRE(properties);
    (*properties)["blendSeconds"] = -1.0;
    result                        = domain.compile(graph);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));

    graph            = basicGraph(domain);
    auto* parameters = graph.parameters.getIf<EditorValue::Object>();
    REQUIRE(parameters);
    (*parameters)["entry"] = "missing";
    result                 = domain.compile(graph);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.animation.state_graph_rejects_direct_state_connections") {
    AnimationStateGraphDomain domain;
    auto                      idle = domain.makeStateNode(GraphNodeId("idle"), "idle.eva");
    auto                      run  = domain.makeStateNode(GraphNodeId("run"), "run.eva");
    REQUIRE(idle.ok());
    REQUIRE(run.ok());
    CHECK(domain.canConnect(idle.value().pins[1], run.value().pins[0]).allowed);

    GraphDocumentData invalid;
    invalid.domain      = domain.domain();
    invalid.nodes       = {idle.value(), run.value()};
    invalid.edges       = {{StableId("direct"), idle.value().pins[1].id, run.value().pins[0].id}};
    invalid.parameters  = EditorValue::Object{{"entry", "idle"}};
    const auto compiled = domain.compile(invalid);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.animation.state_graph_builds_real_runtime_state_machine") {
    AnimationStateGraphDomain    domain;
    const GraphDocumentData      graph = basicGraph(domain);
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::AnimClip idle("idle");
    eve::animation::AnimClip run("run");
    idle.setDuration(1.0F);
    run.setDuration(1.0F);

    AnimationStateGraphRuntimeBuilder builder;
    auto                              built = builder.build(graph, &skeleton, [&](const std::string& asset) {
        if (asset == "asset://animations/idle.eva") return &idle;
        if (asset == "asset://animations/run.eva") return &run;
        return static_cast<eve::animation::AnimClip*>(nullptr);
    });
    REQUIRE(built.ok());
    CHECK_EQ(built.value()->getStateCount(), 2);
    delete built.value();

    auto missing = builder.build(graph, &skeleton,
                                 [](const std::string&) { return static_cast<eve::animation::AnimClip*>(nullptr); });
    CHECK_EQ(static_cast<int>(missing.code()), static_cast<int>(EditorStatus::NotFound));
}
