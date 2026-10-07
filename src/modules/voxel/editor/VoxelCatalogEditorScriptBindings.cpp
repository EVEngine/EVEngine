#include "voxel/editor/VoxelCatalogEditorScriptBindings.h"

#include "editor/EditorWorkspace.h"
#include "voxel/editor/VoxelCatalogEditor.h"
#include "voxel/editor/VoxelEditorModule.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>
#include "editor/EditorScriptProjection.h"

namespace eve::voxel_editor {
namespace {

constexpr const char* kBindingSource = "editor.voxel.sculpt.squirrel";

class ScriptVoxelCatalogEditor {
public:
    explicit ScriptVoxelCatalogEditor(std::string targetId) : editor_(std::move(targetId)) {}

    VoxelCatalogEditor&       editor() noexcept { return editor_; }
    const VoxelCatalogEditor& editor() const noexcept { return editor_; }

private:
    VoxelCatalogEditor editor_;
};

}  // namespace

void exposeVoxelCatalogEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass) {
    const editor::ScriptBind bind{table.getHandle(), kBindingSource};
    auto voxelEditor = editor::addScriptClass<ScriptVoxelCatalogEditor>(table, "VoxelCatalogEditor");

    voxelEditor.addFunc("configureWorkspace",
                        [bind](ScriptVoxelCatalogEditor* self, editor::EditorWorkspace* workspace) {
                            return bind.checked(
                                self && workspace, "voxel editor and workspace must not be null",
                                [&] { return self->editor().configureWorkspace(*workspace); }, "workspace");
                        });
    voxelEditor.addFunc("selectModel", [bind](ScriptVoxelCatalogEditor* self, const std::string& id) {
        return bind.checked(self, "voxel editor must not be null", [&] { return self->editor().selectModel(id); });
    });
    voxelEditor.addFunc("setTool", [bind](ScriptVoxelCatalogEditor* self, const std::string& tool) {
        return bind.checked(self, "voxel editor must not be null", [&] { return self->editor().setTool(tool); });
    });
    voxelEditor.addFunc("setViewport", [bind](ScriptVoxelCatalogEditor* self, float width, float height) {
        return bind.checked(self, "voxel editor must not be null",
                            [&] { return self->editor().setViewport(width, height); });
    });
    voxelEditor.addFunc("orbit", [bind](ScriptVoxelCatalogEditor* self, float yaw, float pitch) {
        return bind.checked(self, "voxel editor must not be null", [&] { return self->editor().orbit(yaw, pitch); });
    });
    voxelEditor.addFunc("pointerDown", [bind](ScriptVoxelCatalogEditor* self, float x, float y) {
        return bind.checked(self, "voxel editor must not be null", [&] { return self->editor().pointerDown(x, y); });
    });
    voxelEditor.addFunc("pointerWorldRay", [bind](ScriptVoxelCatalogEditor* self, float ox, float oy, float oz,
                                                  float dx, float dy, float dz) {
        return bind.checked(self, "voxel editor must not be null",
                            [&] { return self->editor().pointerWorldRay(ox, oy, oz, dx, dy, dz); });
    });
    voxelEditor.addFunc("setVoxel", [bind](ScriptVoxelCatalogEditor* self, int x, int y, int z, bool occupied) {
        return bind.checked(self, "voxel editor must not be null",
                            [&] { return self->editor().setVoxel(x, y, z, occupied); });
    });
    voxelEditor.addFunc("setSelectedSocket",
                        [bind](ScriptVoxelCatalogEditor* self, const std::string& tag, const std::string& kind) {
                            return bind.checked(self, "voxel editor must not be null",
                                                [&] { return self->editor().setSelectedSocket(tag, kind); });
                        });
    voxelEditor.addFunc("selectFace", [bind](ScriptVoxelCatalogEditor* self, int face) {
        return bind.checked(self, "voxel editor must not be null", [&] { return self->editor().selectFace(face); });
    });
    voxelEditor.addFunc("createModel", [bind](ScriptVoxelCatalogEditor* self, const std::string& id,
                                              const std::string& name, int sx, int sy, int sz) {
        return bind.checked(self, "voxel editor must not be null",
                            [&] { return self->editor().createModel(id, name, sx, sy, sz); });
    });
    voxelEditor.addFunc("deleteSelectedModel", [bind](ScriptVoxelCatalogEditor* self) {
        return bind.checked(self, "voxel editor must not be null",
                            [&] { return self->editor().deleteSelectedModel(); });
    });
    voxelEditor.addFunc("undo", [bind](ScriptVoxelCatalogEditor* self) {
        return bind.history(self, "voxel editor must not be null", [&] { return self->editor().undo(); });
    });
    voxelEditor.addFunc("redo", [bind](ScriptVoxelCatalogEditor* self) {
        return bind.history(self, "voxel editor must not be null", [&] { return self->editor().redo(); });
    });
    voxelEditor.addFunc("canUndo", [](ScriptVoxelCatalogEditor* self) { return self && self->editor().canUndo(); });
    voxelEditor.addFunc("canRedo", [](ScriptVoxelCatalogEditor* self) { return self && self->editor().canRedo(); });
    voxelEditor.addFunc("getRevision", [](ScriptVoxelCatalogEditor* self) {
        return self ? static_cast<int>(self->editor().revision()) : 0;
    });
    voxelEditor.addFunc("getSelectedId", [](ScriptVoxelCatalogEditor* self) {
        return self ? self->editor().selectedId() : std::string{};
    });
    voxelEditor.addFunc("getTool", [](ScriptVoxelCatalogEditor* self) {
        return self ? self->editor().toolName() : std::string{};
    });
    voxelEditor.addFunc("getSelectedFace", [](ScriptVoxelCatalogEditor* self) {
        return self ? self->editor().selectedFace() : 0;
    });
    voxelEditor.addFunc("getModelCount", [](ScriptVoxelCatalogEditor* self) {
        return self ? self->editor().modelCount() : 0;
    });
    voxelEditor.addFunc("getModelId", [](ScriptVoxelCatalogEditor* self, int index) {
        return self ? self->editor().modelId(index) : std::string{};
    });
    voxelEditor.addFunc("getModelName", [](ScriptVoxelCatalogEditor* self, int index) {
        return self ? self->editor().modelName(index) : std::string{};
    });
    voxelEditor.addFunc("getModelFill", [](ScriptVoxelCatalogEditor* self, int index) {
        return self ? self->editor().modelFill(index) : std::string{};
    });
    voxelEditor.addFunc("getModelSelected", [](ScriptVoxelCatalogEditor* self, int index) {
        return self && self->editor().isModelSelected(index);
    });
    voxelEditor.addFunc("getVoxelCount", [](ScriptVoxelCatalogEditor* self) {
        return self ? self->editor().voxelCount() : 0;
    });
    voxelEditor.addFunc("getVoxelX", [](ScriptVoxelCatalogEditor* self, int index) {
        return self ? self->editor().voxelX(index) : 0;
    });
    voxelEditor.addFunc("getVoxelY", [](ScriptVoxelCatalogEditor* self, int index) {
        return self ? self->editor().voxelY(index) : 0;
    });
    voxelEditor.addFunc("getVoxelZ", [](ScriptVoxelCatalogEditor* self, int index) {
        return self ? self->editor().voxelZ(index) : 0;
    });
    voxelEditor.addFunc("getModelSizeX", [](ScriptVoxelCatalogEditor* self) {
        return self ? self->editor().modelSizeX() : 0;
    });
    voxelEditor.addFunc("getModelSizeY", [](ScriptVoxelCatalogEditor* self) {
        return self ? self->editor().modelSizeY() : 0;
    });
    voxelEditor.addFunc("getModelSizeZ", [](ScriptVoxelCatalogEditor* self) {
        return self ? self->editor().modelSizeZ() : 0;
    });
    voxelEditor.addFunc("getScreenVoxelCount", [](ScriptVoxelCatalogEditor* self) {
        return self ? self->editor().screenVoxelCount() : 0;
    });
    voxelEditor.addFunc("getScreenVoxelX", [](ScriptVoxelCatalogEditor* self, int index) {
        return self ? self->editor().screenVoxelX(index) : 0.0f;
    });
    voxelEditor.addFunc("getScreenVoxelY", [](ScriptVoxelCatalogEditor* self, int index) {
        return self ? self->editor().screenVoxelY(index) : 0.0f;
    });
    voxelEditor.addFunc("getScreenVoxelW", [](ScriptVoxelCatalogEditor* self, int index) {
        return self ? self->editor().screenVoxelW(index) : 0.0f;
    });
    voxelEditor.addFunc("getScreenVoxelH", [](ScriptVoxelCatalogEditor* self, int index) {
        return self ? self->editor().screenVoxelH(index) : 0.0f;
    });
    voxelEditor.addFunc("getSelectedSocketTag", [](ScriptVoxelCatalogEditor* self) {
        return self ? self->editor().selectedSocketTag() : std::string{};
    });
    voxelEditor.addFunc("getSelectedSocketKind", [](ScriptVoxelCatalogEditor* self) {
        return self ? self->editor().selectedSocketKind() : std::string{};
    });
    voxelEditor.addFunc("getJoinPartnerCount", [](ScriptVoxelCatalogEditor* self) {
        return self ? self->editor().joinPartnerCount() : 0;
    });
    voxelEditor.addFunc("getJoinPartnerId", [](ScriptVoxelCatalogEditor* self, int index) {
        return self ? self->editor().joinPartnerId(index) : std::string{};
    });

    moduleClass.addFunc("create", [bind](VoxelEditorModule*, const std::string& targetId) {
        return bind.ownedCreate<ScriptVoxelCatalogEditor>("voxel target id must not be empty", targetId);
    });
}

}  // namespace eve::voxel_editor
