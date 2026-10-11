#pragma once

#include "editor/EditorWorkspace.h"
#include "procgen/editing/ProcgenEditingTypes.h"

#include <cstdint>
#include <string>

namespace eve::procgen_editor {

/**
 * @brief UI-neutral composition controller for modifier graph, spline, sculpt, UV-paint, merge, and adhere tools.
 *
 * The controller owns only stable target ids and observed revisions. Graph, spline, mesh, and image
 * documents remain authoritative in their respective sessions. Owner-thread only; no callbacks.
 */
class EVENGINE_API_EDITORS MeshModifierEditor {
public:
    /** @brief Construct a controller for one mesh document. */
    explicit MeshModifierEditor(std::string targetId);
    /** @brief Atomically install the complete mesh-modifier panel composition. */
    [[nodiscard]] procgen_editing::Result<void> configureWorkspace(editor::EditorWorkspace& workspace) const;
    /** @brief Activate graph, spline, sculpt, uvPaint, merge, or adhere and update semantic focus. */
    [[nodiscard]] procgen_editing::Result<void> activateTool(editor::EditorWorkspace& workspace,
                                                                   std::string tool);
    /** @brief Observe an externally owned document revision without copying its state. */
    [[nodiscard]] procgen_editing::Result<void> observeRevision(std::string document, std::uint64_t revision);
    const std::string& targetId() const noexcept { return targetId_; }
    /** @brief Active tool. */
    const std::string& activeTool() const noexcept { return activeTool_; }
    /** @brief Graph revision. */
    std::uint64_t graphRevision() const noexcept { return graphRevision_; }
    /** @brief Spline revision. */
    std::uint64_t splineRevision() const noexcept { return splineRevision_; }
    /** @brief Mesh revision. */
    std::uint64_t meshRevision() const noexcept { return meshRevision_; }
    /** @brief Paint revision. */
    std::uint64_t paintRevision() const noexcept { return paintRevision_; }
private:
    std::string targetId_;
    std::string activeTool_ = "graph";
    std::uint64_t graphRevision_ = 0, splineRevision_ = 0, meshRevision_ = 0, paintRevision_ = 0;
};
}  // namespace eve::procgen_editor
