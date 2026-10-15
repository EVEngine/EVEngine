#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadBakeInternal.h"

#include "common/Diagnostic.h"
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
namespace {

struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

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

template <class T>
Result<T> bakeFail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.road"));
}

bool exceedsMeshBudget(const MeshBuild& mesh, const RoadBakeOptions& options) {
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
        sp.x = p.x;
        sp.y = p.y;
        sp.z = p.z;
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
    return 0.5f * edge.style.laneWidth * static_cast<float>(edge.lanesForward + edge.lanesBackward);
}

/** Compute a finite socket distance whose two mouth corners cannot cross an adjacent arm. */
float junctionSocketDistance(const RoadNetwork& network, const RoadNode& node, const RoadEdge& edge, float pathLength) {
    float       required  = node.junctionRadius;
    const V3    direction = authoredOutwardDirection(node, edge);
    const float halfWidth = asphaltHalfWidth(edge);

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

struct JunctionPlane {
    V3    origin;
    float gradeX = 0.f;
    float gradeZ = 0.f;
    V3    up{0.f, 1.f, 0.f};
};

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
        auto pathLength = spline.value().lengthResult(16);
        if (!pathLength.ok()) return Result<void>::failure(pathLength.status());
        const float trim = junctionSocketDistance(network, node, *edge, pathLength.value());
        const float distance = edge->from == node.id ? trim : pathLength.value() - trim;
        auto frame = spline.value().travelFrameResult(distance, "clamp", 16);
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

bool pointInTriangleXZ(V3 p, V3 a, V3 b, V3 c, float orientation) {
    constexpr float epsilon = 1e-5f;
    // Boundary points on a sampled curve must not veto the ear; only a point
    // strictly inside the candidate triangle makes that diagonal invalid.
    return crossXZ(a, b, p) * orientation > epsilon && crossXZ(b, c, p) * orientation > epsilon &&
           crossXZ(c, a, p) * orientation > epsilon;
}

/** Ear clipping keeps every sampled curb-return segment as a hard polygon boundary. */
bool triangulateBoundary(const std::vector<V3>& polygon, std::vector<std::array<std::size_t, 3>>& triangles) {
    if (polygon.size() < 3) return false;
    const float area = signedAreaXZ(polygon);
    if (std::fabs(area) < 1e-5f) return false;
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
                if (pointInTriangleXZ(polygon[candidate], polygon[previous], polygon[current], polygon[next],
                                      orientation)) {
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
        if (!clipped) return false;
    }
    if (remaining.size() == 3) triangles.push_back({remaining[0], remaining[1], remaining[2]});
    return triangles.size() + 2 == polygon.size();
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
                     RoadMaterial material) {
    mesh.setActiveGroup(roadMaterialGroup(material));
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    mesh.addVertex(a.x, a.y, a.z, normal.x, normal.y, normal.z, u0, v0);
    mesh.addVertex(b.x, b.y, b.z, normal.x, normal.y, normal.z, u1, v0);
    mesh.addVertex(c.x, c.y, c.z, normal.x, normal.y, normal.z, u1, v1);
    mesh.addVertex(d.x, d.y, d.z, normal.x, normal.y, normal.z, u0, v1);
    const V3 firstAreaNormal  = cross(b - a, c - a);
    const V3 secondAreaNormal = cross(c - a, d - a);
    if (dot(firstAreaNormal, firstAreaNormal) > 1e-12f) mesh.addTriangle(base, base + 1, base + 2);
    if (dot(secondAreaNormal, secondAreaNormal) > 1e-12f) mesh.addTriangle(base, base + 2, base + 3);
}

/** @brief Emit a quad whose winding matches @p normal (mesh3D expects object-space CCW). */
void appendOrientedQuad(MeshBuild& mesh, V3 a, V3 b, V3 c, V3 d, V3 normal, float u0, float u1, float v0, float v1,
                        RoadMaterial material) {
    const V3 n = normalize(normal);
    // Use both constituent triangles so a deliberately collapsed edge still
    // gets the winding of its non-degenerate half.
    const V3 geometricNormal = cross(b - a, c - a) + cross(c - a, d - a);
    if (dot(geometricNormal, geometricNormal) <= 1e-12f) return;
    if (dot(geometricNormal, n) < 0.f) {
        appendStripQuad(mesh, a, d, c, b, n, u0, u1, v1, v0, material);
    } else {
        appendStripQuad(mesh, a, b, c, d, n, u0, u1, v0, v1, material);
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

Result<void> loftProfileClean(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames, const RoadProfile& profile,
                              float pathLength, float uvMeters) {
    if (frames.size() < 2 || profile.points.size() < 2)
        return bakeFail<void>(DiagnosticCode::InvalidArgument, "loft needs >=2 frames and profile points");

    const int ringCount    = static_cast<int>(frames.size());
    const int profileCount = static_cast<int>(profile.points.size());
    std::vector<V3> ringPositions(static_cast<std::size_t>(ringCount * profileCount));

    for (int ring = 0; ring < ringCount; ++ring) {
        const auto& frame = frames[static_cast<std::size_t>(ring)];
        const V3    origin{frame.sample.x, frame.sample.y, frame.sample.z};
        const V3    side{frame.sideX, frame.sideY, frame.sideZ};
        const V3    up{frame.upX, frame.upY, frame.upZ};
        for (int i = 0; i < profileCount; ++i) {
            const auto& pp = profile.points[static_cast<std::size_t>(i)];
            ringPositions[static_cast<std::size_t>(ring * profileCount + i)] = origin + side * pp.side + up * pp.up;
        }
    }

    for (int ring = 0; ring < ringCount - 1; ++ring) {
        // Spline samples retain their distance along the untrimmed edge. Using
        // that distance keeps texture phase stable when junction radii or bake
        // tessellation change; uvMeters is metres per repeat, not repeat count.
        const float u0 = frames[static_cast<std::size_t>(ring)].sample.normalizedDistance * pathLength / uvMeters;
        const float u1 = frames[static_cast<std::size_t>(ring + 1)].sample.normalizedDistance * pathLength / uvMeters;
        for (int i = 0; i < profileCount - 1; ++i) {
            const auto& a = profile.points[static_cast<std::size_t>(i)];
            const auto& b = profile.points[static_cast<std::size_t>(i + 1)];
            // Prefer asphalt for near-horizontal driving strips (endpoint material alone
            // would tag the asphalt span as curb because the left corner is a curb drop).
            RoadMaterial mat = a.material;
            if (std::fabs(a.up - b.up) <= 1e-3f &&
                (a.material == RoadMaterial::Asphalt || b.material == RoadMaterial::Asphalt)) {
                mat = RoadMaterial::Asphalt;
            }
            const V3&   p00 = ringPositions[static_cast<std::size_t>(ring * profileCount + i)];
            const V3&   p01 = ringPositions[static_cast<std::size_t>(ring * profileCount + i + 1)];
            const V3&   p10 = ringPositions[static_cast<std::size_t>((ring + 1) * profileCount + i)];
            const V3&   p11 = ringPositions[static_cast<std::size_t>((ring + 1) * profileCount + i + 1)];
            const V3    normal = normalize(cross(p10 - p00, p01 - p00));
            const float v0     = a.side / uvMeters;
            const float v1     = b.side / uvMeters;
            appendStripQuad(mesh, p00, p10, p11, p01, normal, u0, u1, v0, v1, mat);
        }
    }
    return Result<void>::success();
}

/** @brief Cap a lofted profile ring so junction-facing open U-channels are not see-through. */
void capProfileRing(MeshBuild& mesh, const SplineFrameSample& frame, const RoadProfile& profile, bool outward) {
    if (profile.points.size() < 3) return;
    const V3 origin{frame.sample.x, frame.sample.y, frame.sample.z};
    const V3 side{frame.sideX, frame.sideY, frame.sideZ};
    const V3 up{frame.upX, frame.upY, frame.upZ};
    V3       fwd{frame.forwardX, frame.forwardY, frame.forwardZ};
    if (!outward) fwd = fwd * -1.f;
    const V3 nrm = normalize(fwd);
    std::vector<V3> ring;
    ring.reserve(profile.points.size());
    for (const auto& pp : profile.points) ring.push_back(origin + side * pp.side + up * pp.up);
    // Fan-fill the end polygon (closes the jersey-barrier U looking into the hub).
    mesh.setActiveGroup(roadMaterialGroup(RoadMaterial::Curb));
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    for (const V3& p : ring) mesh.addVertex(p.x, p.y, p.z, nrm.x, nrm.y, nrm.z, 0.f, 0.f);
    for (std::size_t i = 1; i + 1 < ring.size(); ++i) {
        const V3 areaNormal = cross(ring[i] - ring[0], ring[i + 1] - ring[0]);
        if (dot(areaNormal, areaNormal) <= 1e-12f) continue;
        if (outward)
            mesh.addTriangle(base, base + static_cast<std::uint32_t>(i), base + static_cast<std::uint32_t>(i + 1));
        else
            mesh.addTriangle(base, base + static_cast<std::uint32_t>(i + 1), base + static_cast<std::uint32_t>(i));
    }
}

/**
 * @brief Close only curb/sidewalk shoulders at a tip — leave the asphalt U open.
 *
 * Full `capProfileRing` draws a dark tip bar across the asphalt that reads as a
 * salmon seam from above on large junction trims. Shoulder-only caps still stop
 * see-through holes at the outer gray edge.
 */
void capProfileShoulders(MeshBuild& mesh, const SplineFrameSample& frame, const RoadProfile& profile, bool outward) {
    if (profile.points.size() < 6) return;
    const V3 origin{frame.sample.x, frame.sample.y, frame.sample.z};
    const V3 side{frame.sideX, frame.sideY, frame.sideZ};
    const V3 up{frame.upX, frame.upY, frame.upZ};
    V3       fwd{frame.forwardX, frame.forwardY, frame.forwardZ};
    if (!outward) fwd = fwd * -1.f;
    const V3 nrm = normalize(fwd);

    // First Asphalt point marks the deck span start; shoulders are everything else.
    std::size_t asphaltBegin = profile.points.size();
    for (std::size_t i = 0; i < profile.points.size(); ++i) {
        if (profile.points[i].material == RoadMaterial::Asphalt) {
            asphaltBegin = i;
            break;
        }
    }
    if (asphaltBegin == 0 || asphaltBegin >= profile.points.size()) return;

    auto emitFan = [&](std::size_t begin, std::size_t end, RoadMaterial mat) {
        if (end < begin + 3) return;
        mesh.setActiveGroup(roadMaterialGroup(mat));
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        for (std::size_t i = begin; i < end; ++i) {
            const auto& pp = profile.points[i];
            const V3    p  = origin + side * pp.side + up * pp.up;
            mesh.addVertex(p.x, p.y, p.z, nrm.x, nrm.y, nrm.z, 0.f, 0.f);
        }
        const auto count = static_cast<std::uint32_t>(end - begin);
        for (std::uint32_t i = 1; i + 1 < count; ++i) {
            const auto& originPoint = profile.points[begin];
            const auto& pointA      = profile.points[begin + i];
            const auto& pointB      = profile.points[begin + i + 1];
            const V3    edgeA       = side * (pointA.side - originPoint.side) + up * (pointA.up - originPoint.up);
            const V3    edgeB       = side * (pointB.side - originPoint.side) + up * (pointB.up - originPoint.up);
            const V3    areaNormal  = cross(edgeA, edgeB);
            if (dot(areaNormal, areaNormal) <= 1e-12f) continue;
            if (outward)
                mesh.addTriangle(base, base + i, base + i + 1);
            else
                mesh.addTriangle(base, base + i + 1, base + i);
        }
    };

    // Left shoulder: outer sidewalk → curb down to asphalt lip (excludes asphalt point).
    emitFan(0, asphaltBegin, RoadMaterial::Sidewalk);
    // Right shoulder: asphalt lip → curb → outer sidewalk.
    emitFan(asphaltBegin, profile.points.size(), RoadMaterial::Sidewalk);
}

Result<void> addDeckAndPiers(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames, const RoadStyle& style,
                             float halfWidth, bool includePiers) {
    if (frames.size() < 2) return Result<void>::success();
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
        const V3    a = o0 + s0 * -halfWidth + u0 * -style.deckThickness;
        const V3    b = o0 + s0 * halfWidth + u0 * -style.deckThickness;
        const V3    c = o1 + s1 * halfWidth + u1 * -style.deckThickness;
        const V3    d = o1 + s1 * -halfWidth + u1 * -style.deckThickness;
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
    const auto& style = edge.style;
    const float asphaltHalf = 0.5f * style.laneWidth * static_cast<float>(edge.lanesForward + edge.lanesBackward);
    auto paintLine = [&](float lateral, bool dashed, RoadMaterial material) -> Result<void> {
        if (dashed && style.dashLength <= 1e-5f) return Result<void>::success();
        const bool continuous = !dashed || style.dashGap <= 1e-5f;
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
            auto emitSpan = [&](float t0, float t1) -> Result<void> {
                constexpr std::size_t quadElements = 10u;
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
                const V3    a0 = p0 + s0 * (lateral - hw) + u0 * 0.035f;
                const V3    a1 = p0 + s0 * (lateral + hw) + u0 * 0.035f;
                const V3    b1 = p1 + s1 * (lateral + hw) + u1 * 0.035f;
                const V3    b0 = p1 + s1 * (lateral - hw) + u1 * 0.035f;
                appendStripQuad(mesh, a0, b0, b1, a1, normalize(u0 + u1), 0.f, 1.f, 0.f, 1.f, material);
                return Result<void>::success();
            };
            if (continuous) {
                auto emitted = emitSpan(0.f, 1.f);
                if (!emitted.ok()) return emitted;
                continue;
            }
            const double cycle = static_cast<double>(style.dashLength) + static_cast<double>(style.dashGap);
            const double startDistance = static_cast<double>(a.sample.normalizedDistance) * pathLength;
            const double endDistance   = static_cast<double>(b.sample.normalizedDistance) * pathLength;
            const double distanceSpan  = std::max(0.0, endDistance - startDistance);
            double       local         = 0.0;
            while (local < distanceSpan - 1e-7) {
                const double phase = std::fmod(startDistance + local, cycle);
                const bool   draw  = phase < static_cast<double>(style.dashLength);
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

Result<void> bakeEdgeGeometry(MeshBuild& mesh, RoadOverlay& overlay, const RoadNetwork& network, const RoadEdge& edge,
                              const RoadBakeOptions& options,
                              const std::unordered_set<std::uint32_t>& activeJunctionNodes,
                              const std::unordered_set<std::uint32_t>& terminalNodes) {
    auto spline = edgeToSpline(edge);
    if (!spline.ok()) return Result<void>::failure(spline.status());
    auto pathLength = spline.value().lengthResult(24);
    if (!pathLength.ok()) return Result<void>::failure(pathLength.status());

    auto fromNode = network.nodeResult(edge.from);
    auto toNode   = network.nodeResult(edge.to);
    if (!fromNode.ok() || !toNode.ok())
        return bakeFail<void>(DiagnosticCode::NotFound, "edge endpoints missing during bake");

    // A junction may not consume more than half of a short edge. Keep this
    // clamp shared with junction mouths and navigation turn anchors.
    const float trimStart = activeJunctionNodes.contains(fromNode.value().id)
                                ? junctionSocketDistance(network, fromNode.value(), edge, pathLength.value())
                                : 0.f;
    const float trimEnd   = activeJunctionNodes.contains(toNode.value().id)
                                ? junctionSocketDistance(network, toNode.value(), edge, pathLength.value())
                                : 0.f;
    if (trimStart + trimEnd >= pathLength.value() - 0.5f)
        return Result<void>::success();  // fully inside junction; skip strip

    const int segments = std::max(4, options.pathSegmentsPerEdge);
    auto      frames   = spline.value().sampleFramesResult(segments, true, 0.f, 24);
    if (!frames.ok()) return Result<void>::failure(frames.status());

    // Keep mid frames, then force exact trim endpoints so the loft meets the
    // junction apron (uniform samples alone leave ~segment-sized salmon gaps).
    std::vector<SplineFrameSample> trimmed;
    trimmed.reserve(frames.value().size() + 2);
    if (trimStart > 1e-3f) {
        auto tip = spline.value().travelFrameResult(trimStart, "clamp", 24);
        if (!tip.ok()) return Result<void>::failure(tip.status());
        trimmed.push_back(std::move(tip).takeValue());
    }
    const float endDist = pathLength.value() - trimEnd;
    for (const auto& frame : frames.value()) {
        const float d = frame.sample.normalizedDistance * pathLength.value();
        if (d > trimStart + 1e-3f && d < endDist - 1e-3f) trimmed.push_back(frame);
    }
    if (trimEnd > 1e-3f) {
        auto tip = spline.value().travelFrameResult(endDist, "clamp", 24);
        if (!tip.ok()) return Result<void>::failure(tip.status());
        trimmed.push_back(std::move(tip).takeValue());
    }
    if (trimmed.size() < 2) return Result<void>::success();

    // The edge owns the transition into the shared junction plane. The last
    // cross-section is therefore bit-for-bit compatible with the socket used
    // by bakeJunction, while the preceding rings absorb grade and crossfall.
    if (trimStart > 1e-3f) {
        const JunctionPlane plane      = fitJunctionPlane(network, fromNode.value());
        const float         transition = std::max(3.f, fromNode.value().junctionRadius);
        for (auto& frame : trimmed) {
            const float distance = frame.sample.normalizedDistance * pathLength.value() - trimStart;
            if (distance > transition) continue;
            const float t = std::clamp(1.f - std::max(0.f, distance) / transition, 0.f, 1.f);
            blendFrameToJunctionPlane(frame, plane, t * t * (3.f - 2.f * t));
        }
    }
    if (trimEnd > 1e-3f) {
        const JunctionPlane plane      = fitJunctionPlane(network, toNode.value());
        const float         transition = std::max(3.f, toNode.value().junctionRadius);
        for (auto& frame : trimmed) {
            const float distance = endDist - frame.sample.normalizedDistance * pathLength.value();
            if (distance > transition) continue;
            const float t = std::clamp(1.f - std::max(0.f, distance) / transition, 0.f, 1.f);
            blendFrameToJunctionPlane(frame, plane, t * t * (3.f - 2.f * t));
        }
    }

    auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
    if (!profile.ok()) return Result<void>::failure(profile.status());

    auto lofted = loftProfileClean(mesh, trimmed, profile.value(), pathLength.value(), edge.style.uvMeters);
    if (!lofted.ok()) return lofted;
    // Cap stub ends fully. On large junction trims, only close curb/sidewalk
    // shoulders — a full U-cap draws a dark asphalt tip bar that reads as a gap.
    if (trimStart <= 0.05f && terminalNodes.contains(fromNode.value().id)) {
        capProfileRing(mesh, trimmed.front(), profile.value(), false);
    } else if (trimStart > 0.05f) {
        if (trimStart < 2.5f)
            capProfileRing(mesh, trimmed.front(), profile.value(), false);
        else
            capProfileShoulders(mesh, trimmed.front(), profile.value(), false);
    }
    if (trimEnd <= 0.05f && terminalNodes.contains(toNode.value().id)) {
        capProfileRing(mesh, trimmed.back(), profile.value(), true);
    } else if (trimEnd > 0.05f) {
        if (trimEnd < 2.5f)
            capProfileRing(mesh, trimmed.back(), profile.value(), true);
        else
            capProfileShoulders(mesh, trimmed.back(), profile.value(), true);
    }
    auto deck = addDeckAndPiers(mesh, trimmed, edge.style, profile.value().halfWidth, options.includePiers);
    if (!deck.ok()) return deck;
    if (options.includeMarkings) {
        auto marks = addLaneMarkings(mesh, trimmed, edge, pathLength.value(), options);
        if (!marks.ok()) return marks;
    }

    if (options.includeNavigation) {
        for (int lane = 0; lane < edge.lanesForward; ++lane) {
            const float lateral =
                laneCenterOffset(edge.style, edge.lanesForward, edge.lanesBackward, lane, RoadLaneDirection::Forward);
            RoadPolyline poly;
            poly.r               = 0.15f;
            poly.g               = 0.9f;
            poly.b               = 1.f;
            poly.width           = options.navRibbonHalfWidth * 2.f;
            poly.inEdge          = edge.id;
            poly.inLane          = lane;
            poly.inDirection     = RoadLaneDirection::Forward;
            poly.outDirection    = RoadLaneDirection::Forward;
            poly.speedLimitMps   = edge.style.speedLimitMps;
            poly.trafficPriority = edge.style.trafficPriority;
            float traveled = 0.f;
            float nextArrow = options.arrowSpacing * 0.5f;
            V3    prev{};
            bool  hasPrev = false;
            for (const auto& frame : trimmed) {
                const V3 origin{frame.sample.x, frame.sample.y, frame.sample.z};
                const V3 side{frame.sideX, frame.sideY, frame.sideZ};
                const V3 up{frame.upX, frame.upY, frame.upZ};
                if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) continue;
                const V3 pos = origin + side * lateral + up * 0.08f;
                poly.xyz.push_back(pos.x);
                poly.xyz.push_back(pos.y);
                poly.xyz.push_back(pos.z);
                if (hasPrev) {
                    const float step = length(pos - prev);
                    if (std::isfinite(step) && step > 1e-5f && step < 1.0e4f) {
                        traveled += step;
                        const V3 fwd = normalize(pos - prev);
                        const V3 lat = normalize(cross(up, fwd));
                        const float hw = options.navRibbonHalfWidth;
                        appendStripQuad(mesh, prev + lat * -hw, pos + lat * -hw, pos + lat * hw, prev + lat * hw, up,
                                        0.f, 1.f, 0.f, 1.f, RoadMaterial::Nav);
                        int arrowGuard = 0;
                        while (traveled >= nextArrow && arrowGuard++ < 64) {
                            appendArrow(mesh, pos, fwd, up, 0.85f);
                            nextArrow += std::max(options.arrowSpacing, 0.5f);
                        }
                        if (arrowGuard >= 64) nextArrow = traveled + std::max(options.arrowSpacing, 0.5f);
                    }
                }
                prev    = pos;
                hasPrev = true;
            }
            overlay.lanes.push_back(std::move(poly));
        }
        for (int lane = 0; lane < edge.lanesBackward; ++lane) {
            const float lateral =
                laneCenterOffset(edge.style, edge.lanesForward, edge.lanesBackward, lane, RoadLaneDirection::Backward);
            RoadPolyline poly;
            poly.r               = 0.2f;
            poly.g               = 0.75f;
            poly.b               = 1.f;
            poly.width           = options.navRibbonHalfWidth * 2.f;
            poly.inEdge          = edge.id;
            poly.inLane          = lane;
            poly.inDirection     = RoadLaneDirection::Backward;
            poly.outDirection    = RoadLaneDirection::Backward;
            poly.speedLimitMps   = edge.style.speedLimitMps;
            poly.trafficPriority = edge.style.trafficPriority;
            for (auto it = trimmed.rbegin(); it != trimmed.rend(); ++it) {
                const V3 origin{it->sample.x, it->sample.y, it->sample.z};
                const V3 side{it->sideX, it->sideY, it->sideZ};
                const V3 up{it->upX, it->upY, it->upZ};
                const V3 pos = origin + side * lateral + up * 0.08f;
                poly.xyz.insert(poly.xyz.end(), {pos.x, pos.y, pos.z});
            }
            overlay.lanes.push_back(std::move(poly));
        }
    }
    return Result<void>::success();
}

Result<void> bakeJunction(MeshBuild& mesh, const RoadNetwork& network, const RoadNode& node,
                          const RoadBakeOptions& options) {
    struct ArmTip {
        V3    origin;
        V3    outward;
        V3    authoredOutward;
        V3    side;
        V3    up;
        float asphaltHalf = 0.f;
        float halfWidth   = 0.f;
        float curbWidth   = 0.f;
        float curbHeight  = 0.f;
        float sidewalkW   = 0.f;
        float sidewalkH   = 0.f;
        float uvMeters    = 8.f;
    };
    std::vector<ArmTip> arms;
    float               maxHalfWidth      = 0.f;
    float               maxAsphalt        = 0.f;
    float               maxCurbWidth      = 0.f;
    float               maxCurbHeight     = 0.f;
    float               maxSidewalkWidth  = 0.f;
    float               maxSidewalkHeight = 0.f;
    const JunctionPlane junctionPlane     = fitJunctionPlane(network, node);
    for (const auto& edge : network.edges()) {
        if (edge.to != node.id && edge.from != node.id) continue;
        auto spline = edgeToSpline(edge);
        if (!spline.ok()) continue;
        auto len = spline.value().lengthResult(16);
        if (!len.ok()) continue;
        const float trim  = junctionSocketDistance(network, node, edge, len.value());
        const float d     = edge.to == node.id ? len.value() - trim : trim;
        auto        frame = spline.value().travelFrameResult(d, "clamp", 16);
        if (!frame.ok()) continue;
        blendFrameToJunctionPlane(frame.value(), junctionPlane, 1.f);
        auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
        if (!profile.ok()) continue;
        const float asphaltHalf =
            0.5f * edge.style.laneWidth * static_cast<float>(edge.lanesForward + edge.lanesBackward);
        const auto& f = frame.value();
        ArmTip      tip;
        tip.origin       = V3{f.sample.x, f.sample.y, f.sample.z};
        const V3 outward = normalize(V3{tip.origin.x - node.x, 0.f, tip.origin.z - node.z});
        V3       tangentOut{f.forwardX, f.forwardY, f.forwardZ};
        if (edge.to == node.id) tangentOut = tangentOut * -1.f;
        tip.outward = normalize(
            V3{tangentOut.x, junctionPlane.gradeX * tangentOut.x + junctionPlane.gradeZ * tangentOut.z, tangentOut.z});
        tip.authoredOutward = authoredOutwardDirection(node, edge);
        const V3 canonicalSide{-outward.z, 0.f, outward.x};
        // Preserve the exact spline frame at the seam. Only normalize its sign
        // so every mouth is ordered clockwise -> counter-clockwise around the
        // hub; deriving a new radial side breaks curved approaches.
        tip.side = normalize(V3{f.sideX, f.sideY, f.sideZ});
        if (dot(tip.side, canonicalSide) < 0.f) tip.side = tip.side * -1.f;
        tip.up = normalize(V3{f.upX, f.upY, f.upZ});
        if (tip.up.y < 0.f) tip.up = tip.up * -1.f;
        tip.asphaltHalf = asphaltHalf;
        tip.halfWidth   = profile.value().halfWidth;
        tip.curbWidth   = edge.style.curbWidth;
        tip.curbHeight  = edge.style.curbHeight;
        tip.sidewalkW   = edge.style.sidewalkWidth;
        tip.sidewalkH   = edge.style.sidewalkHeight;
        tip.uvMeters    = edge.style.uvMeters;
        arms.push_back(tip);
        maxHalfWidth      = std::max(maxHalfWidth, tip.halfWidth);
        maxAsphalt        = std::max(maxAsphalt, tip.asphaltHalf);
        maxCurbWidth      = std::max(maxCurbWidth, edge.style.curbWidth);
        maxCurbHeight     = std::max(maxCurbHeight, edge.style.curbHeight);
        maxSidewalkWidth  = std::max(maxSidewalkWidth, edge.style.sidewalkWidth);
        maxSidewalkHeight = std::max(maxSidewalkHeight, edge.style.sidewalkHeight);
    }
    if (static_cast<int>(arms.size()) < 2) return Result<void>::success();

    float       jr       = node.junctionRadius;
    for (const auto& tip : arms) jr = std::min(jr, length(V3{tip.origin.x - node.x, 0.f, tip.origin.z - node.z}));

    // Detect an axis-aligned 4-way cross (the simple debug scene).
    bool axisCross = arms.size() == 4;
    if (axisCross) {
        int axisHits = 0;
        for (const auto& tip : arms) {
            const float dx = std::fabs(tip.origin.x - node.x);
            const float dz = std::fabs(tip.origin.z - node.z);
            if ((dx < 0.35f && dz > jr * 0.4f) || (dz < 0.35f && dx > jr * 0.4f)) ++axisHits;
        }
        axisCross = axisHits == 4;
    }

    // Asphalt fill. Fan from a hub vertex — a single large strip-quad can vanish
    // under Lavapipe even when the CPU triangles cover the origin.
    if (axisCross) {
        // Curb-return fillet: circle centers sit in the outer corner (jr,jr), so the
        // asphalt edge is concave and sidewalks wrap toward the property corner.
        const float ah      = maxAsphalt;
        const float cornerR = std::max(0.75f, jr - ah);
        const int   segs    = circularArcSegments(cornerR, 1.5707963f, options.junctionChordError);
        const V3    upN = junctionPlane.up;
        auto surfacePoint = [&](float x, float z, float offset = 0.f) {
            return V3{x, planeHeight(junctionPlane, x, z), z} + upN * offset;
        };

        // Hub fan along the filleted outline (arm tips + concave corner arcs).
        {
            std::vector<std::pair<float, float>> rim;
            rim.reserve(static_cast<std::size_t>(4 * (segs + 3)));
            auto pushPt = [&](float x, float z) {
                if (!rim.empty() && std::fabs(rim.back().first - x) < 1e-4f && std::fabs(rim.back().second - z) < 1e-4f)
                    return;
                rim.emplace_back(x, z);
            };
            auto pushCornerArc = [&](int sx, int sz, bool reverse) {
                const float sxf = static_cast<float>(sx);
                const float szf = static_cast<float>(sz);
                const float cx  = node.x + sxf * jr;
                const float cz  = node.z + szf * jr;
                for (int i = 0; i <= segs; ++i) {
                    const int   ii = reverse ? (segs - i) : i;
                    const float t  = static_cast<float>(ii) / static_cast<float>(segs);
                    const float th = t * 1.5707963f;
                    pushPt(cx - sxf * cornerR * std::sin(th), cz - szf * cornerR * std::cos(th));
                }
            };
            pushPt(node.x + jr, node.z - ah);
            pushPt(node.x + jr, node.z + ah);
            pushCornerArc(+1, +1, false);
            pushPt(node.x - ah, node.z + jr);
            pushCornerArc(-1, +1, true);
            pushPt(node.x - jr, node.z - ah);
            pushCornerArc(-1, -1, false);
            pushPt(node.x + ah, node.z - jr);
            pushCornerArc(+1, -1, true);

            const V3 hub = surfacePoint(node.x, node.z, 0.01f);
            for (std::size_t i = 0; i < rim.size(); ++i) {
                const auto& p0 = rim[i];
                const auto& p1 = rim[(i + 1) % rim.size()];
                appendOrientedTri(mesh, hub, surfacePoint(p0.first, p0.second, 0.01f),
                                  surfacePoint(p1.first, p1.second, 0.01f), upN, arms.front().uvMeters,
                                  RoadMaterial::Asphalt);
            }

            // Thin seal strips under each arm tip — hides residual sub-cm seams.
            const float seal    = 0.12f;
            auto        addSeal = [&](float x0, float z0, float x1, float z1, float x2, float z2, float x3, float z3) {
                appendOrientedQuad(mesh, surfacePoint(x0, z0, 0.012f), surfacePoint(x1, z1, 0.012f),
                                   surfacePoint(x2, z2, 0.012f), surfacePoint(x3, z3, 0.012f), upN, 0.f, 1.f, 0.f,
                                   1.f, RoadMaterial::Asphalt);
            };
            addSeal(node.x - ah, node.z + jr - seal, node.x + ah, node.z + jr - seal, node.x + ah, node.z + jr + seal,
                    node.x - ah, node.z + jr + seal);
            addSeal(node.x - ah, node.z - jr - seal, node.x + ah, node.z - jr - seal, node.x + ah, node.z - jr + seal,
                    node.x - ah, node.z - jr + seal);
            addSeal(node.x + jr - seal, node.z - ah, node.x + jr + seal, node.z - ah, node.x + jr + seal, node.z + ah,
                    node.x + jr - seal, node.z + ah);
            addSeal(node.x - jr - seal, node.z - ah, node.x - jr + seal, node.z - ah, node.x - jr + seal, node.z + ah,
                    node.x - jr - seal, node.z + ah);
        }

        const float curbW = 0.35f;
        const float walkW = std::max(0.4f, maxHalfWidth - ah - curbW);
        float       curbH = 0.45f;
        float       walkH = 0.14f;
        if (!arms.empty()) {
            curbH = arms.front().curbHeight;
            walkH = arms.front().sidewalkH;
        }
        const float r0      = cornerR;
        const float r1      = std::max(0.2f, cornerR - curbW);
        const float r2      = std::max(0.15f, cornerR - curbW - walkW);
        for (int sx : {-1, 1}) {
            for (int sz : {-1, 1}) {
                const float sxf   = static_cast<float>(sx);
                const float szf   = static_cast<float>(sz);
                const float cx    = node.x + sxf * jr;
                const float cz    = node.z + szf * jr;
                auto        arcPt = [&](float radius, float t) {
                    const float th = t * 1.5707963f;
                    return V3{cx - sxf * radius * std::sin(th), 0.f, cz - szf * radius * std::cos(th)};
                };
                auto wall = [&](V3 p0, V3 p1, float offset0, float offset1, V3 n, RoadMaterial mat) {
                    appendOrientedQuad(mesh, surfacePoint(p0.x, p0.z, offset0), surfacePoint(p1.x, p1.z, offset0),
                                       surfacePoint(p1.x, p1.z, offset1), surfacePoint(p0.x, p0.z, offset1), n, 0.f,
                                       1.f, 0.f, 1.f, mat);
                    appendOrientedQuad(mesh, surfacePoint(p0.x, p0.z, offset0), surfacePoint(p1.x, p1.z, offset0),
                                       surfacePoint(p1.x, p1.z, offset1), surfacePoint(p0.x, p0.z, offset1), n * -1.f,
                                       0.f, 1.f, 0.f, 1.f, mat);
                };
                for (int i = 0; i < segs; ++i) {
                    const float t0 = static_cast<float>(i) / static_cast<float>(segs);
                    const float t1 = static_cast<float>(i + 1) / static_cast<float>(segs);
                    const V3    a0 = arcPt(r0, t0);
                    const V3    a1 = arcPt(r0, t1);
                    const V3    b0 = arcPt(r1, t0);
                    const V3    b1 = arcPt(r1, t1);
                    const V3    s0 = arcPt(r2, t0);
                    const V3    s1 = arcPt(r2, t1);
                    // Sidewalk top slightly past r2 toward the property to cover the skirt seam.
                    const V3 c0         = arcPt(std::max(0.05f, r2 - 0.06f), t0);
                    const V3 c1         = arcPt(std::max(0.05f, r2 - 0.06f), t1);
                    const V3 b0i        = arcPt(r1 - 0.02f, t0);
                    const V3 b1i        = arcPt(r1 - 0.02f, t1);
                    const V3 b0o        = arcPt(r1 + 0.02f, t0);
                    const V3 b1o        = arcPt(r1 + 0.02f, t1);
                    const V3 inToCorner = normalize(V3{cx - a0.x, 0.f, cz - a0.z});
                    const V3 outToRoad  = inToCorner * -1.f;

                    appendOrientedQuad(mesh, surfacePoint(a0.x, a0.z, curbH), surfacePoint(b0o.x, b0o.z, curbH),
                                       surfacePoint(b1o.x, b1o.z, curbH), surfacePoint(a1.x, a1.z, curbH), upN, 0.f,
                                       1.f, 0.f, 1.f, RoadMaterial::Curb);
                    appendOrientedQuad(mesh, surfacePoint(b0i.x, b0i.z, walkH), surfacePoint(c0.x, c0.z, walkH),
                                       surfacePoint(c1.x, c1.z, walkH), surfacePoint(b1i.x, b1i.z, walkH), upN, 0.f,
                                       1.f, 0.f, 1.f, RoadMaterial::Sidewalk);

                    wall(a0, a1, 0.f, curbH, outToRoad, RoadMaterial::Curb);
                    wall(b0, b1, walkH, curbH, outToRoad, RoadMaterial::Curb);
                    wall(s0, s1, 0.f, walkH, outToRoad, RoadMaterial::Sidewalk);
                }
            }
        }
    } else {
        // General junction: retain every trimmed road mouth as an explicit open
        // edge. A convex hull is invalid here: unequal widths can hide a mouth
        // corner inside the hull and the curb pass then closes across traffic.
        // Ordering mouths around the hub produces a star-shaped apron whose
        // alternating edges are exactly [mouth, exposed connector].
        std::sort(arms.begin(), arms.end(), [&](const ArmTip& a, const ArmTip& b) {
            return std::atan2(a.origin.z - node.z, a.origin.x - node.x) <
                   std::atan2(b.origin.z - node.z, b.origin.x - node.x);
        });
        struct RimStyle {
            float curbWidth;
            float curbHeight;
            float sidewalkWidth;
            float sidewalkHeight;
        };
        std::vector<V3>       rim;
        std::vector<V3>       rimUps;
        std::vector<RimStyle> rimStyles;
        rim.reserve(arms.size() * 8);
        rimUps.reserve(arms.size() * 8);
        rimStyles.reserve(arms.size() * 8);
        auto pushRim = [&](V3 point, RimStyle style) {
            point.y = planeHeight(junctionPlane, point.x, point.z);
            if (!rim.empty() && length(point - rim.back()) < 1e-4f) return;
            rim.push_back(point);
            rimUps.push_back(junctionPlane.up);
            rimStyles.push_back(style);
        };
        auto armStyle = [](const ArmTip& tip) {
            return RimStyle{tip.curbWidth, tip.curbHeight, tip.sidewalkW, tip.sidewalkH};
        };
        auto mixStyle = [](RimStyle a, RimStyle b, float t) {
            const float s = t * t * (3.f - 2.f * t);
            return RimStyle{a.curbWidth + (b.curbWidth - a.curbWidth) * s,
                            a.curbHeight + (b.curbHeight - a.curbHeight) * s,
                            a.sidewalkWidth + (b.sidewalkWidth - a.sidewalkWidth) * s,
                            a.sidewalkHeight + (b.sidewalkHeight - a.sidewalkHeight) * s};
        };
        for (std::size_t armIndex = 0; armIndex < arms.size(); ++armIndex) {
            const auto&    tip              = arms[armIndex];
            const auto&    next             = arms[(armIndex + 1) % arms.size()];
            V3             clockwise        = tip.origin - tip.side * tip.asphaltHalf;
            V3             counterClockwise = tip.origin + tip.side * tip.asphaltHalf;
            const RimStyle fromStyle        = armStyle(tip);
            const RimStyle toStyle          = armStyle(next);
            pushRim(clockwise, fromStyle);
            pushRim(counterClockwise, fromStyle);

            // The curb return is defined by the intersection of both outward
            // boundary rays. This is the same finite-corner constraint used by
            // the reference implementation: parallel, rear-facing or remote
            // intersections stay straight instead of creating a wild miter.
            const V3    end         = next.origin - next.side * next.asphaltHalf;
            const float denominator = tip.outward.x * next.outward.z - tip.outward.z * next.outward.x;
            if (arms.size() >= 3 && std::fabs(denominator) > 1e-4f) {
                const V3    delta        = end - counterClockwise;
                const float alongTip     = (delta.x * next.outward.z - delta.z * next.outward.x) / denominator;
                const float alongNext    = (delta.x * tip.outward.z - delta.z * tip.outward.x) / denominator;
                const float maximumMiter = node.junctionRadius * 2.f + tip.asphaltHalf + next.asphaltHalf;
                if (alongTip > 0.f && alongNext > 0.f && alongTip < maximumMiter && alongNext < maximumMiter) {
                    V3 control           = counterClockwise + tip.outward * alongTip;
                    control.y            = planeHeight(junctionPlane, control.x, control.z);
                    const float angle    = std::acos(std::clamp(dot(tip.outward, next.outward), -1.f, 1.f));
                    const int   segments = quadraticSegments(counterClockwise, control, end, angle,
                                                             options.junctionChordError);
                    for (int segment = 1; segment < segments; ++segment) {
                        const float t     = static_cast<float>(segment) / static_cast<float>(segments);
                        const float it    = 1.f - t;
                        const V3    point = counterClockwise * (it * it) + control * (2.f * it * t) + end * (t * t);
                        pushRim(point, mixStyle(fromStyle, toStyle, t));
                    }
                }
            }
        }
        if (rim.size() < 3)
            return bakeFail<void>(DiagnosticCode::InvariantViolation, "junction arm mouths do not form a valid polygon",
                                  "junction");
        const V3                                upN = junctionPlane.up;
        std::vector<std::array<std::size_t, 3>> triangles;
        if (!triangulateBoundary(rim, triangles)) {
            // Dense multi-level nodes may not have enough authored radius for
            // every rounded return. Retain all socket mouth edges and retry
            // with their straight connector polygon; never accept a crossing
            // rounded outline or silently fill its convex hull.
            rim.clear();
            rimUps.clear();
            rimStyles.clear();
            for (const auto& arm : arms) {
                const RimStyle style = armStyle(arm);
                pushRim(arm.origin - arm.side * arm.asphaltHalf, style);
                pushRim(arm.origin + arm.side * arm.asphaltHalf, style);
            }
            if (!triangulateBoundary(rim, triangles))
                return bakeFail<void>(DiagnosticCode::InvariantViolation,
                                      "junction sockets form a self-intersecting boundary", "junction");
        }
        for (const auto& triangle : triangles)
            appendOrientedTri(mesh, rim[triangle[0]], rim[triangle[1]], rim[triangle[2]], upN, arms.front().uvMeters,
                              RoadMaterial::Asphalt);

        // Preserve a through-road when a third arm turns an otherwise smooth
        // split into a junction. The shared authored tangent selects the pair;
        // the two exact mouth edges bound the only surface restored here.
        for (std::size_t first = 0; first < arms.size(); ++first) {
            for (std::size_t second = first + 1; second < arms.size(); ++second) {
                const auto& a = arms[first];
                const auto& b = arms[second];
                if (dot(a.authoredOutward, b.authoredOutward) > -0.999f) continue;
                const V3 lift = upN * 0.01f;
                std::array<V3, 4> corners = {a.origin - a.side * a.asphaltHalf + lift,
                                             a.origin + a.side * a.asphaltHalf + lift,
                                             b.origin - b.side * b.asphaltHalf + lift,
                                             b.origin + b.side * b.asphaltHalf + lift};
                V3 center{};
                for (const auto& corner : corners) center = center + corner * 0.25f;
                std::sort(corners.begin(), corners.end(), [&](const V3& lhs, const V3& rhs) {
                    return std::atan2(lhs.z - center.z, lhs.x - center.x) <
                           std::atan2(rhs.z - center.z, rhs.x - center.x);
                });
                appendOrientedQuad(mesh, corners[0], corners[1], corners[2], corners[3], upN, 0.f, 1.f, 0.f, 1.f,
                                   RoadMaterial::Asphalt);
            }
        }

        // Continue the edge profile around exposed hull sides while leaving
        // every road mouth open. Offset the whole convex ring first so adjacent
        // boundary strips share corners instead of producing one quad per arm.
        auto sameXZ = [](const V3& a, const V3& b) {
            return std::fabs(a.x - b.x) < 1e-3f && std::fabs(a.z - b.z) < 1e-3f;
        };
        auto isMouth = [&](const V3& a, const V3& b) {
            for (const auto& arm : arms) {
                const V3 left  = arm.origin - arm.side * arm.asphaltHalf;
                const V3 right = arm.origin + arm.side * arm.asphaltHalf;
                if ((sameXZ(a, left) && sameXZ(b, right)) || (sameXZ(a, right) && sameXZ(b, left))) return true;
            }
            return false;
        };
        const float orientation = signedAreaXZ(rim) >= 0.f ? 1.f : -1.f;
        auto        edgeOutward = [&](std::size_t i) {
            const V3 edge    = rim[(i + 1) % rim.size()] - rim[i];
            V3       outward = normalize(cross(junctionPlane.up, edge));
            return orientation >= 0.f ? outward : outward * -1.f;
        };
        auto offsetRing = [&](const std::vector<float>& distances) {
            std::vector<V3> result;
            result.reserve(rim.size());
            for (std::size_t i = 0; i < rim.size(); ++i) {
                bool alignedToMouth = false;
                for (const auto& arm : arms) {
                    const V3 left  = arm.origin - arm.side * arm.asphaltHalf;
                    const V3 right = arm.origin + arm.side * arm.asphaltHalf;
                    if (sameXZ(rim[i], left)) {
                        V3 point = arm.origin - arm.side * (arm.asphaltHalf + distances[i]);
                        result.push_back(point);
                        alignedToMouth = true;
                        break;
                    }
                    if (sameXZ(rim[i], right)) {
                        V3 point = arm.origin + arm.side * (arm.asphaltHalf + distances[i]);
                        result.push_back(point);
                        alignedToMouth = true;
                        break;
                    }
                }
                if (alignedToMouth) continue;
                const V3    previous   = edgeOutward((i + rim.size() - 1) % rim.size());
                const V3    next       = edgeOutward(i);
                const V3    miter      = normalize(previous + next);
                const float projection = std::max(0.25f, dot(miter, next));
                result.push_back(rim[i] + miter * (distances[i] / projection));
            }
            return result;
        };
        std::vector<float> curbDistances;
        std::vector<float> walkDistances;
        curbDistances.reserve(rim.size());
        walkDistances.reserve(rim.size());
        for (const auto& style : rimStyles) {
            curbDistances.push_back(style.curbWidth);
            walkDistances.push_back(style.curbWidth + style.sidewalkWidth);
        }
        const auto curbRing = offsetRing(curbDistances);
        const auto walkRing = offsetRing(walkDistances);
        for (std::size_t i = 0; i < rim.size(); ++i) {
            const std::size_t next = (i + 1) % rim.size();
            if (isMouth(rim[i], rim[next])) continue;
            const V3    outward = edgeOutward(i);
            const V3    upA     = rimUps[i];
            const V3    upB     = rimUps[next];
            const auto& styleA  = rimStyles[i];
            const auto& styleB  = rimStyles[next];
            const V3    innerA{rim[i].x, rim[i].y, rim[i].z};
            const V3    innerB{rim[next].x, rim[next].y, rim[next].z};
            const V3    surfaceA  = innerA;
            const V3    surfaceB  = innerB;
            const V3    innerTopA = surfaceA + upA * styleA.curbHeight;
            const V3    innerTopB = surfaceB + upB * styleB.curbHeight;
            const V3    curbTopA  = curbRing[i] + upA * styleA.curbHeight;
            const V3    curbTopB  = curbRing[next] + upB * styleB.curbHeight;
            const V3    curbWalkA = curbRing[i] + upA * styleA.sidewalkHeight;
            const V3    curbWalkB = curbRing[next] + upB * styleB.sidewalkHeight;
            const V3    walkA     = walkRing[i] + upA * styleA.sidewalkHeight;
            const V3    walkB     = walkRing[next] + upB * styleB.sidewalkHeight;
            if (styleA.curbWidth > 1e-4f || styleB.curbWidth > 1e-4f) {
                appendOrientedQuad(mesh, innerA, innerB, innerTopB, innerTopA, outward * -1.f, 0.f, 1.f, 0.f, 1.f,
                                   RoadMaterial::Curb);
                appendOrientedQuad(mesh, innerTopA, innerTopB, curbTopB, curbTopA, upN, 0.f, 1.f, 0.f, 1.f,
                                   RoadMaterial::Curb);
                appendOrientedQuad(mesh, curbWalkA, curbWalkB, curbTopB, curbTopA, outward, 0.f, 1.f, 0.f, 1.f,
                                   RoadMaterial::Curb);
            }
            if (styleA.sidewalkWidth > 1e-4f || styleB.sidewalkWidth > 1e-4f) {
                appendOrientedQuad(mesh, curbWalkA, curbWalkB, walkB, walkA, upN, 0.f, 1.f, 0.f, 1.f,
                                   RoadMaterial::Sidewalk);
                appendOrientedQuad(mesh, walkRing[i], walkRing[next], walkB, walkA, outward, 0.f, 1.f, 0.f, 1.f,
                                   RoadMaterial::Sidewalk);
            }
        }
    }

    if (!options.includeMarkings || arms.size() == 2u) return Result<void>::success();

    // Zebra stripes on every arm tip that meets this hub.
    for (const auto& edge : network.edges()) {
        if (edge.to != node.id && edge.from != node.id) continue;
        auto spline = edgeToSpline(edge);
        if (!spline.ok()) continue;
        auto len = spline.value().lengthResult(16);
        if (!len.ok()) continue;
        const float trim = junctionSocketDistance(network, node, edge, len.value());
        if (len.value() < trim + 1.5f) continue;
        const float alongDist = edge.to == node.id ? (len.value() - trim - 1.6f) : (trim + 1.6f);
        auto        frame     = spline.value().travelFrameResult(alongDist, "clamp", 16);
        if (!frame.ok()) continue;
        const auto& f = frame.value();
        const V3    origin{f.sample.x, f.sample.y, f.sample.z};
        const V3    side{f.sideX, f.sideY, f.sideZ};
        // Stripe direction: toward the hub from this tip.
        V3 fwd{f.forwardX, f.forwardY, f.forwardZ};
        if (edge.from == node.id) fwd = fwd * -1.f;
        const V3    nrm = normalize(V3{f.upX, f.upY, f.upZ});
        const float stripeW =
            edge.style.laneWidth * static_cast<float>(edge.lanesForward + edge.lanesBackward) * 0.45f;
        for (int s = 0; s < 4; ++s) {
            const float along = static_cast<float>(s) * 0.5f;
            const V3    c     = origin - fwd * along + nrm * 0.04f;
            const V3    a     = c + side * -stripeW;
            const V3    b     = c + side * stripeW;
            const V3    d     = a - fwd * 0.25f;
            const V3    e     = b - fwd * 0.25f;
            appendOrientedQuad(mesh, a, b, e, d, nrm, 0.f, 1.f, 0.f, 1.f, RoadMaterial::Marking);
        }
    }
    return Result<void>::success();
}

Result<void> bakeTurnOverlays(MeshBuild& mesh, RoadOverlay& overlay, const RoadNetwork& network,
                              const RoadBakeOptions&                   options,
                              const std::unordered_set<std::uint32_t>& activeJunctionNodes) {
    std::unordered_map<std::uint32_t, SplinePath> paths;
    for (const auto& edge : network.edges()) {
        auto spline = edgeToSpline(edge);
        if (!spline.ok()) return Result<void>::failure(spline.status());
        paths.emplace(edge.id, std::move(spline).takeValue());
    }

    for (const auto& link : network.laneLinks()) {
        auto inEdge  = network.edgeResult(link.inEdge);
        auto outEdge = network.edgeResult(link.outEdge);
        if (!inEdge.ok() || !outEdge.ok()) continue;
        const std::uint32_t nodeId =
            link.inDirection == RoadLaneDirection::Forward ? inEdge.value().to : inEdge.value().from;
        auto node = network.nodeResult(nodeId);
        if (!node.ok()) continue;

        auto& inPath  = paths[link.inEdge];
        auto& outPath = paths[link.outEdge];
        auto  inLen   = inPath.lengthResult(16);
        auto  outLen  = outPath.lengthResult(16);
        if (!inLen.ok() || !outLen.ok()) continue;

        const bool  activeJunction = activeJunctionNodes.contains(node.value().id);
        const float inTrim =
            activeJunction ? junctionSocketDistance(network, node.value(), inEdge.value(), inLen.value()) : 0.f;
        const float outTrim =
            activeJunction ? junctionSocketDistance(network, node.value(), outEdge.value(), outLen.value()) : 0.f;
        const float inDist   = link.inDirection == RoadLaneDirection::Forward ? inLen.value() - inTrim : inTrim;
        const float outDist  = link.outDirection == RoadLaneDirection::Forward ? outTrim : outLen.value() - outTrim;
        auto        inFrame  = inPath.travelFrameResult(inDist, "clamp", 16);
        auto        outFrame = outPath.travelFrameResult(outDist, "clamp", 16);
        if (!inFrame.ok() || !outFrame.ok()) continue;
        if (activeJunction) {
            const JunctionPlane plane = fitJunctionPlane(network, node.value());
            blendFrameToJunctionPlane(inFrame.value(), plane, 1.f);
            blendFrameToJunctionPlane(outFrame.value(), plane, 1.f);
        }

        const float inLat  = laneCenterOffset(inEdge.value().style, inEdge.value().lanesForward,
                                              inEdge.value().lanesBackward, link.inLane, link.inDirection);
        const float outLat = laneCenterOffset(outEdge.value().style, outEdge.value().lanesForward,
                                              outEdge.value().lanesBackward, link.outLane, link.outDirection);

        const auto& fi = inFrame.value();
        const auto& fo = outFrame.value();
        const V3    p0{fi.sample.x, fi.sample.y, fi.sample.z};
        const V3    p1{fo.sample.x, fo.sample.y, fo.sample.z};
        const V3    si{fi.sideX, fi.sideY, fi.sideZ};
        const V3    so{fo.sideX, fo.sideY, fo.sideZ};
        const V3    ui{fi.upX, fi.upY, fi.upZ};
        const V3    uo{fo.upX, fo.upY, fo.upZ};
        V3          ti{fi.forwardX, fi.forwardY, fi.forwardZ};
        V3          to{fo.forwardX, fo.forwardY, fo.forwardZ};
        if (link.inDirection == RoadLaneDirection::Backward) ti = ti * -1.f;
        if (link.outDirection == RoadLaneDirection::Backward) to = to * -1.f;
        const V3 a = p0 + si * inLat + ui * 0.09f;
        const V3 d = p1 + so * outLat + uo * 0.09f;
        // A radius-sized handle works for ordinary junctions, but can overshoot
        // badly when two short/acute arms leave their lane endpoints close
        // together. Bound it by the endpoint chord so the cubic stays local.
        const float handle = std::min(node.value().junctionRadius * 0.65f, length(d - a) * 0.5f);
        const V3    b      = a + ti * handle;
        const V3    c      = d - to * handle;

        RoadPolyline poly;
        poly.r               = 0.25f;
        poly.g               = 0.95f;
        poly.b               = 0.45f;
        poly.width           = options.navRibbonHalfWidth * 2.f;
        poly.inEdge          = link.inEdge;
        poly.outEdge         = link.outEdge;
        poly.inLane          = link.inLane;
        poly.outLane         = link.outLane;
        poly.inDirection     = link.inDirection;
        poly.outDirection    = link.outDirection;
        poly.speedLimitMps   = std::min(inEdge.value().style.speedLimitMps, outEdge.value().style.speedLimitMps);
        poly.trafficPriority = inEdge.value().style.trafficPriority;
        const int samples = std::max(4, options.turnSamples);
        V3        prev{};
        bool      hasPrev = false;
        for (int i = 0; i <= samples; ++i) {
            const float t  = static_cast<float>(i) / static_cast<float>(samples);
            const float u  = 1.f - t;
            const V3    p  = a * (u * u * u) + b * (3.f * u * u * t) + c * (3.f * u * t * t) + d * (t * t * t);
            poly.xyz.push_back(p.x);
            poly.xyz.push_back(p.y);
            poly.xyz.push_back(p.z);
            if (hasPrev) {
                const V3 delta = p - prev;
                if (length(delta) > 1e-5f) {
                    const V3 fwd = normalize(delta);
                    const V3 up  = normalize(ui + uo);
                    const V3 lat = normalize(cross(up, fwd));
                    const float hw = options.navRibbonHalfWidth * 0.9f;
                    appendStripQuad(mesh, prev + lat * -hw, p + lat * -hw, p + lat * hw, prev + lat * hw, up,
                                    0.f, 1.f, 0.f, 1.f, RoadMaterial::Nav);
                }
            }
            if (i == samples / 2 && length(d - a) > 1e-5f) {
                // Follow the curve at the arrow position. Using the endpoint chord
                // points across a hairpin and can visibly cross the turn ribbon.
                const V3 derivative = (b - a) * (3.f * u * u) + (c - b) * (6.f * u * t) +
                                      (d - c) * (3.f * t * t);
                const V3 fwd = length(derivative) > 1e-5f ? normalize(derivative) : normalize(d - a);
                appendArrow(mesh, p, fwd, normalize(ui + uo), 0.7f);
            }
            prev    = p;
            hasPrev = true;
        }
        overlay.turns.push_back(std::move(poly));
        if (exceedsMeshBudget(mesh, options))
            return bakeFail<void>(DiagnosticCode::PreconditionViolation,
                                  "road navigation exceeds the mesh element budget", "mesh");
    }
    return Result<void>::success();
}

Result<void> bakePlacementPoints(PointSet& placements, const RoadNetwork& network, const RoadBakeOptions& options,
                                 const std::unordered_set<std::uint32_t>& activeJunctionNodes) {
    if (!std::isfinite(options.sideObjectSpacing) || options.sideObjectSpacing < 0.25f ||
        !std::isfinite(options.sideObjectOffset) || options.sideObjectOffset < 0.f || options.maximumPlacements < 1)
        return bakeFail<void>(DiagnosticCode::InvalidArgument, "road placement options are invalid", "placements");
    if (!std::isfinite(options.sideObjectClearance) || options.sideObjectClearance < 0.f)
        return bakeFail<void>(DiagnosticCode::InvalidArgument, "road side-object clearance is invalid",
                              "placements.clearance");

    struct EdgeSource {
        const RoadEdge* edge = nullptr;
        SplinePath      spline;
        float           length    = 0.f;
        float           halfWidth = 0.f;
        float           sideStart = 0.f;
        float           sideEnd   = 0.f;
    };
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> adjacency;
    for (const auto& node : network.nodes()) adjacency.emplace(node.id, std::vector<std::uint32_t>{});
    for (const auto& edge : network.edges()) {
        adjacency[edge.from].push_back(edge.to);
        adjacency[edge.to].push_back(edge.from);
    }

    std::vector<std::vector<std::uint32_t>> roundabouts;
    auto canonicalCycle = [](std::vector<std::uint32_t> cycle) {
        const auto minimum = std::min_element(cycle.begin(), cycle.end());
        std::rotate(cycle.begin(), minimum, cycle.end());
        std::vector<std::uint32_t> reversed{cycle.front()};
        for (auto it = cycle.rbegin(); it != cycle.rend() - 1; ++it) reversed.push_back(*it);
        return reversed < cycle ? reversed : cycle;
    };
    for (const auto& node : network.nodes()) {
        if (adjacency[node.id].size() != 3u) continue;
        std::vector<std::uint32_t> path{node.id};
        std::function<void(std::uint32_t)> visit = [&](std::uint32_t current) {
            for (const auto next : adjacency[current]) {
                if (next == path.front()) {
                    if (path.size() < 3u || path.size() > 12u) continue;
                    auto cycle = canonicalCycle(path);
                    if (std::find(roundabouts.begin(), roundabouts.end(), cycle) != roundabouts.end()) continue;
                    float centerX = 0.f, centerZ = 0.f, minY = std::numeric_limits<float>::max();
                    float maxY = -std::numeric_limits<float>::max();
                    bool  valid = true;
                    int   entryCount = 0;
                    for (const auto id : cycle) {
                        if (adjacency[id].size() < 2u || adjacency[id].size() > 3u) {
                            valid = false;
                            break;
                        }
                        if (adjacency[id].size() == 3u) ++entryCount;
                        auto value = network.nodeResult(id);
                        if (!value.ok()) {
                            valid = false;
                            break;
                        }
                        centerX += value.value().x;
                        centerZ += value.value().z;
                        minY = std::min(minY, value.value().y);
                        maxY = std::max(maxY, value.value().y);
                    }
                    if (!valid || entryCount < 3 || maxY - minY > 1.f) continue;
                    for (std::size_t i = 0; i < cycle.size(); ++i) {
                        const auto a = cycle[i];
                        const auto b = cycle[(i + 1u) % cycle.size()];
                        const auto ringEdge = std::find_if(network.edges().begin(), network.edges().end(),
                                                           [&](const RoadEdge& edge) {
                                                               return (edge.from == a && edge.to == b) ||
                                                                      (edge.from == b && edge.to == a);
                                                           });
                        if (ringEdge == network.edges().end() || ringEdge->controlPoints.size() < 3u) {
                            valid = false;
                            break;
                        }
                    }
                    if (!valid) continue;
                    centerX /= static_cast<float>(cycle.size());
                    centerZ /= static_cast<float>(cycle.size());
                    float minRadius = std::numeric_limits<float>::max(), maxRadius = 0.f;
                    for (const auto id : cycle) {
                        const auto value = network.nodeResult(id).value();
                        const float dx = value.x - centerX, dz = value.z - centerZ;
                        const float radius = std::sqrt(dx * dx + dz * dz);
                        minRadius = std::min(minRadius, radius);
                        maxRadius = std::max(maxRadius, radius);
                    }
                    if (minRadius >= 1.f && maxRadius <= minRadius * 1.8f) roundabouts.push_back(std::move(cycle));
                    continue;
                }
                if (path.size() >= 12u || adjacency[next].size() < 2u || adjacency[next].size() > 3u ||
                    std::find(path.begin(), path.end(), next) != path.end())
                    continue;
                path.push_back(next);
                visit(next);
                path.pop_back();
            }
        };
        visit(node.id);
    }
    std::sort(roundabouts.begin(), roundabouts.end());
    std::unordered_set<std::uint32_t> roundaboutNodes;
    for (const auto& cycle : roundabouts)
        for (const auto id : cycle) roundaboutNodes.insert(id);

    std::vector<EdgeSource> sources;
    sources.reserve(network.edges().size());
    std::size_t required = 0;
    for (const auto& cycle : roundabouts) {
        required += 1u;
        for (const auto id : cycle)
            if (adjacency[id].size() == 3u) ++required;
    }
    for (const auto& node : network.nodes()) {
        if (roundaboutNodes.contains(node.id)) continue;
        const auto arms = static_cast<std::size_t>(std::count_if(network.edges().begin(), network.edges().end(),
                                                                 [&](const RoadEdge& edge) {
                                                                     return edge.from == node.id || edge.to == node.id;
                                                                 }));
        if (arms >= 3u) required += arms == 3u ? 2u : 1u;
    }
    for (const auto& edge : network.edges()) {
        auto spline = edgeToSpline(edge);
        if (!spline.ok()) return Result<void>::failure(spline.status());
        auto lengthResult = spline.value().lengthResult(32);
        if (!lengthResult.ok()) return Result<void>::failure(lengthResult.status());
        auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
        if (!profile.ok()) return Result<void>::failure(profile.status());
        const float length = lengthResult.value();
        float       sideStart = edge.style.sideObjectStartOffset;
        float       sideEnd   = length - edge.style.sideObjectEndOffset;
        if (activeJunctionNodes.contains(edge.from)) {
            const auto node = network.nodeResult(edge.from);
            if (!node.ok()) return Result<void>::failure(node.status());
            sideStart = std::max(sideStart, junctionSocketDistance(network, node.value(), edge, length));
        }
        if (activeJunctionNodes.contains(edge.to)) {
            const auto node = network.nodeResult(edge.to);
            if (!node.ok()) return Result<void>::failure(node.status());
            sideEnd = std::min(sideEnd, length - junctionSocketDistance(network, node.value(), edge, length));
        }
        sideEnd                    = std::max(sideStart, sideEnd);
        const float placementLength = sideEnd - sideStart;
        const float firstSideDistance = options.sideObjectSpacing * 0.5f;
        const auto  sideSamples = placementLength <= firstSideDistance
                                      ? 0u
                                      : static_cast<std::size_t>(
                                            std::floor((placementLength - firstSideDistance) /
                                                       options.sideObjectSpacing)) +
                                            1u;
        const auto enabledSides = static_cast<std::size_t>(edge.style.sideObjectsLeft) +
                                  static_cast<std::size_t>(edge.style.sideObjectsRight);
        required += 2u + sideSamples * enabledSides;
        if (required > static_cast<std::size_t>(options.maximumPlacements))
            return bakeFail<void>(DiagnosticCode::PreconditionViolation, "road placement budget exceeded",
                                  "placements");
        sources.push_back(
            {&edge, std::move(spline).takeValue(), length, profile.value().halfWidth, sideStart, sideEnd});
    }
    placements.reserve(required);

    for (const auto& cycle : roundabouts) {
        float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
        for (const auto id : cycle) {
            const auto node = network.nodeResult(id).value();
            centerX += node.x;
            centerY += node.y;
            centerZ += node.z;
        }
        const float divisor = static_cast<float>(cycle.size());
        centerX /= divisor;
        centerY /= divisor;
        centerZ /= divisor;
        const std::uint32_t roundaboutId = cycle.front();
        const auto entryCount = static_cast<std::int64_t>(
            std::count_if(cycle.begin(), cycle.end(), [&](std::uint32_t id) { return adjacency[id].size() == 3u; }));
        const auto pointNamespace = 0x524e444200000000ull | roundaboutId;
        auto appendRoundaboutPoint = [&](const RoadNode& node, V3 heading, const char* role,
                                         std::uint64_t ordinal) -> Result<void> {
            const int row = placements.add(node.x, node.y, node.z);
            placements.setNormal(row, 0.f, 1.f, 0.f);
            placements.setYaw(row, std::atan2(heading.x, heading.z) * 57.2957795f);
            placements.setPointSeed(row, deriveSeed(roundaboutId, std::string(role) + std::to_string(node.id)));
            auto status = placements.trySetPointId(row, derivePointId(pointNamespace, ordinal));
            if (!status.ok()) return status;
            status = placements.trySetStringAttribute(row, "road_role", role);
            if (!status.ok()) return status;
            status = placements.trySetIntAttribute(row, "road_node_id", node.id);
            if (!status.ok()) return status;
            status = placements.trySetIntAttribute(row, "road_roundabout_id", roundaboutId);
            if (!status.ok()) return status;
            status = placements.trySetIntAttribute(row, "road_arm_count", entryCount);
            if (!status.ok()) return status;
            return placements.trySetVectorAttribute(row, "road_direction", heading.x, heading.y, heading.z);
        };

        RoadNode center;
        center.x = centerX;
        center.y = centerY;
        center.z = centerZ;
        auto status = appendRoundaboutPoint(center, V3{0.f, 0.f, 1.f}, "junction.roundabout.center", 1u);
        if (!status.ok()) return status;
        std::uint64_t entryOrdinal = 2u;
        for (std::size_t i = 0; i < cycle.size(); ++i) {
            const auto node = network.nodeResult(cycle[i]).value();
            if (adjacency[node.id].size() != 3u) continue;
            V3        heading{node.x - centerX, 0.f, node.z - centerZ};
            for (const auto neighbor : adjacency[node.id]) {
                if (std::find(cycle.begin(), cycle.end(), neighbor) != cycle.end()) continue;
                const auto outside = network.nodeResult(neighbor).value();
                heading = V3{outside.x - node.x, 0.f, outside.z - node.z};
                break;
            }
            status = appendRoundaboutPoint(node, normalize(heading), "junction.roundabout.entry", entryOrdinal++);
            if (!status.ok()) return status;
        }
    }

    for (const auto& node : network.nodes()) {
        if (roundaboutNodes.contains(node.id)) continue;
        std::vector<V3> directions;
        for (const auto& edge : network.edges()) {
            auto appendDirection = [&](const RoadControlPoint& point) {
                const V3 direction{point.x - node.x, 0.f, point.z - node.z};
                if (dot(direction, direction) <= 1e-6f) return false;
                directions.push_back(normalize(direction));
                return true;
            };
            if (edge.from == node.id) {
                for (std::size_t i = 1u; i < edge.controlPoints.size(); ++i)
                    if (appendDirection(edge.controlPoints[i])) break;
            } else if (edge.to == node.id) {
                for (std::size_t i = edge.controlPoints.size() - 1u; i-- > 0u;)
                    if (appendDirection(edge.controlPoints[i])) break;
            }
        }
        if (directions.size() < 3u) continue;

        int oppositePairs = 0;
        for (std::size_t a = 0; a < directions.size(); ++a)
            for (std::size_t b = a + 1u; b < directions.size(); ++b)
                if (dot(directions[a], directions[b]) < -0.85f) ++oppositePairs;

        const char* role = "junction.multi";
        if (directions.size() == 3u) role = oppositePairs > 0 ? "junction.t" : "junction.y";
        if (directions.size() == 4u) role = oppositePairs >= 2 ? "junction.x" : "junction.multi";

        V3 heading{};
        if (directions.size() == 3u && oppositePairs > 0) {
            float leastOpposed = -2.f;
            for (const auto& candidate : directions) {
                float closest = 1.f;
                for (const auto& other : directions) {
                    if (&candidate != &other) closest = std::min(closest, dot(candidate, other));
                }
                if (closest > leastOpposed) {
                    leastOpposed = closest;
                    heading      = candidate;
                }
            }
        } else {
            heading = directions.front();
        }

        const int row = placements.add(node.x, node.y, node.z);
        placements.setNormal(row, 0.f, 1.f, 0.f);
        placements.setYaw(row, std::atan2(heading.x, heading.z) * 57.2957795f);
        placements.setPointSeed(row, deriveSeed(node.id, role));
        auto status = placements.trySetPointId(row, derivePointId(0x4a554e4300000000ull | node.id, 1u));
        if (!status.ok()) return status;
        status = placements.trySetStringAttribute(row, "road_role", role);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_node_id", node.id);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_arm_count", static_cast<std::int64_t>(directions.size()));
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_side", 0);
        if (!status.ok()) return status;
        status = placements.trySetVectorAttribute(row, "road_direction", heading.x, heading.y, heading.z);
        if (!status.ok()) return status;

        if (directions.size() == 3u) {
            const float islandDistance = std::max(1.f, node.junctionRadius * 0.45f);
            const int   islandRow = placements.add(node.x + heading.x * islandDistance, node.y,
                                                   node.z + heading.z * islandDistance);
            placements.setNormal(islandRow, 0.f, 1.f, 0.f);
            placements.setYaw(islandRow, std::atan2(heading.x, heading.z) * 57.2957795f);
            constexpr const char* islandRole = "junction.channelizing.island";
            placements.setPointSeed(islandRow, deriveSeed(node.id, islandRole));
            status = placements.trySetPointId(islandRow,
                                              derivePointId(0x4a554e4300000000ull | node.id, 2u));
            if (!status.ok()) return status;
            status = placements.trySetStringAttribute(islandRow, "road_role", islandRole);
            if (!status.ok()) return status;
            status = placements.trySetIntAttribute(islandRow, "road_node_id", node.id);
            if (!status.ok()) return status;
            status = placements.trySetIntAttribute(islandRow, "road_arm_count", 3);
            if (!status.ok()) return status;
            status = placements.trySetFloatAttribute(islandRow, "road_distance", islandDistance);
            if (!status.ok()) return status;
            status = placements.trySetVectorAttribute(islandRow, "road_direction", heading.x, heading.y,
                                                      heading.z);
            if (!status.ok()) return status;
        }
    }

    std::vector<V3> acceptedSidePoints;
    for (const auto& source : sources) {
        std::uint64_t ordinal = 1;
        const auto    pointNamespace = 0x524f414400000000ull | source.edge->id;
        auto append = [&](float distance, float lateral, const char* role, int side,
                          bool avoidConflicts) -> Result<bool> {
            auto frame = source.spline.travelFrameResult(distance, "clamp", 32);
            if (!frame.ok()) return Result<bool>::failure(frame.status());
            const auto& f = frame.value();
            const V3 point{f.sample.x + f.sideX * lateral, f.sample.y + f.sideY * lateral,
                           f.sample.z + f.sideZ * lateral};
            if (avoidConflicts) {
                for (const auto& other : sources) {
                    if (other.edge->id == source.edge->id) continue;
                    auto closest = other.spline.closestPointResult(point.x, point.y, point.z, 32);
                    if (!closest.ok()) return Result<bool>::failure(closest.status());
                    const V3 delta{point.x - closest.value().x, point.y - closest.value().y,
                                   point.z - closest.value().z};
                    if (length(delta) < other.halfWidth + options.sideObjectClearance)
                        return Result<bool>::success(false);
                }
                for (const auto& accepted : acceptedSidePoints)
                    if (length(point - accepted) < options.sideObjectClearance) return Result<bool>::success(false);
            }
            const int row = placements.add(point.x, point.y, point.z);
            placements.setNormal(row, f.upX, f.upY, f.upZ);
            placements.setYaw(row, std::atan2(f.forwardX, f.forwardZ) * 57.2957795f);
            placements.setPointSeed(row, deriveSeed(source.edge->id, std::string(role) + std::to_string(ordinal)));
            auto identified = placements.trySetPointId(row, derivePointId(pointNamespace, ordinal++));
            if (!identified.ok()) return Result<bool>::failure(identified.status());
            auto edgeId = placements.trySetIntAttribute(row, "road_edge_id", source.edge->id);
            if (!edgeId.ok()) return Result<bool>::failure(edgeId.status());
            auto pointRole = placements.trySetStringAttribute(row, "road_role", role);
            if (!pointRole.ok()) return Result<bool>::failure(pointRole.status());
            auto pointSide = placements.trySetIntAttribute(row, "road_side", side);
            if (!pointSide.ok()) return Result<bool>::failure(pointSide.status());
            auto along = placements.trySetFloatAttribute(row, "road_distance", distance);
            if (!along.ok()) return Result<bool>::failure(along.status());
            auto clearance = placements.trySetFloatAttribute(row, "road_clearance", options.sideObjectClearance);
            if (!clearance.ok()) return Result<bool>::failure(clearance.status());
            auto direction = placements.trySetVectorAttribute(row, "road_direction", f.forwardX, f.forwardY,
                                                              f.forwardZ);
            if (!direction.ok()) return Result<bool>::failure(direction.status());
            if (avoidConflicts) acceptedSidePoints.push_back(point);
            return Result<bool>::success(true);
        };

        auto start = append(0.f, 0.f, "transition.start", 0, false);
        if (!start.ok()) return Result<void>::failure(start.status());
        auto end = append(source.length, 0.f, "transition.end", 0, false);
        if (!end.ok()) return Result<void>::failure(end.status());
        const float lateral = source.halfWidth + options.sideObjectOffset;
        for (float distance = source.sideStart + options.sideObjectSpacing * 0.5f; distance < source.sideEnd;
             distance += options.sideObjectSpacing) {
            if (source.edge->style.sideObjectsLeft) {
                auto left = append(distance, -lateral, "side.left", -1, true);
                if (!left.ok()) return Result<void>::failure(left.status());
            }
            if (source.edge->style.sideObjectsRight) {
                auto right = append(distance, lateral, "side.right", 1, true);
                if (!right.ok()) return Result<void>::failure(right.status());
            }
        }
    }
    return detail::bakeTrafficControlPoints(placements, network, options);
}

}  // namespace

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
    if (name == "asphalt") return {0.28f, 0.28f, 0.30f, 1.f};
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
            if (found == incidentEdges.end() || found->second.size() < 2) continue;
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
        const int laneCount = edge.lanesForward + edge.lanesBackward;
        if (laneCount >= 6 && pathLength.value() - start - end <= 1.001f)
            return bakeFail<RoadBakeResult>(DiagnosticCode::PreconditionViolation,
                                            "junction socket trims consume the complete road edge", "edge");
    }
    for (const auto& edge : network.edges()) {
        auto baked =
            bakeEdgeGeometry(result.mesh, result.overlay, network, edge, options, activeJunctionNodes, terminalNodes);
        if (!baked.ok()) return Result<RoadBakeResult>::failure(baked.status());
        if (exceedsMeshBudget(result.mesh, options))
            return bakeFail<RoadBakeResult>(DiagnosticCode::PreconditionViolation,
                                            "road edges exceed the mesh element budget", "mesh");
    }
    if (options.includeJunctions) {
        for (const auto& node : network.nodes()) {
            if (!activeJunctionNodes.contains(node.id)) continue;
            auto junction = bakeJunction(result.mesh, network, node, options);
            if (!junction.ok()) return Result<RoadBakeResult>::failure(junction.status());
            if (exceedsMeshBudget(result.mesh, options))
                return bakeFail<RoadBakeResult>(DiagnosticCode::PreconditionViolation,
                                                "road junctions exceed the mesh element budget", "mesh");
        }
    }
    if (options.includeNavigation) {
        auto turns = bakeTurnOverlays(result.mesh, result.overlay, network, options, activeJunctionNodes);
        if (!turns.ok()) return Result<RoadBakeResult>::failure(turns.status());
    }
    if (options.includePlacements) {
        auto placements = bakePlacementPoints(result.placements, network, options, activeJunctionNodes);
        if (!placements.ok()) return Result<RoadBakeResult>::failure(placements.status());
    }

    auto painted = paintGroupVertexColors(result.mesh);
    if (!painted.ok()) return Result<RoadBakeResult>::failure(painted.status());

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
