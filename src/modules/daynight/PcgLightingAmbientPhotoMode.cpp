#include "daynight/PcgLightingAmbientPhotoMode.h"

#include "common/Capability.h"

#include <cmath>

namespace eve::daynight {
namespace {
bool accepts(const std::string &field) {
    return field == "m_ambientIntensity" || field == "m_ambientSkyColor" ||
           field == "m_ambientEquatorColor" || field == "m_ambientGroundColor" ||
           field == "m_globalLightIntensityMultiplier";
}
}  // namespace

PcgLightingAmbientPhotoMode::~PcgLightingAmbientPhotoMode() {
    if (authority_) {
        cap::removeListener<IPhotoModeFieldSink>(this);
        if (target_) target_->setPcgAmbientLight(baseline_).value();
    }
}

void PcgLightingAmbientPhotoMode::setTarget(DayNight *target) noexcept {
    target_ = target;
    if (target_) {
        baseline_ = target_->getPcgAmbientLight();
        state_ = baseline_;
    }
}

void PcgLightingAmbientPhotoMode::setAuthority(bool enabled) {
    if (enabled == authority_) return;
    if (enabled) {
        state_.active = true;
        if (target_) target_->setPcgAmbientLight(state_).value();
        cap::addListener<IPhotoModeFieldSink>(this);
    } else {
        cap::removeListener<IPhotoModeFieldSink>(this);
        if (target_) target_->setPcgAmbientLight(baseline_).value();
    }
    authority_ = enabled;
}

PhotoModeFieldAcceptance PcgLightingAmbientPhotoMode::acceptsPhotoModeField(
    const PhotoModeAssignment &assignment) const noexcept {
    return assignment.domain == PhotoModeDomain::Lighting && accepts(assignment.field)
               ? PhotoModeFieldAcceptance::Accepted
               : PhotoModeFieldAcceptance::Rejected;
}

Result<void> PcgLightingAmbientPhotoMode::applyPhotoModeField(
    const PhotoModeAssignment &assignment) {
    const auto fail = [&](DiagnosticCode code, const char *message) {
        return Result<void>::failure(Diagnostic::error(
            code, message, assignment.field, {}, "daynight.pcgLightingAmbient"));
    };
    if (!target_)
        return fail(DiagnosticCode::Unsupported, "day-night target is unavailable");
    if (!accepts(assignment.field))
        return fail(DiagnosticCode::InvalidArgument, "unsupported Pcg ambient field");

    PcgAmbientLightState candidate = state_;
    if (assignment.field == "m_ambientSkyColor" ||
        assignment.field == "m_ambientEquatorColor" ||
        assignment.field == "m_ambientGroundColor") {
        const auto *value = std::get_if<PhotoModeColor>(&assignment.value);
        if (!value || !std::isfinite(value->r) || !std::isfinite(value->g) ||
            !std::isfinite(value->b) || value->r < 0.f || value->g < 0.f ||
            value->b < 0.f)
            return fail(DiagnosticCode::InvalidArgument,
                        "ambient color requires finite non-negative RGB channels");
        float *red = &candidate.skyRed;
        float *green = &candidate.skyGreen;
        float *blue = &candidate.skyBlue;
        if (assignment.field == "m_ambientEquatorColor") {
            red = &candidate.equatorRed;
            green = &candidate.equatorGreen;
            blue = &candidate.equatorBlue;
        } else if (assignment.field == "m_ambientGroundColor") {
            red = &candidate.groundRed;
            green = &candidate.groundGreen;
            blue = &candidate.groundBlue;
        }
        *red = value->r;
        *green = value->g;
        *blue = value->b;
    } else {
        const auto *value = std::get_if<float>(&assignment.value);
        if (!value || !std::isfinite(*value))
            return fail(DiagnosticCode::InvalidArgument,
                        "ambient setting requires a finite float");
        if (assignment.field == "m_ambientIntensity")
            candidate.intensity = *value;
        else
            candidate.globalLightMultiplier = *value;
    }

    auto result = target_->setPcgAmbientLight(candidate);
    if (!result.ok()) return result;
    state_ = candidate;
    return Result<void>::success();
}

}  // namespace eve::daynight
