// eve-hot-path
#include "physics/field/GravitySimulation.h"

#include <cmath>
#include <new>
#include <utility>

namespace eve::physics::field {
namespace {
struct SimulationOwner {};

template <class T>
Result<T> simulationFailure(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "physics.field.simulation"));
}

bool validMotion(const MotionState& state) {
    for (double v : state.velocity)
        if (!std::isfinite(v)) return false;
    return true;
}

Result<std::vector<GravitySample>> forces(const std::vector<MotionState>& states, FieldStamp stamp,
                                          GravityPolicy policy, GravityMetrics& metrics) {
    std::vector<Particle> particles;
    particles.reserve(states.size());
    for (const auto& state : states) particles.push_back(state.particle);
    auto field = GravityField::build(particles, stamp, policy);
    if (!field) return Result<std::vector<GravitySample>>::failure(field.status());
    auto result = field.value().evaluate(stamp);
    if (!result) return Result<std::vector<GravitySample>>::failure(result.status());
    metrics = field.value().metrics();
    return result;
}
}  // namespace

struct PreparedGravityStep::Impl {
    std::weak_ptr<const SimulationOwner> owner;
    FieldStamp                           base, next;
    std::vector<MotionState>             states;
    std::vector<GravitySample>           samples;
    GravityMetrics                       metrics;
};

struct GravitySimulation::Impl {
    std::shared_ptr<const SimulationOwner> owner = std::make_shared<const SimulationOwner>();
    FieldStamp                             stamp;
    GravityPolicy                          policy;
    Duration                               fixedStep;
    std::vector<MotionState>               states;
    std::vector<GravitySample>             samples;
    GravityMetrics                         metrics;
};

PreparedGravityStep::PreparedGravityStep(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedGravityStep::~PreparedGravityStep()                                         = default;
PreparedGravityStep::PreparedGravityStep(PreparedGravityStep&&) noexcept            = default;
PreparedGravityStep& PreparedGravityStep::operator=(PreparedGravityStep&&) noexcept = default;
GravitySimulation::GravitySimulation(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GravitySimulation::~GravitySimulation()                                       = default;
GravitySimulation::GravitySimulation(GravitySimulation&&) noexcept            = default;
GravitySimulation& GravitySimulation::operator=(GravitySimulation&&) noexcept = default;

Result<GravitySimulation> GravitySimulation::create(std::span<const MotionState> initial, FieldStamp stamp,
                                                    GravityPolicy policy, Duration fixedStep) {
    if (initial.empty() || initial.size() > policy.maxParticles || fixedStep.nanoseconds() <= 0)
        return simulationFailure<GravitySimulation>(DiagnosticCode::InvalidArgument,
                                                    "Invalid state count or fixed step");
    try {
        auto impl       = std::make_unique<Impl>();
        impl->stamp     = stamp;
        impl->policy    = policy;
        impl->fixedStep = fixedStep;
        for (const auto& state : initial)
            if (!validMotion(state))
                return simulationFailure<GravitySimulation>(DiagnosticCode::InvalidArgument, "Velocity must be finite");
        impl->states.assign(initial.begin(), initial.end());
        auto sampled = forces(impl->states, stamp, policy, impl->metrics);
        if (!sampled) return Result<GravitySimulation>::failure(sampled.status());
        impl->samples = std::move(sampled.value());
        return Result<GravitySimulation>::success(GravitySimulation(std::move(impl)));
    } catch (const std::bad_alloc&) {
        return simulationFailure<GravitySimulation>(DiagnosticCode::Failed, "Initial simulation allocation failed");
    }
}

Result<PreparedGravityStep> GravitySimulation::prepareStep(SimulationStep step) const {
    if (!impl_) return simulationFailure<PreparedGravityStep>(DiagnosticCode::StaleHandle, "Simulation was moved");
    const auto tick     = impl_->stamp.tick.incremented();
    const auto revision = impl_->stamp.revision.incremented();
    if (!tick || !revision || step.tick != *tick || step.delta != impl_->fixedStep)
        return simulationFailure<PreparedGravityStep>(DiagnosticCode::InvalidArgument,
                                                      "Require next tick and configured dt");
    const double dt = static_cast<double>(step.delta.nanoseconds()) * 1e-9;
    try {
        auto candidate    = std::make_unique<PreparedGravityStep::Impl>();
        candidate->owner  = impl_->owner;
        candidate->base   = impl_->stamp;
        candidate->next   = {*tick, *revision};
        candidate->states = impl_->states;
        for (std::size_t i = 0; i < candidate->states.size(); ++i) {
            auto& state = candidate->states[i];
            for (unsigned axis = 0; axis < 3; ++axis) {
                state.velocity[axis] += 0.5 * dt * impl_->samples[i].acceleration[axis];
                state.particle.position[axis] += dt * state.velocity[axis];
            }
        }
        auto sampled = forces(candidate->states, candidate->next, impl_->policy, candidate->metrics);
        if (!sampled) return Result<PreparedGravityStep>::failure(sampled.status());
        candidate->samples = std::move(sampled.value());
        for (std::size_t i = 0; i < candidate->states.size(); ++i) {
            auto& state = candidate->states[i];
            for (unsigned axis = 0; axis < 3; ++axis)
                state.velocity[axis] += 0.5 * dt * candidate->samples[i].acceleration[axis];
            if (!validMotion(state))
                return simulationFailure<PreparedGravityStep>(DiagnosticCode::Unsupported, "Velocity range overflow");
        }
        return Result<PreparedGravityStep>::success(PreparedGravityStep(std::move(candidate)));
    } catch (const std::bad_alloc&) {
        return simulationFailure<PreparedGravityStep>(DiagnosticCode::Failed, "Step staging allocation failed");
    }
}

Result<void> GravitySimulation::commit(PreparedGravityStep step) {
    if (!impl_ || !step.impl_ || step.impl_->owner.lock() != impl_->owner || step.impl_->base != impl_->stamp)
        return simulationFailure<void>(DiagnosticCode::StaleHandle, "Candidate is foreign, consumed or superseded");
    impl_->stamp = step.impl_->next;
    impl_->states.swap(step.impl_->states);
    impl_->samples.swap(step.impl_->samples);
    impl_->metrics = step.impl_->metrics;
    return Result<void>::success();
}

Result<MotionSnapshot> GravitySimulation::snapshot() const {
    if (!impl_) return simulationFailure<MotionSnapshot>(DiagnosticCode::StaleHandle, "Simulation was moved");
    try {
        return Result<MotionSnapshot>::success(MotionSnapshot{impl_->stamp, impl_->states});
    } catch (const std::bad_alloc&) {
        return simulationFailure<MotionSnapshot>(DiagnosticCode::Failed, "Snapshot allocation failed");
    }
}

GravityMetrics GravitySimulation::metrics() const noexcept { return impl_ ? impl_->metrics : GravityMetrics{}; }

}  // namespace eve::physics::field
