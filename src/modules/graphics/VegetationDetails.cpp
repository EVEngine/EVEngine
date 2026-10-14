#include "graphics/VegetationDetails.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace eve::graphics {
namespace {
void exactFields(const Value& value, std::initializer_list<std::string_view> names) {
    const auto* object = value.getIf<Value::Object>();
    if (!object || object->size() != names.size()) throw std::invalid_argument("unexpected Global Details fields");
    for (const auto name : names)
        if (!object->contains(std::string(name))) throw std::invalid_argument("missing Global Details field");
}

const Value& member(const Value& value, std::string_view name) {
    return value.getIf<Value::Object>()->at(std::string(name));
}

float finiteNumber(const Value& value) {
    if (!value.isInt64() && !value.isDouble()) throw std::invalid_argument("Global Details number is invalid");
    const float result = value.isDouble() ? float(value.asDouble()) : float(value.asInt());
    if (!std::isfinite(result)) throw std::invalid_argument("Global Details number is non-finite");
    return result;
}

uint8_t layerNumber(const Value& value) {
    if (!value.isInt64() || value.asInt() < 0 || value.asInt() > 8)
        throw std::invalid_argument("Global Details layer is outside [0,8]");
    return uint8_t(value.asInt());
}
}  // namespace

Result<VegetationDetailRuntime> configureVegetationDetails(const PbrSurface&               baseSurface,
                                                           const VegetationMotion&         baseMotion,
                                                           const VegetationDetailSettings& s) {
    auto unit  = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
    auto range = [](float value, float maximum) { return std::isfinite(value) && value >= 0 && value <= maximum; };
    if (s.colorsLayer > 8 || s.extrasLayer > 8 || s.motionLayer > 8 || !unit(s.globalColor) || !unit(s.globalAlpha) ||
        !unit(s.globalOverlay) || !unit(s.globalWetness) || !unit(s.colorMaskMinimum) || !unit(s.colorMaskMaximum) ||
        !unit(s.overlayMaskMinimum) || !unit(s.overlayMaskMaximum) || !unit(s.alphaThreshold) ||
        s.colorMaskMaximum - s.colorMaskMinimum + .0001f == 0 ||
        s.overlayMaskMaximum - s.overlayMaskMinimum + .0001f == 0 || !range(s.perspectivePush, 4) ||
        !range(s.perspectiveNoise, 4) || !range(s.perspectiveAngle, 8) || !range(s.bendingAmplitude, 2) ||
        !range(s.bendingSpeed, 40) || !range(s.bendingScale, 20) || !range(s.flutterAmplitude, 2) ||
        !range(s.flutterSpeed, 40) || !range(s.flutterScale, 20) || !range(s.interactionAmplitude, 2))
        return Result<VegetationDetailRuntime>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "invalid TVE Global Details values", {}, {}, "graphics.vegetation"));
    for (float value : s.motionHighlight)
        if (!std::isfinite(value) || value < 0)
            return Result<VegetationDetailRuntime>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "invalid TVE motion highlight", {}, {}, "graphics.vegetation"));

    VegetationDetailRuntime result{baseSurface,           baseMotion,           s.colorMaskMinimum,
                                   s.colorMaskMaximum,    s.overlayMaskMinimum, s.overlayMaskMaximum,
                                   s.alphaThreshold - .5f};
    result.surface.vegetationColors.layer = s.colorsLayer;
    result.surface.vegetationExtras.layer = s.extrasLayer;
    result.surface.vegetationMotion.layer = s.motionLayer;
    result.surface.vegetationColor.colorsCoverage *= s.globalColor;
    result.surface.vegetationColor.overlay *= s.globalOverlay;
    result.surface.vegetationColor.wetness *= s.globalWetness;
    result.surface.vegetationColor.globalColorMaskMinimum     = s.colorMaskMinimum;
    result.surface.vegetationColor.globalColorMaskMaximum     = s.colorMaskMaximum;
    result.surface.vegetationColor.globalOverlayMaskMinimum   = s.overlayMaskMinimum;
    result.surface.vegetationColor.globalOverlayMaskMaximum   = s.overlayMaskMaximum;
    result.surface.vegetationColor.globalAlphaThresholdOffset = s.alphaThreshold - .5f;
    const float materialAlpha = result.surface.vegetationAlpha.enabled ? result.surface.vegetationAlpha.global : 1.f;
    result.surface.vegetationAlpha.enabled = true;
    result.surface.vegetationAlpha.global  = materialAlpha * s.globalAlpha;
    result.surface.motionHighlightColor    = s.motionHighlight;
    auto& gpu                              = result.surface.vegetationMotion;
    gpu.bending                            = s.bendingAmplitude;
    gpu.bendingSpeed                       = s.bendingSpeed;
    gpu.bendingScale                       = s.bendingScale;
    gpu.flutter                            = s.flutterAmplitude;
    gpu.flutterSpeed                       = s.flutterSpeed;
    gpu.flutterScale                       = s.flutterScale;
    gpu.interaction                        = s.interactionAmplitude;
    gpu.perspectivePush                    = s.perspectivePush;
    gpu.perspectiveNoise                   = s.perspectiveNoise;
    gpu.perspectiveAngle                   = s.perspectiveAngle;
    auto& cpu                              = result.motion;
    cpu.motionLayer                        = s.motionLayer;
    cpu.bending                            = gpu.bending;
    cpu.bendingSpeed                       = gpu.bendingSpeed;
    cpu.bendingScale                       = gpu.bendingScale;
    cpu.flutter                            = gpu.flutter;
    cpu.flutterSpeed                       = gpu.flutterSpeed;
    cpu.flutterScale                       = gpu.flutterScale;
    cpu.interaction                        = gpu.interaction;
    cpu.perspectivePush                    = gpu.perspectivePush;
    cpu.perspectiveNoise                   = gpu.perspectiveNoise;
    cpu.perspectiveAngle                   = gpu.perspectiveAngle;
    auto valid                             = validatePbrSurface(result.surface);
    if (!valid) return Result<VegetationDetailRuntime>::failure(valid.status());
    return Result<VegetationDetailRuntime>::success(std::move(result));
}

Result<Value> snapshotVegetationDetails(const VegetationDetailSettings& settings) {
    auto validated = configureVegetationDetails(PbrSurface{}, VegetationMotion{}, settings);
    if (!validated) return Result<Value>::failure(validated.status());
    try {
        return Result<Value>::success(Value::object(
            {{"schema", "eve.graphics.vegetation-details"},
             {"version", 1},
             {"layers",
              Value::array({int(settings.colorsLayer), int(settings.extrasLayer), int(settings.motionLayer)})},
             {"global", Value::array({settings.globalColor, settings.globalAlpha, settings.globalOverlay,
                                      settings.globalWetness})},
             {"colorMask", Value::array({settings.colorMaskMinimum, settings.colorMaskMaximum})},
             {"overlayMask", Value::array({settings.overlayMaskMinimum, settings.overlayMaskMaximum})},
             {"alphaThreshold", settings.alphaThreshold},
             {"perspective",
              Value::array({settings.perspectivePush, settings.perspectiveNoise, settings.perspectiveAngle})},
             {"motionHighlight",
              Value::array({settings.motionHighlight[0], settings.motionHighlight[1], settings.motionHighlight[2]})},
             {"bending", Value::array({settings.bendingAmplitude, settings.bendingSpeed, settings.bendingScale})},
             {"flutter", Value::array({settings.flutterAmplitude, settings.flutterSpeed, settings.flutterScale})},
             {"interactionAmplitude", settings.interactionAmplitude}}));
    } catch (const std::bad_alloc&) {
        return Result<Value>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Global Details snapshot allocation failed"));
    }
}

Result<VegetationDetailSettings> restoreVegetationDetails(const Value& document) {
    try {
        exactFields(document, {"schema", "version", "layers", "global", "colorMask", "overlayMask", "alphaThreshold",
                               "perspective", "motionHighlight", "bending", "flutter", "interactionAmplitude"});
        if (!member(document, "schema").isString() ||
            member(document, "schema").asString() != "eve.graphics.vegetation-details")
            throw std::invalid_argument("unexpected Global Details schema");
        if (!member(document, "version").isInt64() || member(document, "version").asInt() != 1)
            return Result<VegetationDetailSettings>::failure(
                Diagnostic::error(DiagnosticCode::UnknownVersion, "only Global Details version 1 is supported"));
        auto numbers = [&](std::string_view name, size_t count) {
            const auto* array = member(document, name).getIf<Value::Array>();
            if (!array || array->size() != count)
                throw std::invalid_argument("Global Details vector length is invalid");
            std::array<float, 4> result{};
            for (size_t index = 0; index < count; ++index) result[index] = finiteNumber((*array)[index]);
            return result;
        };
        const auto* layers = member(document, "layers").getIf<Value::Array>();
        if (!layers || layers->size() != 3) throw std::invalid_argument("Global Details layers are invalid");
        VegetationDetailSettings result;
        result.colorsLayer        = layerNumber((*layers)[0]);
        result.extrasLayer        = layerNumber((*layers)[1]);
        result.motionLayer        = layerNumber((*layers)[2]);
        const auto global         = numbers("global", 4);
        result.globalColor        = global[0];
        result.globalAlpha        = global[1];
        result.globalOverlay      = global[2];
        result.globalWetness      = global[3];
        const auto colorMask      = numbers("colorMask", 2);
        result.colorMaskMinimum   = colorMask[0];
        result.colorMaskMaximum   = colorMask[1];
        const auto overlayMask    = numbers("overlayMask", 2);
        result.overlayMaskMinimum = overlayMask[0];
        result.overlayMaskMaximum = overlayMask[1];
        result.alphaThreshold     = finiteNumber(member(document, "alphaThreshold"));
        const auto perspective    = numbers("perspective", 3);
        result.perspectivePush    = perspective[0];
        result.perspectiveNoise   = perspective[1];
        result.perspectiveAngle   = perspective[2];
        const auto highlight      = numbers("motionHighlight", 3);
        std::copy_n(highlight.begin(), 3, result.motionHighlight.begin());
        const auto bending          = numbers("bending", 3);
        result.bendingAmplitude     = bending[0];
        result.bendingSpeed         = bending[1];
        result.bendingScale         = bending[2];
        const auto flutter          = numbers("flutter", 3);
        result.flutterAmplitude     = flutter[0];
        result.flutterSpeed         = flutter[1];
        result.flutterScale         = flutter[2];
        result.interactionAmplitude = finiteNumber(member(document, "interactionAmplitude"));
        auto validated              = configureVegetationDetails(PbrSurface{}, VegetationMotion{}, result);
        if (!validated) return Result<VegetationDetailSettings>::failure(validated.status());
        return Result<VegetationDetailSettings>::success(std::move(result));
    } catch (const std::invalid_argument& error) {
        return Result<VegetationDetailSettings>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), {}, {}, "graphics.vegetation.details"));
    } catch (const std::bad_alloc&) {
        return Result<VegetationDetailSettings>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Global Details restore allocation failed"));
    }
}

}  // namespace eve::graphics
