#include "graphics/PrimitiveDrawList.h"

#include "common/Assert.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <unordered_map>
#include <utility>

namespace eve::graphics {
namespace {

eve::Result<PrimitiveRecordStatus> primitiveBudgetFailure(std::size_t hardLimit, std::size_t current) {
    return eve::Result<PrimitiveRecordStatus>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::Failed, "primitive command hard limit exceeded", "commandBudget",
        {{"hardLimit", std::to_string(hardLimit)}, {"current", std::to_string(current)}}, "graphics.primitive"));
}

template <typename Point>
std::vector<float> cumulativeLengths(std::span<const Point> points, bool closed) {
    std::vector<float> lengths(points.size() + (closed ? 1u : 0u), 0.f);
    for (std::size_t i = 1; i < points.size(); ++i) {
        lengths[i] = lengths[i - 1] + glm::length(points[i] - points[i - 1]);
    }
    if (closed) lengths.back() = lengths[points.size() - 1] + glm::length(points.front() - points.back());
    return lengths;
}

template <typename Point>
void validatePoints(std::span<const Point> points, bool closed) {
    EV_PARAM_CHECK(points.size() >= 2, "a polyline requires at least two points");
    const bool validClosedCount = !closed || points.size() >= 3;
    EV_PARAM_CHECK(validClosedCount, "a closed polyline requires at least three points");
    for (const auto& point : points) {
        for (glm::length_t i = 0; i < point.length(); ++i) {
            EV_PARAM_CHECK(std::isfinite(point[i]), "polyline points must be finite");
        }
    }
}

void validateRadius(float radius) {
    const bool valid = std::isfinite(radius) && radius >= 0.f;
    EV_PARAM_CHECK(valid, "primitive radius must be finite and non-negative");
}

glm::vec2 transformPoint2D(const glm::mat3& transform, glm::vec2 point) {
    const glm::vec3 transformed = transform * glm::vec3(point, 1.f);
    return {transformed.x, transformed.y};
}

void validateSegments(std::uint32_t segments) {
    const bool valid = segments >= 3 && segments <= 4096;
    EV_PARAM_CHECK(valid, "primitive circle segments must be in [3, 4096]");
}

std::pair<glm::vec3, glm::vec3> perpendicularBasis(glm::vec3 axis) {
    const float axisLength = glm::length(axis);
    const bool  validAxis  = std::isfinite(axisLength) && axisLength > 1e-6f;
    EV_PARAM_CHECK(validAxis, "primitive axis must have non-zero finite length");
    const glm::vec3 direction = axis / axisLength;
    const glm::vec3 helper    = std::fabs(direction.y) < 0.9f ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
    const glm::vec3 u         = glm::normalize(glm::cross(direction, helper));
    return {u, glm::normalize(glm::cross(direction, u))};
}

struct CachedUnitCircle {
    std::shared_ptr<const std::vector<glm::vec2>> points;
    bool                                          hit = false;
};

CachedUnitCircle unitCircle(std::uint32_t segments) {
    static std::mutex                                                                       cacheMutex;
    static std::unordered_map<std::uint32_t, std::shared_ptr<const std::vector<glm::vec2>>> cache;
    {
        const std::scoped_lock lock(cacheMutex);
        if (const auto found = cache.find(segments); found != cache.end()) return {found->second, true};
    }
    auto generated = std::make_shared<std::vector<glm::vec2>>();
    generated->reserve(segments);
    for (std::uint32_t i = 0; i < segments; ++i) {
        const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        generated->push_back({std::cos(angle), std::sin(angle)});
    }
    const std::scoped_lock lock(cacheMutex);
    const auto [iterator, inserted] = cache.emplace(segments, generated);
    return {iterator->second, !inserted};
}

std::vector<glm::vec3> circlePoints(glm::vec3 center, glm::vec3 u, glm::vec3 v, float radius, std::uint32_t segments,
                                    PrimitiveDrawStatistics& statistics) {
    const CachedUnitCircle circle = unitCircle(segments);
    if (circle.hit) ++statistics.cacheHits;
    std::vector<glm::vec3> points;
    points.reserve(segments);
    for (const glm::vec2 unit : *circle.points) points.push_back(center + radius * (unit.x * u + unit.y * v));
    return points;
}

float signedArea(std::span<const glm::vec2> polygon) {
    float area = 0.f;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const glm::vec2 a = polygon[i];
        const glm::vec2 b = polygon[(i + 1u) % polygon.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5f;
}

bool pointInTriangle(glm::vec2 point, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
    const auto  cross = [](glm::vec2 u, glm::vec2 v) { return u.x * v.y - u.y * v.x; };
    const float ab    = cross(b - a, point - a);
    const float bc    = cross(c - b, point - b);
    const float ca    = cross(a - c, point - c);
    return (ab >= -1e-6f && bc >= -1e-6f && ca >= -1e-6f) || (ab <= 1e-6f && bc <= 1e-6f && ca <= 1e-6f);
}

bool pointInPolygon(glm::vec2 point, std::span<const glm::vec2> polygon) {
    bool inside = false;
    for (std::size_t i = 0, previous = polygon.size() - 1u; i < polygon.size(); previous = i++) {
        const glm::vec2 a = polygon[i];
        const glm::vec2 b = polygon[previous];
        if ((a.y > point.y) != (b.y > point.y) && point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

float cross2D(glm::vec2 a, glm::vec2 b, glm::vec2 c) {
    const glm::vec2 ab = b - a;
    const glm::vec2 ac = c - a;
    return ab.x * ac.y - ab.y * ac.x;
}

bool properSegmentsIntersect(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d) {
    const float abC = cross2D(a, b, c);
    const float abD = cross2D(a, b, d);
    const float cdA = cross2D(c, d, a);
    const float cdB = cross2D(c, d, b);
    return ((abC > 1e-6f && abD < -1e-6f) || (abC < -1e-6f && abD > 1e-6f)) &&
           ((cdA > 1e-6f && cdB < -1e-6f) || (cdA < -1e-6f && cdB > 1e-6f));
}

bool bridgeIsVisible(glm::vec2 holePoint, glm::vec2 outerPoint, std::span<const glm::vec2> outer,
                     std::span<const glm::vec2> hole) {
    for (std::size_t i = 0; i < outer.size(); ++i)
        if (properSegmentsIntersect(holePoint, outerPoint, outer[i], outer[(i + 1u) % outer.size()])) return false;
    for (std::size_t i = 0; i < hole.size(); ++i)
        if (properSegmentsIntersect(holePoint, outerPoint, hole[i], hole[(i + 1u) % hole.size()])) return false;
    const glm::vec2 midpoint = (holePoint + outerPoint) * 0.5f;
    return pointInPolygon(midpoint, outer) && !pointInPolygon(midpoint, hole);
}

std::vector<glm::vec2> bridgeHole(std::vector<glm::vec2> outer, std::vector<glm::vec2> hole) {
    if (signedArea(outer) < 0.f) std::reverse(outer.begin(), outer.end());
    if (signedArea(hole) > 0.f) std::reverse(hole.begin(), hole.end());
    std::size_t holeIndex = 0;
    for (std::size_t i = 1; i < hole.size(); ++i)
        if (hole[i].x > hole[holeIndex].x || (hole[i].x == hole[holeIndex].x && hole[i].y < hole[holeIndex].y))
            holeIndex = i;
    std::size_t outerIndex          = outer.size();
    float       bestDistanceSquared = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < outer.size(); ++i) {
        if (!bridgeIsVisible(hole[holeIndex], outer[i], outer, hole)) continue;
        const glm::vec2 delta           = outer[i] - hole[holeIndex];
        const float     distanceSquared = glm::dot(delta, delta);
        if (distanceSquared < bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            outerIndex          = i;
        }
    }
    EV_PARAM_CHECK(outerIndex != outer.size(), "path fill could not connect a hole to its outer contour");
    std::vector<glm::vec2> merged;
    merged.reserve(outer.size() + hole.size() + 2u);
    merged.insert(merged.end(), outer.begin(), outer.begin() + outerIndex + 1u);
    for (std::size_t offset = 0; offset <= hole.size(); ++offset)
        merged.push_back(hole[(holeIndex + offset) % hole.size()]);
    merged.push_back(outer[outerIndex]);
    merged.insert(merged.end(), outer.begin() + outerIndex + 1u, outer.end());
    return merged;
}

std::vector<std::array<std::size_t, 3>> triangulateSimplePolygon(std::span<const glm::vec2> polygon) {
    std::vector<std::array<std::size_t, 3>> triangles;
    if (polygon.size() < 3) return triangles;
    std::vector<std::size_t> remaining(polygon.size());
    std::iota(remaining.begin(), remaining.end(), 0u);
    if (signedArea(polygon) < 0.f) std::reverse(remaining.begin(), remaining.end());
    while (remaining.size() > 3) {
        bool clipped = false;
        for (std::size_t cursor = 0; cursor < remaining.size(); ++cursor) {
            const std::size_t previous = remaining[(cursor + remaining.size() - 1u) % remaining.size()];
            const std::size_t current  = remaining[cursor];
            const std::size_t next     = remaining[(cursor + 1u) % remaining.size()];
            const glm::vec2   a        = polygon[previous];
            const glm::vec2   b        = polygon[current];
            const glm::vec2   c        = polygon[next];
            const float       turn     = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
            if (turn <= 1e-6f) continue;
            bool containsVertex = false;
            for (const std::size_t candidate : remaining) {
                if (candidate == previous || candidate == current || candidate == next) continue;
                if (glm::length(polygon[candidate] - a) <= 1e-6f || glm::length(polygon[candidate] - b) <= 1e-6f ||
                    glm::length(polygon[candidate] - c) <= 1e-6f)
                    continue;
                if (pointInTriangle(polygon[candidate], a, b, c)) {
                    containsVertex = true;
                    break;
                }
            }
            if (containsVertex) continue;
            triangles.push_back({previous, current, next});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(cursor));
            clipped = true;
            break;
        }
        EV_PARAM_CHECK(clipped, "path fill contour must be a simple non-self-intersecting polygon");
    }
    triangles.push_back({remaining[0], remaining[1], remaining[2]});
    return triangles;
}

}  // namespace

PrimitiveCanvas2D::PrimitiveCanvas2D(std::size_t hardCommandLimit) : hardCommandLimit_(hardCommandLimit) {
    EV_PARAM_CHECK(hardCommandLimit_ > 0, "primitive command limit must be positive");
}

void PrimitiveCanvas2D::save() { stack_.push_back(transform_); }

void PrimitiveCanvas2D::restore() {
    EV_PARAM_CHECK(!stack_.empty(), "primitive canvas restore requires a matching save");
    transform_ = stack_.back();
    stack_.pop_back();
}

void PrimitiveCanvas2D::concat(const glm::mat3& transform) { transform_ *= transform; }

void PrimitiveCanvas2D::drawLine(glm::vec2 a, glm::vec2 b, const PrimitivePaint& paint) {
    const std::array points{a, b};
    drawPolyline(points, false, paint);
}

void PrimitiveCanvas2D::drawPoint(glm::vec2 point, const PrimitivePaint& paint) {
    PrimitivePaint pointPaint = paint;
    pointPaint.mode           = PaintMode::Fill;
    if (paint.stroke.cap == LineCap::Square)
        drawRect(point - glm::vec2(paint.stroke.width * 0.5f), point + glm::vec2(paint.stroke.width * 0.5f),
                 pointPaint);
    else
        drawCircle(point, paint.stroke.width * 0.5f, pointPaint, 16);
}

void PrimitiveCanvas2D::drawPolyline(std::span<const glm::vec2> points, bool closed, const PrimitivePaint& paint) {
    auto result = tryDrawPolyline(points, closed, paint);
    std::move(result).expect("drawPolyline compatibility API exceeded its command budget");
}

eve::Result<PrimitiveRecordStatus> PrimitiveCanvas2D::tryDrawPolyline(std::span<const glm::vec2> points, bool closed,
                                                                      const PrimitivePaint& paint) {
    validatePoints(points, closed);
    paint.validate();
    if (commands_.size() + triangles_.size() >= hardCommandLimit_) {
        ++statistics_.droppedCommands;
        return primitiveBudgetFailure(hardCommandLimit_, commands_.size() + triangles_.size());
    }
    commands_.push_back({std::vector<glm::vec2>(points.begin(), points.end()), cumulativeLengths(points, closed),
                         transform_, paint, closed});
    ++statistics_.commandCount;
    statistics_.segmentCount += points.size() - 1 + (closed ? 1u : 0u);
    return eve::Result<PrimitiveRecordStatus>::success(PrimitiveRecordStatus::Recorded);
}

void PrimitiveCanvas2D::drawPath(const Path2D& path, const PrimitivePaint& paint, float tolerance) {
    const auto contours = path.flatten(tolerance);
    if (paint.mode != PaintMode::Stroke) {
        struct FillBoundary {
            std::vector<glm::vec2>              points;
            bool                                outer = false;
            std::vector<std::vector<glm::vec2>> holes;
        };
        std::vector<FillBoundary> boundaries;
        for (std::size_t contourIndex = 0; contourIndex < contours.size(); ++contourIndex) {
            const auto& contour = contours[contourIndex];
            if (!contour.closed || contour.points.size() < 3) continue;
            int windingBefore   = 0;
            int containingCount = 0;
            for (std::size_t other = 0; other < contours.size(); ++other) {
                if (other == contourIndex || !contours[other].closed || contours[other].points.size() < 3) continue;
                if (pointInPolygon(contour.points.front(), contours[other].points)) {
                    ++containingCount;
                    windingBefore += signedArea(contours[other].points) >= 0.f ? 1 : -1;
                }
            }
            const int  ownWinding = signedArea(contour.points) >= 0.f ? 1 : -1;
            const bool beforeFilled =
                path.fillRule() == PathFillRule::EvenOdd ? containingCount % 2 != 0 : windingBefore != 0;
            const bool afterFilled =
                path.fillRule() == PathFillRule::EvenOdd ? !beforeFilled : windingBefore + ownWinding != 0;
            if (beforeFilled == afterFilled) continue;
            boundaries.push_back({contour.points, !beforeFilled && afterFilled, {}});
        }
        for (FillBoundary& boundary : boundaries) {
            if (boundary.outer) continue;
            FillBoundary* owner     = nullptr;
            float         ownerArea = std::numeric_limits<float>::max();
            for (FillBoundary& candidate : boundaries) {
                const float area = std::fabs(signedArea(candidate.points));
                if (candidate.outer && area < ownerArea && pointInPolygon(boundary.points.front(), candidate.points)) {
                    owner     = &candidate;
                    ownerArea = area;
                }
            }
            EV_PARAM_CHECK(owner != nullptr, "path fill hole requires a containing outer contour");
            owner->holes.push_back(boundary.points);
        }
        for (FillBoundary& boundary : boundaries) {
            if (!boundary.outer) continue;
            std::vector<glm::vec2> polygon = boundary.points;
            for (const auto& hole : boundary.holes) polygon = bridgeHole(std::move(polygon), hole);
            for (const auto triangle : triangulateSimplePolygon(polygon)) {
                drawTriangle(polygon[triangle[0]], polygon[triangle[1]], polygon[triangle[2]], paint);
            }
        }
        for (const FillBoundary& boundary : boundaries) drawCoverageFringe(boundary.points, paint);
    }
    for (const auto& contour : contours) {
        if (contour.points.size() >= 2 && paint.mode != PaintMode::Fill)
            drawPolyline(contour.points, contour.closed, paint);
    }
}

void PrimitiveCanvas2D::drawTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const PrimitivePaint& paint) {
    const std::array points{a, b, c};
    validatePoints(std::span<const glm::vec2>(points), true);
    paint.validate();
    EV_PARAM_CHECK(commands_.size() + triangles_.size() < hardCommandLimit_,
                   "primitive 2D command hard limit exceeded");
    triangles_.push_back({points, transform_, paint});
    ++statistics_.commandCount;
}

void PrimitiveCanvas2D::drawColoredTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, Color colorA, Color colorB,
                                            Color colorC, const PrimitivePaint& paint) {
    const std::array points{a, b, c};
    validatePoints(std::span<const glm::vec2>(points), true);
    paint.validate();
    EV_PARAM_CHECK(commands_.size() + triangles_.size() < hardCommandLimit_,
                   "primitive 2D command hard limit exceeded");
    triangles_.push_back({points, transform_, paint, std::array<Color, 3>{colorA, colorB, colorC}});
    ++statistics_.commandCount;
}

void PrimitiveCanvas2D::drawCoverageFringe(std::span<const glm::vec2> outline, const PrimitivePaint& paint) {
    if (!paint.antialias || outline.size() < 3) return;
    std::vector<glm::vec2> points;
    points.reserve(outline.size());
    for (const glm::vec2 point : outline) points.push_back(transformPoint2D(transform_, point));
    const float            orientation = signedArea(points) >= 0.f ? 1.f : -1.f;
    std::vector<glm::vec2> outer(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        const glm::vec2 previous          = points[(i + points.size() - 1u) % points.size()];
        const glm::vec2 current           = points[i];
        const glm::vec2 next              = points[(i + 1u) % points.size()];
        const glm::vec2 previousDirection = glm::normalize(current - previous);
        const glm::vec2 nextDirection     = glm::normalize(next - current);
        const glm::vec2 previousOutward   = glm::vec2(previousDirection.y, -previousDirection.x) * orientation;
        const glm::vec2 nextOutward       = glm::vec2(nextDirection.y, -nextDirection.x) * orientation;
        glm::vec2       bisector          = previousOutward + nextOutward;
        if (glm::length(bisector) <= 1e-6f) bisector = nextOutward;
        bisector                = glm::normalize(bisector);
        const float denominator = std::max(0.25f, glm::dot(bisector, nextOutward));
        outer[i]                = current + bisector * std::min(2.f, 0.5f / denominator);
    }
    Color transparent             = paint.color;
    transparent.a                 = 0.f;
    const std::size_t required    = points.size() * 2u;
    const bool        hasCapacity = commands_.size() + triangles_.size() + required <= hardCommandLimit_;
    if (!hasCapacity) statistics_.droppedCommands += required;
    EV_PARAM_CHECK(hasCapacity, "primitive 2D coverage fringe exceeds command hard limit");
    for (std::size_t i = 0; i < points.size(); ++i) {
        const std::size_t next = (i + 1u) % points.size();
        triangles_.push_back({{points[i], points[next], outer[i]},
                              glm::mat3(1.f),
                              paint,
                              std::array<Color, 3>{paint.color, paint.color, transparent}});
        triangles_.push_back({{points[next], outer[next], outer[i]},
                              glm::mat3(1.f),
                              paint,
                              std::array<Color, 3>{paint.color, transparent, transparent}});
        statistics_.commandCount += 2u;
    }
}

void PrimitiveCanvas2D::drawRect(glm::vec2 minimum, glm::vec2 maximum, const PrimitivePaint& paint) {
    const bool ordered = minimum.x <= maximum.x && minimum.y <= maximum.y;
    EV_PARAM_CHECK(ordered, "primitive rectangle minimum must not exceed maximum");
    const std::array outline{minimum, glm::vec2(maximum.x, minimum.y), maximum, glm::vec2(minimum.x, maximum.y)};
    if (paint.mode != PaintMode::Stroke) {
        drawTriangle(outline[0], outline[1], outline[2], paint);
        drawTriangle(outline[0], outline[2], outline[3], paint);
        drawCoverageFringe(outline, paint);
    }
    if (paint.mode != PaintMode::Fill) drawPolyline(outline, true, paint);
}

void PrimitiveCanvas2D::drawRoundedRect(glm::vec2 minimum, glm::vec2 maximum, glm::vec2 radii,
                                        const PrimitivePaint& paint, std::uint32_t cornerSegments) {
    const glm::vec2 size  = maximum - minimum;
    const bool      valid = minimum.x <= maximum.x && minimum.y <= maximum.y && std::isfinite(radii.x) &&
                       std::isfinite(radii.y) && radii.x >= 0.f && radii.y >= 0.f;
    EV_PARAM_CHECK(valid, "rounded rectangle bounds and radii must be valid");
    validateSegments(std::max(3u, cornerSegments));
    radii = glm::min(radii, size * 0.5f);
    std::vector<glm::vec2> outline;
    outline.reserve((cornerSegments + 1u) * 4u);
    const std::array centers{glm::vec2(maximum.x - radii.x, minimum.y + radii.y), maximum - radii,
                             glm::vec2(minimum.x + radii.x, maximum.y - radii.y), minimum + radii};
    for (std::uint32_t corner = 0; corner < 4; ++corner) {
        const float start = -glm::half_pi<float>() + glm::half_pi<float>() * corner;
        for (std::uint32_t i = 0; i <= cornerSegments; ++i) {
            const float angle =
                start + glm::half_pi<float>() * static_cast<float>(i) / static_cast<float>(cornerSegments);
            outline.push_back(centers[corner] + glm::vec2(std::cos(angle) * radii.x, std::sin(angle) * radii.y));
        }
    }
    const glm::vec2 center = (minimum + maximum) * 0.5f;
    if (paint.mode != PaintMode::Stroke)
        for (std::size_t i = 0; i < outline.size(); ++i)
            drawTriangle(center, outline[i], outline[(i + 1u) % outline.size()], paint);
    if (paint.mode != PaintMode::Stroke) drawCoverageFringe(outline, paint);
    if (paint.mode != PaintMode::Fill) drawPolyline(outline, true, paint);
}

void PrimitiveCanvas2D::drawCircle(glm::vec2 center, float radius, const PrimitivePaint& paint,
                                   std::uint32_t segments) {
    drawEllipse(center, glm::vec2(radius), paint, segments);
}

void PrimitiveCanvas2D::drawCircle(glm::vec2 center, float radius, const PrimitivePaint& paint,
                                   const RadialTessellation& tessellation) {
    validateRadius(radius);
    const glm::vec2 transformedCenter = transformPoint2D(transform_, center);
    const float     projectedRadius =
        std::max(glm::length(transformPoint2D(transform_, center + glm::vec2(radius, 0.f)) - transformedCenter),
                 glm::length(transformPoint2D(transform_, center + glm::vec2(0.f, radius)) - transformedCenter));
    drawCircle(center, radius, paint, resolveRadialSegments(tessellation, projectedRadius));
}

void PrimitiveCanvas2D::drawEllipse(glm::vec2 center, glm::vec2 radii, const PrimitivePaint& paint,
                                    std::uint32_t segments) {
    const bool validRadii = std::isfinite(radii.x) && std::isfinite(radii.y) && radii.x >= 0.f && radii.y >= 0.f;
    EV_PARAM_CHECK(validRadii, "ellipse radii must be finite and non-negative");
    validateSegments(segments);
    const CachedUnitCircle circle = unitCircle(segments);
    if (circle.hit) ++statistics_.cacheHits;
    std::vector<glm::vec2> outline;
    outline.reserve(segments);
    for (const glm::vec2 unit : *circle.points) outline.push_back(center + unit * radii);
    if (paint.mode != PaintMode::Stroke) {
        for (std::uint32_t i = 0; i < segments; ++i) {
            drawTriangle(center, outline[i], outline[(i + 1u) % segments], paint);
        }
        drawCoverageFringe(outline, paint);
    }
    if (paint.mode != PaintMode::Fill) drawPolyline(outline, true, paint);
}

void PrimitiveCanvas2D::drawArc(glm::vec2 center, glm::vec2 radii, float startRadians, float sweepRadians,
                                const PrimitivePaint& paint, std::uint32_t segments) {
    const bool validArc = std::isfinite(startRadians) && std::isfinite(sweepRadians) && std::isfinite(radii.x) &&
                          std::isfinite(radii.y) && radii.x >= 0.f && radii.y >= 0.f;
    EV_PARAM_CHECK(validArc, "arc values must be finite and radii non-negative");
    validateSegments(segments);
    std::vector<glm::vec2> arc;
    arc.reserve(segments + 1u);
    for (std::uint32_t i = 0; i <= segments; ++i) {
        const float t     = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = startRadians + sweepRadians * t;
        arc.push_back(center + glm::vec2(std::cos(angle) * radii.x, std::sin(angle) * radii.y));
    }
    if (paint.mode != PaintMode::Stroke) {
        for (std::uint32_t i = 0; i < segments; ++i) {
            drawTriangle(center, arc[i], arc[i + 1], paint);
        }
        std::vector<glm::vec2> sector;
        sector.reserve(arc.size() + 1u);
        sector.push_back(center);
        sector.insert(sector.end(), arc.begin(), arc.end());
        drawCoverageFringe(sector, paint);
    }
    if (paint.mode != PaintMode::Fill) drawPolyline(arc, false, paint);
}

void PrimitiveCanvas2D::reset() {
    commands_.clear();
    triangles_.clear();
    stack_.clear();
    transform_  = glm::mat3(1.f);
    statistics_ = {};
}

PrimitiveSceneCanvas3D::PrimitiveSceneCanvas3D(SceneDrawContext context, std::size_t hardCommandLimit)
    : context_(std::move(context)), hardCommandLimit_(hardCommandLimit) {
    context_.validate();
    EV_PARAM_CHECK(hardCommandLimit_ > 0, "primitive command limit must be positive");
}

void PrimitiveSceneCanvas3D::save() { stack_.push_back(transform_); }

void PrimitiveSceneCanvas3D::restore() {
    EV_PARAM_CHECK(!stack_.empty(), "primitive scene canvas restore requires a matching save");
    transform_ = stack_.back();
    stack_.pop_back();
}

void PrimitiveSceneCanvas3D::concat(const glm::mat4& transform) { transform_ *= transform; }

void PrimitiveSceneCanvas3D::drawLine(glm::vec3 a, glm::vec3 b, const ScenePrimitivePaint& paint) {
    const std::array points{a, b};
    drawPolyline(points, false, paint);
}

void PrimitiveSceneCanvas3D::drawPoint(glm::vec3 point, const ScenePrimitivePaint& paint) {
    const glm::vec3 cameraRight{context_.view[0][0], context_.view[1][0], context_.view[2][0]};
    const float     worldRadius =
        paint.stroke.widthSpace == WidthSpace::WorldUnits
                ? paint.stroke.width * 0.5f
                : context_.nearPlane * paint.stroke.width / static_cast<float>(context_.viewportSize.y);
    drawLine(point - cameraRight * worldRadius, point + cameraRight * worldRadius, paint);
}

void PrimitiveSceneCanvas3D::drawPolyline(std::span<const glm::vec3> points, bool closed,
                                          const ScenePrimitivePaint& paint) {
    auto result = tryDrawPolyline(points, closed, paint);
    std::move(result).expect("drawPolyline compatibility API exceeded its command budget");
}

eve::Result<PrimitiveRecordStatus> PrimitiveSceneCanvas3D::tryDrawPolyline(std::span<const glm::vec3> points,
                                                                           bool                       closed,
                                                                           const ScenePrimitivePaint& paint) {
    validatePoints(points, closed);
    paint.validate();
    if (commands_.size() + triangles_.size() >= hardCommandLimit_) {
        ++statistics_.droppedCommands;
        return primitiveBudgetFailure(hardCommandLimit_, commands_.size() + triangles_.size());
    }
    commands_.push_back({std::vector<glm::vec3>(points.begin(), points.end()), cumulativeLengths(points, closed),
                         transform_, paint, closed});
    ++statistics_.commandCount;
    statistics_.segmentCount += points.size() - 1 + (closed ? 1u : 0u);
    return eve::Result<PrimitiveRecordStatus>::success(PrimitiveRecordStatus::Recorded);
}

void PrimitiveSceneCanvas3D::drawRay(glm::vec3 origin, glm::vec3 direction, float length,
                                     const ScenePrimitivePaint& paint) {
    const float directionLength = glm::length(direction);
    const bool  validDirection  = std::isfinite(directionLength) && directionLength > 1e-6f;
    EV_PARAM_CHECK(validDirection, "primitive ray direction must be finite and non-zero");
    const bool validLength = std::isfinite(length) && length >= 0.f;
    EV_PARAM_CHECK(validLength, "primitive ray length must be finite and non-negative");
    drawLine(origin, origin + direction / directionLength * length, paint);
}

void PrimitiveSceneCanvas3D::drawTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, const ScenePrimitivePaint& paint) {
    const std::array points{a, b, c};
    validatePoints(std::span<const glm::vec3>(points), true);
    paint.validate();
    if (paint.mode != PaintMode::Stroke) recordTriangle(a, b, c, paint);
    if (paint.mode != PaintMode::Fill) drawPolyline(points, true, paint);
}

void PrimitiveSceneCanvas3D::recordTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, const ScenePrimitivePaint& paint) {
    EV_PARAM_CHECK(commands_.size() + triangles_.size() < hardCommandLimit_,
                   "primitive 3D command hard limit exceeded");
    triangles_.push_back({{a, b, c}, transform_, paint});
    ++statistics_.commandCount;
}

void PrimitiveSceneCanvas3D::drawQuad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                                      const ScenePrimitivePaint& paint) {
    if (paint.mode != PaintMode::Stroke) {
        recordTriangle(a, b, c, paint);
        recordTriangle(a, c, d, paint);
    }
    if (paint.mode != PaintMode::Fill) {
        const std::array outline{a, b, c, d};
        drawPolyline(outline, true, paint);
    }
}

void PrimitiveSceneCanvas3D::drawDisk(glm::vec3 center, glm::vec3 normal, float radius,
                                      const ScenePrimitivePaint& paint, std::uint32_t segments) {
    validateRadius(radius);
    validateSegments(segments);
    const auto [u, v]  = perpendicularBasis(normal);
    const auto outline = circlePoints(center, u, v, radius, segments, statistics_);
    if (paint.mode != PaintMode::Stroke)
        for (std::uint32_t i = 0; i < segments; ++i)
            recordTriangle(center, outline[i], outline[(i + 1u) % segments], paint);
    if (paint.mode != PaintMode::Fill) drawPolyline(outline, true, paint);
}

void PrimitiveSceneCanvas3D::drawArc(glm::vec3 center, glm::vec3 normal, glm::vec3 zeroDirection, float radius,
                                     float startRadians, float sweepRadians, const ScenePrimitivePaint& paint,
                                     std::uint32_t segments) {
    validateRadius(radius);
    validateSegments(segments);
    const bool validAngles = std::isfinite(startRadians) && std::isfinite(sweepRadians);
    EV_PARAM_CHECK(validAngles, "primitive arc angles must be finite");
    const glm::vec3 n = glm::normalize(normal);
    glm::vec3       u = zeroDirection - n * glm::dot(zeroDirection, n);
    EV_PARAM_CHECK(glm::length(u) > 1e-6f, "primitive arc zero direction must not be parallel to normal");
    u                        = glm::normalize(u);
    const glm::vec3        v = glm::normalize(glm::cross(n, u));
    std::vector<glm::vec3> points;
    points.reserve(segments + 1u);
    for (std::uint32_t i = 0; i <= segments; ++i) {
        const float angle = startRadians + sweepRadians * static_cast<float>(i) / static_cast<float>(segments);
        points.push_back(center + radius * (std::cos(angle) * u + std::sin(angle) * v));
    }
    if (paint.mode != PaintMode::Stroke)
        for (std::uint32_t i = 0; i < segments; ++i) recordTriangle(center, points[i], points[i + 1u], paint);
    if (paint.mode != PaintMode::Fill) drawPolyline(points, false, paint);
}

void PrimitiveSceneCanvas3D::drawAabb(glm::vec3 minimum, glm::vec3 maximum, const ScenePrimitivePaint& paint) {
    const bool ordered = minimum.x <= maximum.x && minimum.y <= maximum.y && minimum.z <= maximum.z;
    EV_PARAM_CHECK(ordered, "primitive AABB minimum must not exceed maximum");
    const std::array corners{glm::vec3{minimum.x, minimum.y, minimum.z}, glm::vec3{maximum.x, minimum.y, minimum.z},
                             glm::vec3{maximum.x, maximum.y, minimum.z}, glm::vec3{minimum.x, maximum.y, minimum.z},
                             glm::vec3{minimum.x, minimum.y, maximum.z}, glm::vec3{maximum.x, minimum.y, maximum.z},
                             glm::vec3{maximum.x, maximum.y, maximum.z}, glm::vec3{minimum.x, maximum.y, maximum.z}};
    if (paint.mode != PaintMode::Stroke) {
        recordTriangle(corners[0], corners[3], corners[2], paint);
        recordTriangle(corners[0], corners[2], corners[1], paint);
        recordTriangle(corners[4], corners[5], corners[6], paint);
        recordTriangle(corners[4], corners[6], corners[7], paint);
        recordTriangle(corners[0], corners[1], corners[5], paint);
        recordTriangle(corners[0], corners[5], corners[4], paint);
        recordTriangle(corners[1], corners[2], corners[6], paint);
        recordTriangle(corners[1], corners[6], corners[5], paint);
        recordTriangle(corners[2], corners[3], corners[7], paint);
        recordTriangle(corners[2], corners[7], corners[6], paint);
        recordTriangle(corners[3], corners[0], corners[4], paint);
        recordTriangle(corners[3], corners[4], corners[7], paint);
    }
    if (paint.mode != PaintMode::Fill) {
        const std::array<std::uint8_t, 24> edges{0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6,
                                                 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7};
        for (std::size_t i = 0; i < edges.size(); i += 2) drawLine(corners[edges[i]], corners[edges[i + 1]], paint);
    }
}

void PrimitiveSceneCanvas3D::drawObb(glm::vec3 center, const std::array<glm::vec3, 3>& halfAxes,
                                     const ScenePrimitivePaint& paint) {
    for (const glm::vec3 axis : halfAxes) EV_PARAM_CHECK(glm::length(axis) > 1e-6f, "OBB half axes must be non-zero");
    std::array<glm::vec3, 8> corners{};
    for (std::uint32_t index = 0; index < 8; ++index)
        corners[index] = center + (index & 1u ? halfAxes[0] : -halfAxes[0]) +
                         (index & 2u ? halfAxes[1] : -halfAxes[1]) + (index & 4u ? halfAxes[2] : -halfAxes[2]);
    drawFrustum(corners, paint);
}

void PrimitiveSceneCanvas3D::drawGrid(glm::vec3 origin, glm::vec3 axisU, glm::vec3 axisV, std::uint32_t cellsU,
                                      std::uint32_t cellsV, const ScenePrimitivePaint& paint) {
    const bool validCellsU = cellsU > 0 && cellsU <= 4096;
    const bool validCellsV = cellsV > 0 && cellsV <= 4096;
    const bool validAxes   = glm::length(axisU) > 1e-6f && glm::length(axisV) > 1e-6f;
    EV_PARAM_CHECK(validCellsU, "grid cellsU must be in [1, 4096]");
    EV_PARAM_CHECK(validCellsV, "grid cellsV must be in [1, 4096]");
    EV_PARAM_CHECK(validAxes, "grid axes must have non-zero length");
    for (std::uint32_t i = 0; i <= cellsU; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(cellsU);
        drawLine(origin + axisU * t, origin + axisU * t + axisV, paint);
    }
    for (std::uint32_t i = 0; i <= cellsV; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(cellsV);
        drawLine(origin + axisV * t, origin + axisV * t + axisU, paint);
    }
}

void PrimitiveSceneCanvas3D::drawSphere(glm::vec3 center, float radius, const ScenePrimitivePaint& paint,
                                        std::uint32_t segments) {
    validateRadius(radius);
    validateSegments(segments);
    if (paint.mode != PaintMode::Stroke) {
        const std::uint32_t rings = std::max(3u, segments / 2u);
        for (std::uint32_t ring = 0; ring < rings; ++ring) {
            const float latitudeA = -glm::half_pi<float>() + glm::pi<float>() * ring / rings;
            const float latitudeB = -glm::half_pi<float>() + glm::pi<float>() * (ring + 1u) / rings;
            for (std::uint32_t slice = 0; slice < segments; ++slice) {
                const float longitudeA = glm::two_pi<float>() * slice / segments;
                const float longitudeB = glm::two_pi<float>() * (slice + 1u) / segments;
                const auto  point      = [&](float latitude, float longitude) {
                    return center + radius * glm::vec3(std::cos(latitude) * std::cos(longitude), std::sin(latitude),
                                                             std::cos(latitude) * std::sin(longitude));
                };
                const glm::vec3 a = point(latitudeA, longitudeA);
                const glm::vec3 b = point(latitudeA, longitudeB);
                const glm::vec3 c = point(latitudeB, longitudeB);
                const glm::vec3 d = point(latitudeB, longitudeA);
                recordTriangle(a, b, c, paint);
                recordTriangle(a, c, d, paint);
            }
        }
    }
    if (paint.mode != PaintMode::Fill) {
        drawPolyline(circlePoints(center, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, radius, segments, statistics_), true,
                     paint);
        drawPolyline(circlePoints(center, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, radius, segments, statistics_), true,
                     paint);
        drawPolyline(circlePoints(center, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, radius, segments, statistics_), true,
                     paint);
    }
}

void PrimitiveSceneCanvas3D::drawSphere(glm::vec3 center, float radius, const ScenePrimitivePaint& paint,
                                        const RadialTessellation& tessellation) {
    validateRadius(radius);
    const glm::mat4 modelView       = context_.view * transform_;
    const glm::vec3 viewCenter      = glm::vec3(modelView * glm::vec4(center, 1.f));
    const glm::vec3 viewEdge        = glm::vec3(modelView * glm::vec4(center + glm::vec3(radius, 0.f, 0.f), 1.f));
    const glm::vec4 centerClip      = context_.projection * glm::vec4(viewCenter, 1.f);
    const glm::vec4 edgeClip        = context_.projection * glm::vec4(viewEdge, 1.f);
    float           projectedRadius = 0.f;
    if (centerClip.w > 1e-8f && edgeClip.w > 1e-8f) {
        const glm::vec2 centerNdc = glm::vec2(centerClip) / centerClip.w;
        const glm::vec2 edgeNdc   = glm::vec2(edgeClip) / edgeClip.w;
        projectedRadius           = glm::length((edgeNdc - centerNdc) * glm::vec2(context_.viewportSize) * 0.5f);
    }
    drawSphere(center, radius, paint, resolveRadialSegments(tessellation, projectedRadius));
}

void PrimitiveSceneCanvas3D::drawCapsule(glm::vec3 a, glm::vec3 b, float radius, const ScenePrimitivePaint& paint,
                                         std::uint32_t segments) {
    validateRadius(radius);
    validateSegments(segments);
    drawCylinder(a, b, radius, paint, segments);
    drawSphere(a, radius, paint, segments);
    drawSphere(b, radius, paint, segments);
}

void PrimitiveSceneCanvas3D::drawCylinder(glm::vec3 a, glm::vec3 b, float radius, const ScenePrimitivePaint& paint,
                                          std::uint32_t segments) {
    validateRadius(radius);
    validateSegments(segments);
    const auto [u, v]  = perpendicularBasis(b - a);
    const auto circleA = circlePoints(a, u, v, radius, segments, statistics_);
    const auto circleB = circlePoints(b, u, v, radius, segments, statistics_);
    if (paint.mode != PaintMode::Stroke) {
        for (std::uint32_t i = 0; i < segments; ++i) {
            const std::uint32_t next = (i + 1u) % segments;
            recordTriangle(a, circleA[next], circleA[i], paint);
            recordTriangle(b, circleB[i], circleB[next], paint);
            recordTriangle(circleA[i], circleA[next], circleB[next], paint);
            recordTriangle(circleA[i], circleB[next], circleB[i], paint);
        }
    }
    if (paint.mode != PaintMode::Fill) {
        drawPolyline(circleA, true, paint);
        drawPolyline(circleB, true, paint);
        for (std::uint32_t i : {0u, segments / 4u, segments / 2u, (segments * 3u) / 4u})
            drawLine(circleA[i % segments], circleB[i % segments], paint);
    }
}

void PrimitiveSceneCanvas3D::drawCone(glm::vec3 apex, glm::vec3 axis, float height, float radius,
                                      const ScenePrimitivePaint& paint, std::uint32_t segments) {
    validateRadius(radius);
    validateSegments(segments);
    const bool validHeight = std::isfinite(height) && height >= 0.f;
    EV_PARAM_CHECK(validHeight, "cone height must be finite and non-negative");
    const auto [u, v]         = perpendicularBasis(axis);
    const glm::vec3 direction = glm::normalize(axis);
    const auto      circle    = circlePoints(apex + direction * height, u, v, radius, segments, statistics_);
    if (paint.mode != PaintMode::Stroke) {
        const glm::vec3 base = apex + direction * height;
        for (std::uint32_t i = 0; i < segments; ++i) {
            const std::uint32_t next = (i + 1u) % segments;
            recordTriangle(base, circle[next], circle[i], paint);
            recordTriangle(apex, circle[i], circle[next], paint);
        }
    }
    if (paint.mode != PaintMode::Fill) {
        drawPolyline(circle, true, paint);
        for (std::uint32_t i : {0u, segments / 4u, segments / 2u, (segments * 3u) / 4u})
            drawLine(apex, circle[i % segments], paint);
    }
}

void PrimitiveSceneCanvas3D::drawArrow(glm::vec3 from, glm::vec3 to, float headLength, float headRadius,
                                       const ScenePrimitivePaint& paint) {
    validateRadius(headRadius);
    const glm::vec3 axis      = to - from;
    const float     length    = glm::length(axis);
    const bool      validAxis = std::isfinite(length) && length > 1e-6f;
    EV_PARAM_CHECK(validAxis, "arrow endpoints must define a non-zero finite axis");
    const bool validHead = std::isfinite(headLength) && headLength >= 0.f && headLength <= length;
    EV_PARAM_CHECK(validHead, "arrow head length must be finite and within arrow length");
    const glm::vec3 direction = axis / length;
    const glm::vec3 base      = to - direction * headLength;
    drawLine(from, base, paint);
    drawCone(to, -direction, headLength, headRadius, paint, 16);
}

void PrimitiveSceneCanvas3D::drawFrustum(const std::array<glm::vec3, 8>& corners, const ScenePrimitivePaint& paint) {
    static constexpr std::array<std::array<std::uint8_t, 4>, 6> faces{
        {{{0, 1, 3, 2}}, {{4, 6, 7, 5}}, {{0, 4, 5, 1}}, {{2, 3, 7, 6}}, {{0, 2, 6, 4}}, {{1, 5, 7, 3}}}};
    if (paint.mode != PaintMode::Stroke)
        for (const auto& face : faces) {
            recordTriangle(corners[face[0]], corners[face[1]], corners[face[2]], paint);
            recordTriangle(corners[face[0]], corners[face[2]], corners[face[3]], paint);
        }
    if (paint.mode != PaintMode::Fill) {
        static constexpr std::array<std::uint8_t, 24> edges{0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7,
                                                            7, 6, 6, 4, 0, 4, 1, 5, 2, 6, 3, 7};
        for (std::size_t i = 0; i < edges.size(); i += 2) drawLine(corners[edges[i]], corners[edges[i + 1]], paint);
    }
}

void PrimitiveSceneCanvas3D::reset() {
    commands_.clear();
    triangles_.clear();
    stack_.clear();
    transform_  = glm::mat4(1.f);
    statistics_ = {};
}

}  // namespace eve::graphics
