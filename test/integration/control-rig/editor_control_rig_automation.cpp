// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "cases": {
    "module-composition": [
      "editor.controlRig.automation.keysNodesAndGeneration",
      "editor.controlRig.automation.lifecycleAndAbsentHost",
      "editor.controlRig.automation.nestedSchemaUsesSharedProjection",
      "editor.controlRig.automation.oldOwnerCannotRemoveReplacement",
      "editor.controlRig.automation.plansRejectStaleAndInvalidAtomically",
      "editor.controlRig.automation.sharedDocumentHistoryAndSchema"
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
#include "editing/EditingValueJson.h"
#include "editing/PropertySchemaJson.h"
#include "editor/Editor.h"
#include "editor/EditorTargetCoordinator.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
using eve::animation_editor::ControlRigEditor;
std::string snapshot(ControlRigEditor& rig) {
    auto v = rig.snapshot();
    REQUIRE(v.ok());
    auto text = v.value().toJson();
    REQUIRE(text.ok());
    return text.value();
}
std::string changed(ControlRigEditor& rig, float x) {
    auto v = rig.snapshot();
    REQUIRE(v.ok());
    auto d = eve::animation::decodeControlRig(v.value());
    REQUIRE(d.ok());
    d.value().controls[0].initial.px = x;
    auto encoded                     = eve::animation::encodeControlRig(d.value());
    REQUIRE(encoded.ok());
    auto text = encoded.value().toJson();
    REQUIRE(text.ok());
    return text.value();
}
std::string request(std::string payload) {
    return "{\"target\":\"rig\",\"command\":\"animation.rig.replace.v1\",\"payload\":" + payload + "}";
}
float x(ControlRigEditor& rig) {
    eve::animation::AnimPose p;
    REQUIRE(rig.copyPose(p).ok());
    return p.local(0).px;
}
}  // namespace
TEST_CASE("editor.controlRig.automation.sharedDocumentHistoryAndSchema") {
    eve::editor::Editor                          host;
    eve::animation_editor::AnimationEditorModule module;
    eve::animation::AnimSkeleton                 skeleton;
    skeleton.addBone("root");
    ControlRigEditor rig("rig");
    REQUIRE(rig.initialize(skeleton).ok());
    REQUIRE(rig.moveControl(0, 1, 0, 0, false).ok());  // History created before registration is preserved.
    REQUIRE(rig.enableAutomation().ok());
    auto* api = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(api != nullptr);
    CHECK(api->invoke("target-list", "{}").find("control-rig") != std::string::npos);
    auto commands = api->invoke("commands", "{}");
    CHECK(commands.find("inputSchema") != std::string::npos);
    CHECK(commands.find("additionalProperties") != std::string::npos);
    CHECK(commands.find("twoBoneIK") != std::string::npos);
    CHECK(api->invoke("execute", request(changed(rig, 2))).find("applied") != std::string::npos);
    CHECK(x(rig) == 2);
    REQUIRE(rig.undo().ok());
    CHECK(x(rig) == 1);  // UI undoes Agent.
    CHECK(api->invoke("redo", R"({"target":"rig"})").find("applied") != std::string::npos);
    CHECK(x(rig) == 2);
    REQUIRE(rig.moveControl(0, 3, 0, 0, false).ok());
    CHECK(api->invoke("undo", R"({"target":"rig"})").find("applied") != std::string::npos);
    CHECK(x(rig) == 2);  // Agent undoes UI.
    CHECK(api->invoke("inspect", R"({"target":"rig"})").find("eve.control-rig") != std::string::npos);
}
TEST_CASE("editor.controlRig.automation.plansRejectStaleAndInvalidAtomically") {
    eve::editor::Editor                          host;
    eve::animation_editor::AnimationEditorModule module;
    eve::animation::AnimSkeleton                 skeleton;
    skeleton.addBone("root");
    ControlRigEditor rig("rig");
    REQUIRE(rig.initialize(skeleton).ok());
    REQUIRE(rig.enableAutomation().ok());
    auto* api = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(api != nullptr);
    const auto before  = snapshot(rig);
    auto       planned = api->invoke("plan", request(changed(rig, 2)));
    CHECK(snapshot(rig) == before);
    auto parsed = eve::editing::editorValueFromJson(planned);
    REQUIRE(parsed.ok());
    auto& object = *parsed.value().getIf<eve::editing::Value::Object>();
    auto  planId = *object.at("planId").getIf<std::string>();
    REQUIRE(rig.moveControl(0, 1, 0, 0, false).ok());
    CHECK(api->invoke("commit", "{\"planId\":\"" + planId + "\"}").find("conflict") != std::string::npos);
    CHECK(x(rig) == 1);
    auto malformed = changed(rig, 2);
    malformed.insert(1, "\"unexpected\":0,");
    const auto stable = snapshot(rig);
    CHECK(api->invoke("execute", request(malformed)).find("applied") == std::string::npos);
    CHECK(snapshot(rig) == stable);
    auto invalidBone = changed(rig, 2);
    auto pos         = invalidBone.find("\"bone\":\"root\"");
    REQUIRE(pos != std::string::npos);
    invalidBone.replace(pos, 13, "\"bone\":\"absent\"");
    CHECK(api->invoke("execute", request(invalidBone)).find("applied") == std::string::npos);
    CHECK(snapshot(rig) == stable);
    REQUIRE(rig.undo().ok());
    CHECK(x(rig) == 0);
}
TEST_CASE("editor.controlRig.automation.lifecycleAndAbsentHost") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    ControlRigEditor rig("rig");
    REQUIRE(rig.initialize(skeleton).ok());
    CHECK(!rig.enableAutomation().ok());
    {
        eve::editor::Editor                          host;
        eve::animation_editor::AnimationEditorModule module;
        REQUIRE(rig.enableAutomation().ok());
        ControlRigEditor duplicate("rig");
        REQUIRE(duplicate.initialize(skeleton).ok());
        CHECK(!duplicate.enableAutomation().ok());
        CHECK(!rig.initialize(skeleton).ok());
        {
            ControlRigEditor temporary("temporary");
            REQUIRE(temporary.initialize(skeleton).ok());
            REQUIRE(temporary.enableAutomation().ok());
        }
        auto* api = eve::cap::query<eve::IEditorAutomation>();
        REQUIRE(api != nullptr);
        CHECK(api->invoke("target-list", "{}").find("temporary") == std::string::npos);
    }
    REQUIRE(rig.moveControl(0, 4, 0, 0, false).ok());
    REQUIRE(rig.disableAutomation().ok());
    eve::editor::Editor                          replacement;
    eve::animation_editor::AnimationEditorModule module;
    REQUIRE(rig.enableAutomation().ok());
    REQUIRE(rig.undo().ok());
    CHECK(x(rig) == 0);
}
TEST_CASE("editor.controlRig.automation.keysNodesAndGeneration") {
    eve::editor::Editor                          host;
    eve::animation_editor::AnimationEditorModule module;
    eve::animation::AnimSkeleton                 skeleton;
    skeleton.addBone("root");
    ControlRigEditor rig("rig");
    REQUIRE(rig.initialize(skeleton).ok());
    REQUIRE(rig.enableAutomation().ok());
    auto* api = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(api != nullptr);
    auto value = rig.snapshot();
    REQUIRE(value.ok());
    auto d = eve::animation::decodeControlRig(value.value());
    REQUIRE(d.ok());
    auto transform             = d.value().controls[0].initial;
    transform.px               = 2;
    d.value().controls[0].keys = {{0, d.value().controls[0].initial}, {1, transform}};
    eve::animation::RigControl control;
    control.id = "extra";
    d.value().controls.push_back(control);
    d.value().nodes[0].control = "extra";
    auto encoded               = eve::animation::encodeControlRig(d.value());
    REQUIRE(encoded.ok());
    auto text = encoded.value().toJson();
    REQUIRE(text.ok());
    auto plan = eve::editing::editorValueFromJson(api->invoke("plan", request(text.value())));
    REQUIRE(plan.ok());
    auto id = *plan.value().getIf<eve::editing::Value::Object>()->at("planId").getIf<std::string>();
    REQUIRE(api->invoke("commit", "{\"planId\":\"" + id + "\"}").find("applied") != std::string::npos);
    CHECK(rig.controlCount() == 2);
    REQUIRE(rig.seek(1).ok());
    CHECK(x(rig) == 0);
    REQUIRE(rig.undo().ok());
    CHECK(rig.controlCount() == 1);
    auto pending = eve::editing::editorValueFromJson(api->invoke("plan", request(text.value())));
    REQUIRE(pending.ok());
    auto stale = *pending.value().getIf<eve::editing::Value::Object>()->at("planId").getIf<std::string>();
    REQUIRE(rig.disableAutomation().ok());
    REQUIRE(rig.enableAutomation().ok());
    auto result = api->invoke("commit", "{\"planId\":\"" + stale + "\"}");
    CHECK(result.find("applied") == std::string::npos);
    CHECK(rig.controlCount() == 1);
}

TEST_CASE("editor.controlRig.automation.oldOwnerCannotRemoveReplacement") {
    eve::editor::Editor                          host;
    eve::animation_editor::AnimationEditorModule module;
    eve::animation::AnimSkeleton                 skeleton;
    skeleton.addBone("root");
    auto old = std::make_unique<ControlRigEditor>("rig");
    REQUIRE(old->initialize(skeleton).ok());
    REQUIRE(old->enableAutomation().ok());
    auto* coordinator =
        dynamic_cast<eve::editor::EditorTargetCoordinator*>(eve::cap::query<eve::editing::IEditingCommandRegistry>());
    REQUIRE(coordinator != nullptr);
    REQUIRE(coordinator->unregisterTarget(eve::editing::TargetId("rig")).ok());
    REQUIRE(old->enableAutomation().ok());  // Re-register after external removal.
    REQUIRE(coordinator->unregisterTarget(eve::editing::TargetId("rig")).ok());
    ControlRigEditor replacement("rig");
    REQUIRE(replacement.initialize(skeleton).ok());
    REQUIRE(replacement.enableAutomation().ok());
    CHECK(!old->enableAutomation().ok());
    old.reset();
    auto* api = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(api != nullptr);
    CHECK(api->invoke("execute", request(changed(replacement, 2))).find("applied") != std::string::npos);
    CHECK(x(replacement) == 2);
}

TEST_CASE("editor.controlRig.automation.nestedSchemaUsesSharedProjection") {
    eve::editing::PropertySchema schema;
    schema.typeId         = "rig-document";
    schema.documentSchema = eve::editing::Value::Object{{"type", "object"}, {"additionalProperties", false}};
    auto projected        = eve::editing::propertySchemaToJsonSchema(schema, "rig-document");
    REQUIRE(projected.ok());
    CHECK(eve::editing::editorValueToJson(projected.value()) == eve::editing::editorValueToJson(schema.documentSchema));
    eve::editing::PropertyDescriptor property;
    property.path = eve::editing::PropertyPath("conflicting");
    schema.properties.push_back(property);
    CHECK(!eve::editing::propertySchemaToJsonSchema(schema, "rig-document").ok());
    schema.properties.clear();
    schema.documentSchema = 1;
    CHECK(!eve::editing::propertySchemaToJsonSchema(schema, "rig-document").ok());
}
