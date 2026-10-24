#include "editing/EditingValueJson.h"
#include "graphics/material/editor/MaterialEditor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>
#include <type_traits>

namespace {
using eve::editing::Value;
using eve::material_editor::MaterialEditor;
static_assert(!std::is_copy_constructible_v<MaterialEditor>);
static_assert(!std::is_move_constructible_v<MaterialEditor>);

Value readMaterial(const MaterialEditor& editor, const std::string& path) {
    auto value = editor.read(path);
    REQUIRE(value.ok());
    return value.value();
}
}  // namespace

TEST_CASE("editor.material.facade_patch_is_one_undo_record") {
    MaterialEditor editor("gold");
    const auto     original = editor.snapshot();
    auto           patched  = editor.setProperties(
        {{"shading.metallic", 1.0}, {"shading.roughness", 0.22}, {"shading.tint", Value::Array{1.0, 0.72, 0.22, 1.0}}});
    REQUIRE(patched.ok());
    const auto gold = editor.snapshot();
    REQUIRE(editor.undo().ok());
    CHECK(editor.snapshot() == original);
    CHECK(!editor.canUndo());
    REQUIRE(editor.redo().ok());
    CHECK(editor.snapshot() == gold);
}

TEST_CASE("editor.material.facade_invalid_patch_preserves_document_and_history") {
    MaterialEditor editor("atomic");
    REQUIRE(editor.setProperty("shading.metallic", 0.7).ok());
    REQUIRE(editor.undo().ok());
    const auto original = editor.snapshot();
    const auto revision = editor.revision();
    auto       invalid  = editor.setProperties({{"shading.metallic", 0.5}, {"shading.roughness", -1.0}});
    CHECK(!invalid.ok());
    CHECK(editor.snapshot() == original);
    CHECK_EQ(editor.revision(), revision);
    CHECK(editor.canRedo());
    CHECK(!editor.canUndo());
    CHECK(!editor.setProperty("shading.metallic", std::numeric_limits<double>::infinity()).ok());
    CHECK(!editor.setProperty("unknown.property", true).ok());
    CHECK(!editor.read("unknown.property").ok());
}

TEST_CASE("editor.material.facade_cross_field_validation_is_candidate_first") {
    MaterialEditor editor("custom");
    CHECK(!editor.setProperty("shading.model", "custom").ok());
    CHECK(readMaterial(editor, "shading.model") == Value("pbr"));
    CHECK(!editor.canUndo());
    REQUIRE(editor.setProperties({{"shading.model", "custom"}, {"textures.shader", "asset://shader/custom"}}).ok());
    CHECK(readMaterial(editor, "shading.model") == Value("custom"));
    CHECK(!editor.setProperty("textures.shader", "").ok());
    REQUIRE(editor.undo().ok());
    CHECK(readMaterial(editor, "shading.model") == Value("pbr"));
}

TEST_CASE("editor.material.facade_snapshot_restore_is_undoable_and_migrates") {
    MaterialEditor source("source");
    REQUIRE(source.setProperty("shading.metallic", 0.8).ok());
    const auto encoded = eve::editing::editorValueToJson(source.snapshot());
    auto       parsed  = eve::editing::editorValueFromJson(encoded);
    REQUIRE(parsed.ok());
    MaterialEditor restored("restored");
    const auto     original = restored.snapshot();
    REQUIRE(restored.restore(parsed.value()).ok());
    CHECK(restored.snapshot() == source.snapshot());
    REQUIRE(restored.undo().ok());
    CHECK(restored.snapshot() == original);
    REQUIRE(restored.redo().ok());
    const auto before = restored.snapshot();
    CHECK(!restored.restore(Value::Object{{"schemaVersion", 100}, {"properties", Value::Object{}}}).ok());
    CHECK(restored.snapshot() == before);
    REQUIRE(restored.restore(Value::Object{{"schemaVersion", 1}, {"properties", Value::Object{}}}).ok());
    CHECK(restored.snapshot() == original);
}

TEST_CASE("editor.material.facade_noop_preserves_redo_and_revision") {
    MaterialEditor editor("noop");
    REQUIRE(editor.setProperty("shading.metallic", 1.0).ok());
    REQUIRE(editor.undo().ok());
    const auto revision = editor.revision();
    REQUIRE(editor.setProperty("shading.metallic", 0).ok());
    REQUIRE(editor.setProperties({}).ok());
    CHECK_EQ(editor.revision(), revision);
    CHECK(editor.canRedo());
    CHECK(!editor.canUndo());
}

TEST_CASE("editor.material.facade_schema_and_workspace_are_detached") {
    MaterialEditor editor("schema");
    const auto     metadata = editor.schema();
    const auto*    root     = metadata.getIf<Value::Object>();
    REQUIRE(root);
    REQUIRE(root->at("properties").getIf<Value::Array>());
    CHECK_GT(root->at("properties").getIf<Value::Array>()->size(), 50U);
    eve::editor::EditorWorkspace workspace("material", "Material");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 3);
    CHECK(!editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 3);
    auto detached = editor.snapshot();
    detached.getIf<Value::Object>()->clear();
    CHECK(editor.snapshot().getIf<Value::Object>()->contains("properties"));
}
