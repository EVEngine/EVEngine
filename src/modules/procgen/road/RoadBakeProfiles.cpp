#include <algorithm>
#include <cmath>
#include "procgen/road/RoadBakeInternal.h"

namespace eve::procgen::road::detail {
namespace {
bool sectionTriangleHasArea(const RoadProfile& profile, float scale, std::size_t a, std::size_t b, std::size_t c) {
    const float  asphaltHalf = profileAsphaltHalfWidth(profile);
    const double originSide  = scaledProfileSide(profile.points[a].side, asphaltHalf, scale);
    const double ax   = static_cast<double>(scaledProfileSide(profile.points[b].side, asphaltHalf, scale)) - originSide;
    const double ay   = static_cast<double>(profile.points[b].up) - profile.points[a].up;
    const double bx   = static_cast<double>(scaledProfileSide(profile.points[c].side, asphaltHalf, scale)) - originSide;
    const double by   = static_cast<double>(profile.points[c].up) - profile.points[a].up;
    const double area = ax * by - ay * bx;
    return area * area > 1e-12;
}
}  // namespace
Result<void> loftProfileClean(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames,
                              const RoadProfile& sourceProfile, float pathLength, float uvMeters,
                              RoadMaterial asphaltMaterial, std::size_t budget, int subdivisions) {
    if (frames.size() < 2 || sourceProfile.points.size() < 2 || subdivisions < 1 || subdivisions > 32)
        return bakeFail<void>(DiagnosticCode::InvalidArgument, "invalid section subdivision input", "profile");
    RoadProfile        refined;
    const RoadProfile* selected = &sourceProfile;
    if (subdivisions > 1) {
        const std::size_t originalSegments =
            sourceProfile.closed ? sourceProfile.points.size() : sourceProfile.points.size() - 1;
        const std::size_t work = (frames.size() - 1) * originalSegments * static_cast<std::size_t>(subdivisions) * 10u;
        const auto        existing =
            static_cast<std::size_t>(mesh.getVertexCount()) + static_cast<std::size_t>(mesh.getIndexCount());
        if (existing > budget || work > budget - existing)
            return bakeFail<void>(DiagnosticCode::PreconditionViolation, "section subdivisions exceed mesh budget",
                                  "mesh");
        refined = sourceProfile;
        refined.points.clear();
        refined.explicitMaterials = true;
        refined.points.reserve(originalSegments * static_cast<std::size_t>(subdivisions) + 1);
        for (std::size_t i = 0; i < originalSegments; ++i) {
            const auto& a        = sourceProfile.points[i];
            const auto& b        = sourceProfile.points[(i + 1) % sourceProfile.points.size()];
            auto        material = a.material;
            if (!sourceProfile.explicitMaterials &&
                (a.material == RoadMaterial::Asphalt || b.material == RoadMaterial::Asphalt))
                material = std::fabs(a.up - b.up) > 1e-3f ? RoadMaterial::Curb : RoadMaterial::Asphalt;
            for (int j = 0; j < subdivisions; ++j) {
                const float t = static_cast<float>(j) / static_cast<float>(subdivisions);
                refined.points.push_back({std::lerp(a.side, b.side, t), std::lerp(a.up, b.up, t), material});
            }
        }
        if (!sourceProfile.closed) refined.points.push_back(sourceProfile.points.back());
        selected = &refined;
    }
    const auto& profile = *selected;
    if (frames.size() < 2 || profile.points.size() < 2)
        return bakeFail<void>(DiagnosticCode::InvalidArgument, "loft needs >=2 frames and profile points");

    const std::size_t segments = profile.closed ? profile.points.size() : profile.points.size() - 1;
    const std::size_t needed   = (frames.size() - 1) * segments * 10u;
    const auto        existing =
        static_cast<std::size_t>(mesh.getVertexCount()) + static_cast<std::size_t>(mesh.getIndexCount());
    if (existing > budget || needed > budget - existing)
        return bakeFail<void>(DiagnosticCode::PreconditionViolation, "profile loft exceeds mesh budget", "mesh");
    const int       ringCount    = static_cast<int>(frames.size());
    const int       profileCount = static_cast<int>(profile.points.size());
    std::vector<V3> ringPositions(static_cast<std::size_t>(ringCount * profileCount));

    float profileArea = 0.f;
    if (profile.closed)
        for (std::size_t point = 0; point < profile.points.size(); ++point) {
            const auto& a = profile.points[point];
            const auto& b = profile.points[(point + 1) % profile.points.size()];
            profileArea += a.side * b.up - a.up * b.side;
        }
    for (int ring = 0; ring < ringCount; ++ring) {
        const auto& frame = frames[static_cast<std::size_t>(ring)];
        const V3    origin{frame.sample.x, frame.sample.y, frame.sample.z};
        const V3    side{frame.sideX, frame.sideY, frame.sideZ};
        const V3    up{frame.upX, frame.upY, frame.upZ};
        for (int i = 0; i < profileCount; ++i) {
            const auto& pp = profile.points[static_cast<std::size_t>(i)];
            ringPositions[static_cast<std::size_t>(ring * profileCount + i)] =
                origin + side * scaledProfileSide(pp.side, profileAsphaltHalfWidth(profile), frame.sample.scaleX) +
                up * pp.up;
        }
    }

    for (int ring = 0; ring < ringCount - 1; ++ring) {
        // Spline samples retain their distance along the untrimmed edge. Using
        // that distance keeps texture phase stable when junction radii or bake
        // tessellation change; uvMeters is metres per repeat, not repeat count.
        const float u0 = frames[static_cast<std::size_t>(ring)].sample.normalizedDistance * pathLength / uvMeters;
        const float u1 = frames[static_cast<std::size_t>(ring + 1)].sample.normalizedDistance * pathLength / uvMeters;
        for (int i = 0; i < static_cast<int>(segments); ++i) {
            const auto& a    = profile.points[static_cast<std::size_t>(i)];
            const int   next = (i + 1) % profileCount;
            const auto& b    = profile.points[static_cast<std::size_t>(next)];
            // Prefer asphalt for near-horizontal driving strips (endpoint material alone
            // would tag the asphalt span as curb because the left corner is a curb drop).
            RoadMaterial mat = a.material;
            if (!profile.explicitMaterials && std::fabs(a.up - b.up) > 1e-3f &&
                (a.material == RoadMaterial::Asphalt || b.material == RoadMaterial::Asphalt))
                mat = RoadMaterial::Curb;
            if (!profile.explicitMaterials && std::fabs(a.up - b.up) <= 1e-3f &&
                (a.material == RoadMaterial::Asphalt || b.material == RoadMaterial::Asphalt)) {
                mat = RoadMaterial::Asphalt;
            }
            if (mat == RoadMaterial::Asphalt) mat = asphaltMaterial;
            const V3& p00    = ringPositions[static_cast<std::size_t>(ring * profileCount + i)];
            const V3& p01    = ringPositions[static_cast<std::size_t>(ring * profileCount + next)];
            const V3& p10    = ringPositions[static_cast<std::size_t>((ring + 1) * profileCount + i)];
            const V3& p11    = ringPositions[static_cast<std::size_t>((ring + 1) * profileCount + next)];
            V3        normal = normalize(cross(p10 - p00, p01 - p00));
            if (profile.closed && profileArea > 0.f) normal = normal * -1.f;
            const float v0 = a.side / uvMeters;
            const float v1 = b.side / uvMeters;
            appendOrientedQuad(mesh, p00, p10, p11, p01, normal, u0, u1, v0, v1, mat, QuadDiagonal::SecondToFourth);
        }
    }
    return Result<void>::success();
}

/** @brief Cap a lofted profile ring so junction-facing open U-channels are not see-through. */
Result<void> capProfileRing(MeshBuild& mesh, const SplineFrameSample& frame, const RoadProfile& profile, bool outward) {
    if (profile.points.size() < 3) return Result<void>::success();
    const V3 origin{frame.sample.x, frame.sample.y, frame.sample.z};
    const V3 side{frame.sideX, frame.sideY, frame.sideZ};
    const V3 up{frame.upX, frame.upY, frame.upZ};
    V3       fwd{frame.forwardX, frame.forwardY, frame.forwardZ};
    if (!outward) fwd = fwd * -1.f;
    const V3        nrm = normalize(fwd);
    std::vector<V3> ring;
    ring.reserve(profile.points.size());
    for (const auto& pp : profile.points)
        ring.push_back(origin +
                       side * scaledProfileSide(pp.side, profileAsphaltHalfWidth(profile), frame.sample.scaleX) +
                       up * pp.up);
    if (profile.explicitMaterials) {
        if (!profile.closed) return Result<void>::success();
        std::vector<V3> polygon;
        for (const auto& point : profile.points) polygon.push_back({point.side, 0.f, point.up});
        std::vector<std::array<std::size_t, 3>> triangles;
        if (triangulateBoundary(polygon, triangles, BoundaryPointPolicy::IncludeBoundary) !=
            TriangulationStatus::Complete)
            return bakeFail<void>(DiagnosticCode::InvalidArgument, "section cap cannot be triangulated", "profile");
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        mesh.setActiveGroup(roadMaterialGroup(profile.points.front().material == RoadMaterial::Asphalt
                                                  ? RoadMaterial::Deck
                                                  : profile.points.front().material));
        for (const auto& p : ring) mesh.addVertex(p.x, p.y, p.z, nrm.x, nrm.y, nrm.z, 0.f, 0.f);
        for (const auto& tri : triangles) {
            auto a = tri[0], b = tri[1], c = tri[2];
            if (dot(cross(ring[b] - ring[a], ring[c] - ring[a]), nrm) < 0.f) std::swap(b, c);
            mesh.addTriangle(base + static_cast<std::uint32_t>(a), base + static_cast<std::uint32_t>(b),
                             base + static_cast<std::uint32_t>(c));
        }
        return Result<void>::success();
    }
    // Fan-fill the end polygon (closes the jersey-barrier U looking into the hub).
    mesh.setActiveGroup(roadMaterialGroup(RoadMaterial::Curb));
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    for (const V3& p : ring) mesh.addVertex(p.x, p.y, p.z, nrm.x, nrm.y, nrm.z, 0.f, 0.f);
    for (std::size_t i = 1; i + 1 < ring.size(); ++i) {
        if (!sectionTriangleHasArea(profile, frame.sample.scaleX, 0, i, i + 1)) continue;
        if (outward)
            mesh.addTriangle(base, base + static_cast<std::uint32_t>(i), base + static_cast<std::uint32_t>(i + 1));
        else
            mesh.addTriangle(base, base + static_cast<std::uint32_t>(i + 1), base + static_cast<std::uint32_t>(i));
    }
    return Result<void>::success();
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
            const V3    p  = origin +
                         side * scaledProfileSide(pp.side, profileAsphaltHalfWidth(profile), frame.sample.scaleX) +
                         up * pp.up;
            mesh.addVertex(p.x, p.y, p.z, nrm.x, nrm.y, nrm.z, 0.f, 0.f);
        }
        const auto count = static_cast<std::uint32_t>(end - begin);
        for (std::uint32_t i = 1; i + 1 < count; ++i) {
            if (!sectionTriangleHasArea(profile, frame.sample.scaleX, begin, begin + i, begin + i + 1)) continue;
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


}  // namespace eve::procgen::road::detail
