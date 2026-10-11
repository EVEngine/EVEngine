#include "weather/PcgInteriorWeatherVolume.h"

#include <cmath>

namespace eve::weather {
namespace {
Result<void> invalidVolume(const char* message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message,
                                                    "weather.interiorVolume"));
}
bool validMode(int mode) {
    return mode == static_cast<int>(PcgInteriorWeatherMode::Collision) ||
           mode == static_cast<int>(PcgInteriorWeatherMode::DisableVfx);
}
bool finite3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}
}  // namespace

Result<void> PcgInteriorWeatherVolume::configureBox(float centerX, float centerY, float centerZ,
                                                      float sizeX, float sizeY, float sizeZ,
                                                      int mode, int interiorReverbPreset,
                                                      int exteriorReverbPreset) {
    if (!finite3(centerX, centerY, centerZ) || !finite3(sizeX, sizeY, sizeZ) ||
        sizeX <= 0.f || sizeY <= 0.f || sizeZ <= 0.f)
        return invalidVolume("box center and positive dimensions must be finite");
    if (!validMode(mode) || interiorReverbPreset < 0 || exteriorReverbPreset < 0)
        return invalidVolume("mode and reverb preset identifiers are invalid");
    shape_ = PcgInteriorWeatherShape::Box;
    mode_ = static_cast<PcgInteriorWeatherMode>(mode);
    centerX_ = centerX; centerY_ = centerY; centerZ_ = centerZ;
    extentX_ = sizeX * .5f; extentY_ = sizeY * .5f; extentZ_ = sizeZ * .5f;
    interiorReverbPreset_ = interiorReverbPreset;
    exteriorReverbPreset_ = exteriorReverbPreset;
    return Result<void>::success();
}

Result<void> PcgInteriorWeatherVolume::configureSphere(float centerX, float centerY, float centerZ,
                                                         float radius, int mode,
                                                         int interiorReverbPreset,
                                                         int exteriorReverbPreset) {
    if (!finite3(centerX, centerY, centerZ) || !std::isfinite(radius) || radius <= 0.f)
        return invalidVolume("sphere center and positive radius must be finite");
    if (!validMode(mode) || interiorReverbPreset < 0 || exteriorReverbPreset < 0)
        return invalidVolume("mode and reverb preset identifiers are invalid");
    shape_ = PcgInteriorWeatherShape::Sphere;
    mode_ = static_cast<PcgInteriorWeatherMode>(mode);
    centerX_ = centerX; centerY_ = centerY; centerZ_ = centerZ;
    extentX_ = radius; extentY_ = radius; extentZ_ = radius;
    interiorReverbPreset_ = interiorReverbPreset;
    exteriorReverbPreset_ = exteriorReverbPreset;
    return Result<void>::success();
}

bool PcgInteriorWeatherVolume::contains(float x, float y, float z) const noexcept {
    if (!finite3(x, y, z)) return false;
    const float dx = x - centerX_, dy = y - centerY_, dz = z - centerZ_;
    if (shape_ == PcgInteriorWeatherShape::Sphere)
        return dx * dx + dy * dy + dz * dz <= extentX_ * extentX_;
    return std::fabs(dx) <= extentX_ && std::fabs(dy) <= extentY_ &&
           std::fabs(dz) <= extentZ_;
}
}  // namespace eve::weather
