#include "graphics/WaterUnderwaterEffects.h"
#include "graphics/Graphics.h"
#include "graphics/Volumetric.h"
#include "graphics/RenderSystem3D.h"

#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::graphics {

void exposeWaterUnderwaterEffectsBindings(ssq::Table& table) {
    auto gradient = table.addClass("UnderwaterColorGradient", ssq::Class::Ctor<UnderwaterColorGradient()>());
    gradient.addFunc("addStop", [vm = table.getHandle()](UnderwaterColorGradient* value, float time,
                                                          float r, float g, float b) {
        auto result = value ? value->addStop(time, r, g, b)
                            : Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "water.underwater: non-null gradient required"));
        return eve::script::projectResult(vm, std::move(result));
    });
    gradient.addFunc("clear", &UnderwaterColorGradient::clear);

    auto curve = table.addClass("UnderwaterScalarCurve", ssq::Class::Ctor<UnderwaterScalarCurve()>());
    curve.addFunc("addKey", [vm = table.getHandle()](UnderwaterScalarCurve* value, float time, float sample) {
        auto result = value ? value->addKey(time, sample)
                            : Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                      "water.underwater: non-null curve required"));
        return eve::script::projectResult(vm, std::move(result));
    });
    curve.addFunc("clear", &UnderwaterScalarCurve::clear);

    auto settings = table.addClass("WaterUnderwaterSettings", ssq::Class::Ctor<WaterUnderwaterSettings()>());
    settings.addVar("enabled", &WaterUnderwaterSettings::enabled);
    settings.addVar("supportFog", &WaterUnderwaterSettings::supportFog);
    settings.addVar("supportPostFx", &WaterUnderwaterSettings::supportPostFx);
    settings.addVar("enableTransitionFx", &WaterUnderwaterSettings::enableTransitionFx);
    settings.addVar("useCaustics", &WaterUnderwaterSettings::useCaustics);
    settings.addVar("hdrp", &WaterUnderwaterSettings::hdrp);
    settings.addVar("overrideFogColor", &WaterUnderwaterSettings::overrideFogColor);
    settings.addVar("fogDepth", &WaterUnderwaterSettings::fogDepth);
    settings.addVar("fogDistance", &WaterUnderwaterSettings::fogDistance);
    settings.addVar("hdrpFogDistance", &WaterUnderwaterSettings::hdrpFogDistance);
    settings.addVar("nearFogDistance", &WaterUnderwaterSettings::nearFogDistance);
    settings.addVar("fogDensity", &WaterUnderwaterSettings::fogDensity);
    settings.addVar("playbackVolume", &WaterUnderwaterSettings::playbackVolume);
    settings.addVar("causticSize", &WaterUnderwaterSettings::causticSize);
    settings.addVar("framesPerSecond", &WaterUnderwaterSettings::framesPerSecond);
    settings.addVar("causticTextureCount", &WaterUnderwaterSettings::causticTextureCount);
    settings.addVar("overrideFogCurve", &WaterUnderwaterSettings::overrideFogCurve);
    settings.addVar("anisotropyShallow", &WaterUnderwaterSettings::anisotropyShallow);
    settings.addVar("anisotropyDeep", &WaterUnderwaterSettings::anisotropyDeep);
    settings.addVar("timeDrivenPostFx", &WaterUnderwaterSettings::timeDrivenPostFx);
    settings.addVar("constantPostExposure", &WaterUnderwaterSettings::constantPostExposure);
    settings.addVar("transitionHalfHeight", &WaterUnderwaterSettings::transitionHalfHeight);
    settings.addVar("transitionBlendDistance", &WaterUnderwaterSettings::transitionBlendDistance);
    settings.addVar("transitionVignette", &WaterUnderwaterSettings::transitionVignette);
    settings.addVar("transitionVignetteSmoothness", &WaterUnderwaterSettings::transitionVignetteSmoothness);
    settings.addVar("transitionLensDistortion", &WaterUnderwaterSettings::transitionLensDistortion);
    settings.addVar("transitionLensScale", &WaterUnderwaterSettings::transitionLensScale);
    settings.addFunc("setTransitionLift", [](WaterUnderwaterSettings* value, float r, float g, float b) {
        value->transitionLift = {r, g, b};
    });
    settings.addFunc("setTransitionInverseGamma", [](WaterUnderwaterSettings* value, float r, float g, float b) {
        value->transitionInverseGamma = {r, g, b};
    });
    settings.addFunc("setTransitionGain", [](WaterUnderwaterSettings* value, float r, float g, float b) {
        value->transitionGain = {r, g, b};
    });
    settings.addFunc("setTransitionColorFilter", [](WaterUnderwaterSettings* value, float r, float g, float b) {
        value->transitionColorFilter = {r, g, b};
    });
    settings.addFunc("setFogColorMultiplier", [](WaterUnderwaterSettings* value, float r, float g, float b) {
        value->fogColorMultiplier = {r, g, b};
    });
    settings.addFunc("setOverrideFogMultiplier", [](WaterUnderwaterSettings* value, float r, float g, float b) {
        value->overrideFogMultiplier = {r, g, b};
    });
    settings.addFunc("setConstantPostColor", [](WaterUnderwaterSettings* value, float r, float g, float b) {
        value->constantPostColor = {r, g, b};
    });

    auto input = table.addClass("WaterUnderwaterInput", ssq::Class::Ctor<WaterUnderwaterInput()>());
    input.addVar("cameraY", &WaterUnderwaterInput::cameraY);
    input.addVar("seaLevel", &WaterUnderwaterInput::seaLevel);
    input.addVar("timeOfDay", &WaterUnderwaterInput::timeOfDay);
    input.addVar("causticTicks", &WaterUnderwaterInput::causticTicks);
    input.addVar("hasMainLight", &WaterUnderwaterInput::hasMainLight);
    input.addFunc("setMainLightColor", [](WaterUnderwaterInput* value, float r, float g, float b) {
        value->mainLightColor = {r, g, b};
    });

    auto state = table.addClass("WaterUnderwaterState", ssq::Class::Ctor<WaterUnderwaterState()>());
    state.addVar("initialized", &WaterUnderwaterState::initialized);
    state.addVar("isUnderwater", &WaterUnderwaterState::isUnderwater);
    state.addVar("causticFrame", &WaterUnderwaterState::causticFrame);

    auto triggerState = table.addClass("WaterUnderwaterDisableTriggerState",
                                       ssq::Class::Ctor<WaterUnderwaterDisableTriggerState()>());
    triggerState.addVar("effectsEnabled", &WaterUnderwaterDisableTriggerState::effectsEnabled);
    auto triggerEvent = table.addClass("WaterUnderwaterTriggerEvent",
                                       ssq::Class::Ctor<WaterUnderwaterTriggerEvent()>());
    triggerEvent.addVar("sensorTag", &WaterUnderwaterTriggerEvent::sensorTag);
    triggerEvent.addVar("visitorTag", &WaterUnderwaterTriggerEvent::visitorTag);
    triggerEvent.addVar("entered", &WaterUnderwaterTriggerEvent::entered);
    triggerEvent.addVar("exited", &WaterUnderwaterTriggerEvent::exited);

    auto output = table.addClass("WaterUnderwaterOutput", ssq::Class::Ctor<WaterUnderwaterOutput()>());
#define EV_UW_BOOL(name) output.addVar(#name, &WaterUnderwaterOutput::name)
    EV_UW_BOOL(isUnderwater); EV_UW_BOOL(entered); EV_UW_BOOL(exited); EV_UW_BOOL(playSubmergeDown);
    EV_UW_BOOL(playSubmergeUp); EV_UW_BOOL(loopAudio); EV_UW_BOOL(particles); EV_UW_BOOL(horizon);
    EV_UW_BOOL(surfaceVfx); EV_UW_BOOL(postFx); EV_UW_BOOL(transitionFx); EV_UW_BOOL(caustics);
#undef EV_UW_BOOL
    output.addVar("causticFrame", &WaterUnderwaterOutput::causticFrame);
    output.addVar("causticSize", &WaterUnderwaterOutput::causticSize);
    output.addVar("fogDensity", &WaterUnderwaterOutput::fogDensity);
    output.addVar("fogStart", &WaterUnderwaterOutput::fogStart);
    output.addVar("fogEnd", &WaterUnderwaterOutput::fogEnd);
    output.addVar("fogHeight", &WaterUnderwaterOutput::fogHeight);
    output.addVar("depth01", &WaterUnderwaterOutput::depth01);
    output.addVar("hdrpBaseHeight", &WaterUnderwaterOutput::hdrpBaseHeight);
    output.addVar("hdrpMeanFreePath", &WaterUnderwaterOutput::hdrpMeanFreePath);
    output.addVar("hdrpProbeDimmer", &WaterUnderwaterOutput::hdrpProbeDimmer);
    output.addVar("hdrpAnisotropy", &WaterUnderwaterOutput::hdrpAnisotropy);
    output.addVar("hdrpDepthExtent", &WaterUnderwaterOutput::hdrpDepthExtent);
    output.addVar("postExposure", &WaterUnderwaterOutput::postExposure);
    output.addVar("transitionWeight", &WaterUnderwaterOutput::transitionWeight);
    output.addVar("transitionVignette", &WaterUnderwaterOutput::transitionVignette);
    output.addVar("transitionVignetteSmoothness", &WaterUnderwaterOutput::transitionVignetteSmoothness);
    output.addVar("transitionLensDistortion", &WaterUnderwaterOutput::transitionLensDistortion);
    output.addVar("transitionLensScale", &WaterUnderwaterOutput::transitionLensScale);
    output.addFunc("getTransitionLiftR", [](const WaterUnderwaterOutput* value) { return value->transitionLift.r; });
    output.addFunc("getTransitionInverseGammaR", [](const WaterUnderwaterOutput* value) { return value->transitionInverseGamma.r; });
    output.addFunc("getTransitionGainR", [](const WaterUnderwaterOutput* value) { return value->transitionGain.r; });
    output.addFunc("getTransitionColorFilterR", [](const WaterUnderwaterOutput* value) {
        return value->transitionColorFilter.r;
    });
    output.addFunc("getFogR", [](const WaterUnderwaterOutput* value) { return value->fogColor.r; });
    output.addFunc("getFogG", [](const WaterUnderwaterOutput* value) { return value->fogColor.g; });
    output.addFunc("getFogB", [](const WaterUnderwaterOutput* value) { return value->fogColor.b; });
    output.addFunc("getPostR", [](const WaterUnderwaterOutput* value) { return value->postColor.r; });
    output.addFunc("getPostG", [](const WaterUnderwaterOutput* value) { return value->postColor.g; });
    output.addFunc("getPostB", [](const WaterUnderwaterOutput* value) { return value->postColor.b; });
    output.addFunc("getUnderwaterMaterialR", [](const WaterUnderwaterOutput* value) {
        return value->underwaterMaterialColor.r;
    });
    output.addFunc("getUnderwaterMaterialG", [](const WaterUnderwaterOutput* value) {
        return value->underwaterMaterialColor.g;
    });
    output.addFunc("getUnderwaterMaterialB", [](const WaterUnderwaterOutput* value) {
        return value->underwaterMaterialColor.b;
    });

    auto surface = table.addClass("WaterSurfaceFogSnapshot", ssq::Class::Ctor<WaterSurfaceFogSnapshot()>());
    surface.addVar("density", &WaterSurfaceFogSnapshot::density);
    surface.addVar("start", &WaterSurfaceFogSnapshot::start);
    surface.addVar("end", &WaterSurfaceFogSnapshot::end);
    surface.addVar("height", &WaterSurfaceFogSnapshot::height);
    surface.addVar("heightFalloff", &WaterSurfaceFogSnapshot::heightFalloff);
    surface.addFunc("setColor", [](WaterSurfaceFogSnapshot* value, float r, float g, float b) {
        value->color = {r, g, b};
    });

    auto surfacePostFx = table.addClass("WaterSurfacePostFxSnapshot",
                                        ssq::Class::Ctor<WaterSurfacePostFxSnapshot()>());
    surfacePostFx.addVar("exposureEv", &WaterSurfacePostFxSnapshot::exposureEv);
    surfacePostFx.addVar("vignette", &WaterSurfacePostFxSnapshot::vignette);
    surfacePostFx.addVar("vignetteSmoothness", &WaterSurfacePostFxSnapshot::vignetteSmoothness);
    surfacePostFx.addVar("lensDistortion", &WaterSurfacePostFxSnapshot::lensDistortion);
    surfacePostFx.addVar("lensScale", &WaterSurfacePostFxSnapshot::lensScale);
    surfacePostFx.addFunc("setColor", [](WaterSurfacePostFxSnapshot* value, float r, float g,
                                          float b) { value->color = {r, g, b}; });
    surfacePostFx.addFunc("setLiftGammaGain", [](WaterSurfacePostFxSnapshot* value,
                                                   float lr, float lg, float lb,
                                                   float gr, float gg, float gb,
                                                   float ar, float ag, float ab) {
        value->lift = {lr, lg, lb};
        value->inverseGamma = {gr, gg, gb};
        value->gain = {ar, ag, ab};
    });

    auto surfaceMaterial = table.addClass("WaterSurfaceMaterialSnapshot",
                                          ssq::Class::Ctor<WaterSurfaceMaterialSnapshot()>());
    surfaceMaterial.addVar("alpha", &WaterSurfaceMaterialSnapshot::alpha);
    surfaceMaterial.addFunc("setColor", [](WaterSurfaceMaterialSnapshot* value, float r, float g,
                                             float b) { value->color = {r, g, b}; });

    table.addFunc("advanceWaterUnderwaterEffects",
                  [vm = table.getHandle()](WaterUnderwaterState* state, WaterUnderwaterOutput* output,
                                           const WaterUnderwaterSettings* settings,
                                           const WaterUnderwaterInput* input,
                                           const UnderwaterColorGradient* depthGradient,
                                           const UnderwaterColorGradient* timeGradient,
                                           const UnderwaterScalarCurve* postExposureCurve,
                                           const UnderwaterColorGradient* postColorGradient) {
        auto result = state && output && settings && input && depthGradient && timeGradient &&
                              postExposureCurve && postColorGradient
                          ? advanceWaterUnderwaterEffects(*state, *output, *settings, *input,
                                                         *depthGradient, *timeGradient, *postExposureCurve,
                                                         *postColorGradient)
                          : Result<void>::failure(Diagnostic::error(
                                DiagnosticCode::InvalidArgument, "water.underwater: non-null arguments required"));
        return eve::script::projectResult(vm, std::move(result));
    });
    table.addFunc("advanceWaterUnderwaterDisableTrigger",
                  [vm = table.getHandle()](WaterUnderwaterDisableTriggerState* state,
                                           WaterUnderwaterTriggerEvent* event,
                                           int expectedSensorTag, int expectedVisitorTag) {
        if (!state || !event)
            return eve::script::projectResult(
                vm, Result<void>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument,
                        "water.underwaterTrigger: state and event are required")));
        return eve::script::projectResult(
            vm, advanceWaterUnderwaterDisableTrigger(*state, *event, expectedSensorTag,
                                                      expectedVisitorTag));
    });
    table.addFunc("applyWaterUnderwaterFog",
                  [vm = table.getHandle()](Volumetric* volumetric, const WaterUnderwaterOutput* output,
                                           const WaterSurfaceFogSnapshot* surface) {
        auto result = output && surface
                          ? applyWaterUnderwaterFog(volumetric, *output, *surface)
                          : Result<void>::failure(Diagnostic::error(
                                DiagnosticCode::InvalidArgument, "water.underwaterFog: non-null arguments required"));
        return eve::script::projectResult(vm, std::move(result));
    });
    table.addFunc("applyWaterUnderwaterPostFx",
                  [vm = table.getHandle()](Graphics* graphics, Camera3D* camera,
                                           const WaterUnderwaterOutput* output,
                                           const WaterSurfacePostFxSnapshot* surface) {
        auto result = output && surface
                          ? applyWaterUnderwaterPostFx(graphics, camera, *output, *surface)
                             : Result<void>::failure(Diagnostic::error(
                                   DiagnosticCode::InvalidArgument,
                                   "water.underwaterPostFx: non-null output and surface required"));
        return eve::script::projectResult(vm, std::move(result));
    });
    table.addFunc("applyWaterUnderwaterHorizon",
                  [vm = table.getHandle()](Renderable3D* horizon,
                                           const WaterUnderwaterOutput* output) {
        auto result = output ? applyWaterUnderwaterHorizon(horizon, *output)
                             : Result<void>::failure(Diagnostic::error(
                                   DiagnosticCode::InvalidArgument,
                                   "water.underwaterHorizon: non-null output required"));
        return eve::script::projectResult(vm, std::move(result));
    });
    table.addFunc("applyWaterUnderwaterMaterial",
                  [vm = table.getHandle()](Renderable3D* renderable,
                                           const WaterUnderwaterOutput* output,
                                           const WaterSurfaceMaterialSnapshot* surface) {
        auto result = output && surface
                          ? applyWaterUnderwaterMaterial(renderable, *output, *surface)
                          : Result<void>::failure(Diagnostic::error(
                                DiagnosticCode::InvalidArgument,
                                "water.underwaterMaterial: output and surface are required"));
        return eve::script::projectResult(vm, std::move(result));
    });
}

}  // namespace eve::graphics
