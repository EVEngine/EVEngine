#include "vehicle/VehicleTireRuntime.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include "physics/forces/ForceBatch.h"
#include "vehicle/VehiclePhysics.h"
namespace eve::vehicle {
class VehicleWheelRuntime {
public:
    std::optional<physics::ForceReceipt>                    receipt;
    std::vector<VehicleEntity::SuspensionState::WheelState> candidate;
    bool                                                    published = false;
};
namespace tire_detail {
Result<VehiclePhysicsStatus> synchronize(VehicleEntity& vehicle, std::shared_ptr<VehicleWheelRuntime>& runtime) {
    if (!runtime) return Result<VehiclePhysicsStatus>::success(VehiclePhysicsStatus::Unavailable);
    using physics::ForceStepStatus;
    if (runtime->receipt->status() == ForceStepStatus::Pending)
        return Result<VehiclePhysicsStatus>::success(VehiclePhysicsStatus::Pending);
    if (runtime->receipt->status() == ForceStepStatus::Cancelled) {
        runtime.reset();
        return Result<VehiclePhysicsStatus>::success(VehiclePhysicsStatus::Cancelled);
    }
    if (!runtime->published) {
        vehicle.suspension()->wheels = std::move(runtime->candidate);
        runtime->published           = true;
    }
    return Result<VehiclePhysicsStatus>::success(VehiclePhysicsStatus::Applied);
}
Result<VehiclePhysicsStatus> submissionState(const std::shared_ptr<VehicleWheelRuntime>& runtime,
                                             const SimulationStep&                       step) {
    using physics::ForceStepStatus;
    auto reject = [](std::string message) {
        return Result<VehiclePhysicsStatus>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, std::move(message), "vehicle.tires"));
    };
    if (runtime) {
        if (runtime->receipt->status() == ForceStepStatus::Pending) {
            const auto previous = runtime->receipt->step();
            if (previous.tick != step.tick || previous.delta != step.delta)
                return reject("Complete the pending vehicle world step before submitting another");
            return Result<VehiclePhysicsStatus>::success(VehiclePhysicsStatus::Pending);
        }
        if (!runtime->published) return reject("Synchronize the completed vehicle step before submitting another");
        if (runtime->receipt->step().tick == step.tick && runtime->receipt->step().delta == step.delta)
            return Result<VehiclePhysicsStatus>::success(VehiclePhysicsStatus::Applied);
    }
    return Result<VehiclePhysicsStatus>::success(VehiclePhysicsStatus::Unavailable);
}
physics::TireModel model(const SuspensionConfig& config, double radius) {
    physics::TireModel result;
    result.radius                = radius;
    result.inertia               = config.wheelInertia;
    result.friction              = config.tireFriction;
    result.longitudinalStiffness = config.longitudinalStiffness;
    result.corneringStiffness    = config.corneringStiffness;
    result.rollingResistance     = config.rollingResistance;
    return result;
}
void driveInputs(physics::TireInput& target, const SuspensionConfig& config, const VehicleEntity::Input& input,
                 const SuspensionWheel& wheel, std::size_t driven, double maximumSpeed, double traction) {
    const double throttle   = std::clamp(double(input.throttle), -1.0, 1.0);
    const double speedLimit = std::abs(throttle) * maximumSpeed;
    const bool   driving =
        driven > 0 && wheel.drive && throttle * target.longitudinalVelocity < std::abs(throttle) * speedLimit;
    target.driveTorque = driving ? traction * throttle * wheel.radius / static_cast<double>(driven) : 0;
    target.brakeTorque = config.brakeTorque * (input.handbrake ? 1 : std::clamp(double(input.brake), 0.0, 1.0));
}
namespace {
template <class W>
Result<VehiclePhysicsStatus> commitImpl(W& world, physics::ForceBatch& batch,
                                        std::shared_ptr<VehicleWheelRuntime>&                   runtime,
                                        std::vector<VehicleEntity::SuspensionState::WheelState> candidate,
                                        const SimulationStep&                                   step) {
    // Reserve all candidate storage before commit. Construct receipt state after commit without allocation.
    auto next      = std::make_shared<VehicleWheelRuntime>();
    auto submitted = batch.commit(world, step);
    if (!submitted) return Result<VehiclePhysicsStatus>::failure(submitted.status());
    next->receipt   = std::move(submitted).takeValue();
    next->candidate = std::move(candidate);
    next->published = false;
    runtime         = std::move(next);
    return Result<VehiclePhysicsStatus>::success(VehiclePhysicsStatus::Pending);
}
}  // namespace
Result<VehiclePhysicsStatus> commit(physics::World& world, physics::ForceBatch& batch,
                                    std::shared_ptr<VehicleWheelRuntime>&                   runtime,
                                    std::vector<VehicleEntity::SuspensionState::WheelState> candidate,
                                    const SimulationStep&                                   step) {
    return commitImpl(world, batch, runtime, std::move(candidate), step);
}
Result<VehiclePhysicsStatus> commit(physics::World3D& world, physics::ForceBatch& batch,
                                    std::shared_ptr<VehicleWheelRuntime>&                   runtime,
                                    std::vector<VehicleEntity::SuspensionState::WheelState> candidate,
                                    const SimulationStep&                                   step) {
    return commitImpl(world, batch, runtime, std::move(candidate), step);
}
}  // namespace tire_detail
}  // namespace eve::vehicle
