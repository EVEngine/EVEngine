// eve-hot-path
#include "map/spawn/editor/SpawnEditorWorkspace.h"
#include <cstddef>
#include <exception>
#include <new>
#include <type_traits>
#include "SpawnFieldProjection.inl"
#include "SpawnOverlayProjection.inl"
#include "SpawnViewportFrame.inl"
#include "map/spawn/MapSpawnLoad.h"
#include "map/spawn/editor/SpawnForm.h"
#include "map/spawn/editor/SpawnFormPanel.h"
#include "map/spawn/editor/SpawnRegionTool.h"
#include "property_access/PropertyAccess.h"
#include "spawn/SpawnMap.h"
#include "ui/Component.h"
namespace eve::spawn_editor {
namespace {
template <class T>
Result<T> failed(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), "spawn.workspace"));
}
struct Guard {
    bool& busy;
    explicit Guard(bool& value) : busy(value) { busy = true; }
    ~Guard() { busy = false; }
};
struct State {
    std::shared_ptr<SpawnForm>        form;
    map_spawn::PixelTransform         transform;
    MapViewportFrame                  frame;
    std::string                       mapId, selected;
    RegionShape                       shape = RegionShape::Circle;
    std::unique_ptr<SpawnMapViewport> viewport;
    std::shared_ptr<const std::byte>  stamp = std::make_shared<const std::byte>();
    std::vector<Diagnostic>           errors;
    std::function<void()>             invalidate;
    bool                              busy = false, closed = false;
    Result<void>                      binding() const {
        const auto value = form->view().read("map");
        if (!value || !value->isString() || value->asString() != mapId)
            return failed<void>(DiagnosticCode::Conflict, "Map binding changed; reopen this workspace");
        return Result<void>::success();
    }
    template <class T>
    void record(const Result<T>& result) {
        auto next = result.diagnostics();
        if (invalidate) invalidate();
        errors.swap(next);
    }
    Result<void> select(std::string id) {
        if (closed || busy)
            return failed<void>(DiagnosticCode::PreconditionViolation, "Workspace is closed or handling input");
        Guard guard(busy);
        auto  bound = binding();
        if (!bound) return bound;
        auto candidate = SpawnMapViewport::create(form, id, transform, frame);
        if (!candidate) return Result<void>::failure(candidate.status());
        auto selectedShape = candidate.value()->selectShape(shape);
        if (!selectedShape) return selectedShape;
        auto next = std::make_shared<const std::byte>();
        if (invalidate) invalidate();
        viewport = std::move(candidate).takeValue();
        selected = std::move(id);
        stamp    = std::move(next);
        errors.clear();
        return Result<void>::success();
    }
    Result<void> chooseShape(RegionShape choice) {
        if (closed || busy)
            return failed<void>(DiagnosticCode::PreconditionViolation, "Workspace is closed or handling input");
        if (!viewport) return failed<void>(DiagnosticCode::NotFound, "Select a spawn instance first");
        Guard guard(busy);
        auto  bound = binding();
        if (!bound) return bound;
        auto next = std::make_shared<const std::byte>();
        if (invalidate) invalidate();
        auto result = viewport->selectShape(choice);
        if (!result) return result;
        shape = choice;
        stamp = std::move(next);
        errors.clear();
        return Result<void>::success();
    }
    Result<void> clear() {
        if (closed || busy)
            return failed<void>(DiagnosticCode::PreconditionViolation, "Workspace is closed or handling input");
        auto next = std::make_shared<const std::byte>();
        if (invalidate) invalidate();
        viewport.reset();
        selected.clear();
        stamp = std::move(next);
        errors.clear();
        return Result<void>::success();
    }
    Result<void> changeFrame(MapViewportFrame candidate) {
        if (closed || busy)
            return failed<void>(DiagnosticCode::PreconditionViolation, "Workspace is closed or handling input");
        Guard guard(busy);
        auto  bound = binding();
        if (!bound) return bound;
        auto valid = detail::validateViewportFrame(candidate);
        if (!valid) return valid;
        auto next = std::make_shared<const std::byte>();
        if (invalidate) invalidate();
        if (viewport) {
            auto changed = viewport->setFrame(candidate);
            if (!changed) return changed;
        }
        frame = candidate;
        stamp = std::move(next);
        errors.clear();
        return Result<void>::success();
    }
};
std::function<void()> action(const std::shared_ptr<State>& state, std::function<Result<void>()> callback) {
    return [state, callback = std::move(callback), stamp = state->stamp, revision = state->form->view().revision()] {
        if (state->closed || state->busy || state->stamp != stamp || state->form->view().revision() != revision) {
            state->record(
                failed<void>(DiagnosticCode::Conflict, "Workspace controls changed; use the refreshed controls"));
            return;
        }
        auto result = callback();
        state->record(result);
    };
}
class Controls final : public ui::Component {
    std::shared_ptr<State>        state_;
    property_access::Subscription subscription_;

public:
    explicit Controls(std::shared_ptr<State> state) : state_(std::move(state)) {
        state_->invalidate = [this] { markDirty(); };
        subscription_      = state_->form->view().subscribe([weak = std::weak_ptr<State>(state_)](const auto&) {
            if (auto state = weak.lock(); state && state->invalidate) state->invalidate();
        });
    }
    ~Controls() override { state_->invalidate = {}; }
    ui::WidgetDesc build() override {
        const auto                  state  = state_;
        auto                        bound  = state->binding();
        const bool                  usable = bound.ok();
        std::vector<ui::WidgetDesc> rows;
        rows.push_back(ui::text("Map: " + state->mapId, "spawn/workspace/map"));
        rows.push_back(ui::text(state->selected.empty() ? "Select a spawn instance to edit its region."
                                                        : "Selected instance: " + state->selected,
                                "spawn/workspace/selection")
                           .withWrap(280));
        if (!usable)
            rows.push_back(ui::text("Map binding changed; reopen this workspace.", "spawn/workspace/binding-error"));
        if (const auto instances = state->form->view().read("instances")) {
            if (const auto* array = instances->getIf<Value::Array>())
                for (const auto& item : *array) {
                    const auto* id = item.find("id");
                    if (!id || !id->isString()) continue;
                    const auto value = id->asString();
                    auto       button =
                        ui::button("Edit region: " + value, "spawn/workspace/instance/" + detail::fieldKey({value}),
                                   action(state, [state, value] { return state->select(value); }));
                    button.enabled = usable;
                    rows.push_back(std::move(button));
                }
        }
        std::vector<ui::WidgetDesc> shapes;
        for (const auto& [shape, label] :
             {std::pair{RegionShape::Circle, "Circle"}, std::pair{RegionShape::Points, "Points"},
              std::pair{RegionShape::Polygon, "Polygon"}}) {
            auto button    = ui::button(label, "spawn/workspace/shape/" + std::string(label),
                                        action(state, [state, shape] { return state->chooseShape(shape); }));
            button.enabled = usable && bool(state->viewport);
            shapes.push_back(std::move(button));
        }
        auto shapeBar      = ui::toolbar(std::move(shapes), "spawn/workspace/shapes");
        shapeBar.alignSelf = static_cast<int>(ui::FlexAlign::Stretch);
        rows.push_back(std::move(shapeBar));
        rows.push_back(ui::button("Leave region tool", "spawn/workspace/clear",
                                  action(state, [state] { return state->clear(); })));
        rows.push_back(
            ui::text("Circle: drag. Points/Polygon: click, Enter to apply. Escape cancels.", "spawn/workspace/help")
                .withWrap(280));
        for (const auto& [name, member] :
             {std::pair{"Screen X", &MapViewportFrame::screenX}, std::pair{"Screen Y", &MapViewportFrame::screenY},
              std::pair{"Pan X", &MapViewportFrame::panX}, std::pair{"Pan Y", &MapViewportFrame::panY},
              std::pair{"Zoom", &MapViewportFrame::zoom}}) {
            auto input =
                ui::inputText(name, std::to_string(state->frame.*member), "spawn/workspace/frame/" + std::string(name));
            input.enabled      = usable;
            input.onTextChange = [state, member, stamp = state->stamp,
                                  revision = state->form->view().revision()](const std::string& text) {
                auto run = action(state, [state, member, text] {
                    auto parsed = Value::fromJson(text);
                    if (!parsed) return Result<void>::failure(parsed.status());
                    if (!parsed.value().isNumeric())
                        return failed<void>(DiagnosticCode::InvalidArgument, "Expected a numeric viewport value");
                    auto frame    = state->frame;
                    frame.*member = parsed.value().isInt64() ? static_cast<double>(parsed.value().asInt())
                                                             : parsed.value().asDouble();
                    return state->changeFrame(frame);
                });
                if (stamp != state->stamp || revision != state->form->view().revision()) {
                    state->record(failed<void>(DiagnosticCode::Conflict, "Viewport controls changed"));
                    return;
                }
                run();
            };
            rows.push_back(std::move(input));
        }
        for (std::size_t i = 0; i < state->errors.size(); ++i)
            rows.push_back(ui::text(state->errors[i].message(), "spawn/workspace/error/" + std::to_string(i)));
        return ui::column(std::move(rows), "spawn/workspace");
    }
};
}  // namespace
struct SpawnEditorWorkspace::Impl {
    std::shared_ptr<State>          state;
    std::unique_ptr<SpawnFormPanel> formPanel;
    std::unique_ptr<Controls>       controls;
    ~Impl() {
        state->closed     = true;
        state->invalidate = {};
        state->viewport.reset();
    }
};
SpawnEditorWorkspace::SpawnEditorWorkspace(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SpawnEditorWorkspace::~SpawnEditorWorkspace() = default;
Result<std::unique_ptr<SpawnEditorWorkspace>> SpawnEditorWorkspace::create(std::shared_ptr<SpawnForm>       form,
                                                                           const map_spawn::PixelTransform& transform,
                                                                           MapViewportFrame                 frame) {
    using R = Result<std::unique_ptr<SpawnEditorWorkspace>>;
    if (!form)
        return failed<std::unique_ptr<SpawnEditorWorkspace>>(DiagnosticCode::InvalidArgument, "Missing form owner");
    auto valid = detail::validateViewportFrame(frame);
    if (!valid) return R::failure(valid.status());
    auto origin = map_spawn::pixelToWorld(transform, {0, 0});
    if (!origin) return R::failure(origin.status());
    auto map = form->view().read("map");
    if (!map || !map->isString())
        return failed<std::unique_ptr<SpawnEditorWorkspace>>(DiagnosticCode::InvalidArgument,
                                                             "Workspace requires a map document");
    auto panel = createSpawnFormPanel(form);
    if (!panel) return R::failure(panel.status());
    auto state       = std::make_shared<State>();
    state->form      = std::move(form);
    state->transform = transform;
    state->frame     = frame;
    state->mapId     = map->asString();
    auto impl        = std::make_unique<Impl>();
    impl->state      = std::move(state);
    impl->formPanel  = std::move(panel).takeValue();
    impl->controls   = std::make_unique<Controls>(impl->state);
    return R::success(std::unique_ptr<SpawnEditorWorkspace>(new SpawnEditorWorkspace(std::move(impl))));
}
ui::Component&             SpawnEditorWorkspace::controlsPanel() noexcept { return *impl_->controls; }
ui::Component&             SpawnEditorWorkspace::formPanel() noexcept { return *impl_->formPanel; }
std::shared_ptr<SpawnForm> SpawnEditorWorkspace::form() const noexcept { return impl_->state->form; }
MapViewportFrame           SpawnEditorWorkspace::frame() const noexcept { return impl_->state->frame; }
map_spawn::PixelTransform  SpawnEditorWorkspace::pixelTransform() const noexcept { return impl_->state->transform; }
Result<void>               SpawnEditorWorkspace::setFrame(MapViewportFrame frame) {
    auto result = impl_->state->changeFrame(frame);
    impl_->state->record(result);
    return result;
}
Result<editor::ToolResponse> SpawnEditorWorkspace::pointerEvent(const editor::EditorPointerEvent& event) {
    auto state = impl_->state;
    if (state->busy)
        return failed<editor::ToolResponse>(DiagnosticCode::PreconditionViolation, "Workspace mutation reentry");
    // Native hover is normal before an explicit instance selection; it starts no edit.
    if (!state->viewport && event.phase == editor::EditorPointerEvent::Phase::Move)
        return Result<editor::ToolResponse>::success(editor::ToolResponse::ignored());
    if (!state->viewport)
        return failed<editor::ToolResponse>(DiagnosticCode::NotFound, "Select a spawn instance first");
    Guard guard(state->busy);
    auto  result = state->viewport->pointerEvent(event);
    if (!result)
        state->record(result);
    else if (event.phase != editor::EditorPointerEvent::Phase::Move) {
        auto next = state->viewport->diagnostics();
        if (state->invalidate) state->invalidate();
        state->errors.swap(next);
    }
    return result;
}
Result<editor::ToolResponse> SpawnEditorWorkspace::keyEvent(const editor::EditorKeyEvent& event) {
    auto state = impl_->state;
    if (state->busy)
        return failed<editor::ToolResponse>(DiagnosticCode::PreconditionViolation, "Workspace mutation reentry");
    if (!state->viewport)
        return failed<editor::ToolResponse>(DiagnosticCode::NotFound, "Select a spawn instance first");
    Guard guard(state->busy);
    auto  result = state->viewport->keyEvent(event);
    if (!result)
        state->record(result);
    else {
        auto next = state->viewport->diagnostics();
        if (state->invalidate) state->invalidate();
        state->errors.swap(next);
    }
    return result;
}
Result<editor::ToolResponse> SpawnEditorWorkspace::cancelInteraction() {
    if (impl_->state->busy)
        return failed<editor::ToolResponse>(DiagnosticCode::PreconditionViolation, "Workspace mutation reentry");
    if (!impl_->state->viewport) return Result<editor::ToolResponse>::success(editor::ToolResponse::ignored());
    return keyEvent({"Escape", true});
}
Result<void> SpawnEditorWorkspace::drawOverlay(editor::IEditorOverlay& overlay) {
    auto state = impl_->state;
    if (state->busy) return failed<void>(DiagnosticCode::PreconditionViolation, "Workspace mutation reentry");
    Guard guard(state->busy);
    auto  snapshot = state->form->captureMap();
    if (!snapshot) return Result<void>::failure(snapshot.status());
    if (snapshot.value().map->id() != state->mapId)
        return failed<void>(DiagnosticCode::Conflict, "Map binding changed; reopen this workspace");
    detail::ProjectedOverlay projected(state->transform, state->frame, overlay);
    editor::OverlayStyle     style;
    style.color     = 0xff40c8ffU;
    auto configured = detail::drawRegions(snapshot.value().map->instances(), projected, overlay, style);
    if (!configured) return configured;
    if (!state->viewport) return Result<void>::success();
    auto result = state->viewport->drawOverlay(overlay);
    if (!result) state->record(result);
    return result;
}
bool SpawnEditorWorkspace::hasPointerCapture() const noexcept {
    return impl_->state->viewport && impl_->state->viewport->hasPointerCapture();
}
std::vector<Diagnostic> SpawnEditorWorkspace::diagnostics() const { return impl_->state->errors; }
bool SpawnEditorWorkspace::hasStructureDraft() const noexcept { return impl_->formPanel->hasStructureDraft(); }
}  // namespace eve::spawn_editor
