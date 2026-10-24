#include "avatar/editor/AvatarDocumentEditor.h"
#include "editor/EditorWorkspace.h"

#include <map>
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using eve::avatar_editor::AvatarDocumentEditor;
using eve::editor::EditorWorkspace;

TEST_CASE("editor.avatar.document_editor_installs_workspace_and_reverses_layer_z") {
    AvatarDocumentEditor editor("preview.face");
    EditorWorkspace      workspace("avatar.preview", "Avatar Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    REQUIRE_EQ(workspace.getPanelCount(), 5);
    const std::map<std::string, std::string> expected = {{"avatar.layers", "list"},
                                                         {"avatar.preview", "preview"},
                                                         {"avatar.inspector", "inspector"},
                                                         {"avatar.parameters", "parameters"},
                                                         {"avatar.expressions", "expressions"}};
    for (int i = 0; i < workspace.getPanelCount(); ++i) {
        REQUIRE(expected.contains(workspace.getPanelId(i)));
        REQUIRE_EQ(workspace.getPanelCapability(i), std::string("avatar.document"));
        REQUIRE_EQ(workspace.getPanelContext(i), expected.at(workspace.getPanelId(i)));
    }
    REQUIRE_EQ(editor.layerCount(), 2);
    REQUIRE_EQ(editor.parameterCount(), 1);
    REQUIRE_EQ(editor.expressionCount(), 1);
    REQUIRE_EQ(editor.previewRevision(), editor.revision());
    REQUIRE(editor.previewLayerCount() >= 2);

    REQUIRE(editor.selectLayer("eyes").ok());
    const int before = editor.layerZ(1);
    REQUIRE(editor.setLayerZ(4).ok());
    REQUIRE_EQ(editor.layerZ(1), 4);
    REQUIRE(editor.undo().ok());
    REQUIRE_EQ(editor.layerZ(1), before);
}

TEST_CASE("editor.avatar.document_editor_rejects_referenced_parameter_delete") {
    AvatarDocumentEditor editor("preview.face");
    REQUIRE(editor.selectParameter("smile").ok());
    const auto previewY     = editor.previewY(1);
    const auto layers       = editor.layerCount();
    const auto revision     = editor.revision();
    const auto previewRev   = editor.previewRevision();
    auto       rejected     = editor.deleteSelectedParameter();
    REQUIRE(!rejected.ok());
    REQUIRE_EQ(editor.layerCount(), layers);
    REQUIRE_EQ(editor.parameterCount(), 1);
    REQUIRE_EQ(editor.revision(), revision);
    REQUIRE_EQ(editor.previewRevision(), previewRev);
    REQUIRE_EQ(editor.previewY(1), previewY);

    REQUIRE(editor.setParameterValue(0.9).ok());
    REQUIRE(editor.previewY(1) > previewY);
    REQUIRE_EQ(editor.previewRevision(), editor.revision());
}
