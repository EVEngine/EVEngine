#pragma once
#include "building_editing/BuildingTarget.h"
#include "editor/EditorPresentation.h"
#include "editor/EditorTool.h"

#include <memory>
#include <optional>

namespace eve::editor {
using BuildingGarrisonMemberRecord=eve::building_editing::BuildingGarrisonMemberRecord;
using BuildingInstanceSnapshot=eve::building_editing::BuildingInstanceSnapshot;
using BuildingFootprintCell=eve::building_editing::BuildingFootprintCell;
using BuildingPlacementPreview=eve::building_editing::BuildingPlacementPreview;
using BuildingPlacementTarget=eve::building_editing::BuildingPlacementTarget;

/** @brief World-space ray produced by a building editor viewport. */
struct BuildingViewportRay {
    std::array<double, 3> origin{0.0, 0.0, 0.0};
    std::array<double, 3> direction{0.0, 0.0, -1.0};
};

/**
 * @brief Host-owned conversion boundary between pointer coordinates and a 2D/3D viewport.
 *
 * The tool invokes this interface synchronously on the editor thread and never retains values
 * returned by it across callbacks. Implementations must not re-enter the tool.
 */
class IBuildingViewportAdapter {
public:
    virtual ~IBuildingViewportAdapter() = default;

    /** @brief Build a world ray for one normalized pointer event. */
    virtual editing::Result<BuildingViewportRay> pointerRay(
        const EditorPointerEvent& event) const = 0;
    /** @brief Project a world position into the coordinate space accepted by IEditorOverlay. */
    virtual editing::Result<OverlayPoint> projectWorld(
        const std::array<double, 3>& world) const = 0;
};

/** @brief Immutable selection/configuration consumed by BuildingEdgeCurveTool. */
struct BuildingEdgeCurveToolSelection {
    std::string buildingId;
    int memberInstanceId = 0;
    std::vector<building_editing::BuildingEdgeCurvePoint> controlPoints;
    int subdivisions = 16;
    std::string surfaceName;
};

/**
 * @brief EditorSession tool that turns one curve-handle drag into one authority transaction.
 *
 * @ownership Borrows viewport and authority; both must outlive the tool or be explicitly cleared.
 * The selected BuildingPlacementTarget is borrowed only during EditorSession callbacks. Preview
 * state is tool-owned and never mutates the target until pointer-up authority commit succeeds.
 * @thread Editor/viewport thread only.
 */
class BuildingEdgeCurveTool final : public IEditorTool {
public:
    BuildingEdgeCurveTool(IBuildingViewportAdapter* viewport,
                          editing::IEditAuthority* authority);

    const ToolDescriptor& descriptor() const override { return descriptor_; }
    /** @brief Replace the host viewport adapter and cancel any active gesture. @param viewport Borrowed adapter, or null to detach. */
    void setViewportAdapter(IBuildingViewportAdapter* viewport);
    /** @brief Replace the transaction authority and cancel any active gesture. @param authority Borrowed authority, or null to detach. */
    void setAuthority(editing::IEditAuthority* authority);
    /** @brief Select one authoritative curve group member for handle editing. @param selection Owning edit configuration. @return Applied or a structured validation failure. */
    [[nodiscard]] editing::Result<void> setSelection(BuildingEdgeCurveToolSelection selection);
    /** @brief Clear selection and any uncommitted gesture. */
    void clearSelection();

    ToolResponse pointerEvent(EditorContext& context,
                              const EditorPointerEvent& event) override;
    void deactivate(EditorContext& context) override;
    void cancel(EditorContext& context) override;
    void drawOverlay(EditorContext& context, IEditorOverlay& overlay) override;

    /** @brief Whether a handle currently owns pointer-drag state. @return True only between accepted down and release/cancel. */
    bool isDragging() const;
    /** @brief Last valid transient preview. @return Tool-owned preview, when available. */
    const std::optional<building_editing::BuildingEdgeCurveDragPreview>& preview() const {
        return preview_;
    }
    /** @brief Last committed authority receipt. @return Tool-owned receipt, when a commit succeeded. */
    const std::optional<editing::TransactionReceipt>& lastReceipt() const {
        return lastReceipt_;
    }
    /** @brief Status of the latest tool operation. @return Structured editing status. */
    editing::Status lastStatus() const { return lastStatus_; }
    /** @brief Diagnostics from the latest preview, adapter or authority operation. @return Tool-owned diagnostics. */
    const std::vector<editing::Diagnostic>& diagnostics() const { return diagnostics_; }

private:
    void recordFailure(const editing::Status& status,
                       const std::vector<editing::Diagnostic>& diagnostics);
    void recordPreview(building_editing::BuildingEdgeCurveDragPreview preview);
    ToolResponse begin(EditorContext& context, const EditorPointerEvent& event);
    ToolResponse move(const EditorPointerEvent& event);
    ToolResponse finish(EditorContext& context, const EditorPointerEvent& event);

    ToolDescriptor descriptor_{"building.edge-curve", "Edit Building Curve", {}};
    IBuildingViewportAdapter* viewport_ = nullptr;
    editing::IEditAuthority* authority_ = nullptr;
    std::optional<BuildingEdgeCurveToolSelection> selection_;
    std::unique_ptr<building_editing::BuildingEdgeCurveDragSession> drag_;
    std::optional<building_editing::BuildingEdgeCurveDragPreview> preview_;
    std::optional<editing::TransactionReceipt> lastReceipt_;
    editing::Status lastStatus_ = editing::Status::NotFound;
    std::vector<editing::Diagnostic> diagnostics_;
    std::uint64_t transactionSequence_ = 0;
};
}
