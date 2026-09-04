#include "action_editor/ActionEditorModule.h"
#include "action_editor/ActionTimelineEditor.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
    auto operation = target.makeSet(selection, PropertyPath("timeline.animationUri"),
                                    EditorValue("asset://animations/light-attack.eva"), PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(target.timeline().animationUri, std::string("asset://animations/light-attack.eva"));
    CHECK_EQ(target.revision(), 1u);

    auto snapshot = target.snapshotValue();
    const auto* root = snapshot.getIf<EditorValue::Object>();
    REQUIRE(root != nullptr);
    CHECK_EQ(root->at("schema").getIf<std::string>() ?
                 *root->at("schema").getIf<std::string>() :
                 std::string{},
             std::string("eve.action.timeline"));
}

TEST_CASE("actionEditor.module_registers_automation_target_and_property_command") {
    Editor                                   editor;
    eve::action_editor::ActionEditorModule   adapter;
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
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
