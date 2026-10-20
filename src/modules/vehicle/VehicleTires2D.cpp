#include <Box2D/Box2D.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include "physics/Body.h"
#include "physics/World.h"
#include "physics/forces/ForceBatch.h"
#include "vehicle/VehiclePhysics.h"
#include "vehicle/VehicleTireRuntime.h"

namespace eve::vehicle {
namespace tire_detail {
namespace {
Result<VehiclePhysicsStatus> reject(std::string message) {
    return Result<VehiclePhysicsStatus>::failure(
        Diagnostic::error(DiagnosticCode::PreconditionViolation, std::move(message), "vehicle.tires"));
}
bool positive(double x) { return std::isfinite(x) && x > 0; }
bool nonnegative(double x) { return std::isfinite(x) && x >= 0; }
}  // namespace
Result<VehiclePhysicsStatus> submit2D(VehicleEntity& vehicle, physics::Body& body,
                                      std::shared_ptr<VehicleWheelRuntime>& runtime, const SimulationStep& step) {
    using namespace physics;
    auto pending = submissionState(runtime, step);
    if (!pending || pending.value() != VehiclePhysicsStatus::Unavailable) return pending;
    const auto* definition = vehicle.definition()->def;
    auto*       world      = body.getWorld();
    if (!definition || !world || !body.isValid()) return reject("Vehicle definition or planar body is unavailable");
    const auto&  config = definition->suspension;
    const double dt = step.delta.seconds(), meter = world->getMeter(), mass = body.getMass();
    const auto   input = *vehicle.input();
    if (!positive(dt) || dt > .05 || !positive(meter) || !positive(mass) || !nonnegative(definition->maxSpeed) ||
        !nonnegative(definition->accel) || !nonnegative(config.supportAcceleration2D) ||
        !nonnegative(config.driveForce) || !nonnegative(config.brakeTorque) || !nonnegative(config.maxSteerAngle) ||
        config.maxSteerAngle > 1.4 || !std::isfinite(input.throttle) || !std::isfinite(input.steer) ||
        !std::isfinite(input.brake))
        return reject("Invalid vehicle tire input, units or fixed duration");
    auto wheels = config.wheels;
    if (wheels.empty()) {
        if (!positive(definition->radius)) return reject("Default wheel layout requires a positive chassis radius");
        SuspensionWheel front, rear;
        front.x      = static_cast<float>(definition->radius / meter * .7);
        rear.x       = -front.x;
        front.radius = rear.radius = static_cast<float>(std::clamp(definition->radius / meter * .5, .05, .5));
        rear.steer                 = false;
        wheels                     = {front, rear};
    }
    std::vector<VehicleEntity::SuspensionState::WheelState> candidate(wheels.size());
    const auto&                                             previous = vehicle.suspension()->wheels;
    if (previous.size() == wheels.size()) candidate = previous;
    const double           wheelCount = static_cast<double>(wheels.size());
    std::vector<ForceLoad> loads;
    loads.reserve(wheels.size());
    auto link = PhysicsLink::fromBody(body);
    if (!link) return Result<VehiclePhysicsStatus>::failure(link.status());
    auto*             raw            = body.raw();
    const auto        localCenter    = raw->GetLocalCenter();
    const double      inertia        = raw->GetInertia() - mass * b2Dot(localCenter, localCenter);
    const double      inverseInertia = raw->IsFixedRotation() ? 0 : (inertia > 0 ? 1 / inertia : 0);
    const auto        center         = raw->GetWorldCenter();
    const std::size_t driven =
        std::count_if(wheels.begin(), wheels.end(), [](const auto& wheel) { return wheel.drive; });
    for (std::size_t i = 0; i < wheels.size(); ++i) {
        const auto& wheel = wheels[i];
        if (!std::isfinite(wheel.x) || !std::isfinite(wheel.y) || wheel.z != 0 || !positive(wheel.radius))
            return reject("2D tire mounts require finite XY meters, zero Z and positive radius");
        const auto   point   = raw->GetWorldPoint({wheel.x, wheel.y});
        const double steer   = wheel.steer ? std::clamp(double(input.steer), -1.0, 1.0) * config.maxSteerAngle : 0;
        const auto   forward = raw->GetWorldVector({float(std::cos(steer)), float(std::sin(steer))});
        const b2Vec2 lateral{-forward.y, forward.x};
        const auto   velocity = raw->GetLinearVelocityFromWorldPoint(point);
        const auto   lever    = point - center;
        const double a = b2Cross(lever, forward), b = b2Cross(lever, lateral);
        const double crossMass = std::abs(a * b * inverseInertia);
        auto         tireModel = model(config, wheel.radius);
        TireInput    sampleInput;
        sampleInput.longitudinalVelocity = b2Dot(velocity, forward);
        sampleInput.lateralVelocity      = b2Dot(velocity, lateral);
        sampleInput.angularVelocity      = candidate[i].angularVelocity;
        sampleInput.normalLoad           = mass * config.supportAcceleration2D / wheelCount;
        driveInputs(sampleInput, config, input, wheel, driven, definition->maxSpeed / meter,
                    std::min(double(config.driveForce), mass * definition->accel / meter));
        // A diagonal majorant bounds cross-axis and all-wheel chassis response (Cauchy-Schwarz).
        // It adds implicit damping, rather than pretending off-center contacts have diagonal inverse mass.
        sampleInput.longitudinalInverseMass = wheelCount * (1 / mass + a * a * inverseInertia + crossMass);
        sampleInput.lateralInverseMass      = wheelCount * (1 / mass + b * b * inverseInertia + crossMass);
        auto sampled                        = ForceModels::tire(tireModel, sampleInput, dt);
        if (!sampled) return Result<VehiclePhysicsStatus>::failure(sampled.status());
        const auto& value       = sampled.value();
        auto&       state       = candidate[i];
        state.angularVelocity   = value.angularVelocity;
        state.longitudinalForce = value.longitudinalForce;
        state.lateralForce      = value.lateralForce;
        state.slipRatio         = value.slipRatio;
        state.slipAngle         = value.slipAngle;
        state.normalLoad        = sampleInput.normalLoad;
        state.grounded          = sampleInput.normalLoad > 0;
        const ForceVector force{forward.x * value.longitudinalForce + lateral.x * value.lateralForce,
                                forward.y * value.longitudinalForce + lateral.y * value.lateralForce, 0};
        loads.push_back({link.value(), force, {point.x, point.y, 0}, {}});
    }
    auto batch = ForceBatch::prepare(*world, loads);
    if (!batch) return Result<VehiclePhysicsStatus>::failure(batch.status());
    return commit(*world, batch.value(), runtime, std::move(candidate), step);
}
}  // namespace tire_detail
}  // namespace eve::vehicle
