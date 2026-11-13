// eve-hot-path
// Module composition face; input is bounded and drained outside the platform observer and all ECS Views.
#include "map/rpg_spawn/editor/RPGSpawnEditor.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/SquirrelBinding.h"
#include "definitions/Definitions.h"
#include "editor/EditorDiskDocumentStore.h"
#include "filesystem/AtomicTextWriteLease.h"
#include "filesystem/Filesystem.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/ISolidRectRenderer.h"
#include "graphics/PrimitiveDrawList.h"
#include "map/TileRenderViewport.h"
#include "map/rpg_spawn/MapRPGPresentation.h"
#include "map/rpg_spawn/editor/RPGSpawnPreview.h"
#include "map/spawn/MapSpawnLoad.h"
#include "map/spawn/MapSpawnSpace.h"
#include "map/spawn/editor/SpawnAssetDocument.h"
#include "map/spawn/editor/SpawnAssetSavePanel.h"
#include "map/spawn/editor/SpawnDiagnosticsPanel.h"
#include "map/spawn/editor/SpawnEditorApplication.h"
#include "map/spawn/editor/SpawnEditorHost.h"
#include "map/spawn/editor/SpawnEditorWorkspace.h"
#include "map/spawn/editor/SpawnExportPanel.h"
#include "map/spawn/editor/SpawnForm.h"
#include "map/spawn/editor/SpawnFormPanel.h"
#include "map/spawn/editor/TiledSpawnImport.h"
#include "platform_event/PlatformEventSink.h"
#include "property_access/PropertyAccess.h"
#include "rpg/spawn/RPGUnitCatalogue.h"
#include "spawn/SpawnDiagnostics.h"
#include "spawn/SpawnRegion.h"
#include "spawn/definitions/SpawnTemplateSource.h"
#include "ui/Component.h"
#include "ui/UI.h"
#include "ui/UIAutomationCapabilities.h"
#include "window/Window.h"
namespace eve::map_rpg_spawn_editor {
namespace {
template <class T>
Result<T> fail(std::string message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, std::move(message)));
}
struct Content final : spawn::ITemplateResolver {
    std::shared_ptr<definitions::DefinitionRegistry> registry = std::make_shared<definitions::DefinitionRegistry>();
    std::shared_ptr<rpg_spawn::RPGUnitCatalogue>     units    = std::make_shared<rpg_spawn::RPGUnitCatalogue>(registry);
    spawn::SpawnTemplateSource                       templates{*registry, *units};
    Result<spawn::CompiledTemplate> resolve(const DefinitionRef& ref) const override { return templates.resolve(ref); }
};
enum class Decision { None, Clean, Discard };
enum class PreviewAction { None, Start, Step, Stop, Event, Players, Death };
enum class ImportAction { None, Prepare, Commit, Cancel };
struct Controls {
    bool         mapMode = false, importTab = false;
    ImportAction importAction = ImportAction::None;
    std::string  importPath, importGuid, importPrototype;
    std::string  importStatus =
        "Stop runtime preview, then prepare Tiled geometry. New objects require an existing prototype ID.";
    std::shared_ptr<spawn_editor::PreparedTiledSpawnImport> importPlan;
    std::shared_ptr<const unsigned char>                    importDisplay = std::make_shared<const unsigned char>(0);
    std::function<void()>                                   invalidate;
    bool                                                    closed = false, closeRequested = false;
    Decision                                                decision = Decision::None;
    std::string           status = "Edit configuration; Save stores the editor document, Export writes runtime JSON.";
    std::optional<Status> previewFailure;
    PreviewAction         previewAction = PreviewAction::None;
    std::string           previewEvent;
    std::string           previewPlayersDraft = "[]";
    std::string           previewPlayersRequest;
    std::string           previewSubject;
    std::function<void()> invalidatePreview;
    std::unique_ptr<spawn_editor::SpawnDiagnosticsPanel> diagnostics;
    Revision                                             previewRevision;
    std::optional<map_spawn::PlacementTrace>             previewTrace;
    std::optional<spawn::AdvanceReport>                  previewReport;
    std::string previewStatus = "Start a frozen preview; Step advances 1 second. Restart to use source edits.";
};
std::string_view decisionName(map_spawn::PlacementDecision decision) {
    using D = map_spawn::PlacementDecision;
    switch (decision) {
        case D::Accepted: return "spatially accepted";
        case D::OutsideRegion: return "outside region";
        case D::Walkability: return "not walkable";
        case D::TerrainClearance: return "terrain clearance";
        case D::PlayerDistance: return "player distance";
        case D::Occupied: return "occupied by domain unit";
        case D::BatchClearance: return "batch clearance";
        case D::SamplingError: return "sampling failed";
    }
    return "unknown decision";
}
bool currentPreview(const Controls& state, const spawn::WorldDiagnostics& display) {
    if (state.closed || !state.diagnostics) return false;
    const auto current = state.diagnostics->snapshot();
    return current->owner == display.owner && current->epoch == display.epoch &&
           current->incarnation == display.incarnation;
}
class ClosePanel final : public ui::Component {
public:
    explicit ClosePanel(std::shared_ptr<Controls> controls) : state_(std::move(controls)) {
        state_->invalidate = [this] { markDirty(); };
    }
    ~ClosePanel() override {
        state_->closed     = true;
        state_->invalidate = {};
    }
    ui::WidgetDesc build() override {
        auto state   = state_;
        auto request = [state](Decision decision) {
            if (!state->closed) {
                state->decision = decision;
                if (state->invalidate) state->invalidate();
            }
        };
        auto tab = [state](bool imported) {
            if (state->closed) return;
            state->importTab     = imported;
            state->importDisplay = std::make_shared<const unsigned char>(0);
            if (state->invalidate) state->invalidate();
        };
        if (state->mapMode && state->importTab) {
            auto display = state->importDisplay;
            auto current = [state, display] { return !state->closed && display == state->importDisplay; };
            auto field   = [state, current](const char* label, std::string Controls::*member, const char* key) {
                auto input         = ui::inputText(label, state.get()->*member, key);
                input.enabled      = !state->importPlan && state->importAction == ImportAction::None;
                input.onTextChange = [state, current, member](const std::string& text) {
                    if (current() && !state->importPlan && state->importAction == ImportAction::None)
                        state.get()->*member = text;
                };
                return input;
            };
            auto requestImport = [state, current](ImportAction action) {
                if (!current() || state->importAction != ImportAction::None) return;
                state->importAction = action;
                if (state->invalidate) state->invalidate();
            };
            return ui::column(
                {ui::button("Session / close", "spawn/import/session", [tab] { tab(false); }),
                 ui::text(state->importStatus, "spawn/import/status").withWrap(1240),
                 field("Tiled path", &Controls::importPath, "spawn/import/path"),
                 field("Source UUID", &Controls::importGuid, "spawn/import/guid"),
                 field("New instance prototype ID (optional)", &Controls::importPrototype, "spawn/import/prototype"),
                 ui::button("Prepare geometry", "spawn/import/prepare",
                            [requestImport] { requestImport(ImportAction::Prepare); }),
                 ui::button("Commit import", "spawn/import/commit",
                            [requestImport] { requestImport(ImportAction::Commit); }),
                 ui::button("Cancel import", "spawn/import/cancel",
                            [requestImport] { requestImport(ImportAction::Cancel); })},
                "spawn/import");
        }
        auto status = state->status;
        if (state->previewFailure)
            status +=
                " Preview unavailable: " + state->previewFailure->describe() + ". Edit source or adjust the viewport.";
        return ui::column(
            {ui::button("Tiled geometry import", "spawn/session/import",
                        [tab, state] {
                            if (state->mapMode) tab(true);
                        }),
             ui::text(status, "spawn/session/status").withWrap(1240),
             ui::button("Close when saved", "spawn/session/close", [request] { request(Decision::Clean); }),
             ui::button("Discard changes and close", "spawn/session/discard",
                        [request] { request(Decision::Discard); }),
             ui::button("Keep editing", "spawn/session/keep",
                        [state] {
                            if (!state->closed) {
                                state->closeRequested = false;
                                state->decision       = Decision::None;
                                state->status = "Continue editing. Save source and resolve drafts before closing.";
                                if (state->invalidate) state->invalidate();
                            }
                        })},
            "spawn/session");
    }

private:
    std::shared_ptr<Controls> state_;
};
class PreviewPanel final : public ui::Component {
    std::shared_ptr<Controls>                state_;
    std::shared_ptr<spawn_editor::SpawnForm> form_;
    property_access::Subscription            changes_;

public:
    PreviewPanel(std::shared_ptr<Controls> state, std::shared_ptr<spawn_editor::SpawnForm> form)
        : state_(std::move(state)), form_(std::move(form)) {
        state_->invalidatePreview = [this] { markDirty(); };
        changes_                  = form_->view().subscribe([weak = std::weak_ptr<Controls>(state_)](const auto&) {
            if (auto state = weak.lock(); state && state->invalidatePreview) state->invalidatePreview();
        });
    }
    ~PreviewPanel() override { state_->invalidatePreview = {}; }
    ui::WidgetDesc build() override {
        const auto state   = state_;
        auto       request = [state](PreviewAction action) {
            if (!state->closed && state->previewAction == PreviewAction::None) {
                state->previewAction = action;
                if (state->invalidatePreview) state->invalidatePreview();
            }
        };
        auto start = ui::button("Start / restart", "spawn/preview/start", [request] { request(PreviewAction::Start); });
        auto step  = ui::button("Step 1s", "spawn/preview/step", [request] { request(PreviewAction::Step); });
        auto stop  = ui::button("Stop", "spawn/preview/stop", [request] { request(PreviewAction::Stop); });
        start.enabled = !state->closed && state->previewAction == PreviewAction::None;
        step.enabled = stop.enabled = start.enabled && state->diagnostics != nullptr;
        std::vector<ui::WidgetDesc> rows;
        rows.push_back(ui::row({std::move(start), std::move(step), std::move(stop)}, "spawn/preview/actions"));
        if (state->previewTrace) {
            const auto& trace = *state->previewTrace;
            rows.push_back(ui::text("Placement: " + std::to_string(trace.attempts.size()) + " recent attempts, " +
                                        std::to_string(trace.dropped) +
                                        " older rows dropped. Red: historical rejection.",
                                    "spawn/preview/trace-summary")
                               .withWrap(440));
        }
        if (state->diagnostics) {
            const auto            display = state->diagnostics->snapshot();
            std::set<std::string> events;
            for (const auto& source : display->sources)
                if (const auto* activation = std::get_if<spawn::EventActivation>(&source.activation))
                    events.insert(activation->event);
            for (const auto& event : events) {
                auto button =
                    ui::button("Trigger event: " + event, "spawn/preview/event/" + event, [state, display, event] {
                        if (state->previewAction != PreviewAction::None || !currentPreview(*state, *display)) return;
                        state->previewEvent  = event;
                        state->previewAction = PreviewAction::Event;
                        if (state->invalidatePreview) state->invalidatePreview();
                    });
                button.enabled = !state->closed && state->previewAction == PreviewAction::None;
                rows.push_back(std::move(button));
            }
            for (const auto& source : display->sources) {
                if (source.state.subjects.empty()) continue;
                const auto subject = source.state.subjects.front().format();
                auto       defeat =
                    ui::button("Defeat one: " + source.state.id, "spawn/preview/defeat/" + source.state.id,
                               [state, display, subject] {
                                   if (state->previewAction != PreviewAction::None || !currentPreview(*state, *display))
                                       return;
                                   state->previewSubject = subject;
                                   state->previewAction  = PreviewAction::Death;
                                   if (state->invalidatePreview) state->invalidatePreview();
                               });
                defeat.enabled = !state->closed && state->previewAction == PreviewAction::None;
                rows.push_back(std::move(defeat));
            }
            auto players = ui::inputText("Player draft", state->previewPlayersDraft, "spawn/preview/players");
            players.withSize(160, 0);
            players.onTextChange = [state, display](const std::string& text) {
                if (!currentPreview(*state, *display)) return;
                state->previewPlayersDraft = text;
                if (state->invalidatePreview) state->invalidatePreview();
            };
            auto apply    = ui::button("Apply players", "spawn/preview/apply-players", [state, display] {
                if (state->previewAction != PreviewAction::None || !currentPreview(*state, *display)) return;
                state->previewPlayersRequest = state->previewPlayersDraft;
                state->previewAction         = PreviewAction::Players;
                if (state->invalidatePreview) state->invalidatePreview();
            });
            apply.enabled = !state->closed && state->previewAction == PreviewAction::None;
            rows.push_back(ui::row({std::move(players), std::move(apply)}, "spawn/preview/player-input"));
            rows.push_back(ui::text("World2D [[x,y], ...]; [] clears. Apply advances 0s.", "spawn/preview/player-help")
                               .withWrap(440));
        }
        if (state->previewTrace) {
            const auto& trace = *state->previewTrace;
            for (std::size_t i = 0; i < trace.attempts.size(); ++i) {
                const auto& attempt  = trace.attempts[i];
                const auto  location = attempt.point ? "[" + std::to_string(attempt.point->x) + ", " +
                                                          std::to_string(attempt.point->y) + "]"
                                                     : "no candidate position";
                const auto  stage = attempt.stage == map_spawn::PlacementStage::Sampling ? "sample " : "final check ";
                rows.push_back(ui::text(std::string(stage) + std::to_string(attempt.attempt) + ": " +
                                            std::string(decisionName(attempt.decision)) + " " + location + " subject " +
                                            attempt.subject.format() + " operation " + attempt.operation.format(),
                                        "spawn/preview/trace/" + std::to_string(i))
                                   .withWrap(440));
            }
        }
        rows.push_back(ui::text(state->previewStatus, "spawn/preview/status").withWrap(440));
        if (state->previewReport) {
            for (std::size_t i = 0; i < state->previewReport->diagnostics.size(); ++i)
                rows.push_back(ui::text(state->previewReport->diagnostics[i].message(),
                                        "spawn/preview/step-diagnostic/" + std::to_string(i))
                                   .withWrap(440));
        }
        if (state->diagnostics) {
            const auto current = form_->sourceRevision();
            rows.push_back(ui::text("Preview source revision: " + std::to_string(state->previewRevision.value()) +
                                        (current == state->previewRevision ? " (current)"
                                                                           : " (source changed; restart to update)"),
                                    "spawn/preview/revision")
                               .withWrap(440));
            // Canonical detached diagnostic presenter; this parent schedules after every explicit refresh.
            rows.push_back(state->diagnostics->component().build());
        }
        return ui::column(std::move(rows), "spawn/preview");
    }
};
struct CloseMount {
    std::shared_ptr<ecs::Table>    world;
    std::unique_ptr<ui::Component> panel;
    ui::UIHostHandle               handle{};
    ~CloseMount() {
        panel.reset();
        if (world) {
            ecs::ScopedTable scope(*world);
            if (auto host = ui::UIHost::resolve(handle)) ecs::DestroyEntity(&host->get());
        }
    }
};
Result<std::unique_ptr<CloseMount>> mountPanel(std::shared_ptr<ecs::Table>    world,
                                               std::unique_ptr<ui::Component> component, const std::string& name,
                                               const std::string& title, spawn_editor::SpawnHostRect rect) {
    auto result   = std::make_unique<CloseMount>();
    result->world = std::move(world);
    result->panel = std::move(component);
    ecs::ScopedTable scope(*result->world);
    result->handle = ui::UIHost::createHost(name);
    auto host      = ui::UIHost::resolve(result->handle);
    if (!host) return fail<std::unique_ptr<CloseMount>>("Panel admission failed");
    auto meta    = host->get().meta();
    meta->hasPos = meta->lockPos = meta->hasSize = meta->lockSize = true;
    meta->posX                                                    = rect.x;
    meta->posY                                                    = rect.y;
    meta->sizeX                                                   = rect.width;
    meta->sizeY                                                   = rect.height;
    result->panel->setWindowFrame(title);
    result->panel->attach(result->handle);
    return Result<std::unique_ptr<CloseMount>>::success(std::move(result));
}
struct PreviewRects final : graphics::ISolidRectRenderer {
    map::TileRenderViewport     viewport;
    graphics::PrimitiveCanvas2D primitives;
    std::optional<Diagnostic>   error;
    explicit PreviewRects(map::TileRenderViewport frame) : viewport(frame) {}
    void drawSolidRectRGBA(float x, float y, float width, float height, float red, float green, float blue,
                           float alpha) override {
        const double left   = (double(x) - viewport.centerX) * viewport.zoom + viewport.width * 0.5;
        const double top    = (double(y) - viewport.centerY) * viewport.zoom + viewport.height * 0.5;
        const double right  = left + double(width) * viewport.zoom;
        const double bottom = top + double(height) * viewport.zoom;
        for (double coordinate : {left, top, right, bottom})
            if (!std::isfinite(coordinate) || std::abs(coordinate) > std::numeric_limits<float>::max()) {
                error = Diagnostic::error(DiagnosticCode::InvalidArgument, "Preview marker projection overflow");
                return;
            }
        graphics::PrimitivePaint paint;
        paint.color = {red, green, blue, alpha};
        paint.mode  = graphics::PaintMode::Fill;
        primitives.drawRect({float(left), float(top)}, {float(right), float(bottom)}, paint);
    }
};
// Authoring-only composition: template source/history stay in the existing asset/form owners.
// Components retire before resolving their own mounts; the actual Table remains owned throughout teardown.
struct TemplateApp {
    std::shared_ptr<ecs::Table>                       world = std::make_shared<ecs::Table>();
    std::shared_ptr<spawn_editor::SpawnAssetDocument> asset;
    std::unique_ptr<spawn_editor::SpawnFormPanel>     form;
    std::unique_ptr<ui::Component>                    save, exportSource;
    std::array<ui::UIHostHandle, 3>                   mounts{};
    ~TemplateApp() {
        form.reset();
        save.reset();
        exportSource.reset();
        ecs::ScopedTable scope(*world);
        for (auto handle : mounts)
            if (auto host = ui::UIHost::resolve(handle)) ecs::DestroyEntity(&host->get());
    }
    static Result<std::unique_ptr<TemplateApp>> open(const Value&                                  source,
                                                     std::shared_ptr<editor::IAtomicDocumentStore> store,
                                                     editor::DocumentKey key, const std::string& uri,
                                                     std::shared_ptr<const spawn::IUnitDefinitionResolver> units,
                                                     filesystem::AtomicTextWriteLease                      writer,
                                                     const std::string&                                    exportPath) {
        using R       = Result<std::unique_ptr<TemplateApp>>;
        auto document = spawn_editor::SpawnAssetDocument::openTemplate(std::move(store), std::move(key), uri, source,
                                                                       std::move(units));
        if (!document) return R::failure(document.status());
        auto state   = std::make_unique<TemplateApp>();
        state->asset = std::move(document).takeValue();
        auto form    = spawn_editor::createSpawnFormPanel(state->asset->form());
        if (!form) return R::failure(form.status());
        auto save = spawn_editor::createSpawnAssetSavePanel(state->asset);
        if (!save) return R::failure(save.status());
        auto exported = spawn_editor::createSpawnExportPanel(state->asset->form(), std::move(writer), exportPath);
        if (!exported) return R::failure(exported.status());
        state->form         = std::move(form).takeValue();
        state->save         = std::move(save).takeValue();
        state->exportSource = std::move(exported).takeValue();
        ecs::ScopedTable                                 scope(*state->world);
        const std::array<spawn_editor::SpawnHostRect, 3> rectangles{
            {{0, 0, 800, 640}, {800, 0, 480, 240}, {800, 240, 480, 400}}};
        const std::array<ui::Component*, 3> panels{state->form.get(), state->save.get(), state->exportSource.get()};
        const std::array<const char*, 3>    names{"Spawn template", "Spawn template save",
                                               "Spawn template runtime export"};
        for (std::size_t i = 0; i < panels.size(); ++i) {
            state->mounts[i] = ui::UIHost::createHost("spawn-template/" + std::to_string(i));
            auto host        = ui::UIHost::resolve(state->mounts[i]);
            if (!host) return fail<std::unique_ptr<TemplateApp>>("Template panel admission failed");
            auto       meta = host->get().meta();
            const auto rect = rectangles[i];
            meta->hasPos = meta->lockPos = meta->hasSize = meta->lockSize = true;
            meta->posX                                                    = rect.x;
            meta->posY                                                    = rect.y;
            meta->sizeX                                                   = rect.width;
            meta->sizeY                                                   = rect.height;
            panels[i]->setWindowFrame(std::string(names[i]));
            panels[i]->attach(state->mounts[i]);
        }
        return R::success(std::move(state));
    }
    Result<void> checkClose() const {
        if (asset->dirty() || form->hasStructureDraft() || asset->form()->history().pending)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict,
                                  "Save template and resolve structure drafts and pending commits before closing"));
        return Result<void>::success();
    }
};
enum class EditorKind { Map, Template };
struct Guard {
    bool& busy;
    explicit Guard(bool& flag) : busy(flag) { busy = true; }
    ~Guard() { busy = false; }
};
}  // namespace
struct RPGSpawnEditor::Impl final : platform_event::IPlatformEventSink {
    std::shared_ptr<Content>                              content = std::make_shared<Content>();
    std::unique_ptr<spawn_editor::SpawnEditorApplication> app;
    std::unique_ptr<TemplateApp>                          templateApp;
    std::shared_ptr<Controls>                             controls;
    std::unique_ptr<ui::UIAutomationWorld>                automationWorld;
    std::unique_ptr<CloseMount>                           mount;
    std::unique_ptr<CloseMount>                           previewMount;
    std::unique_ptr<RPGSpawnPreview>                      preview;
    Value                                                 previewBinding;
    std::string                                           previewContract;
    std::array<SDL_Event, 1024>                           inbox;
    std::size_t                                           count    = 0;
    std::uint32_t                                         windowId = 0;
    bool                                                  overflow = false, busy = false;
    Impl() {
        cap::addListener<platform_event::IPlatformEventSink>(this, platform_event::IPlatformEventSink::kObserver + 1);
    }
    ~Impl() override {
        cap::removeListener<platform_event::IPlatformEventSink>(this);
        retire();
    }
    void retire() noexcept {
        windowId = 0;
        automationWorld.reset();
        previewMount.reset();
        mount.reset();
        preview.reset();
        previewBinding = Value{};
        previewContract.clear();
        if (controls) controls->importPlan.reset();
        app.reset();
        templateApp.reset();
        controls.reset();
        count    = 0;
        overflow = false;
        windowId = 0;
    }
    bool                        opened() const noexcept { return app || templateApp; }
    std::shared_ptr<ecs::Table> world() const noexcept { return app ? app->world() : templateApp->world; }
    Result<void>                cancel(SDL_Window& window) {
        return app ? app->cancelWindowInteraction(window) : Result<void>::success();
    }
    Result<void> checkClose() const {
        if (controls && (controls->importPlan || controls->importAction != ImportAction::None))
            return fail<void>("Commit or cancel the Tiled import before closing");
        return app ? app->checkClose() : templateApp->checkClose();
    }
    bool shouldConsumePlatformEvent(const void* nativeEvent) override {
        if (!opened() || !controls || windowId == 0 || !nativeEvent) return false;
        const auto&   event = *static_cast<const SDL_Event*>(nativeEvent);
        std::uint32_t id    = 0;
        switch (event.type) {
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: id = event.button.windowID; break;
            case SDL_MOUSEMOTION: id = event.motion.windowID; break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: id = event.key.windowID; break;
            case SDL_WINDOWEVENT: id = event.window.windowID; break;
            case SDL_QUIT: id = windowId; break;
            default: return false;
        }
        if (id != windowId) return false;
        const bool closing =
            event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE);
        if (closing) controls->closeRequested = true;
        if (count == inbox.size())
            overflow = true;
        else
            inbox[count++] = event;
        return closing;
    }
    Result<void> publish(const std::string& json, bool unit) {
        if (busy || opened()) return fail<void>("Publish content before opening the editor");
        Guard guard(busy);
        auto  source = Value::fromJson(json);
        if (!source) return Result<void>::failure(source.status());
        return unit ? content->units->publish(source.value()) : content->templates.publish(source.value());
    }
    // @lifetime Native pointer result is borrowed only for this synchronous bound call; never retained.
    Result<SDL_Window*> native(window::Window* window) const {
        if (!window || !window->isOpen() || !window->getHandle())
            return fail<SDL_Window*>("Live editor window required");
        auto* handle = static_cast<SDL_Window*>(window->getHandle());
        if (windowId && SDL_GetWindowID(handle) != windowId)
            return fail<SDL_Window*>("Editor window changed; shut down before replacing it");
        return Result<SDL_Window*>::success(handle);
    }
    Result<void> open(window::Window* window, filesystem::Filesystem* files, const std::string& bindingJson,
                      const std::string& contract, const std::string& root, const std::string& assetId,
                      const std::string& uri, const std::string& exportPath, EditorKind kind) {
        if (busy || opened() || !files) return fail<void>("Editor is already open or file provider is absent");
        Guard         guard(busy);
        std::uint32_t openingWindowId = 0;
        {
            auto handle = native(window);
            if (!handle) return Result<void>::failure(handle.status());
            openingWindowId = SDL_GetWindowID(handle.value());
        }
        if (window->getWidth() < 1280 || window->getHeight() < 800)
            return fail<void>("Editor requires a window of at least 1280 by 800 logical pixels");
        auto binding = Value::fromJson(bindingJson);
        if (!binding) return Result<void>::failure(binding.status());
        auto                          store = std::make_shared<editor::DiskAtomicDocumentStore>(root);
        spawn_editor::SpawnHostLayout layout{
            {0, 0, 480, 480}, {480, 0, 320, 300}, {800, 0, 480, 640}, {480, 300, 320, 150}, {480, 450, 320, 190}};
        std::unique_ptr<spawn_editor::SpawnEditorApplication> nextApp;
        std::unique_ptr<TemplateApp>                          nextTemplate;
        if (kind == EditorKind::Map) {
            auto candidate = spawn_editor::SpawnEditorApplication::open(
                binding.value(), contract, *files, store, {editor::DocumentKind::Generic, editor::AssetGuid(assetId)},
                uri, content, content->units, files->observeAtomicWrites(), {0, 0, 0, 0, 0.75}, layout, exportPath);
            if (!candidate) return Result<void>::failure(candidate.status());
            nextApp = std::move(candidate).takeValue();
        } else {
            auto candidate =
                TemplateApp::open(binding.value(), store, {editor::DocumentKind::Generic, editor::AssetGuid(assetId)},
                                  uri, content->units, files->observeAtomicWrites(), exportPath);
            if (!candidate) return Result<void>::failure(candidate.status());
            nextTemplate = std::move(candidate).takeValue();
        }
        auto nextControls     = std::make_shared<Controls>();
        nextControls->mapMode = bool(nextApp);
        if (nextTemplate)
            nextControls->status =
                "Edit template; Save stores its document, Export writes runtime JSON. Map sessions are unchanged.";
        auto nextWorld = nextApp ? nextApp->world() : nextTemplate->world;
        auto observed  = ui::UIAutomationWorld::observe(nextWorld);
        if (!observed) return Result<void>::failure(observed.status());
        auto closeMount = mountPanel(nextWorld, std::make_unique<ClosePanel>(nextControls), "spawn-editor/session",
                                     "Spawn editor session", {0, 640, 1280, 160});
        if (!closeMount) return Result<void>::failure(closeMount.status());
        auto                        nextMount = std::move(closeMount).takeValue();
        std::unique_ptr<CloseMount> nextPreviewMount;
        Value                       nextBinding;
        std::string                 nextContract;
        if (nextApp) {
            nextBinding  = binding.value();
            nextContract = contract;
            auto mounted = mountPanel(nextWorld, std::make_unique<PreviewPanel>(nextControls, nextApp->asset()->form()),
                                      "spawn-editor/preview", "Spawn preview and diagnostics", {0, 480, 480, 160});
            if (!mounted) return Result<void>::failure(mounted.status());
            nextPreviewMount = std::move(mounted).takeValue();
        }
        auto live = native(window);
        if (!live) return Result<void>::failure(live.status());
        if (SDL_GetWindowID(live.value()) != openingWindowId)
            return fail<void>("Editor native window changed during asset admission");
        automationWorld = std::move(observed).takeValue();
        app             = std::move(nextApp);
        templateApp     = std::move(nextTemplate);
        controls        = std::move(nextControls);
        previewMount    = std::move(nextPreviewMount);
        previewBinding  = std::move(nextBinding);
        previewContract = std::move(nextContract);
        mount           = std::move(nextMount);
        windowId        = openingWindowId;
        return Result<void>::success();
    }
    Result<void> shutdown(window::Window* window, const std::string& policy) {
        if (busy) return fail<void>("Editor call reentry");
        if (policy != "require_clean" && policy != "discard") return fail<void>("Unknown editor close policy");
        if (!opened()) return Result<void>::success();
        Guard guard(busy);
        if (SDL_GetWindowFromID(windowId)) {
            auto handle = native(window);
            if (!handle) return Result<void>::failure(handle.status());
            auto cancelled = cancel(*handle.value());
            if (!cancelled) return cancelled;
        } else if (app) {
            // The native registry proves the old window is gone; SDL already revoked its capture.
            ecs::ScopedTable scope(*app->world());
            auto             cancelled = app->host().workspace().cancelInteraction();
            if (!cancelled) return Result<void>::failure(cancelled.status());
        }
        if (policy == "require_clean") {
            auto clean = checkClose();
            if (!clean) return clean;
        }
        if (preview) {
            auto stopped = preview->stop();
            if (!stopped) return stopped;
        }
        retire();
        return Result<void>::success();
    }
    Result<Value> update(window::Window* window, ui::UI* ui) {
        if (busy || !opened() || !ui) return fail<Value>("Open editor and UI required");
        auto handle = native(window);
        if (!handle) return Result<Value>::failure(handle.status());
        {
            Guard                       guard(busy);
            std::array<SDL_Event, 1024> events;
            const auto                  size = std::exchange(count, 0);
            std::copy_n(inbox.begin(), size, events.begin());
            const bool dropped = std::exchange(overflow, false);
            Status     inputStatus;
            if (dropped)
                inputStatus = Status::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                                "Editor input queue overflow; interaction cancelled"));
            else
                for (std::size_t i = 0; app && i < size; ++i) {
                    auto result = app->processWindowEvent(events[i], *handle.value(), ui->wantCaptureMouse(),
                                                          ui->wantCaptureKeyboard());
                    if (!result) {
                        inputStatus = result.status();
                        break;
                    }
                }
            if (!inputStatus.isSuccess()) {
                auto cancelled   = cancel(*handle.value());
                controls->status = inputStatus.describe();
                if (!cancelled) controls->status += "; " + cancelled.status().describe();
                mount->panel->markDirty();
                auto diagnostics = inputStatus.diagnostics();
                if (!cancelled)
                    diagnostics.insert(diagnostics.end(), cancelled.diagnostics().begin(),
                                       cancelled.diagnostics().end());
                return Result<Value>::failure(Status(inputStatus.code(), std::move(diagnostics)));
            }
            if (controls->closeRequested && controls->decision == Decision::None) {
                const std::string message =
                    "Close requested. Save source and resolve drafts, then Close when saved; or explicitly Discard "
                    "changes and close.";
                if (controls->status != message) {
                    controls->status = message;
                    mount->panel->markDirty();
                }
            }
        }
        const auto decision = std::exchange(controls->decision, Decision::None);
        if (decision != Decision::None) {
            auto closed = shutdown(window, decision == Decision::Discard ? "discard" : "require_clean");
            if (!closed) {
                controls->status = closed.status().describe();
                mount->panel->markDirty();
                return Result<Value>::failure(closed.status());
            }
        }
        const auto  action    = opened() ? controls->previewAction : PreviewAction::None;
        const char* requested = action == PreviewAction::Start     ? "start"
                                : action == PreviewAction::Step    ? "step"
                                : action == PreviewAction::Stop    ? "stop"
                                : action == PreviewAction::Death   ? "death"
                                : action == PreviewAction::Players ? "players"
                                : action == PreviewAction::Event   ? "event"
                                                                   : "none";
        auto        payload   = Value::object(
            {{"closed", !opened()},
                      {"preview_action", requested},
                      {"preview_event", action == PreviewAction::Event ? controls->previewEvent : std::string{}},
                      {"preview_subject", action == PreviewAction::Death ? controls->previewSubject : std::string{}},
                      {"preview_players", action == PreviewAction::Players ? controls->previewPlayersRequest : std::string{}}});
        if (opened()) {
            const auto request = controls->importAction;
            payload.set("import_action", request == ImportAction::Prepare  ? "prepare"
                                         : request == ImportAction::Commit ? "commit"
                                         : request == ImportAction::Cancel ? "cancel"
                                                                           : "none");
            payload.set("import_path", controls->importPath);
            payload.set("import_guid", controls->importGuid);
            payload.set("import_prototype", controls->importPrototype);
            controls->importAction = ImportAction::None;
            if (request != ImportAction::None) controls->importDisplay = std::make_shared<const unsigned char>(0);
            controls->previewAction = PreviewAction::None;
            controls->previewEvent.clear();
            controls->previewPlayersRequest.clear();
            controls->previewSubject.clear();
        }
        return Result<Value>::success(std::move(payload));
    }
    Result<void> importResult(Result<void> result) {
        if (!result) controls->importStatus = result.status().describe();
        controls->importDisplay = std::make_shared<const unsigned char>(0);
        if (controls->invalidate) controls->invalidate();
        return result;
    }
    Result<void> prepareTiledImport(filesystem::Filesystem* files, const std::string& path,
                                    const std::string& sourceGuid, const std::string& prototype) {
        if (busy || !app || !files) return fail<void>("Open map editor and filesystem required");
        Guard guard(busy);
        if (preview || controls->importPlan || controls->previewAction != PreviewAction::None)
            return importResult(fail<void>("Stop preview and resolve the current import first"));
        auto& workspace = app->host().workspace();
        if (workspace.hasStructureDraft() || workspace.hasPointerCapture())
            return importResult(fail<void>("Resolve drafts and pointer interaction before import"));
        auto guid = AssetGuid::parse(sourceGuid);
        if (!guid || guid->isNil()) return importResult(fail<void>("Explicit source asset UUID required"));
        auto form   = app->asset()->form();
        auto source = form->captureSource();
        if (!source) return importResult(Result<void>::failure(source.status()));
        Value defaults;
        if (!prototype.empty()) {
            const auto* rows  = source.value().source.find("instances")->getIf<Value::Array>();
            auto        found = std::find_if(rows->begin(), rows->end(),
                                             [&](const auto& row) { return row.find("id")->asString() == prototype; });
            if (found == rows->end()) return importResult(fail<void>("Prototype instance ID not found"));
            Value::Object fields;
            for (const auto& [key, value] : *found->getIf<Value::Object>())
                if (key != "id" && key != "region") fields.emplace(key, value);
            defaults = Value(std::move(fields));
        }
        std::vector<std::uint8_t> bytes;
        if (!files->readFile(path, bytes)) return importResult(fail<void>("Cannot read Tiled source: " + path));
        auto tiled = Value::fromJson(std::string(bytes.begin(), bytes.end()));
        if (!tiled) return importResult(Result<void>::failure(tiled.status()));
        if (form->sourceRevision() != source.value().revision)
            return importResult(fail<void>("Source changed while reading Tiled asset"));
        auto prepared = spawn_editor::PreparedTiledSpawnImport::prepare(form, std::move(tiled).takeValue(), *guid,
                                                                        workspace.pixelTransform(), std::move(defaults),
                                                                        content, content->units);
        if (!prepared) return importResult(Result<void>::failure(prepared.status()));
        controls->importPlan =
            std::make_shared<spawn_editor::PreparedTiledSpawnImport>(std::move(prepared).takeValue());
        controls->importStatus = "Prepared " + std::to_string(controls->importPlan->importedIds().size()) +
                                 " regions in amber; source revision " +
                                 std::to_string(controls->importPlan->sourceRevision().value()) +
                                 ". Commit or cancel explicitly.";
        return importResult(Result<void>::success());
    }
    Result<void> commitTiledImport() {
        if (busy || !app || !controls->importPlan) return fail<void>("Prepared map import required");
        Guard guard(busy);
        if (preview || app->host().workspace().hasStructureDraft() || app->host().workspace().hasPointerCapture())
            return importResult(fail<void>("Resolve preview/drafts/interaction before import commit"));
        auto applied = controls->importPlan->commit();
        if (!applied) return importResult(std::move(applied));
        controls->importPlan.reset();
        controls->importStatus = "Import committed through the canonical undo history. Save or export explicitly.";
        return importResult(std::move(applied));
    }
    Result<void> cancelTiledImport() {
        if (busy || !app) return fail<void>("Idle map editor required");
        Guard guard(busy);
        controls->importPlan.reset();
        controls->importAction = ImportAction::None;
        controls->importStatus = "Import cancelled; committed source unchanged.";
        return importResult(Result<void>::success());
    }
    Result<void> previewResult(Result<void> result) {
        if (result) return result;
        controls->previewStatus = result.status().describe();
        if (controls->invalidatePreview) controls->invalidatePreview();
        return result;
    }
    Result<void> startPreview(filesystem::Filesystem* files, const std::string& ownerText, std::int64_t seed,
                              double radius) {
        if (busy || !app || !files) return fail<void>("Open map editor and file provider required");
        Guard guard(busy);
        if (controls->importPlan || controls->importAction != ImportAction::None)
            return previewResult(fail<void>("Commit or cancel Tiled import before starting preview"));
        if (seed < 0) return previewResult(fail<void>("Preview seed must be nonnegative"));
        auto owner = PersistentId::parse(ownerText);
        if (!owner || owner->isNil()) return previewResult(fail<void>("Explicit non-nil preview owner required"));
        if (app->host().workspace().hasStructureDraft())
            return previewResult(fail<void>("Apply or cancel the structure draft before starting preview"));
        auto source = app->asset()->form()->captureSource();
        if (!source) return previewResult(Result<void>::failure(source.status()));
        auto next = RPGSpawnPreview::create(previewBinding, previewContract, *files, std::move(source).takeValue(),
                                            *content, content->units, *owner, std::uint64_t(seed), radius);
        if (!next) return previewResult(Result<void>::failure(next.status()));
        auto captured = next.value()->captureDiagnostics();
        if (!captured) return previewResult(Result<void>::failure(captured.status()));
        auto panel = spawn_editor::SpawnDiagnosticsPanel::create(std::move(captured).takeValue());
        if (!panel) return previewResult(Result<void>::failure(panel.status()));
        std::string message = "Frozen preview ready. Green markers are committed preview units; Step advances 1s.";
        if (preview) {
            auto stopped = preview->stop();
            if (!stopped) return previewResult(std::move(stopped));
        }
        preview                 = std::move(next).takeValue();
        controls->previewAction = PreviewAction::None;
        controls->previewEvent.clear();
        controls->previewPlayersRequest.clear();
        controls->previewSubject.clear();
        controls->diagnostics = std::move(panel).takeValue();
        controls->previewReport.reset();
        controls->previewTrace.reset();
        controls->previewRevision = preview->sourceRevision();
        controls->previewStatus   = std::move(message);
        if (controls->invalidatePreview) controls->invalidatePreview();
        return Result<void>::success();
    }
    Result<void> stepPreview(std::int64_t nanoseconds, std::optional<std::string_view> event,
                             std::optional<std::string_view> players, std::optional<SubjectRef> death) {
        if (busy || !app || !preview) return fail<void>("Active map preview required");
        Guard guard(busy);
        if (nanoseconds < 0) return previewResult(fail<void>("Nonnegative preview step required"));
        std::vector<spawn::Point> playerFrame;
        if (players) {
            auto input = Value::fromJson(*players);
            if (!input) return previewResult(Result<void>::failure(input.status()));
            auto decoded = spawn::decodePlayerFrame(input.value(), spawn::WorkLimits{}.queuedCommands);
            if (!decoded) return previewResult(Result<void>::failure(decoded.status()));
            playerFrame = std::move(decoded).takeValue();
        }
        const auto delta  = Duration::fromNanoseconds(nanoseconds);
        auto       before = preview->captureDiagnostics();
        if (!before) return previewResult(Result<void>::failure(before.status()));
        if (before.value().tick.value() == std::numeric_limits<std::uint64_t>::max())
            return previewResult(fail<void>("Preview tick exhausted"));
        const SimulationStep              step{SimulationTick(before.value().tick.value() + 1), delta};
        std::vector<spawn::SpawnerHandle> eventSources;
        if (event) {
            auto sources = preview->resolveEvent(*event);
            if (!sources) return previewResult(Result<void>::failure(sources.status()));
            eventSources = std::move(sources).takeValue();
        }
        const auto playerInput = players ? std::optional<std::span<const spawn::Point>>(playerFrame) : std::nullopt;
        if (death) {
            auto defeated = preview->defeat(*death);
            if (!defeated) return previewResult(Result<void>::failure(defeated.status()));
        }
        auto advanced = preview->advance(step, {playerInput, eventSources});
        auto trace    = preview->capturePlacementTrace();
        if (trace)
            controls->previewTrace = std::move(trace).takeValue();
        else {
            if (!advanced) return previewResult(Result<void>::failure(advanced.status()));
            return previewResult(Result<void>::failure(trace.status()));
        }
        if (!advanced) {
            auto refreshed =
                controls->diagnostics->refresh(Result<spawn::WorldDiagnostics>::failure(advanced.status()));
            return previewResult(std::move(refreshed));
        }
        auto        report  = std::move(advanced).takeValue();
        std::string message = "Last accepted step: " + std::to_string(report.units) + " preview units committed, " +
                              std::to_string(report.diagnostics.size()) + " diagnostics.";
        auto refreshed = controls->diagnostics->refresh(preview->captureDiagnostics());
        if (!refreshed) return previewResult(std::move(refreshed));
        controls->previewStatus = std::move(message);
        controls->previewReport = std::move(report);
        if (controls->invalidatePreview) controls->invalidatePreview();
        return Result<void>::success();
    }
    Result<void> defeatPreviewUnit(std::string_view subject) {
        if (busy || !app || !preview) return fail<void>("Active map preview required");
        auto id = PersistentId::parse(subject);
        if (!id || id->isNil()) return previewResult(fail<void>("Non-nil preview subject identity required"));
        return stepPreview(0, std::nullopt, std::nullopt, SubjectRef::fromPersistentId(*id));
    }
    Result<void> stopPreview() {
        if (busy || !app) return fail<void>("Open map editor required");
        Guard       guard(busy);
        std::string message = "Preview stopped. Start freezes the current committed source.";
        if (preview) {
            auto stopped = preview->stop();
            if (!stopped) return previewResult(std::move(stopped));
        }
        preview.reset();
        controls->previewAction = PreviewAction::None;
        controls->previewEvent.clear();
        controls->previewPlayersRequest.clear();
        controls->previewSubject.clear();
        controls->diagnostics.reset();
        controls->previewReport.reset();
        controls->previewTrace.reset();
        controls->previewStatus = std::move(message);
        if (controls->invalidatePreview) controls->invalidatePreview();
        return Result<void>::success();
    }
    Result<graphics::PrimitiveCanvas2D> previewScene(int width, int height) {
        using R = Result<graphics::PrimitiveCanvas2D>;
        if (!preview) return R::success(graphics::PrimitiveCanvas2D{});
        auto viewport = app->host().backgroundViewport(width, height);
        if (!viewport) return R::failure(viewport.status());
        PreviewRects              rectangles(viewport.value());
        map_rpg_spawn::ActorStyle style;
        style.red   = 0.2f;
        style.green = 1.f;
        style.blue  = 0.4f;
        auto trace  = preview->drawPlacementTrace(rectangles);
        if (!trace) return R::failure(trace.status());
        auto drawn = preview->draw(rectangles, style);
        if (!drawn) return R::failure(drawn.status());
        if (rectangles.error) return R::failure(*rectangles.error);
        return R::success(std::move(rectangles.primitives));
    }
    Result<void> render(graphics::Graphics* graphics, graphics::Canvas* target, ui::UI* ui) {
        if (busy || !opened() || !graphics || (app && !target) || !ui)
            return fail<void>("Open editor, graphics, target and UI required");
        if (graphics->getCanvas() && graphics->getCanvas() != static_cast<graphics::Canvas*>(graphics))
            return fail<void>("Editor screen composition requires the default graphics canvas");
        if (!ui->isBackendReady() && !ui->initBackend()) return fail<void>("Editor UI backend initialization failed");
        Guard guard(busy);
        auto  drawn = [&]() -> Result<void> {
            if (!app) return Result<void>::success();
            auto scene = previewScene(target->getWidth(), target->getHeight());
            if (scene) return app->render(*graphics, *target, scene.value(), controls->importPlan.get());
            // Keep editable background/feedback on a failed preview recording, reporting every failure.
            graphics::PrimitiveCanvas2D empty;
            auto                        background = app->render(*graphics, *target, empty, controls->importPlan.get());
            auto                        diagnostics = scene.diagnostics();
            if (!background)
                diagnostics.insert(diagnostics.end(), background.diagnostics().begin(), background.diagnostics().end());
            return Result<void>::failure(Status(scene.status().code(), std::move(diagnostics)));
        }();
        if (!drawn) {
            controls->previewFailure = drawn.status();
            mount->panel->markDirty();
        } else if (controls->previewFailure) {
            controls->previewFailure.reset();
            mount->panel->markDirty();
        }
        if (app) {
            const auto rect = app->host().layout().canvas;
            graphics->drawTexturedRectRGBA(target->getTexture(), rect.x, rect.y, rect.width, rect.height, 1, 1, 1, 1);
        }
        ecs::ScopedTable scope(*world());
        ui->beginFrameAndRender();
        return drawn;
    }
};
RPGSpawnEditor::RPGSpawnEditor() : impl_(std::make_unique<Impl>()) {}
RPGSpawnEditor::~RPGSpawnEditor() = default;
Module_IMPL(RPGSpawnEditor, new RPGSpawnEditor());
void RPGSpawnEditor::expose(ssq::Table& table) {
    auto cls = table.addClass(name, RPGSpawnEditor::create, false);
    expose(cls);
}
void RPGSpawnEditor::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("publishUnit", [vm](RPGSpawnEditor* self, const std::string& json) {
        return script::projectResult(vm, self->impl_->publish(json, true));
    });
    cls.addFunc("publishTemplate", [vm](RPGSpawnEditor* self, const std::string& json) {
        return script::projectResult(vm, self->impl_->publish(json, false));
    });
    cls.addFunc("open", [vm](RPGSpawnEditor* self, window::Window* window, filesystem::Filesystem* files,
                             const std::string& binding, const std::string& contract, const std::string& root,
                             const std::string& asset, const std::string& uri, const std::string& path) {
        return script::projectResult(
            vm, self->impl_->open(window, files, binding, contract, root, asset, uri, path, EditorKind::Map));
    });
    cls.addFunc("openTemplate", [vm](RPGSpawnEditor* self, window::Window* window, filesystem::Filesystem* files,
                                     const std::string& source, const std::string& root, const std::string& asset,
                                     const std::string& uri, const std::string& path) {
        return script::projectResult(
            vm, self->impl_->open(window, files, source, "", root, asset, uri, path, EditorKind::Template));
    });
    cls.addFunc("prepareTiledImport", [vm](RPGSpawnEditor* self, filesystem::Filesystem* files, const std::string& path,
                                           const std::string& guid, const std::string& prototype) {
        return script::projectResult(vm, self->impl_->prepareTiledImport(files, path, guid, prototype));
    });
    cls.addFunc("commitTiledImport",
                [vm](RPGSpawnEditor* self) { return script::projectResult(vm, self->impl_->commitTiledImport()); });
    cls.addFunc("cancelTiledImport",
                [vm](RPGSpawnEditor* self) { return script::projectResult(vm, self->impl_->cancelTiledImport()); });
    cls.addFunc("startPreview", [vm](RPGSpawnEditor* self, filesystem::Filesystem* files, const std::string& owner,
                                     std::int64_t seed, float radius) {
        return script::projectResult(vm, self->impl_->startPreview(files, owner, seed, radius));
    });
    cls.addFunc("stepPreview", [vm](RPGSpawnEditor* self, std::int64_t nanoseconds) {
        return script::projectResult(vm,
                                     self->impl_->stepPreview(nanoseconds, std::nullopt, std::nullopt, std::nullopt));
    });
    cls.addFunc("triggerPreviewEvent", [vm](RPGSpawnEditor* self, const std::string& event) {
        return script::projectResult(vm,
                                     self->impl_->stepPreview(0, std::string_view(event), std::nullopt, std::nullopt));
    });
    cls.addFunc("applyPreviewPlayers", [vm](RPGSpawnEditor* self, const std::string& players) {
        return script::projectResult(
            vm, self->impl_->stepPreview(0, std::nullopt, std::string_view(players), std::nullopt));
    });
    cls.addFunc("defeatPreviewUnit", [vm](RPGSpawnEditor* self, const std::string& subject) {
        return script::projectResult(vm, self->impl_->defeatPreviewUnit(subject));
    });
    cls.addFunc("stopPreview",
                [vm](RPGSpawnEditor* self) { return script::projectResult(vm, self->impl_->stopPreview()); });
    cls.addFunc("update", [vm](RPGSpawnEditor* self, window::Window* window, ui::UI* ui) {
        return script::projectResult(vm, self->impl_->update(window, ui), [](Value value) { return value; });
    });
    cls.addFunc("render", [vm](RPGSpawnEditor* self, graphics::Graphics* gfx, ssq::Object target, ui::UI* ui) {
        // The pointer binder rejects script null before entry. Decode null here, retaining the normal typed cast
        // for actual Canvas instances; neither the script object nor native borrow survives this bound call.
        auto* canvas = target.isNull() ? nullptr : target.to<graphics::Canvas*>();
        return script::projectResult(vm, self->impl_->render(gfx, canvas, ui));
    });
    cls.addFunc("shutdown", [vm](RPGSpawnEditor* self, window::Window* window, const std::string& policy) {
        return script::projectResult(vm, self->impl_->shutdown(window, policy));
    });
    cls.addFunc("isOpen", [](RPGSpawnEditor* self) { return self->impl_->opened(); });
}
}  // namespace eve::map_rpg_spawn_editor
