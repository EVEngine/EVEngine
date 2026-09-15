#include "daynight/PcgLightingTimePhotoMode.h"

#include "common/Capability.h"
#include "daynight/DayNight.h"

#include <cmath>

namespace eve::daynight {

PcgLightingTimePhotoMode::~PcgLightingTimePhotoMode() {
    if (authority_) cap::removeListener<IPhotoModeFieldSink>(this);
}

void PcgLightingTimePhotoMode::setAuthority(bool enabled) {
    if (enabled == authority_) return;
    if (enabled) cap::addListener<IPhotoModeFieldSink>(this);
    else cap::removeListener<IPhotoModeFieldSink>(this);
    authority_ = enabled;
}

PhotoModeFieldAcceptance PcgLightingTimePhotoMode::acceptsPhotoModeField(
    const PhotoModeAssignment& assignment) const noexcept {
    if (assignment.domain != PhotoModeDomain::Lighting) return PhotoModeFieldAcceptance::Rejected;
    return assignment.field == "m_pcgTime" || assignment.field == "m_pcgTimeScale" ||
                   assignment.field == "m_pcgTimeOfDayEnabled"
               ? PhotoModeFieldAcceptance::Accepted
               : PhotoModeFieldAcceptance::Rejected;
}

Result<void> PcgLightingTimePhotoMode::applyPhotoModeField(const PhotoModeAssignment& assignment) {
    const auto fail = [&](DiagnosticCode code, const char* message) {
        return Result<void>::failure(
            Diagnostic::error(code, message, assignment.field, {}, "daynight.pcgLightingTime"));
    };
    if (!target_) return fail(DiagnosticCode::Unsupported, "day-night target is unavailable");

    if (assignment.field == "m_pcgTimeOfDayEnabled") {
        const auto* value = std::get_if<bool>(&assignment.value);
        if (!value) return fail(DiagnosticCode::InvalidArgument, "time-of-day enabled requires a bool");
        target_->setPaused(!*value);
        return Result<void>::success();
    }
    const auto* value = std::get_if<float>(&assignment.value);
    if (!value || !std::isfinite(*value))
        return fail(DiagnosticCode::InvalidArgument, "Pcg time field requires a finite float");
    if (assignment.field == "m_pcgTime") {
        if (*value < 0.f || *value > 24.f)
            return fail(DiagnosticCode::InvalidArgument, "Pcg time must be in [0,24]");
        target_->setTimeOfDay(*value);
    } else if (assignment.field == "m_pcgTimeScale") {
        if (*value < 0.f || *value > 200.f)
            return fail(DiagnosticCode::InvalidArgument, "Pcg time scale must be in [0,200]");
        target_->setSpeed(*value);
    } else {
        return fail(DiagnosticCode::InvalidArgument, "unsupported Pcg lighting-time field");
    }
    return Result<void>::success();
}

}  // namespace eve::daynight
