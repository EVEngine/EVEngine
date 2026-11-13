#pragma once
/** @file SpawnMapViewport.h
 * @brief spawn_editor map viewport face: explicit map-pixel/screen projection and owning region-tool session.
 */
#include <memory>
#include <string>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
#include "editor/EditorTool.h"
namespace eve::map_spawn {
struct PixelTransform;
}
namespace eve::spawn_editor {
class SpawnForm;
enum class RegionShape;
/** @brief Axis-aligned map viewport: screen origin, map-pixel pan and positive screen-pixels/map-pixel zoom. */
struct MapViewportFrame {
    double screenX = 0, screenY = 0, panX = 0, panY = 0, zoom = 1;
};
/** @brief Own the existing editor session and all three World2D tools behind an explicit map viewport transform.
 * @ownership Shares the real form, copies pixel transform/frame, and owns tools/session. No live Map, camera,
 * runtime session, renderer, subscription or ECS pointer is retained. Recreate on map/binding replacement; source
 * map-id changes reject new gestures. Host draws its map with the same frame and routes only its viewport input; hit
 * testing/clipping remain host-owned.
 * @thread Authoring/UI owner thread; mutations reject reentry, including renderer callbacks. Do not destroy the
 * active viewport in callbacks. Read-only capture/diagnostic queries are permitted.
 */
class EVENGINE_API_EDITORS SpawnMapViewport final {
public:
    /** @brief Unregister tools before releasing them and the form; no gameplay cleanup. */
    ~SpawnMapViewport();
    SpawnMapViewport(const SpawnMapViewport&)            = delete;
    SpawnMapViewport& operator=(const SpawnMapViewport&) = delete;
    /** @brief Prepare all region tools using the explicit map pixel transform (for example session.pixelTransform()).
     * @return Owning viewport or invalid frame/transform/form/instance error; no registration in global editor state.
     * @cost Three source-admitted tool snapshots per viewport creation; pan/zoom updates do not reconstruct tools.
     */
    [[nodiscard]] static Result<std::unique_ptr<SpawnMapViewport>> create(std::shared_ptr<SpawnForm>       form,
                                                                          std::string                      instanceId,
                                                                          const map_spawn::PixelTransform& transform,
                                                                          MapViewportFrame                 frame);
    /** @brief Change pan/zoom after validating it, then cancel the current gesture and release pointer capture.
     * @return Success or invalid frame/reentry failure preserving the old frame and gesture.
     * @cost Constant frame validation; cancelling an active gesture retires its captured source and vertices once.
     */
    [[nodiscard]] Result<void> setFrame(MapViewportFrame frame);
    /** @brief Select one already-prepared shape, cancelling the previous gesture.
     * @return Success or unknown shape/reentry error; no source compilation or publication.
     * @cost Fixed tool selection plus retirement of the previous gesture source/vertices, amortized per selection.
     */
    [[nodiscard]] Result<void> selectShape(RegionShape shape);
    /** @brief Convert a screen sample through map pixels to World2D and dispatch through actual EditorSession capture.
     * @remarks Positions round to the existing float pointer protocol after finite/overflow checks.
     * @return Tool response or projection/reentry error; invalid release coordinates cancel capture without committing.
     * @cost Constant coordinate conversion; source copying/commit follows the tool's start/release boundary.
     */
    [[nodiscard]] Result<editor::ToolResponse> pointerEvent(const editor::EditorPointerEvent& event);
    /** @brief Route keyboard input through the actual editor session.
     * @return Protocol response or reentry failure; commit diagnostics are available through diagnostics().
     */
    [[nodiscard]] Result<editor::ToolResponse> keyEvent(const editor::EditorKeyEvent& event);
    /** @brief Project tool World2D feedback into the host's screen-space overlay sink using one captured frame.
     * @return Success or projection/reentry failure. Presentation may be partial on projection failure; source is
     * untouched.
     * @remarks Unknown sink callbacks run without locks; viewport mutations are rejected during this call.
     * @cost Linear in gesture samples per overlay frame; no map/source lookup or parsing.
     */
    [[nodiscard]] Result<void> drawOverlay(editor::IEditorOverlay& screenOverlay);
    /** @brief Read the actual session's capture fact. */
    [[nodiscard]] bool hasPointerCapture() const noexcept;
    /** @brief Copy current tool interaction diagnostics; projection failures are returned directly by the API.
     * @cost Owning diagnostic copy per host refresh, amortized per attempted interaction.
     */
    [[nodiscard]] std::vector<Diagnostic> diagnostics() const;

private:
    struct Impl;
    explicit SpawnMapViewport(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::spawn_editor
