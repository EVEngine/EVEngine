#include "biome_editor/BiomeRulesEditorScriptBindings.h"

#include "biome_editor/BiomeEditorModule.h"
#include "biome_editor/BiomeRulesEditor.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "editor/EditorWorkspace.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace eve::biome_editor {
namespace {

constexpr const char* kBindingSource = "editor.biome.rules.squirrel";

Status statusFrom(const biome_editing::EditorResult<void>& result) { return result.status(); }

template <class T>
Status statusFrom(const biome_editing::EditorResult<T>& result) {
    return result.status();
}

ssq::Table project(HSQUIRRELVM vm, const biome_editing::EditorResult<void>& result) {
    return script::projectStatusResult(vm, statusFrom(result), result.ok(), false);
}

template <class T>
ssq::Table project(HSQUIRRELVM vm, const biome_editing::EditorResult<T>& result, Value value) {
    const bool hasValue = result.ok();
    return script::projectStatusResult(vm, statusFrom(result), hasValue, hasValue, value);
}

ssq::Table bindingFailure(HSQUIRRELVM vm, DiagnosticCode code, std::string message, std::string path = {}) {
    return script::projectStatusResult(
        vm, Status::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, kBindingSource)), false,
        false);
}

class ScriptBiomeRulesEditor {
public:
    explicit ScriptBiomeRulesEditor(std::string targetId) : editor_(std::move(targetId)) {}

    BiomeRulesEditor&       editor() noexcept { return editor_; }
    const BiomeRulesEditor& editor() const noexcept { return editor_; }

private:
    BiomeRulesEditor editor_;
};

}  // namespace

void exposeBiomeRulesEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass) {
    const HSQUIRRELVM vm = table.getHandle();
    auto biomeEditor     = table.addClass<ScriptBiomeRulesEditor>(
        "BiomeRulesEditor",
        std::function<ScriptBiomeRulesEditor*()>([]() -> ScriptBiomeRulesEditor* { return nullptr; }), true);

    biomeEditor.addFunc("configureWorkspace",
                        [vm](ScriptBiomeRulesEditor* self, editor::EditorWorkspace* workspace) {
                            if (!self || !workspace)
                                return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                      "biome editor and workspace must not be null", "workspace");
                            return project(vm, self->editor().configureWorkspace(*workspace));
                        });
    biomeEditor.addFunc("selectLayer", [vm](ScriptBiomeRulesEditor* self, const std::string& id) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome editor must not be null");
        return project(vm, self->editor().selectLayer(id));
    });
    biomeEditor.addFunc("selectAsset", [vm](ScriptBiomeRulesEditor* self, const std::string& id) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome editor must not be null");
        return project(vm, self->editor().selectAsset(id));
    });
    biomeEditor.addFunc("setLayerDensity", [vm](ScriptBiomeRulesEditor* self, float density) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome editor must not be null");
        return project(vm, self->editor().setLayerDensity(static_cast<double>(density)));
    });
    biomeEditor.addFunc("setLayerPriority", [vm](ScriptBiomeRulesEditor* self, int priority) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome editor must not be null");
        return project(vm, self->editor().setLayerPriority(priority));
    });
    biomeEditor.addFunc("setAssetWeight", [vm](ScriptBiomeRulesEditor* self, float weight) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome editor must not be null");
        return project(vm, self->editor().setAssetWeight(static_cast<double>(weight)));
    });
    biomeEditor.addFunc("createLayer",
                        [vm](ScriptBiomeRulesEditor* self, const std::string& id, const std::string& name) {
                            if (!self)
                                return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                      "biome editor must not be null");
                            return project(vm, self->editor().createLayer(id, name));
                        });
    biomeEditor.addFunc("deleteSelectedLayer", [vm](ScriptBiomeRulesEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome editor must not be null");
        return project(vm, self->editor().deleteSelectedLayer());
    });
    biomeEditor.addFunc("createAsset",
                        [vm](ScriptBiomeRulesEditor* self, const std::string& id, const std::string& asset) {
                            if (!self)
                                return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                      "biome editor must not be null");
                            return project(vm, self->editor().createAsset(id, asset));
                        });
    biomeEditor.addFunc("deleteSelectedAsset", [vm](ScriptBiomeRulesEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome editor must not be null");
        return project(vm, self->editor().deleteSelectedAsset());
    });
    biomeEditor.addFunc("addExclusion", [vm](ScriptBiomeRulesEditor* self, const std::string& spatialAsset) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome editor must not be null");
        return project(vm, self->editor().addExclusion(spatialAsset));
    });
    biomeEditor.addFunc("removeExclusion", [vm](ScriptBiomeRulesEditor* self, const std::string& spatialAsset) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome editor must not be null");
        return project(vm, self->editor().removeExclusion(spatialAsset));
    });
    biomeEditor.addFunc("setSeed", [vm](ScriptBiomeRulesEditor* self, int seed) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome editor must not be null");
        return project(vm, self->editor().setSeed(static_cast<std::uint32_t>(seed)));
    });
    biomeEditor.addFunc("setSpacing", [vm](ScriptBiomeRulesEditor* self, float spacing) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome editor must not be null");
        return project(vm, self->editor().setSpacing(spacing));
    });
    biomeEditor.addFunc("undo", [vm](ScriptBiomeRulesEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome editor must not be null");
        auto result = self->editor().undo();
        return project(vm, result,
                       Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    biomeEditor.addFunc("redo", [vm](ScriptBiomeRulesEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome editor must not be null");
        auto result = self->editor().redo();
        return project(vm, result,
                       Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    biomeEditor.addFunc("canUndo", [](ScriptBiomeRulesEditor* self) { return self && self->editor().canUndo(); });
    biomeEditor.addFunc("canRedo", [](ScriptBiomeRulesEditor* self) { return self && self->editor().canRedo(); });
    biomeEditor.addFunc("getRevision", [](ScriptBiomeRulesEditor* self) {
        return self ? static_cast<int>(self->editor().revision()) : 0;
    });
    biomeEditor.addFunc("getPreviewRevision", [](ScriptBiomeRulesEditor* self) {
        return self ? static_cast<int>(self->editor().previewRevision()) : 0;
    });
    biomeEditor.addFunc("getSeed", [](ScriptBiomeRulesEditor* self) {
        return self ? static_cast<int>(self->editor().seed()) : 0;
    });
    biomeEditor.addFunc("getSpacing", [](ScriptBiomeRulesEditor* self) {
        return self ? self->editor().spacing() : 0.0f;
    });
    biomeEditor.addFunc("getSelectedId", [](ScriptBiomeRulesEditor* self) {
        return self ? self->editor().selectedId() : std::string{};
    });
    biomeEditor.addFunc("getSelectedType", [](ScriptBiomeRulesEditor* self) {
        return self ? self->editor().selectedType() : std::string{};
    });
    biomeEditor.addFunc("getLayerCount", [](ScriptBiomeRulesEditor* self) {
        return self ? self->editor().layerCount() : 0;
    });
    biomeEditor.addFunc("getLayerId", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().layerId(index) : std::string{};
    });
    biomeEditor.addFunc("getLayerName", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().layerName(index) : std::string{};
    });
    biomeEditor.addFunc("getLayerDensity", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().layerDensity(index) : 0.0f;
    });
    biomeEditor.addFunc("getLayerPriority", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().layerPriority(index) : 0;
    });
    biomeEditor.addFunc("getLayerSelected", [](ScriptBiomeRulesEditor* self, int index) {
        return self && self->editor().isLayerSelected(index);
    });
    biomeEditor.addFunc("getAssetCount", [](ScriptBiomeRulesEditor* self) {
        return self ? self->editor().assetCount() : 0;
    });
    biomeEditor.addFunc("getAssetId", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().assetId(index) : std::string{};
    });
    biomeEditor.addFunc("getAssetRef", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().assetRef(index) : std::string{};
    });
    biomeEditor.addFunc("getAssetWeight", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().assetWeight(index) : 0.0f;
    });
    biomeEditor.addFunc("getAssetSelected", [](ScriptBiomeRulesEditor* self, int index) {
        return self && self->editor().isAssetSelected(index);
    });
    biomeEditor.addFunc("getExclusionCount", [](ScriptBiomeRulesEditor* self) {
        return self ? self->editor().exclusionCount() : 0;
    });
    biomeEditor.addFunc("getExclusionAsset", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().exclusionAsset(index) : std::string{};
    });
    biomeEditor.addFunc("getPointCount", [](ScriptBiomeRulesEditor* self) {
        return self ? self->editor().pointCount() : 0;
    });
    biomeEditor.addFunc("getPointX", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().pointX(index) : 0.0f;
    });
    biomeEditor.addFunc("getPointZ", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().pointZ(index) : 0.0f;
    });
    biomeEditor.addFunc("getPointAsset", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().pointAsset(index) : std::string{};
    });

    moduleClass.addFunc("create", [vm](BiomeEditorModule*, const std::string& targetId) {
        if (targetId.empty())
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "biome target id must not be empty", "targetId");
        auto object = script::makeOwnedSquirrelInstance<ScriptBiomeRulesEditor>(
            vm, std::make_unique<ScriptBiomeRulesEditor>(targetId));
        if (!object) return script::projectStatusResult(vm, object.status(), false, false);
        ssq::Object owned = std::move(object).takeValue();
        auto        result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", owned);
        result.set("ownership", std::string("owned"));
        return result;
    });
}

}  // namespace eve::biome_editor
