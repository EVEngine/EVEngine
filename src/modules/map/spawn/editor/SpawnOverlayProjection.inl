#pragma once
// Canonical screen projection shared by committed region previews and live tool feedback.
#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include "editor/EditorPresentation.h"
#include "map/spawn/MapSpawnLoad.h"
#include "map/spawn/editor/SpawnMapViewport.h"
#include "spawn/SpawnMap.h"
namespace eve::spawn_editor::detail {
class ProjectedOverlay final : public editor::IEditorOverlay {
public:
    map_spawn::PixelTransform transform;
    MapViewportFrame          frame;
    editor::IEditorOverlay&   sink;
    std::optional<Diagnostic> error;
    ProjectedOverlay(map_spawn::PixelTransform t, MapViewportFrame f, editor::IEditorOverlay& output)
        : transform(t), frame(f), sink(output) {}
    Result<editor::OverlayPoint> project(double x, double y) const {
        auto pixel = map_spawn::worldToPixel(transform, {x, y});
        if (!pixel) return Result<editor::OverlayPoint>::failure(pixel.status());
        const auto sx = std::fma(pixel.value().x - frame.panX, frame.zoom, frame.screenX);
        const auto sy = std::fma(pixel.value().y - frame.panY, frame.zoom, frame.screenY);
        if (!std::isfinite(sx) || !std::isfinite(sy) || std::abs(sx) > std::numeric_limits<float>::max() ||
            std::abs(sy) > std::numeric_limits<float>::max())
            return Result<editor::OverlayPoint>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Unrepresentable overlay coordinate", "spawn.viewport"));
        return Result<editor::OverlayPoint>::success({static_cast<float>(sx), static_cast<float>(sy), 0});
    }
    template <class T>
    bool admit(const Result<T>& result) {
        if (result.ok()) return true;
        if (!error) {
            const auto& diagnostics = result.diagnostics();
            error                   = diagnostics.empty()
                                          ? Diagnostic::error(DiagnosticCode::InvalidArgument, "Overlay projection failed")
                                          : diagnostics.front();
        }
        return false;
    }
    void worldLine(double ax, double ay, double bx, double by, const editor::OverlayStyle& style) {
        auto       first = project(ax, ay), second = project(bx, by);
        const bool goodFirst = admit(first), goodSecond = admit(second);
        if (goodFirst && goodSecond) sink.line(first.value(), second.value(), style);
    }
    void line(const editor::OverlayPoint& a, const editor::OverlayPoint& b,
              const editor::OverlayStyle& style) override {
        worldLine(a.x, a.y, b.x, b.y, style);
    }
    void rectangle(const editor::OverlayPoint& a, const editor::OverlayPoint& b,
                   const editor::OverlayStyle& style) override {
        auto       first     = project(a.x, a.y);
        auto       second    = project(b.x, b.y);
        const bool goodFirst = admit(first), goodSecond = admit(second);
        if (goodFirst && goodSecond) sink.rectangle(first.value(), second.value(), style);
    }
    void worldCircle(double x, double y, double radius, const editor::OverlayStyle& style) {
        auto center = project(x, y);
        if (!admit(center)) return;
        const double pixels = (radius / transform.scale) * frame.zoom;
        if (!std::isfinite(pixels) || pixels < 0 || pixels > std::numeric_limits<float>::max()) {
            error = Diagnostic::error(DiagnosticCode::InvalidArgument, "Unrepresentable overlay radius");
            return;
        }
        sink.circle(center.value(), static_cast<float>(pixels), style);
    }
    void circle(const editor::OverlayPoint& center, float radius, const editor::OverlayStyle& style) override {
        worldCircle(center.x, center.y, radius, style);
    }
    void text(const editor::OverlayPoint& at, const std::string& value, const editor::OverlayStyle& style) override {
        auto p = project(at.x, at.y);
        if (admit(p)) sink.text(p.value(), value, style);
    }
};
template <class Instances>
Result<void> drawRegions(const Instances& instances, ProjectedOverlay& projected, editor::IEditorOverlay& overlay,
                         const editor::OverlayStyle& style) {
    try {
        for (const auto& instance : instances) {
            std::visit(
                [&](const auto& region) {
                    using Region = std::decay_t<decltype(region)>;
                    if constexpr (std::is_same_v<Region, spawn::PointRegion>) {
                        for (const auto& point : region.points) {
                            auto p = projected.project(point.x, point.y);
                            if (!projected.admit(p)) return;
                            overlay.circle(p.value(), 3.f, style);
                        }
                    } else if constexpr (std::is_same_v<Region, spawn::CircleRegion>) {
                        projected.worldCircle(region.center.x, region.center.y, region.radius, style);
                    } else {
                        for (std::size_t i = 0; i < region.vertices.size(); ++i) {
                            const auto& a = region.vertices[i];
                            const auto& b = region.vertices[(i + 1) % region.vertices.size()];
                            projected.worldLine(a.x, a.y, b.x, b.y, style);
                            if (projected.error) return;
                        }
                    }
                },
                instance.region);
            if (projected.error) return Result<void>::failure(*projected.error);
        }
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& error) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::CallbackFailure, error.what()));
    } catch (...) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::CallbackFailure, "Configured region overlay sink threw"));
    }
    return Result<void>::success();
}
}  // namespace eve::spawn_editor::detail
