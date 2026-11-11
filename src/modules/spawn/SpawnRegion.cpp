// eve-hot-path
// bounded geometry shared by spatial planning and activation.
#include "spawn/SpawnRegion.h"
#include "spawn/SpawnRegionInternal.inl"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace eve::spawn {
namespace {
template <class T>
Result<T> invalid() {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid or out-of-range region query", "spawn.region"));
}
long double segmentDistance(Point p, Point a, Point b) {
    const long double dx = static_cast<long double>(b.x) - a.x, dy = static_cast<long double>(b.y) - a.y;
    const long double px = static_cast<long double>(p.x) - a.x, py = static_cast<long double>(p.y) - a.y;
    if (px * dy == py * dx && p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) && p.y >= std::min(a.y, b.y) &&
        p.y <= std::max(a.y, b.y))
        return 0;
    const auto length = dx * dx + dy * dy;
    const auto t      = length == 0 ? 0.L : std::clamp((px * dx + py * dy) / length, 0.L, 1.L);
    return std::hypot(px - t * dx, py - t * dy);
}
long double polygonDistance(const PolygonRegion& polygon, Point p) {
    bool        inside   = false;
    long double distance = std::numeric_limits<long double>::infinity();
    for (std::size_t i = 0; i < polygon.vertices.size(); ++i) {
        const auto a = polygon.vertices[i], b = polygon.vertices[(i + 1) % polygon.vertices.size()];
        distance = std::min(distance, segmentDistance(p, a, b));
        if ((a.y > p.y) != (b.y > p.y)) {
            const auto crossing = static_cast<long double>(a.x) + (static_cast<long double>(p.y) - a.y) *
                                                                      (static_cast<long double>(b.x) - a.x) /
                                                                      (static_cast<long double>(b.y) - a.y);
            if (static_cast<long double>(p.x) < crossing) inside = !inside;
        }
    }
    return inside ? 0.L : distance;
}
}  // namespace
Result<double> distanceToRegion(const CompiledMap& map, std::size_t instance, Point point) {
    if (instance >= map.instances().size()) return invalid<double>();
    return detail::distanceToValidatedRegion(map.instances()[instance].region, point);
}
Result<double> detail::distanceToValidatedRegion(const Region& region, Point point) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return invalid<double>();
    const auto result = std::visit(
        [&](const auto& region) -> long double {
            using T = std::decay_t<decltype(region)>;
            if constexpr (std::is_same_v<T, CircleRegion>) {
                return std::max(0.L, std::hypot(static_cast<long double>(point.x) - region.center.x,
                                                static_cast<long double>(point.y) - region.center.y) -
                                         region.radius);
            } else if constexpr (std::is_same_v<T, PolygonRegion>) {
                return polygonDistance(region, point);
            } else {
                long double nearest = std::numeric_limits<long double>::infinity();
                for (auto p : region.points)
                    nearest = std::min(nearest, std::hypot(static_cast<long double>(point.x) - p.x,
                                                           static_cast<long double>(point.y) - p.y));
                return nearest;
            }
        },
        region);
    if (!std::isfinite(result) || result > std::numeric_limits<double>::max()) return invalid<double>();
    return Result<double>::success(static_cast<double>(result));
}
Result<std::optional<Point>> sampleRegion(const CompiledMap& map, std::size_t instance, double u, double v) {
    if (instance >= map.instances().size() || !std::isfinite(u) || !std::isfinite(v) || u < 0 || u >= 1 || v < 0 ||
        v >= 1)
        return invalid<std::optional<Point>>();
    auto candidate = std::visit(
        [&](const auto& region) -> std::optional<Point> {
            using T = std::decay_t<decltype(region)>;
            if constexpr (std::is_same_v<T, CircleRegion>) {
                const auto radius = region.radius * std::sqrt(u), angle = 2 * std::numbers::pi * v;
                return Point{region.center.x + radius * std::cos(angle), region.center.y + radius * std::sin(angle)};
            } else if constexpr (std::is_same_v<T, PointRegion>) {
                if (region.points.empty()) return std::nullopt;
                const auto index =
                    std::min(region.points.size() - 1, static_cast<std::size_t>(u * region.points.size()));
                return region.points[index];
            } else {
                auto minimum = region.vertices.front(), maximum = minimum;
                for (auto p : region.vertices) {
                    minimum.x = std::min(minimum.x, p.x);
                    minimum.y = std::min(minimum.y, p.y);
                    maximum.x = std::max(maximum.x, p.x);
                    maximum.y = std::max(maximum.y, p.y);
                }
                Point p{std::lerp(minimum.x, maximum.x, u), std::lerp(minimum.y, maximum.y, v)};
                if (polygonDistance(region, p) != 0) return std::nullopt;
                return p;
            }
        },
        map.instances()[instance].region);
    if (candidate && (!std::isfinite(candidate->x) || !std::isfinite(candidate->y)))
        return invalid<std::optional<Point>>();
    return Result<std::optional<Point>>::success(candidate);
}
}  // namespace eve::spawn
