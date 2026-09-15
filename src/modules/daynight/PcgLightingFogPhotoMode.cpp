#include "daynight/PcgLightingFogPhotoMode.h"

#include "common/Capability.h"

#include <cmath>
#include <string_view>

namespace eve::daynight {
namespace {
bool isFogField(std::string_view field) {
    return field == "m_pcgAdditionalLinearFog" ||
           field == "m_pcgAdditionalExponentialFog" ||
           field == "m_fogOverride" || field == "m_fogMode" ||
           field == "m_fogColor" || field == "m_fogDensity" ||
           field == "m_fogStart" || field == "m_fogEnd" ||
           field == "m_globalFogDensityMultiplier" ||
           field == "m_overrideDensityVolume" ||
           field == "m_densityVolumeAlbedoColor" ||
           field == "m_densityVolumeFogDistance" ||
           field == "m_densityVolumeEffectType" ||
           field == "m_densityVolumeTilingResolution";
}
}  // namespace

PcgLightingFogPhotoMode::~PcgLightingFogPhotoMode() {
    if (authority_) cap::removeListener<IPhotoModeFieldSink>(this);
}

void PcgLightingFogPhotoMode::setTarget(DayNight *target) noexcept {
    target_ = target;
    if (target_) state_ = target_->getPcgFog();
}

void PcgLightingFogPhotoMode::setAuthority(bool enabled) {
    if (enabled == authority_) return;
    if (enabled) cap::addListener<IPhotoModeFieldSink>(this);
    else cap::removeListener<IPhotoModeFieldSink>(this);
    authority_ = enabled;
}

PhotoModeFieldAcceptance PcgLightingFogPhotoMode::acceptsPhotoModeField(
    const PhotoModeAssignment &assignment) const noexcept {
    return assignment.domain == PhotoModeDomain::Lighting && isFogField(assignment.field)
               ? PhotoModeFieldAcceptance::Accepted
               : PhotoModeFieldAcceptance::Rejected;
}

Result<void> PcgLightingFogPhotoMode::applyPhotoModeField(
    const PhotoModeAssignment &assignment) {
    const auto fail = [&](DiagnosticCode code, const char *message) {
        return Result<void>::failure(Diagnostic::error(
            code, message, assignment.field, {}, "daynight.pcgLightingFog"));
    };
    if (!target_)
        return fail(DiagnosticCode::Unsupported, "day-night target is unavailable");
    if (!isFogField(assignment.field))
        return fail(DiagnosticCode::InvalidArgument, "unsupported Pcg fog field");

    PcgFogState candidate = state_;
    if (assignment.field == "m_fogOverride" ||
        assignment.field == "m_overrideDensityVolume") {
        const auto *value = std::get_if<bool>(&assignment.value);
        if (!value)
            return fail(DiagnosticCode::InvalidArgument, "fog toggle requires a bool");
        if (assignment.field == "m_fogOverride") candidate.overrideFog = *value;
        else candidate.overrideDensityVolume = *value;
    } else if (assignment.field == "m_fogMode" ||
               assignment.field == "m_densityVolumeEffectType" ||
               assignment.field == "m_densityVolumeTilingResolution") {
        const auto *value = std::get_if<std::int64_t>(&assignment.value);
        if (!value)
            return fail(DiagnosticCode::InvalidArgument, "fog enum requires an integer");
        if (assignment.field == "m_fogMode") candidate.mode = static_cast<int>(*value);
        else if (assignment.field == "m_densityVolumeEffectType")
            candidate.densityVolumeEffect = static_cast<int>(*value);
        else candidate.densityVolumeTiling = static_cast<int>(*value);
    } else if (assignment.field == "m_fogColor" ||
               assignment.field == "m_densityVolumeAlbedoColor") {
        const auto *value = std::get_if<PhotoModeColor>(&assignment.value);
        if (!value || !std::isfinite(value->r) || !std::isfinite(value->g) ||
            !std::isfinite(value->b) || value->r < 0.f || value->g < 0.f ||
            value->b < 0.f)
            return fail(DiagnosticCode::InvalidArgument,
                        "fog color requires finite non-negative RGB channels");
        if (assignment.field == "m_fogColor") {
            candidate.red = value->r;
            candidate.green = value->g;
            candidate.blue = value->b;
        } else {
            candidate.densityAlbedoRed = value->r;
            candidate.densityAlbedoGreen = value->g;
            candidate.densityAlbedoBlue = value->b;
        }
    } else {
        const auto *value = std::get_if<float>(&assignment.value);
        if (!value || !std::isfinite(*value))
            return fail(DiagnosticCode::InvalidArgument,
                        "fog setting requires a finite float");
        if (assignment.field == "m_pcgAdditionalLinearFog")
            candidate.additionalLinearDistance = *value;
        else if (assignment.field == "m_pcgAdditionalExponentialFog")
            candidate.additionalExponentialDensity = *value;
        else if (assignment.field == "m_fogDensity") candidate.density = *value;
        else if (assignment.field == "m_fogStart") candidate.startDistance = *value;
        else if (assignment.field == "m_fogEnd") candidate.endDistance = *value;
        else if (assignment.field == "m_globalFogDensityMultiplier")
            candidate.globalDensityMultiplier = *value;
        else if (assignment.field == "m_densityVolumeFogDistance")
            candidate.densityVolumeDistance = *value;
    }

    auto result = target_->setPcgFog(candidate);
    if (!result.ok()) return result;
    state_ = candidate;
    return Result<void>::success();
}

}  // namespace eve::daynight
