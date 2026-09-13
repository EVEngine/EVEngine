#pragma once

#include "editing/EditingGizmo.h"
#include "procgen/editing/SplinePathDocument.h"

#include <array>
#include <string>

namespace eve::procgen_editing {

/** @brief Renderer-neutral appearance settings for spline viewport overlays. */
struct SplineGizmoStyle {
    double                nodeSize   = 0.13;
    double                handleSize = 0.10;
    std::array<double, 4> lineColor{0.2, 0.85, 1.0, 1.0};
    std::array<double, 4> textColor{1.0, 1.0, 1.0, 1.0};
    bool                  showPointLabels = true;
};

/** @brief Immutable renderer-neutral spline drag preview. */
struct SplinePathDragPreview {
    EditorStatus           status = EditorStatus::Failed;
    StableId               point;
    std::string            handle;
    SplinePathControlPoint draft;
    editing::GizmoSnapshot gizmo;
};

/** @brief Builds sampled path, anchor and Bezier handle overlay primitives. */
class SplinePathGizmoBuilder {
public:
    /** @brief Build an immutable overlay with bounded path sampling. */
    [[nodiscard]] editing::GizmoSnapshot build(const SplinePathDocument& document, int sampleCount = 64,
                                               int maximumSamples = 4096, SplineGizmoStyle style = {}) const;
};

/**
 * @brief UI-neutral ray picking and transient drag state for a spline document.
 * @ownership Borrows the document; the caller must finish or cancel before destroying it.
 * @thread Viewport/editor thread only; no callbacks are retained or invoked.
 */
class SplinePathDragSession {
public:
    /** @brief Create an idle session borrowing an authoritative spline document. */
    explicit SplinePathDragSession(SplinePathDocument* document, int sampleCount = 64);
    /** @brief Pick the nearest anchor or Bezier handle sphere intersected by a world ray. */
    [[nodiscard]] EditorResult<SplinePathDragPreview> beginDrag(double rayOriginX, double rayOriginY, double rayOriginZ,
                                                                double rayDirectionX, double rayDirectionY,
                                                                double rayDirectionZ);
    /** @brief Project a pointer ray onto the original camera-facing drag plane and update only the draft. */
    [[nodiscard]] EditorResult<SplinePathDragPreview> updateDrag(double rayOriginX, double rayOriginY,
                                                                 double rayOriginZ, double rayDirectionX,
                                                                 double rayDirectionY, double rayDirectionZ);
    /** @brief Produce one reversible point replacement operation without applying it. */
    [[nodiscard]] EditorResult<DomainOperation> finishDrag();
    /** @brief Discard transient state without changing the document. */
    void cancelDrag();
    /** @brief Report whether a drag owns transient state. */
    [[nodiscard]] bool isDragging() const noexcept { return dragging_; }
    /** @brief Return the active stable point id, or empty when idle. */
    [[nodiscard]] StableId activePoint() const { return activePoint_; }
    /** @brief Return anchor, in, or out for the active handle. */
    [[nodiscard]] const std::string& activeHandle() const noexcept { return activeHandle_; }

private:
    [[nodiscard]] EditorResult<SplinePathDragPreview> previewCurrent() const;
    SplinePathDocument*                               document_     = nullptr;
    int                                               sampleCount_  = 64;
    Revision                                          baseRevision_ = 0;
    StableId                                          activePoint_;
    std::string                                       activeHandle_;
    SplinePathControlPoint                            draft_;
    std::array<double, 3>                             dragPlanePoint_{0.0, 0.0, 0.0};
    std::array<double, 3>                             dragPlaneNormal_{0.0, 0.0, 1.0};
    bool                                              dragging_ = false;
};

}  // namespace eve::procgen_editing
