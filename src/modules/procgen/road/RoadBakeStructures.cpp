#include "procgen/road/RoadBakeInternal.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen::road::detail {
namespace {
SplineFrameSample interpolate(const SplineFrameSample& a, const SplineFrameSample& b, float fraction) {
    const float t =
        (fraction - a.sample.normalizedDistance) / (b.sample.normalizedDistance - a.sample.normalizedDistance);
    auto out                      = a;
    out.authoredMarker            = false;
    out.sample.x                  = std::lerp(a.sample.x, b.sample.x, t);
    out.sample.y                  = std::lerp(a.sample.y, b.sample.y, t);
    out.sample.z                  = std::lerp(a.sample.z, b.sample.z, t);
    out.sample.scaleX             = std::lerp(a.sample.scaleX, b.sample.scaleX, t);
    out.sample.rollDegrees        = std::lerp(a.sample.rollDegrees, b.sample.rollDegrees, t);
    out.sample.normalizedDistance = fraction;
    const auto forward = normalize(V3{std::lerp(a.forwardX, b.forwardX, t), std::lerp(a.forwardY, b.forwardY, t),
                                      std::lerp(a.forwardZ, b.forwardZ, t)});
    const auto upHint =
        normalize(V3{std::lerp(a.upX, b.upX, t), std::lerp(a.upY, b.upY, t), std::lerp(a.upZ, b.upZ, t)});
    const auto side = normalize(cross(upHint, forward)), up = normalize(cross(forward, side));
    out.forwardX = forward.x;
    out.forwardY = forward.y;
    out.forwardZ = forward.z;
    out.sideX    = side.x;
    out.sideY    = side.y;
    out.sideZ    = side.z;
    out.upX      = up.x;
    out.upY      = up.y;
    out.upZ      = up.z;
    return out;
}
std::vector<SplineFrameSample> clipFrames(const std::vector<SplineFrameSample>& frames, float start, float end) {
    std::vector<SplineFrameSample> result;
    start = std::max(start, frames.front().sample.normalizedDistance);
    end   = std::min(end, frames.back().sample.normalizedDistance);
    if (end - start <= 1e-6f) return result;
    auto sample = [&](float fraction) {
        for (std::size_t i = 1; i < frames.size(); ++i) {
            if (fraction <= frames[i].sample.normalizedDistance + 1e-7f) {
                if (std::fabs(fraction - frames[i].sample.normalizedDistance) < 1e-7f) return frames[i];
                if (fraction <= frames[i - 1].sample.normalizedDistance) return frames[i - 1];
                return interpolate(frames[i - 1], frames[i], fraction);
            }
        }
        return frames.back();
    };
    result.push_back(sample(start));
    for (const auto& f : frames)
        if (f.sample.normalizedDistance > start + 1e-7f && f.sample.normalizedDistance < end - 1e-7f)
            result.push_back(f);
    result.push_back(sample(end));
    return result;
}
}  // namespace
Result<void> bakeStructuralGeometry(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames, const RoadEdge& edge,
                                    const RoadProfile& profile, float pathLength, const RoadBakeOptions& options) {
    std::vector<float> boundaries{frames.front().sample.normalizedDistance, frames.back().sample.normalizedDistance};
    for (const auto& interval : edge.style.structures) {
        if (interval.start > boundaries.front() && interval.start < frames.back().sample.normalizedDistance)
            boundaries.push_back(interval.start);
        if (interval.end > boundaries.front() && interval.end < frames.back().sample.normalizedDistance)
            boundaries.push_back(interval.end);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    for (std::size_t i = 1; i < boundaries.size(); ++i) {
        auto chunk = clipFrames(frames, boundaries[i - 1], boundaries[i]);
        if (chunk.size() < 2) continue;
        const float                  middle    = (boundaries[i - 1] + boundaries[i]) * 0.5f;
        const RoadStructureInterval* structure = nullptr;
        for (const auto& item : edge.style.structures)
            if (middle >= item.start && middle < item.end) {
                structure = &item;
                break;
            }
        const auto material = structure ? (structure->kind == RoadStructureKind::Bridge ? RoadMaterial::BridgeAsphalt
                                                                                        : RoadMaterial::TunnelAsphalt)
                                        : RoadMaterial::Asphalt;
        if (structure && structure->kind == RoadStructureKind::Tunnel) {
            const float startEnd =
                std::min(structure->end, structure->start + structure->portalStartLength / pathLength);
            const float endStart = std::max(structure->start, structure->end - structure->portalEndLength / pathLength);
            std::vector<float> cuts{chunk.front().sample.normalizedDistance, chunk.back().sample.normalizedDistance};
            for (float cut : {startEnd, endStart})
                if (cut > cuts.front() && cut < chunk.back().sample.normalizedDistance) cuts.push_back(cut);
            std::sort(cuts.begin(), cuts.end());
            cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
            for (std::size_t j = 1; j < cuts.size(); ++j) {
                const float center = (cuts[j - 1] + cuts[j]) * .5f;
                const auto  tag    = center < startEnd || center > endStart ? RoadMaterial::TunnelPortalAsphalt
                                                                            : RoadMaterial::TunnelAsphalt;
                auto        loft = loftProfileClean(mesh, clipFrames(chunk, cuts[j - 1], cuts[j]), profile, pathLength,
                                                    edge.style.uvMeters, tag, options.maximumMeshElements,
                                                    edge.style.crossSectionSubdivisions);
                if (!loft.ok()) return loft;
            }
        } else {
            auto loft = loftProfileClean(mesh, chunk, profile, pathLength, edge.style.uvMeters, material,
                                         options.maximumMeshElements, edge.style.crossSectionSubdivisions);
            if (!loft.ok()) return loft;
        }
        if ((!structure && edge.style.structures.empty() && edge.style.automaticStructures) ||
            (structure && structure->kind == RoadStructureKind::Bridge)) {
            auto deck = addDeckAndPiers(mesh, chunk, edge.style, profile.halfWidth, asphaltHalfWidth(edge),
                                        options.includePiers, options.maximumMeshElements);
            if (!deck.ok()) return deck;
        }
        if (structure && structure->kind == RoadStructureKind::Tunnel) {
            const float w = profile.halfWidth, h = structure->tunnelHeight, t = structure->wallThickness;
            RoadProfile shell;
            shell.closed            = true;
            shell.explicitMaterials = true;
            shell.halfWidth         = w + t;
            shell.points            = {{-w, 0.f, RoadMaterial::Tunnel},       {-w, h, RoadMaterial::Tunnel},
                                       {w, h, RoadMaterial::Tunnel},          {w, 0.f, RoadMaterial::Tunnel},
                                       {w + t, 0.f, RoadMaterial::Tunnel},    {w + t, h + t, RoadMaterial::Tunnel},
                                       {-w - t, h + t, RoadMaterial::Tunnel}, {-w - t, 0.f, RoadMaterial::Tunnel}};
            auto tunnel = loftProfileClean(mesh, chunk, shell, pathLength, edge.style.uvMeters, RoadMaterial::Tunnel,
                                           options.maximumMeshElements);
            if (!tunnel.ok()) return tunnel;
            {
                auto capped = capProfileRing(mesh, chunk.front(), shell, false);
                if (!capped.ok()) return capped;
            }
            {
                auto capped = capProfileRing(mesh, chunk.back(), shell, true);
                if (!capped.ok()) return capped;
            }
        }
        if (isMeshBudgetExceeded(mesh, options))
            return bakeFail<void>(DiagnosticCode::PreconditionViolation, "structures exceed mesh budget", "mesh");
    }
    return Result<void>::success();
}
Result<void> bakeContinuousFacilities(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames,
                                      const RoadNetwork& network, const RoadEdge& edge, float pathLength,
                                      const RoadBakeOptions&                   options,
                                      const std::unordered_set<std::uint32_t>& activeNodes) {
    for (const auto& facility : edge.style.facilities) {
        auto chunk = clipFrames(frames, facility.start, facility.end);
        if (chunk.size() < 2) continue;
        for (auto& frame : chunk) {
            const float lateral =
                scaledProfileSide(facility.lateralOffset, asphaltHalfWidth(edge), frame.sample.scaleX);
            frame.sample.x += frame.sideX * lateral + frame.upX * facility.verticalOffset;
            frame.sample.y += frame.sideY * lateral + frame.upY * facility.verticalOffset;
            frame.sample.z += frame.sideZ * lateral + frame.upZ * facility.verticalOffset;
            frame.sample.scaleX = 1.f;
        }
        RoadProfile section;
        section.points            = facility.profile;
        section.closed            = true;
        section.explicitMaterials = true;
        if (!facility.prefab.vertices.empty()) {
            const float low =
                std::lerp(facility.prefab.sourceStart, facility.prefab.sourceEnd,
                          (chunk.front().sample.normalizedDistance - facility.start) / (facility.end - facility.start));
            const float high =
                std::lerp(facility.prefab.sourceStart, facility.prefab.sourceEnd,
                          (chunk.back().sample.normalizedDistance - facility.start) / (facility.end - facility.start));
            auto deformed = bakeFacilityPrefab(mesh, facility.prefab, chunk, low, high,
                                               pathLength * (facility.end - facility.start) /
                                                   (facility.prefab.sourceEnd - facility.prefab.sourceStart),
                                               options.maximumMeshElements);
            if (!deformed.ok()) return deformed;
            continue;
        }
        auto loft = loftProfileClean(mesh, chunk, section, pathLength, facility.uvMeters, RoadMaterial::Curb,
                                     options.maximumMeshElements);
        if (!loft.ok()) return loft;
        if (facility.capStart &&
            !(activeNodes.contains(edge.from) && isFacilityContinued(network, edge.from, edge, facility))) {
            auto capped = capProfileRing(mesh, chunk.front(), section, false);
            if (!capped.ok()) return capped;
        }
        if (facility.capEnd &&
            !(activeNodes.contains(edge.to) && isFacilityContinued(network, edge.to, edge, facility))) {
            auto capped = capProfileRing(mesh, chunk.back(), section, true);
            if (!capped.ok()) return capped;
        }
        if (isMeshBudgetExceeded(mesh, options))
            return bakeFail<void>(DiagnosticCode::PreconditionViolation, "facilities exceed mesh budget", "mesh");
    }
    return Result<void>::success();
}
}  // namespace eve::procgen::road::detail
