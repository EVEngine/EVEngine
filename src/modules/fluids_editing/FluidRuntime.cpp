#include "fluids_editing/FluidTarget.h"

#include "fluids/FluidSimulation.h"

namespace eve::fluids_editing {

EditorResult<void> FluidSimulationRuntimeApplier::apply(const FluidSimulationTarget& target,
                                                        fluids::FluidSimulation*     simulation) const {
    if (!simulation)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.fluid.runtime-required"),
                                          "Fluid parameter application requires a live simulation");
    const auto diagnostics = target.validate();
    for (const auto& diagnostic : diagnostics)
        if (diagnostic.severity() == DiagnosticSeverity::Error)
            return eve::editing::failed<void>(EditorStatus::Rejected, eve::editing::diagnosticRule(diagnostic),
                                               diagnostic.message());
    const FluidSimulationSettings settings = target.settings();
    if (simulation->maxParticles() != settings.maxParticles)
        return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.fluid.capacity-mismatch"),
                                          "Fluid capacity is immutable; rebuild the preview simulation first");
    auto& params          = simulation->params();
    params.particleRadius = static_cast<float>(settings.particleRadius);
    params.supportRadius  = static_cast<float>(settings.supportRadius);
    params.restDensity    = static_cast<float>(settings.restDensity);
    params.gravity        = {static_cast<float>(settings.gravityX), static_cast<float>(settings.gravityY),
                             static_cast<float>(settings.gravityZ)};
    params.viscosity      = static_cast<float>(settings.viscosity);
    params.yieldStress    = static_cast<float>(settings.yieldStress);
    params.cohesion       = static_cast<float>(settings.cohesion);
    params.adhesion       = static_cast<float>(settings.adhesion);
    params.damping        = static_cast<float>(settings.damping);
    params.maxVelocity    = static_cast<float>(settings.maximumVelocity);
    params.iterations     = settings.iterations;
    params.pbfIterations  = settings.pbfIterations;
    return eve::editing::applied<void>();
}

}  // namespace eve::fluids_editing
