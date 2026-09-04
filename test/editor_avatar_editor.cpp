#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "avatar_editor/AvatarEditorModule.h"
#include "avatar_editor/EditorAvatarTarget.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"

using eve::avatar_editing::AvatarDocumentTarget;
using eve::avatar_editing::AvatarLayerValue;
using eve::editing::DomainOperation;
using eve::editing::ObjectId;
using eve::editing::TargetId;
using eve::editor::Editor;

namespace {
void apply(AvatarDocumentTarget& target, eve::editing::Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
}  // namespace

TEST_CASE("editor.avatar.automation_edits_layers_through_the_same_transaction_path") {
    AvatarDocumentTarget target("hero");
    AvatarLayerValue     layer;
    layer.id           = ObjectId("eyes");
    layer.name         = "eyes";
    layer.textureAsset = "eyes.png";
    apply(target, target.makeCreateLayer(layer));

    Editor                               editor;
    eve::avatar_editor::AvatarEditorModule adapter;
    REQUIRE(editor.registerEditingTarget(target).ok());
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("avatar.property.set.v1") != std::string::npos);
    CHECK(commands.find("avatar.layer.create.v1") != std::string::npos);

    const std::string changed = automation->invoke(
        "execute",
        R"({"target":"hero","command":"avatar.property.set.v1","payload":{"item":"eyes","type":"avatar.layer","path":"layer.z","value":7}})");
    CHECK(changed.find("\"status\":\"applied\"") != std::string::npos);
    CHECK_EQ(target.layers().front().zIndex, 7);

    const std::string undone = automation->invoke("undo", R"({"target":"hero"})");
    CHECK(undone.find("\"status\":\"applied\"") != std::string::npos);
    CHECK_EQ(target.layers().front().zIndex, 0);

    CHECK(editor.unregisterEditingTarget(TargetId("hero")).ok());
}

TEST_CASE("editor.avatar.automation_owns_headless_targets_for_agent_lifecycle") {
    Editor                                 editor;
    eve::avatar_editor::AvatarEditorModule adapter;
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string created = automation->invoke(
        "target-create", R"({"target":"agent.avatar","type":"avatar","layer":"eyes","parameter":"mouth"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.avatar"})");
    CHECK(inspected.find("\"id\":\"agent.avatar\"") != std::string::npos);
    CHECK(inspected.find("avatar-asset") != std::string::npos);

    const std::string changed = automation->invoke(
        "execute",
        R"({"target":"agent.avatar","command":"avatar.property.set.v1","payload":{"item":"mouth","type":"avatar.parameter","path":"parameter.value","value":0.75}})");
    CHECK(changed.find("\"status\":\"applied\"") != std::string::npos);

    REQUIRE(automation->invoke("target-close", R"({"target":"agent.avatar"})")
                .find("\"status\":\"applied\"") != std::string::npos);
}
