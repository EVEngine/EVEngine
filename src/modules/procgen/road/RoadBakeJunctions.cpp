#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadBakeInternal.h"

#include "common/Diagnostic.h"
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

namespace eve::procgen::road::detail {

Result<void> bakeJunction(MeshBuild& mesh, const RoadNetwork& network, const RoadNode& node,
                          const RoadBakeOptions& options) {
    if (!node.connection.ports.empty()) return bakeCustomConnection(mesh, network, node, options);
    struct ArmTip {
        V3    origin;
        V3    outward;
        V3    authoredOutward;
        V3    side;
        V3    up;
        float asphaltHalf = 0.f;
        float curbWidth   = 0.f;
        float curbHeight  = 0.f;
        float sidewalkW   = 0.f;
        float sidewalkH   = 0.f;
        float uvMeters    = 8.f;
    };
    std::vector<ArmTip> arms;
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
        auto len = spline.value().lengthResult(24);
        if (!len.ok()) continue;
        const float trim  = junctionSocketDistance(network, node, edge, len.value());
        const float d     = edge.to == node.id ? len.value() - trim : trim;
        auto        frame = spline.value().travelFrameResult(d, "clamp", 24);
        if (!frame.ok()) continue;
        blendFrameToJunctionPlane(frame.value(), junctionPlane, 1.f);
        auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
        if (!profile.ok()) continue;
        const float asphaltHalf = asphaltHalfWidth(edge);
        const auto& f           = frame.value();
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
        tip.asphaltHalf        = asphaltHalf * f.sample.scaleX;
        const bool plainCustom = profile.value().explicitMaterials && profile.value().points.size() == 2u;
        tip.curbWidth          = plainCustom ? 0.f : edge.style.curbWidth;
        tip.curbHeight         = plainCustom ? 0.f : edge.style.curbHeight;
        tip.sidewalkW          = plainCustom ? 0.f : edge.style.sidewalkWidth;
        tip.sidewalkH          = plainCustom ? 0.f : edge.style.sidewalkHeight;
        tip.uvMeters           = edge.style.uvMeters;
        arms.push_back(tip);
        maxAsphalt        = std::max(maxAsphalt, tip.asphaltHalf);
        maxCurbWidth      = std::max(maxCurbWidth, tip.curbWidth);
        maxCurbHeight     = std::max(maxCurbHeight, tip.curbHeight);
        maxSidewalkWidth  = std::max(maxSidewalkWidth, tip.sidewalkW);
        maxSidewalkHeight = std::max(maxSidewalkHeight, tip.sidewalkH);
    }
    if (static_cast<int>(arms.size()) < 2) return Result<void>::success();

    float jr = node.junctionRadius;
    for (const auto& tip : arms) jr = std::min(jr, length(V3{tip.origin.x - node.x, 0.f, tip.origin.z - node.z}));

    // The circular fast path requires uniform mouth profiles. Mixed arms use
    // the general path, which preserves each mouth and interpolates its bands.
    bool axisCross = arms.size() == 4;
    if (axisCross) {
        int axisHits = 0;
        for (const auto& tip : arms) {
            const float dx = std::fabs(tip.origin.x - node.x);
            const float dz = std::fabs(tip.origin.z - node.z);
            if ((dx < 0.35f && dz > jr * 0.4f) || (dz < 0.35f && dx > jr * 0.4f)) ++axisHits;
        }
        const auto& first = arms.front();
        auto        same  = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
        axisCross         = axisHits == 4 && std::all_of(arms.begin(), arms.end(), [&](const ArmTip& tip) {
                        return same(tip.asphaltHalf, first.asphaltHalf) && same(tip.curbWidth, first.curbWidth) &&
                               same(tip.curbHeight, first.curbHeight) && same(tip.sidewalkW, first.sidewalkW) &&
                               same(tip.sidewalkH, first.sidewalkH);
                    });
    }

    // Asphalt fill. Fan from a hub vertex — a single large strip-quad can vanish
    // under Lavapipe even when the CPU triangles cover the origin.
    if (axisCross) {
        // Curb-return fillet: circle centers sit in the outer corner (jr,jr), so the
        // asphalt edge is concave and sidewalks wrap toward the property corner.
        const float ah           = maxAsphalt;
        const float cornerR      = std::max(0.75f, jr - ah);
        const int   segs         = circularArcSegments(cornerR, 1.5707963f, options.junctionChordError);
        const V3    upN          = junctionPlane.up;
        auto        surfacePoint = [&](float x, float z, float offset = 0.f) {
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

            const V3 hub = surfacePoint(node.x, node.z);
            for (std::size_t i = 0; i < rim.size(); ++i) {
                const auto& p0 = rim[i];
                const auto& p1 = rim[(i + 1) % rim.size()];
                appendOrientedTri(mesh, hub, surfacePoint(p0.first, p0.second), surfacePoint(p1.first, p1.second), upN,
                                  arms.front().uvMeters, RoadMaterial::Asphalt);
            }

            // Thin seal strips under each arm tip — hides residual sub-cm seams.
            const float seal    = 0.12f;
            auto        addSeal = [&](float x0, float z0, float x1, float z1, float x2, float z2, float x3, float z3) {
                appendOrientedQuad(mesh, surfacePoint(x0, z0), surfacePoint(x1, z1), surfacePoint(x2, z2),
                                          surfacePoint(x3, z3), upN, 0.f, 1.f, 0.f, 1.f, RoadMaterial::Asphalt);
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

        const float curbW = maxCurbWidth;
        const float walkW = maxSidewalkWidth;
        const float curbH = maxCurbHeight;
        const float walkH = maxSidewalkHeight;
        const float r0    = cornerR;
        const float r1    = std::max(0.2f, cornerR - curbW);
        const float r2    = std::max(0.15f, cornerR - curbW - walkW);
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

                    if (curbW > 1e-4f) {
                        appendOrientedQuad(mesh, surfacePoint(a0.x, a0.z, curbH), surfacePoint(b0o.x, b0o.z, curbH),
                                           surfacePoint(b1o.x, b1o.z, curbH), surfacePoint(a1.x, a1.z, curbH), upN, 0.f,
                                           1.f, 0.f, 1.f, RoadMaterial::Curb);
                        wall(a0, a1, 0.f, curbH, outToRoad, RoadMaterial::Curb);
                        wall(b0, b1, walkH, curbH, outToRoad, RoadMaterial::Curb);
                    }
                    if (walkW > 1e-4f) {
                        appendOrientedQuad(mesh, surfacePoint(b0i.x, b0i.z, walkH), surfacePoint(c0.x, c0.z, walkH),
                                           surfacePoint(c1.x, c1.z, walkH), surfacePoint(b1i.x, b1i.z, walkH), upN, 0.f,
                                           1.f, 0.f, 1.f, RoadMaterial::Sidewalk);
                        wall(s0, s1, 0.f, walkH, outToRoad, RoadMaterial::Sidewalk);
                    }
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

            // Concave returns intersect behind both outward socket rays, toward
            // the hub. Forward or remote intersections must remain straight.
            const V3    end         = next.origin - next.side * next.asphaltHalf;
            const float denominator = tip.outward.x * next.outward.z - tip.outward.z * next.outward.x;
            if (arms.size() >= 3 && std::fabs(denominator) > 1e-4f) {
                const V3    delta        = end - counterClockwise;
                const float alongTip     = (delta.x * next.outward.z - delta.z * next.outward.x) / denominator;
                const float alongNext    = (delta.x * tip.outward.z - delta.z * tip.outward.x) / denominator;
                const float maximumMiter = node.junctionRadius * 2.f + tip.asphaltHalf + next.asphaltHalf;
                if (alongTip < 0.f && alongNext < 0.f && -alongTip < maximumMiter && -alongNext < maximumMiter) {
                    V3 control        = counterClockwise + tip.outward * alongTip;
                    control.y         = planeHeight(junctionPlane, control.x, control.z);
                    const float angle = std::acos(std::clamp(dot(tip.outward, next.outward), -1.f, 1.f));
                    const int   segments =
                        quadraticSegments(counterClockwise, control, end, angle, options.junctionChordError);
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
        if (triangulateBoundary(rim, triangles, BoundaryPointPolicy::IgnoreCollinear) !=
            TriangulationStatus::Complete) {
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
            if (triangulateBoundary(rim, triangles, BoundaryPointPolicy::IgnoreCollinear) !=
                TriangulationStatus::Complete)
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
                std::array<V3, 4> corners = {a.origin - a.side * a.asphaltHalf, a.origin + a.side * a.asphaltHalf,
                                             b.origin - b.side * b.asphaltHalf, b.origin + b.side * b.asphaltHalf};
                V3                center{};
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
        const V3    nrm     = normalize(V3{f.upX, f.upY, f.upZ});
        const float stripeW = edge.style.laneWidth * static_cast<float>(edge.lanesForward + edge.lanesBackward) *
                              frame.value().sample.scaleX * 0.45f;
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


}  // namespace eve::procgen::road::detail
