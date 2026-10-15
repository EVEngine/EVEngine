#pragma once
#include "common/Export.h"


/**
 * @file VoxelCatalogEditor.h
 * @brief MagicaVoxel-style miniature sculpt editor: orbit view, attach/erase, undo.
 */

#include "editor/EditorAuthority.h"
#include "editor/EditorSelection.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorWorkspace.h"
#include "voxel/editing/VoxelCatalog.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::voxel_editor {

/** @brief MagicaVoxel-like brush: attach on the previous cell, or erase the hit cell. */
enum class VoxelSculptTool { Attach, Erase };

/**
 * @brief Workspace controller for a MagicaVoxel-style voxel sculpt project.
 *
 * Owns the catalog of bounded occupancy models, local undo, and sculpt tools.
 * The presenter renders with `eve.Voxel()` and picks with a camera ray.
 *
 * @ownership Editor owns the document. Screen getters copy values.
 * @threadaffinity Owner thread only.
 * @reentrancy No unknown callbacks.
 */
class EVENGINE_API_EDITORS VoxelCatalogEditor {
public:
    /** @brief Construct a seeded solid cube and a sculpted bed. */
    explicit VoxelCatalogEditor(std::string targetId);

    VoxelCatalogEditor(const VoxelCatalogEditor&)            = delete;
    VoxelCatalogEditor& operator=(const VoxelCatalogEditor&) = delete;

    /** @brief Target. */
    const voxel_editing::VoxelCatalogTarget& target() const noexcept { return target_; }

    /**
     * @brief Install Models / Viewport / Tools / Inspector panels.
     * @note Does not retain @p workspace.
     */
    [[nodiscard]] voxel_editing::Result<void> configureWorkspace(editor::EditorWorkspace& workspace) const;

    [[nodiscard]] voxel_editing::Result<void> selectModel(std::string id);
    [[nodiscard]] voxel_editing::Result<void> setTool(std::string tool);
    [[nodiscard]] voxel_editing::Result<void> setViewport(float width, float height);
    [[nodiscard]] voxel_editing::Result<void> orbit(float yawDelta, float pitchDelta);
    [[nodiscard]] voxel_editing::Result<void> pointerDown(float x, float y);
    /**
     * @brief MagicaVoxel attach/erase using a world-space camera ray.
     * @param ox Ray origin X in model space.
     * @param oy Ray origin Y in model space.
     * @param oz Ray origin Z in model space.
     * @param dx Ray direction X.
     * @param dy Ray direction Y.
     * @param dz Ray direction Z.
     */
    [[nodiscard]] voxel_editing::Result<void> pointerWorldRay(float ox, float oy, float oz, float dx, float dy,
                                                                    float dz);
    /** @brief Sets the voxel. */
    [[nodiscard]] voxel_editing::Result<void> setVoxel(int x, int y, int z, bool occupied);
    /** @brief Sets the selected socket. */
    [[nodiscard]] voxel_editing::Result<void> setSelectedSocket(std::string tag, std::string kind);
    /** @brief Select face. */
    [[nodiscard]] voxel_editing::Result<void> selectFace(int face);
    /** @brief Creates model. */
    [[nodiscard]] voxel_editing::Result<void> createModel(std::string id, std::string name, int sizeX, int sizeY,
                                                                int sizeZ);
    /** @brief Delete selected model. */
    [[nodiscard]] voxel_editing::Result<void> deleteSelectedModel();

    /** @brief Undo. */
    [[nodiscard]] voxel_editing::Result<editor::TransactionReceipt> undo();
    /** @brief Redo. */
    [[nodiscard]] voxel_editing::Result<editor::TransactionReceipt> redo();

    /** @brief Can undo. */
    bool          canUndo() const noexcept { return transactions_.canUndo(); }
    /** @brief Can redo. */
    bool          canRedo() const noexcept { return transactions_.canRedo(); }
    /** @brief Revision. */
    std::uint64_t revision() const noexcept { return target_.revision(); }
    /** @brief Preview revision. */
    std::uint64_t previewRevision() const noexcept { return previewRevision_; }
    /** @brief Selected id. */
    std::string   selectedId() const { return selectedId_; }
    /** @brief Tool name. */
    std::string   toolName() const;
    /** @brief Selected face. */
    int           selectedFace() const noexcept { return selectedFace_; }
    /** @brief Orbit yaw. */
    float         orbitYaw() const noexcept { return yaw_; }
    /** @brief Orbit pitch. */
    float         orbitPitch() const noexcept { return pitch_; }

    /** @brief Model count. */
    int         modelCount() const { return static_cast<int>(target_.models().size()); }
    /** @brief Model id. */
    std::string modelId(int index) const;
    /** @brief Model name. */
    std::string modelName(int index) const;
    /** @brief Model fill. */
    std::string modelFill(int index) const;
    /** @brief True when model selected. */
    bool        isModelSelected(int index) const;

    /** @brief Voxel count. */
    int voxelCount() const;
    /** @brief Voxel x. */
    int voxelX(int index) const;
    /** @brief Voxel y. */
    int voxelY(int index) const;
    /** @brief Voxel z. */
    int voxelZ(int index) const;
    /** @brief Model size x. */
    int modelSizeX() const;
    /** @brief Model size y. */
    int modelSizeY() const;
    /** @brief Model size z. */
    int modelSizeZ() const;

    /** @brief Screen voxel count. */
    int   screenVoxelCount() const { return static_cast<int>(screen_.size()); }
    /** @brief Screen voxel x. */
    float screenVoxelX(int index) const;
    /** @brief Screen voxel y. */
    float screenVoxelY(int index) const;
    /** @brief Screen voxel w. */
    float screenVoxelW(int index) const;
    /** @brief Screen voxel h. */
    float screenVoxelH(int index) const;

    /** @brief Selected socket tag. */
    std::string selectedSocketTag() const;
    /** @brief Selected socket kind. */
    std::string selectedSocketKind() const;
    /** @brief Join partner count. */
    int         joinPartnerCount() const { return static_cast<int>(joinPartners_.size()); }
    /** @brief Join partner id. */
    std::string joinPartnerId(int index) const;

private:
    struct ScreenVoxel {
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;
        float depth = 0;
    };

    [[nodiscard]] voxel_editing::Result<void> commit(
        voxel_editing::Result<voxel_editing::DomainOperation> operation, std::string label);
    [[nodiscard]] voxel_editing::Result<void> refreshPreview();
    void                                            seedProject();
    void                                            rebuildScreen();
    [[nodiscard]] voxel_editing::Result<void> applyPick(const voxel_editing::VoxelPick& pick);
    void                                            cameraAxes(float& fx, float& fy, float& fz, float& rx, float& ry,
                                                               float& rz, float& ux, float& uy, float& uz) const;
    editor::SelectionSnapshot                       selection() const;
    /**
     * @brief Selected sculpted model.
     * @ownership Borrowed from the catalog document; callers must not delete it.
     * @lifetime Valid until the next catalog mutation or editor destruction.
     * @thread Owner-thread only.
     */
    const voxel_editing::VoxelModelValue* selectedModel() const;
    /**
     * @brief Stable fill-kind name for script and inspector labels.
     * @ownership Observed static string; callers must not free it.
     * @lifetime Valid for the process lifetime.
     * @thread Any.
     */
    static const char* fillName(voxel_editing::VoxelCellFill fill);

    voxel_editing::VoxelCatalogTarget    target_;
    editor::LocalWorldAuthority          authority_;
    editor::LocalTransactionBackend      transactions_;
    std::vector<ScreenVoxel>             screen_;
    std::vector<voxel_editing::ObjectId> joinPartners_;
    std::string                          selectedId_      = "cube";
    VoxelSculptTool                      tool_            = VoxelSculptTool::Attach;
    int                                  selectedFace_    = 0;
    float                                viewportW_       = 640.0f;
    float                                viewportH_       = 400.0f;
    float                                yaw_             = 0.7f;
    float                                pitch_           = 0.45f;
    float                                distance_        = 18.0f;
    std::uint64_t                        txSequence_      = 0;
    std::uint64_t                        previewRevision_ = 0;
};

}  // namespace eve::voxel_editor
