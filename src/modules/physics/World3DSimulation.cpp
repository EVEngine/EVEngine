#include <algorithm>
#include <cmath>
#include "common/Profile.h"
#include "physics/Mechanism3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

namespace eve::physics {
namespace {
eve::Result<eve::SimulationStep> makeLegacyStep(float dt, eve::SimulationTick currentTick) {
    if (!std::isfinite(dt) || dt < 0.f) {
        return eve::Result<eve::SimulationStep>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "World3D update dt must be finite and non-negative", "physics.world3d.update.dt"));
    }
    const auto nextTick = currentTick.incremented();
    if (!nextTick) {
        return eve::Result<eve::SimulationStep>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                   "World3D simulation tick cannot be incremented", "physics.world3d.simulationTick"));
    }
    auto duration = eve::Duration::fromSeconds(static_cast<double>(std::min(dt, 0.05f)));
    if (!duration) return eve::Result<eve::SimulationStep>::failure(duration.status());
    return eve::Result<eve::SimulationStep>::success({*nextTick, std::move(duration).takeValue()});
}
}  // namespace
void World3D::update(float dt) { updateFull(dt, 4); }

void World3D::updateFull(float dt, int subStepCount) {
    EV_PROFILE_MODULE("physics", "World3D::update");
    auto legacyStep = makeLegacyStep(dt, simulationTick_);
    if (!legacyStep) {
        legacyStep.ignore("legacy World3D::updateFull cannot return a structured error");
        return;
    }
    auto result = step(std::move(legacyStep).takeValue(), SimulationSettings{8, 3, subStepCount});
    result.ignore("legacy World3D::updateFull cannot return a structured result");
}

eve::Result<void> World3D::step(const eve::SimulationStep &stepValue, const SimulationSettings &settings) {
    if (!isValid() || !simulation_ || couplingStepActive_) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Cannot step a destroyed or uninitialized 3D physics world",
            "physics.world3d.step"));
    }
    auto forceStep = validateForceStep(stepValue);
    if (!forceStep) return forceStep;
    auto valid = detail::validateSimulationStep(stepValue, settings, simulation_->observation());
    if (!valid) return valid;
    for (Mechanism3D *mechanism : mechanisms_) {
        if (mechanism) mechanism->syncBeforeStep();
    }
    for (Shape3D *shape : shapes_) {
        if (shape && shape->isOneWay()) shape->refreshOneWayWorldData();
    }
    auto result = stepCoupledBackend(stepValue, settings);
    if (!result) return result;
    simulationTick_ = stepValue.tick;
    finishForceStep();
    clearContactEvents();
    emitContactEvents();
    return result;
}

SimulationObservation World3D::simulationObservation() const noexcept {
    return simulation_ ? simulation_->observation() : SimulationObservation{};
}

SimulationBackendKind World3D::backendKind() const noexcept {
    return simulation_ ? simulation_->kind() : SimulationBackendKind::Cpu;
}

SimulationDeterminism World3D::backendDeterminism() const noexcept {
    return simulation_ ? simulation_->determinism() : SimulationDeterminism::ToleranceBounded;
}

eve::Status World3D::backendSelectionStatus() const { return backendSelectionStatus_; }

}  // namespace eve::physics
