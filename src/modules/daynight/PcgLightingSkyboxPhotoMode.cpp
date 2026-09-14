#include "daynight/PcgLightingSkyboxPhotoMode.h"

#include "common/Capability.h"

#include <cmath>

namespace eve::daynight {

PcgLightingSkyboxPhotoMode::~PcgLightingSkyboxPhotoMode() {
    if (authority_) cap::removeListener<IPhotoModeFieldSink>(this);
}

void PcgLightingSkyboxPhotoMode::setTarget(DayNight *target) noexcept {
    target_ = target;
    if (target_) state_ = target_->getPcgSkybox();
}

void PcgLightingSkyboxPhotoMode::setAuthority(bool enabled) {
    if (enabled == authority_) return;
    if (enabled) cap::addListener<IPhotoModeFieldSink>(this);
    else cap::removeListener<IPhotoModeFieldSink>(this);
    authority_ = enabled;
}

PhotoModeFieldAcceptance PcgLightingSkyboxPhotoMode::acceptsPhotoModeField(
    const PhotoModeAssignment &assignment) const noexcept {
    if (assignment.domain != PhotoModeDomain::Lighting)
        return PhotoModeFieldAcceptance::Rejected;
    return assignment.field == "m_skyboxOverride" ||
                   assignment.field == "m_skyboxRotation" ||
                   assignment.field == "m_skyboxExposure" ||
                   assignment.field == "m_skyboxTint"
               ? PhotoModeFieldAcceptance::Accepted
               : PhotoModeFieldAcceptance::Rejected;
}

Result<void> PcgLightingSkyboxPhotoMode::applyPhotoModeField(
    const PhotoModeAssignment &assignment) {
    const auto fail = [&](DiagnosticCode code, const char *message) {
        return Result<void>::failure(Diagnostic::error(
            code, message, assignment.field, {}, "daynight.pcgLightingSkybox"));
    };
    if (!target_)
        return fail(DiagnosticCode::Unsupported, "day-night target is unavailable");

    PcgSkyboxState candidate = state_;
    if (assignment.field == "m_skyboxOverride") {
        const auto *value = std::get_if<bool>(&assignment.value);
        if (!value)
            return fail(DiagnosticCode::InvalidArgument,
                        "skybox override requires a bool");
        candidate.enabled = *value;
    } else if (assignment.field == "m_skyboxTint") {
        const auto *value = std::get_if<PhotoModeColor>(&assignment.value);
        if (!value || !std::isfinite(value->r) || !std::isfinite(value->g) ||
            !std::isfinite(value->b) || value->r < 0.f || value->g < 0.f ||
            value->b < 0.f)
            return fail(DiagnosticCode::InvalidArgument,
                        "skybox tint requires finite non-negative RGB channels");
        candidate.tintRed = value->r;
        candidate.tintGreen = value->g;
        candidate.tintBlue = value->b;
    } else {
        const auto *value = std::get_if<float>(&assignment.value);
        if (!value || !std::isfinite(*value))
            return fail(DiagnosticCode::InvalidArgument,
                        "skybox field requires a finite float");
        if (assignment.field == "m_skyboxRotation")
            candidate.rotationDegrees = *value;
        else if (assignment.field == "m_skyboxExposure")
            candidate.exposure = *value;
        else
            return fail(DiagnosticCode::InvalidArgument,
                        "unsupported Pcg skybox field");
    }

    auto result = target_->setPcgSkybox(candidate);
    if (!result.ok()) return result;
    state_ = candidate;
    return Result<void>::success();
}

}  // namespace eve::daynight
