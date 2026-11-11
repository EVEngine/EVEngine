#pragma once
/** @file SpawnEditorWorkspace.h
 * @brief spawn_editor host composition face: actual form panel, instance/shape controls and map viewport routing.
 */
#include "map/spawn/editor/SpawnMapViewport.h"
namespace eve::ui {
class Component;
}
namespace eve::spawn_editor {
/** @brief Owning configuration editor composition; independent from runtime world/save authority.
 * @ownership Shares the actual form, owns its panel/controls/selected viewport, copies the explicit map transform.
 * Component references expire at destruction; control callbacks retain safe state and reject a closed workspace.
 * No Map, renderer, runtime provider or ECS pointer is retained. Host owns hit-testing, background and panel mounting.
 * @thread UI/authoring owner thread; workspace selection/frame/input mutations reject renderer/commit callback reentry.
 * Independent form edits invalidate gestures through existing revision checks. Do not destroy an active workspace from
 * callbacks. Recreate after map identity or binding replacement.
 */
class EVENGINE_API_EDITORS SpawnEditorWorkspace final {
public:
    /** @brief Unregister local tools and detach UI observation before releasing owned state; no runtime cleanup. */
    ~SpawnEditorWorkspace();
    SpawnEditorWorkspace(const SpawnEditorWorkspace&)            = delete;
    SpawnEditorWorkspace& operator=(const SpawnEditorWorkspace&) = delete;
    /** @brief Prepare a form panel and controls without requiring any spawn instance or selecting one implicitly.
     * @return Owning composition or invalid form/map binding/frame error; does not mount or mutate configuration.
     * @cost Full form property snapshot plus UI owner allocation per workspace creation; source selection prepares
     * its viewport through the existing explicit SpawnMapViewport builder only when the user selects an instance.
     */
    [[nodiscard]] static Result<std::unique_ptr<SpawnEditorWorkspace>> create(
        std::shared_ptr<SpawnForm> form, const map_spawn::PixelTransform& transform, MapViewportFrame frame);
    /** @brief Borrow instance/shape/frame controls until workspace destruction. */
    [[nodiscard]] ui::Component& controlsPanel() noexcept;
    /** @brief Borrow the actual source form component until workspace destruction. */
    [[nodiscard]] ui::Component& formPanel() noexcept;
    /** @brief Share the authoritative source form; runtime diagnostics remain a separate read-only participant. */
    [[nodiscard]] std::shared_ptr<SpawnForm> form() const noexcept;
    /** @brief Copy the single frame used for selected viewport input/overlay and host map-background drawing. */
    [[nodiscard]] MapViewportFrame frame() const noexcept;
    /** @brief Copy the bound immutable pixel transform; no callbacks or rebuild. */
    [[nodiscard]] map_spawn::PixelTransform pixelTransform() const noexcept;
    /** @brief Validate and publish a frame, cancelling a selected gesture without altering the source.
     * @return Success or stale binding/invalid frame/reentry failure preserving previous frame.
     * @cost Constant frame admission plus retirement of an active gesture's owning source/vertices per change.
     */
    [[nodiscard]] Result<void> setFrame(MapViewportFrame frame);
    /** @brief Route host-hit-tested canvas input to the selected actual viewport.
     * @return Protocol response or missing selection/projection/reentry failure; interaction diagnostics are visible.
     * @cost Constant routing plus the existing tool start/commit costs; moves perform no source lookup here.
     */
    [[nodiscard]] Result<editor::ToolResponse> pointerEvent(const editor::EditorPointerEvent& event);
    /** @brief Route canvas-focused keyboard interaction to the selected actual viewport or return a structured
     * selection error. */
    [[nodiscard]] Result<editor::ToolResponse> keyEvent(const editor::EditorKeyEvent& event);
    /** @brief Cancel the actual gesture/tool interaction, including polygon drafts; no selection is a no-op.
     * @return Existing Escape response/capture release hint, or reentry/cancellation failure without source commit.
     */
    [[nodiscard]] Result<editor::ToolResponse> cancelInteraction();
    /** @brief Draw all committed typed regions in cyan, then selected tool feedback in screen space.
     * @return Viewport projection/callback result; never changes runtime or configured units.
     * @cost Linear in committed region vertices and gesture samples per frame; constant immutable owner acquisition, no
     * JSON parsing or resolver callbacks.
     */
    [[nodiscard]] Result<void> drawOverlay(editor::IEditorOverlay& overlay);
    /** @brief Read actual selected-session pointer capture, false when no instance is selected. */
    [[nodiscard]] bool hasPointerCapture() const noexcept;
    /** @brief Read the actual form panel's explicit structure draft fact; no source copy or duplicate dirty flag. */
    [[nodiscard]] bool hasStructureDraft() const noexcept;
    /** @brief Copy visible configuration/viewport interaction diagnostics.
     * @cost Owning diagnostic copy per host refresh, amortized per interaction.
     */
    [[nodiscard]] std::vector<Diagnostic> diagnostics() const;

private:
    struct Impl;
    explicit SpawnEditorWorkspace(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::spawn_editor
