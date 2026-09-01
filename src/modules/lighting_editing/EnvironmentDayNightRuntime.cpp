#include "lighting_editing/LightingTarget.h"

#include "daynight/DayNight.h"

#include <utility>

namespace eve::lighting_editing {

EditorResult<void> EnvironmentRuntimeApplier::applyDayNight(const EnvironmentDocumentTarget& document,
                                                            daynight::DayNight*              environment) const {
    if (!environment)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.environment.daynight-required"),
                                          "Runtime DayNight environment is required");
    const auto* mode = document.value("environment.mode")->getIf<std::string>();
    if (!mode || *mode != "daynight")
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.environment.mode-mismatch"),
                                          "Environment document is not in daynight mode");
    const auto diagnostics = document.validate();
    for (const EditorDiagnostic& diagnostic : diagnostics)
        if (diagnostic.severity() == DiagnosticSeverity::Error)
            return EditorResult<void>::failure(eve::Status(EditorStatus::Rejected, diagnostics));
    environment->setTimeOfDay(static_cast<float>(*document.value("daynight.time")->getIf<double>()));
    environment->setSpeed(static_cast<float>(*document.value("daynight.speed")->getIf<double>()));
    environment->setPaused(*document.value("daynight.paused")->getIf<bool>());
    environment->setTurbidity(static_cast<float>(*document.value("daynight.turbidity")->getIf<double>()));
    environment->setMieStrength(static_cast<float>(*document.value("daynight.mie")->getIf<double>()));
    environment->setSkyExposure(static_cast<float>(*document.value("environment.exposure")->getIf<double>()));
    environment->setSkyboxEnabled(*document.value("daynight.skybox")->getIf<bool>());
    return eve::editing::applied<void>(diagnostics);
}

}  // namespace eve::lighting_editing
