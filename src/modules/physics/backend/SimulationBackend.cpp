#include "physics/backend/SimulationBackend.h"

#include "common/Capability.h"
#include "common/Exception.h"

#include <Box2D/Box2D.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::physics {
namespace {

constexpr int kMinimumIterations = 1;
constexpr int kMaximumIterations = 1024;

eve::Result<void> validateStep(const eve::SimulationStep& step, const SimulationSettings& settings,
                               const SimulationObservation& observation) {
    const double seconds = step.delta.seconds();
    if (!std::isfinite(seconds) || seconds < 0.0) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Physics simulation step duration must be finite and non-negative",
            "physics.simulationStep.delta"));
    }
    if (seconds > 0.05) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Physics simulation step duration must be in [0, 0.05] seconds",
            "physics.simulationStep.delta"));
    }
    if (!(step.tick > observation.lastTick)) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "Physics simulation tick must increase strictly for every backend step", "physics.simulationStep.tick"));
    }
    if (settings.velocityIterations < kMinimumIterations || settings.velocityIterations > kMaximumIterations ||
        settings.positionIterations < kMinimumIterations || settings.positionIterations > kMaximumIterations ||
        settings.subStepCount < kMinimumIterations || settings.subStepCount > kMaximumIterations) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Physics simulation iteration counts must be in [1, 1024]",
            "physics.simulationStep.settings"));
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<eve::Duration> accumulatedDuration(const eve::Duration& current, const eve::Duration& delta) {
    auto total = current.tryAdd(delta);
    if (!total) {
        const eve::Status status = total.status();
        return eve::Result<eve::Duration>::failure(status);
    }
    return eve::Result<eve::Duration>::success(std::move(total).takeValue());
}

class CallbackSimulationBackend final : public ISimulationBackend {
public:
    CallbackSimulationBackend(void* context, SimulationStepCallback callback, SimulationBackendKind kind,
                              SimulationDeterminism determinism)
        : context_(context), callback_(callback), kind_(kind), determinism_(determinism) {}

    [[nodiscard]] eve::Result<void> step(const eve::SimulationStep& step, const SimulationSettings& settings) override {
        auto valid = detail::validateSimulationStep(step, settings, observation_);
        if (!valid) return valid;

        auto next = detail::advanceSimulationObservation(observation_, step);
        if (!next) return eve::Result<void>::failure(next.status());

        if (callback_) callback_(context_, step, settings);

        observation_ = std::move(next).takeValue();
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] SimulationObservation observation() const noexcept override { return observation_; }

    [[nodiscard]] SimulationBackendKind kind() const noexcept override { return kind_; }

    [[nodiscard]] SimulationDeterminism determinism() const noexcept override { return determinism_; }

    [[nodiscard]] eve::Result<void> restoreObservation(const SimulationObservation& observation) override {
        auto valid = detail::validateSimulationObservation(observation, "physics.simulationBackend.restoreObservation");
        if (!valid) return valid;
        observation_ = observation;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    void*                  context_     = nullptr;  // borrowed by the owner that created this backend
    SimulationStepCallback callback_    = nullptr;
    SimulationBackendKind  kind_        = SimulationBackendKind::Cpu;
    SimulationDeterminism  determinism_ = SimulationDeterminism::ToleranceBounded;
    SimulationObservation  observation_;
};

void stepBox2D(void* context, const eve::SimulationStep& step, const SimulationSettings& settings) noexcept {
    auto* world = static_cast<b2World*>(context);
    world->Step(static_cast<float>(step.delta.seconds()), settings.velocityIterations, settings.positionIterations);
}

eve::Diagnostic fallbackDiagnostic(SimulationBackendDomain domain, const char* reason) {
    return eve::Diagnostic::warning(eve::DiagnosticCode::Unsupported,
                                    "Physics accelerator capability is unavailable; CPU backend selected",
                                    "physics.simulationBackend",
                                    {{"provider", IAcceleratorBackendProvider::capabilityName},
                                     {"selected", "cpu"},
                                     {"domain", std::string(simulationBackendDomainName(domain))},
                                     {"fallback", "structured-capability-fallback"},
                                     {"reason", reason}});
}

eve::Result<SimulationBackendSelection> cpuFallback(std::unique_ptr<ISimulationBackend> cpuBackend,
                                                    SimulationBackendDomain domain,
                                                    const char* reason,
                                                    std::vector<eve::Diagnostic> diagnostics = {}) {
    diagnostics.insert(diagnostics.begin(), fallbackDiagnostic(domain, reason));
    return eve::Result<SimulationBackendSelection>::success(
        {std::move(cpuBackend), SimulationBackendKind::Gpu, SimulationBackendKind::Cpu, true},
        eve::Status(eve::StatusCode::Applied, std::move(diagnostics)));
}

std::unordered_set<IAcceleratorBackendProvider*>& registeredAcceleratorProviders() {
    static std::unordered_set<IAcceleratorBackendProvider*> providers;
    return providers;
}

eve::Diagnostic attemptedProviderWarning(const eve::Diagnostic& diagnostic, std::size_t attempt) {
    auto details = diagnostic.details();
    details.emplace_back("providerAttempt", std::to_string(attempt));
    return eve::Diagnostic::warning(diagnostic.code(), diagnostic.message(), diagnostic.path(), std::move(details),
                                    diagnostic.source());
}

}  // namespace

eve::Result<AcceleratorBackendProviderRegistration> AcceleratorBackendProviderRegistration::registerProvider(
    IAcceleratorBackendProvider& provider, int priority) {
    auto& providers = registeredAcceleratorProviders();
    if (providers.contains(&provider)) {
        return eve::Result<AcceleratorBackendProviderRegistration>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "Physics accelerator provider is already registered",
            "physics.simulationBackend.provider.registration"));
    }
    providers.insert(&provider);
    eve::cap::addListener<IAcceleratorBackendProvider>(&provider, priority);
    AcceleratorBackendProviderRegistration registration;
    registration.provider_ = &provider;
    return eve::Result<AcceleratorBackendProviderRegistration>::success(std::move(registration));
}

AcceleratorBackendProviderRegistration::~AcceleratorBackendProviderRegistration() { reset(); }

AcceleratorBackendProviderRegistration::AcceleratorBackendProviderRegistration(
    AcceleratorBackendProviderRegistration&& other) noexcept
    : provider_(std::exchange(other.provider_, nullptr)) {}

AcceleratorBackendProviderRegistration& AcceleratorBackendProviderRegistration::operator=(
    AcceleratorBackendProviderRegistration&& other) noexcept {
    if (this == &other) return *this;
    reset();
    provider_ = std::exchange(other.provider_, nullptr);
    return *this;
}

void AcceleratorBackendProviderRegistration::reset() noexcept {
    if (!provider_) return;
    eve::cap::removeListener<IAcceleratorBackendProvider>(provider_);
    registeredAcceleratorProviders().erase(provider_);
    provider_ = nullptr;
}

namespace detail {

eve::Result<void> validateSimulationStep(const eve::SimulationStep& step, const SimulationSettings& settings,
                                         const SimulationObservation& observation) {
    return validateStep(step, settings, observation);
}

eve::Result<SimulationObservation> advanceSimulationObservation(const SimulationObservation& current,
                                                                const eve::SimulationStep&   step) {
    if (current.stepCount == std::numeric_limits<std::uint64_t>::max()) {
        return eve::Result<SimulationObservation>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "Physics simulation observation step count cannot overflow",
            "physics.simulationBackend.stepCount"));
    }
    auto total = accumulatedDuration(current.simulatedDuration, step.delta);
    if (!total) return eve::Result<SimulationObservation>::failure(total.status());

    SimulationObservation next = current;
    next.simulatedDuration     = std::move(total).takeValue();
    next.lastTick              = step.tick;
    next.simulatedSeconds      = next.simulatedDuration.seconds();
    next.lastDeltaSeconds      = static_cast<float>(step.delta.seconds());
    ++next.stepCount;
    if (!std::isfinite(next.simulatedSeconds) || !std::isfinite(next.lastDeltaSeconds)) {
        return eve::Result<SimulationObservation>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                   "Physics simulation observation exceeded representable floating-point range",
                                   "physics.simulationBackend.observation"));
    }
    return eve::Result<SimulationObservation>::success(std::move(next));
}

eve::Result<void> validateSimulationObservation(const SimulationObservation& observation, const char* path) {
    if (!std::isfinite(observation.simulatedSeconds) || observation.simulatedSeconds < 0.0 ||
        !std::isfinite(observation.lastDeltaSeconds) || observation.lastDeltaSeconds < 0.f ||
        observation.simulatedDuration.nanoseconds() < 0 ||
        (observation.stepCount == 0 && !observation.lastTick.isZero()) ||
        (observation.stepCount == 0 && observation.lastDeltaSeconds != 0.f)) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Simulation observation contains invalid progress values", path));
    }
    const double durationSeconds = observation.simulatedDuration.seconds();
    if (!std::isfinite(durationSeconds) || std::fabs(durationSeconds - observation.simulatedSeconds) > 1e-6) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "Simulation observation seconds disagree with exact duration", path));
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

std::unique_ptr<ISimulationBackend> makeBox2DSimulationBackend(b2World* world) {
    if (!world) throw eve::Exception("Physics: cannot create a simulation backend for a null world");
    return makeCallbackSimulationBackend(world, &stepBox2D, SimulationBackendKind::Cpu,
                                         SimulationDeterminism::ToleranceBounded);
}

std::unique_ptr<ISimulationBackend> makeCallbackSimulationBackend(void* context, SimulationStepCallback callback,
                                                                  SimulationBackendKind kind,
                                                                  SimulationDeterminism determinism) {
    if (!callback) throw eve::Exception("Physics: callback simulation backend requires a callback");
    return std::make_unique<CallbackSimulationBackend>(context, callback, kind, determinism);
}

std::unique_ptr<ISimulationBackend> makeMockAcceleratorBackend() {
    return std::make_unique<CallbackSimulationBackend>(nullptr, nullptr, SimulationBackendKind::MockAccelerator,
                                                       SimulationDeterminism::ToleranceBounded);
}

eve::Result<SimulationBackendSelection> selectSimulationBackend(SimulationBackendDomain             domain,
                                                                std::unique_ptr<ISimulationBackend> cpuBackend,
                                                                void* state, bool preferAccelerator) {
    if (!cpuBackend) {
        return eve::Result<SimulationBackendSelection>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Physics backend selection requires a CPU fallback backend",
            "physics.simulationBackend.cpu"));
    }

    if (!preferAccelerator) {
        return eve::Result<SimulationBackendSelection>::success(
            {std::move(cpuBackend), SimulationBackendKind::Cpu, SimulationBackendKind::Cpu, false},
            eve::Status::success(eve::StatusCode::Applied));
    }

    std::vector<IAcceleratorBackendProvider*> providers;
    const auto listenerCount = eve::cap::listenerCount<IAcceleratorBackendProvider>();
    providers.reserve(listenerCount + 1);
    for (std::size_t index = 0; index < listenerCount; ++index) {
        if (auto* provider = eve::cap::listenerAt<IAcceleratorBackendProvider>(index)) providers.push_back(provider);
    }
    if (auto* legacy = eve::cap::query<IAcceleratorBackendProvider>();
        legacy && std::find(providers.begin(), providers.end(), legacy) == providers.end()) {
        providers.push_back(legacy);
    }
    if (providers.empty()) return cpuFallback(std::move(cpuBackend), domain, "capability-absent");

    std::vector<eve::Diagnostic> failedDiagnostics;
    bool                         supported = false;
    std::size_t attempt = 0;
    for (auto* provider : providers) {
        if (!provider->supports(domain)) continue;
        supported = true;
        ++attempt;
        try {
            auto candidate = provider->create(domain, state);
            if (!candidate) {
                const auto& diagnostics = candidate.diagnostics();
                for (const auto& diagnostic : diagnostics)
                    failedDiagnostics.push_back(attemptedProviderWarning(diagnostic, attempt));
                continue;
            }
            auto successDiagnostics = candidate.diagnostics();
            for (auto& diagnostic : successDiagnostics)
                diagnostic.addDetail("providerAttempt", std::to_string(attempt));
            auto backend = std::move(candidate).takeValue();
            if (!backend) {
                failedDiagnostics.push_back(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvariantViolation, "Physics accelerator provider returned a null backend",
                    "physics.simulationBackend.provider.create"));
                continue;
            }

            SimulationBackendSelection selection;
            selection.backend       = std::move(backend);
            selection.requestedKind = SimulationBackendKind::Gpu;
            selection.actualKind    = selection.backend->kind();
            selection.usedFallback  = false;
            failedDiagnostics.insert(failedDiagnostics.end(), successDiagnostics.begin(), successDiagnostics.end());
            return eve::Result<SimulationBackendSelection>::success(std::move(selection),
                                                                    eve::Status(eve::StatusCode::Applied,
                                                                                std::move(failedDiagnostics)));
        } catch (const std::exception& error) {
            failedDiagnostics.push_back(eve::Diagnostic::warning(
                eve::DiagnosticCode::CallbackFailure, error.what(), "physics.simulationBackend.provider.create",
                {{"providerAttempt", std::to_string(attempt)}}));
        } catch (...) {
            failedDiagnostics.push_back(eve::Diagnostic::warning(
                eve::DiagnosticCode::CallbackFailure,
                "Physics accelerator provider threw a non-standard exception",
                "physics.simulationBackend.provider.create", {{"providerAttempt", std::to_string(attempt)}}));
        }
    }

    return cpuFallback(std::move(cpuBackend), domain,
                       supported ? "all-supporting-providers-failed" : "provider-does-not-support-domain",
                       std::move(failedDiagnostics));
}

}  // namespace detail
}  // namespace eve::physics
