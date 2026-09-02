#include "lighting_editing/LightingTarget.h"

#include "weather/Weather.h"

namespace eve::lighting_editing {
namespace {

double component(const EditorValue::Array& values, size_t index) { return *values[index].getIf<double>(); }

}  // namespace

EditorResult<void> EnvironmentRuntimeApplier::applyWeather(const EnvironmentDocumentTarget& document,
                                                           weather::Weather*                environment) const {
    if (!environment)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.environment.weather-required"),
                                          "Runtime Weather environment is required");
    const auto* mode = document.value("environment.mode")->getIf<std::string>();
    if (!mode || *mode != "weather")
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.environment.mode-mismatch"),
                                          "Environment document is not in weather mode");
    const auto diagnostics = document.validate();
    for (const EditorDiagnostic& diagnostic : diagnostics)
        if (diagnostic.severity() == DiagnosticSeverity::Error)
            return EditorResult<void>::failure(eve::Status(EditorStatus::Rejected, diagnostics));
    const auto& sky = *document.value("environment.sky-color")->getIf<EditorValue::Array>();
    const auto& fog = *document.value("weather.fog-color")->getIf<EditorValue::Array>();
    environment->setPreset(*document.value("weather.preset")->getIf<std::string>());
    environment->setIntensity(static_cast<float>(*document.value("weather.intensity")->getIf<double>()));
    environment->setWindSpeed(static_cast<float>(*document.value("weather.wind-speed")->getIf<double>()));
    environment->setWindDirection(static_cast<float>(*document.value("weather.wind-direction")->getIf<double>()));
    environment->setLightningEnabled(*document.value("weather.lightning")->getIf<bool>());
    environment->setSkyColor(static_cast<float>(component(sky, 0)), static_cast<float>(component(sky, 1)),
                             static_cast<float>(component(sky, 2)));
    environment->setFogColor(static_cast<float>(component(fog, 0)), static_cast<float>(component(fog, 1)),
                             static_cast<float>(component(fog, 2)));
    environment->setFogDensity(static_cast<float>(*document.value("weather.fog-density")->getIf<double>()));
    environment->setEnvironmentEnabled(*document.value("weather.environment-enabled")->getIf<bool>());
    return eve::editing::applied<void>(diagnostics);
}

}  // namespace eve::lighting_editing
