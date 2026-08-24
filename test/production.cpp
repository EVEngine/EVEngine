#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "production/Production.h"

#include <simplesquirrel/simplesquirrel.hpp>

using namespace eve::production;

TEST_CASE("production.enqueue.stableIdsAndCanonicalContext") {
    ProductionQueue queue;
    auto            first  = queue.enqueue("factory:1", "vehicle", "tank.medium", "{\"z\":2,\"a\":1}", 10.0, 2);
    auto            second = queue.enqueue("factory:1", "vehicle", "truck", "{}", 5.0, 1);
    CHECK_EQ(first, std::string("task-0000000000000001"));
    CHECK_EQ(second, std::string("task-0000000000000002"));
    CHECK_EQ(queue.find(first)->contextJson, std::string("{\"a\":1,\"z\":2}"));
    CHECK_EQ(queue.find(first)->state == TaskState::Running, true);
    CHECK_EQ(queue.find(second)->state == TaskState::Queued, true);
    CHECK(queue.enqueue("", "x", "y", "{}", 1.0).empty());
    CHECK(queue.enqueue("owner", "x", "y", "bad", 1.0).empty());
}

TEST_CASE("production.scheduling.prioritySlotsAndOwnersAreDeterministic") {
    ProductionQueue queue;
    CHECK(queue.setSlotCount("base:a", 0));
    auto low   = queue.enqueue("base:a", "build", "low", "{}", 2.0, 1);
    auto high1 = queue.enqueue("base:a", "build", "high1", "{}", 2.0, 9);
    auto high2 = queue.enqueue("base:a", "build", "high2", "{}", 2.0, 9);
    auto other = queue.enqueue("base:b", "train", "unit", "{}", 2.0, 0);
    CHECK(queue.setSlotCount("base:a", 2));
    CHECK_EQ(queue.find(high1)->state == TaskState::Running, true);
    CHECK_EQ(queue.find(high2)->state == TaskState::Running, true);
    CHECK_EQ(queue.find(low)->state == TaskState::Queued, true);
    CHECK_EQ(queue.find(other)->state == TaskState::Running, true);
}

TEST_CASE("production.update.fixedDeltaSpeedAndCompletion") {
    ProductionQueue queue;
    auto            first  = queue.enqueue("yard", "assemble", "a", "{}", 4.0);
    auto            second = queue.enqueue("yard", "assemble", "b", "{}", 1.0);
    queue.update(1.0, 2.0);
    CHECK_EQ(queue.find(first)->progress, 2.0);
    queue.update(1.0, 2.0);
    CHECK_EQ(queue.find(first)->state == TaskState::Completed, true);
    CHECK_EQ(queue.find(second)->state == TaskState::Running, true);
    queue.update(0.5, 2.0);
    CHECK_EQ(queue.find(second)->state == TaskState::Completed, true);
    CHECK_EQ(queue.eventAt(queue.eventCount() - 1)->kind == ProductionEventKind::Completed, true);
}

TEST_CASE("production.lifecycle.pauseResumeCancelFail") {
    ProductionQueue queue;
    auto            a = queue.enqueue("owner", "kind", "a", "{}", 2.0);
    auto            b = queue.enqueue("owner", "kind", "b", "{}", 2.0);
    CHECK(queue.pause(a));
    CHECK_EQ(queue.find(a)->state == TaskState::Paused, true);
    CHECK_EQ(queue.find(b)->state == TaskState::Running, true);
    CHECK(queue.resume(a));
    CHECK(queue.cancel(b, "changed_plan"));
    CHECK_EQ(queue.find(a)->state == TaskState::Running, true);
    CHECK_EQ(queue.find(b)->reason, std::string("changed_plan"));
    CHECK(queue.fail(a, "power_loss"));
    CHECK(!queue.resume(a));
    CHECK_EQ(queue.find(a)->state == TaskState::Failed, true);
}

TEST_CASE("production.snapshot.restoreIsDeterministicAndTransactional") {
    ProductionQueue source;
    source.setSlotCount("yard", 2);
    auto id = source.enqueue("yard", "vehicle", "tank", "{\"variant\":\"a\"}", 5.0, 7);
    source.update(1.25, 1.0);
    const auto      snapshot = source.snapshot();
    ProductionQueue restored;
    REQUIRE(restored.restore(snapshot));
    CHECK_EQ(restored.snapshot(), snapshot);
    CHECK_EQ(restored.find(id)->progress, 1.25);
    const auto before = restored.snapshot();
    CHECK(!restored.restore("{\"broken\":true}"));
    CHECK_EQ(restored.snapshot(), before);
}

TEST_CASE("production.script.queueLifecycle") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Production();
        local queue = module.newQueue();
        queue.setSlotCount("factory", 2);
        local id = queue.enqueue("factory", "vehicle", "tank", "{\"armor\":100}", 2.0, 5);
        queue.update(1.0, 2.0);
        local task = queue.find(id);
        local saved = queue.snapshot();
        if (task.getState() == "completed" && task.getContextJson() == "{\"armor\":100}" &&
            queue.eventAt(queue.eventCount() - 1).getKind() == "completed" && queue.restore(saved)) result = "ok";
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
