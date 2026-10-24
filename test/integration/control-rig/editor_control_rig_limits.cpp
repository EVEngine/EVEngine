// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "cases": {
    "module-composition": [
      "editor.controlRig.limits.sharedHistoryAndAtomicRejection"
    ]
  },
  "module": "animation"
}
*/
// clang-format on
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/ControlRig.h"
#include "animation/editor/AnimationEditorModule.h"
#include "animation/editor/ControlRigEditor.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "common/Value.h"
#include "editor/Editor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
TEST_CASE("editor.controlRig.limits.sharedHistoryAndAtomicRejection") {
    eve::editor::Editor                          host;
    eve::animation_editor::AnimationEditorModule module;
    eve::animation::AnimSkeleton                 skeleton;
    skeleton.addBone("root");
    eve::animation_editor::ControlRigEditor rig("rig");
    REQUIRE(rig.initialize(skeleton).ok());
    REQUIRE(rig.moveControl(0, 3, 0, 0, false).ok());
    REQUIRE(rig.enableAutomation().ok());
    auto snapshot = rig.snapshot();
    REQUIRE(snapshot.ok());
    auto d = eve::animation::decodeControlRig(snapshot.value());
    REQUIRE(d.ok());
    d.value().controls[0].limits.translation[0] = {eve::animation::RigAxisMode::Limited, -1, 1};
    auto encoded                                = eve::animation::encodeControlRig(d.value());
    REQUIRE(encoded.ok());
    auto json = encoded.value().toJson();
    REQUIRE(json.ok());
    auto* api = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(api != nullptr);
    auto result = api->invoke(
        "execute", "{\"target\":\"rig\",\"command\":\"animation.rig.replace.v1\",\"payload\":" + json.value() + "}");
    CHECK(result.find("applied") != std::string::npos);
    auto local = rig.controlLocal(0);
    REQUIRE(local.ok());
    CHECK(local.value().px == 1);
    REQUIRE(rig.undo().ok());
    local = rig.controlLocal(0);
    REQUIRE(local.ok());
    CHECK(local.value().px == 3);
    CHECK(api->invoke("redo", R"({"target":"rig"})").find("applied") != std::string::npos);
    REQUIRE(rig.moveControl(0, -9, 0, 0, true).ok());
    local = rig.controlLocal(0);
    REQUIRE(local.ok());
    CHECK(local.value().px == -1);
    const auto revision = rig.revision();
    auto       invalid  = rig.snapshot();
    REQUIRE(invalid.ok());
    invalid.value().find("controls")->at(0).find("limits")->find("translation")->at(0).set("min", 2);
    CHECK(!rig.restore(invalid.value()).ok());
    CHECK(rig.revision() == revision);
    CHECK(api->invoke("undo", R"({"target":"rig"})").find("applied") != std::string::npos);
    local = rig.controlLocal(0);
    REQUIRE(local.ok());
    CHECK(local.value().px == 1);
}
