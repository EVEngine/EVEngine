#include "procgen/biome/editor/BiomeRulesEditor.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include "editor/EditorProtocol.h"
#include "editor/EditorScriptProjection.h"
#include "editor/EditorWorkspace.h"
#include "procgen/PointSet.h"
#include "procgen/biome/editor/BiomeEditorModule.h"
#include "procgen/biome/editor/BiomeRulesEditorScriptBindings.h"

// BiomeRulesEditor.cpp
namespace eve::biome_editor {
namespace {

template <class T = void>
biome_editing::Result<T> editorError(biome_editing::EditorStatus status, std::string rule, std::string message) {
    return eve::editing::failed<T>(status, biome_editing::RuleId(std::move(rule)), std::move(message));
}

}  // namespace

BiomeRulesEditor::SpatialResolver::SpatialResolver(procgen::SpatialData* forest, procgen::SpatialData* clearing)
    : forest_(forest), clearing_(clearing) {}

biome_editing::Result<procgen::SpatialData*> BiomeRulesEditor::SpatialResolver::resolve(
    const std::string& asset) const {
    if (asset == "asset://preview/forest.spatial") return eve::editing::applied<procgen::SpatialData*>(forest_);
    if (asset == "asset://preview/clearing.spatial") return eve::editing::applied<procgen::SpatialData*>(clearing_);
    return editorError<procgen::SpatialData*>(biome_editing::EditorStatus::NotFound, "editor.biome.spatial",
                                              "Unknown biome spatial asset");
}

BiomeRulesEditor::BiomeRulesEditor(std::string targetId)
    : target_(std::move(targetId)),
      authority_(&target_),
      transactions_(&authority_),
      forestDomain_(procgen::SpatialData::box(0.0f, 0.0f, 0.0f, 4.0f, 0.0f, 4.0f)),
      clearingDomain_(procgen::SpatialData::box(0.0f, 0.0f, 0.0f, 1.5f, 0.0f, 1.5f)),
      resolver_(&forestDomain_, &clearingDomain_) {
    seedPreviewDocument();
    auto previewed = refreshPreview();
    if (!previewed.ok()) previewed.ignore("biome editor keeps an empty PointSet when the seeded preview is rejected");
}

void BiomeRulesEditor::seedPreviewDocument() {
    biome_editing::EditorValue::Array assets;
    assets.push_back(biome_editing::EditorValue::Object{{"id", std::string("oak")},
                                                        {"asset", std::string("models/oak.prefab")},
                                                        {"weight", 1.0},
                                                        {"minScale", 1.0},
                                                        {"maxScale", 1.0},
                                                        {"randomYaw", false}});
    biome_editing::EditorValue::Array layers;
    layers.push_back(biome_editing::EditorValue::Object{{"id", std::string("forest")},
                                                        {"name", std::string("forest")},
                                                        {"spatial", std::string("asset://preview/forest.spatial")},
                                                        {"priority", std::int64_t{1}},
                                                        {"density", 1.0},
                                                        {"assets", std::move(assets)}});
    biome_editing::EditorValue::Object content;
    content["layers"]     = std::move(layers);
    content["exclusions"] = biome_editing::EditorValue::Array{};
    biome_editing::EditorValue::Object root;
    root["schemaVersion"] = std::int64_t{1};
    root["content"]       = biome_editing::EditorValue(std::move(content));
    auto loaded           = target_.loadSnapshot(biome_editing::EditorValue(std::move(root)));
    if (!loaded.ok()) loaded.ignore("biome editor keeps defaults when the preview snapshot is rejected");
}

biome_editing::Result<void> BiomeRulesEditor::configureWorkspace(editor::EditorWorkspace& workspace) const {
    editor::EditorWorkspace candidate = workspace;
    struct Panel {
        const char* id;
        const char* title;
        const char* region;
        const char* context;
        int         order;
    };
    constexpr Panel panels[] = {
        {"biome.layers", "Layers", "left", "list", 100},
        {"biome.preview", "World Preview", "center", "preview", 100},
        {"biome.inspector", "Biome Inspector", "right", "inspector", 100},
        {"biome.assets", "Layer Assets", "bottom", "assets", 100},
    };
    for (const auto& panel : panels) {
        if (!candidate.registerPanel(panel.id, panel.title, panel.region, panel.order) ||
            !candidate.setPanelCapability(panel.id, "biome.rules") ||
            !candidate.setPanelContext(panel.id, panel.context))
            return editorError(biome_editing::EditorStatus::Rejected, "editor.biome.workspace-conflict",
                               "Could not install the biome workspace composition");
    }
    if (!candidate.activatePanel("biome.preview"))
        return editorError(biome_editing::EditorStatus::Rejected, "editor.biome.workspace-activate",
                           "Could not activate the biome preview panel");
    workspace = std::move(candidate);
    return eve::editing::applied<void>();
}

editor::SelectionSnapshot BiomeRulesEditor::selection() const {
    editor::SelectionSnapshot snapshot;
    snapshot.channel = "biome";
    editor::SelectionItem item;
    item.domain = editor::SelectionDomain::Asset;
    item.target = editor::TargetId(target_.targetId());
    item.item   = editor::StableId(selectedId_);
    item.type   = selectedType_;
    snapshot.items.push_back(item);
    snapshot.primary = item;
    return snapshot;
}

biome_editing::Result<void> BiomeRulesEditor::commit(biome_editing::Result<biome_editing::DomainOperation> operation,
                                                     std::string                                           label) {
    if (!operation.ok()) return biome_editing::Result<void>::failure(operation.status());
    editor::TransactionSpec spec;
    spec.id           = editor::TransactionId("biome.rules.tx." + std::to_string(++txSequence_));
    spec.label        = std::move(label);
    spec.target       = editor::TargetId(target_.targetId());
    spec.baseRevision = target_.revision();
    auto begun        = transactions_.begin(std::move(spec));
    if (!begun.ok()) return editorError(begun.code(), "editor.biome.begin", "Could not begin the biome transaction");
    auto appended = transactions_.append(std::move(operation).takeValue());
    if (!appended.ok()) {
        auto discarded = transactions_.discard();
        if (!discarded.ok()) discarded.ignore("pending biome transaction already inactive");
        return biome_editing::Result<void>::failure(appended.status());
    }
    auto committed = transactions_.commit();
    if (!committed.ok()) return biome_editing::Result<void>::failure(committed.status());
    return refreshPreview();
}

biome_editing::Result<void> BiomeRulesEditor::refreshPreview() {
    auto published = runtime_.publish(target_, resolver_);
    if (!published.ok()) return biome_editing::Result<void>::failure(published.status());
    auto generated = runtime_.preview(&forestDomain_, spacing_, seed_, 0.0f, runtime_.revision());
    if (!generated.ok()) return biome_editing::Result<void>::failure(generated.status());
    std::vector<PreviewPoint> next;
    const auto&               points = generated.value();
    if (points) {
        next.reserve(static_cast<std::size_t>(points->getCount()));
        for (int i = 0; i < points->getCount(); ++i) {
            PreviewPoint point;
            point.x     = points->getX(i);
            point.z     = points->getZ(i);
            point.asset = points->getStringAttribute(i, "asset", {});
            next.push_back(std::move(point));
        }
    }
    points_          = std::move(next);
    previewRevision_ = target_.revision();
    return eve::editing::applied<void>();
}

biome_editing::Result<void> BiomeRulesEditor::selectLayer(std::string id) {
    bool found = false;
    for (const auto& layer : target_.layers())
        if (layer.id.value() == id) found = true;
    if (!found)
        return editorError(biome_editing::EditorStatus::NotFound, "editor.biome.layer", "Biome layer was not found");
    selectedId_   = std::move(id);
    selectedType_ = "biome.layer";
    return eve::editing::applied<void>();
}

biome_editing::Result<void> BiomeRulesEditor::selectAsset(std::string id) {
    bool found = false;
    for (const auto& layer : target_.layers())
        for (const auto& asset : layer.assets)
            if (asset.id.value() == id) found = true;
    if (!found)
        return editorError(biome_editing::EditorStatus::NotFound, "editor.biome.asset", "Biome asset was not found");
    selectedId_   = std::move(id);
    selectedType_ = "biome.asset";
    return eve::editing::applied<void>();
}

biome_editing::Result<void> BiomeRulesEditor::setLayerDensity(double density) {
    if (selectedType_ != "biome.layer")
        return editorError(biome_editing::EditorStatus::Rejected, "editor.biome.selection",
                           "A layer must be selected to change density");
    const auto previous = points_;
    const auto revision = previewRevision_;
    auto       applied =
        commit(target_.makeSet(selection(), biome_editing::PropertyPath("layer.density"),
                               biome_editing::EditorValue(density), biome_editing::PropertySetMode::Absolute),
               "Set layer density");
    if (!applied.ok()) {
        points_          = previous;
        previewRevision_ = revision;
    }
    return applied;
}

biome_editing::Result<void> BiomeRulesEditor::setLayerPriority(int priority) {
    if (selectedType_ != "biome.layer")
        return editorError(biome_editing::EditorStatus::Rejected, "editor.biome.selection",
                           "A layer must be selected to change priority");
    return commit(
        target_.makeSet(selection(), biome_editing::PropertyPath("layer.priority"),
                        biome_editing::EditorValue(std::int64_t{priority}), biome_editing::PropertySetMode::Absolute),
        "Set layer priority");
}

biome_editing::Result<void> BiomeRulesEditor::setAssetWeight(double weight) {
    if (selectedType_ != "biome.asset")
        return editorError(biome_editing::EditorStatus::Rejected, "editor.biome.selection",
                           "An asset must be selected to change weight");
    return commit(target_.makeSet(selection(), biome_editing::PropertyPath("asset.weight"),
                                  biome_editing::EditorValue(weight), biome_editing::PropertySetMode::Absolute),
                  "Set asset weight");
}

biome_editing::Result<void> BiomeRulesEditor::createLayer(std::string id, std::string name) {
    biome_editing::BiomeLayerValue layer;
    layer.id           = biome_editing::ObjectId(std::move(id));
    layer.name         = std::move(name);
    layer.spatialAsset = "asset://preview/forest.spatial";
    layer.density      = 1.0f;
    auto created       = commit(target_.makeCreateLayer(layer), "Create layer");
    if (!created.ok()) return created;
    return selectLayer(layer.id.value());
}

biome_editing::Result<void> BiomeRulesEditor::deleteSelectedLayer() {
    if (selectedType_ != "biome.layer")
        return editorError(biome_editing::EditorStatus::Rejected, "editor.biome.selection",
                           "A layer must be selected to delete it");
    auto deleted = commit(target_.makeDeleteLayer(biome_editing::ObjectId(selectedId_)), "Delete layer");
    if (!deleted.ok()) return deleted;
    if (!target_.layers().empty()) return selectLayer(target_.layers().front().id.value());
    selectedId_.clear();
    return eve::editing::applied<void>();
}

biome_editing::Result<void> BiomeRulesEditor::createAsset(std::string id, std::string asset) {
    const auto* layer = selectedLayer();
    if (!layer)
        return editorError(biome_editing::EditorStatus::Rejected, "editor.biome.selection",
                           "A layer must be selected to add an asset");
    biome_editing::BiomeAssetValue value;
    value.id     = biome_editing::ObjectId(std::move(id));
    value.asset  = std::move(asset);
    auto created = commit(target_.makeCreateAsset(layer->id, value), "Create asset");
    if (!created.ok()) return created;
    return selectAsset(value.id.value());
}

biome_editing::Result<void> BiomeRulesEditor::deleteSelectedAsset() {
    if (selectedType_ != "biome.asset")
        return editorError(biome_editing::EditorStatus::Rejected, "editor.biome.selection",
                           "An asset must be selected to delete it");
    return commit(target_.makeDeleteAsset(biome_editing::ObjectId(selectedId_)), "Delete asset");
}

biome_editing::Result<void> BiomeRulesEditor::addExclusion(std::string spatialAsset) {
    auto next = target_.exclusions();
    next.push_back(std::move(spatialAsset));
    const auto previous = points_;
    const auto revision = previewRevision_;
    auto       applied  = commit(target_.makeSetExclusions(std::move(next)), "Add exclusion");
    if (!applied.ok()) {
        points_          = previous;
        previewRevision_ = revision;
    }
    return applied;
}

biome_editing::Result<void> BiomeRulesEditor::removeExclusion(std::string spatialAsset) {
    auto next = target_.exclusions();
    std::erase(next, spatialAsset);
    return commit(target_.makeSetExclusions(std::move(next)), "Remove exclusion");
}

biome_editing::Result<void> BiomeRulesEditor::setSeed(std::uint32_t seed) {
    seed_ = seed;
    return refreshPreview();
}

biome_editing::Result<void> BiomeRulesEditor::setSpacing(float spacing) {
    if (!std::isfinite(spacing) || spacing <= 0.0f)
        return editorError(biome_editing::EditorStatus::Rejected, "editor.biome.spacing",
                           "Biome preview spacing must be positive");
    spacing_ = spacing;
    return refreshPreview();
}

biome_editing::Result<editor::TransactionReceipt> BiomeRulesEditor::undo() {
    auto result = transactions_.undo();
    if (!result.ok()) return result;
    auto previewed = refreshPreview();
    if (!previewed.ok()) return biome_editing::Result<editor::TransactionReceipt>::failure(previewed.status());
    return result;
}

biome_editing::Result<editor::TransactionReceipt> BiomeRulesEditor::redo() {
    auto result = transactions_.redo();
    if (!result.ok()) return result;
    auto previewed = refreshPreview();
    if (!previewed.ok()) return biome_editing::Result<editor::TransactionReceipt>::failure(previewed.status());
    return result;
}

const biome_editing::BiomeLayerValue* BiomeRulesEditor::selectedLayer() const {
    if (selectedType_ == "biome.layer") {
        for (const auto& layer : target_.layers())
            if (layer.id.value() == selectedId_) return &layer;
    }
    if (selectedType_ == "biome.asset") {
        for (const auto& layer : target_.layers())
            for (const auto& asset : layer.assets)
                if (asset.id.value() == selectedId_) return &layer;
    }
    if (!target_.layers().empty()) return &target_.layers().front();
    return nullptr;
}

const biome_editing::BiomeAssetValue* BiomeRulesEditor::assetAt(int index) const {
    const auto* layer = selectedLayer();
    if (!layer || index < 0 || static_cast<std::size_t>(index) >= layer->assets.size()) return nullptr;
    return &layer->assets[static_cast<std::size_t>(index)];
}

std::string BiomeRulesEditor::layerId(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= target_.layers().size()) return {};
    return target_.layers()[static_cast<std::size_t>(index)].id.value();
}
std::string BiomeRulesEditor::layerName(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= target_.layers().size()) return {};
    return target_.layers()[static_cast<std::size_t>(index)].name;
}
float BiomeRulesEditor::layerDensity(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= target_.layers().size()) return 0.0f;
    return target_.layers()[static_cast<std::size_t>(index)].density;
}
int BiomeRulesEditor::layerPriority(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= target_.layers().size()) return 0;
    return target_.layers()[static_cast<std::size_t>(index)].priority;
}
bool BiomeRulesEditor::isLayerSelected(int index) const {
    return selectedType_ == "biome.layer" && layerId(index) == selectedId_;
}

int BiomeRulesEditor::assetCount() const {
    const auto* layer = selectedLayer();
    return layer ? static_cast<int>(layer->assets.size()) : 0;
}
std::string BiomeRulesEditor::assetId(int index) const {
    const auto* asset = assetAt(index);
    return asset ? asset->id.value() : std::string{};
}
std::string BiomeRulesEditor::assetRef(int index) const {
    const auto* asset = assetAt(index);
    return asset ? asset->asset : std::string{};
}
float BiomeRulesEditor::assetWeight(int index) const {
    const auto* asset = assetAt(index);
    return asset ? asset->weight : 0.0f;
}
bool BiomeRulesEditor::isAssetSelected(int index) const {
    const auto* asset = assetAt(index);
    return asset && selectedType_ == "biome.asset" && asset->id.value() == selectedId_;
}

std::string BiomeRulesEditor::exclusionAsset(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= target_.exclusions().size()) return {};
    return target_.exclusions()[static_cast<std::size_t>(index)];
}

float BiomeRulesEditor::pointX(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= points_.size()) return 0.0f;
    return points_[static_cast<std::size_t>(index)].x;
}
float BiomeRulesEditor::pointZ(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= points_.size()) return 0.0f;
    return points_[static_cast<std::size_t>(index)].z;
}
std::string BiomeRulesEditor::pointAsset(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= points_.size()) return {};
    return points_[static_cast<std::size_t>(index)].asset;
}

}  // namespace eve::biome_editor

// BiomeRulesEditorScriptBindings.cpp
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
    biomeEditor.addFunc(
        "getSeed", [](ScriptBiomeRulesEditor* self) { return self ? static_cast<int>(self->editor().seed()) : 0; });
    biomeEditor.addFunc("getSpacing",
                        [](ScriptBiomeRulesEditor* self) { return self ? self->editor().spacing() : 0.0f; });
    biomeEditor.addFunc("getSelectedId", [](ScriptBiomeRulesEditor* self) {
        return self ? self->editor().selectedId() : std::string{};
    });
    biomeEditor.addFunc("getSelectedType", [](ScriptBiomeRulesEditor* self) {
        return self ? self->editor().selectedType() : std::string{};
    });
    biomeEditor.addFunc("getLayerCount",
                        [](ScriptBiomeRulesEditor* self) { return self ? self->editor().layerCount() : 0; });
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
    biomeEditor.addFunc("getAssetCount",
                        [](ScriptBiomeRulesEditor* self) { return self ? self->editor().assetCount() : 0; });
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
    biomeEditor.addFunc("getExclusionCount",
                        [](ScriptBiomeRulesEditor* self) { return self ? self->editor().exclusionCount() : 0; });
    biomeEditor.addFunc("getExclusionAsset", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().exclusionAsset(index) : std::string{};
    });
    biomeEditor.addFunc("getPointCount",
                        [](ScriptBiomeRulesEditor* self) { return self ? self->editor().pointCount() : 0; });
    biomeEditor.addFunc("getPointX", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().pointX(index) : 0.0f;
    });
    biomeEditor.addFunc("getPointZ", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().pointZ(index) : 0.0f;
    });
    biomeEditor.addFunc("getPointAsset", [](ScriptBiomeRulesEditor* self, int index) {
        return self ? self->editor().pointAsset(index) : std::string{};
    });

    editor::registerEditorOwnedCreate<ScriptBiomeRulesEditor, BiomeEditorModule>(moduleClass, bind,
                                                                                 "biome target id must not be empty");
}

}  // namespace eve::biome_editor
