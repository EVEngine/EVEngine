#pragma once
#include "common/Export.h"


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
    /** @brief Releases IArchSpaceViewportAdapter resources. */
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
class EVENGINE_API_EDITORS ArchSpaceWallDrawTool final : public IEditorTool {
public:
    /** @brief Arch space wall draw tool. */
    ArchSpaceWallDrawTool(IArchSpaceViewportAdapter* viewport, editing::IEditAuthority* authority);

    /** @brief Descriptor. */
    const ToolDescriptor& descriptor() const override { return descriptor_; }
    /** @brief Sets the viewport adapter. */
    void                  setViewportAdapter(IArchSpaceViewportAdapter* viewport);
    /** @brief Sets the authority. */
    void                  setAuthority(editing::IEditAuthority* authority);
    /** @brief Sets the level id. */
    void                  setLevelId(std::string levelId);
    /** @brief Sets the wall height. */
    void                  setWallHeight(double height) { wallHeight_ = height; }
    /** @brief Sets the wall thickness. */
    void                  setWallThickness(double thickness) { wallThickness_ = thickness; }
    /** @brief Sets the contiguous. */
    void                  setContiguous(bool enabled) { contiguous_ = enabled; }
    /** @brief Sets the id prefix. */
    void                  setIdPrefix(std::string prefix) { idPrefix_ = std::move(prefix); }

    /** @brief Pointer event. */
    ToolResponse pointerEvent(EditorContext& context, const EditorPointerEvent& event) override;
    /** @brief Deactivate. */
    void         deactivate(EditorContext& context) override;
    /** @brief Cancel. */
    void         cancel(EditorContext& context) override;
    /** @brief Draws overlay. */
    void         drawOverlay(EditorContext& context, IEditorOverlay& overlay) override;
    /** @brief Inspect. */
    void         inspect(EditorContext& context, IEditorInspector& inspector) override;

    /** @brief True when drawing. */
    [[nodiscard]] bool                                              isDrawing() const { return drawing_; }
    /** @brief Last receipt. */
    [[nodiscard]] const std::optional<editing::TransactionReceipt>& lastReceipt() const { return lastReceipt_; }
    /** @brief Last wall id. */
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
class EVENGINE_API_EDITORS ArchSpaceOpeningPlaceTool final : public IEditorTool {
public:
    /** @brief Arch space opening place tool. */
    ArchSpaceOpeningPlaceTool(IArchSpaceViewportAdapter* viewport, editing::IEditAuthority* authority);

    /** @brief Descriptor. */
    const ToolDescriptor& descriptor() const override { return descriptor_; }
    /** @brief Sets the viewport adapter. */
    void                  setViewportAdapter(IArchSpaceViewportAdapter* viewport);
    /** @brief Sets the authority. */
    void                  setAuthority(editing::IEditAuthority* authority);
    /** @brief Sets the opening kind. */
    void                  setOpeningKind(archspace::OpeningKind kind) { kind_ = kind; }
    /** @brief Sets the width. */
    void                  setWidth(double width) { width_ = width; }
    /** @brief Sets the height. */
    void                  setHeight(double height) { height_ = height; }
    /** @brief Sets the sill. */
    void                  setSill(double sill) { sill_ = sill; }
    /** @brief Sets the id prefix. */
    void                  setIdPrefix(std::string prefix) { idPrefix_ = std::move(prefix); }

    /** @brief Pointer event. */
    ToolResponse pointerEvent(EditorContext& context, const EditorPointerEvent& event) override;
    /** @brief Deactivate. */
    void         deactivate(EditorContext& context) override;
    /** @brief Cancel. */
    void         cancel(EditorContext& context) override;
    /** @brief Draws overlay. */
    void         drawOverlay(EditorContext& context, IEditorOverlay& overlay) override;
    /** @brief Inspect. */
    void         inspect(EditorContext& context, IEditorInspector& inspector) override;

    /** @brief Last receipt. */
    [[nodiscard]] const std::optional<editing::TransactionReceipt>& lastReceipt() const { return lastReceipt_; }
    /** @brief Last opening id. */
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
class EVENGINE_API_EDITORS ArchSpaceItemPlaceTool final : public IEditorTool {
public:
    /** @brief Arch space item place tool. */
    ArchSpaceItemPlaceTool(IArchSpaceViewportAdapter* viewport, editing::IEditAuthority* authority);

    /** @brief Descriptor. */
    const ToolDescriptor& descriptor() const override { return descriptor_; }
    /** @brief Sets the viewport adapter. */
    void                  setViewportAdapter(IArchSpaceViewportAdapter* viewport);
    /** @brief Sets the authority. */
    void                  setAuthority(editing::IEditAuthority* authority);
    /** @brief Sets the level id. */
    void                  setLevelId(std::string levelId);
    /** @brief Sets the catalog id. */
    void                  setCatalogId(std::string catalogId);
    /** @brief Sets the yaw degrees. */
    void                  setYawDegrees(double yaw) { yawDegrees_ = yaw; }
    /** @brief Sets the id prefix. */
    void                  setIdPrefix(std::string prefix) { idPrefix_ = std::move(prefix); }

    /** @brief Pointer event. */
    ToolResponse pointerEvent(EditorContext& context, const EditorPointerEvent& event) override;
    /** @brief Deactivate. */
    void         deactivate(EditorContext& context) override;
    /** @brief Cancel. */
    void         cancel(EditorContext& context) override;
    /** @brief Draws overlay. */
    void         drawOverlay(EditorContext& context, IEditorOverlay& overlay) override;
    /** @brief Inspect. */
    void         inspect(EditorContext& context, IEditorInspector& inspector) override;

    /** @brief Last receipt. */
    [[nodiscard]] const std::optional<editing::TransactionReceipt>& lastReceipt() const { return lastReceipt_; }
    /** @brief Last item id. */
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
