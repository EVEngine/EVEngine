#pragma once
/** @file SpawnEditorHost.h
 * @brief spawn_editor native host face: owned panel mounts, canvas hit testing and focus/capture routing.
 */
#include <memory>
#include <span>
#include <string>
#include "map/spawn/editor/SpawnMapViewport.h"
union SDL_Event;
namespace ecs {
class Table;
struct EntityHandle;
}  // namespace ecs
namespace eve::filesystem {
class AtomicTextWriteLease;
}
namespace eve::ui {
class Component;
}
namespace eve::graphics {
class Graphics;
class Canvas;
}  // namespace eve::graphics
namespace eve::map {
struct TileRenderViewport;
}
namespace eve::spawn_editor {
class PreparedTiledSpawnImport;
class SpawnAssetDocument;
class SpawnEditorWorkspace;
/** @brief Fixed screen-space host rectangle; dimensions must be finite and positive. */
struct SpawnHostRect {
    float x = 0, y = 0, width = 1, height = 1;
};
/** @brief Explicit non-overlapping canvas and panel rectangles; no second camera state. */
struct SpawnHostLayout {
    SpawnHostRect canvas, controls, source, save, exportSource;
};
/** @brief Own mounted configuration panels and route only canvas-focused input to the actual workspace.
 * @ownership Shares real UI Table/asset owners, observes the provider-specific atomic writer, and owns
 * components/mounts. No Map, renderer or runtime world pointer is retained. UI world may retire mounts first; queries
 * then reject stale handles, teardown resolves each handle anew. Host destruction closes host actions before destroying
 * its own mounts; retained form callbacks follow SpawnFormPanel ownership.
 * @thread UI/authoring owner thread, outside ECS Views/deferred mutation. No locks; do not destroy/reenter the host
 * from input/overlay callbacks. Rendering background and OS event translation remain with the application.
 */
class EVENGINE_API_EDITORS SpawnEditorHost final {
public:
    /** @brief Close component callbacks, detach observation, then destroy owned live UI hosts. */
    ~SpawnEditorHost();
    SpawnEditorHost(const SpawnEditorHost&)            = delete;
    SpawnEditorHost& operator=(const SpawnEditorHost&) = delete;
    /** @brief Prepare the real workspace/save/export components and mount four independent native UI panels.
     * @return Owning host or invalid owners/layout/binding error, with no surviving partial mounts on failure.
     * @ownership Copies explicit layout/transform; shares table/asset owners and captures the weak writer before
     * mounting.
     * @remarks Panel titles are presentation metadata; transient ImGui scopes separate simultaneous hosts. Canvas
     * origin must equal the workspace frame origin; all rectangles must be disjoint. Filesystem must be used on this
     * same owner thread.
     * @cost Full form binding, four UI ECS admissions and initial panel scheduling per host creation.
     */
    [[nodiscard]] static Result<std::unique_ptr<SpawnEditorHost>> create(std::shared_ptr<ecs::Table>         uiWorld,
                                                                         std::shared_ptr<SpawnAssetDocument> asset,
                                                                         filesystem::AtomicTextWriteLease    filesystem,
                                                                         const map_spawn::PixelTransform&    transform,
                                                                         MapViewportFrame frame, SpawnHostLayout layout,
                                                                         std::string exportPath);
    /** @brief Borrow the actual workspace until host destruction.
     * @remarks Host hit-testing uses the fixed canvas rectangle; pan/zoom remain workspace-owned.
     */
    [[nodiscard]] SpawnEditorWorkspace& workspace() noexcept;
    /** @brief Borrow actual mounted save controls until host destruction. */
    [[nodiscard]] ui::Component& savePanel() noexcept;
    /** @brief Borrow actual mounted export controls until host destruction. */
    [[nodiscard]] ui::Component& exportPanel() noexcept;
    /** @brief Copy immutable host layout; no source or camera lookup. */
    [[nodiscard]] SpawnHostLayout layout() const noexcept;
    /** @brief Route hit-tested canvas input; captured Up/Cancel always reaches the workspace even outside canvas.
     * @param blockedByUi True when another UI surface owns this sample; never starts a canvas gesture in that case.
     * @return Actual tool response, ignored off-canvas sample, or stale mount/reentry/tool error.
     * @cost Constant hit test/routing plus existing workspace start/commit costs per input event.
     */
    [[nodiscard]] Result<editor::ToolResponse> pointerEvent(const editor::EditorPointerEvent& event, bool blockedByUi);
    /** @brief Route keys only while canvas focused; captured gestures continue receiving cancellation keys.
     * @return Actual tool response, ignored unfocused key, or structured failure.
     */
    [[nodiscard]] Result<editor::ToolResponse> keyEvent(const editor::EditorKeyEvent& event);
    /** @brief Translate a host-window SDL mouse/key/focus event into the existing normalized routing protocol.
     * @param blockedMouse True when UI owns mouse input; captured release/cancel still reaches its tool.
     * @param blockedKeyboard True when UI owns keyboard input; only captured Escape may override this fact.
     * @return Actual tool response/capture hints, ignored unsupported/touch-synthetic event, or conversion failure.
     * @ownership Borrows event synchronously; no window/event pointer or SDL queue is retained.
     * @thread UI owner thread. Caller filters window id, feeds UI first, and applies returned native capture hints.
     * @remarks Focus loss/hide/close and SDL_QUIT cancel without source publication. Return/keypad Enter,
     * Escape and Backspace are supported; text/wheel/touch events remain with their existing consumers.
     * @cost Constant translation and existing workspace start/commit work per supported event; no per-frame lookup.
     */
    [[nodiscard]] Result<editor::ToolResponse> processSdlEvent(const SDL_Event& event, bool blockedMouse,
                                                               bool blockedKeyboard);
    /** @brief Describe the canonical offscreen map-pixel camera for the current frame and target size.
     * @return Explicit viewport or aspect/size/projection failure; supports uniform high-DPI target scaling.
     * @remarks Uses the single workspace frame and fixed screen-space canvas; no ECS or GPU mutation.
     */
    [[nodiscard]] Result<map::TileRenderViewport> backgroundViewport(int targetWidth, int targetHeight) const;
    /** @brief Draw selected real map layers into the caller's canvas through canonical tile/render systems.
     * @return Success or stale host/layer/projection error before drawing; render exceptions propagate.
     * @ownership Borrows layers, graphics and target synchronously; retains no GPU/map pointers or draw queue.
     * @lifetime Target, graphics and layer resources must remain live without mutation/reentry until return.
     * @thread Render/authoring owner thread with the selected layers' ECS world current; outside structural mutation.
     * @remarks Clears the target, overrides only frame-local draw items, uses the target bounds for clipping, and
     * restores the prior graphics canvas on success or attempts restoration before propagating an exception.
     * Caller presents the target in layout().canvas; this operation does not present an OS window or draw UI/overlay.
     * @cost Canonical visible selected chunk collection and drawing per frame, proportional to emitted tile items.
     * @throws std::bad_alloc or backend exceptions; source/layer/camera components remain unchanged.
     */
    [[nodiscard]] Result<void> renderBackground(std::span<const ecs::EntityHandle> layers, graphics::Graphics& graphics,
                                                graphics::Canvas& target);
    /** @brief Draw real tool feedback onto the same offscreen target without clearing its map background.
     * @return Projection/recording/text-font failure before drawing, or success restoring the prior graphics canvas.
     * @ownership Borrows graphics/target synchronously; all primitive/text commands are frame-local owning copies.
     * @param proposal Optional call-only candidate, live without outer mutation until return; null draws ordinary
     * feedback.
     * @remarks A proposal overlays immutable imported regions using the workspace frame; no source publication.
     * @lifetime Graphics/target must remain live without reentry/mutation until return; no GPU pointers are retained.
     * @thread Render/UI owner thread. Uses canonical PrimitiveCanvas2D and the single workspace/layout projection.
     * @remarks Overlay colors are 0xAARRGGBB. Text annotations require graphics.getFont(); absent font rejects text
     * explicitly. Target bounds clip primitives; no runtime state or source/history is changed.
     * @cost Tool preview copy, canonical primitive tessellation and drawing proportional to samples per overlay frame.
     * @throws std::bad_alloc or backend exceptions; restoration is attempted before propagating draw exceptions.
     */
    [[nodiscard]] Result<void> renderOverlay(graphics::Graphics& graphics, graphics::Canvas& target,
                                             const PreparedTiledSpawnImport* proposal = nullptr);
    /** @brief Emit actual screen-space workspace feedback; application supplies clipping/background renderer.
     * @return Existing viewport result or stale mount/reentry error without source changes.
     * @cost Linear in existing gesture preview samples per overlay frame.
     */
    [[nodiscard]] Result<void> drawOverlay(editor::IEditorOverlay& overlay);

private:
    struct Impl;
    explicit SpawnEditorHost(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::spawn_editor
