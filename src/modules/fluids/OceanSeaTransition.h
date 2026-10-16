#pragma once

#include "common/Export.h"

#include "fluids/OceanSpectrum.h"

namespace eve::fluids {
class OceanSeaTransitionFrameBuilder;
/** @brief Immutable finite sea-state transition; the caller supplies the only simulation clock.
 * Owns both endpoint spectra. Concurrent reads are safe; no provider, callbacks or mutable timer.
 * Quintic blending has zero first/second derivatives at both endpoints. Its velocity includes
 * the blend derivative, so buoyancy/drag consume the same changing surface as rendering.
 */
class OceanSeaTransition {
public:
    /** @brief Prepare an owning frame builder, safe after this transition is destroyed. */
    [[nodiscard]] EVENGINE_API_DOMAINS OceanSeaTransitionFrameBuilder frameBuilder(double time) const;

private:
    friend class OceanGpuSpectrumBuilder;
    friend class OceanSeaTransitionBuilder;
    OceanSeaTransition(OceanSpectrum from, OceanSpectrum to, double start, double duration)
        : from_(std::move(from)), to_(std::move(to)), start_(start), duration_(duration) {}
    OceanSpectrum from_, to_;
    double        start_, duration_;
};
/** @brief Own endpoint leases immediately; changing/destroying original spectra cannot affect build. */
class OceanSeaTransitionBuilder {
public:
    /** @brief Copy immutable endpoint leases; spectra use matching tile geometry, current and gravity. */
    OceanSeaTransitionBuilder(const OceanSpectrum& from, const OceanSpectrum& to) : from_(from), to_(to) {}
    double start    = 0.0;
    double duration = 30.0;
    /** @brief Validate duration and endpoint compatibility without mutating either spectrum.
     * @return Owning transition or InvalidArgument. Sea level, seed, wave height, period, direction,
     * enhancement and choppiness may differ; current/gravity and tile resolution/length must match.
     * @cost Constant bounded validation of at most four bands; no FFT or GPU work.
     * @thread Reentrant const reads; no callbacks or retained borrowed references.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<OceanSeaTransition> build() const;

private:
    OceanSpectrum from_, to_;
};
/** @brief Owning transition projection preparation; no borrowed source lifetime requirements. */
class OceanSeaTransitionFrameBuilder {
public:
    /** @brief Evaluate endpoint spectra and blend displacement and material velocity atomically.
     * Outside the interval evaluates just the corresponding endpoint. All consumers receive the
     * ordinary OceanSpectrumFrame; no separate physics surface or second clock exists.
     * @return Owning frame or diagnostic; failure leaves all inputs untouched.
     * @cost Up to two sets of inverse FFTs plus linear grid blending per simulation tick.
     * @thread Reentrant const operation, private scratch, no callbacks or GPU synchronization.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<OceanSpectrumFrame> build() const;

private:
    friend class OceanSeaTransition;
    OceanSeaTransitionFrameBuilder(OceanSpectrum from, OceanSpectrum to, double start, double duration, double time)
        : from_(std::move(from)), to_(std::move(to)), start_(start), duration_(duration), time_(time) {}
    OceanSpectrum from_, to_;
    double        start_, duration_, time_;
};
}  // namespace eve::fluids
