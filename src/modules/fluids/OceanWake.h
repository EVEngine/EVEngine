#pragma once

#include "common/Export.h"

#include "common/Result.h"

#include <glm/glm.hpp>
#include <memory>
#include <span>
#include <vector>

namespace eve::fluids {
class OceanSpectrumFrame;
class OceanWakeStep;
/** @brief Local dispersive wave tile, SI units; kept independent of the observing camera. */
struct OceanWakeSettings {
    unsigned   resolution      = 64;
    double     length          = 128.0;
    double     depth           = 100.0;
    double     gravity         = 9.81;
    double     damping         = 0.15;
    double     absorptionWidth = 16.0;
    double     foamDecay       = 0.5;
    glm::dvec2 center{0.0};
};
/** @brief Localized vertical velocity impulse, with optional directional bow/stern dipole. */
struct OceanWakeImpulse {
    glm::dvec2 position{0.0};
    double     radius   = 2.0;
    double     velocity = 0.0;
    glm::dvec2 direction{0.0};
    double     foam = 0.0;
};
/** @brief Local world-space height perturbation, world slope and vertical particle speed. */
struct OceanWakeSample {
    double     height   = 0.0;
    double     velocity = 0.0;
    glm::dvec2 gradient{0.0};
    double     foam = 0.0;
};
/** @brief Call-only borrowed raw grids; owner must remain alive and unmodified during consumption. */
struct OceanWakeGridView {
    OceanWakeSettings       settings;
    double                  time = 0.0;
    std::span<const double> height;
    std::span<const double> foam;
};
/**
 * @brief Caller-owned wave field with finite-depth gravity dispersion and absorbing boundaries.
 * No graphics/physics pointers or internal clock. All mutations are owner-thread only;
 * const reads may run concurrently only while the owner is not mutating. No callbacks.
 */
class OceanWake {
public:
    /** @brief Construct a checked field via explicit settings; copies settings and retains no input. */
    [[nodiscard]] static EVENGINE_API_DOMAINS Result<std::unique_ptr<OceanWake>> create(
        const OceanWakeSettings& settings);
    EVENGINE_API_DOMAINS ~OceanWake();
    OceanWake(const OceanWake&)            = delete;
    OceanWake& operator=(const OceanWake&) = delete;
    /**
     * @brief Inject a bounded batch of localized disturbances atomically into vertical velocity/foam.
     * @param impulses At most 256 finite sources; inputs borrowed for this call only.
     * @return Success or InvalidArgument. Entire batch validates before mutation.
     * @cost O(cells * sources); batch hull contacts once per fixed step.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> inject(std::span<const OceanWakeImpulse> impulses);
    /**
     * @brief Prepare an owning candidate for one fixed step with exact per-mode dispersion omega² = g*k*tanh(k*depth).
     * @param dt Seconds in (0,1/30]; endTime must equal time()+dt within 1e-8 seconds.
     * @param endTime Injected simulation timestamp, not a wall clock.
     * @param water Borrowed immutable start-of-step frame, alive for this call only.
     * Foam coverage follows its horizontal particle velocity using midpoint backtracing
     * and bounded bilinear sampling. Open boundaries discard outgoing coverage; this is
     * a dissipative coverage approximation, not conserved fluid mass. No input is retained.
     * @return Owning candidate, InvalidArgument, or spectral query diagnostic.
     * Preparation never changes the live owner; discard the candidate to cancel.
     * @cost O(N² log N) plus two surface queries per foam-grid cell; execute once
     * per interaction region per fixed step. Empty foam skips transport queries.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<std::unique_ptr<OceanWakeStep>> prepareStep(
        double dt, double endTime, const OceanSpectrumFrame& water) const;
    /**
     * @brief Publish a prepared candidate by constant-time state swap without allocation.
     * @param step Borrowed only for this call; successful commit consumes its publication right.
     * @return Success or Conflict if foreign, already consumed, or invalidated by any owner mutation.
     * Rejection changes neither owner nor candidate. Old arrays remain in the consumed candidate
     * until its destruction. Owner-thread only, no callbacks or retained references.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> commitStep(OceanWakeStep& step);
    /**
     * @brief Move the window by whole cells, retaining overlapping world-space history.
     * @param center Requested finite center; quantized to the nearest cell relative to current center.
     * @return Success or InvalidArgument; new exposed cells start at rest.
     * @cost O(N²) copy; amortize by moving only when interaction approaches the boundary.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> recenter(glm::dvec2 center);
    /** @brief Query a world point; returns zero outside the tile, InvalidArgument for nonfinite inputs. */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<OceanWakeSample> sample(glm::dvec2 worldXZ) const;
    /** @brief Current externally advanced simulation time, initially zero. */
    EVENGINE_API_DOMAINS double time() const;
    /** @brief Owning configuration including current window center. */
    EVENGINE_API_DOMAINS OceanWakeSettings settings() const;
    /** @brief Borrow raw height/foam for immediate GPU upload; never retain across mutation or destruction. */
    [[nodiscard]] EVENGINE_API_DOMAINS OceanWakeGridView gridView() const;

private:
    friend class OceanWakeStep;
    [[nodiscard]] Result<void> advance(double dt, double endTime, const OceanSpectrumFrame& water);
    struct Impl;
    explicit OceanWake(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/**
 * @brief Owning, single-use candidate; no source frame, owner pointer or GPU resource is retained.
 * Safe to destroy after its source owner or frame. Commit requires the original live owner
 * and unchanged revision. Mutations and commit are owner-thread only; no callbacks.
 */
class OceanWakeStep {
public:
    EVENGINE_API_DOMAINS ~OceanWakeStep();
    OceanWakeStep(const OceanWakeStep&)            = delete;
    OceanWakeStep& operator=(const OceanWakeStep&) = delete;

private:
    friend class OceanWake;
    explicit OceanWakeStep(std::unique_ptr<OceanWake> candidate);
    std::unique_ptr<OceanWake> candidate_;
    bool                       consumed_ = false;
};
}  // namespace eve::fluids
