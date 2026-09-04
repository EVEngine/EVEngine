#include "avatar_editor/AvatarDocumentEditor.h"
#include "editor/EditorWorkspace.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using eve::avatar_editor::AvatarDocumentEditor;
using eve::editor::EditorWorkspace;

TEST_CASE("editor.avatar.document_editor_installs_workspace_and_reverses_layer_z") {
    AvatarDocumentEditor editor("preview.face");
    EditorWorkspace      workspace("avatar.preview", "Avatar Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 5);
    CHECK_EQ(workspace.getPanelId(0), std::string("avatar.layers"));
    CHECK_EQ(workspace.getPanelCapability(0), std::string("avatar.document"));
    CHECK_EQ(workspace.getPanelContext(0), std::string("list"));
    CHECK_EQ(workspace.getPanelContext(1), std::string("preview"));
    CHECK_EQ(workspace.getPanelContext(2), std::string("inspector"));
    CHECK_EQ(workspace.getPanelContext(3), std::string("parameters"));
    CHECK_EQ(workspace.getPanelContext(4), std::string("expressions"));
    CHECK_EQ(editor.layerCount(), 2);
    CHECK_EQ(editor.parameterCount(), 1);
    CHECK_EQ(editor.expressionCount(), 1);
    CHECK_EQ(editor.previewRevision(), editor.revision());
    CHECK(editor.previewLayerCount() >= 2);

    REQUIRE(editor.selectLayer("eyes").ok());
    const int before = editor.layerZ(1);
    REQUIRE(editor.setLayerZ(4).ok());
    CHECK_EQ(editor.layerZ(1), 4);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.layerZ(1), before);
}

TEST_CASE("editor.avatar.document_editor_rejects_referenced_parameter_delete") {
    AvatarDocumentEditor editor("preview.face");
    REQUIRE(editor.selectParameter("smile").ok());
    const auto previewY     = editor.previewY(1);
    const auto layers       = editor.layerCount();
    const auto revision     = editor.revision();
    const auto previewRev   = editor.previewRevision();
    auto       rejected     = editor.deleteSelectedParameter();
    CHECK(!rejected.ok());
    CHECK_EQ(editor.layerCount(), layers);
    CHECK_EQ(editor.parameterCount(), 1);
    CHECK_EQ(editor.revision(), revision);
    CHECK_EQ(editor.previewRevision(), previewRev);
    CHECK_EQ(editor.previewY(1), previewY);

    REQUIRE(editor.setParameterValue(0.9).ok());
    CHECK(editor.previewY(1) > previewY);
    CHECK_EQ(editor.previewRevision(), editor.revision());
}
