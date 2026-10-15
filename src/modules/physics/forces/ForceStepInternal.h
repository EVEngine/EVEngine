#pragma once
#include "physics/forces/ForceReceipt.h"

namespace eve::physics {
// One authoritative completion state for all force batches submitted to the same world step.
struct ForceStepState {
    SimulationStep  step;
    ForceStepStatus status = ForceStepStatus::Pending;
};
}  // namespace eve::physics
