#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "production/Production.h"

#include <simplesquirrel/simplesquirrel.hpp>

using namespace eve::production;

TEST_CASE("production.enqueue.stableIdsAndCanonicalContext") {
    WorkQueue queue;
    eve::Value::Object context;
    context.emplace("z", eve::Value(2));
    context.emplace("a", eve::Value(1));
    auto firstResult = queue.enqueue("factory:1", "vehicle", "tank.medium",
                                     eve::Value(std::move(context)), 10.0, 2);
    REQUIRE(firstResult.ok());
    const auto first = std::move(firstResult).takeValue();
    auto secondResult = queue.enqueue("factory:1", "vehicle", "truck",
                                      eve::Value(eve::Value::Object{}), 5.0, 1);
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
    auto slotsDisabled = queue.setSlotCount("base:a", 0);
    REQUIRE(slotsDisabled.ok());
    auto lowResult = queue.enqueue("base:a", "build", "low",
                                   eve::Value(eve::Value::Object{}), 2.0, 1);
    auto high1Result = queue.enqueue("base:a", "build", "high1",
                                     eve::Value(eve::Value::Object{}), 2.0, 9);
    auto high2Result = queue.enqueue("base:a", "build", "high2",
                                     eve::Value(eve::Value::Object{}), 2.0, 9);
    auto otherResult = queue.enqueue("base:b", "train", "unit",
                                    eve::Value(eve::Value::Object{}), 2.0, 0);
    REQUIRE(lowResult.ok());
    REQUIRE(high1Result.ok());
    REQUIRE(high2Result.ok());
    REQUIRE(otherResult.ok());
    const auto low = std::move(lowResult).takeValue();
    const auto high1 = std::move(high1Result).takeValue();
    const auto high2 = std::move(high2Result).takeValue();
    const auto other = std::move(otherResult).takeValue();
    auto slotsEnabled = queue.setSlotCount("base:a", 2);
    REQUIRE(slotsEnabled.ok());
    auto high1Task = queue.find(high1);
    auto high2Task = queue.find(high2);
    auto lowTask = queue.find(low);
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
    auto firstResult = queue.enqueue("yard", "assemble", "a",
                                     eve::Value(eve::Value::Object{}), 4.0);
    auto secondResult = queue.enqueue("yard", "assemble", "b",
                                      eve::Value(eve::Value::Object{}), 1.0);
    REQUIRE(firstResult.ok());
    REQUIRE(secondResult.ok());
    const auto first = std::move(firstResult).takeValue();
    const auto second = std::move(secondResult).takeValue();
    auto firstStep = queue.advance(
        {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1000000000)});
    REQUIRE(firstStep.ok());
    auto firstTask = queue.find(first);
    REQUIRE(firstTask);
    CHECK_EQ(firstTask->get().progress.seconds(), 1.0);
    auto secondStep = queue.advance(
        {eve::SimulationTick(2), eve::Duration::fromNanoseconds(1000000000)});
    REQUIRE(secondStep.ok());
    firstTask = queue.find(first);
    auto secondTask = queue.find(second);
    REQUIRE(firstTask);
    REQUIRE(secondTask);
    CHECK_EQ(firstTask->get().progress.seconds(), 2.0);
    CHECK_EQ(firstTask->get().state == TaskState::Running, true);
    CHECK_EQ(secondTask->get().state == TaskState::Queued, true);
    auto thirdStep = queue.advance(
        {eve::SimulationTick(3), eve::Duration::fromNanoseconds(2000000000)});
    REQUIRE(thirdStep.ok());
    firstTask = queue.find(first);
    secondTask = queue.find(second);
    REQUIRE(firstTask);
    REQUIRE(secondTask);
    CHECK_EQ(firstTask->get().state == TaskState::Completed, true);
    CHECK_EQ(secondTask->get().state == TaskState::Running, true);
    auto fourthStep = queue.advance(
        {eve::SimulationTick(4), eve::Duration::fromNanoseconds(1000000000)});
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
    auto aResult = queue.enqueue("owner", "kind", "a", eve::Value(eve::Value::Object{}), 2.0);
    auto bResult = queue.enqueue("owner", "kind", "b", eve::Value(eve::Value::Object{}), 2.0);
    REQUIRE(aResult.ok());
    REQUIRE(bResult.ok());
    const auto a = std::move(aResult).takeValue();
    const auto b = std::move(bResult).takeValue();
    auto paused = queue.pause(a);
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
    auto slots = source.setSlotCount("yard", 2);
    REQUIRE(slots.ok());
    auto idResult = source.enqueue("yard", "vehicle", "tank",
                                   eve::Value(eve::Value::Object{{"variant", eve::Value("a")}}),
                                   5.0, 7);
    REQUIRE(idResult.ok());
    const auto id = std::move(idResult).takeValue();
    auto sourceStep = source.advance(
        {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1250000000)});
    REQUIRE(sourceStep.ok());
    auto snapshotResult = source.snapshot();
    REQUIRE(snapshotResult.ok());
    const auto snapshot = std::move(snapshotResult).takeValue();
    WorkQueue restored;
    auto restoredResult = restored.restore(snapshot);
    REQUIRE(restoredResult.ok());
    auto restoredSnapshot = restored.snapshot();
    REQUIRE(restoredSnapshot.ok());
    CHECK_EQ(std::move(restoredSnapshot).takeValue(), snapshot);
    auto restoredTask = restored.find(id);
    REQUIRE(restoredTask);
    CHECK_EQ(restoredTask->get().progress.seconds(), 1.25);
    auto beforeResult = restored.snapshot();
    REQUIRE(beforeResult.ok());
    const auto before = std::move(beforeResult).takeValue();
    auto malformed = restored.restore("{\"broken\":true}");
    CHECK(!malformed.ok());
    auto afterMalformed = restored.snapshot();
    REQUIRE(afterMalformed.ok());
    CHECK_EQ(std::move(afterMalformed).takeValue(), before);
}

TEST_CASE("production.script.queueLifecycle") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Production();
        local queueResult = module.newWorkQueue();
        local queue = queueResult.ok ? queueResult.value : null;
        local slots = queue == null ? { ok=false } : queue.setSlotCount("factory", 2);
        local idResult = queue == null ? { ok=false } : queue.enqueue(
            "factory", "vehicle", "tank", "{\"armor\":100}", 2.0, 5);
        local id = idResult.ok ? idResult.value : "";
        local advanced = queue == null ? { ok=false } : queue.advance(1, 2.0);
        local task = queue == null ? null : queue.find(id);
        local savedResult = queue == null ? { ok=false } : queue.snapshot();
        local saved = savedResult.ok ? savedResult.value : "";
        local context = task == null ? {} : task.getContext();
        if (queueResult.ok && slots.ok && idResult.ok && advanced.ok && savedResult.ok && task != null &&
            task.getState() == "completed" && context.armor == 100 &&
            queue.eventAt(queue.eventCount() - 1).getKind() == "completed" && queue.restore(saved).ok) result = "ok";
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
