#include "common/Identity.h"
#include "procgen/road/RoadAuthoringInternal.h"
#include "procgen/spline/SplinePath.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <unordered_set>

namespace eve::procgen::road::detail {
namespace {
Result<void> invalid(std::string message) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "style.authoring"));
}
float area(const RoadProfilePoint& a, const RoadProfilePoint& b, const RoadProfilePoint& c) {
    return (b.side - a.side) * (c.up - a.up) - (b.up - a.up) * (c.side - a.side);
}
bool isOnSegment(const RoadProfilePoint& a, const RoadProfilePoint& b, const RoadProfilePoint& p) {
    return std::fabs(area(a, b, p)) < 1e-5f && p.side >= std::min(a.side, b.side) - 1e-5f &&
           p.side <= std::max(a.side, b.side) + 1e-5f && p.up >= std::min(a.up, b.up) - 1e-5f &&
           p.up <= std::max(a.up, b.up) + 1e-5f;
}
bool isIntersecting(const RoadProfilePoint& a, const RoadProfilePoint& b, const RoadProfilePoint& c,
                    const RoadProfilePoint& d) {
    const float abC = area(a, b, c), abD = area(a, b, d), cdA = area(c, d, a), cdB = area(c, d, b);
    return (abC * abD < 0.f && cdA * cdB < 0.f) || isOnSegment(a, b, c) || isOnSegment(a, b, d) ||
           isOnSegment(c, d, a) || isOnSegment(c, d, b);
}
bool isValidInterval(float start, float end) {
    return std::isfinite(start) && std::isfinite(end) && start >= 0.f && end <= 1.f && end - start > 1e-5f;
}

template <class T>
std::vector<T> clip(const std::vector<T>& intervals, float low, float high, float pathLength) {
    std::vector<T> result;
    for (const auto& original : intervals) {
        const float a = std::max(low, original.start), b = std::min(high, original.end);
        if (b - a <= 1e-5f) continue;
        auto item  = original;
        item.start = (a - low) / (high - low);
        item.end   = (b - low) / (high - low);
        if constexpr (std::is_same_v<T, RoadContinuousFacility>) {
            if (item.prefab.automaticSpacing && item.prefab.repeatSourceLength == 0.f)
                item.prefab.repeatSourceLength = pathLength * (original.end - original.start) /
                                                 (original.prefab.sourceEnd - original.prefab.sourceStart);
            item.capStart = original.capStart && a == original.start;
            item.capEnd   = original.capEnd && b == original.end;
            if (!original.prefab.vertices.empty()) {
                item.prefab.sourceStart = std::lerp(original.prefab.sourceStart, original.prefab.sourceEnd,
                                                    (a - original.start) / (original.end - original.start));
                item.prefab.sourceEnd   = std::lerp(original.prefab.sourceStart, original.prefab.sourceEnd,
                                                    (b - original.start) / (original.end - original.start));
            }
        }
        if constexpr (std::is_same_v<T, RoadStructureInterval>) {
            item.portalStartLength = std::max(0.f, original.portalStartLength - (a - original.start) * pathLength);
            item.portalEndLength   = std::max(0.f, original.portalEndLength - (original.end - b) * pathLength);
        }
        result.push_back(std::move(item));
    }
    return result;
}
}  // namespace
void reverseSectionGeometry(std::vector<RoadProfilePoint>& section, bool closed) {
    if (section.empty()) return;
    const auto old = section;
    for (std::size_t i = 0; i < section.size(); ++i) {
        const std::size_t source = section.size() - 1 - i;
        section[i]               = old[source];
        section[i].side          = -section[i].side;
        if (source > 0)
            section[i].material = old[source - 1].material;
        else if (closed)
            section[i].material = old.back().material;
    }
}
Result<void> validateSection(const std::vector<RoadProfilePoint>& points, bool closed) {
    if (points.size() < (closed ? 3u : 2u) || points.size() > 128u)
        return invalid("section needs 2..128 points (3 for closed)");
    for (const auto& p : points)
        if (!std::isfinite(p.side) || !std::isfinite(p.up) || std::fabs(p.side) > 10000.f ||
            std::fabs(p.up) > 10000.f || p.material > RoadMaterial::TunnelPortalAsphalt)
            return invalid("section has invalid coordinates or materials");
    for (std::size_t i = closed ? 0u : 1u; i < (closed ? points.size() : points.size() - 1); ++i) {
        const auto& a = points[(i + points.size() - 1) % points.size()];
        const auto& b = points[i];
        const auto& c = points[(i + 1) % points.size()];
        if (std::fabs(area(a, b, c)) < 1e-5f &&
            (b.side - a.side) * (c.side - b.side) + (b.up - a.up) * (c.up - b.up) < 0.f)
            return invalid("section has overlapping adjacent segments");
    }
    const std::size_t count      = closed ? points.size() : points.size() - 1;
    float             signedArea = 0.f;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& a = points[i];
        const auto& b = points[(i + 1) % points.size()];
        if (std::hypot(a.side - b.side, a.up - b.up) < 1e-5f) return invalid("section has a zero-length segment");
        signedArea += a.side * b.up - a.up * b.side;
        for (std::size_t j = i + 2; j < count; ++j) {
            if (closed && i == 0 && j + 1 == count) continue;
            if (isIntersecting(a, b, points[j], points[(j + 1) % points.size()]))
                return invalid("section self-intersects");
        }
    }
    if (closed && std::fabs(signedArea) < 1e-5f) return invalid("closed section has zero area");
    return Result<void>::success();
}
Result<void> validateRoadAuthoring(const RoadStyle& style) {
    const auto& policy = style.terrainPolicy;
    if (!std::isfinite(policy.blendDistance) || policy.blendDistance < 0.f ||
        !std::isfinite(policy.maxVerticalDelta) || policy.maxVerticalDelta < 0.f || policy.splatLayer < -1 ||
        !std::isfinite(policy.splatOpacity) || policy.splatOpacity < 0.f || policy.splatOpacity > 1.f)
        return invalid("invalid road terrain author policy");
    const auto& terrain = style.sideTerrain;
    if (terrain.asset && terrain.asset->rootPose != eve::PrefabRootPose::Authored &&
        terrain.asset->rootPose != eve::PrefabRootPose::ReplaceTranslationRotation)
        return invalid("unknown side-object prefab root pose");
    if (terrain.asset && terrain.asset->buildId.isNil())
        return invalid("side-object package build identity must not be nil");
    if (terrain.mode > RoadSideTerrainMode::Average || terrain.alignment > RoadSideAlignment::PathNormal ||
        terrain.samplesPerAxis < 2 || terrain.samplesPerAxis > 17 || !std::isfinite(terrain.minSide) ||
        !std::isfinite(terrain.maxSide) || !std::isfinite(terrain.minForward) || !std::isfinite(terrain.maxForward) ||
        terrain.minSide > terrain.maxSide || terrain.minForward > terrain.maxForward ||
        std::max(std::fabs(terrain.minSide), std::fabs(terrain.maxSide)) > 10000.f ||
        std::max(std::fabs(terrain.minForward), std::fabs(terrain.maxForward)) > 10000.f)
        return invalid("invalid side-object terrain footprint or sampling mode");
    if (style.samplingPolicy > RoadSamplingPolicy::ParametricDistanceTurn ||
        (style.samplingPolicy == RoadSamplingPolicy::ParametricDistanceTurn && style.samplingAngleDegrees < 1.f))
        return invalid("unknown sampling policy or angle below the parametric minimum of one degree");
    if (!std::isfinite(style.samplingSpacingMeters) || style.samplingSpacingMeters < 0.f ||
        !std::isfinite(style.samplingAngleDegrees) || style.samplingAngleDegrees < 0.f ||
        style.samplingAngleDegrees > 180.f || style.crossSectionSubdivisions < 1 || style.crossSectionSubdivisions > 32)
        return invalid("road metric sampling settings are outside supported bounds");
    if (style.rollInterpolation > RoadRollInterpolation::SmoothStep) return invalid("unknown roll interpolation");
    if (style.widthInterpolation > RoadWidthInterpolation::CatmullRom) return invalid("unknown width interpolation");
    if (style.frameOrientation > RoadFrameOrientation::WorldUp) return invalid("unknown frame orientation");
    if (style.crossSection.empty() && style.crossSectionClosed) return invalid("closed flag needs a custom section");
    if (!style.crossSection.empty()) {
        auto valid = validateSection(style.crossSection, style.crossSectionClosed);
        if (!valid.ok()) return valid;
    }
    if (style.structures.size() > 256u || style.facilities.size() > 64u)
        return invalid("road authoring exceeds interval budget");
    float previous = 0.f;
    for (const auto& item : style.structures) {
        if (!isValidInterval(item.start, item.end) || item.start < previous || item.kind > RoadStructureKind::Tunnel ||
            !std::isfinite(item.tunnelHeight) || item.tunnelHeight <= 0.f || item.tunnelHeight > 1000.f ||
            !std::isfinite(item.portalStartLength) || !std::isfinite(item.portalEndLength) ||
            item.portalStartLength < 0.f || item.portalEndLength < 0.f || item.portalStartLength > 10000.f ||
            item.portalEndLength > 10000.f || !std::isfinite(item.wallThickness) || item.wallThickness <= 0.f ||
            item.wallThickness > 100.f)
            return invalid("structural intervals must be sorted, nonoverlapping, finite and bounded");
        float maximumHeight = style.crossSection.empty() ? std::max(style.curbHeight, style.sidewalkHeight) : 0.f;
        for (const auto& point : style.crossSection) maximumHeight = std::max(maximumHeight, point.up);
        if (item.kind == RoadStructureKind::Tunnel && item.tunnelHeight <= maximumHeight + 1e-4f)
            return invalid("tunnel roof intersects the authored road section");
        previous = item.end;
    }
    std::unordered_set<std::uint32_t> ids;
    for (const auto& item : style.facilities) {
        if (item.id == 0 || !ids.insert(item.id).second || !isValidInterval(item.start, item.end) ||
            !std::isfinite(item.lateralOffset) || std::fabs(item.lateralOffset) > 10000.f ||
            !std::isfinite(item.verticalOffset) || std::fabs(item.verticalOffset) > 10000.f ||
            !std::isfinite(item.uvMeters) || item.uvMeters <= 1e-4f)
            return invalid("continuous facility authoring is invalid");
        auto valid = validateSection(item.profile, true);
        if (!valid.ok()) return valid;
        auto prefab = validateFacilityPrefab(item.prefab);
        if (!prefab.ok()) return prefab;
    }
    return Result<void>::success();
}
void reverseRoadAuthoring(RoadStyle& style) {
    reverseSectionGeometry(style.crossSection, style.crossSectionClosed);
    for (auto& item : style.structures) {
        const float a = item.start;
        item.start    = 1.f - item.end;
        item.end      = 1.f - a;
        std::swap(item.portalStartLength, item.portalEndLength);
    }
    std::reverse(style.structures.begin(), style.structures.end());
    for (auto& item : style.facilities) {
        const float a      = item.start;
        item.start         = 1.f - item.end;
        item.end           = 1.f - a;
        item.lateralOffset = -item.lateralOffset;
        std::swap(item.capStart, item.capEnd);
        reverseSectionGeometry(item.profile, true);
        reverseFacilityPrefab(item.prefab);
    }
}
void reverseFacilityPrefab(RoadFacilityPrefab& prefab) {
    if (prefab.automaticSpacing) prefab.reverseRepeatSequence = !prefab.reverseRepeatSequence;
    const float start  = prefab.sourceStart;
    prefab.sourceStart = 1.f - prefab.sourceEnd;
    prefab.sourceEnd   = 1.f - start;
    auto reflect       = [](auto& vertices, float length) {
        for (auto& v : vertices) {
            v.x  = -v.x;
            v.z  = length - v.z;
            v.nx = -v.nx;
            v.nz = -v.nz;
        }
    };
    reflect(prefab.vertices, prefab.length);
    std::swap(prefab.startMesh, prefab.endMesh);
    reflect(prefab.startMesh.vertices, prefab.startMesh.length);
    reflect(prefab.endMesh.vertices, prefab.endMesh.length);
}
Result<std::pair<RoadStyle, RoadStyle>> splitRoadAuthoring(const RoadEdge& edge, std::size_t pointIndex) {
    SplinePath path;
    auto       kind = path.setKindResult("catmullRom");
    if (!kind.ok()) return Result<std::pair<RoadStyle, RoadStyle>>::failure(kind.status());
    for (const auto& p : edge.controlPoints) {
        SplinePoint point;
        point.alignStraightStart = p.alignStraightStart;
        point.alignStraightEnd   = p.alignStraightEnd;
        point.circularFromEnd    = p.circularFromEnd;
        point.spanControl        = static_cast<eve::procgen::SplineSpanControl>(p.pathControl);
        point.spanStrength       = p.splineStrength;
        point.x                  = p.x;
        point.y                  = p.y;
        point.z                  = p.z;
        auto added               = path.addPointResult(point);
        if (!added.ok()) return Result<std::pair<RoadStyle, RoadStyle>>::failure(added.status());
    }
    const int    count = path.segmentCount() * 24;
    float        total = 0.f, firstLength = 0.f;
    SplineSample previous;
    for (int i = 0; i <= count; ++i) {
        auto sample = path.evaluateResult(static_cast<float>(i) / static_cast<float>(count));
        if (!sample.ok()) return Result<std::pair<RoadStyle, RoadStyle>>::failure(sample.status());
        if (i > 0)
            total +=
                std::sqrt(std::pow(sample.value().x - previous.x, 2.f) + std::pow(sample.value().y - previous.y, 2.f) +
                          std::pow(sample.value().z - previous.z, 2.f));
        if (i == static_cast<int>(pointIndex) * 24) firstLength = total;
        previous = sample.value();
    }
    const float fraction = firstLength / total;
    if (!std::isfinite(fraction) || fraction <= 1e-5f || fraction >= 1.f - 1e-5f)
        return Result<std::pair<RoadStyle, RoadStyle>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "split has a degenerate arc interval"));
    auto first = edge.style, second = edge.style;
    if (!edge.style.structures.empty()) first.automaticStructures = second.automaticStructures = false;
    first.structures  = clip(edge.style.structures, 0.f, fraction, total);
    second.structures = clip(edge.style.structures, fraction, 1.f, total);
    first.facilities  = clip(edge.style.facilities, 0.f, fraction, total);
    second.facilities = clip(edge.style.facilities, fraction, 1.f, total);
    return Result<std::pair<RoadStyle, RoadStyle>>::success({std::move(first), std::move(second)});
}
float roadSurfaceHeight(const RoadStyle& style, float lateral) {
    if (style.crossSection.empty()) return 0.f;
    const std::size_t count = style.crossSectionClosed ? style.crossSection.size() : style.crossSection.size() - 1;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& a = style.crossSection[i];
        const auto& b = style.crossSection[(i + 1) % style.crossSection.size()];
        if (a.material != RoadMaterial::Asphalt || std::fabs(a.side - b.side) < 1e-5f ||
            lateral < std::min(a.side, b.side) - 1e-5f || lateral > std::max(a.side, b.side) + 1e-5f)
            continue;
        return std::lerp(a.up, b.up, std::clamp((lateral - a.side) / (b.side - a.side), 0.f, 1.f));
    }
    return 0.f;
}
}  // namespace eve::procgen::road::detail
