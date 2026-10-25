#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_production_time)
#include "PrivateTestScope.h"
#include "common/Time.h"
#include "production/Production.h"

namespace test_production_time {
namespace {
class ManualTimeSource final : public eve::ITimeSource {
public:
    eve::MonotonicTimestamp monotonicNow() const override { return now; }
    eve::WallClockTimestamp wallClockNow() const override { return eve::WallClockTimestamp(1234); }
    eve::MonotonicTimestamp now;
};
}  // namespace

TEST_CASE("Production simulation clock and injected step contract") {
    SUB_CASE("Production injected fixed steps accept increasing ticks and reject duplicate tick atomically") {
        eve::production::WorkQueue queue;
        auto id = queue.enqueue("factory", "vehicle", "tank", eve::Value(eve::Value::Object{}), 1.0);
        REQUIRE(id.ok());
        REQUIRE(queue.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(400000000)}).ok());
        REQUIRE(queue.find(id.value()));
        CHECK_EQ(queue.find(id.value())->get().progress.seconds(), 0.4);
        CHECK(!queue.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(500000000)}).ok());
        CHECK_EQ(queue.currentTick().value(), std::uint64_t(1));
        CHECK_EQ(queue.find(id.value())->get().progress.seconds(), 0.4);
        REQUIRE(queue.advance({eve::SimulationTick(2), eve::Duration::fromNanoseconds(600000000)}).ok());
        CHECK(queue.find(id.value())->get().state == eve::production::TaskState::Completed);
        auto snapshot = queue.snapshot();
        REQUIRE(snapshot.ok());
        CHECK(snapshot.value().find("wallClock") == std::string::npos);
        eve::production::WorkQueue restored;
        REQUIRE(restored.restore(snapshot.value()).ok());
        CHECK_EQ(restored.currentTick().value(), std::uint64_t(2));
    };

    SUB_CASE("Production clock rate changes only supplied step count") {
        ManualTimeSource           source;
        eve::SimulationClock       clock(source, eve::Duration::fromNanoseconds(250000000));
        eve::production::WorkQueue queue;
        auto id = queue.enqueue("factory", "vehicle", "tank", eve::Value(eve::Value::Object{}), 0.5);
        REQUIRE(id.ok());
        REQUIRE(clock.setRate(0.5).ok());
        REQUIRE(clock.sample().ok());
        source.now = eve::MonotonicTimestamp(1000000000);
        auto steps = clock.sample();
        REQUIRE(steps.ok());
        REQUIRE_EQ(steps.value().size(), 2u);
        for (const auto& step : steps.value()) REQUIRE(queue.advance(step).ok());
        CHECK_EQ(queue.currentTick(), clock.currentTick());
        CHECK(queue.find(id.value())->get().state == eve::production::TaskState::Completed);
    };
}
}  // namespace test_production_time
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_production)
#include "PrivateTestScope.h"
#include "production/Production.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace test_production_unit {
using namespace eve::production;

namespace {
eve::Duration       seconds(double value) { return eve::Duration::fromSeconds(value).expect("valid test duration"); }
eve::SimulationStep step(std::uint64_t tick, double duration) { return {eve::SimulationTick(tick), seconds(duration)}; }
ProductionRequest   request(std::string owner, std::string product, double duration = 1.0) {
    ProductionRequest value;
    value.owner              = std::move(owner);
    value.kind               = "craft";
    value.product            = std::move(product);
    value.duration           = seconds(duration);
    value.settlementRequired = false;
    return value;
}
}  // namespace

TEST_CASE("Production enqueue scheduling and progress contract") {
    SUB_CASE("Production enqueue assigns stable IDs retains canonical context and rejects invalid input") {
        WorkQueue          queue;
        eve::Value::Object context;
        context.emplace("z", eve::Value(2));
        context.emplace("a", eve::Value(1));
        auto first  = queue.enqueue("factory", "vehicle", "tank", eve::Value(std::move(context)), 10.0, 2);
        auto second = queue.enqueue("factory", "vehicle", "truck", eve::Value(eve::Value::Object{}), 5.0, 1);
        REQUIRE(first.ok());
        REQUIRE(second.ok());
        CHECK_EQ(first.value(), std::string("task-0000000000000001"));
        CHECK_EQ(second.value(), std::string("task-0000000000000002"));
        REQUIRE(queue.find(first.value()));
        const auto* fields = queue.find(first.value())->get().context.getIf<eve::Value::Object>();
        REQUIRE(fields != nullptr);
        CHECK_EQ(fields->at("a").asInt(), 1);
        CHECK_EQ(queue.taskCount(), 2);
        CHECK(!queue.enqueue("", "vehicle", "tank", eve::Value(eve::Value::Object{}), 1).ok());
        CHECK(!queue.enqueue("factory", "vehicle", "tank", eve::Value("bad"), 1).ok());
        CHECK_EQ(queue.taskCount(), 2);
    };

    SUB_CASE("Production scheduler applies owner slots priority and enqueue order") {
        WorkQueue queue;
        REQUIRE(queue.setSlotCount("a", 0).ok());
        auto low       = queue.enqueue(request("a", "low"));
        auto high1     = request("a", "high1");
        high1.priority = 9;
        auto high2     = request("a", "high2");
        high2.priority = 9;
        auto a         = queue.enqueue(std::move(high1));
        auto b         = queue.enqueue(std::move(high2));
        auto other     = queue.enqueue(request("b", "other"));
        REQUIRE(low.ok());
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        REQUIRE(other.ok());
        CHECK(queue.find(other.value())->get().state == TaskState::Running);
        CHECK(queue.find(low.value())->get().state == TaskState::Queued);
        REQUIRE(queue.setSlotCount("a", 2).ok());
        CHECK(queue.find(a.value())->get().state == TaskState::Running);
        CHECK(queue.find(b.value())->get().state == TaskState::Running);
        CHECK(queue.find(low.value())->get().state == TaskState::Queued);
        CHECK_EQ(queue.runningCount("a"), 2);
    };

    SUB_CASE("Production progress waits for a free slot and completes in fixed injected durations") {
        WorkQueue queue;
        auto      first  = queue.enqueue(request("yard", "a", 4));
        auto      second = queue.enqueue(request("yard", "b", 1));
        REQUIRE(first.ok());
        REQUIRE(second.ok());
        REQUIRE(queue.advance(step(1, 1)).ok());
        CHECK_EQ(queue.find(first.value())->get().progress.seconds(), 1.0);
        CHECK_EQ(queue.find(second.value())->get().progress.seconds(), 0.0);
        REQUIRE(queue.advance(step(2, 3)).ok());
        CHECK(queue.find(first.value())->get().state == TaskState::Completed);
        CHECK(queue.find(second.value())->get().state == TaskState::Running);
        REQUIRE(queue.advance(step(3, 1)).ok());
        CHECK(queue.find(second.value())->get().state == TaskState::Completed);
        CHECK(queue.eventCount() > 0);
    };
}

TEST_CASE("Production lifecycle and settlement contract") {
    SUB_CASE("Production pause resume cancel and fail enforce terminal lifecycle") {
        WorkQueue queue;
        auto      a = queue.enqueue(request("owner", "a", 2));
        auto      b = queue.enqueue(request("owner", "b", 2));
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        REQUIRE(queue.pause(a.value()).ok());
        CHECK(queue.find(a.value())->get().state == TaskState::Paused);
        CHECK(queue.find(b.value())->get().state == TaskState::Running);
        REQUIRE(queue.resume(a.value()).ok());
        REQUIRE(queue.cancel(b.value(), "changed_plan").ok());
        CHECK_EQ(queue.find(b.value())->get().reason, std::string("changed_plan"));
        CHECK(queue.find(a.value())->get().state == TaskState::Running);
        REQUIRE(queue.fail(a.value(), "power_loss").ok());
        CHECK(queue.find(a.value())->get().state == TaskState::Failed);
        CHECK(!queue.resume(a.value()).ok());
    };

    SUB_CASE("Production explicit settlement accepts one receipt and retry preserves completed work") {
        WorkQueue queue;
        auto      req          = request("bench", "potion");
        req.settlementRequired = true;
        req.reservation        = eve::Value(eve::Value::Object{{"inventory", eve::Value("reservation-42")}});
        auto id                = queue.enqueue(std::move(req));
        REQUIRE(id.ok());
        REQUIRE(queue.advance(step(1, 1)).ok());
        CHECK(queue.find(id.value())->get().state == TaskState::ReadyToSettle);
        CHECK(queue.find(id.value())->get().reservationState == ReservationState::Consumed);
        REQUIRE(queue.failSettlement(id.value(), "output full").ok());
        CHECK(queue.find(id.value())->get().state == TaskState::SettlementFailed);
        REQUIRE(queue.retrySettlement(id.value()).ok());
        ProductionSettlementReceipt receipt;
        receipt.settlementId = "craft:42";
        receipt.payload      = eve::Value(eve::Value::Object{{"count", eve::Value(1)}});
        REQUIRE(queue.settle(id.value(), receipt).ok());
        auto repeated = queue.settle(id.value(), receipt);
        REQUIRE(repeated.ok());
        CHECK(repeated.status().code() == eve::StatusCode::NoOp);
        CHECK(queue.find(id.value())->get().state == TaskState::Completed);
    };
}

TEST_CASE("Production dependency resource and refund contract") {
    SUB_CASE("Production prerequisites gate cross-owner execution until settled") {
        WorkQueue queue;
        auto      sourceRequest          = request("forge", "ingot");
        sourceRequest.settlementRequired = true;
        auto source                      = queue.enqueue(std::move(sourceRequest));
        REQUIRE(source.ok());
        auto dependent = request("anvil", "blade");
        dependent.dependencies.prerequisites.push_back(source.value());
        auto id = queue.enqueue(std::move(dependent));
        REQUIRE(id.ok());
        CHECK(queue.find(id.value())->get().state == TaskState::Queued);
        auto reason = queue.blockReason(id.value());
        REQUIRE(reason.ok());
        CHECK_EQ(reason.value(), std::string("waiting_for:") + source.value());
        REQUIRE(queue.advance(step(1, 1)).ok());
        CHECK(queue.find(source.value())->get().state == TaskState::ReadyToSettle);
        CHECK(queue.find(id.value())->get().state == TaskState::Queued);
        ProductionSettlementReceipt receipt;
        receipt.settlementId = "ingot-1";
        REQUIRE(queue.settle(source.value(), receipt).ok());
        CHECK(queue.find(id.value())->get().state == TaskState::Running);
    };

    SUB_CASE("Production resource capacity blocks additional running work and frees on completion") {
        WorkQueue queue;
        REQUIRE(queue.setSlotCount("yard", 2).ok());
        REQUIRE(queue.setResourceCapacity("yard", "forge", 1).ok());
        auto a = request("yard", "a");
        a.resources.push_back({"forge", 1});
        auto b = request("yard", "b");
        b.resources.push_back({"forge", 1});
        auto first  = queue.enqueue(std::move(a));
        auto second = queue.enqueue(std::move(b));
        REQUIRE(first.ok());
        REQUIRE(second.ok());
        CHECK(queue.find(first.value())->get().state == TaskState::Running);
        CHECK(queue.find(second.value())->get().state == TaskState::Queued);
        REQUIRE(queue.advance(step(1, 1)).ok());
        CHECK(queue.find(first.value())->get().state == TaskState::Completed);
        CHECK(queue.find(second.value())->get().state == TaskState::Running);
    };

    SUB_CASE("Production contribution scales fixed work and cancellation releases a proportional reservation once") {
        WorkQueue queue;
        auto      req                = request("forge", "potion", 4);
        req.efficiencyPermille       = 500;
        req.batchSize                = 3;
        req.reservation              = eve::Value(eve::Value::Object{{"ingredients", eve::Value("reserved")}});
        req.termination.cancellation = RefundPolicy::Proportional;
        auto id                      = queue.enqueue(std::move(req));
        REQUIRE(id.ok());
        REQUIRE(queue.advance(step(1, 2)).ok());
        CHECK_EQ(queue.find(id.value())->get().progress.nanoseconds(), std::int64_t{1000000000});
        WorkContribution work{"crafter", seconds(1), 2000};
        REQUIRE(queue.contribute(id.value(), work).ok());
        CHECK_EQ(queue.find(id.value())->get().progress.nanoseconds(), std::int64_t{3000000000});
        REQUIRE(queue.cancel(id.value()).ok());
        CHECK_EQ(queue.find(id.value())->get().refundPermille, 250u);
        auto release = queue.releaseReservation(id.value(), "release-1");
        REQUIRE(release.ok());
        CHECK_EQ(release.value().refundPermille, 250u);
        auto repeated = queue.releaseReservation(id.value(), "release-1");
        REQUIRE(repeated.ok());
        CHECK(repeated.status().code() == eve::StatusCode::NoOp);
    };
}

TEST_CASE("Production strategy and maintain stock contract") {
    SUB_CASE("Production scheduler strategies select FIFO or shortest remaining within owner") {
        WorkQueue queue;
        REQUIRE(queue.setSlotCount("fifo", 0).ok());
        REQUIRE(queue.setSchedulerStrategy("fifo", SchedulerStrategy::Fifo).ok());
        auto old       = request("fifo", "old", 4);
        old.priority   = -10;
        auto newer     = request("fifo", "new", 1);
        newer.priority = 100;
        auto first     = queue.enqueue(std::move(old));
        auto second    = queue.enqueue(std::move(newer));
        REQUIRE(first.ok());
        REQUIRE(second.ok());
        REQUIRE(queue.setSlotCount("fifo", 1).ok());
        CHECK(queue.find(first.value())->get().state == TaskState::Running);
        CHECK(queue.find(second.value())->get().state == TaskState::Queued);

        REQUIRE(queue.setSlotCount("short", 0).ok());
        REQUIRE(queue.setSchedulerStrategy("short", SchedulerStrategy::ShortestRemaining).ok());
        auto longJob  = queue.enqueue(request("short", "long", 8));
        auto shortJob = queue.enqueue(request("short", "short", 1));
        REQUIRE(longJob.ok());
        REQUIRE(shortJob.ok());
        REQUIRE(queue.setSlotCount("short", 1).ok());
        CHECK(queue.find(shortJob.value())->get().state == TaskState::Running);
        CHECK(queue.find(longJob.value())->get().state == TaskState::Queued);
    };

    SUB_CASE("Production continuous maintain-stock task stops at target and resumes after stock report") {
        WorkQueue queue;
        auto      req                  = request("still", "tonic");
        req.batchSize                  = 2;
        req.repeat.continuous          = true;
        req.repeat.maintainStockTarget = 2;
        req.random                     = {"production.tonic", 0x12345678u, 9, 4};
        req.correlationId              = "order:tonic:42";
        auto id                        = queue.enqueue(std::move(req));
        REQUIRE(id.ok());
        auto prediction = queue.predict(id.value());
        REQUIRE(prediction.ok());
        CHECK(prediction.value().exact);
        CHECK_EQ(prediction.value().estimatedCompletionAfter.nanoseconds(), std::int64_t{1000000000});
        REQUIRE(queue.advance(step(1, 1)).ok());
        CHECK_EQ(queue.find(id.value())->get().completedCycles, 1u);
        CHECK(queue.find(id.value())->get().state == TaskState::Queued);
        CHECK_EQ(queue.blockReason(id.value()).value(), std::string("stock_target_satisfied"));
        auto diagnostic = queue.diagnose(id.value());
        REQUIRE(diagnostic.ok());
        CHECK_EQ(diagnostic.value().correlationId, std::string("order:tonic:42"));
        REQUIRE(queue.reportStock(id.value(), 0).ok());
        CHECK(queue.find(id.value())->get().state == TaskState::Running);
    };
}

TEST_CASE("Production fixed point and overflow contract") {
    SUB_CASE("Production fixed-point remainder survives snapshot and eventually advances a nanosecond") {
        WorkQueue queue;
        auto      req          = request("micro", "part");
        req.duration           = eve::Duration::fromNanoseconds(2);
        req.efficiencyPermille = 1;
        auto id                = queue.enqueue(std::move(req));
        REQUIRE(id.ok());
        for (std::uint64_t tick = 1; tick <= 500; ++tick)
            REQUIRE(queue.advance({eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}).ok());
        CHECK_EQ(queue.find(id.value())->get().progress.nanoseconds(), std::int64_t{0});
        CHECK_EQ(queue.find(id.value())->get().workRemainderPermille, 500u);
        auto snapshot = queue.snapshot();
        REQUIRE(snapshot.ok());
        WorkQueue restored;
        REQUIRE(restored.restore(snapshot.value()).ok());
        for (std::uint64_t tick = 501; tick <= 1000; ++tick)
            REQUIRE(restored.advance({eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}).ok());
        CHECK_EQ(restored.find(id.value())->get().progress.nanoseconds(), std::int64_t{1});
        CHECK_EQ(restored.find(id.value())->get().workRemainderPermille, 0u);
    };

    SUB_CASE("Production work overflow leaves tick and all running task progress unchanged") {
        WorkQueue queue;
        REQUIRE(queue.setSlotCount("factory", 2).ok());
        auto safe                   = request("factory", "safe");
        safe.duration               = eve::Duration::fromNanoseconds(std::numeric_limits<std::int64_t>::max());
        safe.efficiencyPermille     = 1;
        auto overflow               = request("factory", "overflow");
        overflow.duration           = safe.duration;
        overflow.efficiencyPermille = std::numeric_limits<std::uint32_t>::max();
        auto a                      = queue.enqueue(std::move(safe));
        auto b                      = queue.enqueue(std::move(overflow));
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        CHECK(!queue
                   .advance({eve::SimulationTick(1),
                             eve::Duration::fromNanoseconds(std::numeric_limits<std::int64_t>::max())})
                   .ok());
        CHECK_EQ(queue.currentTick(), eve::SimulationTick::zero());
        CHECK_EQ(queue.find(a.value())->get().progress, eve::Duration::zero());
        CHECK_EQ(queue.find(b.value())->get().progress, eve::Duration::zero());
    };
}

TEST_CASE("Production persistence and reset contract") {
    SUB_CASE("Production snapshot restores exact queue state and malformed restore is atomic") {
        WorkQueue original;
        REQUIRE(original.setSlotCount("yard", 2).ok());
        auto id = original.enqueue(request("yard", "tank", 5));
        REQUIRE(id.ok());
        REQUIRE(original.advance(step(1, 1.25)).ok());
        auto snapshot = original.snapshot();
        REQUIRE(snapshot.ok());
        WorkQueue restored;
        REQUIRE(restored.restore(snapshot.value()).ok());
        auto duplicate = restored.snapshot();
        REQUIRE(duplicate.ok());
        CHECK_EQ(duplicate.value(), snapshot.value());
        CHECK_EQ(restored.find(id.value())->get().progress.seconds(), 1.25);
        CHECK(!restored.restore("{\"broken\":true}").ok());
        auto afterFailure = restored.snapshot();
        REQUIRE(afterFailure.ok());
        CHECK_EQ(afterFailure.value(), snapshot.value());
    };

    SUB_CASE("Production clear drops retained tasks events owner config and resets counters") {
        WorkQueue queue;
        REQUIRE(queue.setSlotCount("owner", 3).ok());
        auto id = queue.enqueue(request("owner", "a"));
        REQUIRE(id.ok());
        queue.clear();
        CHECK_EQ(queue.taskCount(), 0);
        CHECK_EQ(queue.eventCount(), 0);
        CHECK_EQ(queue.slotCount("owner"), 1);
        auto next = queue.enqueue(request("owner", "b"));
        REQUIRE(next.ok());
        CHECK_EQ(next.value(), std::string("task-0000000000000001"));
    };
}
}  // namespace test_production_unit
#endif
