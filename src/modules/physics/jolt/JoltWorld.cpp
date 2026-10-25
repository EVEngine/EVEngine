// eve-hot-path
// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Physics/Body/MotionProperties.h>
#include <algorithm>
#include <chrono>
#include "physics/Joint3D.h"
#include "physics/World3D.h"
#include "physics/jolt/JoltInternal.h"
#include "physics/jolt/JoltJoint.h"
#include "physics/jolt/JoltWorld.h"
#include "physics/jolt/JoltWorldFactory.h"
namespace eve::physics::jolt {
namespace {
class StepDriver final : public ISimulationBackend {
    WorldBackend&         world;
    SimulationObservation progress;

public:
    explicit StepDriver(WorldBackend& value) : world(value) {}
    eve::Result<void> step(const SimulationStep& step, const SimulationSettings& settings) override {
        auto valid = detail::validateSimulationStep(step, settings, progress);
        if (!valid) return valid;
        if (!world.isValid()) return rejected<void>("World expired", eve::DiagnosticCode::StaleHandle);
        auto next = detail::advanceSimulationObservation(progress, step);
        if (!next) return eve::Result<void>::failure(next.status());
        auto& state = *world.state;
        if (state.faulted)
            return rejected<void>("Solver requires world recreation after capacity failure",
                                  eve::DiagnosticCode::Failed);
        auto policy              = state.system.GetPhysicsSettings();
        policy.mNumVelocitySteps = static_cast<JPH::uint>(settings.velocityIterations);
        policy.mNumPositionSteps = static_cast<JPH::uint>(settings.positionIterations);
        state.system.SetPhysicsSettings(policy);
        if (step.delta.seconds() > 0) {
            state.listener->solved.clear();
            state.listener->hits.clear();
            state.listener->normalTotals.clear();
            state.listener->substep = 0;
            if (policy.mContactPointPreserveLambdaMaxDistSq == 0)
                for (const auto& [id, body] : state.bodies) body->api().InvalidateContactCache(body->id);
            state.inverseStep  = settings.subStepCount / static_cast<float>(step.delta.seconds());
            const auto started = std::chrono::steady_clock::now();
            auto       error   = state.system.Update(static_cast<float>(step.delta.seconds()), settings.subStepCount,
                                                     &state.allocator, &state.jobs);
            state.stepMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
            if (error != JPH::EPhysicsUpdateError::None) {
                state.faulted = true;
                return rejected<void>("Solver capacity exceeded; simulation stopped", eve::DiagnosticCode::Failed);
            }
        }
        // Explicit per-body thresholds use a conservative point-speed bound and simulation time.
        // Native automatic sleep is disabled only for these bodies, so there is one decision owner.
        for (const auto& [id, body] : state.bodies) {
            if (!body->customSleepThreshold) continue;
            auto* native = state.system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
            if (!state.sleep || !body->sleepEnabled || !native->IsDynamic() || !native->IsActive()) {
                body->quietSeconds = 0;
                continue;
            }
            const float speed =
                native->GetLinearVelocity().Length() +
                native->GetAngularVelocity().Length() * native->GetShape()->GetLocalBounds().GetExtent().Length();
            body->quietSeconds = speed > body->sleepThreshold ? 0.f : body->quietSeconds + float(step.delta.seconds());
            if (body->quietSeconds >= state.system.GetPhysicsSettings().mTimeBeforeSleep) {
                body->api().DeactivateBody(body->id);
                body->quietSeconds = 0;
            }
        }
        progress = std::move(next).takeValue();
        return success();
    }
    SimulationObservation observation() const noexcept override { return progress; }
    SimulationBackendKind kind() const noexcept override { return SimulationBackendKind::Cpu; }
    SimulationDeterminism determinism() const noexcept override { return SimulationDeterminism::ToleranceBounded; }
    eve::Result<void>     restoreObservation(const SimulationObservation& value) override {
        auto valid = detail::validateSimulationObservation(value, "physics.jolt.restore");
        if (!valid) return valid;
        progress = value;
        return success();
    }
};
}  // namespace
WorldBackend::WorldBackend(World3D& facade, float x, float y, float z, bool sleep)
    : owner(&facade), state((initializeRuntime(), std::make_shared<State>(x, y, z, sleep))) {}
WorldBackend::~WorldBackend() { destroy(); }
bool WorldBackend::isValid() const noexcept { return state && state->alive; }
void WorldBackend::destroy() noexcept {
    if (state) {
        state->alive = false;
        state->bodies.clear();
        state.reset();
    }
}
eve::Result<std::unique_ptr<IWorldBackend3D>> makeWorld(World3D& owner, float x, float y, float z, bool sleep) {
    return eve::Result<std::unique_ptr<IWorldBackend3D>>::success(
        std::make_unique<WorldBackend>(owner, x, y, z, sleep));
}
eve::Result<SimulationBackendSelection> WorldBackend::selectSimulation() {
    return eve::Result<SimulationBackendSelection>::success(
        {std::make_unique<StepDriver>(*this), SimulationBackendKind::Cpu, SimulationBackendKind::Cpu, false});
}
eve::Result<void> WorldBackend::swapTopology(IWorldBackend3D& prepared) {
    auto* other = dynamic_cast<WorldBackend*>(&prepared);
    if (!other) return rejected<void>("Cannot swap different rigid backends");
    state.swap(other->state);
    return success();
}
uint64_t WorldBackend::shapeKey(const Shape3D& shape) const { return static_cast<uint64_t>(shape.getId()); }
eve::Result<std::unique_ptr<IBodyBackend3D>> WorldBackend::createBody(std::string_view type, float x, float y,
                                                                      float z) {
    if (!isValid()) return rejected<std::unique_ptr<IBodyBackend3D>>("World expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(x, y, z) || (type != "static" && type != "dynamic" && type != "kinematic"))
        return rejected<std::unique_ptr<IBodyBackend3D>>("Invalid body definition",
                                                         eve::DiagnosticCode::InvalidArgument);
    auto                      motion = type == "static"    ? JPH::EMotionType::Static
                                       : type == "dynamic" ? JPH::EMotionType::Dynamic
                                                           : JPH::EMotionType::Kinematic;
    JPH::RefConst<JPH::Shape> empty  = new JPH::EmptyShape;
    JPH::BodyCreationSettings settings(empty, {x, y, z}, JPH::Quat::sIdentity(), motion,
                                       motion == JPH::EMotionType::Static ? 0 : 1);
    settings.mAllowDynamicOrKinematic = true;
    settings.mAllowSleeping           = state->sleep;
    settings.mMaxLinearVelocity       = state->maximumLinearSpeed;
    settings.mOverrideMassProperties  = JPH::EOverrideMassProperties::MassAndInertiaProvided;
    settings.mMassPropertiesOverride.SetMassAndInertiaOfSolidBox({1, 1, 1}, 1);
    auto& api    = state->system.GetBodyInterface();
    auto* native = api.CreateBody(settings);
    if (!native)
        return rejected<std::unique_ptr<IBodyBackend3D>>("Body capacity exhausted", eve::DiagnosticCode::Failed);
    auto body   = std::make_shared<BodyState>();
    body->mass  = settings.mMassPropertiesOverride;
    body->world = state;
    body->id    = native->GetID();
    state->bodies.emplace(body->id.GetIndexAndSequenceNumber(), body);
    api.AddBody(body->id, JPH::EActivation::Activate);
    return eve::Result<std::unique_ptr<IBodyBackend3D>>::success(std::make_unique<BodyBackend>(body));
}
eve::Result<void> WorldBackend::collectEvents() {
    if (!isValid()) return rejected<void>("World expired", eve::DiagnosticCode::StaleHandle);
    auto append = [&](const ContactPair& pair, bool begin) {
        World3D::EventShape a{pair.bodyA, pair.shapeA, pair.tagA}, b{pair.bodyB, pair.shapeB, pair.tagB};
        if (pair.sensorA || pair.sensorB) {
            auto& output = begin ? owner->beginTriggers_ : owner->endTriggers_;
            if (pair.sensorA) output.push_back({a, b});
            if (pair.sensorB) output.push_back({b, a});
        } else {
            if (a.shapeId > b.shapeId) std::swap(a, b);
            (begin ? owner->beginContacts_ : owner->endContacts_).push_back({a, b});
        }
    };
    for (const auto& pair : state->listener->begins) append(pair, true);
    for (const auto& pair : state->listener->ends) append(pair, false);
    const auto hitStart = owner->hits_.size();
    for (const auto& [key, hit] : state->listener->hits) {
        if (!owner->findShape(hit.shapeA) || !owner->findShape(hit.shapeB)) continue;
        const auto& pair = hit.pair;
        owner->hits_.push_back({{pair.bodyA, pair.shapeA, pair.tagA},
                                {pair.bodyB, pair.shapeB, pair.tagB},
                                hit.x,
                                hit.y,
                                hit.z,
                                hit.nx,
                                hit.ny,
                                hit.nz,
                                hit.speed,
                                hit.impulse});
    }
    state->listener->hits.clear();
    std::sort(owner->hits_.begin() + hitStart, owner->hits_.end(), [](const auto& a, const auto& b) {
        if (a.shapeA.shapeId != b.shapeA.shapeId) return a.shapeA.shapeId < b.shapeA.shapeId;
        return a.shapeB.shapeId < b.shapeB.shapeId;
    });
    for (const auto& weak : state->joints)
        if (auto joint = weak.lock(); joint && joint->alive && joint->owner) {
            auto&     facade = *joint->owner;
            JPH::Vec3 f(facade.getConstraintForceX(), facade.getConstraintForceY(), facade.getConstraintForceZ());
            JPH::Vec3 t(facade.getConstraintTorqueX(), facade.getConstraintTorqueY(), facade.getConstraintTorqueZ());
            if (f.Length() > joint->forceThreshold || t.Length() > joint->torqueThreshold)
                owner->jointStressEvents_.push_back({facade.getId(), facade.getBodyAId(), facade.getBodyBId(),
                                                     static_cast<int>(joint->kind), f.GetX(), f.GetY(), f.GetZ(),
                                                     t.GetX(), t.GetY(), t.GetZ()});
        }
    state->listener->begins.clear();
    state->listener->ends.clear();
    return success();
}
}  // namespace eve::physics::jolt
