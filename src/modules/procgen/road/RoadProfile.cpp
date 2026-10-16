#include "procgen/road/RoadTypes.h"

#include <algorithm>
#include "common/Diagnostic.h"
#include "procgen/road/RoadAuthoringInternal.h"

#include <cmath>

namespace eve::procgen::road {
namespace {

Result<void> validateStyle(const RoadStyle& style) {
    const float values[] = {style.laneWidth,     style.curbWidth,     style.curbHeight,    style.sidewalkWidth,
                            style.sidewalkHeight, style.deckThickness, style.pierWidth,     style.pierDepth,
                            style.pierSpacing,    style.pierClearance, style.markingWidth,  style.dashLength,
                            style.dashGap,        style.uvMeters};
    for (float v : values) {
        if (!std::isfinite(v) || v < 0.f)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                          "road style values must be finite and non-negative",
                                                          "style"));
    }
    if (style.laneWidth <= 1e-4f)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "laneWidth must be positive", "laneWidth"));
    if (style.uvMeters <= 1e-4f)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "uvMeters must be positive", "uvMeters"));
    return detail::validateRoadAuthoring(style);
}

void pushPoint(RoadProfile& profile, float side, float up, RoadMaterial material) {
    profile.points.push_back(RoadProfilePoint{side, up, material});
}

}  // namespace

const char* roadMaterialGroup(RoadMaterial material) noexcept {
    switch (material) {
        case RoadMaterial::Asphalt: return "asphalt";
        case RoadMaterial::Curb: return "curb";
        case RoadMaterial::Sidewalk: return "sidewalk";
        case RoadMaterial::Deck: return "deck";
        case RoadMaterial::Pier: return "pier";
        case RoadMaterial::Marking: return "marking";
        case RoadMaterial::MarkingYellow: return "markingYellow";
        case RoadMaterial::Nav: return "nav";
        case RoadMaterial::BridgeAsphalt: return "asphalt.bridge";
        case RoadMaterial::TunnelAsphalt: return "asphalt.tunnel";
        case RoadMaterial::Tunnel: return "tunnel";
        case RoadMaterial::TunnelPortalAsphalt: return "asphalt.tunnel.portal";
    }
    return "asphalt";
}

Result<RoadProfile> makeRoadProfile(const RoadStyle& style, int lanesForward, int lanesBackward) {
    auto ok = validateStyle(style);
    if (!ok.ok()) return Result<RoadProfile>::failure(ok.status());
    if (lanesForward < 0 || lanesForward > 8)
        return Result<RoadProfile>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "lanesForward must be in [0,8]", "lanesForward"));
    if (lanesBackward < 0 || lanesBackward > 8)
        return Result<RoadProfile>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "lanesBackward must be in [0,8]", "lanesBackward"));
    if (lanesForward + lanesBackward < 1)
        return Result<RoadProfile>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "road must contain at least one directional lane", "lanes"));

    const float asphaltHalf =
        0.5f * style.laneWidth * static_cast<float>(lanesForward + lanesBackward);
    if (!style.crossSection.empty()) {
        std::vector<std::pair<float, float>> coverage;
        const std::size_t                    segments =
            style.crossSectionClosed ? style.crossSection.size() : style.crossSection.size() - 1;
        for (std::size_t i = 0; i < segments; ++i) {
            const auto& a = style.crossSection[i];
            const auto& b = style.crossSection[(i + 1) % style.crossSection.size()];
            if (a.material == RoadMaterial::Asphalt && std::fabs(a.side - b.side) > 1e-5f)
                coverage.emplace_back(std::min(a.side, b.side), std::max(a.side, b.side));
        }
        std::sort(coverage.begin(), coverage.end());
        for (std::size_t i = 1; i < coverage.size(); ++i)
            if (coverage[i].first < coverage[i - 1].second - 1e-5f)
                return Result<RoadProfile>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument,
                    "custom driving surface must be single-valued across its lateral range", "style.crossSection"));
        float covered = -asphaltHalf;
        for (const auto& span : coverage) {
            if (span.first > covered + 1e-4f) break;
            covered = std::max(covered, span.second);
        }
        if (coverage.empty() || coverage.front().first > -asphaltHalf + 1e-4f || covered < asphaltHalf - 1e-4f)
            return Result<RoadProfile>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "custom asphalt section does not cover every driving lane", "style.crossSection"));
        RoadProfile profile;
        profile.points            = style.crossSection;
        profile.closed            = style.crossSectionClosed;
        profile.explicitMaterials = true;
        for (const auto& point : profile.points) profile.halfWidth = std::max(profile.halfWidth, std::fabs(point.side));
        return Result<RoadProfile>::success(std::move(profile));
    }
    const float barrierInner  = asphaltHalf;
    const float barrierOuter  = asphaltHalf + style.curbWidth;
    const float sidewalkOuter = barrierOuter + style.sidewalkWidth;
    const float barrierTop    = style.curbHeight;
    const float walkH         = style.sidewalkHeight;

    RoadProfile profile;
    profile.halfWidth = sidewalkOuter;

    // Jersey-barrier U-channel matching the reference interchange look:
    // sidewalk → tall barrier outer/top/inner → asphalt deck → mirror.
    pushPoint(profile, -sidewalkOuter, walkH, RoadMaterial::Sidewalk);
    pushPoint(profile, -barrierOuter, walkH, RoadMaterial::Sidewalk);
    pushPoint(profile, -barrierOuter, barrierTop, RoadMaterial::Curb);
    pushPoint(profile, -barrierInner, barrierTop, RoadMaterial::Curb);
    pushPoint(profile, -barrierInner, 0.f, RoadMaterial::Curb);
    pushPoint(profile, barrierInner, 0.f, RoadMaterial::Asphalt);
    pushPoint(profile, barrierInner, barrierTop, RoadMaterial::Curb);
    pushPoint(profile, barrierOuter, barrierTop, RoadMaterial::Curb);
    pushPoint(profile, barrierOuter, walkH, RoadMaterial::Sidewalk);
    pushPoint(profile, sidewalkOuter, walkH, RoadMaterial::Sidewalk);

    return Result<RoadProfile>::success(std::move(profile));
}

}  // namespace eve::procgen::road
