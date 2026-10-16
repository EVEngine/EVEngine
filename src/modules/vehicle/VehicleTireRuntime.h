#pragma once
#include <memory>
#include "common/Result.h"
#include "common/Time.h"
#include "vehicle/VehicleTypes.h"
namespace eve::physics {
class Body;
class Body3D;
class World;
class World3D;
class ForceBatch;
struct TireModel;
struct TireInput;
}  // namespace eve::physics
namespace eve::vehicle {
class VehicleEntity;
class VehicleWheelRuntime;
enum class VehiclePhysicsStatus : unsigned char;
namespace tire_detail {
// Internal optional-physics implementation; the binding owns pending state, ECS owns completed wheel state.
// Unavailable from submissionState means there is no matching operation and preparation may proceed.
[[nodiscard]] Result<VehiclePhysicsStatus> submissionState(const std::shared_ptr<VehicleWheelRuntime>& runtime,
                                                           const SimulationStep&                       step);
[[nodiscard]] Result<VehiclePhysicsStatus> commit(physics::World& world, physics::ForceBatch& batch,
                                                  std::shared_ptr<VehicleWheelRuntime>&                   runtime,
                                                  std::vector<VehicleEntity::SuspensionState::WheelState> candidate,
                                                  const SimulationStep&                                   step);
[[nodiscard]] Result<VehiclePhysicsStatus> commit(physics::World3D& world, physics::ForceBatch& batch,
                                                  std::shared_ptr<VehicleWheelRuntime>&                   runtime,
                                                  std::vector<VehicleEntity::SuspensionState::WheelState> candidate,
                                                  const SimulationStep&                                   step);
[[nodiscard]] physics::TireModel           model(const SuspensionConfig& config, double radius);
void driveInputs(physics::TireInput& target, const SuspensionConfig& config, const VehicleEntity::Input& input,
                 const SuspensionWheel& wheel, std::size_t driven, double maximumSpeed, double traction);
[[nodiscard]] Result<VehiclePhysicsStatus> submit3D(VehicleEntity& vehicle, physics::Body3D& body,
                                                    std::shared_ptr<VehicleWheelRuntime>& runtime,
                                                    const SimulationStep&                 step);

[[nodiscard]] Result<VehiclePhysicsStatus> submit2D(VehicleEntity& vehicle, physics::Body& body,
                                                    std::shared_ptr<VehicleWheelRuntime>& runtime,
                                                    const SimulationStep&                 step);
[[nodiscard]] Result<VehiclePhysicsStatus> synchronize(VehicleEntity&                        vehicle,
                                                       std::shared_ptr<VehicleWheelRuntime>& runtime);
}  // namespace tire_detail
}  // namespace eve::vehicle
