#pragma once

#include "archspace/ArchSpaceTypes.h"
#include "archspace/editing/ArchSpaceTarget.h"
#include "editing/EditingAuthority.h"
#include "editor/EditorPresentation.h"
#include "editor/EditorTool.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace eve::editor {

/** @brief World-space ray produced by an ArchSpace editor viewport. */
struct ArchSpaceViewportRay {
    std::array<double, 3> origin{0.0, 0.0, 0.0};
    std::array<double, 3> direction{0.0, 0.0, -1.0};
};

/**
 * @brief Host-owned conversion boundary between pointer coordinates and the ArchSpace viewport.
 *
 * Tools invoke this interface synchronously on the editor thread and never retain returned values
 * across callbacks. Implementations must not re-enter the tool.
 */
class IArchSpaceViewportAdapter {
public:
    virtual ~IArchSpaceViewportAdapter() = default;

    /** @brief Build a world ray for one normalized pointer event. */
    virtual editing::Result<ArchSpaceViewportRay> pointerRay(const EditorPointerEvent& event) const = 0;
    /** @brief Project a world position into the coordinate space accepted by IEditorOverlay. */
    virtual editing::Result<OverlayPoint> projectWorld(const std::array<double, 3>& world) const = 0;
};

/**
 * @brief Click-drag wall draw tool. Down starts, Move previews, Up commits `makeCreateWall`.
 *
 * Contiguous mode chains the next start to the previous end. Shift constrains to axis-aligned.
 */
class ArchSpaceWallDrawTool final : public IEditorTool {
public:
    ArchSpaceWallDrawTool(IArchSpaceViewportAdapter* viewport, editing::IEditAuthority* authority);

    const ToolDescriptor& descriptor() const override { return descriptor_; }
    void                  setViewportAdapter(IArchSpaceViewportAdapter* viewport);
    void                  setAuthority(editing::IEditAuthority* authority);
    void                  setLevelId(std::string levelId);
    void                  setWallHeight(double height) { wallHeight_ = height; }
    void                  setWallThickness(double thickness) { wallThickness_ = thickness; }
    void                  setContiguous(bool enabled) { contiguous_ = enabled; }
    void                  setIdPrefix(std::string prefix) { idPrefix_ = std::move(prefix); }

    ToolResponse pointerEvent(EditorContext& context, const EditorPointerEvent& event) override;
    void         deactivate(EditorContext& context) override;
    void         cancel(EditorContext& context) override;
    void         drawOverlay(EditorContext& context, IEditorOverlay& overlay) override;
    void         inspect(EditorContext& context, IEditorInspector& inspector) override;

    [[nodiscard]] bool                                              isDrawing() const { return drawing_; }
    [[nodiscard]] const std::optional<editing::TransactionReceipt>& lastReceipt() const { return lastReceipt_; }
    [[nodiscard]] const std::string&                                lastWallId() const { return lastWallId_; }

private:
    ToolResponse       begin(EditorContext& context, const EditorPointerEvent& event);
    ToolResponse       move(const EditorPointerEvent& event);
    ToolResponse       finish(EditorContext& context, const EditorPointerEvent& event);
    [[nodiscard]] bool hitFloor(const EditorPointerEvent& event, archspace::Vec2& out) const;
    [[nodiscard]] bool commitWall(archspace_editing::ArchSpaceDocumentTarget& target, archspace::Vec2 start,
                                  archspace::Vec2 end);

    ToolDescriptor                             descriptor_{"archspace.wall.draw", "Draw Wall", {}};
    IArchSpaceViewportAdapter*                 viewport_  = nullptr;
    editing::IEditAuthority*                   authority_ = nullptr;
    std::string                                levelId_;
    std::string                                idPrefix_{"wall"};
    double                                     wallHeight_    = 3.0;
    double                                     wallThickness_ = 0.2;
    bool                                       contiguous_    = true;
    bool                                       drawing_       = false;
    bool                                       haveAnchor_    = false;
    archspace::Vec2                            anchor_{};
    archspace::Vec2                            cursor_{};
    std::string                                lastWallId_;
    std::optional<editing::TransactionReceipt> lastReceipt_;
    std::uint64_t                              sequence_ = 0;
};

/** @brief Click-to-place door/window tool committing `makeCreateOpening` on the nearest wall. */
class ArchSpaceOpeningPlaceTool final : public IEditorTool {
public:
    ArchSpaceOpeningPlaceTool(IArchSpaceViewportAdapter* viewport, editing::IEditAuthority* authority);

    const ToolDescriptor& descriptor() const override { return descriptor_; }
    void                  setViewportAdapter(IArchSpaceViewportAdapter* viewport);
    void                  setAuthority(editing::IEditAuthority* authority);
    void                  setOpeningKind(archspace::OpeningKind kind) { kind_ = kind; }
    void                  setWidth(double width) { width_ = width; }
    void                  setHeight(double height) { height_ = height; }
    void                  setSill(double sill) { sill_ = sill; }
    void                  setIdPrefix(std::string prefix) { idPrefix_ = std::move(prefix); }

    ToolResponse pointerEvent(EditorContext& context, const EditorPointerEvent& event) override;
    void         deactivate(EditorContext& context) override;
    void         cancel(EditorContext& context) override;
    void         drawOverlay(EditorContext& context, IEditorOverlay& overlay) override;
    void         inspect(EditorContext& context, IEditorInspector& inspector) override;

    [[nodiscard]] const std::optional<editing::TransactionReceipt>& lastReceipt() const { return lastReceipt_; }
    [[nodiscard]] const std::string&                                lastOpeningId() const { return lastOpeningId_; }

private:
    struct Pick {
        std::string     wallId;
        double          t = 0.5;
        archspace::Vec2 point{};
    };
    [[nodiscard]] bool                hitFloor(const EditorPointerEvent& event, archspace::Vec2& out) const;
    [[nodiscard]] std::optional<Pick> pickWall(const archspace_editing::ArchSpaceDocumentTarget& target,
                                               const archspace::Vec2&                            point) const;

    ToolDescriptor                             descriptor_{"archspace.opening.place", "Place Opening", {}};
    IArchSpaceViewportAdapter*                 viewport_  = nullptr;
    editing::IEditAuthority*                   authority_ = nullptr;
    archspace::OpeningKind                     kind_      = archspace::OpeningKind::Door;
    double                                     width_     = 0.9;
    double                                     height_    = 2.1;
    double                                     sill_      = 0.0;
    std::string                                idPrefix_{"opening"};
    std::optional<Pick>                        preview_;
    std::string                                lastOpeningId_;
    std::optional<editing::TransactionReceipt> lastReceipt_;
    std::uint64_t                              sequence_ = 0;
};

/** @brief Click-to-place furniture tool committing `makePlaceItem` with catalog id + yaw. */
class ArchSpaceItemPlaceTool final : public IEditorTool {
public:
    ArchSpaceItemPlaceTool(IArchSpaceViewportAdapter* viewport, editing::IEditAuthority* authority);

    const ToolDescriptor& descriptor() const override { return descriptor_; }
    void                  setViewportAdapter(IArchSpaceViewportAdapter* viewport);
    void                  setAuthority(editing::IEditAuthority* authority);
    void                  setLevelId(std::string levelId);
    void                  setCatalogId(std::string catalogId);
    void                  setYawDegrees(double yaw) { yawDegrees_ = yaw; }
    void                  setIdPrefix(std::string prefix) { idPrefix_ = std::move(prefix); }

    ToolResponse pointerEvent(EditorContext& context, const EditorPointerEvent& event) override;
    void         deactivate(EditorContext& context) override;
    void         cancel(EditorContext& context) override;
    void         drawOverlay(EditorContext& context, IEditorOverlay& overlay) override;
    void         inspect(EditorContext& context, IEditorInspector& inspector) override;

    [[nodiscard]] const std::optional<editing::TransactionReceipt>& lastReceipt() const { return lastReceipt_; }
    [[nodiscard]] const std::string&                                lastItemId() const { return lastItemId_; }

private:
    [[nodiscard]] bool hitFloor(const EditorPointerEvent& event, archspace::Vec2& out) const;

    ToolDescriptor                             descriptor_{"archspace.item.place", "Place Item", {}};
    IArchSpaceViewportAdapter*                 viewport_  = nullptr;
    editing::IEditAuthority*                   authority_ = nullptr;
    std::string                                levelId_;
    std::string                                catalogId_{"furniture.desk"};
    std::string                                idPrefix_{"item"};
    double                                     yawDegrees_ = 0.0;
    std::optional<archspace::Vec2>             preview_;
    std::string                                lastItemId_;
    std::optional<editing::TransactionReceipt> lastReceipt_;
    std::uint64_t                              sequence_ = 0;
};

}  // namespace eve::editor
