#pragma once
/** @file SpawnEditorApplication.h
 * @brief spawn_editor module-interface composition: private map/UI world and asset-backed native host.
 */
#include <memory>
#include <string>
#include "common/Export.h"
#include "common/Result.h"
union SDL_Event;
struct SDL_Window;
namespace ecs {
class Table;
}
namespace eve {
class Value;
}
namespace eve::service {
class IFileSystem;
}
namespace eve::filesystem {
class AtomicTextWriteLease;
}
namespace eve::editor {
class IAtomicDocumentStore;
struct DocumentKey;
struct ToolResponse;
}  // namespace eve::editor
namespace eve::spawn {
class ITemplateResolver;
class IUnitDefinitionResolver;
}  // namespace eve::spawn
namespace eve::graphics {
class Graphics;
class Canvas;
class PrimitiveCanvas2D;
}  // namespace eve::graphics
namespace eve::spawn_editor {
class PreparedTiledSpawnImport;
class SpawnAssetDocument;
class SpawnEditorHost;
/** @brief Independent actual authoring facts that must be resolved before normal close; no cached dirty state. */
struct SpawnEditorCloseState {
    bool unsavedSource  = false;
    bool structureDraft = false;
    bool pendingCommit  = false;
    bool pointerCapture = false;
};
struct MapViewportFrame;
struct SpawnHostLayout;
/** @brief Own an isolated authoring world, canonical map, editable asset and mounted host.
 * @ownership Owns a real private Table; host is destroyed before map and Table. No borrowed service/GPU pointers
 * survive open/render. Shared content/store owners follow the asset; writer observation never retains its provider.
 * @thread Authoring/render owner thread, outside ECS Views. Callbacks must not destroy/reenter this application.
 * @remarks No runtime units or simulation are created. Destruction does not save or discard UI drafts through a
 * business operation; the outer application must resolve unsaved source and unapplied drafts before closing.
 */
class EVENGINE_API_EDITORS SpawnEditorApplication final {
public:
    /** @brief Tear down host mounts before the private map/world, without writing any asset. */
    ~SpawnEditorApplication();
    SpawnEditorApplication(const SpawnEditorApplication&)            = delete;
    SpawnEditorApplication& operator=(const SpawnEditorApplication&) = delete;
    /** @brief Load explicit binding assets and assemble a complete private editor using canonical admission paths.
     * @return Owning application or read/schema/map/content/layout failure; caller worlds and store remain unchanged.
     * @ownership Copies descriptor/contract before file callbacks; retains real resolver/store owners and weak writer.
     * @remarks v1 requires an orthogonal contiguous navigation layer. Persisted editor source must match binding map
     * id.
     * @cost Full map load, sidecar compilation, persisted document read and panel mounting per explicit open.
     * @throws std::bad_alloc or backend exceptions; private staged resources are destroyed on failure.
     */
    [[nodiscard]] static Result<std::unique_ptr<SpawnEditorApplication>> open(
        const Value& binding, std::string objectContract, service::IFileSystem& files,
        std::shared_ptr<editor::IAtomicDocumentStore> store, editor::DocumentKey key, std::string documentUri,
        std::shared_ptr<const spawn::ITemplateResolver>       templates,
        std::shared_ptr<const spawn::IUnitDefinitionResolver> units, filesystem::AtomicTextWriteLease writer,
        MapViewportFrame frame, SpawnHostLayout layout, std::string exportPath);
    /** @brief Share the actual private world for canonical UI rendering and scoped host input routing.
     * @remarks Handles are ephemeral; external structural mutation can make the host stale. No default-world alias.
     */
    [[nodiscard]] std::shared_ptr<ecs::Table> world() const noexcept;
    /** @brief Borrow mounted host until destruction; caller scopes world() for input and UI operations. */
    [[nodiscard]] SpawnEditorHost& host() noexcept;
    /** @brief Share the authoritative editable asset, not a source copy or second undo history. */
    [[nodiscard]] std::shared_ptr<SpawnAssetDocument> asset() const noexcept;
    /** @brief Read actual asset/form/panel/gesture facts without applying, saving or cancelling anything. */
    [[nodiscard]] SpawnEditorCloseState closeState() const noexcept;
    /** @brief Reject normal close until source is saved and explicit drafts/pending commits/gestures are resolved.
     * @return Success for a currently clean application, or Conflict preserving all outstanding authoring work.
     * @remarks Synchronous check only, not a durable close token. Call immediately before destruction on this owner
     * thread; external callbacks/edits require another check. Explicit discard is an outer application's user decision.
     */
    [[nodiscard]] Result<void> checkClose() const;
    /** @brief Filter the supplied live window's SDL events, route in the private world and apply native capture hints.
     * @return Actual tool response or native capture/routing failure. Foreign/unsupported events are ignored.
     * @ownership Borrows event/window synchronously; retains no native window pointer or event. Caller must always
     * supply the same window for one interaction and exclusively coordinate this window's capture while using this API.
     * @thread SDL video/UI owner thread. Feed UI first, pass its current mouse/keyboard capture facts; do not feed the
     * same event through host().processSdlEvent as well. Events use logical window coordinates matching host layout.
     * @remarks Capture start requires foreground focus. Failure cancels the uncommitted gesture; cancellation failures
     * are included in diagnostics. Release failure is observable and does not undo an already committed source edit.
     * Call cancelWindowInteraction before app/window teardown or changing the associated window. SDL focus loss also
     * releases capture automatically; this method never releases another window's capture.
     * @cost Constant window filtering/native capture and existing workspace work per supported input event.
     */
    [[nodiscard]] Result<editor::ToolResponse> processWindowEvent(const SDL_Event& event, SDL_Window& window,
                                                                  bool blockedMouse, bool blockedKeyboard);
    /** @brief Cancel tool interaction and release this supplied window's current native capture, without saving.
     * @return Success or combined cancellation/native release diagnostics; a failed release can be retried.
     * @ownership Synchronous live-window borrow. No native resource or service pointer is retained.
     * @remarks Does not cancel a structure draft or discard committed/pending form edits. Call before checkClose.
     */
    [[nodiscard]] Result<void> cancelWindowInteraction(SDL_Window& window);
    /** @brief Scope the private world and draw background, supplied frame primitives, then configured/tool overlay.
     * @return Host/render diagnostic; prior graphics canvas follows the host restoration contract.
     * @ownership Borrows graphics/target and owning frame primitives synchronously; none are retained. Caller
     * composites and renders UI. Supply an empty primitive canvas when no transient scene is present. Units cannot
     * cover editing feedback.
     * @param proposal Optional call-only immutable import candidate, live until return; null omits candidate feedback.
     * @cost Visible tile collection, supplied primitive batch and configured/gesture overlay per explicit frame.
     */
    [[nodiscard]] Result<void> render(graphics::Graphics& graphics, graphics::Canvas& target,
                                      const graphics::PrimitiveCanvas2D& scene,
                                      const PreparedTiledSpawnImport*    proposal = nullptr);

private:
    struct Impl;
    explicit SpawnEditorApplication(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::spawn_editor
