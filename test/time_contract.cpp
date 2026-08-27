#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>

#include "common/Time.h"

namespace {

class ManualTimeSource final : public eve::ITimeSource {
public:
    eve::MonotonicTimestamp monotonicNow() const override { return monotonic_; }
    eve::WallClockTimestamp wallClockNow() const override { return wallClock_; }

    void advance(std::int64_t nanoseconds) {
        monotonic_ = eve::MonotonicTimestamp(monotonic_.nanoseconds() + nanoseconds);
    }

    void setMonotonic(std::int64_t nanoseconds) { monotonic_ = eve::MonotonicTimestamp(nanoseconds); }
    void setWallClock(std::int64_t nanoseconds) { wallClock_ = eve::WallClockTimestamp(nanoseconds); }

private:
    eve::MonotonicTimestamp monotonic_;
    eve::WallClockTimestamp wallClock_;
};

}  // namespace

TEST_CASE("time.durationAndTimestampUseStrongFixedUnits") {
    auto duration = eve::Duration::fromSeconds(1.25);
    REQUIRE(duration.ok());
    CHECK_EQ(duration.value().nanoseconds(), std::int64_t(1250000000));
    CHECK_EQ(duration.value().seconds(), 1.25);

    eve::MonotonicTimestamp later(150);
    auto elapsed = later.since(eve::MonotonicTimestamp(50));
    REQUIRE(elapsed.ok());
    CHECK_EQ(elapsed.value().nanoseconds(), std::int64_t(100));

    auto backwards = eve::MonotonicTimestamp(10).since(eve::MonotonicTimestamp(11));
    CHECK(!backwards.ok());

    auto maximum = eve::MonotonicTimestamp(std::numeric_limits<std::int64_t>::max())
                       .since(eve::MonotonicTimestamp::zero());
    REQUIRE(maximum.ok());
    CHECK_EQ(maximum.value().nanoseconds(), std::numeric_limits<std::int64_t>::max());

    auto tooWide = eve::MonotonicTimestamp(std::numeric_limits<std::int64_t>::max())
                       .since(eve::MonotonicTimestamp(std::numeric_limits<std::int64_t>::min()));
    CHECK(!tooWide.ok());
    CHECK_EQ(tooWide.code(), eve::StatusCode::Failed);
    REQUIRE(tooWide.error() != nullptr);
    CHECK_EQ(tooWide.error()->code(), eve::DiagnosticCode::InvariantViolation);
}

TEST_CASE("time.simulationClock.fixedStepPauseAndSlowMotion") {
    ManualTimeSource source;
    eve::SimulationClock clock(source, eve::Duration::fromNanoseconds(100));

    auto first = clock.sample();
    REQUIRE(first.ok());
    CHECK(first.value().empty());
    CHECK_EQ(clock.currentTick().value(), std::uint64_t(0));
    CHECK_EQ(clock.frameIndex().value(), std::uint64_t(1));

    source.advance(350);
    auto steps = clock.sample();
    REQUIRE(steps.ok());
    CHECK_EQ(steps.value().size(), std::size_t(3));
    CHECK_EQ(steps.value()[0].tick.value(), std::uint64_t(1));
    CHECK_EQ(steps.value()[2].tick.value(), std::uint64_t(3));
    CHECK_EQ(steps.value()[0].delta.nanoseconds(), std::int64_t(100));

    clock.setPaused(true);
    source.advance(1000);
    auto paused = clock.sample();
    REQUIRE(paused.ok());
    CHECK(paused.value().empty());
    CHECK_EQ(clock.currentTick().value(), std::uint64_t(3));

    clock.setPaused(false);
    source.advance(100);
    auto resumed = clock.sample();
    REQUIRE(resumed.ok());
    CHECK_EQ(resumed.value().size(), std::size_t(1));
    CHECK_EQ(resumed.value()[0].tick.value(), std::uint64_t(4));

    clock.reset();
    auto rate = clock.setRate(0.5);
    REQUIRE(rate.ok());
    auto rateStart = clock.sample();
    REQUIRE(rateStart.ok());
    source.advance(250);
    auto slow = clock.sample();
    REQUIRE(slow.ok());
    CHECK_EQ(slow.value().size(), std::size_t(1));
    CHECK_EQ(clock.currentTick().value(), std::uint64_t(1));

    auto badRate = clock.setRate(-1.0);
    CHECK(!badRate.ok());
}

TEST_CASE("time.clockUsesMonotonicOnlyAndWallClockIsMetadata") {
    ManualTimeSource firstSource;
    ManualTimeSource secondSource;
    firstSource.setWallClock(1000);
    secondSource.setWallClock(9000000);
    eve::SimulationClock first(firstSource, eve::Duration::fromNanoseconds(100));
    eve::SimulationClock second(secondSource, eve::Duration::fromNanoseconds(100));

    REQUIRE(first.sample().ok());
    REQUIRE(second.sample().ok());
    firstSource.advance(250);
    secondSource.advance(250);
    auto firstSteps = first.sample();
    auto secondSteps = second.sample();
    REQUIRE(firstSteps.ok());
    REQUIRE(secondSteps.ok());
    CHECK_EQ(firstSteps.value().size(), secondSteps.value().size());
    CHECK_EQ(first.currentTick(), second.currentTick());
    CHECK_EQ(firstSource.wallClockNow().unixNanoseconds(), std::int64_t(1000));
    CHECK_EQ(secondSource.wallClockNow().unixNanoseconds(), std::int64_t(9000000));
}

TEST_CASE("time.clockRejectsBackwardSourceWithoutStateMutation") {
    ManualTimeSource source;
    eve::SimulationClock clock(source, eve::Duration::fromNanoseconds(100));
    REQUIRE(clock.sample().ok());
    source.advance(100);
    auto accepted = clock.sample();
    REQUIRE(accepted.ok());
    const auto tick = clock.currentTick();
    const auto frame = clock.frameIndex();

    source.advance(-200);
    auto rejected = clock.sample();
    CHECK(!rejected.ok());
    CHECK_EQ(clock.currentTick(), tick);
    CHECK_EQ(clock.frameIndex(), frame);
}

TEST_CASE("time.clockRejectsTimestampDistanceOutsideDurationWithoutStateMutation") {
    ManualTimeSource source;
    source.setMonotonic(std::numeric_limits<std::int64_t>::min());
    eve::SimulationClock clock(source, eve::Duration::fromNanoseconds(100));
    REQUIRE(clock.sample().ok());
    const auto tick = clock.currentTick();
    const auto frame = clock.frameIndex();

    source.setMonotonic(std::numeric_limits<std::int64_t>::max());
    auto rejected = clock.sample();
    CHECK(!rejected.ok());
    REQUIRE(rejected.error() != nullptr);
    CHECK_EQ(rejected.error()->code(), eve::DiagnosticCode::InvariantViolation);
    CHECK_EQ(clock.currentTick(), tick);
    CHECK_EQ(clock.frameIndex(), frame);
}

TEST_CASE("time.clockRejectsExtremeCatchUpWithoutMultiplicationOverflow") {
    ManualTimeSource source;
    eve::SimulationClock clock(source, eve::Duration::fromNanoseconds(1));
    const auto beforeTick = clock.currentTick();
    const auto beforeFrame = clock.frameIndex();

    auto extreme = clock.advance(eve::Duration::fromNanoseconds(std::numeric_limits<std::int64_t>::max()));
    CHECK(!extreme.ok());
    CHECK_EQ(extreme.code(), eve::StatusCode::Failed);
    REQUIRE(extreme.error() != nullptr);
    CHECK_EQ(extreme.error()->code(), eve::DiagnosticCode::InvariantViolation);
    CHECK_EQ(clock.currentTick(), beforeTick);
    CHECK_EQ(clock.frameIndex(), beforeFrame);

    eve::SimulationClock boundary(source,
                                   eve::Duration::fromNanoseconds(std::numeric_limits<std::int64_t>::max()));
    auto oneStep = boundary.advance(eve::Duration::fromNanoseconds(std::numeric_limits<std::int64_t>::max()));
    REQUIRE(oneStep.ok());
    CHECK_EQ(oneStep.value().size(), std::size_t(1));
    CHECK_EQ(boundary.currentTick().value(), std::uint64_t(1));
}
