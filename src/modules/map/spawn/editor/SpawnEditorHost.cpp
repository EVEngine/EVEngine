// eve-hot-path
#include "map/spawn/editor/SpawnEditorHost.h"
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_mouse.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include "common/ECS.h"
#include "editor/EditorPresentation.h"
#include "filesystem/AtomicTextWriteLease.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/RenderSystem.h"
#include "map/TileRenderViewport.h"
#include "map/TileSystem.h"
#include "map/spawn/editor/SpawnAssetDocument.h"
#include "map/spawn/editor/SpawnAssetSavePanel.h"
#include "map/spawn/editor/SpawnEditorWorkspace.h"
#include "map/spawn/editor/SpawnExportPanel.h"
#include "map/spawn/editor/TiledSpawnImport.h"
#include "ui/Component.h"
namespace eve::spawn_editor {
namespace {
std::uint64_t nextHostSerial =
    1;  // UI-thread-only ephemeral ImGui scope, never persisted or used as an entity identity.
bool valid(SpawnHostRect r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
           r.width > 0 && r.height > 0 && std::isfinite(r.x + r.width) && std::isfinite(r.y + r.height);
}
bool overlaps(SpawnHostRect a, SpawnHostRect b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}
bool contains(SpawnHostRect r, float x, float y) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}
template <class T>
Result<T> failure(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "spawn.host"));
}
struct Guard {
    bool& busy;
    explicit Guard(bool& value) : busy(value) { busy = true; }
    ~Guard() { busy = false; }
};
}  // namespace
struct SpawnEditorHost::Impl {
    std::shared_ptr<ecs::Table>           uiWorld;
    std::unique_ptr<SpawnEditorWorkspace> workspace;
    std::unique_ptr<ui::Component>        save, exportSource;
    std::array<ui::UIHostHandle, 4>       mounts{};
    SpawnHostLayout                       layout;
    bool                                  busy = false, focused = false;
    ~Impl() {
        save.reset();
        exportSource.reset();
        workspace.reset();
        for (const auto handle : mounts)
            if (auto host = ui::UIHost::resolve(handle)) ecs::DestroyEntity(&host->get());
    }
    Result<void> live() const {
        for (const auto handle : mounts)
            if (!ui::UIHost::resolve(handle))
                return failure<void>(DiagnosticCode::StaleHandle, "Editor panel was retired; reopen the host");
        return Result<void>::success();
    }
};
SpawnEditorHost::SpawnEditorHost(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SpawnEditorHost::~SpawnEditorHost() = default;
Result<std::unique_ptr<SpawnEditorHost>> SpawnEditorHost::create(std::shared_ptr<ecs::Table>         uiWorld,
                                                                 std::shared_ptr<SpawnAssetDocument> asset,
                                                                 filesystem::AtomicTextWriteLease    filesystem,
                                                                 const map_spawn::PixelTransform&    transform,
                                                                 MapViewportFrame frame, SpawnHostLayout layout,
                                                                 std::string exportPath) {
    using R = Result<std::unique_ptr<SpawnEditorHost>>;
    if (!uiWorld || !asset || !filesystem.available())
        return failure<std::unique_ptr<SpawnEditorHost>>(DiagnosticCode::InvalidArgument, "Missing actual host owners");
    if (uiWorld->is_deferred())
        return failure<std::unique_ptr<SpawnEditorHost>>(DiagnosticCode::PreconditionViolation,
                                                         "Host mounting requires a non-deferred UI world");
    const std::array rectangles{layout.canvas, layout.controls, layout.source, layout.save, layout.exportSource};
    for (std::size_t i = 0; i < rectangles.size(); ++i) {
        if (!valid(rectangles[i]))
            return failure<std::unique_ptr<SpawnEditorHost>>(DiagnosticCode::InvalidArgument, "Invalid host rectangle");
        for (std::size_t j = 0; j < i; ++j)
            if (overlaps(rectangles[i], rectangles[j]))
                return failure<std::unique_ptr<SpawnEditorHost>>(DiagnosticCode::Conflict, "Host rectangles overlap");
    }
    if (frame.screenX != layout.canvas.x || frame.screenY != layout.canvas.y)
        return failure<std::unique_ptr<SpawnEditorHost>>(DiagnosticCode::Conflict,
                                                         "Canvas and viewport origins disagree");
    if (nextHostSerial == std::numeric_limits<std::uint64_t>::max())
        return failure<std::unique_ptr<SpawnEditorHost>>(DiagnosticCode::PreconditionViolation,
                                                         "Editor window scope identities exhausted");
    const auto serial    = nextHostSerial++;
    auto       workspace = SpawnEditorWorkspace::create(asset->form(), transform, frame);
    if (!workspace) return R::failure(workspace.status());
    auto save = createSpawnAssetSavePanel(asset);
    if (!save) return R::failure(save.status());
    auto exportSource = createSpawnExportPanel(asset->form(), std::move(filesystem), std::move(exportPath));
    if (!exportSource) return R::failure(exportSource.status());
    auto impl          = std::make_unique<Impl>();
    impl->uiWorld      = std::move(uiWorld);
    impl->layout       = layout;
    impl->workspace    = std::move(workspace).takeValue();
    impl->save         = std::move(save).takeValue();
    impl->exportSource = std::move(exportSource).takeValue();
    std::array<ui::Component*, 4>    components{&impl->workspace->controlsPanel(), &impl->workspace->formPanel(),
                                             impl->save.get(), impl->exportSource.get()};
    const std::array<const char*, 4> names{"Spawn instances and viewport", "Spawn configuration", "Spawn asset save",
                                           "Spawn runtime export"};
    ecs::ScopedTable                 scope(*impl->uiWorld);
    for (std::size_t i = 0; i < components.size(); ++i) {
        impl->mounts[i] = ui::UIHost::createHost("spawn-host/" + std::to_string(serial) + "/" + std::to_string(i));
        auto host       = ui::UIHost::resolve(impl->mounts[i]);
        if (!host)
            return failure<std::unique_ptr<SpawnEditorHost>>(DiagnosticCode::Failed, "UI panel admission failed");
        auto       meta = host->get().meta();
        const auto rect = rectangles[i + 1];
        meta->hasPos    = true;
        meta->posX      = rect.x;
        meta->posY      = rect.y;
        meta->lockPos   = true;
        meta->hasSize   = true;
        meta->sizeX     = rect.width;
        meta->sizeY     = rect.height;
        meta->lockSize  = true;
        components[i]->setWindowFrame(std::string(names[i]));
        components[i]->attach(impl->mounts[i]);
    }
    return R::success(std::unique_ptr<SpawnEditorHost>(new SpawnEditorHost(std::move(impl))));
}
SpawnEditorWorkspace&        SpawnEditorHost::workspace() noexcept { return *impl_->workspace; }
ui::Component&               SpawnEditorHost::savePanel() noexcept { return *impl_->save; }
ui::Component&               SpawnEditorHost::exportPanel() noexcept { return *impl_->exportSource; }
SpawnHostLayout              SpawnEditorHost::layout() const noexcept { return impl_->layout; }
Result<editor::ToolResponse> SpawnEditorHost::pointerEvent(const editor::EditorPointerEvent& event, bool blockedByUi) {
    if (impl_->busy) return failure<editor::ToolResponse>(DiagnosticCode::PreconditionViolation, "Host input reentry");
    Guard guard(impl_->busy);
    auto  live = impl_->live();
    if (!live) return Result<editor::ToolResponse>::failure(live.status());
    const bool captured = impl_->workspace->hasPointerCapture();
    const bool inside   = !blockedByUi && contains(impl_->layout.canvas, event.x, event.y);
    if (!captured && !inside) {
        if (event.phase == editor::EditorPointerEvent::Phase::Down) impl_->focused = false;
        return Result<editor::ToolResponse>::success(editor::ToolResponse::ignored());
    }
    auto result = impl_->workspace->pointerEvent(event);
    if (result && event.phase == editor::EditorPointerEvent::Phase::Down && result.value().handled)
        impl_->focused = true;
    return result;
}
Result<editor::ToolResponse> SpawnEditorHost::keyEvent(const editor::EditorKeyEvent& event) {
    if (impl_->busy) return failure<editor::ToolResponse>(DiagnosticCode::PreconditionViolation, "Host input reentry");
    Guard guard(impl_->busy);
    auto  live = impl_->live();
    if (!live) return Result<editor::ToolResponse>::failure(live.status());
    if (!impl_->focused && !impl_->workspace->hasPointerCapture())
        return Result<editor::ToolResponse>::success(editor::ToolResponse::ignored());
    return impl_->workspace->keyEvent(event);
}
Result<editor::ToolResponse> SpawnEditorHost::processSdlEvent(const SDL_Event& event, bool blockedMouse,
                                                              bool blockedKeyboard) {
    using Pointer = editor::EditorPointerEvent;
    auto ignored  = [] { return Result<editor::ToolResponse>::success(editor::ToolResponse::ignored()); };
    if (impl_->busy)
        return failure<editor::ToolResponse>(DiagnosticCode::PreconditionViolation, "Host native event reentry");
    const bool losesFocus =
        event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                                                                     event.window.event == SDL_WINDOWEVENT_HIDDEN ||
                                                                     event.window.event == SDL_WINDOWEVENT_CLOSE));
    if (losesFocus) {
        Guard guard(impl_->busy);
        auto  live = impl_->live();
        if (!live) return Result<editor::ToolResponse>::failure(live.status());
        impl_->focused = false;
        return impl_->workspace->cancelInteraction();
    }
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        std::string key;
        switch (event.key.keysym.sym) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER: key = "Enter"; break;
            case SDLK_ESCAPE: key = "Escape"; break;
            case SDLK_BACKSPACE: key = "Backspace"; break;
            default: return ignored();
        }
        if (blockedKeyboard && !(key == "Escape" && impl_->workspace->hasPointerCapture())) return ignored();
        const auto modifiers = event.key.keysym.mod;
        return keyEvent({std::move(key), event.type == SDL_KEYDOWN, event.key.repeat != 0, bool(modifiers & KMOD_SHIFT),
                         bool(modifiers & KMOD_CTRL), bool(modifiers & KMOD_ALT)});
    }
    if (event.type != SDL_MOUSEBUTTONDOWN && event.type != SDL_MOUSEBUTTONUP && event.type != SDL_MOUSEMOTION)
        return ignored();
    const auto which = event.type == SDL_MOUSEMOTION ? event.motion.which : event.button.which;
    if (which == SDL_TOUCH_MOUSEID) return ignored();
    if (which > static_cast<Uint32>(std::numeric_limits<int>::max()))
        return failure<editor::ToolResponse>(DiagnosticCode::InvalidArgument,
                                             "SDL mouse identity exceeds editor pointer range");
    Pointer pointer;
    pointer.pointerId = static_cast<int>(which);
    if (event.type == SDL_MOUSEMOTION) {
        pointer.phase  = Pointer::Phase::Move;
        pointer.x      = static_cast<float>(event.motion.x);
        pointer.y      = static_cast<float>(event.motion.y);
        pointer.deltaX = static_cast<float>(event.motion.xrel);
        pointer.deltaY = static_cast<float>(event.motion.yrel);
    } else {
        pointer.phase = event.type == SDL_MOUSEBUTTONDOWN ? Pointer::Phase::Down : Pointer::Phase::Up;
        pointer.x     = static_cast<float>(event.button.x);
        pointer.y     = static_cast<float>(event.button.y);
        switch (event.button.button) {
            case SDL_BUTTON_LEFT: pointer.button = 0; break;
            case SDL_BUTTON_RIGHT: pointer.button = 1; break;
            case SDL_BUTTON_MIDDLE: pointer.button = 2; break;
            default: return ignored();
        }
    }
    const auto modifiers = SDL_GetModState();
    pointer.shift        = bool(modifiers & KMOD_SHIFT);
    pointer.control      = bool(modifiers & KMOD_CTRL);
    pointer.alt          = bool(modifiers & KMOD_ALT);
    return pointerEvent(pointer, blockedMouse);
}
Result<map::TileRenderViewport> SpawnEditorHost::backgroundViewport(int targetWidth, int targetHeight) const {
    const auto rect  = impl_->layout.canvas;
    const auto frame = impl_->workspace->frame();
    if (targetWidth <= 0 || targetHeight <= 0)
        return failure<map::TileRenderViewport>(DiagnosticCode::InvalidArgument, "Invalid map target size");
    const double ratioX = double(targetWidth) / rect.width, ratioY = double(targetHeight) / rect.height;
    if (std::abs(ratioX - ratioY) > 1e-6 * std::max(ratioX, ratioY))
        return failure<map::TileRenderViewport>(DiagnosticCode::Conflict,
                                                "Map target must preserve canvas aspect ratio");
    const double centerX       = frame.panX + (rect.width * 0.5 - (frame.screenX - rect.x)) / frame.zoom;
    const double centerY       = frame.panY + (rect.height * 0.5 - (frame.screenY - rect.y)) / frame.zoom;
    const double zoom          = frame.zoom * ratioX;
    const auto   representable = [](double value) {
        return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
    };
    if (!representable(centerX) || !representable(centerY) || !representable(zoom) || static_cast<float>(zoom) <= 0)
        return failure<map::TileRenderViewport>(DiagnosticCode::InvalidArgument,
                                                "Map background projection overflows renderer range");
    return Result<map::TileRenderViewport>::success(
        {targetWidth, targetHeight, float(centerX), float(centerY), float(zoom)});
}
Result<void> SpawnEditorHost::renderBackground(std::span<const ecs::EntityHandle> layers, graphics::Graphics& graphics,
                                               graphics::Canvas& target) {
    if (impl_->busy) return failure<void>(DiagnosticCode::PreconditionViolation, "Host background reentry");
    Guard guard(impl_->busy);
    auto  live = impl_->live();
    if (!live) return live;
    auto viewport = backgroundViewport(target.getWidth(), target.getHeight());
    if (!viewport) return Result<void>::failure(viewport.status());
    std::vector<graphics::DrawItem2D> items;
    auto collected = map::TileRenderSystem::collectViewport(items, layers, viewport.value());
    if (!collected) return collected;
    for (auto& item : items) item.canvas = &target;
    auto* previous = graphics.getCanvas();  // @lifetime call-only borrow; restored before return, never retained.
    try {
        graphics.setCanvas(&target);
        target.clear(graphics.getBackgroundColor(), std::nullopt, std::nullopt);
        graphics::RenderSystem::drawItems(graphics, items, false);
    } catch (...) {
        graphics.setCanvas(previous);
        throw;
    }
    graphics.setCanvas(previous);
    return Result<void>::success();
}
namespace {
struct CanvasOverlay final : editor::IEditorOverlay {
    graphics::PrimitiveCanvas2D primitives;
    struct Text {
        glm::vec2       point;
        std::string     value;
        graphics::Color color;
        float           scale;
    };
    std::vector<Text> annotations;
    SpawnHostRect     rect;
    double            ratio;
    CanvasOverlay(SpawnHostRect bounds, double scale) : rect(bounds), ratio(scale) {}
    float number(double value) const {
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            throw std::overflow_error("Overlay target projection exceeds renderer range");
        return static_cast<float>(value);
    }
    glm::vec2 point(editor::OverlayPoint p) const {
        return {number((double(p.x) - rect.x) * ratio), number((double(p.y) - rect.y) * ratio)};
    }
    graphics::PrimitivePaint paint(const editor::OverlayStyle& style) const {
        graphics::PrimitivePaint result;
        const auto               color = style.color;
        result.color = {float((color >> 16) & 255) / 255, float((color >> 8) & 255) / 255, float(color & 255) / 255,
                        float((color >> 24) & 255) / 255};
        result.mode  = style.filled ? graphics::PaintMode::Fill : graphics::PaintMode::Stroke;
        result.stroke.width = number(style.thickness * ratio);
        return result;
    }
    void line(const editor::OverlayPoint& a, const editor::OverlayPoint& b,
              const editor::OverlayStyle& style) override {
        primitives.drawLine(point(a), point(b), paint(style));
    }
    void circle(const editor::OverlayPoint& center, float radius, const editor::OverlayStyle& style) override {
        primitives.drawCircle(point(center), number(radius * ratio), paint(style));
    }
    void rectangle(const editor::OverlayPoint& a, const editor::OverlayPoint& b,
                   const editor::OverlayStyle& style) override {
        primitives.drawRect(point(a), point(b), paint(style));
    }
    void text(const editor::OverlayPoint& p, const std::string& value, const editor::OverlayStyle& style) override {
        annotations.push_back({point(p), value, paint(style).color, number(ratio)});
    }
};
}  // namespace
Result<void> SpawnEditorHost::renderOverlay(graphics::Graphics& graphics, graphics::Canvas& target,
                                            const PreparedTiledSpawnImport* proposal) {
    if (impl_->busy) return failure<void>(DiagnosticCode::PreconditionViolation, "Host overlay rendering reentry");
    Guard guard(impl_->busy);
    auto  live = impl_->live();
    if (!live) return live;
    auto viewport = backgroundViewport(target.getWidth(), target.getHeight());
    if (!viewport) return Result<void>::failure(viewport.status());
    CanvasOverlay overlay(impl_->layout.canvas, double(target.getWidth()) / impl_->layout.canvas.width);
    auto          recorded = impl_->workspace->drawOverlay(overlay);
    if (!recorded) return recorded;
    if (proposal) {
        auto candidate = proposal->drawPreview(impl_->workspace->frame(), overlay);
        if (!candidate) return candidate;
    }
    if (!overlay.annotations.empty() && !graphics.getFont())
        return failure<void>(DiagnosticCode::PreconditionViolation,
                             "Overlay text requires an explicitly selected font");
    if (overlay.primitives.commands().empty() && overlay.primitives.triangles().empty() && overlay.annotations.empty())
        return Result<void>::success();
    auto* previous = graphics.getCanvas();  // @lifetime call-only canvas borrow, restored before return.
    try {
        graphics.setCanvas(&target);
        graphics.drawPrimitiveCanvas(overlay.primitives);
        for (const auto& text : overlay.annotations)
            graphics.print(text.value, text.point.x, text.point.y, text.color, text.scale);
    } catch (...) {
        graphics.setCanvas(previous);
        throw;
    }
    graphics.setCanvas(previous);
    return Result<void>::success();
}
Result<void> SpawnEditorHost::drawOverlay(editor::IEditorOverlay& overlay) {
    if (impl_->busy) return failure<void>(DiagnosticCode::PreconditionViolation, "Host overlay reentry");
    Guard guard(impl_->busy);
    auto  live = impl_->live();
    if (!live) return live;
    return impl_->workspace->drawOverlay(overlay);
}
}  // namespace eve::spawn_editor
