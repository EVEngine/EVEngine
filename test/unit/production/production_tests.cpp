#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_production_time)
#include "PrivateTestScope.h"
#include "common/Time.h"
#include "production/Production.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// production_time.cpp
namespace test_production_time {
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
    auto      idResult = queue.enqueue("factory", "vehicle", "tank", eve::Value(eve::Value::Object{}), 1.0);
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
    auto      restoredResult = restored.restore(saved);
    REQUIRE(restoredResult.ok());
    CHECK_EQ(restored.currentTick().value(), std::uint64_t(2));
    auto afterRestore = restored.advance({eve::SimulationTick(3), eve::Duration::fromNanoseconds(1)});
    REQUIRE(afterRestore.ok());
    CHECK_EQ(restored.currentTick().value(), std::uint64_t(3));
}

TEST_CASE("production.advanceFacadeAdvancesDeterministicTick") {
    WorkQueue queue;
    auto      idResult = queue.enqueue("factory", "vehicle", "tank", eve::Value(eve::Value::Object{}), 1.0);
    REQUIRE(idResult.ok());
    const auto id   = std::move(idResult).takeValue();
    auto       step = queue.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(125000000)});
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
    ManualTimeSource     source;
    eve::SimulationClock clock(source, eve::Duration::fromNanoseconds(250000000));
    WorkQueue            queue;
    auto                 idResult = queue.enqueue("factory", "vehicle", "tank", eve::Value(eve::Value::Object{}), 0.5);
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
}  // namespace test_production_time
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_production)
#include "PrivateTestScope.h"
#include <cstdint>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "production/Production.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// production.cpp
namespace test_production_unit {
using namespace eve::production;

TEST_CASE("production.enqueue.stableIdsAndCanonicalContext") {
    WorkQueue          queue;
    eve::Value::Object context;
    context.emplace("z", eve::Value(2));
    context.emplace("a", eve::Value(1));
    auto firstResult = queue.enqueue("factory:1", "vehicle", "tank.medium", eve::Value(std::move(context)), 10.0, 2);
    REQUIRE(firstResult.ok());
    const auto first        = std::move(firstResult).takeValue();
    auto       secondResult = queue.enqueue("factory:1", "vehicle", "truck", eve::Value(eve::Value::Object{}), 5.0, 1);
    REQUIRE(secondResult.ok());
    const auto second = std::move(secondResult).takeValue();
    CHECK_EQ(first, std::string("task-0000000000000001"));
    CHECK_EQ(second, std::string("task-0000000000000002"));
    auto firstTask = queue.find(first);
    REQUIRE(firstTask);
    const auto* firstContext = firstTask->get().context.getIf<eve::Value::Object>();
    REQUIRE(firstContext != nullptr);
    CHECK_EQ(firstContext->at("a").asInt(), 1);
    CHECK_EQ(firstContext->at("z").asInt(), 2);
    CHECK_EQ(firstTask->get().state == TaskState::Running, true);
    auto secondTask = queue.find(second);
    REQUIRE(secondTask);
    CHECK_EQ(secondTask->get().state == TaskState::Queued, true);
    auto emptyOwner = queue.enqueue("", "x", "y", eve::Value(eve::Value::Object{}), 1.0);
    CHECK(!emptyOwner.ok());
    auto nonObjectContext = queue.enqueue("owner", "x", "y", eve::Value("bad"), 1.0);
    CHECK(!nonObjectContext.ok());
}

TEST_CASE("production.scheduling.prioritySlotsAndOwnersAreDeterministic") {
    WorkQueue queue;
    auto      slotsDisabled = queue.setSlotCount("base:a", 0);
    REQUIRE(slotsDisabled.ok());
    auto lowResult   = queue.enqueue("base:a", "build", "low", eve::Value(eve::Value::Object{}), 2.0, 1);
    auto high1Result = queue.enqueue("base:a", "build", "high1", eve::Value(eve::Value::Object{}), 2.0, 9);
    auto high2Result = queue.enqueue("base:a", "build", "high2", eve::Value(eve::Value::Object{}), 2.0, 9);
    auto otherResult = queue.enqueue("base:b", "train", "unit", eve::Value(eve::Value::Object{}), 2.0, 0);
    REQUIRE(lowResult.ok());
    REQUIRE(high1Result.ok());
    REQUIRE(high2Result.ok());
    REQUIRE(otherResult.ok());
    const auto low          = std::move(lowResult).takeValue();
    const auto high1        = std::move(high1Result).takeValue();
    const auto high2        = std::move(high2Result).takeValue();
    const auto other        = std::move(otherResult).takeValue();
    auto       slotsEnabled = queue.setSlotCount("base:a", 2);
    REQUIRE(slotsEnabled.ok());
    auto high1Task = queue.find(high1);
    auto high2Task = queue.find(high2);
    auto lowTask   = queue.find(low);
    auto otherTask = queue.find(other);
    REQUIRE(high1Task);
    REQUIRE(high2Task);
    REQUIRE(lowTask);
    REQUIRE(otherTask);
    CHECK_EQ(high1Task->get().state == TaskState::Running, true);
    CHECK_EQ(high2Task->get().state == TaskState::Running, true);
    CHECK_EQ(lowTask->get().state == TaskState::Queued, true);
    CHECK_EQ(otherTask->get().state == TaskState::Running, true);
}

TEST_CASE("production.advance.fixedDeltaCompletion") {
    WorkQueue queue;
    auto      firstResult  = queue.enqueue("yard", "assemble", "a", eve::Value(eve::Value::Object{}), 4.0);
    auto      secondResult = queue.enqueue("yard", "assemble", "b", eve::Value(eve::Value::Object{}), 1.0);
    REQUIRE(firstResult.ok());
    REQUIRE(secondResult.ok());
    const auto first     = std::move(firstResult).takeValue();
    const auto second    = std::move(secondResult).takeValue();
    auto       firstStep = queue.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(1000000000)});
    REQUIRE(firstStep.ok());
    auto firstTask = queue.find(first);
    REQUIRE(firstTask);
    CHECK_EQ(firstTask->get().progress.seconds(), 1.0);
    auto secondStep = queue.advance({eve::SimulationTick(2), eve::Duration::fromNanoseconds(1000000000)});
    REQUIRE(secondStep.ok());
    firstTask       = queue.find(first);
    auto secondTask = queue.find(second);
    REQUIRE(firstTask);
    REQUIRE(secondTask);
    CHECK_EQ(firstTask->get().progress.seconds(), 2.0);
    CHECK_EQ(firstTask->get().state == TaskState::Running, true);
    CHECK_EQ(secondTask->get().state == TaskState::Queued, true);
    auto thirdStep = queue.advance({eve::SimulationTick(3), eve::Duration::fromNanoseconds(2000000000)});
    REQUIRE(thirdStep.ok());
    firstTask  = queue.find(first);
    secondTask = queue.find(second);
    REQUIRE(firstTask);
    REQUIRE(secondTask);
    CHECK_EQ(firstTask->get().state == TaskState::Completed, true);
    CHECK_EQ(secondTask->get().state == TaskState::Running, true);
    auto fourthStep = queue.advance({eve::SimulationTick(4), eve::Duration::fromNanoseconds(1000000000)});
    REQUIRE(fourthStep.ok());
    secondTask = queue.find(second);
    REQUIRE(secondTask);
    CHECK_EQ(secondTask->get().state == TaskState::Completed, true);
    auto lastEvent = queue.eventAt(queue.eventCount() - 1);
    REQUIRE(lastEvent);
    CHECK_EQ(lastEvent->get().kind == ProductionEventKind::Completed, true);
}

TEST_CASE("production.lifecycle.pauseResumeCancelFail") {
    WorkQueue queue;
    auto      aResult = queue.enqueue("owner", "kind", "a", eve::Value(eve::Value::Object{}), 2.0);
    auto      bResult = queue.enqueue("owner", "kind", "b", eve::Value(eve::Value::Object{}), 2.0);
    REQUIRE(aResult.ok());
    REQUIRE(bResult.ok());
    const auto a      = std::move(aResult).takeValue();
    const auto b      = std::move(bResult).takeValue();
    auto       paused = queue.pause(a);
    REQUIRE(paused.ok());
    auto aTask = queue.find(a);
    auto bTask = queue.find(b);
    REQUIRE(aTask);
    REQUIRE(bTask);
    CHECK_EQ(aTask->get().state == TaskState::Paused, true);
    CHECK_EQ(bTask->get().state == TaskState::Running, true);
    auto resumed = queue.resume(a);
    REQUIRE(resumed.ok());
    auto cancelled = queue.cancel(b, "changed_plan");
    REQUIRE(cancelled.ok());
    aTask = queue.find(a);
    bTask = queue.find(b);
    REQUIRE(aTask);
    REQUIRE(bTask);
    CHECK_EQ(aTask->get().state == TaskState::Running, true);
    CHECK_EQ(bTask->get().reason, std::string("changed_plan"));
    auto failed = queue.fail(a, "power_loss");
    REQUIRE(failed.ok());
    auto resumeFailed = queue.resume(a);
    CHECK(!resumeFailed.ok());
    aTask = queue.find(a);
    REQUIRE(aTask);
    CHECK_EQ(aTask->get().state == TaskState::Failed, true);
}

TEST_CASE("production.snapshot.restoreIsDeterministicAndTransactional") {
    WorkQueue source;
    auto      slots = source.setSlotCount("yard", 2);
    REQUIRE(slots.ok());
    auto idResult =
        source.enqueue("yard", "vehicle", "tank", eve::Value(eve::Value::Object{{"variant", eve::Value("a")}}), 5.0, 7);
    REQUIRE(idResult.ok());
    const auto id         = std::move(idResult).takeValue();
    auto       sourceStep = source.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(1250000000)});
    REQUIRE(sourceStep.ok());
    auto snapshotResult = source.snapshot();
    REQUIRE(snapshotResult.ok());
    const auto snapshot = std::move(snapshotResult).takeValue();
    WorkQueue  restored;
    auto       restoredResult = restored.restore(snapshot);
    REQUIRE(restoredResult.ok());
    auto restoredSnapshot = restored.snapshot();
    REQUIRE(restoredSnapshot.ok());
    CHECK_EQ(std::move(restoredSnapshot).takeValue(), snapshot);
    auto restoredTask = restored.find(id);
    REQUIRE(restoredTask);
    CHECK_EQ(restoredTask->get().progress.seconds(), 1.25);
    auto beforeResult = restored.snapshot();
    REQUIRE(beforeResult.ok());
    const auto before    = std::move(beforeResult).takeValue();
    auto       malformed = restored.restore("{\"broken\":true}");
    CHECK(!malformed.ok());
    auto afterMalformed = restored.snapshot();
    REQUIRE(afterMalformed.ok());
    CHECK_EQ(std::move(afterMalformed).takeValue(), before);
}

TEST_CASE("production.explicitSettlementPinsDefinitionAndIsIdempotent") {
    WorkQueue queue;
    auto      definition = eve::DefinitionRef::parse("recipe:healing_potion");
    REQUIRE(definition.ok());
    ProductionRequest request;
    request.owner       = "alchemy:bench";
    request.kind        = "craft";
    request.product     = "healing_potion";
    request.duration    = eve::Duration::fromSeconds(1.0).expect("test craft duration");
    request.definition  = {std::move(definition).takeValue(), eve::Generation(7)};
    request.reservation = eve::Value(eve::Value::Object{{"inventory", eve::Value("reservation-42")}});
    auto enqueued       = queue.enqueue(std::move(request));
    REQUIRE(enqueued.ok());
    const std::string taskId = std::move(enqueued).takeValue();
    CHECK_EQ(queue.find(taskId)->get().reservationState, ReservationState::Started);

    REQUIRE(
        queue.advance({eve::SimulationTick(1), eve::Duration::fromSeconds(1.0).expect("test settlement step")}).ok());
    auto ready = queue.find(taskId);
    REQUIRE(ready);
    CHECK_EQ(ready->get().state, TaskState::ReadyToSettle);
    CHECK_EQ(ready->get().reservationState, ReservationState::Consumed);
    CHECK_EQ(ready->get().definition.generation.value(), std::uint64_t{7});

    REQUIRE(queue.failSettlement(taskId, "output container full").ok());
    CHECK_EQ(queue.find(taskId)->get().state, TaskState::SettlementFailed);
    REQUIRE(queue.retrySettlement(taskId).ok());
    ProductionSettlementReceipt receipt;
    receipt.settlementId = "craft:receipt-42";
    receipt.payload      = eve::Value(eve::Value::Object{{"count", eve::Value(1)}});
    REQUIRE(queue.settle(taskId, receipt).ok());
    auto repeated = queue.settle(taskId, receipt);
    REQUIRE(repeated.ok());
    CHECK_EQ(repeated.status().code(), eve::StatusCode::NoOp);
    CHECK_EQ(queue.find(taskId)->get().state, TaskState::Completed);

    auto snapshot = queue.snapshot();
    REQUIRE(snapshot.ok());
    WorkQueue restored;
    REQUIRE(restored.restore(snapshot.value()).ok());
    auto restoredTask = restored.find(taskId);
    REQUIRE(restoredTask);
    CHECK_EQ(restoredTask->get().settlement.settlementId, std::string("craft:receipt-42"));
    CHECK_EQ(restoredTask->get().definition.generation.value(), std::uint64_t{7});
}

TEST_CASE("production.dependenciesGateCrossOwnerWorkAndSurviveSnapshot") {
    WorkQueue queue;
    auto      first = queue.enqueue("forge", "smelt", "ingot", eve::Value(eve::Value::Object{}), 1.0);
    REQUIRE(first.ok());

    ProductionRequest chained;
    chained.owner              = "anvil";
    chained.kind               = "forge";
    chained.product            = "blade";
    chained.duration           = eve::Duration::fromSeconds(1.0).expect("dependency duration");
    chained.settlementRequired = false;
    chained.dependencies.prerequisites.push_back(first.value());
    auto second = queue.enqueue(std::move(chained));
    REQUIRE(second.ok());
    CHECK_EQ(queue.find(second.value())->get().state, TaskState::Queued);
    REQUIRE(queue.blockReason(second.value()).ok());
    CHECK_EQ(queue.blockReason(second.value()).value(), std::string("waiting_for:") + first.value());

    ProductionRequest exclusive;
    exclusive.owner              = "laboratory";
    exclusive.kind               = "research";
    exclusive.product            = "tempering";
    exclusive.duration           = eve::Duration::fromSeconds(1.0).expect("block duration");
    exclusive.settlementRequired = false;
    exclusive.dependencies.blocks.push_back(first.value());
    auto third = queue.enqueue(std::move(exclusive));
    REQUIRE(third.ok());
    CHECK_EQ(queue.find(third.value())->get().state, TaskState::Queued);
    CHECK_EQ(queue.blockReason(third.value()).value(), std::string("blocked_by:") + first.value());

    auto snapshot = queue.snapshot();
    REQUIRE(snapshot.ok());
    WorkQueue restored;
    REQUIRE(restored.restore(snapshot.value()).ok());
    CHECK_EQ(restored.blockReason(second.value()).value(), std::string("waiting_for:") + first.value());
    CHECK_EQ(restored.find(second.value())->get().dependencies.prerequisites.front(), first.value());

    REQUIRE(restored.advance({eve::SimulationTick(1), eve::Duration::fromSeconds(1.0).expect("dependency step")}).ok());
    CHECK_EQ(restored.find(first.value())->get().state, TaskState::Completed);
    CHECK_EQ(restored.find(second.value())->get().state, TaskState::Running);
    CHECK_EQ(restored.find(third.value())->get().state, TaskState::Running);
    CHECK(restored.blockReason(second.value()).value().empty());
}

TEST_CASE("production.dependenciesRejectUnknownAndDuplicateReferences") {
    WorkQueue         queue;
    ProductionRequest request;
    request.owner                      = "forge";
    request.kind                       = "forge";
    request.product                    = "blade";
    request.duration                   = eve::Duration::fromSeconds(1.0).expect("dependency validation duration");
    request.dependencies.prerequisites = {"task-missing"};
    auto missing                       = queue.enqueue(request);
    CHECK(!missing.ok());
    CHECK_EQ(queue.taskCount(), 0);

    auto first = queue.enqueue("forge", "smelt", "ingot", eve::Value(eve::Value::Object{}), 1.0);
    REQUIRE(first.ok());
    request.dependencies.prerequisites = {first.value()};
    request.dependencies.blocks        = {first.value()};
    auto duplicate                     = queue.enqueue(std::move(request));
    CHECK(!duplicate.ok());
    CHECK_EQ(queue.taskCount(), 1);
}

TEST_CASE("production.dependenciesAnyOfStartsAfterFirstSuccessfulPrerequisite") {
    WorkQueue queue;
    REQUIRE(queue.setSlotCount("sources", 2).ok());
    auto failedSource    = queue.enqueue("sources", "source", "failed", eve::Value(eve::Value::Object{}), 1.0);
    auto completedSource = queue.enqueue("sources", "source", "completed", eve::Value(eve::Value::Object{}), 1.0);
    REQUIRE(failedSource.ok());
    REQUIRE(completedSource.ok());

    ProductionRequest request;
    request.owner                      = "consumer";
    request.kind                       = "assemble";
    request.product                    = "result";
    request.duration                   = eve::Duration::fromSeconds(1.0).expect("any-of duration");
    request.settlementRequired         = false;
    request.dependencies.mode          = TaskDependencyMode::AnyOf;
    request.dependencies.prerequisites = {failedSource.value(), completedSource.value()};
    auto dependent                     = queue.enqueue(std::move(request));
    REQUIRE(dependent.ok());
    CHECK_EQ(queue.find(dependent.value())->get().state, TaskState::Queued);

    REQUIRE(queue.fail(failedSource.value(), "source unavailable").ok());
    CHECK_EQ(queue.find(dependent.value())->get().state, TaskState::Queued);
    REQUIRE(queue.advance({eve::SimulationTick(1), eve::Duration::fromSeconds(1.0).expect("any-of step")}).ok());
    CHECK_EQ(queue.find(completedSource.value())->get().state, TaskState::Completed);
    CHECK_EQ(queue.find(dependent.value())->get().state, TaskState::Running);
}

TEST_CASE("production.dependenciesRequireExactDefinitionAndTagFacts") {
    WorkQueue queue;
    REQUIRE(queue.setSlotCount("academy", 2).ok());
    auto reference = eve::DefinitionRef::parse("technology:advanced_metallurgy");
    REQUIRE(reference.ok());
    const eve::definition::DefinitionHandle generation7{reference.value(), eve::Generation(7)};
    const eve::definition::DefinitionHandle generation8{reference.value(), eve::Generation(8)};

    ProductionRequest request;
    request.owner                            = "academy";
    request.kind                             = "research";
    request.product                          = "alloy";
    request.duration                         = eve::Duration::fromSeconds(1.0).expect("requirement duration");
    request.settlementRequired               = false;
    request.dependencies.requiredDefinitions = {generation7};
    request.dependencies.requiredTags        = {"facility:powered"};
    auto taskId                              = queue.enqueue(std::move(request));
    REQUIRE(taskId.ok());
    CHECK_EQ(queue.blockReason(taskId.value()).value(),
             std::string("requires_definition:technology:advanced_metallurgy@7"));

    REQUIRE(queue.setDefinitionAvailable("academy", generation8, true).ok());
    CHECK_EQ(queue.find(taskId.value())->get().state, TaskState::Queued);
    REQUIRE(queue.setDefinitionAvailable("academy", generation7, true).ok());
    CHECK_EQ(queue.blockReason(taskId.value()).value(), std::string("requires_tag:facility:powered"));
    REQUIRE(queue.setTagAvailable("academy", "facility:powered", true).ok());
    CHECK_EQ(queue.find(taskId.value())->get().state, TaskState::Running);

    auto snapshot = queue.snapshot();
    REQUIRE(snapshot.ok());
    WorkQueue restored;
    REQUIRE(restored.restore(snapshot.value()).ok());
    CHECK(restored.definitionAvailable("academy", generation7));
    CHECK(restored.definitionAvailable("academy", generation8));
    CHECK(restored.tagAvailable("academy", "facility:powered"));
    CHECK_EQ(restored.find(taskId.value())->get().dependencies.requiredDefinitions.front(), generation7);
    CHECK_EQ(restored.find(taskId.value())->get().dependencies.requiredTags.front(), std::string("facility:powered"));
    CHECK_EQ(restored.snapshot().value(), snapshot.value());
}

TEST_CASE("production.batchContributionResourcesAndRefundPolicyRoundTrip") {
    WorkQueue queue;
    REQUIRE(queue.setSlotCount("forge", 2).ok());
    REQUIRE(queue.setResourceCapacity("forge", "crucible", 2).ok());

    ProductionRequest batch;
    batch.owner                    = "forge";
    batch.kind                     = "alchemy";
    batch.product                  = "healing_potion";
    batch.duration                 = eve::Duration::fromSeconds(4.0).expect("batch duration");
    batch.batchSize                = 3;
    batch.efficiencyPermille       = 500;
    batch.resources                = {{"crucible", 2}};
    batch.reservation              = eve::Value(eve::Value::Object{{"ingredients", eve::Value("reservation:batch:1")}});
    batch.termination.cancellation = RefundPolicy::Proportional;
    batch.settlementRequired       = false;
    auto first                     = queue.enqueue(std::move(batch));
    REQUIRE(first.ok());

    ProductionRequest waiting;
    waiting.owner               = "forge";
    waiting.kind                = "alchemy";
    waiting.product             = "mana_potion";
    waiting.duration            = eve::Duration::fromSeconds(1.0).expect("waiting duration");
    waiting.resources           = {{"crucible", 1}};
    waiting.termination.failure = RefundPolicy::None;
    waiting.settlementRequired  = false;
    auto second                 = queue.enqueue(std::move(waiting));
    REQUIRE(second.ok());
    CHECK_EQ(queue.find(second.value())->get().state, TaskState::Queued);
    CHECK_EQ(queue.blockReason(second.value()).value(), std::string("resource_unavailable"));

    REQUIRE(queue.advance({eve::SimulationTick(1), eve::Duration::fromSeconds(2.0).expect("scaled step")}).ok());
    CHECK_EQ(queue.find(first.value())->get().progress.nanoseconds(), std::int64_t{1000000000});
    WorkContribution contribution{"crafter:7", eve::Duration::fromSeconds(1.0).expect("contribution"), 2000};
    REQUIRE(queue.contribute(first.value(), std::move(contribution)).ok());
    CHECK_EQ(queue.find(first.value())->get().progress.nanoseconds(), std::int64_t{3000000000});
    REQUIRE(queue.cancel(first.value(), "player_cancelled").ok());
    CHECK_EQ(queue.find(first.value())->get().refundPermille, std::uint32_t{250});
    auto released = queue.releaseReservation(first.value(), "release:batch:1");
    REQUIRE(released.ok());
    CHECK_EQ(released.value().refundPermille, std::uint32_t{250});
    CHECK_EQ(queue.find(first.value())->get().reservationState, ReservationState::Released);
    auto repeatedRelease = queue.releaseReservation(first.value(), "release:batch:1");
    REQUIRE(repeatedRelease.ok());
    CHECK_EQ(repeatedRelease.status().code(), eve::StatusCode::NoOp);
    CHECK_EQ(queue.find(first.value())->get().batchSize, std::uint32_t{3});
    CHECK_EQ(queue.find(second.value())->get().state, TaskState::Running);

    auto snapshot = queue.snapshot();
    REQUIRE(snapshot.ok());
    WorkQueue restored;
    REQUIRE(restored.restore(snapshot.value()).ok());
    CHECK_EQ(restored.resourceCapacity("forge", "crucible"), 2);
    CHECK_EQ(restored.find(first.value())->get().refundPermille, std::uint32_t{250});
    CHECK_EQ(restored.find(first.value())->get().reservationRelease.releaseId, std::string("release:batch:1"));
    CHECK_EQ(restored.snapshot().value(), snapshot.value());
}

TEST_CASE("production.advancedDeterminismSchedulingPredictionAndCatchUp") {
    WorkQueue queue;
    REQUIRE(queue.setSlotCount("lab", 0).ok());
    REQUIRE(queue.setSchedulerStrategy("lab", SchedulerStrategy::Fifo).ok());

    ProductionRequest first;
    first.owner              = "lab";
    first.kind               = "research";
    first.product            = "slow_low_priority";
    first.duration           = eve::Duration::fromSeconds(4.0).expect("first duration");
    first.priority           = -10;
    first.settlementRequired = false;
    auto firstId             = queue.enqueue(std::move(first));
    REQUIRE(firstId.ok());

    ProductionRequest second;
    second.owner              = "lab";
    second.kind               = "research";
    second.product            = "fast_high_priority";
    second.duration           = eve::Duration::fromSeconds(1.0).expect("second duration");
    second.priority           = 100;
    second.settlementRequired = false;
    auto secondId             = queue.enqueue(std::move(second));
    REQUIRE(secondId.ok());
    REQUIRE(queue.setSlotCount("lab", 1).ok());
    CHECK_EQ(queue.find(firstId.value())->get().state, TaskState::Running);
    CHECK_EQ(queue.find(secondId.value())->get().state, TaskState::Queued);

    REQUIRE(queue.setSlotCount("smith", 0).ok());
    REQUIRE(queue.setSchedulerStrategy("smith", SchedulerStrategy::ShortestRemaining).ok());
    auto longJob  = queue.enqueue("smith", "forge", "plate", eve::Value(eve::Value::Object{}), 8.0, 100);
    auto shortJob = queue.enqueue("smith", "forge", "nail", eve::Value(eve::Value::Object{}), 1.0, -100);
    REQUIRE(longJob.ok());
    REQUIRE(shortJob.ok());
    REQUIRE(queue.setSlotCount("smith", 1).ok());
    CHECK_EQ(queue.find(shortJob.value())->get().state, TaskState::Running);
    CHECK_EQ(queue.find(longJob.value())->get().state, TaskState::Queued);

    ProductionRequest recurring;
    recurring.owner                      = "still";
    recurring.kind                       = "brewing";
    recurring.product                    = "tonic";
    recurring.duration                   = eve::Duration::fromSeconds(1.0).expect("recurring duration");
    recurring.batchSize                  = 2;
    recurring.repeat.continuous          = true;
    recurring.repeat.maintainStockTarget = 2;
    recurring.random                     = {"production.tonic", 0x12345678u, 9, 4};
    recurring.correlationId              = "order:tonic:42";
    recurring.settlementRequired         = false;
    auto recurringId                     = queue.enqueue(std::move(recurring));
    REQUIRE(recurringId.ok());
    auto prediction = queue.predict(recurringId.value());
    REQUIRE(prediction.ok());
    CHECK(prediction.value().exact);
    CHECK_EQ(prediction.value().estimatedCompletionAfter.nanoseconds(), std::int64_t{1000000000});

    REQUIRE(queue.advance({eve::SimulationTick(1), eve::Duration::fromSeconds(1.0).expect("first step")}).ok());
    auto recurringTask = queue.find(recurringId.value());
    REQUIRE(recurringTask);
    CHECK_EQ(recurringTask->get().completedCycles, std::uint32_t{1});
    CHECK_EQ(recurringTask->get().state, TaskState::Queued);
    CHECK_EQ(queue.blockReason(recurringId.value()).value(), std::string("stock_target_satisfied"));
    auto diagnostic = queue.diagnose(recurringId.value());
    REQUIRE(diagnostic.ok());
    CHECK_EQ(diagnostic.value().correlationId, std::string("order:tonic:42"));
    CHECK_EQ(diagnostic.value().code, std::string("stock_target_satisfied"));

    REQUIRE(queue.reportStock(recurringId.value(), 0).ok());
    CHECK_EQ(queue.find(recurringId.value())->get().state, TaskState::Running);
    auto bounded = queue.advanceTo(eve::SimulationTick(10), eve::Duration::fromSeconds(1.0).expect("bounded"), 1);
    CHECK(!bounded.ok());
    CHECK_EQ(queue.currentTick().value(), std::uint64_t{1});
    auto caughtUp = queue.advanceTo(eve::SimulationTick(3), eve::Duration::fromSeconds(1.0).expect("catch up"), 2);
    REQUIRE(caughtUp.ok());
    CHECK_EQ(caughtUp.value(), std::uint32_t{2});
    CHECK_EQ(queue.find(recurringId.value())->get().completedCycles, std::uint32_t{2});

    ProductionRequest explicitRepeat;
    explicitRepeat.owner              = "ritual";
    explicitRepeat.kind               = "enchant";
    explicitRepeat.product            = "rune";
    explicitRepeat.duration           = eve::Duration::fromSeconds(1.0).expect("repeat settlement duration");
    explicitRepeat.repeat.totalCycles = 2;
    auto explicitId                   = queue.enqueue(std::move(explicitRepeat));
    REQUIRE(explicitId.ok());
    REQUIRE(queue.advanceTo(eve::SimulationTick(4), eve::Duration::fromSeconds(1.0).expect("settle step"), 1).ok());
    ProductionSettlementReceipt cycleReceipt{"ritual:cycle:1", eve::Value(eve::Value::Object{})};
    REQUIRE(queue.settle(explicitId.value(), cycleReceipt).ok());
    CHECK_EQ(queue.find(explicitId.value())->get().state, TaskState::Running);
    auto duplicateCycle = queue.settle(explicitId.value(), cycleReceipt);
    REQUIRE(duplicateCycle.ok());
    CHECK_EQ(duplicateCycle.status().code(), eve::StatusCode::NoOp);

    auto snapshot = queue.snapshot();
    REQUIRE(snapshot.ok());
    WorkQueue restored;
    REQUIRE(restored.restore(snapshot.value()).ok());
    const auto restoredTask = restored.find(recurringId.value());
    REQUIRE(restoredTask);
    CHECK_EQ(restoredTask->get().random.stream, std::string("production.tonic"));
    CHECK_EQ(restoredTask->get().random.seed, std::uint64_t{0x12345678u});
    CHECK_EQ(restored.schedulerStrategy("lab"), SchedulerStrategy::Fifo);
    CHECK_EQ(restored.diagnose(recurringId.value()).value().correlationId, std::string("order:tonic:42"));
    CHECK_EQ(restored.snapshot().value(), snapshot.value());
}

TEST_CASE("production.fixedPointWorkRemainderSurvivesSnapshotAndEventuallyProgresses") {
    WorkQueue         queue;
    ProductionRequest request;
    request.owner              = "micro-forge";
    request.kind               = "precision";
    request.product            = "micro-part";
    request.duration           = eve::Duration::fromNanoseconds(2);
    request.efficiencyPermille = 1;
    request.settlementRequired = false;
    auto taskId                = queue.enqueue(std::move(request));
    REQUIRE(taskId.ok());

    for (std::uint64_t tick = 1; tick <= 500; ++tick)
        REQUIRE(queue.advance({eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}).ok());
    CHECK_EQ(queue.find(taskId.value())->get().progress.nanoseconds(), std::int64_t{0});
    CHECK_EQ(queue.find(taskId.value())->get().workRemainderPermille, std::uint32_t{500});

    auto snapshot = queue.snapshot();
    REQUIRE(snapshot.ok());
    WorkQueue restored;
    REQUIRE(restored.restore(snapshot.value()).ok());
    CHECK_EQ(restored.find(taskId.value())->get().workRemainderPermille, std::uint32_t{500});
    for (std::uint64_t tick = 501; tick <= 1000; ++tick)
        REQUIRE(restored.advance({eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}).ok());
    CHECK_EQ(restored.find(taskId.value())->get().progress.nanoseconds(), std::int64_t{1});
    CHECK_EQ(restored.find(taskId.value())->get().workRemainderPermille, std::uint32_t{0});

    WorkContribution contribution{"precision-tool", eve::Duration::fromNanoseconds(1), 1};
    REQUIRE(restored.contribute(taskId.value(), contribution).ok());
    CHECK_EQ(restored.find(taskId.value())->get().workRemainderPermille, std::uint32_t{1});
}

TEST_CASE("production.advanceOverflowLeavesTickAndEveryTaskUnchanged") {
    WorkQueue queue;
    REQUIRE(queue.setSlotCount("factory", 2).ok());
    ProductionRequest safe;
    safe.owner              = "factory";
    safe.kind               = "build";
    safe.product            = "safe";
    safe.duration           = eve::Duration::fromNanoseconds(std::numeric_limits<std::int64_t>::max());
    safe.efficiencyPermille = 1;
    safe.settlementRequired = false;
    auto safeId             = queue.enqueue(std::move(safe));
    REQUIRE(safeId.ok());
    ProductionRequest overflowing;
    overflowing.owner              = "factory";
    overflowing.kind               = "build";
    overflowing.product            = "overflow";
    overflowing.duration           = eve::Duration::fromNanoseconds(std::numeric_limits<std::int64_t>::max());
    overflowing.efficiencyPermille = std::numeric_limits<std::uint32_t>::max();
    overflowing.settlementRequired = false;
    auto overflowId                = queue.enqueue(std::move(overflowing));
    REQUIRE(overflowId.ok());

    auto advanced = queue.advance(
        {eve::SimulationTick(1), eve::Duration::fromNanoseconds(std::numeric_limits<std::int64_t>::max())});
    CHECK(!advanced.ok());
    CHECK_EQ(queue.currentTick(), eve::SimulationTick::zero());
    CHECK_EQ(queue.find(safeId.value())->get().progress, eve::Duration::zero());
    CHECK_EQ(queue.find(overflowId.value())->get().progress, eve::Duration::zero());
}

TEST_CASE("production.longPredictionAndRefundUseOverflowSafeArithmetic") {
    WorkQueue         queue;
    ProductionRequest request;
    request.owner                    = "long-project";
    request.kind                     = "build";
    request.product                  = "megaproject";
    request.duration                 = eve::Duration::fromNanoseconds(std::numeric_limits<std::int64_t>::max());
    request.efficiencyPermille       = 1;
    request.termination.cancellation = RefundPolicy::Proportional;
    request.settlementRequired       = false;
    auto taskId                      = queue.enqueue(std::move(request));
    REQUIRE(taskId.ok());
    auto prediction = queue.predict(taskId.value());
    CHECK(!prediction.ok());

    WorkContribution contribution{"builder",
                                  eve::Duration::fromNanoseconds(std::numeric_limits<std::int64_t>::max() / 2), 1000};
    REQUIRE(queue.contribute(taskId.value(), contribution).ok());
    REQUIRE(queue.cancel(taskId.value()).ok());
    CHECK_EQ(queue.find(taskId.value())->get().refundPermille, std::uint32_t{500});
}
}  // namespace test_production_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_production_protobuf)
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/SnapshotHash.h"
#include "production/Production.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
namespace test_production_protobuf {
using eve::production::ProductionRequest;
using eve::production::SchedulerStrategy;
using eve::production::WorkQueue;

TEST_CASE("production.protobuf.typedQueueRoundTripAndAtomicFailure") {
    WorkQueue original;
    REQUIRE(original.setSlotCount("forge", 2).ok());
    REQUIRE(original.setResourceCapacity("forge", "furnace", 3).ok());
    REQUIRE(original.setSchedulerStrategy("forge", SchedulerStrategy::Fifo).ok());
    REQUIRE(original.setTagAvailable("forge", "heated", true).ok());
    auto reference = eve::DefinitionRef::parse("recipe:ingot");
    REQUIRE(reference.ok());
    const eve::definition::DefinitionHandle definition{std::move(reference).takeValue(), eve::Generation(4)};
    REQUIRE(original.setDefinitionAvailable("forge", definition, true).ok());

    ProductionRequest first;
    first.owner    = "forge";
    first.kind     = "smelt";
    first.product  = "ingot";
    first.context  = eve::Value(eve::Value::Object{{"quantity", eve::Value(std::numeric_limits<std::int64_t>::max())}});
    first.duration = eve::Duration::fromNanoseconds(4000000000LL);
    first.definition  = definition;
    first.reservation = eve::Value(eve::Value::Object{{"ore", eve::Value(3)}});
    first.resources.push_back({"furnace", 1});
    first.dependencies.requiredTags.push_back("heated");
    first.dependencies.requiredDefinitions.push_back(definition);
    first.settlementRequired = false;
    auto firstId             = original.enqueue(std::move(first));
    REQUIRE(firstId.ok());

    ProductionRequest second;
    second.owner    = "forge";
    second.kind     = "forge";
    second.product  = "blade";
    second.duration = eve::Duration::fromNanoseconds(2000000000LL);
    second.dependencies.prerequisites.push_back(firstId.value());
    second.settlementRequired = false;
    REQUIRE(original.enqueue(std::move(second)).ok());
    REQUIRE(original.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(1000000000LL)}).ok());

    auto binary = original.snapshotBinary();
    REQUIRE(binary.ok());
    WorkQueue restored;
    REQUIRE(restored.restoreBinary(binary.value()).ok());
    auto recaptured = restored.snapshotBinary();
    REQUIRE(recaptured.ok());
    CHECK_EQ(recaptured.value(), binary.value());
    REQUIRE(restored.find(firstId.value()));
    const auto* context = restored.find(firstId.value())->get().context.getIf<eve::Value::Object>();
    REQUIRE(context != nullptr);
    CHECK_EQ(context->at("quantity").asInt(), std::numeric_limits<std::int64_t>::max());
    CHECK_EQ(restored.resourceCapacity("forge", "furnace"), 3);
    CHECK_EQ(restored.schedulerStrategy("forge"), SchedulerStrategy::Fifo);

    auto legacy = original.snapshot();
    REQUIRE(legacy.ok());
    WorkQueue imported;
    REQUIRE(imported.restore(legacy.value()).ok());
    auto upgraded = imported.snapshotBinary();
    REQUIRE(upgraded.ok());
    CHECK_EQ(upgraded.value(), binary.value());

    std::string unknown = binary.value();
    unknown.append("\xA0\x06\x01", 3);
    CHECK(!restored.restoreBinary(unknown).ok());
    CHECK(!restored.restoreBinary("invalid").ok());
    auto afterFailure = restored.snapshotBinary();
    REQUIRE(afterFailure.ok());
    CHECK_EQ(afterFailure.value(), binary.value());

    const auto hash     = eve::snapshotContentHashProvider();
    auto       envelope = original.snapshotEnvelopeBinary(hash);
    REQUIRE(envelope.ok());
    WorkQueue sealed;
    REQUIRE(sealed.restoreSnapshotBinary(envelope.value(), hash).ok());
    auto sealedBinary = sealed.snapshotBinary();
    REQUIRE(sealedBinary.ok());
    CHECK_EQ(sealedBinary.value(), binary.value());
    std::string tampered = envelope.value();
    tampered.back() ^= 1;
    CHECK(!sealed.restoreSnapshotBinary(tampered, hash).ok());
}

TEST_CASE("production.protobuf.settlementAndReservationRelease") {
    WorkQueue         queue;
    ProductionRequest settledRequest;
    settledRequest.owner       = "alchemy";
    settledRequest.kind        = "craft";
    settledRequest.product     = "potion";
    settledRequest.duration    = eve::Duration::fromNanoseconds(1000000000LL);
    settledRequest.reservation = eve::Value(eve::Value::Object{{"inventory", eve::Value("hold-1")}});
    auto settledId             = queue.enqueue(std::move(settledRequest));
    REQUIRE(settledId.ok());
    REQUIRE(queue.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(1000000000LL)}).ok());
    eve::production::ProductionSettlementReceipt receipt;
    receipt.settlementId = "receipt-1";
    receipt.payload      = eve::Value(eve::Value::Object{{"quantity", eve::Value(1)}});
    REQUIRE(queue.settle(settledId.value(), std::move(receipt)).ok());

    ProductionRequest cancelledRequest;
    cancelledRequest.owner       = "alchemy";
    cancelledRequest.kind        = "craft";
    cancelledRequest.product     = "elixir";
    cancelledRequest.duration    = eve::Duration::fromNanoseconds(2000000000LL);
    cancelledRequest.reservation = eve::Value(eve::Value::Object{{"inventory", eve::Value("hold-2")}});
    auto cancelledId             = queue.enqueue(std::move(cancelledRequest));
    REQUIRE(cancelledId.ok());
    REQUIRE(queue.cancel(cancelledId.value(), "changed_plan").ok());
    REQUIRE(queue.releaseReservation(cancelledId.value(), "release-2").ok());

    auto binary = queue.snapshotBinary();
    REQUIRE(binary.ok());
    WorkQueue restored;
    REQUIRE(restored.restoreBinary(binary.value()).ok());
    REQUIRE(restored.find(settledId.value()));
    REQUIRE(restored.find(cancelledId.value()));
    CHECK_EQ(restored.find(settledId.value())->get().settlement.settlementId, std::string("receipt-1"));
    CHECK_EQ(restored.find(cancelledId.value())->get().reservationRelease.releaseId, std::string("release-2"));
    auto recaptured = restored.snapshotBinary();
    REQUIRE(recaptured.ok());
    CHECK_EQ(recaptured.value(), binary.value());
}
}  // namespace test_production_protobuf
#endif
