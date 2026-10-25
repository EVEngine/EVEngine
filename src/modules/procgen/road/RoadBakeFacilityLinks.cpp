#include "procgen/road/RoadAuthoringInternal.h"
#include "procgen/road/RoadBakeInternal.h"
#include "procgen/road/RoadConnectionInternal.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

namespace eve::procgen::road::detail {
namespace {
struct Peer {
    const RoadEdge*               edge;
    const RoadContinuousFacility* facility;
};
template <class Mesh>
bool samePrefabMesh(const Mesh& a, const Mesh& b) {
    if (a.assetId != b.assetId || std::fabs(a.length - b.length) > 1e-4f || a.vertices.size() != b.vertices.size() ||
        a.triangles.size() != b.triangles.size())
        return false;
    for (std::size_t i = 0; i < a.vertices.size(); ++i) {
        const auto& x = a.vertices[i];
        const auto& y = b.vertices[i];
        const float av[]{x.x, x.y, x.z, x.nx, x.ny, x.nz, x.u, x.v}, bv[]{y.x, y.y, y.z, y.nx, y.ny, y.nz, y.u, y.v};
        for (int k = 0; k < 8; ++k)
            if (std::fabs(av[k] - bv[k]) > 1e-4f) return false;
    }
    for (std::size_t i = 0; i < a.triangles.size(); ++i) {
        const auto& x = a.triangles[i];
        const auto& y = b.triangles[i];
        if (x.a != y.a || x.b != y.b || x.c != y.c || x.material != y.material || x.smoothingGroup != y.smoothingGroup)
            return false;
    }
    return true;
}
bool isCompatible(const RoadEdge& first, const RoadContinuousFacility& a, const RoadEdge& second,
                  const RoadContinuousFacility& b, std::uint32_t nodeId) {
    if (a.prefab.vertices.empty() != b.prefab.vertices.empty() || a.prefab.assetId != b.prefab.assetId) return false;
    const float firstSign = first.to == nodeId ? 1.f : -1.f, secondSign = second.from == nodeId ? 1.f : -1.f;
    if (!a.prefab.vertices.empty()) {
        auto x = a.prefab, y = b.prefab;
        if (firstSign < 0.f) reverseFacilityPrefab(x);
        if (secondSign < 0.f) reverseFacilityPrefab(y);
        if (x.automaticSpacing != y.automaticSpacing || x.averageSpacing != y.averageSpacing ||
            (!x.averageSpacing && x.reverseRepeatSequence != y.reverseRepeatSequence) || !samePrefabMesh(x, y) ||
            !samePrefabMesh(x.startMesh, y.startMesh) || !samePrefabMesh(x.endMesh, y.endMesh))
            return false;
    }
    if (std::fabs(a.lateralOffset * firstSign - b.lateralOffset * secondSign) > 1e-4f ||
        std::fabs(a.verticalOffset - b.verticalOffset) > 1e-4f || std::fabs(a.uvMeters - b.uvMeters) > 1e-4f ||
        a.profile.size() != b.profile.size())
        return false;
    auto profileA = a.profile, profileB = b.profile;
    if (firstSign < 0.f) reverseSectionGeometry(profileA, true);
    if (secondSign < 0.f) reverseSectionGeometry(profileB, true);
    for (std::size_t i = 0; i < profileA.size(); ++i)
        if (std::fabs(profileA[i].side - profileB[i].side) > 1e-4f ||
            std::fabs(profileA[i].up - profileB[i].up) > 1e-4f || profileA[i].material != profileB[i].material)
            return false;
    return true;
}
std::optional<Peer> peer(const RoadNetwork& network, std::uint32_t nodeId, const RoadEdge& edge,
                         const RoadContinuousFacility& facility) {
    if ((edge.from == nodeId && facility.start > 1e-5f) || (edge.to == nodeId && facility.end < 1.f - 1e-5f))
        return std::nullopt;
    const RoadEdge* other = nullptr;
    const auto      node  = network.nodeResult(nodeId);
    if (!node.ok()) return std::nullopt;
    if (!node.value().connection.facilityRoutes.empty()) {
        for (const auto& route : node.value().connection.facilityRoutes) {
            std::uint32_t otherId = 0, facilityId = 0;
            if (route.firstEdge == edge.id && route.firstFacility == facility.id) {
                otherId    = route.secondEdge;
                facilityId = route.secondFacility;
            } else if (route.secondEdge == edge.id && route.secondFacility == facility.id) {
                otherId    = route.firstEdge;
                facilityId = route.firstFacility;
            }
            for (const auto& item : network.edges())
                if (item.id == otherId)
                    for (const auto& f : item.style.facilities)
                        if (f.id == facilityId) return Peer{&item, &f};
        }
        return std::nullopt;
    }
    int count = 0;
    for (const auto& item : network.edges())
        if (item.from == nodeId || item.to == nodeId) {
            ++count;
            if (item.id != edge.id) other = &item;
        }
    if (count != 2 || !other) return std::nullopt;
    for (const auto& candidate : other->style.facilities) {
        if ((other->from == nodeId && candidate.start > 1e-5f) || (other->to == nodeId && candidate.end < 1.f - 1e-5f))
            continue;
        if (facility.id == candidate.id && isCompatible(edge, facility, *other, candidate, nodeId))
            return Peer{other, &candidate};
    }
    return std::nullopt;
}
V3 shifted(const SplineFrameSample& frame, const RoadEdge& edge, const RoadContinuousFacility& facility) {
    const float lateral = scaledProfileSide(facility.lateralOffset, asphaltHalfWidth(edge), frame.sample.scaleX);
    return V3{frame.sample.x, frame.sample.y, frame.sample.z} + V3{frame.sideX, frame.sideY, frame.sideZ} * lateral +
           V3{frame.upX, frame.upY, frame.upZ} * facility.verticalOffset;
}
}  // namespace
Result<void> validateFacilityRoutes(const RoadNetwork& network, const RoadNode& node) {
    if (node.connection.facilityRoutes.size() > 128u)
        return bakeFail<void>(DiagnosticCode::InvalidArgument, "too many facility routes", "facilityRoutes");
    std::set<std::pair<std::uint32_t, std::uint32_t>> endpoints;
    auto lookup = [&](std::uint32_t edgeId, std::uint32_t facilityId) -> std::optional<Peer> {
        for (const auto& edge : network.edges()) {
            if (edge.id != edgeId || (edge.from != node.id && edge.to != node.id)) continue;
            for (const auto& f : edge.style.facilities)
                if (f.id == facilityId && (edge.from != node.id || f.start <= 1e-5f) &&
                    (edge.to != node.id || f.end >= 1.f - 1e-5f))
                    return Peer{&edge, &f};
        }
        return std::nullopt;
    };
    for (const auto& route : node.connection.facilityRoutes) {
        const auto a = lookup(route.firstEdge, route.firstFacility), b = lookup(route.secondEdge, route.secondFacility);
        if (route.firstEdge == route.secondEdge || !a || !b ||
            !endpoints.insert({route.firstEdge, route.firstFacility}).second ||
            !endpoints.insert({route.secondEdge, route.secondFacility}).second ||
            !isCompatible(*a->edge, *a->facility, *b->edge, *b->facility, node.id))
            return bakeFail<void>(DiagnosticCode::PreconditionViolation,
                                  "facility route has stale, duplicate, clipped or incompatible endpoints",
                                  "facilityRoutes");
    }
    return Result<void>::success();
}
bool isFacilityContinued(const RoadNetwork& network, std::uint32_t nodeId, const RoadEdge& edge,
                         const RoadContinuousFacility& facility) {
    return peer(network, nodeId, edge, facility).has_value();
}
Result<void> bakeFacilityContinuations(MeshBuild& mesh, const RoadNetwork& network, const RoadBakeOptions& options,
                                       const std::unordered_set<std::uint32_t>& activeNodes) {
    for (const auto& node : network.nodes()) {
        if (!activeNodes.contains(node.id)) continue;
        for (const auto& edge : network.edges()) {
            if (edge.from != node.id && edge.to != node.id) continue;
            for (const auto& facility : edge.style.facilities) {
                const auto other = peer(network, node.id, edge, facility);
                if (!other || edge.id > other->edge->id) continue;
                auto firstPath = edgeToSpline(edge), secondPath = edgeToSpline(*other->edge);
                if (!firstPath.ok()) return Result<void>::failure(firstPath.status());
                if (!secondPath.ok()) return Result<void>::failure(secondPath.status());
                auto firstLength  = firstPath.value().lengthResult(24),
                     secondLength = secondPath.value().lengthResult(24);
                if (!firstLength.ok()) return Result<void>::failure(firstLength.status());
                if (!secondLength.ok()) return Result<void>::failure(secondLength.status());
                const float firstDistance  = junctionSocketDistance(network, node, edge, firstLength.value());
                const float secondDistance = junctionSocketDistance(network, node, *other->edge, secondLength.value());
                auto        first          = firstPath.value().travelFrameResult(
                    edge.from == node.id ? firstDistance : firstLength.value() - firstDistance, "clamp", 24);
                auto second = secondPath.value().travelFrameResult(
                    other->edge->from == node.id ? secondDistance : secondLength.value() - secondDistance, "clamp", 24);
                if (!first.ok()) return Result<void>::failure(first.status());
                if (!second.ok()) return Result<void>::failure(second.status());
                if (node.connection.ports.empty()) {
                    const auto plane = fitJunctionPlane(network, node);
                    blendFrameToJunctionPlane(first.value(), plane, 1.f);
                    blendFrameToJunctionPlane(second.value(), plane, 1.f);
                }
                const auto& a  = first.value();
                const auto& b  = second.value();
                const V3    p0 = shifted(a, edge, facility), p1 = shifted(b, *other->edge, *other->facility);
                const float span = length(p1 - p0);
                if (span < 1e-5f) continue;
                const V3 f0 = V3{a.forwardX, a.forwardY, a.forwardZ} * (edge.to == node.id ? 1.f : -1.f);
                const V3 f1 = V3{b.forwardX, b.forwardY, b.forwardZ} * (other->edge->from == node.id ? 1.f : -1.f);
                const V3 control0 = p0 + f0 * (span / 3.f), control1 = p1 - f1 * (span / 3.f);
                std::vector<SplineFrameSample> frames;
                for (int i = 0; i <= options.turnSamples; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(options.turnSamples), u = 1.f - t;
                    const V3 position = p0 * (u * u * u) + control0 * (3.f * u * u * t) + control1 * (3.f * u * t * t) +
                                        p1 * (t * t * t);
                    const V3 forward = normalize((control0 - p0) * (u * u) + (control1 - control0) * (2.f * u * t) +
                                                 (p1 - control1) * (t * t));
                    const V3 upHint  = normalize(
                        V3{std::lerp(a.upX, b.upX, t), std::lerp(a.upY, b.upY, t), std::lerp(a.upZ, b.upZ, t)});
                    const V3          side = normalize(cross(upHint, forward)), up = normalize(cross(forward, side));
                    SplineFrameSample frame;
                    frame.sample.x                  = position.x;
                    frame.sample.y                  = position.y;
                    frame.sample.z                  = position.z;
                    frame.sample.normalizedDistance = t;
                    frame.sideX                     = side.x;
                    frame.sideY                     = side.y;
                    frame.sideZ                     = side.z;
                    frame.upX                       = up.x;
                    frame.upY                       = up.y;
                    frame.upZ                       = up.z;
                    frame.forwardX                  = forward.x;
                    frame.forwardY                  = forward.y;
                    frame.forwardZ                  = forward.z;
                    frames.push_back(frame);
                }
                RoadProfile profile;
                profile.points            = facility.profile;
                profile.closed            = true;
                profile.explicitMaterials = true;
                if (edge.from == node.id) reverseSectionGeometry(profile.points, true);
                if (!facility.prefab.vertices.empty()) {
                    auto prefab = facility.prefab;
                    if (edge.from == node.id) reverseFacilityPrefab(prefab);
                    float       sourceLow = 0.f, sourceHigh = 1.f;
                    const auto& next = *other->facility;
                    const float nextStart =
                        other->edge->from == node.id ? next.prefab.sourceStart : 1.f - next.prefab.sourceEnd;
                    if (prefab.middleCopies == next.prefab.middleCopies &&
                        std::fabs(prefab.sourceEnd - nextStart) < 1e-4f) {
                        const float rawFirst =
                            std::lerp(facility.prefab.sourceStart, facility.prefab.sourceEnd,
                                      (a.sample.normalizedDistance - facility.start) / (facility.end - facility.start));
                        const float rawSecond =
                            std::lerp(next.prefab.sourceStart, next.prefab.sourceEnd,
                                      (b.sample.normalizedDistance - next.start) / (next.end - next.start));
                        sourceLow  = edge.to == node.id ? rawFirst : 1.f - rawFirst;
                        sourceHigh = other->edge->from == node.id ? rawSecond : 1.f - rawSecond;
                    }
                    float connectorLength = 0.f;
                    for (std::size_t i = 1; i < frames.size(); ++i)
                        connectorLength += length(V3{frames[i].sample.x - frames[i - 1].sample.x,
                                                     frames[i].sample.y - frames[i - 1].sample.y,
                                                     frames[i].sample.z - frames[i - 1].sample.z});
                    auto deformed =
                        bakeFacilityPrefab(mesh, prefab, frames, sourceLow, sourceHigh,
                                           connectorLength / (sourceHigh - sourceLow), options.maximumMeshElements);
                    if (!deformed.ok()) return deformed;
                    continue;
                }
                auto loft = loftProfileClean(mesh, frames, profile, span, facility.uvMeters, RoadMaterial::Curb,
                                             options.maximumMeshElements);
                if (!loft.ok()) return loft;
            }
        }
    }
    return Result<void>::success();
}
}  // namespace eve::procgen::road::detail
