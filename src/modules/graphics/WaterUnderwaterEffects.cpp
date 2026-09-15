#include "graphics/WaterUnderwaterEffects.h"
#include "graphics/Graphics.h"
#include "graphics/Volumetric.h"
#include "graphics/RenderSystem3D.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics {
namespace {
bool finite3(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
}  // namespace

Result<void> UnderwaterColorGradient::addStop(float time, float r, float g, float b) {
    const glm::vec3 color{r, g, b};
    if (!std::isfinite(time) || time < 0.0F || time > 1.0F || !finite3(color))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.underwater: gradient keys require normalized finite values"));
    if (std::any_of(stops_.begin(), stops_.end(), [time](const auto& stop) { return stop.time == time; }))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.underwater: duplicate gradient key time"));
    auto candidate = stops_;
    candidate.push_back({time, color});
    std::stable_sort(candidate.begin(), candidate.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    stops_ = std::move(candidate);
    return Result<void>::success();
}

Result<glm::vec3> UnderwaterColorGradient::evaluate(float time) const {
    if (stops_.empty() || !std::isfinite(time))
        return Result<glm::vec3>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.underwater: nonempty gradient and finite sample required"));
    if (time <= stops_.front().time) return Result<glm::vec3>::success(stops_.front().color);
    if (time >= stops_.back().time) return Result<glm::vec3>::success(stops_.back().color);
    const auto upper = std::upper_bound(stops_.begin(), stops_.end(), time,
                                        [](float t, const auto& stop) { return t < stop.time; });
    const auto& b = *upper;
    const auto& a = *(upper - 1);
    return Result<glm::vec3>::success(glm::mix(a.color, b.color, (time - a.time) / (b.time - a.time)));
}

Result<void> UnderwaterScalarCurve::addKey(float time, float value) {
    if (!std::isfinite(time) || time < 0.0F || time > 1.0F || !std::isfinite(value))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.underwater: scalar keys require normalized finite time"));
    if (std::any_of(keys_.begin(), keys_.end(), [time](const auto& key) { return key.x == time; }))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.underwater: duplicate scalar key time"));
    auto candidate = keys_;
    candidate.push_back({time, value});
    std::stable_sort(candidate.begin(), candidate.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    keys_ = std::move(candidate);
    return Result<void>::success();
}

Result<float> UnderwaterScalarCurve::evaluate(float time) const {
    if (keys_.empty() || !std::isfinite(time))
        return Result<float>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.underwater: nonempty scalar curve and finite sample required"));
    if (time <= keys_.front().x) return Result<float>::success(keys_.front().y);
    if (time >= keys_.back().x) return Result<float>::success(keys_.back().y);
    const auto upper = std::upper_bound(keys_.begin(), keys_.end(), time,
                                        [](float t, const auto& key) { return t < key.x; });
    const auto& b = *upper;
    const auto& a = *(upper - 1);
    return Result<float>::success(lerp(a.y, b.y, (time - a.x) / (b.x - a.x)));
}

Result<void> advanceWaterUnderwaterEffects(
    WaterUnderwaterState& state, WaterUnderwaterOutput& output, const WaterUnderwaterSettings& settings,
    const WaterUnderwaterInput& input, const UnderwaterColorGradient& depthGradient,
    const UnderwaterColorGradient& timeGradient, const UnderwaterScalarCurve& postExposureCurve,
    const UnderwaterColorGradient& postColorGradient) {
    if (!std::isfinite(input.cameraY) || !std::isfinite(input.seaLevel) || !std::isfinite(input.timeOfDay) ||
        input.timeOfDay < 0.0F || input.timeOfDay > 1.0F || !finite3(input.mainLightColor) ||
        input.causticTicks < 0 || !std::isfinite(settings.fogDepth) || settings.fogDepth <= 0.0F ||
        !std::isfinite(settings.fogDistance) || !std::isfinite(settings.hdrpFogDistance) ||
        !std::isfinite(settings.nearFogDistance) || !std::isfinite(settings.fogDensity) || settings.fogDensity < 0.0F ||
        !std::isfinite(settings.playbackVolume) || settings.playbackVolume < 0.0F || settings.playbackVolume > 1.0F ||
        !std::isfinite(settings.causticSize) || settings.causticSize < 1.0F || settings.causticSize > 100.0F ||
        settings.framesPerSecond <= 0 || settings.causticTextureCount < 0 || !finite3(settings.fogColorMultiplier) ||
        !finite3(settings.photoModeFogColor) ||
        !finite3(settings.overrideFogMultiplier) || !std::isfinite(settings.overrideFogCurve) ||
        !std::isfinite(settings.anisotropyShallow) || !std::isfinite(settings.anisotropyDeep) ||
        !std::isfinite(settings.constantPostExposure) || !finite3(settings.constantPostColor) ||
        !std::isfinite(settings.transitionHalfHeight) || settings.transitionHalfHeight < 0.0F ||
        !std::isfinite(settings.transitionBlendDistance) || settings.transitionBlendDistance <= 0.0F ||
        !std::isfinite(settings.transitionVignette) || settings.transitionVignette < 0.0F ||
        settings.transitionVignette > 1.0F || !std::isfinite(settings.transitionVignetteSmoothness) ||
        settings.transitionVignetteSmoothness < 0.01F || settings.transitionVignetteSmoothness > 1.0F ||
        !std::isfinite(settings.transitionLensDistortion) ||
        settings.transitionLensDistortion < 0.0F || settings.transitionLensDistortion > 1.0F ||
        !std::isfinite(settings.transitionLensScale) || settings.transitionLensScale < 0.01F ||
        settings.transitionLensScale > 5.0F ||
        !finite3(settings.transitionLift) || !finite3(settings.transitionInverseGamma) ||
        glm::any(glm::lessThan(settings.transitionInverseGamma, glm::vec3(0.001F))) ||
        !finite3(settings.transitionGain) || !finite3(settings.transitionColorFilter) ||
        glm::any(glm::lessThan(settings.transitionColorFilter, glm::vec3(0.0F)))) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.underwater: finite normalized settings and frame input required"));
    }

    WaterUnderwaterState candidateState = state;
    WaterUnderwaterOutput candidate;
    const bool underwater = settings.enabled && input.cameraY <= input.seaLevel;
    candidate.entered = state.initialized && !state.isUnderwater && underwater;
    candidate.exited = state.initialized && state.isUnderwater && !underwater;
    candidate.isUnderwater = underwater;
    candidate.playSubmergeDown = candidate.entered;
    candidate.playSubmergeUp = candidate.exited || (!settings.enabled && state.isUnderwater);
    candidate.loopAudio = underwater;
    candidate.particles = underwater;
    candidate.horizon = underwater && !settings.hdrp;
    candidate.surfaceVfx = !underwater;
    candidate.postFx = underwater && settings.supportPostFx;
    candidate.transitionFx = underwater && settings.enableTransitionFx;
    candidate.caustics = underwater && settings.useCaustics && settings.causticTextureCount > 0;
    candidate.causticSize = settings.causticSize;
    candidate.underwaterMaterialColor = input.hasMainLight
                                            ? input.mainLightColor
                                            : glm::vec3(207.0F / 255.0F);
    if (candidate.transitionFx) {
        const float belowSurface = input.seaLevel - input.cameraY;
        candidate.transitionWeight = belowSurface <= settings.transitionHalfHeight
                                         ? 1.0F
                                         : std::clamp(1.0F - (belowSurface - settings.transitionHalfHeight) /
                                                               settings.transitionBlendDistance,
                                                      0.0F, 1.0F);
        candidate.transitionVignette = settings.transitionVignette * candidate.transitionWeight;
        candidate.transitionVignetteSmoothness =
            0.2F + (settings.transitionVignetteSmoothness - 0.2F) * candidate.transitionWeight;
        candidate.transitionLensDistortion =
            settings.transitionLensDistortion * candidate.transitionWeight;
        candidate.transitionLensScale =
            1.0F + (settings.transitionLensScale - 1.0F) * candidate.transitionWeight;
        candidate.transitionLift = settings.transitionLift * candidate.transitionWeight;
        candidate.transitionInverseGamma = glm::mix(glm::vec3(1.0F), settings.transitionInverseGamma,
                                                     candidate.transitionWeight);
        candidate.transitionGain = glm::mix(glm::vec3(1.0F), settings.transitionGain,
                                            candidate.transitionWeight);
        candidate.transitionColorFilter = glm::mix(glm::vec3(1.0F), settings.transitionColorFilter,
                                                   candidate.transitionWeight);
    }

    if (candidate.caustics && input.causticTicks > 0) {
        const std::int64_t advanced = static_cast<std::int64_t>(candidateState.causticFrame) + input.causticTicks;
        candidateState.causticFrame = static_cast<int>(advanced % settings.causticTextureCount);
    } else if (!underwater) {
        candidateState.causticFrame = 0;
    }
    candidate.causticFrame = candidateState.causticFrame;

    if (underwater && settings.supportFog) {
        candidate.depth01 = std::clamp((input.seaLevel - input.cameraY) / settings.fogDepth, 0.0F, 1.0F);
        glm::vec3 color;
        if (settings.photoModeFogColorEnabled) {
            color = settings.photoModeFogColor;
        } else {
            auto sampled = settings.overrideFogColor ? timeGradient.evaluate(input.timeOfDay)
                                                     : depthGradient.evaluate(candidate.depth01);
            if (!sampled) return Result<void>::failure(sampled.status());
            color = sampled.value();
            if (settings.overrideFogColor) {
                color *= settings.overrideFogMultiplier * settings.overrideFogCurve;
            } else {
                color *= input.mainLightColor;
                if (settings.fogColorMultiplier.r >= 0.0F) color.r = std::clamp(color.r + settings.fogColorMultiplier.r, 0.0F, 1.0F);
                if (settings.fogColorMultiplier.g >= 0.0F) color.g = std::clamp(color.g + settings.fogColorMultiplier.g, 0.0F, 1.0F);
                if (settings.fogColorMultiplier.b >= 0.0F) color.b = std::clamp(color.b + settings.fogColorMultiplier.b, 0.0F, 1.0F);
            }
        }        candidate.fogColor = color;
        candidate.fogDensity = settings.fogDensity;
        candidate.fogStart = settings.nearFogDistance;
        candidate.fogEnd = settings.hdrp ? settings.hdrpFogDistance : settings.fogDistance;
        const float d = candidate.depth01;
        candidate.hdrpBaseHeight = lerp(-150.0F, 6000.0F, d);
        candidate.fogHeight = settings.hdrp ? candidate.hdrpBaseHeight : input.seaLevel;
        candidate.hdrpMeanFreePath = lerp(3000.0F, 1.0F, d);
        candidate.fogColor = settings.hdrp ? glm::mix(color, color / 6.0F, d) : color;
        candidate.hdrpProbeDimmer = lerp(1.0F, 0.325F, d);
        candidate.hdrpAnisotropy = lerp(settings.anisotropyShallow, settings.anisotropyDeep, d);
        candidate.hdrpDepthExtent = lerp(150.0F, 300.0F, d);
    }
    if (underwater && settings.supportPostFx) {
        if (settings.timeDrivenPostFx) {
            auto exposure = postExposureCurve.evaluate(input.timeOfDay);
            if (!exposure) return Result<void>::failure(exposure.status());
            auto color = postColorGradient.evaluate(input.timeOfDay);
            if (!color) return Result<void>::failure(color.status());
            candidate.postExposure = exposure.value();
            candidate.postColor = color.value();
        } else {
            candidate.postExposure = settings.constantPostExposure;
            candidate.postColor = settings.constantPostColor;
        }
    }

    candidateState.initialized = true;
    candidateState.isUnderwater = underwater;
    state = candidateState;
    output = candidate;
    return Result<void>::success();
}

Result<void> advanceWaterUnderwaterDisableTrigger(
    WaterUnderwaterDisableTriggerState& state, const WaterUnderwaterTriggerEvent& event,
    int expectedSensorTag, int expectedVisitorTag) {
    if (expectedSensorTag < 0 || expectedVisitorTag < 0 || event.sensorTag < 0 ||
        event.visitorTag < 0 || event.entered == event.exited) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "water.underwaterTrigger: non-negative tags and exactly one event direction required"));
    }
    if (event.sensorTag != expectedSensorTag || event.visitorTag != expectedVisitorTag)
        return Result<void>::success();
    state.effectsEnabled = event.exited;
    return Result<void>::success();
}

Result<void> applyWaterUnderwaterFog(Volumetric* volumetric, const WaterUnderwaterOutput& output,
                                     const WaterSurfaceFogSnapshot& surface) {
    if (!volumetric || !finite3(output.fogColor) || !std::isfinite(output.fogDensity) ||
        output.fogDensity < 0.0F || !std::isfinite(output.fogStart) || !std::isfinite(output.fogEnd) ||
        !finite3(surface.color) || !std::isfinite(surface.density) || surface.density < 0.0F ||
        !std::isfinite(surface.start) || !std::isfinite(surface.end) || surface.end <= surface.start ||
        !std::isfinite(surface.height) || !std::isfinite(surface.heightFalloff) || surface.heightFalloff < 0.0F) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.underwaterFog: valid provider and finite fog snapshot required"));
    }
    const glm::vec3 color = output.isUnderwater ? output.fogColor : surface.color;
    const float density = output.isUnderwater ? output.fogDensity : surface.density;
    const float start = output.isUnderwater ? output.fogStart : surface.start;
    const float end = output.isUnderwater ? output.fogEnd : surface.end;
    if (end <= std::max(0.0F, start))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.underwaterFog: fog end must exceed the clamped start"));
    volumetric->setMode("fog");
    volumetric->setFogColor(color.r, color.g, color.b);
    volumetric->setDensity(density);
    volumetric->setFogStart(start);
    volumetric->setFogEnd(end);
    volumetric->setFogHeight(output.isUnderwater ? output.fogHeight : surface.height);
    volumetric->setFogHeightFalloff(output.isUnderwater ? 0.0F : surface.heightFalloff);
    return Result<void>::success();
}

Result<void> applyWaterUnderwaterPostFx(Graphics* graphics, Camera3D* camera,
                                        const WaterUnderwaterOutput& output,
                                        const WaterSurfacePostFxSnapshot& surface) {
    if (!graphics || !camera || !std::isfinite(output.postExposure) ||
        !finite3(output.postColor) || !std::isfinite(surface.exposureEv) ||
        !finite3(surface.color) || glm::any(glm::lessThan(surface.color, glm::vec3(0.0F))) ||
        !std::isfinite(output.transitionVignette) || output.transitionVignette < 0.0F ||
        output.transitionVignette > 1.0F || !std::isfinite(output.transitionVignetteSmoothness) ||
        output.transitionVignetteSmoothness < 0.01F || output.transitionVignetteSmoothness > 1.0F ||
        !std::isfinite(output.transitionLensDistortion) ||
        output.transitionLensDistortion < 0.0F || output.transitionLensDistortion > 1.0F ||
        !std::isfinite(output.transitionLensScale) || output.transitionLensScale < 0.01F ||
        output.transitionLensScale > 5.0F ||
        !std::isfinite(surface.vignette) || surface.vignette < 0.0F || surface.vignette > 1.0F ||
        !std::isfinite(surface.vignetteSmoothness) || surface.vignetteSmoothness < 0.01F ||
        surface.vignetteSmoothness > 1.0F ||
        !std::isfinite(surface.lensDistortion) || surface.lensDistortion < 0.0F ||
        surface.lensDistortion > 1.0F || !std::isfinite(surface.lensScale) ||
        surface.lensScale < 0.01F || surface.lensScale > 5.0F || !finite3(output.transitionLift) ||
        !finite3(output.transitionInverseGamma) ||
        glm::any(glm::lessThan(output.transitionInverseGamma, glm::vec3(0.001F))) ||
        !finite3(output.transitionGain) || !finite3(output.transitionColorFilter) ||
        glm::any(glm::lessThan(output.transitionColorFilter, glm::vec3(0.0F))) || !finite3(surface.lift) ||
        !finite3(surface.inverseGamma) ||
        glm::any(glm::lessThan(surface.inverseGamma, glm::vec3(0.001F))) || !finite3(surface.gain))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "water.underwaterPostFx: valid providers and non-negative finite surface values required"));
    const bool postActive = output.isUnderwater && output.postFx;
    const bool transitionActive = output.isUnderwater && output.transitionFx;
    camera->setExposure(postActive ? output.postExposure : surface.exposureEv);
    const glm::vec3 baseColor = postActive ? output.postColor : surface.color;
    graphics->setSceneColorFilter(baseColor *
                                  (transitionActive ? output.transitionColorFilter : glm::vec3(1.0F)));
    graphics->setSceneTransitionFx(
        transitionActive ? output.transitionVignette : surface.vignette,
        transitionActive ? output.transitionVignetteSmoothness : surface.vignetteSmoothness,
        transitionActive ? output.transitionLensDistortion : surface.lensDistortion,
        transitionActive ? output.transitionLensScale : surface.lensScale);
    graphics->setSceneLiftGammaGain(
        transitionActive ? output.transitionLift : surface.lift,
        transitionActive ? output.transitionInverseGamma : surface.inverseGamma,
        transitionActive ? output.transitionGain : surface.gain);
    return Result<void>::success();
}

Result<void> applyWaterUnderwaterHorizon(Renderable3D* horizon,
                                         const WaterUnderwaterOutput& output) {
    if (!horizon)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.underwaterHorizon: non-null renderable required"));
    horizon->setVisible(output.horizon);
    return Result<void>::success();
}

Result<void> applyWaterUnderwaterMaterial(Renderable3D* renderable,
                                          const WaterUnderwaterOutput& output,
                                          const WaterSurfaceMaterialSnapshot& surface) {
    if (!renderable || !finite3(output.underwaterMaterialColor) ||
        glm::any(glm::lessThan(output.underwaterMaterialColor, glm::vec3(0.0F))) ||
        !finite3(surface.color) || glm::any(glm::lessThan(surface.color, glm::vec3(0.0F))) ||
        !std::isfinite(surface.alpha) || surface.alpha < 0.0F || surface.alpha > 1.0F) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "water.underwaterMaterial: valid renderable and non-negative finite tint required"));
    }
    const glm::vec3 color = output.isUnderwater ? output.underwaterMaterialColor : surface.color;
    renderable->setTint(color.r, color.g, color.b, surface.alpha);
    return Result<void>::success();
}

}  // namespace eve::graphics
