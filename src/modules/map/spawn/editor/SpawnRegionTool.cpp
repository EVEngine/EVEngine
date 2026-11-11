#include "map/spawn/editor/SpawnRegionTool.h"
#include <cmath>
#include <limits>
#include <optional>
#include "editor/EditorPresentation.h"
#include "editor/EditorSession.h"
#include "map/spawn/editor/SpawnForm.h"
#include "property_access/PropertyAccess.h"
#include "spawn/SpawnRegion.h"
namespace eve::spawn_editor {
namespace {
template <class T>
Result<T> fail(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), "spawn.region-tool"));
}
std::optional<std::size_t> instanceIndex(const Value& source, const std::string& id) {
    const auto* schema    = source.find("schema");
    const auto* instances = source.find("instances");
    if (!schema || !schema->isString() || schema->asString() != "eve.map-spawns" || !instances) return {};
    const auto* array = instances->getIf<Value::Array>();
    if (!array) return {};
    for (std::size_t i = 0; i < array->size(); ++i) {
        const auto* key = (*array)[i].find("id");
        if (key && key->isString() && key->asString() == id) return i;
    }
    return {};
}
}  // namespace
struct SpawnRegionTool::Impl {
    std::shared_ptr<SpawnForm> form;
    std::string                instance;
    RegionShape                shape;
    editor::ToolDescriptor     descriptor;
    std::optional<Value>       source;
    std::uint64_t              base = 0;
    std::vector<spawn::Point>  points;
    spawn::Point               latest;
    int                        pointer = -1;
    bool                       busy    = false;
    std::vector<Diagnostic>    diagnostics;
    void                       clear() {
        source.reset();
        points.clear();
        pointer = -1;
    }
    Result<void> begin() {
        if (form->history().pending)
            return fail<void>(DiagnosticCode::Conflict, "Retry or discard the pending form edit first");
        auto candidate = form->view().read("$source");
        if (!candidate || !instanceIndex(*candidate, instance))
            return fail<void>(DiagnosticCode::NotFound, "Missing map spawn instance");
        source = std::move(candidate);
        base   = form->view().revision();
        points.clear();
        return Result<void>::success();
    }
    void record(Result<void> result) { diagnostics = result.diagnostics(); }
};
SpawnRegionTool::SpawnRegionTool(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SpawnRegionTool::~SpawnRegionTool() = default;
Result<std::unique_ptr<SpawnRegionTool>> SpawnRegionTool::create(std::shared_ptr<SpawnForm> form, std::string id,
                                                                 RegionShape shape) {
    using R = Result<std::unique_ptr<SpawnRegionTool>>;
    if (!form || id.empty())
        return fail<std::unique_ptr<SpawnRegionTool>>(DiagnosticCode::InvalidArgument,
                                                      "Form and stable instance required");
    if (shape != RegionShape::Circle && shape != RegionShape::Points && shape != RegionShape::Polygon)
        return fail<std::unique_ptr<SpawnRegionTool>>(DiagnosticCode::InvalidArgument, "Unknown region shape");
    auto source = form->view().read("$source");
    if (!source || !instanceIndex(*source, id))
        return fail<std::unique_ptr<SpawnRegionTool>>(DiagnosticCode::NotFound, "Missing map spawn instance");
    auto impl        = std::make_unique<Impl>();
    impl->form       = std::move(form);
    impl->instance   = std::move(id);
    impl->shape      = shape;
    const auto kind  = shape == RegionShape::Circle ? "circle" : shape == RegionShape::Points ? "points" : "polygon";
    impl->descriptor = {"spawn.region." + std::string(kind) + "." + impl->instance, "Spawn " + std::string(kind), {}};
    return R::success(std::unique_ptr<SpawnRegionTool>(new SpawnRegionTool(std::move(impl))));
}
const editor::ToolDescriptor& SpawnRegionTool::descriptor() const { return impl_->descriptor; }
Result<void>                  SpawnRegionTool::commitRegion() {
    if (impl_->busy) return fail<void>(DiagnosticCode::PreconditionViolation, "Region commit reentry");
    struct Guard {
        bool& flag;
        explicit Guard(bool& f) : flag(f) { flag = true; }
        ~Guard() { flag = false; }
    } guard(impl_->busy);
    if (!impl_->source || impl_->points.empty())
        return fail<void>(DiagnosticCode::PreconditionViolation, "No region gesture");
    if (impl_->base != impl_->form->view().revision() || impl_->form->history().pending)
        return fail<void>(DiagnosticCode::Conflict, "Form changed during the region gesture");
    auto       candidate = *impl_->source;
    const auto index     = instanceIndex(candidate, impl_->instance);
    if (!index) return fail<void>(DiagnosticCode::NotFound, "Missing selected instance");
    spawn::Region region = spawn::PointRegion{impl_->points};
    if (impl_->shape == RegionShape::Circle)
        region = spawn::CircleRegion{impl_->points.front(), std::hypot(impl_->latest.x - impl_->points.front().x,
                                                                                        impl_->latest.y - impl_->points.front().y)};
    else if (impl_->shape == RegionShape::Polygon)
        region = spawn::PolygonRegion{impl_->points};
    candidate.find("instances")->at(*index).set("region", spawn::buildRegionSource(region));
    auto result = impl_->form->view().write("$source", candidate);
    if (result) impl_->clear();
    return result;
}
editor::ToolResponse SpawnRegionTool::pointerEvent(editor::EditorContext&, const editor::EditorPointerEvent& input) {
    using E          = editor::EditorPointerEvent;
    const auto event = input;
    if (impl_->busy) return editor::ToolResponse::consumed();
    if (event.phase == E::Phase::Cancel) {
        impl_->clear();
        return editor::ToolResponse::release();
    }
    if (!std::isfinite(event.x) || !std::isfinite(event.y)) {
        impl_->record(fail<void>(DiagnosticCode::InvalidArgument, "Non-finite World2D input"));
        if (event.phase == E::Phase::Up && impl_->pointer == event.pointerId) {
            impl_->pointer = -1;
            return editor::ToolResponse::release();
        }
        return editor::ToolResponse::ignored();
    }
    const spawn::Point point{event.x, event.y};
    if (event.phase == E::Phase::Down && event.button == 0) {
        if (!impl_->source || impl_->shape == RegionShape::Circle) {
            auto begun = impl_->begin();
            if (!begun) {
                impl_->record(std::move(begun));
                return editor::ToolResponse::consumed();
            }
        }
        impl_->points.push_back(point);
        impl_->latest = point;
        impl_->diagnostics.clear();
        if (impl_->shape == RegionShape::Circle) {
            impl_->pointer = event.pointerId;
            return editor::ToolResponse::capture();
        }
        return editor::ToolResponse::consumed();
    }
    if (event.phase == E::Phase::Move) {
        if (impl_->source && (impl_->shape != RegionShape::Circle || impl_->pointer == event.pointerId))
            impl_->latest = point;
        return editor::ToolResponse::ignored();
    }
    if (event.phase == E::Phase::Up && event.button == 0 && impl_->shape == RegionShape::Circle &&
        impl_->pointer == event.pointerId) {
        impl_->latest  = point;
        impl_->pointer = -1;
        impl_->record(commitRegion());
        return editor::ToolResponse::release();
    }
    return editor::ToolResponse::ignored();
}
editor::ToolResponse SpawnRegionTool::keyEvent(editor::EditorContext& context, const editor::EditorKeyEvent& event) {
    if (impl_->busy || !event.pressed || event.repeated) return editor::ToolResponse::ignored();
    if (event.key == "Escape") {
        if (auto* session = context.session())
            session->cancelActiveTool();
        else
            impl_->clear();
        return editor::ToolResponse::release();
    }
    if (event.key == "Backspace" && !impl_->points.empty()) {
        impl_->points.pop_back();
        if (impl_->points.empty()) {
            if (auto* session = context.session())
                session->cancelActiveTool();
            else
                impl_->clear();
        }
        return editor::ToolResponse::consumed();
    }
    if (event.key == "Enter" && impl_->shape != RegionShape::Circle) {
        impl_->record(commitRegion());
        return editor::ToolResponse::release();
    }
    return editor::ToolResponse::ignored();
}
void SpawnRegionTool::cancel(editor::EditorContext&) {
    if (!impl_->busy) impl_->clear();
}
void SpawnRegionTool::deactivate(editor::EditorContext& context) { cancel(context); }
void SpawnRegionTool::drawOverlay(editor::EditorContext&, editor::IEditorOverlay& overlay) {
    const auto points = impl_->points;
    const auto latest = impl_->latest;
    const auto shape  = impl_->shape;
    if (points.empty()) return;
    auto point = [](spawn::Point p) {
        return editor::OverlayPoint{static_cast<float>(p.x), static_cast<float>(p.y), 0};
    };
    editor::OverlayStyle style;
    if (shape == RegionShape::Circle) {
        const auto radius = std::hypot(latest.x - points[0].x, latest.y - points[0].y);
        if (radius <= std::numeric_limits<float>::max())
            overlay.circle(point(points[0]), static_cast<float>(radius), style);
        else
            overlay.text(point(points[0]), "Preview radius exceeds viewport range", style);
    } else if (shape == RegionShape::Points) {
        for (auto p : points) overlay.circle(point(p), 0.2f, style);
    } else {
        for (std::size_t i = 1; i < points.size(); ++i) overlay.line(point(points[i - 1]), point(points[i]), style);
        overlay.line(point(points.back()), point(points.front()), style);
    }
}
std::vector<Diagnostic> SpawnRegionTool::diagnostics() const { return impl_->diagnostics; }
}  // namespace eve::spawn_editor
