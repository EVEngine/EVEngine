#include "editing/EditingValueJson.h"
#include "graphics/material/editor/MaterialEditor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>
#include <stdexcept>
#include <type_traits>

using eve::editing::Value;
using eve::material_editor::MaterialEditor;

namespace {
Value property(const MaterialEditor& editor, const std::string& path) {
    auto read = editor.read(path);
    REQUIRE(read.ok());
    return read.value();
}
}  // namespace

TEST_CASE("Material editor document and transaction contract") {
    SUB_CASE("material editor contract: a valid identity owns detached defaults and schema") {
        static_assert(!std::is_copy_constructible_v<MaterialEditor>);
        static_assert(!std::is_move_constructible_v<MaterialEditor>);
        bool rejectedEmptyId = false;
        try {
            MaterialEditor invalid("");
        } catch (const std::invalid_argument&) {
            rejectedEmptyId = true;
        }
        REQUIRE(rejectedEmptyId);

        MaterialEditor editor("material-a");
        REQUIRE_EQ(property(editor, "shading.model"), Value("pbr"));
        REQUIRE_EQ(property(editor, "shading.metallic"), Value(0.0));
        REQUIRE(!editor.read("unknown.path").ok());
        REQUIRE(!editor.canUndo());
        REQUIRE(!editor.canRedo());

        const auto  metadata = editor.schema();
        const auto* root     = metadata.getIf<Value::Object>();
        REQUIRE(root != nullptr);
        const auto* fields = root->at("properties").getIf<Value::Array>();
        REQUIRE(fields != nullptr);
        REQUIRE(fields->size() > 50u);
        auto detached = editor.snapshot();
        detached.getIf<Value::Object>()->clear();
        REQUIRE(editor.snapshot().getIf<Value::Object>()->contains("properties"));
    };

    SUB_CASE("material editor contract: one batch publishes one revision and one undo record") {
        MaterialEditor editor("material-b");
        const auto     initial  = editor.snapshot();
        const auto     revision = editor.revision();
        REQUIRE(editor
                    .setProperties({{"shading.metallic", 1.0},
                                    {"shading.roughness", .22},
                                    {"shading.tint", Value::Array{1.0, .72, .22, 1.0}}})
                    .ok());
        REQUIRE_EQ(property(editor, "shading.metallic"), Value(1.0));
        REQUIRE_EQ(property(editor, "shading.roughness"), Value(.22));
        REQUIRE(editor.revision() > revision);
        const auto changed = editor.snapshot();
        REQUIRE(editor.canUndo());
        REQUIRE(editor.undo().ok());
        REQUIRE(editor.snapshot() == initial);
        REQUIRE(!editor.canUndo());
        REQUIRE(editor.canRedo());
        REQUIRE(editor.redo().ok());
        REQUIRE(editor.snapshot() == changed);
    };

    SUB_CASE("material editor contract: invalid candidate never changes document or history") {
        MaterialEditor editor("material-c");
        REQUIRE(editor.setProperty("shading.metallic", .7).ok());
        REQUIRE(editor.undo().ok());
        const auto before   = editor.snapshot();
        const auto revision = editor.revision();
        REQUIRE(!editor.setProperties({{"shading.metallic", .5}, {"shading.roughness", -1.0}}).ok());
        REQUIRE(!editor.setProperty("shading.metallic", std::numeric_limits<double>::infinity()).ok());
        REQUIRE(!editor.setProperty("unknown.path", true).ok());
        REQUIRE(editor.snapshot() == before);
        REQUIRE_EQ(editor.revision(), revision);
        REQUIRE(!editor.canUndo());
        REQUIRE(editor.canRedo());
    };

    SUB_CASE("material editor contract: cross-field validation sees all writes in a batch") {
        MaterialEditor editor("material-d");
        const auto     before = editor.snapshot();
        REQUIRE(!editor.setProperty("shading.model", "custom").ok());
        REQUIRE(editor.snapshot() == before);
        REQUIRE(editor.setProperties({{"shading.model", "custom"}, {"textures.shader", "asset://shader/custom"}}).ok());
        REQUIRE_EQ(property(editor, "shading.model"), Value("custom"));
        REQUIRE(!editor.setProperty("textures.shader", "").ok());
        REQUIRE_EQ(property(editor, "textures.shader"), Value("asset://shader/custom"));
        REQUIRE(editor.undo().ok());
        REQUIRE(editor.snapshot() == before);
    };

    SUB_CASE("material editor contract: no-op input preserves redo and revision") {
        MaterialEditor editor("material-e");
        REQUIRE(editor.setProperty("shading.metallic", 1.0).ok());
        REQUIRE(editor.undo().ok());
        const auto revision = editor.revision();
        REQUIRE(editor.setProperty("shading.metallic", 0).ok());
        REQUIRE(editor.setProperties({}).ok());
        REQUIRE_EQ(editor.revision(), revision);
        REQUIRE(editor.canRedo());
        REQUIRE(!editor.canUndo());
    };
}

TEST_CASE("Material editor restore and workspace contract") {
    SUB_CASE("material editor contract: versioned restore is atomic and undoable") {
        MaterialEditor source("source");
        REQUIRE(source.setProperty("shading.metallic", .8).ok());
        auto json    = eve::editing::editorValueToJson(source.snapshot());
        auto decoded = eve::editing::editorValueFromJson(json);
        REQUIRE(decoded.ok());

        MaterialEditor target("target");
        const auto     initial = target.snapshot();
        REQUIRE(target.restore(decoded.value()).ok());
        REQUIRE(target.snapshot() == source.snapshot());
        const auto restored = target.snapshot();
        REQUIRE(!target.restore(Value::Object{{"schemaVersion", 100}, {"properties", Value::Object{}}}).ok());
        REQUIRE(target.snapshot() == restored);
        REQUIRE(target.undo().ok());
        REQUIRE(target.snapshot() == initial);
        REQUIRE(target.redo().ok());
        REQUIRE(target.snapshot() == restored);
        REQUIRE(target.restore(Value::Object{{"schemaVersion", 1}, {"properties", Value::Object{}}}).ok());
        REQUIRE(target.snapshot() == initial);
    };

    SUB_CASE("material editor contract: workspace panels install once as an atomic group") {
        MaterialEditor               editor("material-f");
        eve::editor::EditorWorkspace workspace("material", "Material");
        REQUIRE(editor.configureWorkspace(workspace).ok());
        REQUIRE_EQ(workspace.getPanelCount(), 3);
        REQUIRE(!editor.configureWorkspace(workspace).ok());
        REQUIRE_EQ(workspace.getPanelCount(), 3);
    };
}
