#include "ui/PcgPhotoModePhoto.h"

#include "common/Capability.h"

#include <utility>

namespace eve::ui {

PcgPhotoModePhotoAuthority::~PcgPhotoModePhotoAuthority() {
    if (authority_) cap::removeListener<IPhotoModeFieldSink>(this);
}

void PcgPhotoModePhotoAuthority::setAuthority(bool enabled) {
    if (enabled == authority_) return;
    if (enabled) cap::addListener<IPhotoModeFieldSink>(this);
    else cap::removeListener<IPhotoModeFieldSink>(this);
    authority_ = enabled;
}

PhotoModeFieldAcceptance PcgPhotoModePhotoAuthority::acceptsPhotoModeField(
    const PhotoModeAssignment& assignment) const noexcept {
    const bool lightingIdentity = assignment.domain == PhotoModeDomain::Lighting &&
        (assignment.field == "m_isUsingPcgLighting" ||
         assignment.field == "m_selectedPcgLightingProfile");
    return assignment.domain == PhotoModeDomain::Photo || lightingIdentity
               ? PhotoModeFieldAcceptance::Accepted
               : PhotoModeFieldAcceptance::Rejected;
}

Result<void> PcgPhotoModePhotoAuthority::applyPhotoModeField(const PhotoModeAssignment& assignment) {
    auto next = state_;
    const auto invalid = [&](const char* message) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message,
            assignment.field, {}, "ui.pcgPhotoModePhoto"));
    };
    if (assignment.field == "m_isUsingPcgLighting") {
        const auto* value = std::get_if<bool>(&assignment.value);
        if (!value) return invalid("Pcg lighting identity requires a bool");
        next.usingPcgLighting = *value;
    } else if (assignment.field == "m_selectedPcgLightingProfile") {
        const auto* value = std::get_if<int64_t>(&assignment.value);
        if (!value || *value < -1) return invalid("Pcg lighting profile must be -1 or non-negative");
        next.lightingProfile = static_cast<int>(*value);
    } else if (assignment.field == "m_lastSceneName") {
        const auto* value = std::get_if<std::string>(&assignment.value);
        if (!value) return invalid("last scene name requires a string");
        next.lastSceneName = *value;
    } else if (assignment.field == "m_screenshotResolution") {
        const auto* value = std::get_if<int64_t>(&assignment.value);
        if (!value || *value < 0 || *value > 9) return invalid("screenshot resolution index must be in [0,9]");
        next.screenshotResolution = static_cast<int>(*value);
    } else if (assignment.field == "m_screenshotImageFormat") {
        const auto* value = std::get_if<int64_t>(&assignment.value);
        if (!value || *value < 0 || *value > 3) return invalid("screenshot format index must be in [0,3]");
        next.screenshotFormat = static_cast<PcgScreenshotFormat>(*value);
    } else {
        const auto* value = std::get_if<bool>(&assignment.value);
        if (!value) return invalid("photo option requires a bool");
        if (assignment.field == "m_loadSavedSettings") next.loadSavedSettings = *value;
        else if (assignment.field == "m_revertOnDisabled") next.revertOnDisabled = *value;
        else if (assignment.field == "m_showFPS") next.showFps = *value;
        else if (assignment.field == "m_showReticle") next.showReticle = *value;
        else if (assignment.field == "m_showRuleOfThirds") next.showRuleOfThirds = *value;
        else return invalid("unsupported Photo-domain field");
    }
    state_ = std::move(next);
    return Result<void>::success();
}

bool PcgPhotoModePhotoAuthority::lightingProfileMatches(int currentProfile) const noexcept {
    return state_.lightingProfile == currentProfile &&
           state_.usingPcgLighting == (currentProfile >= 0);
}

PcgScreenshotSize PcgPhotoModePhotoAuthority::screenshotSize() const noexcept {
    static constexpr PcgScreenshotSize sizes[] = {
        {0, 0}, {640, 480}, {800, 600}, {1280, 720}, {1366, 768},
        {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}, {7680, 4320}};
    return sizes[state_.screenshotResolution];
}

const char* PcgPhotoModePhotoAuthority::screenshotExtension() const noexcept {
    static constexpr const char* extensions[] = {"exr", "jpg", "png", "tga"};
    return extensions[static_cast<int>(state_.screenshotFormat)];
}

}  // namespace eve::ui
