#include "map/spawn/editor/SpawnEditorApplication.h"
#include <SDL2/SDL.h>
#include "common/ECS.h"
#include "common/ServiceInterfaces.h"
#include "common/Uri.h"
#include "editor/EditorDocumentService.h"
#include "filesystem/AtomicTextWriteLease.h"
#include "graphics/Graphics.h"
#include "graphics/PrimitiveDrawList.h"
#include "map/Map.h"
#include "map/spawn/MapSpawnBinding.h"
#include "map/spawn/MapSpawnLoad.h"
#include "map/spawn/editor/SpawnAssetDocument.h"
#include "map/spawn/editor/SpawnEditorHost.h"
#include "map/spawn/editor/SpawnEditorWorkspace.h"
#include "map/spawn/editor/SpawnForm.h"
namespace eve::spawn_editor {
namespace {
Result<void> releaseWindowCapture(SDL_Window& window) {
    if (!(SDL_GetWindowFlags(&window) & SDL_WINDOW_MOUSE_CAPTURE)) return Result<void>::success();
    if (SDL_GetKeyboardFocus() != &window)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "Editor cannot release capture through another foreground window"));
    if (SDL_CaptureMouse(SDL_FALSE) != 0)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported,
            std::string("Native pointer release failed; cancel interaction to retry: ") + SDL_GetError()));
    return Result<void>::success();
}
}  // namespace
struct SpawnEditorApplication::Impl {
    std::shared_ptr<ecs::Table>         world = std::make_shared<ecs::Table>();
    std::unique_ptr<map::Map>           map;
    std::shared_ptr<SpawnAssetDocument> asset;
    std::vector<ecs::EntityHandle>      layers;
    std::unique_ptr<SpawnEditorHost>    host;
};
SpawnEditorApplication::SpawnEditorApplication(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SpawnEditorApplication::~SpawnEditorApplication() = default;
Result<std::unique_ptr<SpawnEditorApplication>> SpawnEditorApplication::open(
    const Value& binding, std::string objectContract, service::IFileSystem& files,
    std::shared_ptr<editor::IAtomicDocumentStore> store, editor::DocumentKey key, std::string documentUri,
    std::shared_ptr<const spawn::ITemplateResolver>       templates,
    std::shared_ptr<const spawn::IUnitDefinitionResolver> units, filesystem::AtomicTextWriteLease writer,
    MapViewportFrame frame, SpawnHostLayout layout, std::string exportPath) {
    using R         = Result<std::unique_ptr<SpawnEditorApplication>>;
    auto descriptor = map_spawn::MapSpawnBinding::decode(binding);
    if (!descriptor) return R::failure(descriptor.status());
    if (!store || !templates || !units || !writer.available())
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing editor content/store/writer"));
    auto read = [&](const std::string& uri) -> Result<std::string> {
        auto path = ResourceUri::parse(uri);
        if (!path) return Result<std::string>::failure(path.status());
        std::vector<std::uint8_t> bytes;
        if (!files.readFile(std::string(path.value().path()), bytes))
            return Result<std::string>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "Editor asset read failed", uri));
        return Result<std::string>::success(std::string(bytes.begin(), bytes.end()));
    };
    const auto& admitted = descriptor.value();
    auto        mapText  = read(admitted.mapUri());
    if (!mapText) return R::failure(mapText.status());
    auto sourceText = read(admitted.spawnsUri());
    if (!sourceText) return R::failure(sourceText.status());
    auto source = Value::fromJson(sourceText.value());
    if (!source) return R::failure(source.status());
    auto             state = std::make_unique<Impl>();
    ecs::ScopedTable scope(*state->world);
    state->map  = std::make_unique<map::Map>();
    auto mapUri = ResourceUri::parse(admitted.mapUri());
    if (!mapUri) return R::failure(mapUri.status());
    auto candidate = state->map->prepareAssetTextWithObjectContract(mapText.value(), std::string(mapUri.value().path()),
                                                                    files, objectContract);
    if (!candidate) return R::failure(candidate.status());
    auto grid = candidate.value()->grid(admitted.navigationLayer());
    if (!grid) return R::failure(grid.status());
    if (grid.value().orientation != map::MapOrientation::Orthogonal || grid.value().gapX != 0 || grid.value().gapY != 0)
        return R::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                            "Editor v1 requires orthogonal contiguous navigation cells"));
    auto asset = SpawnAssetDocument::openSidecar(std::move(store), std::move(key), std::move(documentUri),
                                                 source.value(), std::move(templates), std::move(units));
    if (!asset) return R::failure(asset.status());
    state->asset = std::move(asset).takeValue();
    auto actual  = state->asset->form()->captureSource();
    if (!actual) return R::failure(actual.status());
    const auto* id = actual.value().source.find("map");
    if (!id || !id->isString() || id->asString() != admitted.mapId())
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Editor sidecar map identity mismatch", "map"));
    auto published = candidate.value()->commit(*state->map);
    if (!published) return R::failure(published.status());
    state->layers.reserve(static_cast<std::size_t>(published.value()));
    for (int i = 0; i < published.value(); ++i) state->layers.push_back(ecs::handle_of(state->map->getLayer(i)));
    auto host = SpawnEditorHost::create(state->world, state->asset, std::move(writer), admitted.transform(), frame,
                                        layout, std::move(exportPath));
    if (!host) return R::failure(host.status());
    state->host = std::move(host).takeValue();
    return R::success(std::unique_ptr<SpawnEditorApplication>(new SpawnEditorApplication(std::move(state))));
}
std::shared_ptr<ecs::Table>         SpawnEditorApplication::world() const noexcept { return impl_->world; }
SpawnEditorHost&                    SpawnEditorApplication::host() noexcept { return *impl_->host; }
std::shared_ptr<SpawnAssetDocument> SpawnEditorApplication::asset() const noexcept { return impl_->asset; }
SpawnEditorCloseState               SpawnEditorApplication::closeState() const noexcept {
    const auto& workspace = impl_->host->workspace();
    return {impl_->asset->dirty(), workspace.hasStructureDraft(), impl_->asset->form()->history().pending,
                          workspace.hasPointerCapture()};
}
Result<void> SpawnEditorApplication::checkClose() const {
    const auto state = closeState();
    if (state.unsavedSource || state.structureDraft || state.pendingCommit || state.pointerCapture)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict,
                              "Save source and resolve structure drafts, pending commits and gestures before closing"));
    return Result<void>::success();
}
Result<editor::ToolResponse> SpawnEditorApplication::processWindowEvent(const SDL_Event& event, SDL_Window& window,
                                                                        bool blockedMouse, bool blockedKeyboard) {
    using R       = Result<editor::ToolResponse>;
    const auto id = SDL_GetWindowID(&window);
    if (id == 0) return R::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Editor window is unavailable"));
    std::uint32_t target = 0;
    switch (event.type) {
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: target = event.button.windowID; break;
        case SDL_MOUSEMOTION: target = event.motion.windowID; break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: target = event.key.windowID; break;
        case SDL_WINDOWEVENT: target = event.window.windowID; break;
        case SDL_QUIT: target = id; break;
        default: return R::success(editor::ToolResponse::ignored());
    }
    if (target != id) return R::success(editor::ToolResponse::ignored());
    ecs::ScopedTable scope(*impl_->world);
    auto             response = impl_->host->processSdlEvent(event, blockedMouse, blockedKeyboard);
    if (!response) return response;
    if (response.value().capturePointer) {
        const bool focused = SDL_GetKeyboardFocus() == &window;
        if (!focused || SDL_CaptureMouse(SDL_TRUE) != 0) {
            std::vector<Diagnostic> diagnostics{
                Diagnostic::error(focused ? DiagnosticCode::Unsupported : DiagnosticCode::PreconditionViolation,
                                  focused ? std::string("Native pointer capture failed: ") + SDL_GetError()
                                          : std::string("Editor capture requires its foreground window"))};
            auto cancelled = cancelWindowInteraction(window);
            if (!cancelled)
                diagnostics.insert(diagnostics.end(), cancelled.diagnostics().begin(), cancelled.diagnostics().end());
            return R::failure(Status(StatusCode::Failed, std::move(diagnostics)));
        }
    }
    if (response.value().releasePointer) {
        auto released = releaseWindowCapture(window);
        if (!released) return R::failure(released.status());
    }
    return response;
}
Result<void> SpawnEditorApplication::cancelWindowInteraction(SDL_Window& window) {
    ecs::ScopedTable scope(*impl_->world);
    auto             cancelled   = impl_->host->workspace().cancelInteraction();
    auto             diagnostics = cancelled.diagnostics();
    auto             released    = releaseWindowCapture(window);
    if (!released) diagnostics.insert(diagnostics.end(), released.diagnostics().begin(), released.diagnostics().end());
    if (!cancelled || !released) return Result<void>::failure(Status(StatusCode::Failed, std::move(diagnostics)));
    return Result<void>::success();
}
Result<void> SpawnEditorApplication::render(graphics::Graphics& graphics, graphics::Canvas& target,
                                            const graphics::PrimitiveCanvas2D& scene,
                                            const PreparedTiledSpawnImport*    proposal) {
    ecs::ScopedTable scope(*impl_->world);
    auto             background = impl_->host->renderBackground(impl_->layers, graphics, target);
    if (!background) return background;
    if (!scene.commands().empty() || !scene.triangles().empty()) {
        auto* previous = graphics.getCanvas();  // @lifetime Synchronous canvas borrow, restored before return.
        try {
            graphics.setCanvas(&target);
            graphics.drawPrimitiveCanvas(scene);
        } catch (...) {
            graphics.setCanvas(previous);
            throw;
        }
        graphics.setCanvas(previous);
    }
    return impl_->host->renderOverlay(graphics, target, proposal);
}
}  // namespace eve::spawn_editor
