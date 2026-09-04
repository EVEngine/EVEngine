#pragma once

/**
 * @file BiomeRulesEditor.h
 * @brief UI-neutral biome editor: workspace, PointSet preview, undo.
 */

#include "biome_editing/BiomeTarget.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorSelection.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorWorkspace.h"
#include "procgen/SpatialData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::biome_editor {

/**
 * @brief Workspace controller for one authored biome rules document.
 *
 * Owns the document, local undo, borrowed spatial domains and a copied PointSet
 * preview. Preview requests always carry the published revision; a stale or
 * failed generation keeps the last successful points on screen.
 *
 * @ownership Editor owns the document and spatial copies. Preview getters copy values.
 * @threadaffinity Owner thread only.
 * @reentrancy No unknown callbacks.
 */
class BiomeRulesEditor {
public:
    /** @brief Construct a seeded forest layer with one weighted asset. */
    explicit BiomeRulesEditor(std::string targetId);

    BiomeRulesEditor(const BiomeRulesEditor&)            = delete;
    BiomeRulesEditor& operator=(const BiomeRulesEditor&) = delete;

    /** @brief Borrow the authoritative biome document. */
    const biome_editing::BiomeDocumentTarget& target() const noexcept { return target_; }

    /**
     * @brief Install Layers / Preview / Inspector / Assets panels.
     * @note Does not retain @p workspace.
     */
    [[nodiscard]] biome_editing::EditorResult<void> configureWorkspace(editor::EditorWorkspace& workspace) const;

    [[nodiscard]] biome_editing::EditorResult<void> selectLayer(std::string id);
    [[nodiscard]] biome_editing::EditorResult<void> selectAsset(std::string id);
    [[nodiscard]] biome_editing::EditorResult<void> setLayerDensity(double density);
    [[nodiscard]] biome_editing::EditorResult<void> setLayerPriority(int priority);
    [[nodiscard]] biome_editing::EditorResult<void> setAssetWeight(double weight);
    [[nodiscard]] biome_editing::EditorResult<void> createLayer(std::string id, std::string name);
    [[nodiscard]] biome_editing::EditorResult<void> deleteSelectedLayer();
    [[nodiscard]] biome_editing::EditorResult<void> createAsset(std::string id, std::string asset);
    [[nodiscard]] biome_editing::EditorResult<void> deleteSelectedAsset();
    [[nodiscard]] biome_editing::EditorResult<void> addExclusion(std::string spatialAsset);
    [[nodiscard]] biome_editing::EditorResult<void> removeExclusion(std::string spatialAsset);
    [[nodiscard]] biome_editing::EditorResult<void> setSeed(std::uint32_t seed);
    [[nodiscard]] biome_editing::EditorResult<void> setSpacing(float spacing);

    [[nodiscard]] biome_editing::EditorResult<editor::TransactionReceipt> undo();
    [[nodiscard]] biome_editing::EditorResult<editor::TransactionReceipt> redo();

    bool          canUndo() const noexcept { return transactions_.canUndo(); }
    bool          canRedo() const noexcept { return transactions_.canRedo(); }
    std::uint64_t revision() const noexcept { return target_.revision(); }
    std::uint64_t previewRevision() const noexcept { return previewRevision_; }
    std::uint32_t seed() const noexcept { return seed_; }
    float         spacing() const noexcept { return spacing_; }
    std::string   selectedId() const { return selectedId_; }
    std::string   selectedType() const { return selectedType_; }

    int         layerCount() const { return static_cast<int>(target_.layers().size()); }
    std::string layerId(int index) const;
    std::string layerName(int index) const;
    float       layerDensity(int index) const;
    int         layerPriority(int index) const;
    bool        layerSelected(int index) const;

    int         assetCount() const;
    std::string assetId(int index) const;
    std::string assetRef(int index) const;
    float       assetWeight(int index) const;
    bool        assetSelected(int index) const;

    int         exclusionCount() const { return static_cast<int>(target_.exclusions().size()); }
    std::string exclusionAsset(int index) const;

    int         pointCount() const { return static_cast<int>(points_.size()); }
    float       pointX(int index) const;
    float       pointZ(int index) const;
    std::string pointAsset(int index) const;

private:
    struct PreviewPoint {
        float       x = 0;
        float       z = 0;
        std::string asset;
    };

    class SpatialResolver final : public biome_editing::IBiomeSpatialResolver {
    public:
        SpatialResolver(procgen::SpatialData* forest, procgen::SpatialData* clearing);
        biome_editing::EditorResult<procgen::SpatialData*> resolve(const std::string& asset) const override;

    private:
        procgen::SpatialData* forest_   = nullptr;
        procgen::SpatialData* clearing_ = nullptr;
    };

    [[nodiscard]] biome_editing::EditorResult<void> commit(
        biome_editing::EditorResult<biome_editing::DomainOperation> operation, std::string label);
    [[nodiscard]] biome_editing::EditorResult<void> refreshPreview();
    void                                            seedPreviewDocument();
    editor::SelectionSnapshot                       selection() const;
    const biome_editing::BiomeLayerValue*           selectedLayer() const;
    const biome_editing::BiomeAssetValue*           assetAt(int index) const;

    biome_editing::BiomeDocumentTarget  target_;
    editor::LocalWorldAuthority         authority_;
    editor::LocalTransactionBackend     transactions_;
    procgen::SpatialData                forestDomain_;
    procgen::SpatialData                clearingDomain_;
    SpatialResolver                     resolver_;
    biome_editing::BiomeDocumentRuntime runtime_;
    std::vector<PreviewPoint>           points_;
    std::string                         selectedId_       = "forest";
    std::string                         selectedType_     = "biome.layer";
    std::uint64_t                       txSequence_       = 0;
    std::uint64_t                       previewRevision_  = 0;
    std::uint32_t                       seed_             = 42;
    float                               spacing_          = 2.0f;
};

}  // namespace eve::biome_editor
