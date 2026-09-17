#include "daynight/PcgLightingSunPhotoMode.h"

#include "common/Capability.h"

#include <cmath>

namespace eve::daynight {

PcgLightingSunPhotoMode::~PcgLightingSunPhotoMode() {
    if (authority_) cap::removeListener<IPhotoModeFieldSink>(this);
}

void PcgLightingSunPhotoMode::setTarget(DayNight *target) noexcept {
    target_ = target;
    if (target_) state_ = target_->getPcgManualSun();
}

void PcgLightingSunPhotoMode::setAuthority(bool enabled) {
    if (enabled == authority_) return;
    if (enabled) cap::addListener<IPhotoModeFieldSink>(this);
    else cap::removeListener<IPhotoModeFieldSink>(this);
    authority_ = enabled;
}

PhotoModeFieldAcceptance PcgLightingSunPhotoMode::acceptsPhotoModeField(
    const PhotoModeAssignment &assignment) const noexcept {
    if (assignment.domain != PhotoModeDomain::Lighting)
        return PhotoModeFieldAcceptance::Rejected;
    return assignment.field == "m_sunRotation" || assignment.field == "m_sunPitch" ||
                   assignment.field == "m_sunOverride" ||
                   assignment.field == "m_sunIntensity" ||
                   assignment.field == "m_sunColor" ||
                   assignment.field == "m_sunKelvinValue"
               ? PhotoModeFieldAcceptance::Accepted
               : PhotoModeFieldAcceptance::Rejected;
}

Result<void> PcgLightingSunPhotoMode::applyPhotoModeField(
    const PhotoModeAssignment &assignment) {
    const auto fail = [&](DiagnosticCode code, const char *message) {
        return Result<void>::failure(Diagnostic::error(
            code, message, assignment.field, {}, "daynight.pcgLightingSun"));
    };
    if (!target_)
        return fail(DiagnosticCode::Unsupported, "day-night target is unavailable");

    PcgManualSunState candidate = state_;
    if (assignment.field == "m_sunOverride") {
        const auto *value = std::get_if<bool>(&assignment.value);
        if (!value)
            return fail(DiagnosticCode::InvalidArgument, "sun override requires a bool");
        candidate.enabled = *value;
    } else if (assignment.field == "m_sunColor") {
        const auto *value = std::get_if<PhotoModeColor>(&assignment.value);
        if (!value || !std::isfinite(value->r) || !std::isfinite(value->g) ||
            !std::isfinite(value->b) || value->r < 0.f || value->g < 0.f ||
            value->b < 0.f)
            return fail(DiagnosticCode::InvalidArgument,
                        "sun color requires finite non-negative RGB channels");
        candidate.red = value->r;
        candidate.green = value->g;
        candidate.blue = value->b;
    } else {
        const auto *value = std::get_if<float>(&assignment.value);
        if (!value || !std::isfinite(*value))
            return fail(DiagnosticCode::InvalidArgument,
                        "sun field requires a finite float");
        if (assignment.field == "m_sunRotation")
            candidate.rotationDegrees = *value;
        else if (assignment.field == "m_sunPitch")
            candidate.pitchDegrees = *value;
        else if (assignment.field == "m_sunIntensity")
            candidate.intensity = *value;
        else if (assignment.field == "m_sunKelvinValue")
            candidate.kelvin = *value;
        else
            return fail(DiagnosticCode::InvalidArgument,
                        "unsupported Pcg manual-sun field");
    }

    auto result = target_->setPcgManualSun(candidate);
    if (!result.ok()) return result;
    state_ = candidate;
    return Result<void>::success();
}

}  // namespace eve::daynight
