#include "physics/forces/ForceBatch.h"
#include <Box2D/Box2D.h>
#include <box3d/box3d.h>
#include <limits>
#include <map>
#include <utility>
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/forces/ForceMath.h"
#include "physics/forces/ForceStepInternal.h"

namespace eve::physics {
namespace {
using namespace force_detail;
struct LoadSum {
    ForceVector force, torque;
};
using Sums = std::map<PhysicsBodyHandle, LoadSum>;
bool zero(ForceVector v) { return v.x == 0 && v.y == 0 && v.z == 0; }
bool fits(ForceVector v) {
    constexpr double limit = std::numeric_limits<float>::max();
    return isFinite(v) && std::abs(v.x) <= limit && std::abs(v.y) <= limit && std::abs(v.z) <= limit;
}
ForceVector cross(ForceVector a, ForceVector b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Result<void> unavailable() {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                   "Force batch is consumed, foreign, or world is stepping",
                                                   "physics.forces.batch"));
}
ForceVector center(Body& body) {
    const auto c = body.raw()->GetWorldCenter();
    return {c.x, c.y, 0};
}
ForceVector center(Body3D& body) { return {body.getWorldCenterX(), body.getWorldCenterY(), body.getWorldCenterZ()}; }
template <class W>
Result<Sums> aggregate(W& world, std::span<const ForceLoad> loads, bool spatial) {
    Sums sums;
    for (const auto& load : loads) {
        auto resolved = load.target.resolve(world);
        if (!resolved) return Result<Sums>::failure(resolved.status());
        auto& body = *resolved.value();
        if (!body.isActive() || body.getType() != "dynamic" || !fits(load.force) || !fits(load.point) ||
            !fits(load.torque) ||
            (!spatial && (load.force.z != 0 || load.point.z != 0 || load.torque.x != 0 || load.torque.y != 0)))
            return invalid<Sums>();
        const auto torque = add(load.torque, cross(add(load.point, scale(center(body), -1)), load.force));
        if (!fits(torque)) return invalid<Sums>();
        auto& sum  = sums[load.target.body];
        sum.force  = add(sum.force, load.force);
        sum.torque = add(sum.torque, torque);
        if (!fits(sum.force) || !fits(sum.torque)) return invalid<Sums>();
    }
    return Result<Sums>::success(std::move(sums));
}
}  // namespace
ForceBatch::ForceBatch(PhysicsWorldHandle world, bool spatial, std::span<const ForceLoad> loads)
    : world_(world), spatial_(spatial), loads_(loads.begin(), loads.end()) {}
ForceBatch::ForceBatch(ForceBatch&& other) noexcept
    : world_(other.world_),
      spatial_(other.spatial_),
      consumed_(std::exchange(other.consumed_, true)),
      loads_(std::move(other.loads_)) {}
ForceBatch& ForceBatch::operator=(ForceBatch&& other) noexcept {
    if (this != &other) {
        world_    = other.world_;
        spatial_  = other.spatial_;
        consumed_ = std::exchange(other.consumed_, true);
        loads_    = std::move(other.loads_);
    }
    return *this;
}
Result<ForceBatch> ForceBatch::prepare(World& world, std::span<const ForceLoad> loads) {
    if (!world.isValid() || (world.raw()->IsLocked() || world.simulationStepActive_))
        return Result<ForceBatch>::failure(unavailable().status());
    auto sums = aggregate(world, loads, false);
    if (!sums) return Result<ForceBatch>::failure(sums.status());
    return Result<ForceBatch>::success(ForceBatch(world.runtimeHandle(), false, loads));
}
Result<ForceBatch> ForceBatch::prepare(World3D& world, std::span<const ForceLoad> loads) {
    if (!world.isValid() || world.couplingStepActive_) return Result<ForceBatch>::failure(unavailable().status());
    auto sums = aggregate(world, loads, true);
    if (!sums) return Result<ForceBatch>::failure(sums.status());
    return Result<ForceBatch>::success(ForceBatch(world.runtimeHandle(), true, loads));
}
Result<ForceReceipt> ForceBatch::commit(World& world, const SimulationStep& step) {
    if (consumed_ || spatial_ || world_ != world.runtimeHandle() || !world.isValid() ||
        (world.raw()->IsLocked() || world.simulationStepActive_))
        return Result<ForceReceipt>::failure(unavailable().status());
    if (step.delta <= Duration::zero()) return Result<ForceReceipt>::failure(invalid<void>().status());
    auto valid = detail::validateSimulationStep(step, {}, world.simulationObservation());
    if (!valid) return Result<ForceReceipt>::failure(valid.status());
    auto compatible = world.validateForceStep(step);
    if (!compatible) return Result<ForceReceipt>::failure(compatible.status());
    if (!world.raw()->GetAutoClearForces()) return Result<ForceReceipt>::failure(unavailable().status());
    auto sums = aggregate(world, loads_, false);
    if (!sums) return Result<ForceReceipt>::failure(sums.status());
    // Allocate before applying any force. All receipts for this step observe the same world-owned state.
    auto state = world.pendingForceStep_;
    if (!state) state = std::make_shared<ForceStepState>(ForceStepState{step});
    for (const auto& [handle, load] : sums.value()) {
        if (zero(load.force) && zero(load.torque)) continue;
        auto* body = world.findBody(handle)->raw();
        body->ApplyForceToCenter({float(load.force.x), float(load.force.y)}, true);
        body->ApplyTorque(float(load.torque.z), true);
    }
    consumed_               = true;
    world.pendingForceStep_ = state;
    return Result<ForceReceipt>::success(ForceReceipt(std::move(state)));
}
Result<ForceReceipt> ForceBatch::commit(World3D& world, const SimulationStep& step) {
    if (consumed_ || !spatial_ || world_ != world.runtimeHandle() || !world.isValid() || world.couplingStepActive_)
        return Result<ForceReceipt>::failure(unavailable().status());
    if (step.delta <= Duration::zero()) return Result<ForceReceipt>::failure(invalid<void>().status());
    auto valid = detail::validateSimulationStep(step, {}, world.simulationObservation());
    if (!valid) return Result<ForceReceipt>::failure(valid.status());
    auto compatible = world.validateForceStep(step);
    if (!compatible) return Result<ForceReceipt>::failure(compatible.status());
    auto sums = aggregate(world, loads_, true);
    if (!sums) return Result<ForceReceipt>::failure(sums.status());
    // Allocate before applying any force. All receipts for this step observe the same world-owned state.
    auto state = world.pendingForceStep_;
    if (!state) state = std::make_shared<ForceStepState>(ForceStepState{step});
    for (const auto& [handle, load] : sums.value()) {
        if (zero(load.force) && zero(load.torque)) continue;
        auto* body = world.findBody(handle);
        body->applyForce(float(load.force.x), float(load.force.y), float(load.force.z));
        body->applyTorque(float(load.torque.x), float(load.torque.y), float(load.torque.z));
    }
    consumed_               = true;
    world.pendingForceStep_ = state;
    return Result<ForceReceipt>::success(ForceReceipt(std::move(state)));
}
}  // namespace eve::physics
