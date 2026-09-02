#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditCommand.h"
#include "editor/Editor.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorSession.h"
#include "editor/EditorTargetCoordinator.h"
#include "editor/EditorTransactions.h"
#include "editor/EditorWorkspace.h"
#include "level_editing/EditorHistory.h"
#include "level_editing/FieldTargets.h"
#include "level_editing/TileBuffer.h"
#include "scene_editor/EditorSceneTarget.h"

using namespace eve::editor;
using namespace eve::level_editing;

TEST_CASE("editor.api.transactions_expose_checked_results") {
    EditorTransactions transactions;

    auto begun = transactions.beginTransaction("checked transaction");
    REQUIRE(begun.ok());

    auto duplicate = transactions.beginTransaction("nested transaction");
    CHECK(!duplicate.ok());

    auto rolledBack = transactions.rollbackTransaction();
    CHECK(rolledBack.ok());

    auto noActiveTransaction = transactions.rollbackTransaction();
    CHECK(!noActiveTransaction.ok());
}

TEST_CASE("editor.api.session_and_history_preserve_failure_status") {
    EditorSession session;
    auto nullCommand = session.executeChecked(nullptr);
    CHECK_EQ(static_cast<int>(nullCommand.code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!nullCommand.diagnostics().empty());

    EditorHistory history;
    auto emptyUndo = history.undoAction();
    CHECK_EQ(static_cast<int>(emptyUndo.code()), static_cast<int>(EditorStatus::NoOp));
    REQUIRE(!emptyUndo.diagnostics().empty());
    CHECK(!history.undo());

    history.push("rename", "payload");
    auto undone = history.undoAction();
    REQUIRE(undone.ok());
    REQUIRE(undone.ok());
    CHECK_EQ(undone.value(), std::string("rename"));
}

TEST_CASE("editor.api.workspace_checked_operations_preserve_diagnostics") {
    EditorWorkspace workspace("checked", "Checked workspace");

    WorkspacePanelDescriptor panel;
    panel.id     = "scene";
    panel.title  = "Scene";
    panel.region = "center";
    auto registered = workspace.registerPanel(panel);
    REQUIRE(registered.ok());
    REQUIRE(registered.ok());

    auto duplicate = workspace.registerPanel(panel);
    CHECK_EQ(static_cast<int>(duplicate.code()), static_cast<int>(EditorStatus::Conflict));
    REQUIRE(!duplicate.diagnostics().empty());

    auto moved = workspace.movePanel(StableId("scene"), WorkspaceRegion::Left, 10);
    REQUIRE(moved.ok());
    REQUIRE(moved.ok());
    CHECK_EQ(moved.value().region, std::string("left"));

    auto unchanged = workspace.movePanel(StableId("scene"), WorkspaceRegion::Left, 10);
    CHECK_EQ(static_cast<int>(unchanged.code()), static_cast<int>(EditorStatus::NoOp));
    CHECK(unchanged.ok());

    auto missing = workspace.panelAt(9);
    CHECK_EQ(static_cast<int>(missing.code()), static_cast<int>(EditorStatus::NotFound));
    REQUIRE(!missing.diagnostics().empty());

    auto activated = workspace.activatePanel(StableId("scene"));
    CHECK(activated.ok());
    auto removed = workspace.removePanel(StableId("scene"));
    REQUIRE(removed.ok());
    auto removedAgain = workspace.removePanel(StableId("scene"));
    CHECK_EQ(static_cast<int>(removedAgain.code()), static_cast<int>(EditorStatus::NotFound));
}

TEST_CASE("editor.api.coordinator_invalidates_bound_target_when_unregistered") {
    SceneDocumentTarget target("scene.lifecycle");
    Editor              editor;
    EditorSession       session;

    REQUIRE(editor.registerEditingTarget(target).ok());
    CHECK(target.capability<eve::editing::IEditingSnapshotProvider>().has_value());
    REQUIRE(editor.bindEditingTarget(session, TargetId("scene.lifecycle")).ok());
    REQUIRE(session.target() == &target);
    REQUIRE(session.context().target() == &target);
    REQUIRE(session.transactions().beginTransaction("pending target gesture").ok());
    REQUIRE(session.transactions().isActive());

    REQUIRE(editor.unregisterEditingTarget(TargetId("scene.lifecycle")).ok());
    CHECK(session.target() == nullptr);
    CHECK(session.context().target() == nullptr);
    CHECK(session.contextSnapshot().target.empty());
    CHECK(!session.transactions().isActive());
    CHECK(!session.transactions().canUndo());
}

TEST_CASE("editor.api.coordinator_unregisters_captured_command_callbacks_before_destruction") {
    EditorCommandService commands;
    {
        EditorTargetCoordinator coordinator(commands);
        eve::editing::EditingCommandDescriptor descriptor;
        descriptor.id = CommandId("test.coordinator.lifecycle");
        descriptor.ownerModule = "test.coordinator";
        auto registered = coordinator.registerPlannedCommand(
            descriptor, [](IEditableTarget&, const CommandRequest&) {
                return eve::editing::applied<CommandPlan>(CommandPlan{});
            });
        REQUIRE(registered.ok());
        CHECK(commands.find(descriptor.id) != nullptr);
    }
    CHECK(commands.find(CommandId("test.coordinator.lifecycle")) == nullptr);
}

TEST_CASE("editor.api.session_execute_checked_forwards_transaction_diagnostics") {
    TileBuffer       buffer(1, 1);
    TileBufferTarget target("tiles", &buffer);
    EditorSession    session;
    session.bindTarget(&target);

    auto command = std::make_unique<IntFieldEditCommand>("set", &target);
    REQUIRE(command->record(0, 0, 3));
    auto missingTransaction = session.executeChecked(std::move(command));
    CHECK_EQ(static_cast<int>(missingTransaction.code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!missingTransaction.diagnostics().empty());
    CHECK_EQ(eve::editing::diagnosticRule(missingTransaction.diagnostics().front()).value(),
             std::string("editor.transaction"));
    CHECK_EQ(buffer.getGid(0, 0), 0);
}

TEST_CASE("editor.api.session_execute_command_reports_empty_transaction_commit") {
    EditorCommandService service;
    CommandDescriptor        descriptor;
    descriptor.id          = CommandId("test.noop");
    descriptor.ownerModule = "test";
    descriptor.displayName = "Noop";
    REQUIRE(service
                .registerCommand(descriptor,
                                 [](const CommandContext&, const EditorValue& payload) {
                                     return eve::editing::applied<EditorValue>(payload);
                                 })
                .ok());

    EditorSession session;
    HostProfile   profile = HostProfile::developer();
    profile.allowCommand(descriptor.id);
    session.setHostProfile(std::move(profile));
    session.setCommandService(&service);

    auto result = session.executeCommand(descriptor.id, EditorValue(1));
    CHECK(!result.ok());
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!result.diagnostics().empty());
    CHECK(!session.transactions().isActive());
}
