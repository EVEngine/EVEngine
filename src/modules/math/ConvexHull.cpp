#include "math/ConvexHull.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace eve::math {
namespace {
using Point = std::array<double, 3>;

Point sub(const Point& a, const Point& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Point cross(const Point& a, const Point& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const Point& a, const Point& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
double norm(const Point& a) { return std::sqrt(dot(a, a)); }

template <class P>
bool finite(const P& p) {
    return std::all_of(p.begin(), p.end(), [](auto x) { return std::isfinite(x); });
}

template <class P>
std::vector<P> uniquePoints(std::span<const P> input) {
    std::vector<P> points(input.begin(), input.end());
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    return points;
}

// Projected values carry an original vertex index alongside the XY coordinates.
template <class P>
std::vector<P> chain(const std::vector<P>& points) {
    if (points.size() <= 1) return points;
    auto turn = [](const P& a, const P& b, const P& c) {
        return (double(b[0]) - a[0]) * (double(c[1]) - a[1]) - (double(b[1]) - a[1]) * (double(c[0]) - a[0]);
    };
    std::vector<P> hull;
    for (const auto& p : points) {
        while (hull.size() >= 2 && turn(hull[hull.size() - 2], hull.back(), p) <= 0) hull.pop_back();
        hull.push_back(p);
    }
    const auto lower = hull.size();
    for (auto it = points.rbegin() + 1; it != points.rend(); ++it) {
        while (hull.size() > lower && turn(hull[hull.size() - 2], hull.back(), *it) <= 0) hull.pop_back();
        hull.push_back(*it);
    }
    hull.pop_back();
    return hull;
}

template <class T>
Result<T> invalid(const char* message, const char* path) {
    return Result<T>::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error, message, path));
}
}  // namespace

Result<ConvexHull2D> convexHull2D(std::span<const HullPoint2> points) {
    for (const auto& p : points)
        if (!finite(p)) return invalid<ConvexHull2D>("Hull coordinates must be finite", "points");
    ConvexHull2D hull;
    hull.vertices = chain(uniquePoints(points));
    switch (hull.vertices.size()) {
        case 0: hull.dimension = HullDimension::Empty; break;
        case 1: hull.dimension = HullDimension::Point; break;
        case 2: hull.dimension = HullDimension::Segment; break;
        default: hull.dimension = HullDimension::Plane; break;
    }
    return Result<ConvexHull2D>::success(std::move(hull));
}

Result<ConvexHull3D> convexHull3D(std::span<const HullPoint3> input, double tolerance) {
    if (!std::isfinite(tolerance) || tolerance < 0 || tolerance >= 1)
        return invalid<ConvexHull3D>("Hull tolerance must be finite and in [0, 1)", "tolerance");
    for (const auto& p : input)
        if (!finite(p)) return invalid<ConvexHull3D>("Hull coordinates must be finite", "points");
    const auto   original = uniquePoints(input);
    ConvexHull3D hull;
    if (original.size() <= 1) {
        hull.dimension = original.empty() ? HullDimension::Empty : HullDimension::Point;
        hull.vertices  = original;
        return Result<ConvexHull3D>::success(std::move(hull));
    }
    double scale = 0;
    for (std::size_t k = 0; k < 3; ++k) {
        auto [lo, hi] = std::minmax_element(original.begin(), original.end(),
                                            [k](const auto& a, const auto& b) { return a[k] < b[k]; });
        scale         = std::max(scale, double((*hi)[k]) - (*lo)[k]);
    }
    std::vector<Point> pts;
    for (const auto& p : original)
        pts.push_back({(double(p[0]) - original[0][0]) / scale, (double(p[1]) - original[0][1]) / scale,
                       (double(p[2]) - original[0][2]) / scale});
    auto farthest = [&](auto distance) {
        std::size_t best    = 0;
        double      maximum = -1;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const double value = distance(i);
            if (value > maximum) {
                maximum = value;
                best    = i;
            }
        }
        return best;
    };
    const std::size_t a      = 0;
    const auto        b      = farthest([&](auto i) { return norm(sub(pts[i], pts[a])); });
    const auto        ab     = sub(pts[b], pts[a]);
    const auto        c      = farthest([&](auto i) { return norm(cross(ab, sub(pts[i], pts[a]))); });
    const auto        normal = cross(ab, sub(pts[c], pts[a]));
    if (norm(normal) <= tolerance * norm(ab)) {
        const auto lo  = farthest([&](auto i) { return -dot(pts[i], ab); });
        const auto hi  = farthest([&](auto i) { return dot(pts[i], ab); });
        hull.dimension = HullDimension::Segment;
        hull.vertices  = {original[lo], original[hi]};
        return Result<ConvexHull3D>::success(std::move(hull));
    }
    const auto d = farthest([&](auto i) { return std::abs(dot(normal, sub(pts[i], pts[a]))); });
    if (std::abs(dot(normal, sub(pts[d], pts[a]))) <= tolerance * norm(normal)) {
        std::size_t drop = 0;
        for (std::size_t k = 1; k < 3; ++k)
            if (std::abs(normal[k]) > std::abs(normal[drop])) drop = k;
        const auto x = (drop + 1) % 3, y = (drop + 2) % 3;
        // Index travels with the projection without conversion to floating point.
        struct Projected {
            double      x, y;
            std::size_t index;
            double      operator[](std::size_t k) const { return k == 0 ? x : y; }
        };
        std::vector<Projected> projected;
        for (std::size_t i = 0; i < pts.size(); ++i) projected.push_back({pts[i][x], pts[i][y], i});
        std::sort(projected.begin(), projected.end(), [](const auto& p, const auto& q) {
            if (p.x != q.x) return p.x < q.x;
            if (p.y != q.y) return p.y < q.y;
            return p.index < q.index;
        });
        // Projection can collapse distinct nearly planar points.
        projected.erase(std::unique(projected.begin(), projected.end(),
                                    [](const auto& p, const auto& q) { return p.x == q.x && p.y == q.y; }),
                        projected.end());
        hull.dimension = HullDimension::Plane;
        for (const auto& p : chain(projected)) hull.vertices.push_back(original[p.index]);
        for (std::size_t i = 1; i + 1 < hull.vertices.size(); ++i) hull.faces.push_back({0, i, i + 1});
        return Result<ConvexHull3D>::success(std::move(hull));
    }
    Point inside{};
    for (auto i : {a, b, c, d})
        for (std::size_t k = 0; k < 3; ++k) inside[k] += pts[i][k] / 4;
    auto normalOf = [&](const HullTriangle& f) { return cross(sub(pts[f[1]], pts[f[0]]), sub(pts[f[2]], pts[f[0]])); };
    auto outward  = [&](std::size_t i, std::size_t j, std::size_t k) {
        HullTriangle f{i, j, k};
        if (dot(normalOf(f), sub(inside, pts[i])) > 0) std::swap(f[1], f[2]);
        return f;
    };
    std::vector<HullTriangle> faces{outward(a, b, c), outward(a, d, b), outward(a, c, d), outward(b, d, c)};
    for (std::size_t p = 0; p < pts.size(); ++p) {
        if (p == a || p == b || p == c || p == d) continue;
        std::map<std::pair<std::size_t, std::size_t>, std::pair<std::size_t, std::size_t>> horizon;
        std::vector<HullTriangle>                                                          retained;
        bool                                                                               visible = false;
        for (const auto& f : faces) {
            const auto n = normalOf(f);
            if (dot(n, sub(pts[p], pts[f[0]])) <= tolerance * norm(n)) {
                retained.push_back(f);
                continue;
            }
            visible = true;
            for (std::size_t e = 0; e < 3; ++e) {
                const auto                                u = f[e], v = f[(e + 1) % 3];
                const std::pair<std::size_t, std::size_t> key{std::min(u, v), std::max(u, v)};
                if (horizon.erase(key) == 0) horizon.emplace(key, std::make_pair(u, v));
            }
        }
        if (!visible) continue;
        for (const auto& edge : horizon) retained.push_back(outward(edge.second.first, edge.second.second, p));
        faces = std::move(retained);
    }
    std::vector<std::size_t> remap(pts.size(), pts.size());
    for (const auto& f : faces)
        for (auto i : f) remap[i] = 0;
    for (std::size_t i = 0; i < pts.size(); ++i)
        if (remap[i] == 0) {
            remap[i] = hull.vertices.size();
            hull.vertices.push_back(original[i]);
        }
    hull.dimension = HullDimension::Volume;
    for (const auto& f : faces) hull.faces.push_back({remap[f[0]], remap[f[1]], remap[f[2]]});
    return Result<ConvexHull3D>::success(std::move(hull));
}
}  // namespace eve::math
