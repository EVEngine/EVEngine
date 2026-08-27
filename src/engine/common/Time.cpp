#include "common/Time.h"

#include <cmath>
#include <limits>
#include <utility>

namespace eve {
namespace {

Diagnostic invalidTime(std::string message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message));
}

}  // namespace

Result<Duration> Duration::fromSeconds(double seconds) {
    if (!std::isfinite(seconds))
        return Result<Duration>::failure(invalidTime("duration seconds must be finite"));

    const long double nanoseconds = static_cast<long double>(seconds) * 1000000000.0L;
    constexpr long double minimum = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
    constexpr long double maximum = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    if (nanoseconds < minimum || nanoseconds > maximum)
        return Result<Duration>::failure(invalidTime("duration seconds are out of range"));

    return Result<Duration>::success(Duration(static_cast<std::int64_t>(std::round(nanoseconds))));
}

double Duration::seconds() const noexcept {
    return static_cast<double>(nanoseconds_) / 1000000000.0;
}

Result<Duration> Duration::tryAdd(Duration other) const {
    if ((other.nanoseconds_ > 0 && nanoseconds_ > std::numeric_limits<std::int64_t>::max() - other.nanoseconds_) ||
        (other.nanoseconds_ < 0 && nanoseconds_ < std::numeric_limits<std::int64_t>::min() - other.nanoseconds_))
        return Result<Duration>::failure(invalidTime("duration addition overflow"));
    return Result<Duration>::success(Duration(nanoseconds_ + other.nanoseconds_));
}

Result<Duration> Duration::scaled(double rate) const {
    if (!std::isfinite(rate) || rate < 0.0)
        return Result<Duration>::failure(invalidTime("duration rate must be finite and non-negative"));
    const long double nanoseconds = static_cast<long double>(nanoseconds_) * static_cast<long double>(rate);
    constexpr long double minimum = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
    constexpr long double maximum = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    if (nanoseconds < minimum || nanoseconds > maximum)
        return Result<Duration>::failure(invalidTime("scaled duration is out of range"));
    return Result<Duration>::success(Duration(static_cast<std::int64_t>(std::round(nanoseconds))));
}

Result<Duration> MonotonicTimestamp::since(MonotonicTimestamp earlier) const {
    if (nanoseconds_ < earlier.nanoseconds_)
        return Result<Duration>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "monotonic source moved backwards"));

    // Subtract in the unsigned domain after the ordering check.  The modulo
    // subtraction is the exact distance for two ordered int64 values, while
    // the explicit bound check prevents converting a distance larger than the
    // Duration domain back to int64_t.
    const std::uint64_t distance = static_cast<std::uint64_t>(nanoseconds_) -
                                   static_cast<std::uint64_t>(earlier.nanoseconds_);
    if (distance > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return Result<Duration>::failure(Diagnostic::error(
            DiagnosticCode::InvariantViolation, "monotonic timestamp distance exceeds Duration range"));
    return Result<Duration>::success(Duration(static_cast<std::int64_t>(distance)));
}

SimulationClock::SimulationClock(ITimeSource& source, Duration fixedStep)
    : source_(source), fixedStep_(fixedStep) {
    EV_ASSERT(fixedStep_.nanoseconds() > 0, "SimulationClock fixed step must be positive");
    if (fixedStep_.nanoseconds() <= 0) fixedStep_ = Duration::fromNanoseconds(16666667);
}

Result<void> SimulationClock::setFixedStep(Duration fixedStep) {
    if (fixedStep.nanoseconds() <= 0)
        return Result<void>::failure(invalidTime("fixed simulation step must be positive"));
    fixedStep_ = fixedStep;
    return Result<void>::success();
}

Result<void> SimulationClock::setRate(double rate) {
    if (!std::isfinite(rate) || rate < 0.0)
        return Result<void>::failure(invalidTime("simulation rate must be finite and non-negative"));
    rate_ = rate;
    return Result<void>::success();
}

Result<std::vector<SimulationStep>> SimulationClock::advance(Duration realDelta) {
    if (realDelta.nanoseconds() < 0)
        return Result<std::vector<SimulationStep>>::failure(
            invalidTime("simulation frame duration must be non-negative"));

    auto nextFrame = frame_.incremented();
    if (!nextFrame)
        return Result<std::vector<SimulationStep>>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "simulation frame index overflow"));

    auto scaled = realDelta.scaled(rate_);
    if (!scaled) return Result<std::vector<SimulationStep>>::failure(scaled.status());
    const Duration scaledDelta = std::move(scaled).takeValue();

    if (paused_) {
        frame_ = *nextFrame;
        lastFrameDelta_ = realDelta;
        return Result<std::vector<SimulationStep>>::success({});
    }

    auto total = accumulator_.tryAdd(scaledDelta);
    if (!total) return Result<std::vector<SimulationStep>>::failure(total.status());
    const std::int64_t accumulated = std::move(total).takeValue().nanoseconds();
    const std::uint64_t accumulatedUnsigned = static_cast<std::uint64_t>(accumulated);
    const std::uint64_t fixedStepUnsigned = static_cast<std::uint64_t>(fixedStep_.nanoseconds());
    const std::uint64_t stepCount = accumulatedUnsigned / fixedStepUnsigned;
    if (stepCount > std::numeric_limits<std::size_t>::max() ||
        stepCount > std::numeric_limits<std::uint64_t>::max() - tick_.value())
        return Result<std::vector<SimulationStep>>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "simulation step count overflow"));

    // Avoid an unbounded allocation when a stalled frame or a malicious
    // replay requests more catch-up steps than one result can safely own.
    constexpr std::uint64_t maxStepsPerAdvance = 1000000;
    if (stepCount > maxStepsPerAdvance)
        return Result<std::vector<SimulationStep>>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "simulation catch-up step limit exceeded"));
    if (stepCount > std::numeric_limits<std::uint64_t>::max() / fixedStepUnsigned)
        return Result<std::vector<SimulationStep>>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "simulation duration multiplication overflow"));
    const std::uint64_t consumedUnsigned = stepCount * fixedStepUnsigned;
    const std::uint64_t remainderUnsigned = accumulatedUnsigned - consumedUnsigned;
    if (remainderUnsigned > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return Result<std::vector<SimulationStep>>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "simulation remainder exceeds Duration range"));

    std::vector<SimulationStep> steps;
    steps.reserve(static_cast<std::size_t>(stepCount));
    for (std::uint64_t i = 0; i < stepCount; ++i)
        steps.push_back({SimulationTick(tick_.value() + i + 1), fixedStep_});

    tick_ = SimulationTick(tick_.value() + stepCount);
    accumulator_ = Duration(static_cast<std::int64_t>(remainderUnsigned));
    frame_ = *nextFrame;
    lastFrameDelta_ = realDelta;
    return Result<std::vector<SimulationStep>>::success(std::move(steps));
}

Result<std::vector<SimulationStep>> SimulationClock::sample() {
    const MonotonicTimestamp now = source_.monotonicNow();
    if (!previousSourceTime_) {
        auto first = advance(Duration::zero());
        if (!first) return first;
        previousSourceTime_ = now;
        return first;
    }

    auto elapsed = now.since(*previousSourceTime_);
    if (!elapsed) return Result<std::vector<SimulationStep>>::failure(elapsed.status());
    Duration delta = std::move(elapsed).takeValue();
    auto result = advance(delta);
    if (!result) return result;
    previousSourceTime_ = now;
    return result;
}

void SimulationClock::reset(SimulationTick tick) noexcept {
    tick_ = tick;
    frame_ = FrameIndex::zero();
    accumulator_ = Duration::zero();
    lastFrameDelta_ = Duration::zero();
    previousSourceTime_.reset();
}

}  // namespace eve
