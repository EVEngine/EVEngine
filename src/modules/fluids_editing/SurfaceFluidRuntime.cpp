#include "fluids_editing/SurfaceFluidTarget.h"

#include "fluids/SurfaceDropletSimulation.h"
#include "fluids/SurfaceFluidRenderData.h"
#include "fluids/SurfaceWetnessField.h"

namespace eve::fluids_editing {

EditorResult<void> SurfaceFluidRuntimeApplier::apply(
    const SurfaceFluidTarget& target, fluids::SurfaceDropletSimulation* simulation,
    fluids::SurfaceFluidRenderParams* render, fluids::SurfaceWetnessParams* wetness) const {
    if (!simulation || !render || !wetness)
        return EditorResult<void>::error(EditorStatus::Rejected,
            RuleId("editor.surface-fluid.runtime-services"),
            "Surface fluid runtime publication requires simulation, render and wetness targets");
    for (const auto& diagnostic : target.validate())
        if (diagnostic.severity == DiagnosticSeverity::Error)
            return EditorResult<void>::error(EditorStatus::Rejected,
                RuleId("editor.surface-fluid.runtime-invalid"),
                "Surface fluid document is invalid");
    const SurfaceFluidSettings s = target.settings();
    fluids::SurfaceDropletParams droplet;
    droplet.gravity = {static_cast<float>(s.gravityX), static_cast<float>(s.gravityY),
                       static_cast<float>(s.gravityZ)};
    droplet.friction = static_cast<float>(s.friction);
    droplet.maxSpeed = static_cast<float>(s.maxSpeed);
    droplet.adhesionAcceleration = static_cast<float>(s.adhesionAcceleration);
    droplet.maxCrossings = s.maxCrossings;
    droplet.contactAngleDegrees = static_cast<float>(s.contactAngleDegrees);
    droplet.mergeRadiusScale = static_cast<float>(s.mergeRadiusScale);
    droplet.trailDeposition = static_cast<float>(s.trailDeposition);
    droplet.airDrag = static_cast<float>(s.airDrag);
    droplet.reattachDistance = static_cast<float>(s.reattachDistance);
    fluids::SurfaceWetnessParams nextWetness;
    nextWetness.diffusion = static_cast<float>(s.diffusion);
    nextWetness.evaporation = static_cast<float>(s.evaporation);
    nextWetness.maxWetness = static_cast<float>(s.maxWetness);
    fluids::SurfaceFluidRenderParams nextRender;
    nextRender.velocityStretch = static_cast<float>(s.velocityStretch);
    nextRender.maxAspectRatio = static_cast<float>(s.maxAspectRatio);
    nextRender.surfaceOffset = static_cast<float>(s.surfaceOffset);
    nextRender.wetMaterial.dryRoughness = static_cast<float>(s.dryRoughness);
    nextRender.wetMaterial.wetRoughness = static_cast<float>(s.wetRoughness);
    nextRender.wetMaterial.drySpecular = static_cast<float>(s.drySpecular);
    nextRender.wetMaterial.wetSpecular = static_cast<float>(s.wetSpecular);
    nextRender.wetMaterial.wetDarkening = static_cast<float>(s.wetDarkening);
    nextRender.wetMaterial.normalStrength = static_cast<float>(s.normalStrength);
    simulation->params() = droplet;
    *render = nextRender;
    *wetness = nextWetness;
    return EditorResult<void>::applied();
}

}  // namespace eve::fluids_editing
