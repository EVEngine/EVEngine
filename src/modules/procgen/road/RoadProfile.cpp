#include "procgen/road/RoadTypes.h"

#include "common/Diagnostic.h"

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
    return Result<void>::success();
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
        case RoadMaterial::Nav: return "nav";
    }
    return "asphalt";
}

Result<RoadProfile> makeRoadProfile(const RoadStyle& style, int lanesForward, int lanesBackward) {
    auto ok = validateStyle(style);
    if (!ok.ok()) return Result<RoadProfile>::failure(ok.status());
    if (lanesForward < 1 || lanesForward > 8)
        return Result<RoadProfile>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "lanesForward must be in [1,8]", "lanesForward"));
    if (lanesBackward < 0 || lanesBackward > 8)
        return Result<RoadProfile>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "lanesBackward must be in [0,8]", "lanesBackward"));

    const float asphaltHalf =
        0.5f * style.laneWidth * static_cast<float>(lanesForward + lanesBackward);
    const float curbOuter     = asphaltHalf + style.curbWidth;
    const float sidewalkOuter = curbOuter + style.sidewalkWidth;

    RoadProfile profile;
    profile.halfWidth = sidewalkOuter;

    // Open U-channel from left sidewalk outer → right sidewalk outer (driving surface on top).
    pushPoint(profile, -sidewalkOuter, style.sidewalkHeight, RoadMaterial::Sidewalk);
    pushPoint(profile, -curbOuter, style.sidewalkHeight, RoadMaterial::Sidewalk);
    pushPoint(profile, -curbOuter, style.curbHeight, RoadMaterial::Curb);
    pushPoint(profile, -asphaltHalf, style.curbHeight, RoadMaterial::Curb);
    pushPoint(profile, -asphaltHalf, 0.f, RoadMaterial::Asphalt);
    pushPoint(profile, asphaltHalf, 0.f, RoadMaterial::Asphalt);
    pushPoint(profile, asphaltHalf, style.curbHeight, RoadMaterial::Curb);
    pushPoint(profile, curbOuter, style.curbHeight, RoadMaterial::Curb);
    pushPoint(profile, curbOuter, style.sidewalkHeight, RoadMaterial::Sidewalk);
    pushPoint(profile, sidewalkOuter, style.sidewalkHeight, RoadMaterial::Sidewalk);

    return Result<RoadProfile>::success(std::move(profile));
}

}  // namespace eve::procgen::road
