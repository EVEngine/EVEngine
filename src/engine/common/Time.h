#pragma once

/**
 * @file Time.h
 * @brief Common time values and an injectable deterministic simulation clock.
 *
 * Simulation code consumes SimulationTick and Duration only.  Monotonic and
 * wall-clock timestamps belong to the time-source boundary; a wall-clock
 * timestamp is metadata and must never be used as simulation state.
 */

#include "common/Export.h"
#include "common/Result.h"
#include "common/StrongUint64.h"

#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace eve {

namespace detail {
struct SimulationTickTag {};
struct FrameIndexTag {};
}  // namespace detail

/** @brief Deterministic simulation time step; it is not wall-clock time. */
using SimulationTick = detail::StrongUint64<detail::SimulationTickTag>;

/** @brief Render/presentation frame ordinal; it is not a simulation tick. */
using FrameIndex = detail::StrongUint64<detail::FrameIndexTag>;

/**
 * @brief Signed, fixed-resolution simulation duration in nanoseconds.
 *
 * The integer representation avoids accumulating binary floating-point frame
 * error in deterministic state.  Conversion from seconds validates finiteness
 * and range and returns a checked Result.
 */
class EVENGINE_API Duration {
public:
    /** @brief Construct a zero duration. */
    constexpr Duration() noexcept = default;

    /**
     * @brief Construct a duration from exact nanoseconds.
     * @param nanoseconds Signed nanosecond value in this duration domain.
     */
    explicit constexpr Duration(std::int64_t nanoseconds) noexcept : nanoseconds_(nanoseconds) {}

    /** @brief Construct an exact duration from nanoseconds. */
    [[nodiscard]] static constexpr Duration fromNanoseconds(std::int64_t nanoseconds) noexcept {
        return Duration(nanoseconds);
    }

    /** @brief Return zero duration. */
    [[nodiscard]] static constexpr Duration zero() noexcept { return {}; }

    /**
     * @brief Convert finite seconds to the nearest nanosecond.
     * @return A checked duration, or Rejected for non-finite/out-of-range input.
     */
    [[nodiscard]] static Result<Duration> fromSeconds(double seconds);

    /** @brief Return the exact signed nanosecond representation. */
    [[nodiscard]] constexpr std::int64_t nanoseconds() const noexcept { return nanoseconds_; }

    /** @brief Return this duration as seconds for legacy/presentation APIs. */
    [[nodiscard]] double seconds() const noexcept;

    /** @brief Whether this duration is exactly zero. */
    [[nodiscard]] constexpr bool isZero() const noexcept { return nanoseconds_ == 0; }

    /**
     * @brief Add two durations with overflow validation.
     * @return A checked sum, or Rejected if the signed range would overflow.
     */
    [[nodiscard]] Result<Duration> tryAdd(Duration other) const;

    /**
     * @brief Scale a duration by a finite non-negative rate.
     * @return A checked rounded duration, or Rejected for invalid/range input.
     */
    [[nodiscard]] Result<Duration> scaled(double rate) const;

    friend constexpr bool operator==(Duration, Duration) noexcept = default;
    friend constexpr auto operator<=>(Duration, Duration) noexcept = default;

private:
    std::int64_t nanoseconds_ = 0;
};

/**
 * @brief Timestamp from a monotonic source.
 *
 * Its epoch is local to the source and must not be persisted or compared with
 * a timestamp produced by another source.  It is appropriate for measuring
 * elapsed real time at the scheduler boundary.
 */
class EVENGINE_API MonotonicTimestamp {
public:
    /** @brief Construct the zero timestamp. */
    constexpr MonotonicTimestamp() noexcept = default;

    /** @brief Construct a timestamp from source-relative nanoseconds. */
    explicit constexpr MonotonicTimestamp(std::int64_t nanoseconds) noexcept : nanoseconds_(nanoseconds) {}

    /** @brief Return the zero timestamp. */
    [[nodiscard]] static constexpr MonotonicTimestamp zero() noexcept { return {}; }

    /** @brief Return source-relative nanoseconds. */
    [[nodiscard]] constexpr std::int64_t nanoseconds() const noexcept { return nanoseconds_; }

    /**
     * @brief Compute elapsed duration from an earlier timestamp.
     * @return A checked non-negative duration; backward movement is rejected.
     */
    [[nodiscard]] Result<Duration> since(MonotonicTimestamp earlier) const;

    friend constexpr bool operator==(MonotonicTimestamp, MonotonicTimestamp) noexcept = default;
    friend constexpr auto operator<=>(MonotonicTimestamp, MonotonicTimestamp) noexcept = default;

private:
    std::int64_t nanoseconds_ = 0;
};

/**
 * @brief Wall-clock timestamp used only for metadata and diagnostics.
 *
 * The value is nanoseconds since the Unix epoch.  It must not appear in
 * deterministic simulation state, replay keys, or simulation hashes.
 */
class EVENGINE_API WallClockTimestamp {
public:
    /** @brief Construct the Unix epoch timestamp. */
    constexpr WallClockTimestamp() noexcept = default;

    /** @brief Construct a timestamp from Unix-epoch nanoseconds. */
    explicit constexpr WallClockTimestamp(std::int64_t unixNanoseconds) noexcept
        : unixNanoseconds_(unixNanoseconds) {}

    /** @brief Return Unix-epoch nanoseconds for metadata serialization. */
    [[nodiscard]] constexpr std::int64_t unixNanoseconds() const noexcept { return unixNanoseconds_; }

    friend constexpr bool operator==(WallClockTimestamp, WallClockTimestamp) noexcept = default;
    friend constexpr auto operator<=>(WallClockTimestamp, WallClockTimestamp) noexcept = default;

private:
    std::int64_t unixNanoseconds_ = 0;
};

/** @brief One deterministic fixed-step emitted by SimulationClock. */
struct EVENGINE_API SimulationStep {
    /** @brief Tick reached after this step is applied. */
    SimulationTick tick = SimulationTick::zero();
    /** @brief Fixed simulation duration for this step. */
    Duration delta = Duration::zero();
};

/**
 * @brief Injectable source for elapsed and metadata time.
 *
 * Implementations must document their thread affinity.  The default engine
 * Timer is owner-thread-only; callers must not invoke it concurrently.  This
 * interface never advances simulation state and never exposes a wall clock to
 * simulation consumers.
 */
class EVENGINE_API ITimeSource {
public:
    static constexpr const char *capabilityName = "ITimeSource";
    virtual ~ITimeSource() = default;

    /**
     * @brief Read the non-decreasing source-relative timestamp.
     * @return Monotonic timestamp; must not be used as persisted state.
     */
    [[nodiscard]] virtual MonotonicTimestamp monotonicNow() const = 0;

    /**
     * @brief Read wall-clock metadata.
     * @return Unix timestamp; never use this value for simulation progression.
     */
    [[nodiscard]] virtual WallClockTimestamp wallClockNow() const = 0;
};

/**
 * @brief Owner-thread fixed-step scheduler driven by an injected time source.
 *
 * `sample()` reads only monotonic time.  `advance()` is the replay/server path
 * and accepts a supplied Duration, so tests and deterministic replays do not
 * need a real clock.  Paused clocks do not accumulate catch-up time.  A slow
 * rate scales accumulated simulation time while emitted steps remain fixed.
 * The scheduler is not thread-safe; one owner thread must serialize all calls.
 */
class EVENGINE_API SimulationClock {
public:
    /**
     * @brief Construct a fixed-step clock without reading the source.
     * @param source Borrowed source; it must outlive this clock.
     * @param fixedStep Positive fixed simulation duration.
     */
    explicit SimulationClock(ITimeSource& source,
                             Duration fixedStep = Duration::fromNanoseconds(16666667));

    /** @brief Read the source and emit deterministic steps for one frame. */
    [[nodiscard]] Result<std::vector<SimulationStep>> sample();

    /**
     * @brief Advance from supplied monotonic elapsed duration without reading a clock.
     * @param realDelta Non-negative elapsed frame duration.
     * @return Fixed steps; failure leaves scheduler state unchanged.
     */
    [[nodiscard]] Result<std::vector<SimulationStep>> advance(Duration realDelta);

    /** @brief Set the positive fixed-step duration; accumulated time is preserved. */
    [[nodiscard]] Result<void> setFixedStep(Duration fixedStep);
    /** @brief Set a finite non-negative simulation rate (1 is normal speed). */
    [[nodiscard]] Result<void> setRate(double rate);
    /** @brief Pause or resume simulation accumulation. */
    void setPaused(bool paused) noexcept { paused_ = paused; }

    /** @brief Reset deterministic state to a supplied tick without reading a clock. */
    void reset(SimulationTick tick = SimulationTick::zero()) noexcept;

    /** @brief Current deterministic simulation tick. */
    [[nodiscard]] SimulationTick currentTick() const noexcept { return tick_; }
    /** @brief Current presentation frame ordinal. */
    [[nodiscard]] FrameIndex frameIndex() const noexcept { return frame_; }
    /** @brief Fixed duration of emitted simulation steps. */
    [[nodiscard]] Duration fixedStep() const noexcept { return fixedStep_; }
    /** @brief Current slow-motion rate. */
    [[nodiscard]] double rate() const noexcept { return rate_; }
    /** @brief Whether simulation accumulation is paused. */
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    /** @brief Real source duration observed during the last frame. */
    [[nodiscard]] Duration lastFrameDelta() const noexcept { return lastFrameDelta_; }

private:
    ITimeSource& source_;
    Duration fixedStep_;
    Duration accumulator_;
    Duration lastFrameDelta_;
    double rate_ = 1.0;
    SimulationTick tick_ = SimulationTick::zero();
    FrameIndex frame_ = FrameIndex::zero();
    bool paused_ = false;
    std::optional<MonotonicTimestamp> previousSourceTime_;
};

}  // namespace eve
