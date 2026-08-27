#include "physics/SimulationBackend.h"

#include "common/Capability.h"
#include "common/Exception.h"

#include <Box2D/Box2D.h>

#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace eve::physics {
namespace {

constexpr int kMinimumIterations = 1;
constexpr int kMaximumIterations = 1024;

eve::Result<void> validateStep(const eve::SimulationStep& step,
                               const SimulationSettings& settings,
                               const SimulationObservation& observation) {
    const double seconds = step.delta.seconds();
    if (!std::isfinite(seconds) || seconds < 0.0) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "Physics simulation step duration must be finite and non-negative",
            "physics.simulationStep.delta"));
    }
    if (seconds > 0.05) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "Physics simulation step duration must be in [0, 0.05] seconds",
            "physics.simulationStep.delta"));
    }
    if (!(step.tick > observation.lastTick)) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "Physics simulation tick must increase strictly for every backend step",
            "physics.simulationStep.tick"));
    }
    if (settings.velocityIterations < kMinimumIterations ||
        settings.velocityIterations > kMaximumIterations ||
        settings.positionIterations < kMinimumIterations ||
        settings.positionIterations > kMaximumIterations ||
        settings.subStepCount < kMinimumIterations ||
        settings.subStepCount > kMaximumIterations) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "Physics simulation iteration counts must be in [1, 1024]",
            "physics.simulationStep.settings"));
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<eve::Duration> accumulatedDuration(const eve::Duration& current,
                                               const eve::Duration& delta) {
    auto total = current.tryAdd(delta);
    if (!total) {
        const eve::Status status = total.status();
        return eve::Result<eve::Duration>::failure(status);
    }
    return eve::Result<eve::Duration>::success(std::move(total).takeValue());
}

class CallbackSimulationBackend final : public ISimulationBackend {
public:
    CallbackSimulationBackend(void* context, SimulationStepCallback callback,
                              SimulationBackendKind kind,
                              SimulationDeterminism determinism)
        : context_(context), callback_(callback), kind_(kind), determinism_(determinism) {}

    [[nodiscard]] eve::Result<void> step(const eve::SimulationStep& step,
                                         const SimulationSettings& settings) override {
        auto valid = detail::validateSimulationStep(step, settings, observation_);
        if (!valid) return valid;

        auto next = detail::advanceSimulationObservation(observation_, step);
        if (!next) return eve::Result<void>::failure(next.status());

        if (callback_) callback_(context_, step, settings);

        observation_ = std::move(next).takeValue();
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] SimulationObservation observation() const noexcept override {
        return observation_;
    }

    [[nodiscard]] SimulationBackendKind kind() const noexcept override { return kind_; }

    [[nodiscard]] SimulationDeterminism determinism() const noexcept override {
        return determinism_;
    }

    [[nodiscard]] eve::Result<void> restoreObservation(
        const SimulationObservation& observation) override {
        auto valid = detail::validateSimulationObservation(
            observation, "physics.simulationBackend.restoreObservation");
        if (!valid) return valid;
        observation_ = observation;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    void* context_ = nullptr;  // borrowed by the owner that created this backend
    SimulationStepCallback callback_ = nullptr;
    SimulationBackendKind kind_ = SimulationBackendKind::Cpu;
    SimulationDeterminism determinism_ = SimulationDeterminism::ToleranceBounded;
    SimulationObservation observation_;
};

void stepBox2D(void* context, const eve::SimulationStep& step,
               const SimulationSettings& settings) noexcept {
    auto* world = static_cast<b2World*>(context);
    world->Step(static_cast<float>(step.delta.seconds()), settings.velocityIterations,
                settings.positionIterations);
}

eve::Diagnostic fallbackDiagnostic(const char* reason) {
    return eve::Diagnostic::warning(
        eve::DiagnosticCode::Unsupported,
        "Physics accelerator capability is unavailable; CPU backend selected",
        "physics.simulationBackend",
        {{"provider", IAcceleratorBackendProvider::capabilityName},
         {"selected", "cpu"},
         {"fallback", "structured-capability-fallback"},
         {"reason", reason}});
}

eve::Result<SimulationBackendSelection> cpuFallback(
    std::unique_ptr<ISimulationBackend> cpuBackend, const char* reason) {
    return eve::Result<SimulationBackendSelection>::success(
        {std::move(cpuBackend), SimulationBackendKind::Gpu,
         SimulationBackendKind::Cpu, true},
        eve::Status(eve::StatusCode::Applied, {fallbackDiagnostic(reason)}));
}

}  // namespace

namespace detail {

eve::Result<void> validateSimulationStep(const eve::SimulationStep& step,
                                         const SimulationSettings& settings,
                                         const SimulationObservation& observation) {
    return validateStep(step, settings, observation);
}

eve::Result<SimulationObservation> advanceSimulationObservation(
    const SimulationObservation& current, const eve::SimulationStep& step) {
    if (current.stepCount == std::numeric_limits<std::uint64_t>::max()) {
        return eve::Result<SimulationObservation>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation,
            "Physics simulation observation step count cannot overflow",
            "physics.simulationBackend.stepCount"));
    }
    auto total = accumulatedDuration(current.simulatedDuration, step.delta);
    if (!total) return eve::Result<SimulationObservation>::failure(total.status());

    SimulationObservation next = current;
    next.simulatedDuration = std::move(total).takeValue();
    next.lastTick = step.tick;
    next.simulatedSeconds = next.simulatedDuration.seconds();
    next.lastDeltaSeconds = static_cast<float>(step.delta.seconds());
    ++next.stepCount;
    if (!std::isfinite(next.simulatedSeconds) ||
        !std::isfinite(next.lastDeltaSeconds)) {
        return eve::Result<SimulationObservation>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation,
            "Physics simulation observation exceeded representable floating-point range",
            "physics.simulationBackend.observation"));
    }
    return eve::Result<SimulationObservation>::success(std::move(next));
}

eve::Result<void> validateSimulationObservation(const SimulationObservation& observation,
                                                const char* path) {
    if (!std::isfinite(observation.simulatedSeconds) ||
        observation.simulatedSeconds < 0.0 ||
        !std::isfinite(observation.lastDeltaSeconds) ||
        observation.lastDeltaSeconds < 0.f ||
        observation.simulatedDuration.nanoseconds() < 0 ||
        (observation.stepCount == 0 && !observation.lastTick.isZero()) ||
        (observation.stepCount == 0 && observation.lastDeltaSeconds != 0.f)) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "Simulation observation contains invalid progress values", path));
    }
    const double durationSeconds = observation.simulatedDuration.seconds();
    if (!std::isfinite(durationSeconds) ||
        std::fabs(durationSeconds - observation.simulatedSeconds) > 1e-6) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict,
            "Simulation observation seconds disagree with exact duration", path));
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

std::unique_ptr<ISimulationBackend> makeBox2DSimulationBackend(b2World* world) {
    if (!world)
        throw eve::Exception("Physics: cannot create a simulation backend for a null world");
    return makeCallbackSimulationBackend(world, &stepBox2D, SimulationBackendKind::Cpu,
                                         SimulationDeterminism::ToleranceBounded);
}

std::unique_ptr<ISimulationBackend> makeCallbackSimulationBackend(
    void* context, SimulationStepCallback callback, SimulationBackendKind kind,
    SimulationDeterminism determinism) {
    if (!callback)
        throw eve::Exception("Physics: callback simulation backend requires a callback");
    return std::make_unique<CallbackSimulationBackend>(context, callback, kind, determinism);
}

std::unique_ptr<ISimulationBackend> makeMockAcceleratorBackend() {
    return std::make_unique<CallbackSimulationBackend>(
        nullptr, nullptr, SimulationBackendKind::MockAccelerator,
        SimulationDeterminism::ToleranceBounded);
}

eve::Result<SimulationBackendSelection> selectSimulationBackend(
    SimulationBackendDomain domain, std::unique_ptr<ISimulationBackend> cpuBackend,
    void* state, bool preferAccelerator) {
    if (!cpuBackend) {
        return eve::Result<SimulationBackendSelection>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "Physics backend selection requires a CPU fallback backend",
            "physics.simulationBackend.cpu"));
    }

    if (!preferAccelerator) {
        return eve::Result<SimulationBackendSelection>::success(
            {std::move(cpuBackend), SimulationBackendKind::Cpu,
             SimulationBackendKind::Cpu, false},
            eve::Status::success(eve::StatusCode::Applied));
    }

    auto* provider = eve::cap::query<IAcceleratorBackendProvider>();
    if (!provider) return cpuFallback(std::move(cpuBackend), "capability-absent");

    try {
        if (!provider->supports(domain))
            return cpuFallback(std::move(cpuBackend), "provider-does-not-support-domain");

        auto candidate = provider->create(domain, state);
        if (!candidate) {
            // Observe the provider failure before selecting the CPU alternate path.
            const eve::Status providerStatus = candidate.status();
            (void)providerStatus;
            return cpuFallback(std::move(cpuBackend), "provider-create-failed");
        }

        auto backend = std::move(candidate).takeValue();
        if (!backend) return cpuFallback(std::move(cpuBackend), "provider-returned-null-backend");

        SimulationBackendSelection selection;
        selection.backend = std::move(backend);
        selection.requestedKind = SimulationBackendKind::Gpu;
        selection.actualKind = selection.backend->kind();
        selection.usedFallback = false;
        return eve::Result<SimulationBackendSelection>::success(
            std::move(selection), eve::Status::success(eve::StatusCode::Applied));
    } catch (const std::exception&) {
        return cpuFallback(std::move(cpuBackend), "provider-create-threw");
    } catch (...) {
        return cpuFallback(std::move(cpuBackend), "provider-create-threw-unknown");
    }
}

}  // namespace detail
}  // namespace eve::physics
