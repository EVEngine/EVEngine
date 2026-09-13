#pragma once

#include "editor/EditorWorkspace.h"
#include "procgen/editing/ProcgenEditingTypes.h"

#include <cstdint>
#include <string>

namespace eve::procgen_editor {

/**
 * @brief UI-neutral composition controller for modifier graph, spline, sculpt, and UV-paint tools.
 *
 * The controller owns only stable target ids and observed revisions. Graph, spline, mesh, and image
 * documents remain authoritative in their respective sessions. Owner-thread only; no callbacks.
 */
class MeshModifierEditor {
public:
    /** @brief Construct a controller for one mesh document. */
    explicit MeshModifierEditor(std::string targetId);
    /** @brief Atomically install the complete mesh-modifier panel composition. */
    [[nodiscard]] procgen_editing::EditorResult<void> configureWorkspace(editor::EditorWorkspace& workspace) const;
    /** @brief Activate graph, spline, sculpt, or uvPaint and update semantic focus. */
    [[nodiscard]] procgen_editing::EditorResult<void> activateTool(editor::EditorWorkspace& workspace,
                                                                   std::string tool);
    /** @brief Observe an externally owned document revision without copying its state. */
    [[nodiscard]] procgen_editing::EditorResult<void> observeRevision(std::string document, std::uint64_t revision);
    const std::string& targetId() const noexcept { return targetId_; }
    const std::string& activeTool() const noexcept { return activeTool_; }
    std::uint64_t graphRevision() const noexcept { return graphRevision_; }
    std::uint64_t splineRevision() const noexcept { return splineRevision_; }
    std::uint64_t meshRevision() const noexcept { return meshRevision_; }
    std::uint64_t paintRevision() const noexcept { return paintRevision_; }
private:
    std::string targetId_;
    std::string activeTool_ = "graph";
    std::uint64_t graphRevision_ = 0, splineRevision_ = 0, meshRevision_ = 0, paintRevision_ = 0;
};
}  // namespace eve::procgen_editor
