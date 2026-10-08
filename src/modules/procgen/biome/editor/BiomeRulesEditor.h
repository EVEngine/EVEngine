#pragma once

/**
 * @file BiomeRulesEditor.h
 * @brief UI-neutral biome editor: workspace, PointSet preview, undo.
 */

#include "procgen/biome/editing/BiomeTarget.h"
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
class EVENGINE_API_EDITORS BiomeRulesEditor {
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
    [[nodiscard]] biome_editing::Result<void> configureWorkspace(editor::EditorWorkspace& workspace) const;

    [[nodiscard]] biome_editing::Result<void> selectLayer(std::string id);
    [[nodiscard]] biome_editing::Result<void> selectAsset(std::string id);
    [[nodiscard]] biome_editing::Result<void> setLayerDensity(double density);
    [[nodiscard]] biome_editing::Result<void> setLayerPriority(int priority);
    [[nodiscard]] biome_editing::Result<void> setAssetWeight(double weight);
    [[nodiscard]] biome_editing::Result<void> createLayer(std::string id, std::string name);
    [[nodiscard]] biome_editing::Result<void> deleteSelectedLayer();
    [[nodiscard]] biome_editing::Result<void> createAsset(std::string id, std::string asset);
    [[nodiscard]] biome_editing::Result<void> deleteSelectedAsset();
    [[nodiscard]] biome_editing::Result<void> addExclusion(std::string spatialAsset);
    [[nodiscard]] biome_editing::Result<void> removeExclusion(std::string spatialAsset);
    [[nodiscard]] biome_editing::Result<void> setSeed(std::uint32_t seed);
    [[nodiscard]] biome_editing::Result<void> setSpacing(float spacing);

    [[nodiscard]] biome_editing::Result<editor::TransactionReceipt> undo();
    [[nodiscard]] biome_editing::Result<editor::TransactionReceipt> redo();

    bool          canUndo() const noexcept { return transactions_.canUndo(); }
    /** @brief Can redo. */
    bool          canRedo() const noexcept { return transactions_.canRedo(); }
    /** @brief Revision. */
    std::uint64_t revision() const noexcept { return target_.revision(); }
    /** @brief Preview revision. */
    std::uint64_t previewRevision() const noexcept { return previewRevision_; }
    /** @brief Seed. */
    std::uint32_t seed() const noexcept { return seed_; }
    /** @brief Spacing. */
    float         spacing() const noexcept { return spacing_; }
    /** @brief Selected id. */
    std::string   selectedId() const { return selectedId_; }
    /** @brief Selected type. */
    std::string   selectedType() const { return selectedType_; }

    /** @brief Layer count. */
    int         layerCount() const { return static_cast<int>(target_.layers().size()); }
    /** @brief Layer id. */
    std::string layerId(int index) const;
    /** @brief Layer name. */
    std::string layerName(int index) const;
    /** @brief Layer density. */
    float       layerDensity(int index) const;
    /** @brief Layer priority. */
    int         layerPriority(int index) const;
    /** @brief True when layer selected. */
    bool        isLayerSelected(int index) const;

    /** @brief Asset count. */
    int         assetCount() const;
    /** @brief Asset id. */
    std::string assetId(int index) const;
    /** @brief Asset ref. */
    std::string assetRef(int index) const;
    /** @brief Asset weight. */
    float       assetWeight(int index) const;
    /** @brief True when asset selected. */
    bool        isAssetSelected(int index) const;

    /** @brief Exclusion count. */
    int         exclusionCount() const { return static_cast<int>(target_.exclusions().size()); }
    /** @brief Exclusion asset. */
    std::string exclusionAsset(int index) const;

    /** @brief Point count. */
    int         pointCount() const { return static_cast<int>(points_.size()); }
    /** @brief Point x. */
    float       pointX(int index) const;
    /** @brief Point z. */
    float       pointZ(int index) const;
    /** @brief Point asset. */
    std::string pointAsset(int index) const;

private:
    struct PreviewPoint {
        float       x = 0;
        float       z = 0;
        std::string asset;
    };

    class SpatialResolver final : public biome_editing::IBiomeSpatialResolver {
    public:
        /** @brief Constructs a SpatialResolver. */
        SpatialResolver(procgen::SpatialData* forest, procgen::SpatialData* clearing);
        /** @brief Resolve. */
        biome_editing::Result<procgen::SpatialData*> resolve(const std::string& asset) const override;

    private:
        procgen::SpatialData* forest_   = nullptr;
        procgen::SpatialData* clearing_ = nullptr;
    };

    [[nodiscard]] biome_editing::Result<void> commit(
        biome_editing::Result<biome_editing::DomainOperation> operation, std::string label);
    [[nodiscard]] biome_editing::Result<void> refreshPreview();
    void                                            seedPreviewDocument();
    editor::SelectionSnapshot                       selection() const;
    /** @ownership Borrowed layer owned by the document target. @lifetime Valid until the next document mutation or destruction; null when no layer is selected. */
    const biome_editing::BiomeLayerValue*           selectedLayer() const;
    /** @ownership Borrowed asset owned by the selected layer. @lifetime Valid until the next document mutation or destruction; null when index is out of range. */
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
