// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "cases": {
    "module-composition": [
      "editor.controlRig.transactionHistoryAndRoundTrip",
      "editor.controlRig.documentRejectsForeignPublication",
      "editor.controlRig.genericGraphRoundTripAndStaleRevision",
      "editor.controlRig.dragPreviewDoesNotMutateHistory",
      "editor.controlRig.matchPoseIsOneUndoableEdit",
      "editor.controlRig.failedHistoryPreviewRestoresDocument"
    ],
    "script-binding": [
      "editor.controlRig.scriptFacadeProjectsNumericTimeAndOwnedResults"
    ]
  },
  "module": "animation"
}
*/
// clang-format on
#include <simplesquirrel/simplesquirrel.hpp>
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/ControlRig.h"
#include "animation/editing/ControlRigDocument.h"
#include "animation/editor/ControlRigEditor.h"
#include "common/Module.h"
#include "common/Value.h"
#include "editing/EditingGraph.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("editor.controlRig.transactionHistoryAndRoundTrip") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation_editor::ControlRigEditor editor("rig");
    REQUIRE(editor.initialize(skeleton).ok());
    eve::animation::TransformTRS t;
    t.px = 3;
    REQUIRE(editor.setControl("root", t, true).ok());
    auto local = editor.controlLocal(0);
    REQUIRE(local.ok());
    CHECK(local.value().px == 3);
    REQUIRE(editor.undo().ok());
    auto undone = editor.controlLocal(0);
    REQUIRE(undone.ok());
    CHECK(undone.value().px == 0);
    REQUIRE(editor.redo().ok());
    auto redone = editor.controlLocal(0);
    REQUIRE(redone.ok());
    CHECK(redone.value().px == 3);
    auto saved = editor.snapshot();
    REQUIRE(saved.ok());
    eve::animation_editor::ControlRigEditor restored("restored");
    REQUIRE(restored.initialize(skeleton).ok());
    REQUIRE(restored.restore(saved.value()).ok());
    eve::animation::AnimPose pose;
    REQUIRE(restored.copyPose(pose).ok());
    CHECK(pose.local(0).px == 3);
    saved.value().set("version", std::int64_t{99});
    CHECK(!restored.restore(saved.value()).ok());
    REQUIRE(restored.copyPose(pose).ok());
    CHECK(pose.local(0).px == 3);
    eve::animation::AnimClip clip;
    REQUIRE(restored.bakeInto(clip, 30).ok());
    clip.sample(1, &pose);
    CHECK(pose.local(0).px == 3);
}

TEST_CASE("editor.controlRig.documentRejectsForeignPublication") {
    eve::animation_editing::ControlRigDocument a("a"), b("b");
    auto                                       candidate = b.cloneDomainState();
    CHECK(!a.commitDomainState(std::move(candidate)).ok());
    eve::animation::RigDefinition definition;
    auto                          operation = b.replace(definition);
    REQUIRE(operation.ok());
    CHECK(!a.applyDomainOperation(operation.value()).ok());
    CHECK(a.revision() == 1);
}

TEST_CASE("editor.controlRig.genericGraphRoundTripAndStaleRevision") {
    eve::animation_editing::ControlRigDocument target("rig");
    eve::animation::RigDefinition              d;
    eve::animation::RigControl                 control;
    control.id = "control";
    d.controls.push_back(control);
    eve::animation::RigNode first;
    first.id      = "first";
    first.control = "control";
    first.bone    = "root";
    d.nodes.push_back(first);
    auto second  = first;
    second.id    = "second";
    second.after = {"first"};
    d.nodes.push_back(second);
    auto op = target.replace(d);
    REQUIRE(op.ok());
    REQUIRE(target.applyDomainOperation(op.value()).ok());
    auto graph = target.graph();
    REQUIRE(graph.ok());
    CHECK(graph.value().edges.size() == 1);
    auto roundtrip = target.replaceGraph(graph.value());
    REQUIRE(roundtrip.ok());
    REQUIRE(target.applyDomainOperation(roundtrip.value()).ok());
    CHECK(!target.replaceGraph(graph.value()).ok());
    auto cyclic = target.graph();
    REQUIRE(cyclic.ok());
    cyclic.value().edges.push_back({eve::editing::StableId("cycle"), eve::editing::GraphPinId("second:out"),
                                    eve::editing::GraphPinId("first:in")});
    CHECK(!target.replaceGraph(cyclic.value()).ok());
}

TEST_CASE("editor.controlRig.dragPreviewDoesNotMutateHistory") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation_editor::ControlRigEditor editor("drag");
    REQUIRE(editor.initialize(skeleton).ok());
    REQUIRE(editor.previewMoveControl(0, 2, 0, 0).ok());
    eve::animation::AnimPose pose;
    REQUIRE(editor.copyPose(pose).ok());
    CHECK(pose.local(0).px == 2);
    auto unchanged = editor.controlLocal(0);
    REQUIRE(unchanged.ok());
    CHECK(unchanged.value().px == 0);
    REQUIRE(editor.seek(0).ok());
    REQUIRE(editor.copyPose(pose).ok());
    CHECK(pose.local(0).px == 0);
    REQUIRE(editor.moveControl(0, 2, 0, 0, true).ok());
    REQUIRE(editor.undo().ok());
    REQUIRE(editor.copyPose(pose).ok());
    CHECK(pose.local(0).px == 0);
}

TEST_CASE("editor.controlRig.matchPoseIsOneUndoableEdit") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("child", 0);
    eve::animation_editor::ControlRigEditor editor("match");
    REQUIRE(editor.initialize(skeleton).ok());
    eve::animation::AnimPose input;
    skeleton.applyBindPose(&input);
    input.local(0).px = 2;
    input.local(1).py = 3;
    REQUIRE(editor.keyFromPose(input).ok());
    eve::animation::AnimPose output;
    REQUIRE(editor.copyPose(output).ok());
    CHECK(output.local(0).px == 2);
    CHECK(output.local(1).py == 3);
    REQUIRE(editor.undo().ok());
    REQUIRE(editor.copyPose(output).ok());
    CHECK(output.local(0).px == 0);
    CHECK(output.local(1).py == 0);
}

TEST_CASE("editor.controlRig.failedHistoryPreviewRestoresDocument") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation_editor::ControlRigEditor editor("history");
    REQUIRE(editor.initialize(skeleton).ok());
    auto snapshot = editor.snapshot();
    REQUIRE(snapshot.ok());
    auto decoded = eve::animation::decodeControlRig(snapshot.value());
    REQUIRE(decoded.ok());
    auto d               = decoded.value();
    d.nodes[0].operation = eve::animation::RigOperation::Aim;
    auto target          = d.controls[0].initial;
    target.px            = 1;
    d.controls[0].keys   = {{0, target}, {1, d.controls[0].initial}};
    REQUIRE(editor.replace(d).ok());
    d.nodes[0].operation = eve::animation::RigOperation::FK;
    REQUIRE(editor.replace(d).ok());
    REQUIRE(editor.seek(1).ok());
    CHECK(!editor.undo().ok());
    auto unchanged = editor.snapshot();
    REQUIRE(unchanged.ok());
    auto definition = eve::animation::decodeControlRig(unchanged.value());
    REQUIRE(definition.ok());
    CHECK(definition.value().nodes[0].operation == eve::animation::RigOperation::FK);
    REQUIRE(editor.seek(0).ok());
    REQUIRE(editor.undo().ok());
}

TEST_CASE("editor.controlRig.scriptFacadeProjectsNumericTimeAndOwnedResults") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        animation <- eve.Animation();
        skeleton <- animation.newSkeleton(); skeleton.addBone("root",-1);
        created <- eve.AnimationEditorModule().createRig("script-rig");
        rig <- created.value;
        initialized <- rig.initialize(skeleton);
        timeType <- typeof rig.getTime(); durationType <- typeof rig.getDuration();
        edited <- rig.setControl("root",2.0,0.0,0.0,0.0,0.0,0.0,1.0,1.0,true);
        sought <- rig.seek(1.0); exported <- rig.exportJson(); restored <- rig.importJson(exported.value);
        pose <- animation.newPose(1); copied <- rig.copyPose(pose);
        clip <- animation.newClip("baked"); baked <- rig.bakeInto(clip,30.0);
    )"));
    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK(vm.find("created").toTable().get<std::string>("ownership") == "owned");
    CHECK(vm.find("timeType").toString() == "float");
    CHECK(vm.find("durationType").toString() == "float");
    for (const char* name : {"initialized", "edited", "sought", "restored", "copied", "baked"})
        REQUIRE(vm.find(name).toTable().get<bool>("ok"));
}
