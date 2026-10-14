#include "graphics/fog/ContinuousArt.h"
#include <algorithm>
#include <cmath>
#include <glm/vec3.hpp>
#include "common/Diagnostic.h"
#include "graphics/fog/SceneWind.h"

// ContinuousArt.cpp
namespace eve::graphics::fog {

Result<void> ContinuousArt::setParams(const ContinuousArtParams& params) {
    auto ok01 = [](float v) { return std::isfinite(v) && v >= 0.f && v <= 2.f; };
    if (!std::isfinite(params.colorTint.x) || !std::isfinite(params.colorTint.y) ||
        !std::isfinite(params.colorTint.z) || params.colorTint.x < 0.f || params.colorTint.y < 0.f ||
        params.colorTint.z < 0.f) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "colorTint must be finite and >= 0", "colorTint", {}, "graphics.fog"));
    }
    if (!ok01(params.edgeBoost) || !ok01(params.silhouetteSoftness) || !ok01(params.internalGlow) ||
        !ok01(params.silverDust)) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "art scalars must be finite and in [0,2]", "art", {}, "graphics.fog"));
    }
    params_ = params;
    return Result<void>::success();
}

ContinuousArtOutput ContinuousArt::stylize(const FogRayResult& physical, float viewZ,
                                           float neighborOpacity) const noexcept {
    ContinuousArtOutput out;
    const float         opacity = std::clamp(1.f - physical.transmittance, 0.f, 1.f);
    out.opacity                 = opacity;
    out.color                   = physical.inScatter * params_.colorTint;

    const float depthCue = 1.f - std::exp(-std::max(viewZ, 0.f) * 0.05f);
    out.edgeMask         = std::clamp(std::fabs(opacity - neighborOpacity) * (0.5f + params_.edgeBoost), 0.f, 1.f);
    out.edgeMask *= std::clamp(1.f - params_.silhouetteSoftness * depthCue, 0.f, 1.f);

    const float glow = params_.internalGlow * opacity * (0.35f + 0.65f * depthCue);
    out.color += glm::vec3(glow) * params_.colorTint;

    // Silver dust: cool highlight on bright in-scatter without touching optics.
    const float luma = 0.2126f * physical.inScatter.r + 0.7152f * physical.inScatter.g + 0.0722f * physical.inScatter.b;
    out.dustHighlight = std::clamp(params_.silverDust * luma, 0.f, 1.f);
    out.color += glm::vec3(0.85f, 0.9f, 1.f) * out.dustHighlight * 0.15f;
    return out;
}

}  // namespace eve::graphics::fog

// SceneWind.cpp
namespace eve::graphics::fog {
namespace {

[[nodiscard]] float approach(float current, float target, float maxDelta) noexcept {
    const float delta = target - current;
    if (std::fabs(delta) <= maxDelta) return target;
    return current + std::copysign(maxDelta, delta);
}

[[nodiscard]] glm::vec3 approach3(const glm::vec3& current, const glm::vec3& target, float maxDelta) noexcept {
    return {approach(current.x, target.x, maxDelta), approach(current.y, target.y, maxDelta),
            approach(current.z, target.z, maxDelta)};
}

}  // namespace

Result<void> SceneWind::setMainWind(const glm::vec3& metersPerSecond) {
    if (!std::isfinite(metersPerSecond.x) || !std::isfinite(metersPerSecond.y) || !std::isfinite(metersPerSecond.z)) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "main wind must be finite",
                                                       "mainWind", {}, "graphics.fog"));
    }
    targetMain_ = metersPerSecond;
    return Result<void>::success();
}

Result<void> SceneWind::setCurlStrength(float metersPerSecond) {
    if (!std::isfinite(metersPerSecond) || metersPerSecond < 0.f) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "curl strength must be finite and >= 0", "curlStrength", {},
                                                       "graphics.fog"));
    }
    targetCurl_ = metersPerSecond;
    return Result<void>::success();
}

Result<void> SceneWind::setCurlFrequency(float frequency) {
    if (!std::isfinite(frequency) || frequency <= 0.f) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "curl frequency must be finite and > 0", "curlFrequency", {},
                                                       "graphics.fog"));
    }
    curlFrequency_ = frequency;
    return Result<void>::success();
}

Result<void> SceneWind::setCurlTimeScale(float scale) {
    if (!std::isfinite(scale) || scale < 0.f) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "curl time scale must be finite and >= 0", "curlTimeScale", {},
                                                       "graphics.fog"));
    }
    curlTimeScale_ = scale;
    return Result<void>::success();
}

Result<void> SceneWind::setResponseRate(float metersPerSecondPerSecond) {
    if (!std::isfinite(metersPerSecondPerSecond) || metersPerSecondPerSecond <= 0.f) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "response rate must be finite and > 0", "responseRate", {},
                                                       "graphics.fog"));
    }
    responseRate_ = metersPerSecondPerSecond;
    return Result<void>::success();
}

Result<void> SceneWind::tick(float dt) {
    if (!std::isfinite(dt) || dt < 0.f) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "dt must be finite and >= 0", "dt", {}, "graphics.fog"));
    }
    const float maxDelta = responseRate_ * dt;
    currentMain_         = approach3(currentMain_, targetMain_, maxDelta);
    currentCurl_         = approach(currentCurl_, targetCurl_, maxDelta);
    return Result<void>::success();
}

glm::vec3 SceneWind::sample(const glm::vec3& world, float timeSeconds) const noexcept {
    const float t = timeSeconds * curlTimeScale_;
    const float f = curlFrequency_;
    // Analytic curl of a smooth potential → locally swirling, nearly div-free.
    const float     sx = std::sin(f * world.y + 0.7f * t);
    const float     sy = std::sin(f * world.z + 1.1f * t);
    const float     sz = std::sin(f * world.x + 0.5f * t);
    const float     cx = std::cos(f * world.z + 0.9f * t);
    const float     cy = std::cos(f * world.x + 0.3f * t);
    const float     cz = std::cos(f * world.y + 1.3f * t);
    const glm::vec3 curl(currentCurl_ * (sy - cz), currentCurl_ * (sz - cx), currentCurl_ * (sx - cy));
    return currentMain_ + curl;
}

}  // namespace eve::graphics::fog
