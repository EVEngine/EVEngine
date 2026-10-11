#include "procgen/biome/editor/BiomeRulesEditorScriptBindings.h"

#include "editor/EditorWorkspace.h"
#include "procgen/biome/editor/BiomeEditorModule.h"
#include "procgen/biome/editor/BiomeRulesEditor.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <string>
#include "editor/EditorScriptProjection.h"

namespace eve::biome_editor {
namespace {

constexpr const char* kBindingSource = "editor.biome.rules.squirrel";

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
    const editor::ScriptBind bind{table.getHandle(), kBindingSource};
    auto                     biomeEditor = editor::addScriptClass<ScriptBiomeRulesEditor>(table, "BiomeRulesEditor");
    editor::registerEditorWorkspace<ScriptBiomeRulesEditor>(biomeEditor, bind,
                                   "biome editor and workspace must not be null");
    biomeEditor.addFunc("selectLayer", [bind](ScriptBiomeRulesEditor* self, const std::string& id) {
        return bind.checked(self, "biome editor must not be null", [&] { return self->editor().selectLayer(id); });
    });
    biomeEditor.addFunc("selectAsset", [bind](ScriptBiomeRulesEditor* self, const std::string& id) {
        return bind.checked(self, "biome editor must not be null", [&] { return self->editor().selectAsset(id); });
    });
    biomeEditor.addFunc("setLayerDensity", [bind](ScriptBiomeRulesEditor* self, float density) {
        return bind.checked(self, "biome editor must not be null",
                            [&] { return self->editor().setLayerDensity(static_cast<double>(density)); });
    });
    biomeEditor.addFunc("setLayerPriority", [bind](ScriptBiomeRulesEditor* self, int priority) {
        return bind.checked(self, "biome editor must not be null",
                            [&] { return self->editor().setLayerPriority(priority); });
    });
    biomeEditor.addFunc("setAssetWeight", [bind](ScriptBiomeRulesEditor* self, float weight) {
        return bind.checked(self, "biome editor must not be null",
                            [&] { return self->editor().setAssetWeight(static_cast<double>(weight)); });
    });
    biomeEditor.addFunc("createLayer",
                        [bind](ScriptBiomeRulesEditor* self, const std::string& id, const std::string& name) {
                            return bind.checked(self, "biome editor must not be null",
                                                [&] { return self->editor().createLayer(id, name); });
                        });
    biomeEditor.addFunc("deleteSelectedLayer", [bind](ScriptBiomeRulesEditor* self) {
        return bind.checked(self, "biome editor must not be null",
                            [&] { return self->editor().deleteSelectedLayer(); });
    });
    biomeEditor.addFunc("createAsset",
                        [bind](ScriptBiomeRulesEditor* self, const std::string& id, const std::string& asset) {
                            return bind.checked(self, "biome editor must not be null",
                                                [&] { return self->editor().createAsset(id, asset); });
                        });
    biomeEditor.addFunc("deleteSelectedAsset", [bind](ScriptBiomeRulesEditor* self) {
        return bind.checked(self, "biome editor must not be null",
                            [&] { return self->editor().deleteSelectedAsset(); });
    });
    biomeEditor.addFunc("addExclusion", [bind](ScriptBiomeRulesEditor* self, const std::string& spatialAsset) {
        return bind.checked(self, "biome editor must not be null",
                            [&] { return self->editor().addExclusion(spatialAsset); });
    });
    biomeEditor.addFunc("removeExclusion", [bind](ScriptBiomeRulesEditor* self, const std::string& spatialAsset) {
        return bind.checked(self, "biome editor must not be null",
                            [&] { return self->editor().removeExclusion(spatialAsset); });
    });
    biomeEditor.addFunc("setSeed", [bind](ScriptBiomeRulesEditor* self, int seed) {
        return bind.checked(self, "biome editor must not be null",
                            [&] { return self->editor().setSeed(static_cast<std::uint32_t>(seed)); });
    });
    biomeEditor.addFunc("setSpacing", [bind](ScriptBiomeRulesEditor* self, float spacing) {
        return bind.checked(self, "biome editor must not be null", [&] { return self->editor().setSpacing(spacing); });
    });
    editor::registerEditorHistory<ScriptBiomeRulesEditor>(biomeEditor, bind, "biome editor must not be null");
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

    editor::registerEditorOwnedCreate<ScriptBiomeRulesEditor, BiomeEditorModule>(moduleClass, bind, "biome target id must not be empty");
}

}  // namespace eve::biome_editor
