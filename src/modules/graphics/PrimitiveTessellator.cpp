#include "graphics/PrimitiveTessellator.h"

#include "common/Assert.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace eve::graphics {
namespace {

void appendQuad(ResolvedPrimitiveTriangles& output, const std::array<glm::vec4, 4>& positions, Color color,
                float distanceA, float distanceB, std::uint32_t commandIndex) {
    const std::array<std::uint8_t, 6> order{0, 1, 2, 1, 3, 2};
    for (std::uint8_t index : order) {
        const bool end          = index == 1 || index == 3;
        const bool positiveEdge = index == 2 || index == 3;
        output.vertices.push_back(
            {positions[index], color, end ? distanceB : distanceA, positiveEdge ? 1.f : -1.f, commandIndex});
    }
    ++output.statistics.triangleCount;
    ++output.statistics.triangleCount;
}

void appendTriangle(ResolvedPrimitiveTriangles& output, glm::vec4 a, glm::vec4 b, glm::vec4 c, Color color,
                    float distance, std::uint32_t commandIndex) {
    output.vertices.push_back({a, color, distance, 0.f, commandIndex});
    output.vertices.push_back({b, color, distance, 0.f, commandIndex});
    output.vertices.push_back({c, color, distance, 0.f, commandIndex});
    ++output.statistics.triangleCount;
}

void appendColoredTriangle(ResolvedPrimitiveTriangles& output, glm::vec4 a, glm::vec4 b, glm::vec4 c, Color colorA,
                           Color colorB, Color colorC, float distanceA, float distanceB, std::uint32_t commandIndex) {
    output.vertices.push_back({a, colorA, distanceA, 0.f, commandIndex});
    output.vertices.push_back({b, colorB, distanceB, 0.f, commandIndex});
    output.vertices.push_back({c, colorC, distanceA, 0.f, commandIndex});
    ++output.statistics.triangleCount;
}

void appendColoredQuad(ResolvedPrimitiveTriangles& output, const std::array<glm::vec4, 4>& positions,
                       const std::array<Color, 4>& colors, float distanceA, float distanceB,
                       std::uint32_t commandIndex) {
    appendColoredTriangle(output, positions[0], positions[1], positions[2], colors[0], colors[1], colors[2], distanceA,
                          distanceB, commandIndex);
    appendColoredTriangle(output, positions[1], positions[3], positions[2], colors[1], colors[3], colors[2], distanceB,
                          distanceB, commandIndex);
}

glm::vec4 pixelToClip(glm::vec2 point, glm::ivec2 viewport) {
    return {point.x * 2.f / static_cast<float>(viewport.x) - 1.f, point.y * 2.f / static_cast<float>(viewport.y) - 1.f,
            0.f, 1.f};
}

void appendCap2D(ResolvedPrimitiveTriangles& output, glm::vec2 point, glm::vec2 outward, float halfWidth, LineCap cap,
                 Color color, float distance, std::uint32_t commandIndex, glm::ivec2 viewport) {
    if (cap == LineCap::Butt) return;
    const glm::vec2 normal{-outward.y, outward.x};
    if (cap == LineCap::Square) {
        const std::array positions{pixelToClip(point - normal * halfWidth, viewport),
                                   pixelToClip(point + outward * halfWidth - normal * halfWidth, viewport),
                                   pixelToClip(point + normal * halfWidth, viewport),
                                   pixelToClip(point + outward * halfWidth + normal * halfWidth, viewport)};
        appendQuad(output, positions, color, distance, distance, commandIndex);
        return;
    }
    constexpr std::uint32_t capSegments = 8;
    const float             base        = std::atan2(outward.y, outward.x) - glm::half_pi<float>();
    for (std::uint32_t segment = 0; segment < capSegments; ++segment) {
        const float a = base + glm::pi<float>() * static_cast<float>(segment) / capSegments;
        const float b = base + glm::pi<float>() * static_cast<float>(segment + 1) / capSegments;
        appendTriangle(output, pixelToClip(point, viewport),
                       pixelToClip(point + glm::vec2(std::cos(a), std::sin(a)) * halfWidth, viewport),
                       pixelToClip(point + glm::vec2(std::cos(b), std::sin(b)) * halfWidth, viewport), color, distance,
                       commandIndex);
    }
}

void appendJoin2D(ResolvedPrimitiveTriangles& output, glm::vec2 point, glm::vec2 previousDirection,
                  glm::vec2 nextDirection, float halfWidth, const StrokeStyle& stroke, Color color, float distance,
                  std::uint32_t commandIndex, glm::ivec2 viewport) {
    const float turn = previousDirection.x * nextDirection.y - previousDirection.y * nextDirection.x;
    if (std::fabs(turn) <= 1e-5f) return;
    const float     outerSign = turn > 0.f ? -1.f : 1.f;
    const glm::vec2 previousNormal{-previousDirection.y, previousDirection.x};
    const glm::vec2 nextNormal{-nextDirection.y, nextDirection.x};
    const glm::vec2 previousOuter = previousNormal * outerSign;
    const glm::vec2 nextOuter     = nextNormal * outerSign;
    const glm::vec2 previousPoint = point + previousOuter * halfWidth;
    const glm::vec2 nextPoint     = point + nextOuter * halfWidth;
    if (stroke.join == LineJoin::Round) {
        float start = std::atan2(previousOuter.y, previousOuter.x);
        float sweep = std::atan2(nextOuter.y, nextOuter.x) - start;
        if (turn > 0.f && sweep < 0.f) sweep += glm::two_pi<float>();
        if (turn < 0.f && sweep > 0.f) sweep -= glm::two_pi<float>();
        const std::uint32_t segments = std::max(1u, static_cast<std::uint32_t>(std::ceil(std::fabs(sweep) * 4.f)));
        for (std::uint32_t segment = 0; segment < segments; ++segment) {
            const float a = start + sweep * static_cast<float>(segment) / segments;
            const float b = start + sweep * static_cast<float>(segment + 1) / segments;
            appendTriangle(output, pixelToClip(point, viewport),
                           pixelToClip(point + glm::vec2(std::cos(a), std::sin(a)) * halfWidth, viewport),
                           pixelToClip(point + glm::vec2(std::cos(b), std::sin(b)) * halfWidth, viewport), color,
                           distance, commandIndex);
        }
        return;
    }
    if (stroke.join == LineJoin::Miter) {
        const glm::vec2 bisector    = glm::normalize(previousOuter + nextOuter);
        const float     denominator = glm::dot(bisector, previousOuter);
        if (std::fabs(denominator) > 1e-5f) {
            const float miterLength = halfWidth / denominator;
            if (std::fabs(miterLength) <= stroke.miterLimit * halfWidth) {
                appendTriangle(output, pixelToClip(previousPoint, viewport),
                               pixelToClip(point + bisector * miterLength, viewport), pixelToClip(nextPoint, viewport),
                               color, distance, commandIndex);
                return;
            }
        }
    }
    appendTriangle(output, pixelToClip(point, viewport), pixelToClip(previousPoint, viewport),
                   pixelToClip(nextPoint, viewport), color, distance, commandIndex);
}

glm::vec2 clipToPixel(glm::vec4 clip, glm::ivec2 viewport) {
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    return (ndc + 1.f) * glm::vec2(viewport) * 0.5f;
}

glm::vec4 pixelToClipAt(glm::vec2 pixel, glm::vec4 reference, glm::ivec2 viewport) {
    const glm::vec2 ndc = pixel * 2.f / glm::vec2(viewport) - 1.f;
    return {ndc.x * reference.w, ndc.y * reference.w, reference.z, reference.w};
}

void appendAntialiasedStroke2D(ResolvedPrimitiveTriangles& output, glm::vec2 a, glm::vec2 b, glm::vec2 unitNormal,
                               float halfWidth, Color color, float distanceA, float distanceB,
                               std::uint32_t commandIndex, glm::ivec2 viewport, bool antialias) {
    if (!antialias) {
        const std::array positions{
            pixelToClip(a - unitNormal * halfWidth, viewport), pixelToClip(b - unitNormal * halfWidth, viewport),
            pixelToClip(a + unitNormal * halfWidth, viewport), pixelToClip(b + unitNormal * halfWidth, viewport)};
        appendQuad(output, positions, color, distanceA, distanceB, commandIndex);
        return;
    }
    constexpr float feather     = 0.5f;
    const float     inner       = std::max(0.f, halfWidth - feather);
    const float     outer       = halfWidth + feather;
    Color           transparent = color;
    transparent.a               = 0.f;
    if (inner > 1e-6f) {
        const std::array core{
            pixelToClip(a - unitNormal * inner, viewport), pixelToClip(b - unitNormal * inner, viewport),
            pixelToClip(a + unitNormal * inner, viewport), pixelToClip(b + unitNormal * inner, viewport)};
        appendQuad(output, core, color, distanceA, distanceB, commandIndex);
    }
    const std::array negative{
        pixelToClip(a - unitNormal * outer, viewport), pixelToClip(b - unitNormal * outer, viewport),
        pixelToClip(a - unitNormal * inner, viewport), pixelToClip(b - unitNormal * inner, viewport)};
    appendColoredQuad(output, negative, {transparent, transparent, color, color}, distanceA, distanceB, commandIndex);
    const std::array positive{
        pixelToClip(a + unitNormal * inner, viewport), pixelToClip(b + unitNormal * inner, viewport),
        pixelToClip(a + unitNormal * outer, viewport), pixelToClip(b + unitNormal * outer, viewport)};
    appendColoredQuad(output, positive, {color, color, transparent, transparent}, distanceA, distanceB, commandIndex);
}

void appendAntialiasedStrokeClip(ResolvedPrimitiveTriangles& output, glm::vec4 clipA, glm::vec4 clipB,
                                 glm::vec2 pixelDirection, float halfWidthA, float halfWidthB, Color color,
                                 float distanceA, float distanceB, std::uint32_t commandIndex, glm::ivec2 viewport,
                                 bool antialias) {
    const glm::vec2 normal{-pixelDirection.y, pixelDirection.x};
    const glm::vec2 a             = clipToPixel(clipA, viewport);
    const glm::vec2 b             = clipToPixel(clipB, viewport);
    const auto      makePositions = [&](float widthA, float widthB) {
        return std::array{
            pixelToClipAt(a - normal * widthA, clipA, viewport), pixelToClipAt(b - normal * widthB, clipB, viewport),
            pixelToClipAt(a + normal * widthA, clipA, viewport), pixelToClipAt(b + normal * widthB, clipB, viewport)};
    };
    if (!antialias) {
        appendQuad(output, makePositions(halfWidthA, halfWidthB), color, distanceA, distanceB, commandIndex);
        return;
    }
    constexpr float feather     = 0.5f;
    const float     innerA      = std::max(0.f, halfWidthA - feather);
    const float     innerB      = std::max(0.f, halfWidthB - feather);
    const float     outerA      = halfWidthA + feather;
    const float     outerB      = halfWidthB + feather;
    Color           transparent = color;
    transparent.a               = 0.f;
    if (innerA > 1e-6f || innerB > 1e-6f)
        appendQuad(output, makePositions(innerA, innerB), color, distanceA, distanceB, commandIndex);
    const std::array negative{
        pixelToClipAt(a - normal * outerA, clipA, viewport), pixelToClipAt(b - normal * outerB, clipB, viewport),
        pixelToClipAt(a - normal * innerA, clipA, viewport), pixelToClipAt(b - normal * innerB, clipB, viewport)};
    appendColoredQuad(output, negative, {transparent, transparent, color, color}, distanceA, distanceB, commandIndex);
    const std::array positive{
        pixelToClipAt(a + normal * innerA, clipA, viewport), pixelToClipAt(b + normal * innerB, clipB, viewport),
        pixelToClipAt(a + normal * outerA, clipA, viewport), pixelToClipAt(b + normal * outerB, clipB, viewport)};
    appendColoredQuad(output, positive, {color, color, transparent, transparent}, distanceA, distanceB, commandIndex);
}

void appendCapClip(ResolvedPrimitiveTriangles& output, glm::vec4 pointClip, glm::vec2 outward, float halfWidth,
                   LineCap cap, Color color, float distance, std::uint32_t commandIndex, glm::ivec2 viewport) {
    if (cap == LineCap::Butt || std::fabs(pointClip.w) <= 1e-8f) return;
    const glm::vec2 point = clipToPixel(pointClip, viewport);
    const glm::vec2 normal{-outward.y, outward.x};
    if (cap == LineCap::Square) {
        const std::array positions{
            pixelToClipAt(point - normal * halfWidth, pointClip, viewport),
            pixelToClipAt(point + outward * halfWidth - normal * halfWidth, pointClip, viewport),
            pixelToClipAt(point + normal * halfWidth, pointClip, viewport),
            pixelToClipAt(point + outward * halfWidth + normal * halfWidth, pointClip, viewport)};
        appendQuad(output, positions, color, distance, distance, commandIndex);
        return;
    }
    constexpr std::uint32_t capSegments = 8;
    const float             base        = std::atan2(outward.y, outward.x) - glm::half_pi<float>();
    for (std::uint32_t segment = 0; segment < capSegments; ++segment) {
        const float a = base + glm::pi<float>() * static_cast<float>(segment) / capSegments;
        const float b = base + glm::pi<float>() * static_cast<float>(segment + 1) / capSegments;
        appendTriangle(output, pointClip,
                       pixelToClipAt(point + glm::vec2(std::cos(a), std::sin(a)) * halfWidth, pointClip, viewport),
                       pixelToClipAt(point + glm::vec2(std::cos(b), std::sin(b)) * halfWidth, pointClip, viewport),
                       color, distance, commandIndex);
    }
}

void appendJoinClip(ResolvedPrimitiveTriangles& output, glm::vec4 pointClip, glm::vec2 previousDirection,
                    glm::vec2 nextDirection, float halfWidth, const StrokeStyle& stroke, Color color, float distance,
                    std::uint32_t commandIndex, glm::ivec2 viewport) {
    if (std::fabs(pointClip.w) <= 1e-8f) return;
    const glm::vec2 point = clipToPixel(pointClip, viewport);
    const float     turn  = previousDirection.x * nextDirection.y - previousDirection.y * nextDirection.x;
    if (std::fabs(turn) <= 1e-5f) return;
    const float     outerSign     = turn > 0.f ? -1.f : 1.f;
    const glm::vec2 previousOuter = glm::vec2(-previousDirection.y, previousDirection.x) * outerSign;
    const glm::vec2 nextOuter     = glm::vec2(-nextDirection.y, nextDirection.x) * outerSign;
    const glm::vec2 previousPoint = point + previousOuter * halfWidth;
    const glm::vec2 nextPoint     = point + nextOuter * halfWidth;
    if (stroke.join == LineJoin::Round) {
        float start = std::atan2(previousOuter.y, previousOuter.x);
        float sweep = std::atan2(nextOuter.y, nextOuter.x) - start;
        if (turn > 0.f && sweep < 0.f) sweep += glm::two_pi<float>();
        if (turn < 0.f && sweep > 0.f) sweep -= glm::two_pi<float>();
        const std::uint32_t segments = std::max(1u, static_cast<std::uint32_t>(std::ceil(std::fabs(sweep) * 4.f)));
        for (std::uint32_t segment = 0; segment < segments; ++segment) {
            const float a = start + sweep * static_cast<float>(segment) / segments;
            const float b = start + sweep * static_cast<float>(segment + 1) / segments;
            appendTriangle(output, pointClip,
                           pixelToClipAt(point + glm::vec2(std::cos(a), std::sin(a)) * halfWidth, pointClip, viewport),
                           pixelToClipAt(point + glm::vec2(std::cos(b), std::sin(b)) * halfWidth, pointClip, viewport),
                           color, distance, commandIndex);
        }
        return;
    }
    if (stroke.join == LineJoin::Miter) {
        const glm::vec2 bisector    = glm::normalize(previousOuter + nextOuter);
        const float     denominator = glm::dot(bisector, previousOuter);
        if (std::fabs(denominator) > 1e-5f) {
            const float miterLength = halfWidth / denominator;
            if (std::fabs(miterLength) <= stroke.miterLimit * halfWidth) {
                appendTriangle(output, pixelToClipAt(previousPoint, pointClip, viewport),
                               pixelToClipAt(point + bisector * miterLength, pointClip, viewport),
                               pixelToClipAt(nextPoint, pointClip, viewport), color, distance, commandIndex);
                return;
            }
        }
    }
    appendTriangle(output, pointClip, pixelToClipAt(previousPoint, pointClip, viewport),
                   pixelToClipAt(nextPoint, pointClip, viewport), color, distance, commandIndex);
}

float projectedWorldHalfWidth(glm::vec3 viewPoint, glm::vec3 tangent, float worldWidth,
                              const SceneDrawContext& context) {
    if (glm::length(tangent) <= 1e-6f) return 0.f;
    glm::vec3 normal = glm::cross(glm::normalize(tangent), glm::vec3(0.f, 0.f, -1.f));
    if (glm::length(normal) <= 1e-6f) normal = glm::vec3(0.f, 1.f, 0.f);
    normal                     = glm::normalize(normal) * (worldWidth * 0.5f);
    const glm::vec4 centerClip = context.projection * glm::vec4(viewPoint, 1.f);
    const glm::vec4 edgeClip   = context.projection * glm::vec4(viewPoint + normal, 1.f);
    if (std::fabs(centerClip.w) <= 1e-8f || std::fabs(edgeClip.w) <= 1e-8f) return 0.f;
    return glm::length(clipToPixel(edgeClip, context.viewportSize) - clipToPixel(centerClip, context.viewportSize));
}

struct VisibleDashSpan {
    float begin = 0.f;
    float end   = 1.f;
};

std::vector<VisibleDashSpan> visibleDashSpans(float distanceA, float distanceB,
                                              const std::optional<DashPattern>& dash) {
    if (!dash || distanceB - distanceA <= 1e-8f) return {{0.f, 1.f}};
    dash->validate();
    const float period          = dash->period();
    const float length          = distanceB - distanceA;
    float       patternPosition = std::fmod(distanceA + dash->phase, period);
    if (patternPosition < 0.f) patternPosition += period;

    std::size_t intervalIndex = 0;
    while (patternPosition >= dash->intervals[intervalIndex] && intervalIndex + 1 < dash->intervals.size()) {
        patternPosition -= dash->intervals[intervalIndex++];
    }

    std::vector<VisibleDashSpan> spans;
    float                        cursor = 0.f;
    while (cursor < length - 1e-6f) {
        const float available = dash->intervals[intervalIndex] - patternPosition;
        const float next      = std::min(length, cursor + available);
        if (intervalIndex % 2 == 0 && next > cursor) {
            spans.push_back({cursor / length, next / length});
        }
        cursor          = next;
        patternPosition = 0.f;
        intervalIndex   = (intervalIndex + 1) % dash->intervals.size();
    }
    return spans;
}

void appendDashedQuad(ResolvedPrimitiveTriangles& output, const std::array<glm::vec4, 4>& positions, Color color,
                      float pathDistanceA, float pathDistanceB, float dashDistanceA, float dashDistanceB,
                      const std::optional<DashPattern>& dash, std::uint32_t commandIndex) {
    for (const VisibleDashSpan span : visibleDashSpans(dashDistanceA, dashDistanceB, dash)) {
        std::array<glm::vec4, 4> clipped{
            glm::mix(positions[0], positions[1], span.begin), glm::mix(positions[0], positions[1], span.end),
            glm::mix(positions[2], positions[3], span.begin), glm::mix(positions[2], positions[3], span.end)};
        appendQuad(output, clipped, color, glm::mix(pathDistanceA, pathDistanceB, span.begin),
                   glm::mix(pathDistanceA, pathDistanceB, span.end), commandIndex);
    }
}

glm::vec2 transformedPoint(const glm::mat3& transform, glm::vec2 point) {
    const glm::vec3 result = transform * glm::vec3(point, 1.f);
    return {result.x, result.y};
}

bool clipViewSegmentToNear(glm::vec3& a, glm::vec3& b, float& distanceA, float& distanceB, float nearPlane) {
    const float planeZ   = -nearPlane;
    const bool  aVisible = a.z <= planeZ;
    const bool  bVisible = b.z <= planeZ;
    if (!aVisible && !bVisible) return false;
    if (aVisible && bVisible) return true;
    const glm::vec3 originalA            = a;
    const glm::vec3 originalB            = b;
    const float     originalDistanceA    = distanceA;
    const float     originalDistanceB    = distanceB;
    const float     t                    = (planeZ - originalA.z) / (originalB.z - originalA.z);
    const glm::vec3 intersection         = originalA + (originalB - originalA) * t;
    const float     intersectionDistance = originalDistanceA + (originalDistanceB - originalDistanceA) * t;
    if (!aVisible) {
        a         = intersection;
        distanceA = intersectionDistance;
    } else {
        b         = intersection;
        distanceB = intersectionDistance;
    }
    return true;
}

std::vector<glm::vec3> clipViewPolygonToNear(std::span<const glm::vec3> input, float nearPlane) {
    std::vector<glm::vec3> output;
    if (input.empty()) return output;
    const float planeZ         = -nearPlane;
    glm::vec3   previous       = input.back();
    bool        previousInside = previous.z <= planeZ;
    for (const glm::vec3 current : input) {
        const bool currentInside = current.z <= planeZ;
        if (currentInside != previousInside) {
            const float t = (planeZ - previous.z) / (current.z - previous.z);
            output.push_back(previous + (current - previous) * t);
        }
        if (currentInside) output.push_back(current);
        previous       = current;
        previousInside = currentInside;
    }
    return output;
}

}  // namespace

ResolvedPrimitiveTriangles resolvePrimitiveStrokes2D(const PrimitiveCanvas2D& canvas, glm::ivec2 viewport) {
    const bool validViewport = viewport.x > 0 && viewport.y > 0;
    EV_PARAM_CHECK(validViewport, "primitive 2D resolve viewport must be positive");
    ResolvedPrimitiveTriangles output;
    output.statistics = canvas.statistics();

    for (std::size_t triangleIndex = 0; triangleIndex < canvas.triangles().size(); ++triangleIndex) {
        const auto&       triangle    = canvas.triangles()[triangleIndex];
        const std::size_t firstVertex = output.vertices.size();
        for (std::size_t pointIndex = 0; pointIndex < triangle.points.size(); ++pointIndex) {
            const glm::vec2 point       = triangle.points[pointIndex];
            const glm::vec2 transformed = transformedPoint(triangle.transform, point);
            const glm::vec4 clip(transformed.x * 2.f / static_cast<float>(viewport.x) - 1.f,
                                 transformed.y * 2.f / static_cast<float>(viewport.y) - 1.f, 0.f, 1.f);
            const Color     color = triangle.vertexColors ? (*triangle.vertexColors)[pointIndex] : triangle.paint.color;
            output.vertices.push_back({clip, color, 0.f, 0.f, static_cast<std::uint32_t>(triangleIndex)});
        }
        ++output.statistics.triangleCount;
        output.batches2D.push_back(
            {firstVertex, output.vertices.size() - firstVertex, triangle.paint.blend, output.batches2D.size()});
    }

    for (std::size_t commandIndex = 0; commandIndex < canvas.commands().size(); ++commandIndex) {
        const auto&            command      = canvas.commands()[commandIndex];
        const std::size_t      firstVertex  = output.vertices.size();
        const std::size_t      segmentCount = command.points.size() - 1 + (command.closed ? 1u : 0u);
        std::vector<glm::vec2> transformed;
        transformed.reserve(command.points.size());
        for (const glm::vec2 point : command.points) transformed.push_back(transformedPoint(command.transform, point));
        for (std::size_t segment = 0; segment < segmentCount; ++segment) {
            const std::size_t next      = (segment + 1) % command.points.size();
            const glm::vec2   a         = transformed[segment];
            const glm::vec2   b         = transformed[next];
            const glm::vec2   direction = b - a;
            const float       length    = glm::length(direction);
            if (!std::isfinite(length) || length <= 1e-6f) continue;
            const float     width         = command.paint.stroke.width;
            const glm::vec2 unitDirection = direction / length;
            const glm::vec2 unitNormal{-unitDirection.y, unitDirection.x};
            const float     halfWidth = width * 0.5f;
            const float     distanceA = command.cumulativeLengths[segment];
            const float     distanceB = command.cumulativeLengths[segment + 1];
            const auto      spans     = visibleDashSpans(distanceA, distanceB, command.paint.stroke.dash);
            for (const VisibleDashSpan span : spans) {
                const glm::vec2 spanA         = glm::mix(a, b, span.begin);
                const glm::vec2 spanB         = glm::mix(a, b, span.end);
                const float     spanDistanceA = glm::mix(distanceA, distanceB, span.begin);
                const float     spanDistanceB = glm::mix(distanceA, distanceB, span.end);
                appendAntialiasedStroke2D(output, spanA, spanB, unitNormal, halfWidth, command.paint.color,
                                          spanDistanceA, spanDistanceB, static_cast<std::uint32_t>(commandIndex),
                                          viewport, command.paint.antialias);
                if (command.paint.stroke.dash) {
                    appendCap2D(output, spanA, -unitDirection, halfWidth, command.paint.stroke.cap, command.paint.color,
                                spanDistanceA, static_cast<std::uint32_t>(commandIndex), viewport);
                    appendCap2D(output, spanB, unitDirection, halfWidth, command.paint.stroke.cap, command.paint.color,
                                spanDistanceB, static_cast<std::uint32_t>(commandIndex), viewport);
                }
            }
        }
        if (!command.paint.stroke.dash) {
            for (std::size_t point = command.closed ? 0u : 1u;
                 point < (command.closed ? transformed.size() : transformed.size() - 1u); ++point) {
                const std::size_t previous          = (point + transformed.size() - 1u) % transformed.size();
                const std::size_t next              = (point + 1u) % transformed.size();
                const glm::vec2   previousDirection = glm::normalize(transformed[point] - transformed[previous]);
                const glm::vec2   nextDirection     = glm::normalize(transformed[next] - transformed[point]);
                appendJoin2D(output, transformed[point], previousDirection, nextDirection,
                             command.paint.stroke.width * 0.5f, command.paint.stroke, command.paint.color,
                             command.cumulativeLengths[point], static_cast<std::uint32_t>(commandIndex), viewport);
            }
            if (!command.closed) {
                const glm::vec2 startDirection = glm::normalize(transformed[1] - transformed[0]);
                const glm::vec2 endDirection =
                    glm::normalize(transformed.back() - transformed[transformed.size() - 2u]);
                appendCap2D(output, transformed.front(), -startDirection, command.paint.stroke.width * 0.5f,
                            command.paint.stroke.cap, command.paint.color, command.cumulativeLengths.front(),
                            static_cast<std::uint32_t>(commandIndex), viewport);
                appendCap2D(output, transformed.back(), endDirection, command.paint.stroke.width * 0.5f,
                            command.paint.stroke.cap, command.paint.color, command.cumulativeLengths.back(),
                            static_cast<std::uint32_t>(commandIndex), viewport);
            }
        }
        if (output.vertices.size() > firstVertex)
            output.batches2D.push_back(
                {firstVertex, output.vertices.size() - firstVertex, command.paint.blend, output.batches2D.size()});
    }
    output.statistics.uploadBytes = output.vertices.size() * sizeof(PrimitiveTriangleVertex);
    output.statistics.batchCount  = output.batches2D.size();
    return output;
}

ResolvedPrimitiveTriangles resolvePrimitiveStrokes3D(const PrimitiveSceneCanvas3D& canvas) {
    const SceneDrawContext& context = canvas.context();
    context.validate();
    ResolvedPrimitiveTriangles output;
    output.statistics = canvas.statistics();

    std::size_t sequence = 0;
    for (const TriangleCommand3D& triangle : canvas.triangles()) {
        const glm::mat4          modelView = context.view * triangle.transform;
        std::array<glm::vec3, 3> viewPoints{};
        for (std::size_t i = 0; i < viewPoints.size(); ++i)
            viewPoints[i] = glm::vec3(modelView * glm::vec4(triangle.points[i], 1.f));
        const std::vector<glm::vec3> clipped = clipViewPolygonToNear(viewPoints, context.nearPlane);
        if (clipped.size() < 3) {
            ++sequence;
            continue;
        }
        const std::size_t   firstVertex = output.vertices.size();
        float               depthSum    = 0.f;
        std::size_t         depthCount  = 0;
        const std::uint32_t batchIndex  = static_cast<std::uint32_t>(output.batches3D.size());
        for (std::size_t i = 1; i + 1 < clipped.size(); ++i) {
            const std::array fan{clipped[0], clipped[i], clipped[i + 1]};
            for (const glm::vec3 viewPoint : fan) {
                const glm::vec4 clip = context.projection * glm::vec4(viewPoint, 1.f);
                output.vertices.push_back({clip, triangle.paint.color, 0.f, 0.f, batchIndex});
                if (std::fabs(clip.w) > 1e-8f) {
                    depthSum += clip.z / clip.w;
                    ++depthCount;
                }
            }
            ++output.statistics.triangleCount;
        }
        output.batches3D.push_back({firstVertex, output.vertices.size() - firstVertex, triangle.paint,
                                    depthCount ? depthSum / static_cast<float>(depthCount) : 1.f, sequence++});
    }

    for (std::size_t commandIndex = 0; commandIndex < canvas.commands().size(); ++commandIndex) {
        const auto&            command      = canvas.commands()[commandIndex];
        const std::size_t      firstVertex  = output.vertices.size();
        const std::size_t      segmentCount = command.points.size() - 1 + (command.closed ? 1u : 0u);
        const glm::mat4        modelView    = context.view * command.transform;
        std::vector<glm::vec3> viewPoints;
        viewPoints.reserve(command.points.size());
        for (const glm::vec3 point : command.points) viewPoints.push_back(glm::vec3(modelView * glm::vec4(point, 1.f)));
        float cumulativeScreenDistance = 0.f;
        for (std::size_t segment = 0; segment < segmentCount; ++segment) {
            const std::size_t next      = (segment + 1) % command.points.size();
            glm::vec3         viewA     = viewPoints[segment];
            glm::vec3         viewB     = viewPoints[next];
            float             distanceA = command.cumulativeWorldLengths[segment];
            float             distanceB = command.cumulativeWorldLengths[segment + 1];
            if (!clipViewSegmentToNear(viewA, viewB, distanceA, distanceB, context.nearPlane)) continue;

            const glm::vec4 clipA = context.projection * glm::vec4(viewA, 1.f);
            const glm::vec4 clipB = context.projection * glm::vec4(viewB, 1.f);
            if (std::fabs(clipA.w) <= 1e-8f || std::fabs(clipB.w) <= 1e-8f) continue;
            const glm::vec2 ndcA         = glm::vec2(clipA) / clipA.w;
            const glm::vec2 ndcB         = glm::vec2(clipB) / clipB.w;
            const float     screenLength = glm::length((ndcB - ndcA) * glm::vec2(context.viewportSize) * 0.5f);
            if (!std::isfinite(screenLength) || screenLength <= 1e-6f) continue;

            std::array<glm::vec4, 4> positions;
            if (command.paint.stroke.widthSpace == WidthSpace::ScreenPixels) {
                glm::vec2 direction = ndcB - ndcA;
                direction           = glm::normalize(direction * glm::vec2(context.viewportSize));
                const glm::vec2 pixelNormal{-direction.y, direction.x};
                const glm::vec2 ndcOffset = pixelNormal * command.paint.stroke.width / glm::vec2(context.viewportSize);
                positions                 = {glm::vec4((ndcA - ndcOffset) * clipA.w, clipA.z, clipA.w),
                                             glm::vec4((ndcB - ndcOffset) * clipB.w, clipB.z, clipB.w),
                                             glm::vec4((ndcA + ndcOffset) * clipA.w, clipA.z, clipA.w),
                                             glm::vec4((ndcB + ndcOffset) * clipB.w, clipB.z, clipB.w)};
            } else {
                const glm::vec3 direction = glm::normalize(viewB - viewA);
                glm::vec3       normal    = glm::cross(direction, glm::vec3(0.f, 0.f, -1.f));
                if (glm::length(normal) <= 1e-6f) normal = glm::vec3(0.f, 1.f, 0.f);
                normal    = glm::normalize(normal) * (command.paint.stroke.width * 0.5f);
                positions = {context.projection * glm::vec4(viewA - normal, 1.f),
                             context.projection * glm::vec4(viewB - normal, 1.f),
                             context.projection * glm::vec4(viewA + normal, 1.f),
                             context.projection * glm::vec4(viewB + normal, 1.f)};
            }
            const bool screenDash =
                command.paint.stroke.dash && command.paint.stroke.dash->space == DashSpace::ScreenPixels;
            const float         dashDistanceA  = screenDash ? cumulativeScreenDistance : distanceA;
            const float         dashDistanceB  = screenDash ? cumulativeScreenDistance + screenLength : distanceB;
            const std::uint32_t batchIndex     = static_cast<std::uint32_t>(output.batches3D.size());
            const glm::vec2     pixelA         = clipToPixel(clipA, context.viewportSize);
            const glm::vec2     pixelB         = clipToPixel(clipB, context.viewportSize);
            const glm::vec2     pixelDirection = glm::normalize(pixelB - pixelA);
            const float         halfWidthA     = glm::length(clipToPixel(positions[0], context.viewportSize) - pixelA);
            const float         halfWidthB     = glm::length(clipToPixel(positions[1], context.viewportSize) - pixelB);
            for (const VisibleDashSpan span :
                 visibleDashSpans(dashDistanceA, dashDistanceB, command.paint.stroke.dash)) {
                const float spanDistanceA = glm::mix(distanceA, distanceB, span.begin);
                const float spanDistanceB = glm::mix(distanceA, distanceB, span.end);
                appendAntialiasedStrokeClip(
                    output, glm::mix(clipA, clipB, span.begin), glm::mix(clipA, clipB, span.end), pixelDirection,
                    glm::mix(halfWidthA, halfWidthB, span.begin), glm::mix(halfWidthA, halfWidthB, span.end),
                    command.paint.color, spanDistanceA, spanDistanceB, batchIndex, context.viewportSize,
                    command.paint.antialias);
                if (command.paint.stroke.dash) {
                    appendCapClip(output, glm::mix(clipA, clipB, span.begin), -pixelDirection,
                                  glm::mix(halfWidthA, halfWidthB, span.begin), command.paint.stroke.cap,
                                  command.paint.color, spanDistanceA, batchIndex, context.viewportSize);
                    appendCapClip(output, glm::mix(clipA, clipB, span.end), pixelDirection,
                                  glm::mix(halfWidthA, halfWidthB, span.end), command.paint.stroke.cap,
                                  command.paint.color, spanDistanceB, batchIndex, context.viewportSize);
                }
            }
            cumulativeScreenDistance += screenLength;
        }
        if (!command.paint.stroke.dash) {
            const std::uint32_t batchIndex = static_cast<std::uint32_t>(output.batches3D.size());
            for (std::size_t point = command.closed ? 0u : 1u;
                 point < (command.closed ? viewPoints.size() : viewPoints.size() - 1u); ++point) {
                if (viewPoints[point].z > -context.nearPlane) continue;
                const std::size_t previous     = (point + viewPoints.size() - 1u) % viewPoints.size();
                const std::size_t next         = (point + 1u) % viewPoints.size();
                const glm::vec4   previousClip = context.projection * glm::vec4(viewPoints[previous], 1.f);
                const glm::vec4   pointClip    = context.projection * glm::vec4(viewPoints[point], 1.f);
                const glm::vec4   nextClip     = context.projection * glm::vec4(viewPoints[next], 1.f);
                if (previousClip.w <= 1e-8f || pointClip.w <= 1e-8f || nextClip.w <= 1e-8f) continue;
                const glm::vec2 previousDirection = glm::normalize(clipToPixel(pointClip, context.viewportSize) -
                                                                   clipToPixel(previousClip, context.viewportSize));
                const glm::vec2 nextDirection     = glm::normalize(clipToPixel(nextClip, context.viewportSize) -
                                                                   clipToPixel(pointClip, context.viewportSize));
                float           halfWidth         = command.paint.stroke.width * 0.5f;
                if (command.paint.stroke.widthSpace == WidthSpace::WorldUnits) {
                    halfWidth = projectedWorldHalfWidth(viewPoints[point], viewPoints[next] - viewPoints[previous],
                                                        command.paint.stroke.width, context);
                }
                appendJoinClip(output, pointClip, previousDirection, nextDirection, halfWidth, command.paint.stroke,
                               command.paint.color, command.cumulativeWorldLengths[point], batchIndex,
                               context.viewportSize);
            }
            if (!command.closed) {
                const auto appendEndpoint = [&](std::size_t point, std::size_t adjacent) {
                    if (viewPoints[point].z > -context.nearPlane) return;
                    const glm::vec4 pointClip    = context.projection * glm::vec4(viewPoints[point], 1.f);
                    const glm::vec4 adjacentClip = context.projection * glm::vec4(viewPoints[adjacent], 1.f);
                    if (pointClip.w <= 1e-8f || adjacentClip.w <= 1e-8f) return;
                    glm::vec2 direction = glm::normalize(clipToPixel(adjacentClip, context.viewportSize) -
                                                         clipToPixel(pointClip, context.viewportSize));
                    float     halfWidth = command.paint.stroke.width * 0.5f;
                    if (command.paint.stroke.widthSpace == WidthSpace::WorldUnits)
                        halfWidth = projectedWorldHalfWidth(viewPoints[point], viewPoints[adjacent] - viewPoints[point],
                                                            command.paint.stroke.width, context);
                    appendCapClip(output, pointClip, -direction, halfWidth, command.paint.stroke.cap,
                                  command.paint.color, command.cumulativeWorldLengths[point], batchIndex,
                                  context.viewportSize);
                };
                appendEndpoint(0u, 1u);
                appendEndpoint(viewPoints.size() - 1u, viewPoints.size() - 2u);
            }
        }
        if (output.vertices.size() > firstVertex) {
            float depthSum = 0.f;
            for (std::size_t vertex = firstVertex; vertex < output.vertices.size(); ++vertex) {
                const glm::vec4 clip = output.vertices[vertex].clipPosition;
                if (std::fabs(clip.w) > 1e-8f) depthSum += clip.z / clip.w;
            }
            output.batches3D.push_back({firstVertex, output.vertices.size() - firstVertex, command.paint,
                                        depthSum / static_cast<float>(output.vertices.size() - firstVertex),
                                        sequence++});
        } else {
            ++sequence;
        }
    }
    output.statistics.uploadBytes = output.vertices.size() * sizeof(PrimitiveTriangleVertex);
    output.statistics.batchCount  = output.vertices.empty() ? 0u : 1u;
    return output;
}

}  // namespace eve::graphics
