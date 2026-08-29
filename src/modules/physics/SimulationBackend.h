#pragma once

/**
 * @file SimulationBackend.h
 * @brief Backend-neutral, observable fixed-step contract for physics domains.
 *
 * This header is part of the physics domain boundary. It intentionally does
 * not include graphics, gpgpu, or a renderer-facing type. Accelerator providers
 * enter through the capability interface declared here and may be absent.
 */

#include "common/Result.h"
#include "common/Time.h"

#include <cstdint>
#include <memory>
#include <ostream>
#include <string_view>

class b2World;

namespace eve::physics {

/**
 * @brief Kind of implementation that owns a simulation step.
 *
 * `Gpu` is reserved for a real accelerator provider. `MockAccelerator` is a
 * provider used by contract tests and headless integrations; it deliberately
 * has no graphics or device dependency.
 */
enum class SimulationBackendKind {
    Cpu,
    Gpu,
    MockAccelerator,
};

/** @brief Domain state consumed by an accelerator provider. */
enum class SimulationBackendDomain {
    World2D,
    World3D,
    Cloth2D,
    Cloth3D,
    SurfaceFluid,
};

/** @brief Determinism guarantee made by a simulation backend. */
enum class SimulationDeterminism {
    /** @brief Same inputs produce identical bits under the declared runtime. */
    BitExact,
    /** @brief Results are equivalent within a documented numeric tolerance. */
    ToleranceBounded,
    /** @brief Results may vary and are not suitable for replay equality. */
    ExplicitlyNondeterministic,
};

/** @brief Returns the stable backend-kind spelling. */
[[nodiscard]] constexpr std::string_view simulationBackendKindName(SimulationBackendKind kind) noexcept {
    switch (kind) {
        case SimulationBackendKind::Cpu: return "cpu";
        case SimulationBackendKind::Gpu: return "gpu";
        case SimulationBackendKind::MockAccelerator: return "mock_accelerator";
    }
    return "unknown";
}

/** @brief Returns the stable determinism-contract spelling. */
[[nodiscard]] constexpr std::string_view simulationDeterminismName(SimulationDeterminism determinism) noexcept {
    switch (determinism) {
        case SimulationDeterminism::BitExact: return "bit_exact";
        case SimulationDeterminism::ToleranceBounded: return "tolerance_bounded";
        case SimulationDeterminism::ExplicitlyNondeterministic: return "explicitly_nondeterministic";
    }
    return "unknown";
}

/** @brief Writes a backend kind for diagnostics. */
inline std::ostream& operator<<(std::ostream& stream, SimulationBackendKind kind) {
    return stream << simulationBackendKindName(kind);
}

/** @brief Writes a determinism contract for diagnostics. */
inline std::ostream& operator<<(std::ostream& stream, SimulationDeterminism determinism) {
    return stream << simulationDeterminismName(determinism);
}

/**
 * @brief Compatibility alias for the common injected simulation step.
 *
 * Physics does not define a second time value. `eve::SimulationStep` carries
 * both the monotonically increasing persisted tick and the fixed duration.
 */
using SimulationStep = eve::SimulationStep;

/**
 * @brief Validated solver policy for one simulation step.
 *
 * `velocityIterations` and `positionIterations` apply to rigid-body solvers;
 * `subStepCount` applies to a 3D or specialized solver. Backends must consume
 * this policy as supplied and must not silently clamp or replace it.
 */
struct SimulationSettings {
    /** @brief Positive rigid-body velocity iteration count. */
    int velocityIterations = 8;
    /** @brief Positive rigid-body position iteration count. */
    int positionIterations = 3;
    /** @brief Positive specialized-solver substep count. */
    int subStepCount = 4;
};

/**
 * @brief Observable backend progress shared by CPU and accelerator providers.
 *
 * Values describe completed steps only. `simulatedDuration` and
 * `lastTick` are authoritative for deterministic state; the seconds fields
 * are compatibility projections for legacy diagnostics and scripts.
 */
struct SimulationObservation {
    /** @brief Number of completed steps since backend creation. */
    std::uint64_t stepCount = 0;
    /** @brief Tick of the most recently completed step. */
    eve::SimulationTick lastTick = eve::SimulationTick::zero();
    /** @brief Accumulated logical simulation time, never wall-clock time. */
    eve::Duration simulatedDuration = eve::Duration::zero();
    /** @brief Accumulated logical simulation time in seconds. */
    double simulatedSeconds = 0.0;
    /** @brief Duration consumed by the most recently completed step. */
    float lastDeltaSeconds = 0.f;
};

/**
 * @brief Minimal observable contract for physics simulation providers.
 *
 * The backend borrows domain-owned state and must be destroyed before that
 * state. Implementations own no graphics or presentation resources. Calls
 * are simulation-thread affine and must not run concurrently with another
 * mutation of the same domain state. A failed step leaves observation and
 * domain state unchanged.
 */
class ISimulationBackend {
public:
    /** @brief Releases the backend; borrowed domain state is not destroyed. */
    virtual ~ISimulationBackend() = default;

    /**
     * @brief Advances the borrowed solver state by one checked fixed step.
     * @param step Injected tick and duration; tick must increase strictly.
     * @param settings Solver policy for this step.
     * @return Applied with updated state, or a structured rejection/failure.
     */
    [[nodiscard("check the step outcome or explicitly ignore it")]]
    virtual eve::Result<void> step(const eve::SimulationStep& step, const SimulationSettings& settings) = 0;

    /**
     * @brief Returns completed-step observables by value.
     * @return A snapshot that remains valid after the backend changes.
     */
    [[nodiscard("inspect simulation progress or explicitly ignore it")]]
    virtual SimulationObservation observation() const noexcept = 0;

    /**
     * @brief Identifies the provider family.
     * @return CPU, GPU, or a named test accelerator.
     */
    [[nodiscard]] virtual SimulationBackendKind kind() const noexcept = 0;

    /**
     * @brief Returns the provider's replay/determinism guarantee.
     * @return The declared determinism level.
     */
    [[nodiscard]] virtual SimulationDeterminism determinism() const noexcept = 0;

    /**
     * @brief Restores progress metadata after the owner restores its state.
     * @param observation Completed-step observation to install.
     * @return Applied when supported, or Unsupported when the backend cannot
     *         restore metadata without rebuilding its state.
     * @remarks This operation changes only backend-owned progress metadata;
     *          callers must restore the domain state atomically as one unit.
     */
    [[nodiscard("check backend observation restore")]]
    virtual eve::Result<void> restoreObservation(const SimulationObservation& /*observation*/) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "This simulation backend does not support observation restore",
            "physics.simulationBackend.restoreObservation"));
    }
};

/**
 * @brief Optional provider that creates an accelerator backend for domain state.
 *
 * The capability is intentionally not required by physics. `state` is an
 * opaque borrowed pointer valid for the duration of `create`; a successful
 * backend must retain only state whose lifetime is guaranteed by its owner.
 * Providers must return `Unsupported` when a domain is not implemented.
 */
class IAcceleratorBackendProvider {
public:
    static constexpr const char* capabilityName = "eve.physics.IAcceleratorBackendProvider";

    /** @brief Releases the provider; it does not own the domain state. */
    virtual ~IAcceleratorBackendProvider() = default;

    /**
     * @brief Reports whether this provider implements a domain.
     * @param domain Domain kind being queried.
     * @return true only when create() can service the domain.
     */
    [[nodiscard]] virtual bool supports(SimulationBackendDomain domain) const noexcept = 0;

    /**
     * @brief Creates an accelerator backend for a borrowed domain state.
     * @param domain Domain kind requested by the owner.
     * @param state Opaque domain state, valid only under the documented owner lifetime.
     * @return A backend or a structured Unsupported/Failed status.
     */
    [[nodiscard("inspect provider creation or explicitly fall back")]]
    virtual eve::Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain domain, void* state) = 0;
};

/**
 * @brief Result of choosing an optional accelerator or CPU alternate path.
 *
 * `usedFallback` is explicit state, while the returned Result status carries
 * a warning diagnostic when the optional capability is absent or unusable.
 */
struct SimulationBackendSelection {
    /** @brief Owning selected backend; it borrows the supplied domain state. */
    std::unique_ptr<ISimulationBackend> backend;
    /** @brief Provider family requested by the owner. */
    SimulationBackendKind requestedKind = SimulationBackendKind::Cpu;
    /** @brief Provider family actually selected. */
    SimulationBackendKind actualKind = SimulationBackendKind::Cpu;
    /** @brief Whether CPU was selected after an accelerator request. */
    bool usedFallback = false;
};

/** @brief Callback used by a domain-owned CPU or test backend adapter. */
using SimulationStepCallback = void (*)(void* context, const eve::SimulationStep& step,
                                        const SimulationSettings& settings) noexcept;

namespace detail {

/**
 * @brief Validates one simulation step against a completed observation.
 * @param step Injected tick and duration.
 * @param settings Solver policy.
 * @param observation Existing completed-step state.
 * @return Applied when all shared backend invariants hold.
 */
[[nodiscard("check simulation-step validation")]]
eve::Result<void> validateSimulationStep(const eve::SimulationStep& step, const SimulationSettings& settings,
                                         const SimulationObservation& observation);

/**
 * @brief Produces the next observation without mutating the current one.
 * @param current Existing completed-step state.
 * @param step Successfully applied step.
 * @return The next observation, or an overflow/invalid-input failure.
 */
[[nodiscard("check observation advancement")]]
eve::Result<SimulationObservation> advanceSimulationObservation(const SimulationObservation& current,
                                                                const eve::SimulationStep&   step);

/**
 * @brief Validates restored backend progress metadata.
 * @param observation Candidate metadata to install.
 * @param path Diagnostic path owned by the caller.
 * @return Applied when all fields are finite, non-negative and coherent.
 */
[[nodiscard("check observation validation")]]
eve::Result<void> validateSimulationObservation(const SimulationObservation& observation, const char* path);

/**
 * @brief Creates the built-in Box2D CPU backend adapter.
 * @param world Borrowed Box2D world; it must outlive the returned backend.
 * @return An owning backend adapter.
 * @throws eve::Exception when `world` is null.
 *
 * This composition hook is intentionally independent of Graphics and Gpgpu.
 */
[[nodiscard("retain the backend until its borrowed world is destroyed")]]
std::unique_ptr<ISimulationBackend> makeBox2DSimulationBackend(b2World* world);

/**
 * @brief Creates a callback-backed backend for a domain-owned CPU or test solver.
 * @param context Borrowed state passed to callback for every step.
 * @param callback Non-null operation that advances the state.
 * @param kind Provider label exposed through the observable contract.
 * @param determinism Declared replay/numeric guarantee.
 * @return An owning backend adapter.
 */
[[nodiscard("retain the backend while its callback state is live")]]
std::unique_ptr<ISimulationBackend> makeCallbackSimulationBackend(void* context, SimulationStepCallback callback,
                                                                  SimulationBackendKind kind,
                                                                  SimulationDeterminism determinism);

/**
 * @brief Creates a no-op mock accelerator used for headless contract tests.
 * @return A backend with observable tick/progress but no solver dependency.
 */
[[nodiscard("retain the mock backend for contract execution")]]
std::unique_ptr<ISimulationBackend> makeMockAcceleratorBackend();

/**
 * @brief Selects an optional accelerator and reports a structured CPU alternate path.
 * @param domain Domain being stepped.
 * @param cpuBackend Already-created CPU alternate backend.
 * @param state Opaque borrowed domain state passed to an accelerator provider.
 * @param preferAccelerator Whether to query the optional capability.
 * @return Selected backend; absent/unsupported providers return Applied with a
 *         Warning diagnostic and `usedFallback=true`.
 */
[[nodiscard("inspect the selected backend and fallback diagnostics")]]
eve::Result<SimulationBackendSelection> selectSimulationBackend(SimulationBackendDomain             domain,
                                                                std::unique_ptr<ISimulationBackend> cpuBackend,
                                                                void* state, bool preferAccelerator = true);

}  // namespace detail
}  // namespace eve::physics
