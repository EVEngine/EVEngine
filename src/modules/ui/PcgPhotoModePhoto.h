#pragma once

#include "common/PcgPhotoModeApply.h"

#include <string>

namespace eve::ui {

/** @brief Pcg screenshot output encoding. */
enum class PcgScreenshotFormat { Exr = 0, Jpg = 1, Png = 2, Tga = 3 };

/** @brief Resolved screenshot dimensions; zero means the current drawable size. */
struct PcgScreenshotSize { int width = 0; int height = 0; };

/** @brief PhotoMode-local capture, persistence and composition-overlay state. */
struct PcgPhotoModePhotoState {
    bool usingPcgLighting = false;
    int lightingProfile = -1;
    std::string lastSceneName;
    int screenshotResolution = 0;
    PcgScreenshotFormat screenshotFormat = PcgScreenshotFormat::Png;
    bool loadSavedSettings = true;
    bool revertOnDisabled = true;
    bool showFps = false;
    bool showReticle = false;
    bool showRuleOfThirds = false;
};

/** @brief Explicit owner for Pcg's Photo domain fields. */
class PcgPhotoModePhotoAuthority final : public IPhotoModeFieldSink {
public:
    ~PcgPhotoModePhotoAuthority() override;
    /** @brief Register or revoke this instance as the unique Photo-domain owner. */
    void setAuthority(bool enabled);
    /** @brief Return the complete retained Photo-domain state. */
    const PcgPhotoModePhotoState& state() const noexcept { return state_; }
    /** @brief Test whether retained lighting identity matches the current scene profile. */
    bool lightingProfileMatches(int currentProfile) const noexcept;
    /** @brief Resolve Pcg's fixed resolution index; index zero returns 0x0 for screen size. */
    PcgScreenshotSize screenshotSize() const noexcept;
    /**
     * @brief Return the lowercase extension for the retained screenshot format.
     * @ownership The returned pointer borrows immutable static storage.
     * @lifetime Valid for the lifetime of the process.
     */
    const char* screenshotExtension() const noexcept;

    PhotoModeFieldAcceptance acceptsPhotoModeField(const PhotoModeAssignment& assignment) const noexcept override;
    [[nodiscard]] Result<void> applyPhotoModeField(const PhotoModeAssignment& assignment) override;

private:
    PcgPhotoModePhotoState state_{};
    bool authority_ = false;
};

}  // namespace eve::ui
