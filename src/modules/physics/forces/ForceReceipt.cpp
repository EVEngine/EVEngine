#include <Box2D/Box2D.h>
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/forces/ForceStepInternal.h"

namespace eve::physics {
ForceStepStatus ForceReceipt::status() const noexcept { return state_ ? state_->status : ForceStepStatus::Cancelled; }
SimulationStep  ForceReceipt::step() const noexcept { return state_ ? state_->step : SimulationStep{}; }
namespace {
Result<void> validate(const std::shared_ptr<ForceStepState>& state, const SimulationStep& step) {
    if (state && (state->step.tick != step.tick || state->step.delta != step.delta))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "Submitted forces require their exact tick and duration", "physics.forces.step"));
    return Result<void>::success();
}
void finish(std::shared_ptr<ForceStepState>& state, ForceStepStatus status) noexcept {
    if (state) state->status = status;
    state.reset();
}
}  // namespace
Result<void> World::validateForceStep(const SimulationStep& step) const {
    if (pendingForceStep_ && !raw()->GetAutoClearForces())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Force batches require automatic backend force clearing",
                                                       "physics.forces.step"));
    return validate(pendingForceStep_, step);
}
void         World::finishForceStep() noexcept { finish(pendingForceStep_, ForceStepStatus::Applied); }
void         World::cancelForceStep() noexcept { finish(pendingForceStep_, ForceStepStatus::Cancelled); }
Result<void> World3D::validateForceStep(const SimulationStep& step) const { return validate(pendingForceStep_, step); }
void         World3D::finishForceStep() noexcept { finish(pendingForceStep_, ForceStepStatus::Applied); }
void         World3D::cancelForceStep() noexcept { finish(pendingForceStep_, ForceStepStatus::Cancelled); }
}  // namespace eve::physics
