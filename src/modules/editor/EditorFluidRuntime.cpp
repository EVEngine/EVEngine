#include "editor/EditorFluidTarget.h"

#include "fluids/FluidSimulation.h"

namespace eve::editor {

EditorResult<void> FluidSimulationRuntimeApplier::apply(
    const FluidSimulationTarget& target, fluids::FluidSimulation* simulation) const {
    if (!simulation)
        return EditorResult<void>::error(EditorStatus::Rejected, RuleId("editor.fluid.runtime-required"),
                                         "Fluid parameter application requires a live simulation");
    const auto diagnostics = target.validate();
    for (const auto& diagnostic : diagnostics)
        if (diagnostic.severity == DiagnosticSeverity::Error)
            return EditorResult<void>::error(EditorStatus::Rejected, diagnostic.rule, diagnostic.message);
    const FluidSimulationSettings settings = target.settings();
    if (simulation->maxParticles() != settings.maxParticles)
        return EditorResult<void>::error(EditorStatus::Conflict, RuleId("editor.fluid.capacity-mismatch"),
                                         "Fluid capacity is immutable; rebuild the preview simulation first");
    auto& params = simulation->params();
    params.particleRadius = static_cast<float>(settings.particleRadius);
    params.supportRadius = static_cast<float>(settings.supportRadius);
    params.restDensity = static_cast<float>(settings.restDensity);
    params.gravity = {static_cast<float>(settings.gravityX), static_cast<float>(settings.gravityY),
                      static_cast<float>(settings.gravityZ)};
    params.viscosity = static_cast<float>(settings.viscosity);
    params.yieldStress = static_cast<float>(settings.yieldStress);
    params.cohesion = static_cast<float>(settings.cohesion);
    params.adhesion = static_cast<float>(settings.adhesion);
    params.damping = static_cast<float>(settings.damping);
    params.maxVelocity = static_cast<float>(settings.maximumVelocity);
    params.iterations = settings.iterations;
    params.pbfIterations = settings.pbfIterations;
    return EditorResult<void>::applied();
}

}  // namespace eve::editor
