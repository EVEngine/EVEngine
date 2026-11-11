// eve-hot-path
#include "map/spawn/editor/SpawnMapViewport.h"
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <optional>
#include "SpawnOverlayProjection.inl"
#include "SpawnViewportFrame.inl"
#include "editor/EditorPresentation.h"
#include "editor/EditorSession.h"
#include "map/spawn/MapSpawnLoad.h"
#include "map/spawn/editor/SpawnForm.h"
#include "map/spawn/editor/SpawnRegionTool.h"
#include "property_access/PropertyAccess.h"
namespace eve::spawn_editor {
namespace {
template <class T>
Result<T> invalid(std::string message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "spawn.viewport"));
}
struct Guard {
    bool& busy;
    explicit Guard(bool& value) : busy(value) { busy = true; }
    ~Guard() { busy = false; }
};
template <class T>
Result<T> reentry() {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Viewport mutation reentry"));
}
}  // namespace
struct SpawnMapViewport::Impl {
    std::shared_ptr<SpawnForm> form;
    std::string                mapId;
    Result<void>               validateMapIdentity() const {
        const auto current = form->view().read("map");
        if (!current || !current->isString() || current->asString() != mapId)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Map identity changed; recreate its viewport binding"));
        return Result<void>::success();
    }
    map_spawn::PixelTransform                       transform;
    MapViewportFrame                                frame;
    std::array<std::unique_ptr<SpawnRegionTool>, 3> tools;
    editor::EditorSession                           session;
    std::size_t                                     active = 0;
    bool                                            busy   = false;
    ~Impl() { session.clearTools(); }
};
SpawnMapViewport::SpawnMapViewport(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SpawnMapViewport::~SpawnMapViewport() = default;
Result<std::unique_ptr<SpawnMapViewport>> SpawnMapViewport::create(std::shared_ptr<SpawnForm> form, std::string id,
                                                                   const map_spawn::PixelTransform& transform,
                                                                   MapViewportFrame                 frame) {
    using R    = Result<std::unique_ptr<SpawnMapViewport>>;
    auto valid = detail::validateViewportFrame(frame);
    if (!valid) return R::failure(valid.status());
    auto point = map_spawn::pixelToWorld(transform, {0, 0});
    if (!point) return R::failure(point.status());
    if (!form) return invalid<std::unique_ptr<SpawnMapViewport>>("Missing form owner");
    auto mapId = form->view().read("map");
    if (!mapId || !mapId->isString())
        return invalid<std::unique_ptr<SpawnMapViewport>>("Viewport requires a map document");
    auto impl       = std::make_unique<Impl>();
    impl->form      = form;
    impl->mapId     = mapId->asString();
    impl->transform = transform;
    impl->frame     = frame;
    for (std::size_t i = 0; i < impl->tools.size(); ++i) {
        auto tool = SpawnRegionTool::create(form, id, static_cast<RegionShape>(i));
        if (!tool) return R::failure(tool.status());
        impl->tools[i] = std::move(tool).takeValue();
        if (!impl->session.addTool(impl->tools[i].get()))
            return invalid<std::unique_ptr<SpawnMapViewport>>("Could not register region tool");
    }
    if (!impl->session.activateTool(impl->tools[0]->descriptor().id))
        return invalid<std::unique_ptr<SpawnMapViewport>>("Could not activate region tool");
    return R::success(std::unique_ptr<SpawnMapViewport>(new SpawnMapViewport(std::move(impl))));
}
Result<void> SpawnMapViewport::setFrame(MapViewportFrame frame) {
    if (impl_->busy) return reentry<void>();
    auto valid = detail::validateViewportFrame(frame);
    if (!valid) return valid;
    impl_->session.cancelActiveTool();
    impl_->frame = frame;
    return Result<void>::success();
}
Result<void> SpawnMapViewport::selectShape(RegionShape shape) {
    if (impl_->busy) return reentry<void>();
    auto identity = impl_->validateMapIdentity();
    if (!identity) return identity;
    const auto index = static_cast<std::size_t>(shape);
    if (index >= impl_->tools.size()) return invalid<void>("Unknown region shape");
    impl_->session.cancelActiveTool();
    if (!impl_->session.activateTool(impl_->tools[index]->descriptor().id))
        return invalid<void>("Could not activate shape");
    impl_->active = index;
    return Result<void>::success();
}
Result<editor::ToolResponse> SpawnMapViewport::pointerEvent(const editor::EditorPointerEvent& input) {
    if (impl_->busy) return reentry<editor::ToolResponse>();
    Guard guard(impl_->busy);
    auto  event = input;
    if (impl_->session.hasPointerCapture() && event.pointerId != impl_->session.capturedPointerId())
        return Result<editor::ToolResponse>::success(editor::ToolResponse::ignored());
    if (event.phase == editor::EditorPointerEvent::Phase::Down) {
        auto identity = impl_->validateMapIdentity();
        if (!identity) {
            impl_->session.cancelActiveTool();
            return Result<editor::ToolResponse>::failure(identity.status());
        }
    }
    if (event.phase != editor::EditorPointerEvent::Phase::Cancel) {
        const auto& f     = impl_->frame;
        auto        world = map_spawn::pixelToWorld(
            impl_->transform, {(event.x - f.screenX) / f.zoom + f.panX, (event.y - f.screenY) / f.zoom + f.panY});
        if (!world || std::abs(world.value().x) > std::numeric_limits<float>::max() ||
            std::abs(world.value().y) > std::numeric_limits<float>::max()) {
            if (event.phase == editor::EditorPointerEvent::Phase::Up) impl_->session.cancelActiveTool();
            if (!world) return Result<editor::ToolResponse>::failure(world.status());
            return invalid<editor::ToolResponse>("Unrepresentable World2D pointer coordinate");
        }
        event.x = static_cast<float>(world.value().x);
        event.y = static_cast<float>(world.value().y);
    }
    return Result<editor::ToolResponse>::success(impl_->session.dispatchPointer(event));
}
Result<editor::ToolResponse> SpawnMapViewport::keyEvent(const editor::EditorKeyEvent& event) {
    if (impl_->busy) return reentry<editor::ToolResponse>();
    Guard guard(impl_->busy);
    return Result<editor::ToolResponse>::success(impl_->session.dispatchKey(event));
}
Result<void> SpawnMapViewport::drawOverlay(editor::IEditorOverlay& output) {
    if (impl_->busy) return reentry<void>();
    Guard                    guard(impl_->busy);
    detail::ProjectedOverlay projected(impl_->transform, impl_->frame, output);
    try {
        impl_->session.drawOverlay(projected);
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& error) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::CallbackFailure, error.what()));
    } catch (...) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::CallbackFailure, "Overlay sink threw"));
    }
    if (projected.error) return Result<void>::failure(*projected.error);
    return Result<void>::success();
}
bool SpawnMapViewport::hasPointerCapture() const noexcept { return impl_->session.hasPointerCapture(); }
std::vector<Diagnostic> SpawnMapViewport::diagnostics() const { return impl_->tools[impl_->active]->diagnostics(); }
}  // namespace eve::spawn_editor
