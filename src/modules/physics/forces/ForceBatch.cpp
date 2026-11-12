// eve-hot-path
#include "physics/forces/ForceBatch.h"
#include <Box2D/Box2D.h>
#include <box3d/box3d.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <unordered_set>
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
    bool        requested = false;
};
template <class BodyType>
using Sums = std::vector<std::pair<BodyType*, LoadSum>>;
std::uint64_t orderKey(PhysicsBodyHandle handle) { return (std::uint64_t{handle.index()} << 32) | handle.generation(); }
// Stable fixed-width sorting: eight byte passes, independent of key distribution.
template <class T, class Key>
void radixOrder(std::vector<T>& values, Key key) {
    std::vector<T> scratch(values.size());
    for (unsigned shift = 0; shift < 64; shift += 8) {
        std::array<std::size_t, 256> offsets{};
        for (const auto& value : values) ++offsets[(key(value) >> shift) & 255];
        std::size_t prefix = 0;
        for (auto& count : offsets) {
            const auto old = count;
            count          = prefix;
            prefix += old;
        }
        for (const auto& value : values) scratch[offsets[(key(value) >> shift) & 255]++] = value;
        values.swap(scratch);
    }
}
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
template <class BodyType>
Result<void> accumulateLoad(BodyType& body, const ForceLoad& load, bool spatial, LoadSum& sum) {
    sum.requested = true;
    if (!body.isActive() || body.getType() != "dynamic" || !fits(load.force) || !fits(load.point) ||
        !fits(load.torque) ||
        (!spatial && (load.force.z != 0 || load.point.z != 0 || load.torque.x != 0 || load.torque.y != 0)))
        return invalid<void>();
    const auto torque = add(load.torque, cross(add(load.point, scale(center(body), -1)), load.force));
    if (!fits(torque)) return invalid<void>();
    sum.force  = add(sum.force, load.force);
    sum.torque = add(sum.torque, torque);
    if (!fits(sum.force) || !fits(sum.torque)) return invalid<void>();
    return Result<void>::success();
}
template <class W, class BodyType>
Result<Sums<BodyType>> aggregate(W& world, std::span<const ForceLoad> loads, bool spatial,
                                 const std::unordered_set<BodyType*>& bodies) {
    // Borrowed body pointers are confined to this synchronous prepare/commit call.
    // No callback or structural mutation occurs between collection and application.
    Sums<BodyType> sums;
    if (loads.empty()) return Result<Sums<BodyType>>::success(std::move(sums));
    // A fixed small request limit preserves the cheap legacy path. At most
    // eight world scans remain O(B), independent of arbitrarily large L.
    if (loads.size() <= 8) {
        sums.reserve(loads.size());
        for (const auto& load : loads) {
            auto resolved = load.target.resolve(world);
            if (!resolved) return Result<Sums<BodyType>>::failure(resolved.status());
            auto* body = resolved.value();
            auto  found =
                std::find_if(sums.begin(), sums.end(), [body](const auto& item) { return item.first == body; });
            if (found == sums.end()) {
                sums.push_back({body, {}});
                found = std::prev(sums.end());
            }
            auto accumulated = accumulateLoad(*body, load, spatial, found->second);
            if (!accumulated) return Result<Sums<BodyType>>::failure(accumulated.status());
        }
        std::sort(sums.begin(), sums.end(),
                  [](const auto& a, const auto& b) { return a.first->runtimeHandle() < b.first->runtimeHandle(); });
        return Result<Sums<BodyType>>::success(std::move(sums));
    }
    sums.reserve(bodies.size());
    for (auto* body : bodies)
        if (body && body->isValid()) sums.push_back({body, {}});
    radixOrder(sums, [](const auto& item) { return orderKey(item.first->runtimeHandle()); });
    std::vector<std::size_t> requests(loads.size()), matched(loads.size(), sums.size());
    for (std::size_t i = 0; i < requests.size(); ++i) requests[i] = i;
    radixOrder(requests, [&](std::size_t i) { return orderKey(loads[i].target.body); });
    std::size_t cursor = 0;
    for (auto request : requests) {
        const auto key = orderKey(loads[request].target.body);
        while (cursor < sums.size() && orderKey(sums[cursor].first->runtimeHandle()) < key) ++cursor;
        if (cursor < sums.size() && sums[cursor].first->runtimeHandle() == loads[request].target.body)
            matched[request] = cursor;
    }
    // Preserve input-order validation and addition, including overflow diagnostics.
    for (std::size_t i = 0; i < loads.size(); ++i) {
        const auto& load = loads[i];
        if (!load.target.isValid() || load.target.world != world.runtimeHandle() || matched[i] == sums.size()) {
            // Use the existing diagnostic contract. Only the first failed request
            // scans the world, so failure still costs O(B+L), never O(B*L).
            auto resolved = load.target.resolve(world);
            if (!resolved) return Result<Sums<BodyType>>::failure(resolved.status());
            return Result<Sums<BodyType>>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                                     "Batch body matching disagrees with world lookup",
                                                                     "physics.forces.batch"));
        }
        auto& body        = *sums[matched[i]].first;
        auto  accumulated = accumulateLoad(body, load, spatial, sums[matched[i]].second);
        if (!accumulated) return Result<Sums<BodyType>>::failure(accumulated.status());
    }
    return Result<Sums<BodyType>>::success(std::move(sums));
}
}  // namespace
Result<ForceBodySnapshot> ForceBatch::captureCenters(World3D& world, std::span<const PhysicsLink> targets) {
    if (!world.isValid() || world.couplingStepActive_)
        return Result<ForceBodySnapshot>::failure(unavailable().status());
    std::vector<ForceLoad> loads;
    loads.reserve(targets.size());
    for (const auto& target : targets) loads.push_back({target, {}, {}, {}});
    auto matched = aggregate(world, loads, true, world.bodies_);
    if (!matched) return Result<ForceBodySnapshot>::failure(matched.status());
    ForceBodySnapshot snapshot{world.runtimeHandle(), world.simulationObservation().lastTick, {}};
    snapshot.bodies.reserve(targets.size());
    for (const auto& [body, load] : matched.value()) {
        if (!load.requested) continue;
        const double mass     = body->getMass();
        const auto   position = center(*body);
        if (!std::isfinite(mass) || mass <= 0 || !isFinite(position)) return invalid<ForceBodySnapshot>();
        snapshot.bodies.push_back({{snapshot.world, body->runtimeHandle()}, position, mass});
    }
    if (snapshot.bodies.size() != targets.size())
        return Result<ForceBodySnapshot>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Duplicate body in force observation", "physics.forces.capture"));
    return Result<ForceBodySnapshot>::success(std::move(snapshot));
}
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
    auto sums = aggregate(world, loads, false, world.bodies_);
    if (!sums) return Result<ForceBatch>::failure(sums.status());
    return Result<ForceBatch>::success(ForceBatch(world.runtimeHandle(), false, loads));
}
Result<ForceBatch> ForceBatch::prepare(World3D& world, std::span<const ForceLoad> loads) {
    if (!world.isValid() || world.couplingStepActive_) return Result<ForceBatch>::failure(unavailable().status());
    auto sums = aggregate(world, loads, true, world.bodies_);
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
    auto sums = aggregate(world, loads_, false, world.bodies_);
    if (!sums) return Result<ForceReceipt>::failure(sums.status());
    // Allocate before applying any force. All receipts for this step observe the same world-owned state.
    auto state = world.pendingForceStep_;
    if (!state) state = std::make_shared<ForceStepState>(ForceStepState{step});
    for (const auto& [resolvedBody, load] : sums.value()) {
        if (zero(load.force) && zero(load.torque)) continue;
        auto* body = resolvedBody->raw();
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
    auto sums = aggregate(world, loads_, true, world.bodies_);
    if (!sums) return Result<ForceReceipt>::failure(sums.status());
    // Allocate before applying any force. All receipts for this step observe the same world-owned state.
    auto state = world.pendingForceStep_;
    if (!state) state = std::make_shared<ForceStepState>(ForceStepState{step});
    for (const auto& [resolvedBody, load] : sums.value()) {
        if (zero(load.force) && zero(load.torque)) continue;
        auto* body = resolvedBody;
        body->applyForce(float(load.force.x), float(load.force.y), float(load.force.z));
        body->applyTorque(float(load.torque.x), float(load.torque.y), float(load.torque.z));
    }
    consumed_               = true;
    world.pendingForceStep_ = state;
    return Result<ForceReceipt>::success(ForceReceipt(std::move(state)));
}
}  // namespace eve::physics
