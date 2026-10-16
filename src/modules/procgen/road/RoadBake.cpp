#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadBakeInternal.h"
#include "procgen/road/RoadGeometrySourcesInternal.h"

#include "common/Diagnostic.h"
#include "procgen/road/RoadAuthoringInternal.h"
#include "procgen/road/RoadConnectionInternal.h"
#include "procgen/spline/SplinePath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::procgen::road {
using namespace detail;
namespace detail {


V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3    cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(V3 a) { return std::sqrt(dot(a, a)); }
V3 normalize(V3 a) {
    const float l = length(a);
    return l > 1e-8f ? a * (1.f / l) : V3{0.f, 1.f, 0.f};
}


bool isMeshBudgetExceeded(const MeshBuild& mesh, const RoadBakeOptions& options) {
    return static_cast<std::size_t>(mesh.getVertexCount()) + static_cast<std::size_t>(mesh.getIndexCount()) >
           options.maximumMeshElements;
}

float junctionTrimDistance(float pathLength, float requestedRadius) {
    const float maximum = std::max(0.f, pathLength * 0.5f - 0.5f);
    return std::min(std::max(0.f, requestedRadius), maximum);
}

int circularArcSegments(float radius, float angle, float maximumError) {
    if (radius <= maximumError) return 4;
    const float halfStep = std::acos(std::clamp(1.f - maximumError / radius, -1.f, 1.f));
    if (!std::isfinite(halfStep) || halfStep <= 1e-5f) return 64;
    return std::clamp(static_cast<int>(std::ceil(angle / (2.f * halfStep))), 4, 64);
}

int quadraticSegments(V3 start, V3 control, V3 end, float angle, float maximumError) {
    const float secondDifference = length(start - control * 2.f + end);
    const int errorSegments =
        std::max(1, static_cast<int>(std::ceil(std::sqrt(secondDifference / (8.f * maximumError)))));
    const int angleSegments = 3 + static_cast<int>(angle * 3.f);
    return std::clamp(std::max(errorSegments, angleSegments), 4, 64);
}

Result<SplinePath> edgeToSpline(const RoadEdge& edge) {
    SplinePath path;
    auto kind = path.setKindResult("catmullRom");
    if (!kind.ok()) return Result<SplinePath>::failure(kind.status());
    path.setClosed(false);
    for (const auto& p : edge.controlPoints) {
        SplinePoint sp;
        sp.alignStraightStart  = p.alignStraightStart;
        sp.alignStraightEnd    = p.alignStraightEnd;
        sp.circularFromEnd     = p.circularFromEnd;
        sp.spanControl         = static_cast<eve::procgen::SplineSpanControl>(p.pathControl);
        sp.spanStrength        = p.splineStrength;
        sp.x = p.x;
        sp.y = p.y;
        sp.z = p.z;
        sp.scaleX              = p.widthScale;
        sp.scaleXInterpolation = edge.style.widthInterpolation == RoadWidthInterpolation::CatmullRom
                                     ? SplineScaleInterpolation::CatmullRom
                                     : SplineScaleInterpolation::Linear;
        sp.rollDegrees         = p.rollDegrees;
        sp.rollPivotSide       = (-p.rollCenterOffset) * 2.f * asphaltHalfWidth(edge);
        sp.worldUpFrame        = edge.style.frameOrientation == RoadFrameOrientation::WorldUp;
        sp.rollInterpolation   = edge.style.rollInterpolation == RoadRollInterpolation::SmoothStep
                                     ? SplineRollInterpolation::SmoothStep
                                     : SplineRollInterpolation::Linear;
        auto added = path.addPointResult(sp);
        if (!added.ok()) return Result<SplinePath>::failure(added.status());
    }
    return Result<SplinePath>::success(std::move(path));
}

V3 authoredOutwardDirection(const RoadNode& node, const RoadEdge& edge) {
    if (edge.controlPoints.size() < 2) return {1.f, 0.f, 0.f};
    const auto& adjacent =
        edge.from == node.id ? edge.controlPoints[1] : edge.controlPoints[edge.controlPoints.size() - 2];
    return normalize(V3{adjacent.x - node.x, 0.f, adjacent.z - node.z});
}

float asphaltHalfWidth(const RoadEdge& edge) {
    float half = 0.5f * edge.style.laneWidth * static_cast<float>(edge.lanesForward + edge.lanesBackward);
    if (!edge.style.crossSection.empty()) {
        half                     = 0.f;
        const auto&       points = edge.style.crossSection;
        const std::size_t count  = edge.style.crossSectionClosed ? points.size() : points.size() - 1;
        for (std::size_t i = 0; i < count; ++i)
            if (points[i].material == RoadMaterial::Asphalt)
                half = std::max({half, std::fabs(points[i].side), std::fabs(points[(i + 1) % points.size()].side)});
    }
    return half;
}

/** Compute a finite socket distance whose two mouth corners cannot cross an adjacent arm. */
float scaledProfileSide(float side, float asphaltHalf, float scale) {
    if (scale == 1.f) return side;
    const float magnitude = std::fabs(side);
    if (magnitude <= asphaltHalf) return side * scale;
    return std::copysign(asphaltHalf * scale + magnitude - asphaltHalf, side);
}

float profileAsphaltHalfWidth(const RoadProfile& profile) {
    float result = 0.f;
    if (profile.explicitMaterials) {
        const std::size_t count = profile.closed ? profile.points.size() : profile.points.size() - 1;
        for (std::size_t i = 0; i < count; ++i)
            if (profile.points[i].material == RoadMaterial::Asphalt)
                result = std::max({result, std::fabs(profile.points[i].side),
                                   std::fabs(profile.points[(i + 1) % profile.points.size()].side)});
    } else
        for (const auto& point : profile.points)
            if (point.material == RoadMaterial::Asphalt) result = std::max(result, std::fabs(point.side));
    return result;
}

float maximumAsphaltHalfWidth(const RoadEdge& edge) {
    float scale   = 0.f;
    float minimum = 16.f;
    for (const auto& point : edge.controlPoints) {
        scale   = std::max(scale, point.widthScale);
        minimum = std::min(minimum, point.widthScale);
    }
    if (edge.style.widthInterpolation == RoadWidthInterpolation::CatmullRom) scale += .125f * (scale - minimum);
    return asphaltHalfWidth(edge) * scale;
}

float junctionSocketDistance(const RoadNetwork& network, const RoadNode& node, const RoadEdge& edge, float pathLength) {
    if (!node.connection.ports.empty()) return connectionSocketDistance(node, edge, pathLength);
    float       required  = node.junctionRadius;
    const V3    direction = authoredOutwardDirection(node, edge);
    const float halfWidth = maximumAsphaltHalfWidth(edge);

    // Only the immediate clockwise/counter-clockwise arms bound this mouth.
    // Letting every incident arm participate makes dense junctions grow with
    // unrelated opposite/diagonal roads and creates overlapping aprons.
    const RoadEdge* clockwise             = nullptr;
    const RoadEdge* counterClockwise      = nullptr;
    float           clockwiseAngle        = std::numeric_limits<float>::max();
    float           counterClockwiseAngle = std::numeric_limits<float>::max();
    for (const auto& other : network.edges()) {
        if (other.id == edge.id || (other.from != node.id && other.to != node.id)) continue;
        const V3    otherDirection = authoredOutwardDirection(node, other);
        const float cosine         = std::clamp(dot(direction, otherDirection), -1.f, 1.f);
        const float signedAngle =
            std::atan2(direction.x * otherDirection.z - direction.z * otherDirection.x, cosine);
        const float angle = std::fabs(signedAngle);
        if (signedAngle < 0.f && angle < clockwiseAngle) {
            clockwise      = &other;
            clockwiseAngle = angle;
        } else if (signedAngle >= 0.f && angle < counterClockwiseAngle) {
            counterClockwise      = &other;
            counterClockwiseAngle = angle;
        }
    }

    auto constrainTo = [&](const RoadEdge* other) {
        if (other == nullptr) return;
        const V3    otherDirection = authoredOutwardDirection(node, *other);
        const float cosine         = std::clamp(dot(direction, otherDirection), -1.f, 1.f);
        const float sine           = std::sqrt(std::max(0.f, 1.f - cosine * cosine));
        // Collinear continuations constrain no corner; near-parallel duplicate
        // arms are rejected by the network validator before reaching bake.
        if (sine < 0.08f) return;
        const float tangentDistance = (asphaltHalfWidth(*other) + halfWidth * std::fabs(cosine)) / sine;
        required                    = std::max(required, tangentDistance + 0.35f);
    };
    constrainTo(clockwise);
    if (counterClockwise != clockwise) constrainTo(counterClockwise);
    return junctionTrimDistance(pathLength, required);
}


float planeHeight(const JunctionPlane& plane, float x, float z) {
    return plane.origin.y + plane.gradeX * (x - plane.origin.x) + plane.gradeZ * (z - plane.origin.z);
}

/** Fit one bounded road plane through the authored node to all incident trim sockets. */
JunctionPlane fitJunctionPlane(const RoadNetwork& network, const RoadNode& node) {
    JunctionPlane plane{{node.x, node.y, node.z}};
    float         xx = 0.f, xz = 0.f, zz = 0.f, xy = 0.f, zy = 0.f;
    for (const auto& edge : network.edges()) {
        if (edge.from != node.id && edge.to != node.id) continue;
        auto spline = edgeToSpline(edge);
        if (!spline.ok()) continue;
        auto pathLength = spline.value().lengthResult(16);
        if (!pathLength.ok()) continue;
        const float trim  = junctionSocketDistance(network, node, edge, pathLength.value());
        const float d     = edge.from == node.id ? trim : pathLength.value() - trim;
        auto        frame = spline.value().travelFrameResult(d, "clamp", 16);
        if (!frame.ok()) continue;
        const float dx = frame.value().sample.x - node.x;
        const float dz = frame.value().sample.z - node.z;
        const float dy = frame.value().sample.y - node.y;
        const float weight =
            std::max(1.f, edge.style.laneWidth * static_cast<float>(edge.lanesForward + edge.lanesBackward));
        xx += weight * dx * dx;
        xz += weight * dx * dz;
        zz += weight * dz * dz;
        xy += weight * dx * dy;
        zy += weight * dz * dy;
    }
    const float determinant = xx * zz - xz * xz;
    if (std::fabs(determinant) > 1e-5f) {
        plane.gradeX = (xy * zz - zy * xz) / determinant;
        plane.gradeZ = (zy * xx - xy * xz) / determinant;
    }
    // A road junction is a local construction plane, not a terrain patch. Limit
    // the fitted grade so a single malformed arm cannot turn it into a wall.
    const float grade = std::sqrt(plane.gradeX * plane.gradeX + plane.gradeZ * plane.gradeZ);
    if (grade > 0.25f) {
        plane.gradeX *= 0.25f / grade;
        plane.gradeZ *= 0.25f / grade;
    }
    plane.up = normalize(V3{-plane.gradeX, 1.f, -plane.gradeZ});
    return plane;
}

Result<void> validateJunctionGeometry(const RoadNetwork& network, const RoadNode& node) {
    if (!node.connection.ports.empty()) return validateConnectionSeams(network, node);
    for (const auto& edge : network.edges()) {
        if ((edge.from != node.id && edge.to != node.id) || edge.style.crossSection.empty()) continue;
        if (!edge.style.crossSectionClosed && edge.style.crossSection.size() == 2u &&
            std::fabs(edge.style.crossSection[0].up) < 1e-4f && std::fabs(edge.style.crossSection[1].up) < 1e-4f &&
            std::fabs(edge.style.crossSection[0].side + edge.style.crossSection[1].side) < 1e-4f)
            continue;
        auto standard = edge.style;
        standard.crossSection.clear();
        standard.crossSectionClosed = false;
        auto canonical              = makeRoadProfile(standard, edge.lanesForward, edge.lanesBackward);
        if (!canonical.ok()) return Result<void>::failure(canonical.status());
        auto samePoint = [](const auto& a, const auto& b) {
            return std::fabs(a.side - b.side) < 1e-4f && std::fabs(a.up - b.up) < 1e-4f;
        };
        for (const auto& point : edge.style.crossSection)
            if (!std::any_of(canonical.value().points.begin(), canonical.value().points.end(),
                             [&](const auto& p) { return samePoint(point, p); }))
                return bakeFail<void>(DiagnosticCode::PreconditionViolation,
                                      "nonstandard sections require a compatible custom connection asset", "junction");
        for (const auto& point : canonical.value().points)
            if (!std::any_of(edge.style.crossSection.begin(), edge.style.crossSection.end(),
                             [&](const auto& p) { return samePoint(point, p); }))
                return bakeFail<void>(DiagnosticCode::PreconditionViolation,
                                      "custom profile does not match the procedural junction section", "junction");
    }
    std::vector<const RoadEdge*> incident;
    std::vector<V3>              directions;
    for (const auto& edge : network.edges()) {
        if (edge.from != node.id && edge.to != node.id) continue;
        incident.push_back(&edge);
        directions.push_back(authoredOutwardDirection(node, edge));
    }
    for (std::size_t first = 0; directions.size() >= 3u && first < directions.size(); ++first)
        for (std::size_t second = first + 1; second < directions.size(); ++second)
            if (dot(directions[first], directions[second]) > 0.9999905f)
                return bakeFail<void>(DiagnosticCode::PreconditionViolation,
                                      "junction contains two arms with indistinguishable outgoing directions",
                                      "junction");

    const JunctionPlane plane = fitJunctionPlane(network, node);
    if (incident.size() < 4u) return Result<void>::success();
    for (const RoadEdge* edge : incident) {
        auto spline = edgeToSpline(*edge);
        if (!spline.ok()) return Result<void>::failure(spline.status());
        auto pathLength = spline.value().lengthResult(24);
        if (!pathLength.ok()) return Result<void>::failure(pathLength.status());
        const float trim = junctionSocketDistance(network, node, *edge, pathLength.value());
        const float distance = edge->from == node.id ? trim : pathLength.value() - trim;
        auto        frame    = spline.value().travelFrameResult(distance, "clamp", 24);
        if (!frame.ok()) return Result<void>::failure(frame.status());
        const float expected = planeHeight(plane, frame.value().sample.x, frame.value().sample.z);
        const float tolerance = std::max(0.35f, asphaltHalfWidth(*edge) * 0.15f);
        if (std::fabs(frame.value().sample.y - expected) > tolerance)
            return bakeFail<void>(DiagnosticCode::PreconditionViolation,
                                  "junction arm sockets cannot share one bounded construction plane", "junction");
    }
    return Result<void>::success();
}

void blendFrameToJunctionPlane(SplineFrameSample& frame, const JunctionPlane& plane, float weight) {
    weight              = std::clamp(weight, 0.f, 1.f);
    const float targetY = planeHeight(plane, frame.sample.x, frame.sample.z);
    frame.sample.y += (targetY - frame.sample.y) * weight;

    const V3 oldForward = normalize(V3{frame.forwardX, frame.forwardY, frame.forwardZ});
    const V3 oldSide    = normalize(V3{frame.sideX, frame.sideY, frame.sideZ});
    V3       targetForward =
        normalize(V3{oldForward.x, plane.gradeX * oldForward.x + plane.gradeZ * oldForward.z, oldForward.z});
    V3 targetSide = normalize(cross(plane.up, targetForward));
    if (dot(targetSide, oldSide) < 0.f) targetSide = targetSide * -1.f;
    V3 targetUp = normalize(cross(targetForward, targetSide));
    if (targetUp.y < 0.f) targetUp = targetUp * -1.f;

    const V3 forward = normalize(oldForward * (1.f - weight) + targetForward * weight);
    const V3 side    = normalize(oldSide * (1.f - weight) + targetSide * weight);
    V3       up      = normalize(cross(forward, side));
    if (dot(up, targetUp) < 0.f) up = up * -1.f;
    frame.forwardX = forward.x;
    frame.forwardY = forward.y;
    frame.forwardZ = forward.z;
    frame.sideX    = side.x;
    frame.sideY    = side.y;
    frame.sideZ    = side.z;
    frame.upX      = up.x;
    frame.upY      = up.y;
    frame.upZ      = up.z;
}

float signedAreaXZ(const std::vector<V3>& polygon) {
    float area = 0.f;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const V3& a = polygon[i];
        const V3& b = polygon[(i + 1) % polygon.size()];
        area += a.x * b.z - b.x * a.z;
    }
    return area * 0.5f;
}

float crossXZ(V3 a, V3 b, V3 c) { return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x); }

bool isPointInTriangleXZ(V3 p, V3 a, V3 b, V3 c, float orientation) {
    constexpr float epsilon = 1e-5f;
    // Boundary points on a sampled curve must not veto the ear; only a point
    // strictly inside the candidate triangle makes that diagonal invalid.
    return crossXZ(a, b, p) * orientation > epsilon && crossXZ(b, c, p) * orientation > epsilon &&
           crossXZ(c, a, p) * orientation > epsilon;
}

/** Ear clipping keeps every sampled curb-return segment as a hard polygon boundary. */
TriangulationStatus triangulateBoundary(const std::vector<V3>&                   polygon,
                                        std::vector<std::array<std::size_t, 3>>& triangles,
                                        BoundaryPointPolicy                      boundaryPolicy) {
    if (polygon.size() < 3) return TriangulationStatus::Degenerate;
    const float area = signedAreaXZ(polygon);
    if (std::fabs(area) < 1e-5f) return TriangulationStatus::Degenerate;
    const float              orientation = area > 0.f ? 1.f : -1.f;
    std::vector<std::size_t> remaining(polygon.size());
    for (std::size_t i = 0; i < remaining.size(); ++i) remaining[i] = i;
    triangles.clear();
    triangles.reserve(polygon.size() - 2);
    std::size_t guard = polygon.size() * polygon.size();
    while (remaining.size() > 3 && guard-- > 0) {
        bool clipped = false;
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            const std::size_t previous = remaining[(i + remaining.size() - 1) % remaining.size()];
            const std::size_t current  = remaining[i];
            const std::size_t next     = remaining[(i + 1) % remaining.size()];
            if (crossXZ(polygon[previous], polygon[current], polygon[next]) * orientation <= 1e-5f) continue;
            bool contains = false;
            for (const std::size_t candidate : remaining) {
                if (candidate == previous || candidate == current || candidate == next) continue;
                const bool inclusive =
                    boundaryPolicy == BoundaryPointPolicy::IncludeBoundary &&
                    crossXZ(polygon[previous], polygon[current], polygon[candidate]) * orientation >= -1e-5f &&
                    crossXZ(polygon[current], polygon[next], polygon[candidate]) * orientation >= -1e-5f &&
                    crossXZ(polygon[next], polygon[previous], polygon[candidate]) * orientation >= -1e-5f;
                if (inclusive || isPointInTriangleXZ(polygon[candidate], polygon[previous], polygon[current],
                                                     polygon[next], orientation)) {
                    contains = true;
                    break;
                }
            }
            if (contains) continue;
            triangles.push_back({previous, current, next});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) return TriangulationStatus::Degenerate;
    }
    if (remaining.size() == 3) triangles.push_back({remaining[0], remaining[1], remaining[2]});
    return triangles.size() + 2 == polygon.size() ? TriangulationStatus::Complete : TriangulationStatus::Degenerate;
}

float laneCenterOffset(const RoadStyle& style, int lanesForward, int lanesBackward, int laneIndex,
                       RoadLaneDirection direction) {
    const float asphaltHalf = 0.5f * style.laneWidth * static_cast<float>(lanesForward + lanesBackward);
    if (lanesBackward <= 0 || direction == RoadLaneDirection::Backward)
        return -asphaltHalf + style.laneWidth * (static_cast<float>(laneIndex) + 0.5f);
    const float opposingBoundary = -asphaltHalf + style.laneWidth * static_cast<float>(lanesBackward);
    return opposingBoundary + style.laneWidth * (static_cast<float>(laneIndex) + 0.5f);
}

void appendBox(MeshBuild& mesh, V3 center, V3 side, V3 up, V3 forward, float hx, float hy, float hz,
               RoadMaterial material) {
    mesh.setActiveGroup(roadMaterialGroup(material));
    const V3 corners[8] = {
        center + side * -hx + up * -hy + forward * -hz, center + side * hx + up * -hy + forward * -hz,
        center + side * hx + up * hy + forward * -hz,   center + side * -hx + up * hy + forward * -hz,
        center + side * -hx + up * -hy + forward * hz,  center + side * hx + up * -hy + forward * hz,
        center + side * hx + up * hy + forward * hz,    center + side * -hx + up * hy + forward * hz,
    };
    const int faces[6][4] = {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1}, {3, 2, 6, 7}, {0, 3, 7, 4}, {1, 5, 6, 2}};
    const V3 normals[6]   = {forward * -1.f, forward, up * -1.f, up, side * -1.f, side};
    for (int f = 0; f < 6; ++f) {
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        for (int c = 0; c < 4; ++c) {
            const V3& p = corners[faces[f][c]];
            mesh.addVertex(p.x, p.y, p.z, normals[f].x, normals[f].y, normals[f].z, static_cast<float>(c & 1),
                           static_cast<float>((c >> 1) & 1));
        }
        mesh.addTriangle(base, base + 1, base + 2);
        mesh.addTriangle(base, base + 2, base + 3);
    }
}

void appendStripQuad(MeshBuild& mesh, V3 a, V3 b, V3 c, V3 d, V3 normal, float u0, float u1, float v0, float v1,
                     RoadMaterial material, QuadDiagonal diagonal) {
    mesh.setActiveGroup(roadMaterialGroup(material));
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    mesh.addVertex(a.x, a.y, a.z, normal.x, normal.y, normal.z, u0, v0);
    mesh.addVertex(b.x, b.y, b.z, normal.x, normal.y, normal.z, u1, v0);
    mesh.addVertex(c.x, c.y, c.z, normal.x, normal.y, normal.z, u1, v1);
    mesh.addVertex(d.x, d.y, d.z, normal.x, normal.y, normal.z, u0, v1);
    const bool across           = diagonal == QuadDiagonal::SecondToFourth;
    const V3   firstAreaNormal  = across ? cross(b - a, d - a) : cross(b - a, c - a);
    const V3   secondAreaNormal = across ? cross(c - b, d - b) : cross(c - a, d - a);
    if (dot(firstAreaNormal, firstAreaNormal) > 1e-12f) mesh.addTriangle(base, base + 1, base + (across ? 3 : 2));
    if (dot(secondAreaNormal, secondAreaNormal) > 1e-12f) mesh.addTriangle(base + (across ? 1 : 0), base + 2, base + 3);
}

/** @brief Emit a quad whose winding matches @p normal (mesh3D expects object-space CCW). */
void appendOrientedQuad(MeshBuild& mesh, V3 a, V3 b, V3 c, V3 d, V3 normal, float u0, float u1, float v0, float v1,
                        RoadMaterial material, QuadDiagonal diagonal) {
    const V3 n = normalize(normal);
    // Use both constituent triangles so a deliberately collapsed edge still
    // gets the winding of its non-degenerate half.
    const V3 geometricNormal = cross(b - a, c - a) + cross(c - a, d - a);
    if (dot(geometricNormal, geometricNormal) <= 1e-12f) return;
    if (dot(geometricNormal, n) < 0.f) {
        appendStripQuad(mesh, a, d, c, b, n, u0, u1, v1, v0, material, diagonal);
    } else {
        appendStripQuad(mesh, a, b, c, d, n, u0, u1, v0, v1, material, diagonal);
    }
}

void appendOrientedTri(MeshBuild& mesh, V3 a, V3 b, V3 c, V3 normal, float uvMeters, RoadMaterial material) {
    const V3 n = normalize(normal);
    const V3 areaNormal = cross(b - a, c - a);
    if (dot(areaNormal, areaNormal) <= 1e-12f) return;
    const float uvScale = 1.f / std::max(uvMeters, 0.1f);
    mesh.setActiveGroup(roadMaterialGroup(material));
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    mesh.addVertex(a.x, a.y, a.z, n.x, n.y, n.z, a.x * uvScale, a.z * uvScale);
    mesh.addVertex(b.x, b.y, b.z, n.x, n.y, n.z, b.x * uvScale, b.z * uvScale);
    mesh.addVertex(c.x, c.y, c.z, n.x, n.y, n.z, c.x * uvScale, c.z * uvScale);
    if (dot(areaNormal, n) < 0.f)
        mesh.addTriangle(base, base + 2, base + 1);
    else
        mesh.addTriangle(base, base + 1, base + 2);
}

Result<void> addDeckAndPiers(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames, const RoadStyle& style,
                             float halfWidth, float asphaltHalf, bool includePiers, std::size_t budget) {
    if (frames.size() < 2) return Result<void>::success();
    double travel = 0.;
    for (std::size_t i = 1; i < frames.size(); ++i)
        travel += length(V3{frames[i].sample.x - frames[i - 1].sample.x, frames[i].sample.y - frames[i - 1].sample.y,
                            frames[i].sample.z - frames[i - 1].sample.z});
    const double piers = includePiers && style.pierSpacing > 1e-3f ? std::ceil(travel / style.pierSpacing) : 0.;
    const auto used = static_cast<std::size_t>(mesh.getVertexCount()) + static_cast<std::size_t>(mesh.getIndexCount());
    if (!std::isfinite(piers) || piers > static_cast<double>(budget / 60u))
        return bakeFail<void>(DiagnosticCode::PreconditionViolation, "bridge piers exceed the mesh/work budget",
                              "mesh");
    const std::size_t needed = (frames.size() - 1) * 10u + static_cast<std::size_t>(piers) * 60u;
    if (used > budget || needed > budget - used)
        return bakeFail<void>(DiagnosticCode::PreconditionViolation, "bridge deck exceeds mesh budget", "mesh");
    // Deck underside strip.
    for (int ring = 0; ring < static_cast<int>(frames.size()) - 1; ++ring) {
        const auto& f0 = frames[static_cast<std::size_t>(ring)];
        const auto& f1 = frames[static_cast<std::size_t>(ring + 1)];
        const V3    o0{f0.sample.x, f0.sample.y, f0.sample.z};
        const V3    o1{f1.sample.x, f1.sample.y, f1.sample.z};
        const V3    s0{f0.sideX, f0.sideY, f0.sideZ};
        const V3    s1{f1.sideX, f1.sideY, f1.sideZ};
        const V3    u0{f0.upX, f0.upY, f0.upZ};
        const V3    u1{f1.upX, f1.upY, f1.upZ};
        const V3 a = o0 + s0 * -scaledProfileSide(halfWidth, asphaltHalf, f0.sample.scaleX) + u0 * -style.deckThickness;
        const V3 b = o0 + s0 * scaledProfileSide(halfWidth, asphaltHalf, f0.sample.scaleX) + u0 * -style.deckThickness;
        const V3 c = o1 + s1 * scaledProfileSide(halfWidth, asphaltHalf, f1.sample.scaleX) + u1 * -style.deckThickness;
        const V3 d = o1 + s1 * -scaledProfileSide(halfWidth, asphaltHalf, f1.sample.scaleX) + u1 * -style.deckThickness;
        appendStripQuad(mesh, a, b, c, d, normalize((u0 + u1) * -0.5f), 0.f, 1.f, 0.f, 1.f, RoadMaterial::Deck);
    }

    if (!includePiers || style.pierSpacing <= 1e-3f) return Result<void>::success();

    float traveled = 0.f;
    float nextPier = style.pierSpacing * 0.5f;
    for (std::size_t i = 1; i < frames.size(); ++i) {
        const auto& a = frames[i - 1];
        const auto& b = frames[i];
        const V3    pa{a.sample.x, a.sample.y, a.sample.z};
        const V3    pb{b.sample.x, b.sample.y, b.sample.z};
        const float seg = length(pb - pa);
        if (seg <= 1e-5f) continue;
        while (nextPier <= traveled + seg + 1e-4f) {
            const float t = std::clamp((nextPier - traveled) / seg, 0.f, 1.f);
            const V3    pos = pa + (pb - pa) * t;
            if (pos.y > style.pierClearance) {
                // World-up pier: never reuse Frenet side (can flip / tilt and skew the box).
                V3 fwdFlat{pb.x - pa.x, 0.f, pb.z - pa.z};
                const float fwdLen = length(fwdFlat);
                fwdFlat = fwdLen > 1e-5f ? fwdFlat * (1.f / fwdLen) : V3{1.f, 0.f, 0.f};
                const V3 up{0.f, 1.f, 0.f};
                const V3 side = normalize(cross(up, fwdFlat));
                const float pierH = pos.y - style.deckThickness;
                if (pierH > 0.2f) {
                    const V3 center{pos.x, pierH * 0.5f, pos.z};
                    appendBox(mesh, center, side, up, fwdFlat, style.pierWidth * 0.5f, pierH * 0.5f,
                              style.pierDepth * 0.5f, RoadMaterial::Pier);
                }
            }
            nextPier += style.pierSpacing;
            if (nextPier > 1.0e7f) break;
        }
        traveled += seg;
    }
    return Result<void>::success();
}

Result<void> addLaneMarkings(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames, const RoadEdge& edge,
                             float pathLength, const RoadBakeOptions& options) {
    if (frames.size() < 2) return Result<void>::success();
    const auto& style       = edge.style;
    const float asphaltHalf = 0.5f * style.laneWidth * static_cast<float>(edge.lanesForward + edge.lanesBackward);
    auto        paintLine   = [&](float lateral, bool dashed, RoadMaterial material) -> Result<void> {
        if (dashed && style.dashLength <= 1e-5f) return Result<void>::success();
        const bool continuous = !dashed || style.dashGap <= 1e-5f;
        // Retain the authored phase at the trim, then measure the emitted polyline.
        double traveled = static_cast<double>(frames.front().sample.normalizedDistance) * pathLength;
        for (std::size_t i = 1; i < frames.size(); ++i) {
            const auto& a = frames[i - 1];
            const auto& b = frames[i];
            const V3    pa{a.sample.x, a.sample.y, a.sample.z};
            const V3    pb{b.sample.x, b.sample.y, b.sample.z};
            const V3    sa{a.sideX, a.sideY, a.sideZ};
            const V3    sb{b.sideX, b.sideY, b.sideZ};
            const V3    ua{a.upX, a.upY, a.upZ};
            const V3    ub{b.upX, b.upY, b.upZ};
            const float seg = length(pb - pa);
            if (seg <= 1e-5f) continue;
            const double startDistance = traveled;
            const double distanceSpan  = seg;
            traveled += distanceSpan;
            auto emitSpan = [&](float t0, float t1) -> Result<void> {
                constexpr std::size_t quadElements    = 10u;
                const std::size_t currentElements = static_cast<std::size_t>(mesh.getVertexCount()) +
                                                    static_cast<std::size_t>(mesh.getIndexCount());
                if (currentElements > options.maximumMeshElements ||
                    options.maximumMeshElements - currentElements < quadElements)
                    return bakeFail<void>(DiagnosticCode::PreconditionViolation,
                                          "road markings exceed the mesh element budget", "mesh");
                const float hw = style.markingWidth * 0.5f;
                const V3    p0 = pa + (pb - pa) * t0;
                const V3    p1 = pa + (pb - pa) * t1;
                if (length(p1 - p0) <= 1e-5f) return Result<void>::success();
                const V3    s0 = normalize(sa + (sb - sa) * t0);
                const V3    s1 = normalize(sa + (sb - sa) * t1);
                const V3    u0 = normalize(ua + (ub - ua) * t0);
                const V3    u1 = normalize(ua + (ub - ua) * t1);
                const float lateral0 = lateral * std::lerp(a.sample.scaleX, b.sample.scaleX, t0);
                const float lateral1 = lateral * std::lerp(a.sample.scaleX, b.sample.scaleX, t1);
                const V3    a0 =
                    p0 + s0 * (lateral0 - hw) +
                    u0 * (roadSurfaceHeight(style, lateral - hw / std::lerp(a.sample.scaleX, b.sample.scaleX, t0)) +
                          0.035f);
                const V3 a1 =
                    p0 + s0 * (lateral0 + hw) +
                    u0 * (roadSurfaceHeight(style, lateral + hw / std::lerp(a.sample.scaleX, b.sample.scaleX, t0)) +
                          0.035f);
                const V3 b1 =
                    p1 + s1 * (lateral1 + hw) +
                    u1 * (roadSurfaceHeight(style, lateral + hw / std::lerp(a.sample.scaleX, b.sample.scaleX, t1)) +
                          0.035f);
                const V3 b0 =
                    p1 + s1 * (lateral1 - hw) +
                    u1 * (roadSurfaceHeight(style, lateral - hw / std::lerp(a.sample.scaleX, b.sample.scaleX, t1)) +
                          0.035f);
                appendStripQuad(mesh, a0, b0, b1, a1, normalize(u0 + u1), 0.f, 1.f, 0.f, 1.f, material);
                return Result<void>::success();
            };
            if (continuous) {
                auto emitted = emitSpan(0.f, 1.f);
                if (!emitted.ok()) return emitted;
                continue;
            }
            const double cycle = static_cast<double>(style.dashLength) + static_cast<double>(style.dashGap);
            double       local = 0.0;
            while (local < distanceSpan - 1e-7) {
                const double phase    = std::fmod(startDistance + local, cycle);
                const bool   draw     = phase < static_cast<double>(style.dashLength);
                const double boundary = draw ? static_cast<double>(style.dashLength) - phase : cycle - phase;
                const double next = std::min(distanceSpan, local + boundary);
                if (!(next > local))
                    return bakeFail<void>(DiagnosticCode::PreconditionViolation,
                                          "road marking period is too small to advance", "style.dashLength");
                if (draw) {
                    auto emitted = emitSpan(static_cast<float>(local / distanceSpan),
                                            static_cast<float>(next / distanceSpan));
                    if (!emitted.ok()) return emitted;
                }
                local = next;
            }
        }
        return Result<void>::success();
    };

    auto leftEdge = paintLine(-asphaltHalf + style.markingWidth, false, RoadMaterial::Marking);
    if (!leftEdge.ok()) return leftEdge;
    auto rightEdge = paintLine(asphaltHalf - style.markingWidth, false, RoadMaterial::Marking);
    if (!rightEdge.ok()) return rightEdge;
    const float divider = -asphaltHalf + style.laneWidth * static_cast<float>(edge.lanesBackward);
    for (int lane = 1; lane < edge.lanesForward; ++lane) {
        const float lateral = (edge.lanesBackward > 0 ? divider : -asphaltHalf) +
                              style.laneWidth * static_cast<float>(lane);
        auto        line    = paintLine(lateral, true, RoadMaterial::Marking);
        if (!line.ok()) return line;
    }
    for (int lane = 1; lane < edge.lanesBackward; ++lane) {
        const float lateral = -asphaltHalf + style.laneWidth * static_cast<float>(lane);
        auto        line    = paintLine(lateral, true, RoadMaterial::Marking);
        if (!line.ok()) return line;
    }
    if (edge.lanesForward > 0 && edge.lanesBackward > 0) {
        const float offset  = std::max(0.08f, style.markingWidth);
        auto        first   = paintLine(divider - offset, false, RoadMaterial::MarkingYellow);
        if (!first.ok()) return first;
        auto second = paintLine(divider + offset, false, RoadMaterial::MarkingYellow);
        if (!second.ok()) return second;
    }
    return Result<void>::success();
}

void appendArrow(MeshBuild& mesh, V3 pos, V3 forward, V3 up, float size) {
    const V3 side = normalize(cross(up, forward));
    const V3 tip  = pos + forward * size + up * 0.05f;
    const V3 left = pos - forward * size * 0.35f + side * (size * 0.5f) + up * 0.05f;
    const V3 right = pos - forward * size * 0.35f - side * (size * 0.5f) + up * 0.05f;
    mesh.setActiveGroup(roadMaterialGroup(RoadMaterial::Nav));
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    mesh.addVertex(tip.x, tip.y, tip.z, up.x, up.y, up.z, 0.5f, 1.f);
    mesh.addVertex(left.x, left.y, left.z, up.x, up.y, up.z, 0.f, 0.f);
    mesh.addVertex(right.x, right.y, right.z, up.x, up.y, up.z, 1.f, 0.f);
    mesh.addTriangle(base, base + 1, base + 2);
    // Back face so arrows stay visible under top-down interchange cameras.
    const auto back = static_cast<std::uint32_t>(mesh.getVertexCount());
    const V3   nd   = up * -1.f;
    mesh.addVertex(tip.x, tip.y, tip.z, nd.x, nd.y, nd.z, 0.5f, 1.f);
    mesh.addVertex(right.x, right.y, right.z, nd.x, nd.y, nd.z, 1.f, 0.f);
    mesh.addVertex(left.x, left.y, left.z, nd.x, nd.y, nd.z, 0.f, 0.f);
    mesh.addTriangle(back, back + 1, back + 2);
}


}  // namespace detail

namespace detail {

Result<SplinePath> edgeSplineForBake(const RoadEdge& edge) { return edgeToSpline(edge); }

float junctionSocketDistanceForBake(const RoadNetwork& network, const RoadNode& node, const RoadEdge& edge,
                                    float pathLength) {
    return junctionSocketDistance(network, node, edge, pathLength);
}

}  // namespace detail

namespace {

struct MaterialColor {
    float r, g, b, a;
};

MaterialColor colorForGroup(const std::string& name) {
    if (name == "asphalt" || name == "asphalt.bridge" || name == "asphalt.tunnel" || name == "asphalt.tunnel.portal")
        return {0.28f, 0.28f, 0.30f, 1.f};
    if (name == "curb") return {0.78f, 0.78f, 0.76f, 1.f};
    if (name == "sidewalk") return {0.86f, 0.86f, 0.84f, 1.f};
    if (name == "deck") return {0.48f, 0.48f, 0.46f, 1.f};
    if (name == "pier") return {0.70f, 0.68f, 0.64f, 1.f};
    if (name == "marking") return {0.96f, 0.96f, 0.94f, 1.f};
    if (name == "markingYellow") return {0.96f, 0.80f, 0.10f, 1.f};
    if (name == "nav") return {0.10f, 0.95f, 1.f, 1.f};
    return {0.55f, 0.55f, 0.55f, 1.f};
}

Result<void> paintGroupVertexColors(MeshBuild& mesh) {
    const int verts = mesh.getVertexCount();
    if (verts <= 0) return Result<void>::success();
    std::vector<float> colors(static_cast<std::size_t>(verts) * 4u, 1.f);
    const int triCount = mesh.getIndexCount() / 3;
    for (int t = 0; t < triCount; ++t) {
        const int group = mesh.getTriangleGroup(t);
        const MaterialColor c =
            group >= 0 ? colorForGroup(mesh.getGroupName(group)) : MaterialColor{0.5f, 0.5f, 0.5f, 1.f};
        for (int k = 0; k < 3; ++k) {
            const int vi = mesh.getIndex(t * 3 + k);
            if (vi < 0 || vi >= verts) continue;
            const auto base = static_cast<std::size_t>(vi) * 4u;
            colors[base + 0] = c.r;
            colors[base + 1] = c.g;
            colors[base + 2] = c.b;
            colors[base + 3] = c.a;
        }
    }
    return mesh.setVertexColors(std::move(colors));
}

}  // namespace

Result<RoadBakeResult> bakeRoadNetwork(const RoadNetwork& network, const RoadBakeOptions& options) {
    if (options.maximumSamplingEvaluations == 0 || options.maximumSamplingEvaluations > 4u * 1024u * 1024u)
        return bakeFail<RoadBakeResult>(DiagnosticCode::InvalidArgument, "sampling work budget must be in [1,4194304]",
                                        "options.maximumSamplingEvaluations");
    if (!std::isfinite(options.samplingScale) || options.samplingScale < 1.f || options.samplingScale > 64.f)
        return bakeFail<RoadBakeResult>(DiagnosticCode::InvalidArgument, "samplingScale must be in [1,64]",
                                        "options.samplingScale");
    auto topology = network.validate();
    if (!topology.ok()) return Result<RoadBakeResult>::failure(topology.status());
    constexpr int maximumSamples = 4096;
    if (options.pathSegmentsPerEdge < 2 || options.turnSamples < 2 ||
        options.pathSegmentsPerEdge > maximumSamples || options.turnSamples > maximumSamples)
        return bakeFail<RoadBakeResult>(DiagnosticCode::InvalidArgument,
                                        "road sample counts must be in [2,4096]", "options");
    if (options.includeNavigation &&
        (!std::isfinite(options.navRibbonHalfWidth) || options.navRibbonHalfWidth <= 0.f ||
         !std::isfinite(options.arrowSpacing) || options.arrowSpacing <= 0.f))
        return bakeFail<RoadBakeResult>(DiagnosticCode::InvalidArgument,
                                        "road navigation dimensions must be finite and positive", "options");
    if (!std::isfinite(options.junctionChordError) || options.junctionChordError <= 0.f ||
        options.junctionChordError > 1.f)
        return bakeFail<RoadBakeResult>(DiagnosticCode::InvalidArgument,
                                        "road junction chord error must be finite and in (0,1] metres", "options");
    if (options.maximumMeshElements == 0u)
        return bakeFail<RoadBakeResult>(DiagnosticCode::InvalidArgument,
                                        "road mesh element budget must be positive", "options");
    if (network.edgeCount() == 0)
        return bakeFail<RoadBakeResult>(DiagnosticCode::PreconditionViolation, "road network has no edges");

    RoadBakeResult result;
    std::unordered_set<std::uint32_t> activeJunctionNodes;
    std::unordered_set<std::uint32_t> terminalNodes;
    std::unordered_map<std::uint32_t, std::vector<const RoadEdge*>> incidentEdges;
    incidentEdges.reserve(network.nodeCount());
    for (const auto& edge : network.edges()) {
        incidentEdges[edge.from].push_back(&edge);
        incidentEdges[edge.to].push_back(&edge);
    }
    for (const auto& node : network.nodes()) {
        const auto found = incidentEdges.find(node.id);
        if (found != incidentEdges.end() && found->second.size() == 1) terminalNodes.insert(node.id);
    }
    if (options.includeJunctions) {
        for (const auto& node : network.nodes()) {
            const auto found = incidentEdges.find(node.id);
            if (found == incidentEdges.end() || (found->second.size() < 2 && node.connection.ports.empty())) continue;
            activeJunctionNodes.insert(node.id);
            auto solvable = validateJunctionGeometry(network, node);
            if (!solvable.ok()) return Result<RoadBakeResult>::failure(solvable.status());
        }
    }
    for (const auto& edge : network.edges()) {
        auto spline = edgeToSpline(edge);
        if (!spline.ok()) return Result<RoadBakeResult>::failure(spline.status());
        auto pathLength = spline.value().lengthResult(16);
        if (!pathLength.ok()) return Result<RoadBakeResult>::failure(pathLength.status());
        const auto from = network.nodeResult(edge.from);
        const auto to   = network.nodeResult(edge.to);
        if (!from.ok() || !to.ok())
            return bakeFail<RoadBakeResult>(DiagnosticCode::InvariantViolation,
                                            "edge endpoint disappeared during junction preflight", "edge");
        const float start = activeJunctionNodes.contains(edge.from)
                                ? junctionSocketDistance(network, from.value(), edge, pathLength.value())
                                : 0.f;
        const float end = activeJunctionNodes.contains(edge.to)
                              ? junctionSocketDistance(network, to.value(), edge, pathLength.value())
                              : 0.f;
        if ((!from.value().connection.ports.empty() || !to.value().connection.ports.empty()) &&
            start + end >= pathLength.value() - .01f)
            return bakeFail<RoadBakeResult>(DiagnosticCode::PreconditionViolation,
                                            "custom sockets consume the complete road edge", "edge");
        const int laneCount = edge.lanesForward + edge.lanesBackward;
        if (laneCount >= 6 && pathLength.value() - start - end <= 1.001f)
            return bakeFail<RoadBakeResult>(DiagnosticCode::PreconditionViolation,
                                            "junction socket trims consume the complete road edge", "edge");
    }
    RoadFrameCache                      frameCache;
    std::vector<RoadGeometrySourceSpan> geometrySources;
    auto                                recordSource = [&](RoadGeometrySourceKind kind, std::uint32_t id, int first) {
        const int end = result.mesh.getIndexCount() / 3;
        if (end > first)
            geometrySources.push_back(
                {kind, id, static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(end - first)});
    };
    for (const auto& edge : network.edges()) {
        const int first = result.mesh.getIndexCount() / 3;
        auto      baked = bakeEdgeGeometry(result.mesh, result.overlay, network, edge, options, activeJunctionNodes,
                                           terminalNodes, frameCache);
        if (!baked.ok()) return Result<RoadBakeResult>::failure(baked.status());
        recordSource(RoadGeometrySourceKind::Edge, edge.id, first);
        if (isMeshBudgetExceeded(result.mesh, options))
            return bakeFail<RoadBakeResult>(DiagnosticCode::PreconditionViolation,
                                            "road edges exceed the mesh element budget", "mesh");
    }
    if (options.includeJunctions) {
        for (const auto& node : network.nodes()) {
            if (!activeJunctionNodes.contains(node.id)) continue;
            const int first    = result.mesh.getIndexCount() / 3;
            auto junction = bakeJunction(result.mesh, network, node, options);
            if (!junction.ok()) return Result<RoadBakeResult>::failure(junction.status());
            recordSource(RoadGeometrySourceKind::Junction, node.id, first);
            if (isMeshBudgetExceeded(result.mesh, options))
                return bakeFail<RoadBakeResult>(DiagnosticCode::PreconditionViolation,
                                                "road junctions exceed the mesh element budget", "mesh");
        }
    }
    const int derivedFirst = result.mesh.getIndexCount() / 3;
    auto      continued    = bakeFacilityContinuations(result.mesh, network, options, activeJunctionNodes);
    if (!continued.ok()) return Result<RoadBakeResult>::failure(continued.status());
    if (options.includeNavigation) {
        auto turns = bakeTurnOverlays(result.mesh, result.overlay, network, options, activeJunctionNodes);
        if (!turns.ok()) return Result<RoadBakeResult>::failure(turns.status());
    }
    if (options.includePlacements) {
        auto placements = bakePlacementPoints(result.placements, network, options, activeJunctionNodes, frameCache);
        if (!placements.ok()) return Result<RoadBakeResult>::failure(placements.status());
    }

    auto painted = paintGroupVertexColors(result.mesh);
    if (!painted.ok()) return Result<RoadBakeResult>::failure(painted.status());
    recordSource(RoadGeometrySourceKind::NetworkDerived, 0, derivedFirst);
    auto sealed = sealRoadGeometrySources(result.mesh, geometrySources);
    if (!sealed.ok()) return Result<RoadBakeResult>::failure(sealed.status());

    result.mesh.setMeta("generator", "procgen.road");
    result.mesh.setMeta("schema", "eve.procgen.roadNetwork");
    result.mesh.setMeta("schemaVersion", "1");
    result.mesh.setMeta("edges", std::to_string(network.edgeCount()));
    result.mesh.setMeta("nodes", std::to_string(network.nodeCount()));
    result.mesh.setMeta("laneLinks", std::to_string(network.laneLinkCount()));
    if (result.mesh.getVertexCount() < 3)
        return bakeFail<RoadBakeResult>(DiagnosticCode::InvariantViolation, "bake produced an empty mesh");
    return Result<RoadBakeResult>::success(std::move(result));
}

}  // namespace eve::procgen::road
