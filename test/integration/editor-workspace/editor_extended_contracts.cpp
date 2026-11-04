// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "editor",
  "modules": [
    "editor",
    "audio_editor",
    "animation_editor",
    "avatar_editor",
    "voxel_editor"
  ]
}
*/
// clang-format on
#include <array>
#include <tuple>
#include <vector>
#include "avatar/editor/AvatarDocumentEditor.h"
#include "voxel/editor/VoxelCatalogEditor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
auto previewOf(const eve::avatar_editor::AvatarDocumentEditor& editor) {
    using Row = std::tuple<std::string, float, float, float, float, float, float, float, float, bool>;
    std::vector<Row> rows;
    for (int i = 0; i < editor.previewLayerCount(); ++i)
        rows.emplace_back(editor.previewName(i), editor.previewX(i), editor.previewY(i), editor.previewW(i),
                          editor.previewH(i), editor.previewR(i), editor.previewG(i), editor.previewB(i),
                          editor.previewA(i), editor.isPreviewSelected(i));
    return rows;
}
auto previewOf(const eve::voxel_editor::VoxelCatalogEditor& editor) {
    std::vector<std::array<float, 4>> rows;
    for (int i = 0; i < editor.screenVoxelCount(); ++i)
        rows.push_back(
            {editor.screenVoxelX(i), editor.screenVoxelY(i), editor.screenVoxelW(i), editor.screenVoxelH(i)});
    return rows;
}
template <class Editor, class Edit>
void historyContract(Editor& first, Editor& second, Edit edit, zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    const auto original        = first.target().snapshotValue();
    const auto other           = second.target().snapshotValue();
    const auto otherRevision   = second.revision();
    const auto originalPreview = previewOf(first);
    const auto otherPreview    = previewOf(second);
    REQUIRE(edit(first, 1).ok());
    const auto changed = first.target().snapshotValue();
    REQUIRE(changed != original);
    REQUIRE_EQ(first.previewRevision(), first.revision());
    REQUIRE(first.canUndo());
    REQUIRE(!second.canUndo());
    REQUIRE(first.undo().ok());
    REQUIRE(first.target().snapshotValue() == original);
    REQUIRE(previewOf(first) == originalPreview);
    REQUIRE_EQ(first.previewRevision(), first.revision());
    REQUIRE(first.canRedo());
    REQUIRE(first.redo().ok());
    REQUIRE(first.target().snapshotValue() == changed);
    REQUIRE(first.undo().ok());
    REQUIRE(edit(first, 2).ok());
    REQUIRE(!first.canRedo());
    REQUIRE(first.target().snapshotValue() != changed);
    REQUIRE(second.target().snapshotValue() == other);
    REQUIRE(previewOf(second) == otherPreview);
    REQUIRE_EQ(second.revision(), otherRevision);
    REQUIRE_EQ(second.previewRevision(), otherRevision);
    REQUIRE(!second.canUndo());
    REQUIRE(!second.canRedo());
}
}  // namespace
TEST_CASE("editor.contract.avatar_history_preview_and_instance_isolation") {
    eve::avatar_editor::AvatarDocumentEditor first("contract.avatar.first"), second("contract.avatar.second");
    REQUIRE(first.selectLayer("eyes").ok());
    historyContract(
        first, second, [](auto& editor, int value) { return editor.setLayerZ(10 + value); }, _ZEROERR_TEST_CONTEXT);
}
TEST_CASE("editor.contract.voxel_history_preview_and_instance_isolation") {
    eve::voxel_editor::VoxelCatalogEditor first("contract.voxel.first"), second("contract.voxel.second");
    historyContract(
        first, second, [](auto& editor, int value) { return editor.setVoxel(0, value, 0, true); },
        _ZEROERR_TEST_CONTEXT);
}
TEST_CASE("editor.contract.avatar_rejection_preserves_redo_document_and_preview") {
    eve::avatar_editor::AvatarDocumentEditor editor("contract.avatar.reject");
    REQUIRE(editor.selectParameter("smile").ok());
    REQUIRE(editor.setParameterValue(0.8).ok());
    REQUIRE(editor.undo().ok());
    const auto document = editor.target().snapshotValue();
    const auto preview  = previewOf(editor);
    const auto revision = editor.revision();
    const auto count    = editor.previewLayerCount();
    const auto selected = editor.selectedId();
    REQUIRE(editor.canRedo());
    REQUIRE(!editor.deleteSelectedParameter().ok());
    REQUIRE(!editor.selectLayer("missing.layer").ok());
    REQUIRE(editor.target().snapshotValue() == document);
    REQUIRE(previewOf(editor) == preview);
    REQUIRE_EQ(editor.revision(), revision);
    REQUIRE_EQ(editor.previewRevision(), revision);
    REQUIRE_EQ(editor.previewLayerCount(), count);
    REQUIRE_EQ(editor.selectedId(), selected);
    REQUIRE(editor.canRedo());
    REQUIRE(editor.redo().ok());
    REQUIRE_EQ(editor.previewRevision(), editor.revision());
}
TEST_CASE("editor.contract.voxel_rejection_preserves_redo_document_and_preview") {
    eve::voxel_editor::VoxelCatalogEditor editor("contract.voxel.reject");
    REQUIRE(editor.setVoxel(0, 0, 0, true).ok());
    REQUIRE(editor.undo().ok());
    const auto document = editor.target().snapshotValue();
    const auto preview  = previewOf(editor);
    const auto revision = editor.revision();
    const auto count    = editor.screenVoxelCount();
    const auto selected = editor.selectedId();
    REQUIRE(editor.canRedo());
    REQUIRE(!editor.setVoxel(-1, 0, 0, true).ok());
    REQUIRE(!editor.selectModel("missing.model").ok());
    REQUIRE(editor.target().snapshotValue() == document);
    REQUIRE(previewOf(editor) == preview);
    REQUIRE_EQ(editor.revision(), revision);
    REQUIRE_EQ(editor.previewRevision(), revision);
    REQUIRE_EQ(editor.screenVoxelCount(), count);
    REQUIRE_EQ(editor.selectedId(), selected);
    REQUIRE(editor.canRedo());
    REQUIRE(editor.redo().ok());
    REQUIRE_EQ(editor.previewRevision(), editor.revision());
}

namespace {
template <class Editor>
void workspaceConflict(const char* conflictId, zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    Editor                                editor("contract.workspace");
    eve::editor::EditorWorkspace          workspace("contract.workspace", "Existing workspace");
    eve::editor::WorkspacePanelDescriptor sentinel;
    sentinel.id         = conflictId;
    sentinel.title      = "Existing provider";
    sentinel.region     = "floating";
    sentinel.capability = "custom.provider";
    sentinel.context    = "custom.context";
    sentinel.order      = 42;
    REQUIRE(workspace.registerPanel(sentinel).ok());
    REQUIRE(workspace.activatePanel(eve::editor::StableId(conflictId)).ok());
    const auto revision = workspace.getRevision();
    REQUIRE(!editor.configureWorkspace(workspace).ok());
    REQUIRE_EQ(workspace.getRevision(), revision);
    REQUIRE_EQ(workspace.getPanelCount(), 1);
    REQUIRE_EQ(workspace.getActivePanel(), std::string(conflictId));
    auto retained = workspace.panelAt(0);
    REQUIRE(retained.ok());
    REQUIRE_EQ(retained.value().id, sentinel.id);
    REQUIRE_EQ(retained.value().title, sentinel.title);
    REQUIRE_EQ(retained.value().region, sentinel.region);
    REQUIRE_EQ(retained.value().capability, sentinel.capability);
    REQUIRE_EQ(retained.value().context, sentinel.context);
    REQUIRE_EQ(retained.value().order, sentinel.order);
    REQUIRE_EQ(retained.value().visible, sentinel.visible);
    REQUIRE_EQ(retained.value().singleton, sentinel.singleton);
    REQUIRE(!editor.canUndo());
}
}  // namespace
TEST_CASE("editor.contract.avatar_workspace_conflict_is_atomic") {
    workspaceConflict<eve::avatar_editor::AvatarDocumentEditor>("avatar.expressions", _ZEROERR_TEST_CONTEXT);
}
TEST_CASE("editor.contract.voxel_workspace_conflict_is_atomic") {
    workspaceConflict<eve::voxel_editor::VoxelCatalogEditor>("voxel.inspector", _ZEROERR_TEST_CONTEXT);
}
