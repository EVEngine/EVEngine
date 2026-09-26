#include "graphics/Graphics.h"
#include "graphics/DisplayOutputEncoding.h"

#include <cmath>

namespace eve::graphics {

Result<void> Graphics::setDisplayOutputMode(DisplayOutputMode mode) {
    if (mode != DisplayOutputMode::Sdr && mode != DisplayOutputMode::Auto &&
        mode != DisplayOutputMode::Hdr10 && mode != DisplayOutputMode::ScRgb)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Unknown display output mode",
                                                       "graphics.presentation"));
    if (mode == DisplayOutputMode::Hdr10 || mode == DisplayOutputMode::ScRgb)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                       "This backend does not support display HDR present",
                                                       "graphics.presentation"));
    displayOutputMode_ = mode;
    activeDisplayColorSpace_ = DisplayColorSpace::Sdr;
    return Result<void>::success();
}

Result<void> Graphics::setDisplayHdrCalibration(float paperWhiteNits, float peakNits) {
    if (!std::isfinite(paperWhiteNits) || !std::isfinite(peakNits))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Display HDR calibration must be finite",
                                                       "graphics.presentation"));
    display::clampCalibration(paperWhiteNits, peakNits);
    displayPaperWhiteNits_ = paperWhiteNits;
    displayPeakNits_ = peakNits;
    return Result<void>::success();
}

Result<Graphics::DisplayOutputSupport> Graphics::queryDisplayOutputSupport() const {
    DisplayOutputSupport support{};
    return Result<DisplayOutputSupport>::success(support);
}

Graphics::DisplayOutputMode Graphics::getDisplayOutputMode() const { return displayOutputMode_; }
float Graphics::getDisplayPaperWhiteNits() const { return displayPaperWhiteNits_; }
float Graphics::getDisplayPeakNits() const { return displayPeakNits_; }
Graphics::DisplayColorSpace Graphics::getActiveDisplayColorSpace() const {
    return activeDisplayColorSpace_;
}
bool Graphics::isDisplayHdrActive() const {
    return activeDisplayColorSpace_ != DisplayColorSpace::Sdr;
}

}  // namespace eve::graphics
