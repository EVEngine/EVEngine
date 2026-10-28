#include "procgen/spline/SplinePath.h"
#include "common/Assert.h"
#include "procgen/spline/SplineCircularArc.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace eve::procgen {
namespace {

bool finite(const SplinePoint& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.inX) &&
           std::isfinite(p.inY) && std::isfinite(p.inZ) && std::isfinite(p.outX) && std::isfinite(p.outY) &&
           std::isfinite(p.outZ) && std::isfinite(p.rollDegrees) && std::isfinite(p.rollPivotSide) &&
           std::isfinite(p.scaleX) && std::isfinite(p.scaleY) && std::isfinite(p.pitchDegrees) &&
           std::isfinite(p.yawDegrees) && p.scaleX > 0.f && p.scaleY > 0.f &&
           p.scaleXInterpolation <= SplineScaleInterpolation::CatmullRom &&
           p.rollInterpolation <= SplineRollInterpolation::SmoothStep && std::isfinite(p.spanStrength) &&
           p.spanStrength >= .01f && p.spanStrength <= 1.f && p.spanControl <= SplineSpanControl::Circular;
}

struct V3 {
    float x, y, z;
};

V3    add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3    sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3    mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3    cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(V3 value) { return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z); }
V3    normalized(V3 value) {
    const float magnitude = length(value);
    return magnitude > 1e-7f ? mul(value, 1.f / magnitude) : V3{1.f, 0.f, 0.f};
}
V3 rotateAround(V3 value, V3 axis, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return add(add(mul(value, c), mul(cross(axis, value), s)), mul(axis, dot(axis, value) * (1.f - c)));
}
V3 position(const SplinePoint& p) { return {p.x, p.y, p.z}; }

Result<detail::CircularArcXZ> circleForSpan(const std::vector<SplinePoint>& points, int aIndex, int bIndex,
                                            bool closed) {
    const auto& a        = points[static_cast<std::size_t>(aIndex)];
    const auto& b        = points[static_cast<std::size_t>(bIndex)];
    int         previous = aIndex, next = bIndex;
    if (aIndex > 0 && !a.breakBefore)
        previous = aIndex - 1;
    else if (closed && aIndex == 0)
        previous = static_cast<int>(points.size()) - 1;
    if (bIndex + 1 < static_cast<int>(points.size()) && !points[static_cast<std::size_t>(bIndex + 1)].breakBefore)
        next = bIndex + 1;
    else if (closed && bIndex == static_cast<int>(points.size()) - 1)
        next = 0;
    double dx = static_cast<double>(b.x) - a.x, dz = static_cast<double>(b.z) - a.z;
    if (a.circularFromEnd && next != bIndex) {
        dx = static_cast<double>(points[static_cast<std::size_t>(next)].x) - b.x;
        dz = static_cast<double>(points[static_cast<std::size_t>(next)].z) - b.z;
    }
    if (!a.circularFromEnd && previous != aIndex) {
        dx = static_cast<double>(a.x) - points[static_cast<std::size_t>(previous)].x;
        dz = static_cast<double>(a.z) - points[static_cast<std::size_t>(previous)].z;
    }
    if (previous != aIndex && next != bIndex) {
        const auto& before = points[static_cast<std::size_t>(previous)];
        const auto& after  = points[static_cast<std::size_t>(next)];
        if (!a.circularFromEnd)
            return detail::makeCircularLeadArcXZ(
                a.x, a.z, b.x, b.z, a.x - static_cast<double>(before.x), a.z - static_cast<double>(before.z),
                static_cast<double>(after.x) - b.x, static_cast<double>(after.z) - b.z);
        auto arc = detail::makeCircularLeadArcXZ(
            b.x, b.z, a.x, a.z, b.x - static_cast<double>(after.x), b.z - static_cast<double>(after.z),
            static_cast<double>(before.x) - a.x, static_cast<double>(before.z) - a.z);
        if (!arc.ok()) return arc;
        arc.value().reversed = true;
        return arc;
    }
    return detail::makeCircularArcXZ(
        a.x, a.z, b.x, b.z, dx, dz,
        a.circularFromEnd ? detail::CircularArcReference::End : detail::CircularArcReference::Start);
}

}  // namespace

Result<void> SplinePath::setKindResult(std::string_view kind) {
    if (kind != "linear" && kind != "catmullRom" && kind != "quadraticBezier" && kind != "bezier")
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "spline kind must be linear, catmullRom, quadraticBezier, or bezier",
            "kind", {}, "procgen.splinePath"));
    kind_ = kind;
    invalidate();
    return Result<void>::success();
}

void SplinePath::setClosed(bool closed) {
    bool changed = closed_ != closed;
    if (closed)
        for (std::size_t i = 1; i < points_.size(); ++i) {
            changed                = changed || points_[i].breakBefore;
            points_[i].breakBefore = false;
        }
    if (!changed) return;
    closed_ = closed;
    invalidate();
}

Result<void> SplinePath::addPointResult(const SplinePoint& point) {
    if (!finite(point))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "spline point must be finite",
                                                       "point", {}, "procgen.splinePath"));
    points_.push_back(point);
    invalidate();
    return Result<void>::success();
}

Result<void> SplinePath::setPointResult(int index, const SplinePoint& point) {
    if (index < 0 || index >= pointCount())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "spline point index is out of range",
                                                       "point.index", {}, "procgen.splinePath"));
    if (!finite(point))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "spline point must be finite",
                                                       "point", {}, "procgen.splinePath"));
    points_[static_cast<std::size_t>(index)] = point;
    invalidate();
    return Result<void>::success();
}

Result<void> SplinePath::setPointProfileResult(int index, float rollDegrees, float scaleX, float scaleY) {
    if (index < 0 || index >= pointCount())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "spline point index is out of range",
                                                       "point.index", {}, "procgen.splinePath"));
    if (!std::isfinite(rollDegrees) || !std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0.f ||
        scaleY <= 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "spline point profile requires finite roll and positive scales",
                                                       "point.profile", {}, "procgen.splinePath"));
    auto candidate                           = points_[static_cast<std::size_t>(index)];
    candidate.rollDegrees                    = rollDegrees;
    candidate.scaleX                         = scaleX;
    candidate.scaleY                         = scaleY;
    points_[static_cast<std::size_t>(index)] = candidate;
    invalidate();
    return Result<void>::success();
}

Result<void> SplinePath::setPointRotationResult(int index, float pitchDegrees, float yawDegrees, float rollDegrees) {
    if (index < 0 || index >= pointCount())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "spline point index is out of range",
                                                       "point.index", {}, "procgen.splinePath"));
    if (!std::isfinite(pitchDegrees) || !std::isfinite(yawDegrees) || !std::isfinite(rollDegrees))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "spline point rotation must be finite", "point.rotation", {},
                                                       "procgen.splinePath"));
    auto candidate                           = points_[static_cast<std::size_t>(index)];
    candidate.pitchDegrees                   = pitchDegrees;
    candidate.yawDegrees                     = yawDegrees;
    candidate.rollDegrees                    = rollDegrees;
    points_[static_cast<std::size_t>(index)] = candidate;
    invalidate();
    return Result<void>::success();
}

Result<void> SplinePath::setPointChunkBreakResult(int index, bool disconnected) {
    if (index <= 0 || index >= pointCount())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                       "chunk breaks require a non-first existing spline point",
                                                       "point.index", {}, "procgen.splinePath"));
    if (closed_ && disconnected)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "closed splines cannot contain disconnected chunks", "closed",
                                                       {}, "procgen.splinePath"));
    if (points_[static_cast<std::size_t>(index)].breakBefore == disconnected) return Result<void>::success();
    points_[static_cast<std::size_t>(index)].breakBefore = disconnected;
    invalidate();
    return Result<void>::success();
}

Result<void> SplinePath::removePointResult(int index) {
    if (index < 0 || index >= pointCount())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "spline point index is out of range",
                                                       "point.index", {}, "procgen.splinePath"));
    points_.erase(points_.begin() + index);
    invalidate();
    return Result<void>::success();
}

void SplinePath::clear() {
    if (points_.empty()) return;
    points_.clear();
    invalidate();
}

int SplinePath::segmentCount() const noexcept {
    if (points_.size() < 2u) return 0;
    int result = closed_ ? 1 : 0;
    for (std::size_t i = 1; i < points_.size(); ++i)
        if (!points_[i].breakBefore) ++result;
    return result;
}

int SplinePath::chunkCount() const noexcept {
    if (points_.empty()) return 0;
    int result = 1;
    for (std::size_t i = 1; i < points_.size(); ++i)
        if (points_[i].breakBefore) ++result;
    return result;
}

Result<SplinePath> SplinePath::chunkPathResult(int chunk) const {
    if (chunk < 0 || chunk >= chunkCount())
        return Result<SplinePath>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "spline chunk index is out of range", "chunk.index", {}, "procgen.splinePath"));
    SplinePath result;
    result.kind_ = kind_;
    int current  = 0;
    for (std::size_t i = 0; i < points_.size(); ++i) {
        if (i > 0u && points_[i].breakBefore) ++current;
        if (current != chunk) continue;
        auto point        = points_[i];
        point.breakBefore = false;
        result.points_.push_back(point);
    }
    result.closed_   = closed_ && chunkCount() == 1;
    result.revision_ = revision_;
    if (result.segmentCount() == 0)
        return Result<SplinePath>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                             "each spline chunk requires at least two points",
                                                             "chunk.points", {}, "procgen.splinePath"));
    return Result<SplinePath>::success(std::move(result));
}

std::vector<std::pair<int, int>> SplinePath::segmentEndpoints() const {
    std::vector<std::pair<int, int>> result;
    for (int i = 0; i + 1 < pointCount(); ++i)
        if (!points_[static_cast<std::size_t>(i + 1)].breakBefore) result.emplace_back(i, i + 1);
    if (closed_ && pointCount() > 1) result.emplace_back(pointCount() - 1, 0);
    return result;
}

Result<void> SplinePath::validateReady() const {
    if (segmentCount() == 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "spline requires at least two points", "points", {},
                                                       "procgen.splinePath"));
    if (std::any_of(points_.begin(), points_.end(),
                    [](const auto& point) { return point.spanControl == SplineSpanControl::Circular; }))
        for (const auto& [a, b] : segmentEndpoints())
            if (points_[static_cast<std::size_t>(a)].spanControl == SplineSpanControl::Circular) {
                auto arc = circleForSpan(points_, a, b, closed_);
                if (!arc.ok()) return Result<void>::failure(arc.status());
                const auto& start = points_[static_cast<std::size_t>(a)];
                const auto& end   = points_[static_cast<std::size_t>(b)];
                auto        ghost = detail::scaleCircularTangentToChord(
                    detail::sampleCircularArcXZ(arc.value(), 0.), 0.,
                    {static_cast<double>(end.x) - start.x, static_cast<double>(end.y) - start.y,
                            static_cast<double>(end.z) - start.z});
                if (!ghost.ok()) return Result<void>::failure(ghost.status());
            }
    return Result<void>::success();
}

SplineSample SplinePath::evaluateUnchecked(float t) const {
    t                    = std::clamp(t, 0.f, 1.f);
    const int   segments = segmentCount();
    const float scaled   = t * static_cast<float>(segments);
    const int   segment  = std::min(static_cast<int>(scaled), segments - 1);
    const float u        = segment == segments - 1 && t == 1.f ? 1.f : scaled - static_cast<float>(segment);
    return evaluateSegmentUnchecked(segment, u, t);
}

SplineSample SplinePath::evaluateSegmentUnchecked(int segment, float u, float normalizedDistance) const {
    const auto  endpoints  = segmentEndpoints();
    const int   aIndex     = endpoints[static_cast<std::size_t>(segment)].first;
    const int   bIndex     = endpoints[static_cast<std::size_t>(segment)].second;
    const auto& a          = points_[static_cast<std::size_t>(aIndex)];
    const auto& b          = points_[static_cast<std::size_t>(bIndex)];
    int         chunkIndex = 0;
    for (int i = 1; i <= aIndex; ++i)
        if (points_[static_cast<std::size_t>(i)].breakBefore) ++chunkIndex;
    V3    value{}, tangent{};
    float scaleX = std::lerp(a.scaleX, b.scaleX, u);
    if (kind_ == "linear") {
        value   = add(position(a), mul(sub(position(b), position(a)), u));
        tangent = sub(position(b), position(a));
    } else if (kind_ == "quadraticBezier") {
        const V3    p0 = position(a), p2 = position(b);
        const V3    outgoing = add(p0, {a.outX, a.outY, a.outZ});
        const V3    incoming = add(p2, {b.inX, b.inY, b.inZ});
        const V3    p1       = mul(add(outgoing, incoming), 0.5f);
        const float v        = 1.f - u;
        value                = add(add(mul(p0, v * v), mul(p1, 2.f * v * u)), mul(p2, u * u));
        tangent              = add(mul(sub(p1, p0), 2.f * v), mul(sub(p2, p1), 2.f * u));
    } else if (kind_ == "bezier") {
        const V3    p0 = position(a), p1 = add(p0, {a.outX, a.outY, a.outZ});
        const V3    p3 = position(b), p2 = add(p3, {b.inX, b.inY, b.inZ});
        const float v = 1.f - u;
        value =
            add(add(mul(p0, v * v * v), mul(p1, 3.f * v * v * u)), add(mul(p2, 3.f * v * u * u), mul(p3, u * u * u)));
        tangent = add(add(mul(sub(p1, p0), 3.f * v * v), mul(sub(p2, p1), 6.f * v * u)), mul(sub(p3, p2), 3.f * u * u));
    } else {
        int previous = aIndex;
        if (aIndex > 0 && !points_[static_cast<std::size_t>(aIndex)].breakBefore)
            previous = aIndex - 1;
        else if (closed_ && aIndex == 0)
            previous = pointCount() - 1;
        int next = bIndex;
        if (bIndex + 1 < pointCount() && !points_[static_cast<std::size_t>(bIndex + 1)].breakBefore)
            next = bIndex + 1;
        else if (closed_ && bIndex == pointCount() - 1)
            next = 0;
        const V3    p0 = position(points_[static_cast<std::size_t>(previous)]);
        const V3    p1 = position(a), p2 = position(b);
        const V3    p3 = position(points_[static_cast<std::size_t>(next)]);
        const float u2 = u * u, u3 = u2 * u;
        if (a.scaleXInterpolation == SplineScaleInterpolation::CatmullRom) {
            const double p0s = points_[static_cast<std::size_t>(previous)].scaleX, p1s = a.scaleX, p2s = b.scaleX,
                         p3s   = points_[static_cast<std::size_t>(next)].scaleX;
            const double cubic = .5 * (2.0 * p1s + (-p0s + p2s) * u + (2.0 * p0s - 5.0 * p1s + 4.0 * p2s - p3s) * u2 +
                                       (-p0s + 3.0 * p1s - 3.0 * p2s + p3s) * u3);
            scaleX =
                static_cast<float>(std::clamp(cubic, 1e-4, static_cast<double>(std::numeric_limits<float>::max())));
        }
        value   = mul(add(add(mul(p1, 2.f), mul(sub(p2, p0), u)),
                          add(mul(add(add(mul(p0, 2.f), mul(p1, -5.f)), add(mul(p2, 4.f), mul(p3, -1.f))), u2),
                              mul(add(add(mul(p0, -1.f), mul(p1, 3.f)), add(mul(p2, -3.f), p3)), u3))),
                      0.5f);
        tangent = mul(
            add(sub(p2, p0), add(mul(add(add(mul(p0, 2.f), mul(p1, -5.f)), add(mul(p2, 4.f), mul(p3, -1.f))), 2.f * u),
                                 mul(add(add(mul(p0, -1.f), mul(p1, 3.f)), add(mul(p2, -3.f), p3)), 3.f * u2))),
            0.5f);
        const bool circularStart = previous != aIndex && points_[static_cast<std::size_t>(previous)].spanControl ==
                                                             SplineSpanControl::Circular;
        const bool circularEnd = next != bIndex && b.spanControl == SplineSpanControl::Circular;
        const bool alignStart  = a.spanControl == SplineSpanControl::Inherit &&
                                (a.alignStraightStart || circularStart) && previous != aIndex &&
                                points_[static_cast<std::size_t>(previous)].spanControl != SplineSpanControl::Inherit;
        const bool alignEnd = a.spanControl == SplineSpanControl::Inherit && (a.alignStraightEnd || circularEnd) &&
                              next != bIndex && b.spanControl != SplineSpanControl::Inherit;
        const float strength = a.spanControl == SplineSpanControl::Inherit ? a.spanStrength : .5f;
        if (strength != .5f || alignStart || alignEnd) {
            V3   d1 = mul(sub(p2, p0), strength), d2 = mul(sub(p3, p1), strength);
            auto align = [&](V3& derivative, V3 axis) {
                const double square = static_cast<double>(axis.x) * axis.x + static_cast<double>(axis.z) * axis.z;
                if (square > 1e-12) {
                    const double coefficient =
                        2.0 * strength *
                        ((static_cast<double>(p2.x) - p1.x) * axis.x + (static_cast<double>(p2.z) - p1.z) * axis.z) /
                        square;
                    derivative.x = static_cast<float>(coefficient * axis.x);
                    derivative.z = static_cast<float>(coefficient * axis.z);
                }
            };
            if (alignStart) {
                V3 axis = sub(p1, p0);
                if (points_[static_cast<std::size_t>(previous)].spanControl == SplineSpanControl::Circular) {
                    auto arc = circleForSpan(points_, previous, aIndex, closed_);
                    EV_ASSERT(arc.ok(), "validated circular neighbor must remain valid");
                    auto end = detail::sampleCircularArcXZ(arc.value(), 1.);
                    if (arc.value().line && points_[static_cast<std::size_t>(previous)].circularFromEnd) {
                        end.dx = static_cast<double>(p2.x) - p1.x;
                        end.dz = static_cast<double>(p2.z) - p1.z;
                    }
                    auto ghost = detail::scaleCircularTangentToChord(
                        end, .5 * (static_cast<double>(p2.y) - p0.y),
                        {static_cast<double>(p1.x) - p0.x, static_cast<double>(p1.y) - p0.y,
                         static_cast<double>(p1.z) - p0.z});
                    EV_ASSERT(ghost.ok(), "validated circular neighbor must retain a representable chord");
                    axis = {ghost.value()[0], ghost.value()[1], ghost.value()[2]};
                    d1   = mul(add(sub(p2, p1), axis), strength);
                }
                if (!circularStart) align(d1, axis);
                if (!circularStart) d1.y = 0.f;
            }
            if (alignEnd) {
                V3 axis = sub(p3, p2);
                if (b.spanControl == SplineSpanControl::Circular) {
                    auto arc = circleForSpan(points_, bIndex, next, closed_);
                    EV_ASSERT(arc.ok(), "validated circular neighbor must remain valid");
                    auto start = detail::sampleCircularArcXZ(arc.value(), 0.);
                    if (arc.value().line && !b.circularFromEnd) {
                        start.dx = static_cast<double>(p2.x) - p1.x;
                        start.dz = static_cast<double>(p2.z) - p1.z;
                    }
                    auto ghost = detail::scaleCircularTangentToChord(
                        start, .5 * (static_cast<double>(p3.y) - p1.y),
                        {static_cast<double>(p3.x) - p2.x, static_cast<double>(p3.y) - p2.y,
                         static_cast<double>(p3.z) - p2.z});
                    EV_ASSERT(ghost.ok(), "validated circular neighbor must retain a representable chord");
                    axis = {ghost.value()[0], ghost.value()[1], ghost.value()[2]};
                    d2   = mul(add(sub(p2, p1), axis), strength);
                }
                if (!circularEnd) align(d2, axis);
                if (!circularEnd) d2.y = 0.f;
            }
            value   = add(add(mul(p1, 2.f * u3 - 3.f * u2 + 1.f), mul(d1, u3 - 2.f * u2 + u)),
                          add(mul(p2, -2.f * u3 + 3.f * u2), mul(d2, u3 - u2)));
            tangent = add(add(mul(p1, 6.f * u2 - 6.f * u), mul(d1, 3.f * u2 - 4.f * u + 1.f)),
                          add(mul(p2, -6.f * u2 + 6.f * u), mul(d2, 3.f * u2 - 2.f * u)));
        }
    }
    if (a.spanControl == SplineSpanControl::StraightXZ || a.spanControl == SplineSpanControl::StraightXYZ) {
        value.x   = std::lerp(a.x, b.x, u);
        value.z   = std::lerp(a.z, b.z, u);
        tangent.x = b.x - a.x;
        tangent.z = b.z - a.z;
        if (a.spanControl == SplineSpanControl::StraightXYZ) {
            value.y   = std::lerp(a.y, b.y, u);
            tangent.y = b.y - a.y;
        }
    }
    if (a.spanControl == SplineSpanControl::Circular) {
        auto arc = circleForSpan(points_, aIndex, bIndex, closed_);
        EV_ASSERT(arc.ok(), "validated circular span must remain valid");
        const auto evaluated   = detail::sampleCircularArcXZ(arc.value(), u);
        value.x                = static_cast<float>(evaluated.x);
        value.z                = static_cast<float>(evaluated.z);
        tangent.x              = static_cast<float>(evaluated.dx);
        tangent.z              = static_cast<float>(evaluated.dz);
        const double magnitude = std::hypot(evaluated.dx, static_cast<double>(tangent.y), evaluated.dz);
        if (std::isfinite(magnitude) && magnitude > 1e-7)
            tangent = {static_cast<float>(evaluated.dx / magnitude), static_cast<float>(tangent.y / magnitude),
                       static_cast<float>(evaluated.dz / magnitude)};
    }
    const float tangentLength = length(tangent);
    if (tangentLength > 1e-7f)
        tangent = mul(tangent, 1.f / tangentLength);
    else
        tangent = {1.f, 0.f, 0.f};
    return {value.x,
            value.y,
            value.z,
            tangent.x,
            tangent.y,
            tangent.z,
            normalizedDistance,
            std::lerp(a.rollDegrees, b.rollDegrees,
                      a.rollInterpolation == SplineRollInterpolation::SmoothStep ? u * u * (3.f - 2.f * u) : u),
            scaleX,
            std::lerp(a.scaleY, b.scaleY, u),
            chunkIndex,
            std::lerp(a.pitchDegrees, b.pitchDegrees, u),
            std::lerp(a.yawDegrees, b.yawDegrees, u),
            a.worldUpFrame,
            std::lerp(a.rollPivotSide, b.rollPivotSide,
                      a.rollInterpolation == SplineRollInterpolation::SmoothStep ? u * u * (3.f - 2.f * u) : u)};
}

Result<SplineSample> SplinePath::evaluateResult(float t) const {
    auto valid = validateReady();
    if (!valid.ok()) return Result<SplineSample>::failure(valid.status());
    if (!std::isfinite(t))
        return Result<SplineSample>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "spline parameter must be finite", "t", {}, "procgen.splinePath"));
    return Result<SplineSample>::success(evaluateUnchecked(t));
}

Result<void> SplinePath::ensureArcTable(int samplesPerSegment) const {
    auto valid = validateReady();
    if (!valid.ok()) return valid;
    if (samplesPerSegment < 2 || samplesPerSegment > 1024)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "arc samples per segment must be in [2, 1024]",
                                                       "samplesPerSegment", {}, "procgen.splinePath"));
    if (arcSamplesPerSegment_ == samplesPerSegment && !arcTable_.empty()) return Result<void>::success();
    arcTable_.clear();
    arcTable_.reserve(static_cast<std::size_t>(segmentCount() * (samplesPerSegment + 1)));
    float      distance  = 0.f;
    const auto endpoints = segmentEndpoints();
    for (int segment = 0; segment < segmentCount(); ++segment) {
        auto previous =
            evaluateSegmentUnchecked(segment, 0.f, static_cast<float>(segment) / static_cast<float>(segmentCount()));
        // Retain both endpoint parameterizations at a connected boundary so the first
        // interval of the next span interpolates from its own u=0 instead of jumping.
        arcTable_.push_back({segment, 0.f, distance});
        for (int sample = 1; sample <= samplesPerSegment; ++sample) {
            const float u       = static_cast<float>(sample) / static_cast<float>(samplesPerSegment);
            const auto  current = evaluateSegmentUnchecked(
                segment, u, (static_cast<float>(segment) + u) / static_cast<float>(segmentCount()));
            distance += length({current.x - previous.x, current.y - previous.y, current.z - previous.z});
            arcTable_.push_back({segment, u, distance});
            previous = current;
        }
    }
    arcSamplesPerSegment_ = samplesPerSegment;
    return Result<void>::success();
}

Result<float> SplinePath::lengthResult(int samplesPerSegment) const {
    auto ready = ensureArcTable(samplesPerSegment);
    if (!ready.ok()) return Result<float>::failure(ready.status());
    return Result<float>::success(arcTable_.back().distance);
}

Result<SplineSample> SplinePath::evaluateDistanceResult(float distance, int samplesPerSegment) const {
    if (!std::isfinite(distance))
        return Result<SplineSample>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "spline distance must be finite", "distance", {}, "procgen.splinePath"));
    auto ready = ensureArcTable(samplesPerSegment);
    if (!ready.ok()) return Result<SplineSample>::failure(ready.status());
    const float total  = arcTable_.back().distance;
    distance           = std::clamp(distance, 0.f, total);
    const auto upper   = std::lower_bound(arcTable_.begin(), arcTable_.end(), distance,
                                          [](const ArcEntry& entry, float value) { return entry.distance < value; });
    int        segment = 0;
    float      u       = 0.f;
    if (upper == arcTable_.begin()) {
        segment = upper->segment;
        u       = upper->u;
    } else if (upper == arcTable_.end()) {
        segment = arcTable_.back().segment;
        u       = arcTable_.back().u;
    } else {
        const auto& before = *(upper - 1);
        const float span   = upper->distance - before.distance;
        if (before.segment != upper->segment || span <= 1e-7f) {
            segment = upper->segment;
            u       = upper->u;
        } else {
            segment = upper->segment;
            u       = std::lerp(before.u, upper->u, (distance - before.distance) / span);
        }
    }
    auto sample =
        evaluateSegmentUnchecked(segment, u, (static_cast<float>(segment) + u) / static_cast<float>(segmentCount()));
    sample.normalizedDistance = total > 1e-7f ? distance / total : 0.f;
    return Result<SplineSample>::success(sample);
}

Result<SplineSample> SplinePath::closestPointResult(float x, float y, float z, int samplesPerSegment) const {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return Result<SplineSample>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "closest point query must be finite", "point", {}, "procgen.splinePath"));
    auto ready = ensureArcTable(samplesPerSegment);
    if (!ready.ok()) return Result<SplineSample>::failure(ready.status());
    float        bestSquared = 0.f;
    SplineSample best;
    bool         assigned = false;
    for (const auto& entry : arcTable_) {
        auto sample = evaluateSegmentUnchecked(
            entry.segment, entry.u, (static_cast<float>(entry.segment) + entry.u) / static_cast<float>(segmentCount()));
        const float dx = sample.x - x, dy = sample.y - y, dz = sample.z - z;
        const float squared = dx * dx + dy * dy + dz * dz;
        if (!assigned || squared < bestSquared) {
            assigned    = true;
            bestSquared = squared;
            best        = sample;
            best.normalizedDistance =
                arcTable_.back().distance > 1e-7f ? entry.distance / arcTable_.back().distance : 0.f;
        }
    }
    return Result<SplineSample>::success(best);
}

Result<std::vector<SplineFrameSample>> SplinePath::sampleFramesResult(int sampleCount, bool uniformByDistance,
                                                                      float rollDegrees, int samplesPerSegment,
                                                                      bool preserveMarkers) const {
    if (sampleCount < 1 || sampleCount > 4096)
        return Result<std::vector<SplineFrameSample>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "frame sampleCount must be in [1, 4096]", "sampleCount",
                              {}, "procgen.splinePath"));
    if (!std::isfinite(rollDegrees))
        return Result<std::vector<SplineFrameSample>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "frame roll must be finite", "rollDegrees", {}, "procgen.splinePath"));
    float totalLength = 0.f;
    if (uniformByDistance) {
        auto measured = lengthResult(samplesPerSegment);
        if (!measured.ok()) return Result<std::vector<SplineFrameSample>>::failure(measured.status());
        totalLength = measured.value();
    } else {
        auto ready = validateReady();
        if (!ready.ok()) return Result<std::vector<SplineFrameSample>>::failure(ready.status());
    }

    std::vector<std::pair<float, int>> locations;
    for (int index = 0; index <= sampleCount; ++index)
        locations.emplace_back(static_cast<float>(index) / static_cast<float>(sampleCount), -1);
    if (preserveMarkers) {
        if (chunkCount() != 1 || (uniformByDistance && totalLength <= 1e-7f))
            return Result<std::vector<SplineFrameSample>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "marker-preserving sampling requires a continuous positive-length path", "preserveMarkers"));
        if (uniformByDistance) {
            float previousAnchor = -1.f;
            for (const auto& entry : arcTable_)
                if (entry.u == 0.f) {
                    if (entry.distance <= previousAnchor)
                        return Result<std::vector<SplineFrameSample>>::failure(Diagnostic::error(
                            DiagnosticCode::InvalidArgument,
                            "marker-preserving distance sampling requires positive-length spans", "preserveMarkers"));
                    locations.emplace_back(entry.distance / totalLength, entry.segment);
                    previousAnchor = entry.distance;
                }
        } else {
            for (int segment = 0; segment < segmentCount(); ++segment)
                locations.emplace_back(static_cast<float>(segment) / static_cast<float>(segmentCount()), segment);
        }
        locations.emplace_back(1.f, segmentCount() - 1);
        std::sort(locations.begin(), locations.end(), [](const auto& a, const auto& b) {
            return a.first < b.first || (a.first == b.first && a.second > b.second);
        });
        locations.erase(std::unique(locations.begin(), locations.end(),
                                    [](const auto& a, const auto& b) { return a.first == b.first; }),
                        locations.end());
        if (locations.size() > 4097u)
            return Result<std::vector<SplineFrameSample>>::failure(
                Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                  "marker-preserving sampling exceeds the frame budget", "preserveMarkers"));
    }
    std::vector<SplineFrameSample> frames;
    frames.reserve(locations.size());
    std::vector<float> ratios;
    ratios.reserve(locations.size());
    for (std::size_t index = 0; index < locations.size(); ++index) {
        const auto [ratio, marker] = locations[index];
        auto sampled =
            uniformByDistance ? evaluateDistanceResult(totalLength * ratio, samplesPerSegment) : evaluateResult(ratio);
        if (!sampled.ok()) return Result<std::vector<SplineFrameSample>>::failure(sampled.status());
        if (marker >= 0)
            sampled = Result<SplineSample>::success(evaluateSegmentUnchecked(marker, ratio == 1.f ? 1.f : 0.f, ratio));
        SplineFrameSample frame;
        frame.sample         = sampled.value();
        frame.authoredMarker = marker >= 0;
        frames.push_back(frame);
        ratios.push_back(ratio);
    }
    return orientFrameSamplesResult(std::move(frames), ratios, rollDegrees);
}

Result<std::vector<SplineFrameSample>> SplinePath::sampleFramesAtParametersResult(std::span<const float> parameters,
                                                                                  int samplesPerSegment) const {
    if (parameters.empty() || parameters.size() > 4097u)
        return Result<std::vector<SplineFrameSample>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "explicit frame parameters require 1..4097 samples", "parameters"));
    float previous = -1.f;
    for (float parameter : parameters) {
        if (!std::isfinite(parameter) || parameter < 0.f || parameter > 1.f || parameter <= previous)
            return Result<std::vector<SplineFrameSample>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "explicit frame parameters must increase strictly within [0,1]", "parameters"));
        previous = parameter;
    }
    auto ready = ensureArcTable(samplesPerSegment);
    if (!ready.ok()) return Result<std::vector<SplineFrameSample>>::failure(ready.status());
    const float total = arcTable_.back().distance;
    if (!std::isfinite(total) || total <= 1e-7f)
        return Result<std::vector<SplineFrameSample>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "explicit frame arc distance requires a positive finite path length", "path"));
    std::vector<SplineFrameSample> frames;
    frames.reserve(parameters.size());
    for (float parameter : parameters) {
        const float scaled  = parameter * static_cast<float>(segmentCount());
        const int   segment = std::min(static_cast<int>(scaled), segmentCount() - 1);
        const float u       = parameter == 1.f ? 1.f : scaled - static_cast<float>(segment);
        const auto  upper   = std::lower_bound(
            arcTable_.begin(), arcTable_.end(), std::pair{segment, u}, [](const ArcEntry& entry, const auto& target) {
                return entry.segment < target.first || (entry.segment == target.first && entry.u < target.second);
            });
        float distance = total;
        if (upper != arcTable_.end()) {
            distance = upper->distance;
            if (upper != arcTable_.begin()) {
                const auto& before = *(upper - 1);
                if (before.segment == segment && upper->u > before.u)
                    distance = std::lerp(before.distance, upper->distance, (u - before.u) / (upper->u - before.u));
            }
        }
        SplineFrameSample frame;
        frame.sample         = evaluateSegmentUnchecked(segment, u, distance / total);
        frame.authoredMarker = u == 0.f || u == 1.f;
        frames.push_back(frame);
    }
    return orientFrameSamplesResult(std::move(frames), parameters, 0.f);
}

Result<std::vector<SplineFrameSample>> SplinePath::orientFrameSamplesResult(std::vector<SplineFrameSample> frames,
                                                                            std::span<const float>         ratios,
                                                                            float rollDegrees) const {
    EV_ASSERT(frames.size() == ratios.size(), "frame transport ratios must match samples");
    EV_ASSERT(!frames.empty(), "frame transport requires a sample");
    V3 previousTangent{}, previousSide{};
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const auto sample  = frames[index].sample;
        const V3   tangent = normalized({sample.tangentX, sample.tangentY, sample.tangentZ});
        V3         side;
        if (sample.worldUpFrame || index == 0 || sample.chunkIndex != frames[index - 1].sample.chunkIndex) {
            const float verticalLimit = sample.worldUpFrame ? .999999f : .9f;
            const V3    reference     = std::abs(tangent.y) < verticalLimit ? V3{0.f, 1.f, 0.f} : V3{1.f, 0.f, 0.f};
            side                      = normalized(cross(reference, tangent));
        } else {
            const V3    axis     = cross(previousTangent, tangent);
            const float axisSize = length(axis);
            if (axisSize > 1e-7f) {
                const float angle = std::atan2(axisSize, std::clamp(dot(previousTangent, tangent), -1.f, 1.f));
                side              = rotateAround(previousSide, mul(axis, 1.f / axisSize), angle);
            } else if (dot(previousTangent, tangent) < 0.f) {
                side = mul(previousSide, -1.f);
            } else {
                side = previousSide;
            }
            side = normalized(sub(side, mul(tangent, dot(side, tangent))));
        }
        V3 up           = normalized(cross(tangent, side));
        side            = normalized(cross(up, tangent));
        auto& frame     = frames[index];
        frame.sideX     = side.x;
        frame.sideY     = side.y;
        frame.sideZ     = side.z;
        frame.upX       = up.x;
        frame.upY       = up.y;
        frame.upZ       = up.z;
        frame.forwardX  = tangent.x;
        frame.forwardY  = tangent.y;
        frame.forwardZ  = tangent.z;
        previousTangent = tangent;
        previousSide    = side;
    }

    if (closed_ && ratios.front() == 0.f && ratios.back() == 1.f && frames.size() > 1u &&
        !frames.front().sample.worldUpFrame) {
        const V3    firstSide{frames.front().sideX, frames.front().sideY, frames.front().sideZ};
        const V3    lastSide{frames.back().sideX, frames.back().sideY, frames.back().sideZ};
        const V3    tangent{frames.front().sample.tangentX, frames.front().sample.tangentY,
                         frames.front().sample.tangentZ};
        const float correction =
            std::atan2(dot(tangent, cross(lastSide, firstSide)), std::clamp(dot(lastSide, firstSide), -1.f, 1.f));
        for (std::size_t index = 1; index < frames.size(); ++index) {
            auto&       frame = frames[static_cast<std::size_t>(index)];
            const float ratio = ratios[index];
            const V3    currentTangent{frame.sample.tangentX, frame.sample.tangentY, frame.sample.tangentZ};
            V3 side     = rotateAround({frame.sideX, frame.sideY, frame.sideZ}, currentTangent, correction * ratio);
            V3 up       = normalized(cross(currentTangent, side));
            side        = normalized(cross(up, currentTangent));
            frame.sideX = side.x;
            frame.sideY = side.y;
            frame.sideZ = side.z;
            frame.upX   = up.x;
            frame.upY   = up.y;
            frame.upZ   = up.z;
        }
        frames.back()                           = frames.front();
        frames.back().sample.normalizedDistance = 1.f;
    }

    for (auto& frame : frames) {
        V3          side{frame.sideX, frame.sideY, frame.sideZ};
        V3          up{frame.upX, frame.upY, frame.upZ};
        V3          forward{frame.sample.tangentX, frame.sample.tangentY, frame.sample.tangentZ};
        const float yaw = frame.sample.yawDegrees * 0.01745329251994329577f;
        if (std::abs(yaw) > 1e-7f) {
            forward = rotateAround(forward, up, yaw);
            side    = rotateAround(side, up, yaw);
        }
        const float pitch = frame.sample.pitchDegrees * 0.01745329251994329577f;
        if (std::abs(pitch) > 1e-7f) {
            forward = rotateAround(forward, side, pitch);
            up      = rotateAround(up, side, pitch);
        }
        const float roll = (rollDegrees + frame.sample.rollDegrees) * 0.01745329251994329577f;
        if (std::abs(roll) > 1e-7f) {
            const float c = std::cos(roll), s = std::sin(roll);
            const V3    rolledSide = add(mul(side, c), mul(up, s));
            const V3    shift      = mul(sub(side, rolledSide), frame.sample.rollPivotSide * frame.sample.scaleX);
            frame.sample.x += shift.x;
            frame.sample.y += shift.y;
            frame.sample.z += shift.z;
            if (!std::isfinite(frame.sample.x) || !std::isfinite(frame.sample.y) || !std::isfinite(frame.sample.z))
                return Result<std::vector<SplineFrameSample>>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument,
                                      "roll-pivot displacement exceeds finite frame coordinates", "rollPivotSide"));
            up   = add(mul(up, c), mul(side, -s));
            side = rolledSide;
        }
        frame.sideX    = side.x;
        frame.sideY    = side.y;
        frame.sideZ    = side.z;
        frame.upX      = up.x;
        frame.upY      = up.y;
        frame.upZ      = up.z;
        frame.forwardX = forward.x;
        frame.forwardY = forward.y;
        frame.forwardZ = forward.z;
    }
    return Result<std::vector<SplineFrameSample>>::success(std::move(frames));
}

Result<SplineSample> SplineDistribution::sampleResult(int index) const {
    if (index < 0 || index >= count())
        return Result<SplineSample>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                               "spline distribution index is out of range",
                                                               "distribution.index", {}, "procgen.splinePath"));
    return Result<SplineSample>::success(frames_[static_cast<std::size_t>(index)].sample);
}

Result<SplineFrameSample> SplineDistribution::frameResult(int index) const {
    if (index < 0 || index >= count())
        return Result<SplineFrameSample>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                                    "spline distribution index is out of range",
                                                                    "distribution.index", {}, "procgen.splinePath"));
    return Result<SplineFrameSample>::success(frames_[static_cast<std::size_t>(index)]);
}

int SplinePolyline::count() const noexcept {
    int result = 0;
    for (const auto& chunk : chunks_) result += static_cast<int>(chunk.size());
    return result;
}

int SplinePolyline::chunkPointCount(int chunk) const noexcept {
    return chunk < 0 || chunk >= chunkCount() ? 0 : static_cast<int>(chunks_[static_cast<std::size_t>(chunk)].size());
}

Result<SplineSample> SplinePolyline::chunkPointResult(int chunk, int index) const {
    if (chunk < 0 || chunk >= chunkCount() || index < 0 || index >= chunkPointCount(chunk))
        return Result<SplineSample>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                               "spline polyline index is out of range",
                                                               "polyline.index", {}, "procgen.splinePath"));
    return Result<SplineSample>::success(chunks_[static_cast<std::size_t>(chunk)][static_cast<std::size_t>(index)]);
}

Result<SplineSample> SplinePolyline::pointResult(int index) const {
    if (index >= 0)
        for (const auto& chunk : chunks_) {
            if (index < static_cast<int>(chunk.size()))
                return Result<SplineSample>::success(chunk[static_cast<std::size_t>(index)]);
            index -= static_cast<int>(chunk.size());
        }
    return Result<SplineSample>::failure(Diagnostic::error(
        DiagnosticCode::NotFound, "spline polyline index is out of range", "polyline.index", {}, "procgen.splinePath"));
}

Result<SplineSample> SplinePath::travelResult(float distance, std::string_view wrapMode, int samplesPerSegment) const {
    if (!std::isfinite(distance))
        return Result<SplineSample>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                               "spline travel distance must be finite", "distance", {},
                                                               "procgen.splinePath"));
    if (wrapMode != "clamp" && wrapMode != "loop" && wrapMode != "pingPong")
        return Result<SplineSample>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "spline travel wrap mode must be clamp, loop, or pingPong", "wrapMode", {},
            "procgen.splinePath"));
    auto measured = lengthResult(samplesPerSegment);
    if (!measured.ok()) return Result<SplineSample>::failure(measured.status());
    const float total    = measured.value();
    float       resolved = distance;
    if (wrapMode == "loop" && total > 1e-7f) {
        resolved = std::fmod(distance, total);
        if (resolved < 0.f) resolved += total;
    } else if (wrapMode == "pingPong" && total > 1e-7f) {
        const float period = total * 2.f;
        resolved           = std::fmod(distance, period);
        if (resolved < 0.f) resolved += period;
        if (resolved > total) resolved = period - resolved;
    }
    return evaluateDistanceResult(resolved, samplesPerSegment);
}

Result<SplineFrameSample> SplinePath::travelFrameResult(float distance, std::string_view wrapMode,
                                                        int samplesPerSegment) const {
    auto sample = travelResult(distance, wrapMode, samplesPerSegment);
    if (!sample.ok()) return Result<SplineFrameSample>::failure(sample.status());
    const int frameIntervals = std::clamp(segmentCount() * samplesPerSegment, 1, 4096);
    auto      frames         = sampleFramesResult(frameIntervals, true, 0.f, samplesPerSegment);
    if (!frames.ok()) return Result<SplineFrameSample>::failure(frames.status());
    const float location   = sample.value().normalizedDistance * static_cast<float>(frameIntervals);
    const int   lowerIndex = std::clamp(static_cast<int>(std::floor(location)), 0, frameIntervals);
    const int   upperIndex = std::min(lowerIndex + 1, frameIntervals);
    const float blend      = location - static_cast<float>(lowerIndex);
    const auto& lower      = frames.value()[static_cast<std::size_t>(lowerIndex)];
    const auto& upper      = frames.value()[static_cast<std::size_t>(upperIndex)];
    V3          forward =
        normalized({std::lerp(lower.forwardX, upper.forwardX, blend), std::lerp(lower.forwardY, upper.forwardY, blend),
                    std::lerp(lower.forwardZ, upper.forwardZ, blend)});
    V3 side     = normalized({std::lerp(lower.sideX, upper.sideX, blend), std::lerp(lower.sideY, upper.sideY, blend),
                              std::lerp(lower.sideZ, upper.sideZ, blend)});
    side           = normalized(sub(side, mul(forward, dot(side, forward))));
    const V3 up    = normalized(cross(forward, side));
    side           = normalized(cross(up, forward));
    auto projected = sample.value();
    if (projected.rollPivotSide != 0.f) {
        const float angle        = projected.rollDegrees * .01745329251994329577f;
        const V3    unrolledSide = sub(mul(side, std::cos(angle)), mul(up, std::sin(angle)));
        const V3    shift        = mul(sub(unrolledSide, side), projected.rollPivotSide * projected.scaleX);
        projected.x += shift.x;
        projected.y += shift.y;
        projected.z += shift.z;
        if (!std::isfinite(projected.x) || !std::isfinite(projected.y) || !std::isfinite(projected.z))
            return Result<SplineFrameSample>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "roll-pivot displacement exceeds finite frame coordinates", "rollPivotSide"));
    }
    return Result<SplineFrameSample>::success(
        {projected, side.x, side.y, side.z, up.x, up.y, up.z, forward.x, forward.y, forward.z});
}

Result<SplineDistribution> SplinePath::distributeResult(int instanceCount, bool includeEnd,
                                                        int samplesPerSegment) const {
    if (instanceCount < 1 || instanceCount > 100000)
        return Result<SplineDistribution>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                     "spline instanceCount must be in [1, 100000]",
                                                                     "instanceCount", {}, "procgen.splinePath"));
    const bool duplicateEnd = includeEnd && !closed_ && instanceCount > 1;
    const int  intervals    = duplicateEnd ? instanceCount - 1 : instanceCount;
    auto       frames       = sampleFramesResult(std::max(1, intervals), true, 0.f, samplesPerSegment);
    if (!frames.ok()) return Result<SplineDistribution>::failure(frames.status());
    frames.value().resize(static_cast<std::size_t>(instanceCount));
    return Result<SplineDistribution>::success(SplineDistribution(std::move(frames).takeValue()));
}

Result<SplinePolyline> SplinePath::polylineResult(int sampleCount, bool uniformByDistance,
                                                  int samplesPerSegment) const {
    if (sampleCount < 2 || sampleCount > 65536)
        return Result<SplinePolyline>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                 "spline polyline sampleCount must be in [2, 65536]",
                                                                 "sampleCount", {}, "procgen.splinePath"));
    if (sampleCount < chunkCount() * 2)
        return Result<SplinePolyline>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "spline polyline requires at least two samples per chunk", "sampleCount",
            {}, "procgen.splinePath"));
    std::vector<std::vector<SplineSample>> chunks;
    chunks.reserve(static_cast<std::size_t>(chunkCount()));
    int remaining = sampleCount;
    for (int chunk = 0; chunk < chunkCount(); ++chunk) {
        auto path = chunkPathResult(chunk);
        if (!path.ok()) return Result<SplinePolyline>::failure(path.status());
        const int chunksLeft   = chunkCount() - chunk;
        const int maximum      = remaining - (chunksLeft - 1) * 2;
        const int proposed     = sampleCount * path.value().segmentCount() / segmentCount();
        const int chunkSamples = chunksLeft == 1 ? remaining : std::clamp(proposed, 2, maximum);
        remaining -= chunkSamples;
        auto measured = uniformByDistance ? path.value().lengthResult(samplesPerSegment) : Result<float>::success(0.f);
        if (!measured.ok()) return Result<SplinePolyline>::failure(measured.status());
        std::vector<SplineSample> points;
        points.reserve(static_cast<std::size_t>(chunkSamples));
        const int denominator = path.value().isClosed() ? chunkSamples : chunkSamples - 1;
        for (int index = 0; index < chunkSamples; ++index) {
            const float ratio  = static_cast<float>(index) / static_cast<float>(denominator);
            auto        sample = uniformByDistance
                                     ? path.value().evaluateDistanceResult(measured.value() * ratio, samplesPerSegment)
                                     : path.value().evaluateResult(ratio);
            if (!sample.ok()) return Result<SplinePolyline>::failure(sample.status());
            points.push_back(sample.value());
        }
        chunks.push_back(std::move(points));
    }
    return Result<SplinePolyline>::success(SplinePolyline(std::move(chunks), closed_));
}

Result<void> SplinePath::applyShapePresetResult(std::string_view preset, int pointCount, float radius, float height,
                                                float turns) {
    if (preset != "line" && preset != "circle" && preset != "arc" && preset != "spiral" && preset != "wave")
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "spline preset must be line, circle, arc, spiral, or wave",
                                                       "preset", {}, "procgen.splinePath"));
    if (pointCount < 2 || pointCount > 4096 || (preset == "circle" && pointCount < 3))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "spline preset pointCount is invalid", "pointCount", {},
                                                       "procgen.splinePath"));
    if (!std::isfinite(radius) || !std::isfinite(height) || !std::isfinite(turns) || radius <= 0.f || turns <= 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "spline preset requires finite positive radius and turns",
                                                       "preset.parameters", {}, "procgen.splinePath"));
    constexpr float          tau = 6.28318530717958647692f;
    std::vector<SplinePoint> candidate;
    candidate.reserve(static_cast<std::size_t>(pointCount));
    for (int index = 0; index < pointCount; ++index) {
        const float t = pointCount > 1 ? static_cast<float>(index) / static_cast<float>(pointCount - 1) : 0.f;
        SplinePoint point;
        if (preset == "line") {
            point = {-radius + 2.f * radius * t, height * t, 0.f};
        } else if (preset == "circle") {
            const float angle = tau * static_cast<float>(index) / static_cast<float>(pointCount);
            point             = {std::cos(angle) * radius, 0.f, std::sin(angle) * radius};
        } else if (preset == "arc") {
            const float angle = (-0.25f + 0.5f * t) * tau * turns;
            point             = {std::cos(angle) * radius, height * t, std::sin(angle) * radius};
        } else if (preset == "spiral") {
            const float angle = tau * turns * t;
            const float ring  = radius * t;
            point             = {std::cos(angle) * ring, height * t, std::sin(angle) * ring};
        } else {
            point = {-radius + 2.f * radius * t, std::sin(tau * turns * t) * height, 0.f};
        }
        candidate.push_back(point);
    }
    points_ = std::move(candidate);
    kind_   = preset == "line" ? "linear" : "catmullRom";
    closed_ = preset == "circle";
    invalidate();
    return Result<void>::success();
}

void SplinePath::invalidate() {
    ++revision_;
    arcTable_.clear();
    arcSamplesPerSegment_ = 0;
}

}  // namespace eve::procgen
