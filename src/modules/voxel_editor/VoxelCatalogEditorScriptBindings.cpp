#include "voxel_editor/VoxelCatalogEditorScriptBindings.h"

#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "editor/EditorWorkspace.h"
#include "voxel_editor/VoxelCatalogEditor.h"
#include "voxel_editor/VoxelEditorModule.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace eve::voxel_editor {
namespace {

constexpr const char* kBindingSource = "editor.voxel.sculpt.squirrel";

Status statusFrom(const voxel_editing::EditorResult<void>& result) { return result.status(); }

template <class T>
Status statusFrom(const voxel_editing::EditorResult<T>& result) {
    return result.status();
}

ssq::Table project(HSQUIRRELVM vm, const voxel_editing::EditorResult<void>& result) {
    return script::projectStatusResult(vm, statusFrom(result), result.ok(), false);
}

template <class T>
ssq::Table project(HSQUIRRELVM vm, const voxel_editing::EditorResult<T>& result, Value value) {
    const bool hasValue = result.ok();
    return script::projectStatusResult(vm, statusFrom(result), hasValue, hasValue, value);
}

ssq::Table bindingFailure(HSQUIRRELVM vm, DiagnosticCode code, std::string message, std::string path = {}) {
    return script::projectStatusResult(
        vm, Status::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, kBindingSource)), false,
        false);
}

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
    const HSQUIRRELVM vm = table.getHandle();
    auto voxelEditor     = table.addClass<ScriptVoxelCatalogEditor>(
        "VoxelCatalogEditor",
        std::function<ScriptVoxelCatalogEditor*()>([]() -> ScriptVoxelCatalogEditor* { return nullptr; }), true);

    voxelEditor.addFunc("configureWorkspace",
                        [vm](ScriptVoxelCatalogEditor* self, editor::EditorWorkspace* workspace) {
                            if (!self || !workspace)
                                return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                      "voxel editor and workspace must not be null", "workspace");
                            return project(vm, self->editor().configureWorkspace(*workspace));
                        });
    voxelEditor.addFunc("selectModel", [vm](ScriptVoxelCatalogEditor* self, const std::string& id) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "voxel editor must not be null");
        return project(vm, self->editor().selectModel(id));
    });
    voxelEditor.addFunc("setTool", [vm](ScriptVoxelCatalogEditor* self, const std::string& tool) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "voxel editor must not be null");
        return project(vm, self->editor().setTool(tool));
    });
    voxelEditor.addFunc("setViewport", [vm](ScriptVoxelCatalogEditor* self, float width, float height) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "voxel editor must not be null");
        return project(vm, self->editor().setViewport(width, height));
    });
    voxelEditor.addFunc("orbit", [vm](ScriptVoxelCatalogEditor* self, float yaw, float pitch) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "voxel editor must not be null");
        return project(vm, self->editor().orbit(yaw, pitch));
    });
    voxelEditor.addFunc("pointerDown", [vm](ScriptVoxelCatalogEditor* self, float x, float y) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "voxel editor must not be null");
        return project(vm, self->editor().pointerDown(x, y));
    });
    voxelEditor.addFunc("pointerWorldRay", [vm](ScriptVoxelCatalogEditor* self, float ox, float oy, float oz, float dx,
                                                float dy, float dz) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "voxel editor must not be null");
        return project(vm, self->editor().pointerWorldRay(ox, oy, oz, dx, dy, dz));
    });
    voxelEditor.addFunc("setVoxel", [vm](ScriptVoxelCatalogEditor* self, int x, int y, int z, bool occupied) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "voxel editor must not be null");
        return project(vm, self->editor().setVoxel(x, y, z, occupied));
    });
    voxelEditor.addFunc("setSelectedSocket",
                        [vm](ScriptVoxelCatalogEditor* self, const std::string& tag, const std::string& kind) {
                            if (!self)
                                return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                      "voxel editor must not be null");
                            return project(vm, self->editor().setSelectedSocket(tag, kind));
                        });
    voxelEditor.addFunc("selectFace", [vm](ScriptVoxelCatalogEditor* self, int face) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "voxel editor must not be null");
        return project(vm, self->editor().selectFace(face));
    });
    voxelEditor.addFunc("createModel", [vm](ScriptVoxelCatalogEditor* self, const std::string& id,
                                            const std::string& name, int sx, int sy, int sz) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "voxel editor must not be null");
        return project(vm, self->editor().createModel(id, name, sx, sy, sz));
    });
    voxelEditor.addFunc("deleteSelectedModel", [vm](ScriptVoxelCatalogEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "voxel editor must not be null");
        return project(vm, self->editor().deleteSelectedModel());
    });
    voxelEditor.addFunc("undo", [vm](ScriptVoxelCatalogEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "voxel editor must not be null");
        auto result = self->editor().undo();
        return project(vm, result, Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    voxelEditor.addFunc("redo", [vm](ScriptVoxelCatalogEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "voxel editor must not be null");
        auto result = self->editor().redo();
        return project(vm, result, Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
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

    moduleClass.addFunc("create", [vm](VoxelEditorModule*, const std::string& targetId) {
        if (targetId.empty())
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "voxel target id must not be empty", "targetId");
        auto object = script::makeOwnedSquirrelInstance<ScriptVoxelCatalogEditor>(
            vm, std::make_unique<ScriptVoxelCatalogEditor>(targetId));
        if (!object) return script::projectStatusResult(vm, object.status(), false, false);
        ssq::Object owned = std::move(object).takeValue();
        auto        result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", owned);
        result.set("ownership", std::string("owned"));
        return result;
    });
}

}  // namespace eve::voxel_editor
