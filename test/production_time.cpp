#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Time.h"
#include "production/Production.h"

using eve::production::WorkQueue;

namespace {

class ManualTimeSource final : public eve::ITimeSource {
public:
    eve::MonotonicTimestamp monotonicNow() const override { return now; }
    eve::WallClockTimestamp wallClockNow() const override { return eve::WallClockTimestamp(1234); }
    eve::MonotonicTimestamp now;
};

}  // namespace

TEST_CASE("production.injectedStepsPauseAndRestoreTick") {
    WorkQueue queue;
    auto idResult = queue.enqueue("factory", "vehicle", "tank",
                                   eve::Value(eve::Value::Object{}), 1.0);
    REQUIRE(idResult.ok());
    const auto id = std::move(idResult).takeValue();

    auto first = queue.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(400000000)});
    REQUIRE(first.ok());
    auto firstTask = queue.find(id);
    REQUIRE(firstTask);
    CHECK_EQ(firstTask->get().progress.seconds(), 0.4);
    CHECK_EQ(queue.currentTick().value(), std::uint64_t(1));

    auto duplicate = queue.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(400000000)});
    CHECK(!duplicate.ok());
    firstTask = queue.find(id);
    REQUIRE(firstTask);
    CHECK_EQ(firstTask->get().progress.seconds(), 0.4);

    auto second = queue.advance({eve::SimulationTick(2), eve::Duration::fromNanoseconds(600000000)});
    REQUIRE(second.ok());
    firstTask = queue.find(id);
    REQUIRE(firstTask);
    CHECK_EQ(firstTask->get().progress.seconds(), 1.0);
    CHECK(firstTask->get().state == eve::production::TaskState::Completed);

    auto savedResult = queue.snapshot();
    REQUIRE(savedResult.ok());
    const auto saved = std::move(savedResult).takeValue();
    CHECK(saved.find("\"tick\":\"2\"") != std::string::npos);
    CHECK(saved.find("wallClock") == std::string::npos);

    WorkQueue restored;
    auto restoredResult = restored.restore(saved);
    REQUIRE(restoredResult.ok());
    CHECK_EQ(restored.currentTick().value(), std::uint64_t(2));
    auto afterRestore = restored.advance({eve::SimulationTick(3), eve::Duration::fromNanoseconds(1)});
    REQUIRE(afterRestore.ok());
    CHECK_EQ(restored.currentTick().value(), std::uint64_t(3));
}

TEST_CASE("production.advanceFacadeAdvancesDeterministicTick") {
    WorkQueue queue;
    auto idResult = queue.enqueue("factory", "vehicle", "tank",
                                  eve::Value(eve::Value::Object{}), 1.0);
    REQUIRE(idResult.ok());
    const auto id = std::move(idResult).takeValue();
    auto step = queue.advance(
        {eve::SimulationTick(1), eve::Duration::fromNanoseconds(125000000)});
    REQUIRE(step.ok());
    CHECK_EQ(queue.currentTick().value(), std::uint64_t(1));
    auto task = queue.find(id);
    REQUIRE(task);
    CHECK_EQ(task->get().progress.seconds(), 0.125);
    auto saved = queue.snapshot();
    REQUIRE(saved.ok());
    CHECK(std::move(saved).takeValue().find("wallClock") == std::string::npos);
}

TEST_CASE("production.clockStepsAreTheOnlySimulationInput") {
    ManualTimeSource source;
    eve::SimulationClock clock(source, eve::Duration::fromNanoseconds(250000000));
    WorkQueue queue;
    auto idResult = queue.enqueue("factory", "vehicle", "tank",
                                  eve::Value(eve::Value::Object{}), 0.5);
    REQUIRE(idResult.ok());
    const auto id = std::move(idResult).takeValue();
    REQUIRE(clock.setRate(0.5).ok());
    REQUIRE(clock.sample().ok());

    source.now = eve::MonotonicTimestamp(1000000000);
    auto steps = clock.sample();
    REQUIRE(steps.ok());
    CHECK_EQ(steps.value().size(), std::size_t(2));
    for (const auto& step : steps.value()) {
        auto applied = queue.advance(step);
        REQUIRE(applied.ok());
    }
    auto task = queue.find(id);
    REQUIRE(task);
    CHECK(task->get().state == eve::production::TaskState::Completed);
    CHECK_EQ(queue.currentTick(), clock.currentTick());
}
