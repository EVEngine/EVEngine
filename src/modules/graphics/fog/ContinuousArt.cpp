#include "graphics/fog/ContinuousArt.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics::fog {

Result<void> ContinuousArt::setParams(const ContinuousArtParams& params) {
    auto ok01 = [](float v) { return std::isfinite(v) && v >= 0.f && v <= 2.f; };
    if (!std::isfinite(params.colorTint.x) || !std::isfinite(params.colorTint.y) ||
        !std::isfinite(params.colorTint.z) || params.colorTint.x < 0.f || params.colorTint.y < 0.f ||
        params.colorTint.z < 0.f) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "colorTint must be finite and >= 0", "colorTint", {},
            "graphics.fog"));
    }
    if (!ok01(params.edgeBoost) || !ok01(params.silhouetteSoftness) || !ok01(params.internalGlow) ||
        !ok01(params.silverDust)) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "art scalars must be finite and in [0,2]", "art", {},
            "graphics.fog"));
    }
    params_ = params;
    return Result<void>::success();
}

ContinuousArtOutput ContinuousArt::stylize(const FogRayResult& physical, float viewZ,
                                           float neighborOpacity) const noexcept {
    ContinuousArtOutput out;
    const float opacity = std::clamp(1.f - physical.transmittance, 0.f, 1.f);
    out.opacity = opacity;
    out.color = physical.inScatter * params_.colorTint;

    const float depthCue = 1.f - std::exp(-std::max(viewZ, 0.f) * 0.05f);
    out.edgeMask = std::clamp(std::fabs(opacity - neighborOpacity) * (0.5f + params_.edgeBoost), 0.f,
                              1.f);
    out.edgeMask *= std::clamp(1.f - params_.silhouetteSoftness * depthCue, 0.f, 1.f);

    const float glow = params_.internalGlow * opacity * (0.35f + 0.65f * depthCue);
    out.color += glm::vec3(glow) * params_.colorTint;

    // Silver dust: cool highlight on bright in-scatter without touching optics.
    const float luma =
        0.2126f * physical.inScatter.r + 0.7152f * physical.inScatter.g + 0.0722f * physical.inScatter.b;
    out.dustHighlight = std::clamp(params_.silverDust * luma, 0.f, 1.f);
    out.color += glm::vec3(0.85f, 0.9f, 1.f) * out.dustHighlight * 0.15f;
    return out;
}

}  // namespace eve::graphics::fog
