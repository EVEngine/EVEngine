#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "orders/CommandQueue.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::orders;

namespace {
std::string takeId(eve::Result<std::string> result) {
    REQUIRE(result.hasValue());
    return std::move(result).takeValue();
}
}  // namespace

TEST_CASE("orders.append.priorityAndStableIds") {
    CommandQueue queue;
    const auto   first = takeId(queue.append("first", 1));
    const auto   low   = takeId(queue.append("low", 1));
    const auto   high  = takeId(queue.append("high", 10));

    CHECK_EQ(first, std::string("order-0000000000000001"));
    CHECK_EQ(low, std::string("order-0000000000000002"));
    CHECK_EQ(high, std::string("order-0000000000000003"));
    REQUIRE(static_cast<bool>(queue.current()));
    CHECK_EQ(queue.current()->get().kind, std::string("first"));
    CHECK(queue.complete(first).ok());
    CHECK_EQ(queue.current()->get().kind, std::string("high"));
    CHECK(queue.complete(high).ok());
    CHECK_EQ(queue.current()->get().kind, std::string("low"));
}

TEST_CASE("orders.replace.cancelsUnfinishedDeterministically") {
    CommandQueue queue;
    const auto   active = takeId(queue.append("work"));
    const auto   queued = takeId(queue.append("later"));
    const auto   next   = takeId(queue.replace("replacement", 5));

    CHECK_EQ(static_cast<int>(queue.find(active)->get().state), static_cast<int>(OrderState::Cancelled));
    CHECK_EQ(static_cast<int>(queue.find(queued)->get().state), static_cast<int>(OrderState::Cancelled));
    CHECK_EQ(queue.current()->get().id, next);
    REQUIRE(queue.eventCount() == 7);
    CHECK_EQ(queue.eventAt(0)->get().sequence, uint64_t{1});
    CHECK_EQ(static_cast<int>(queue.eventAt(0)->get().to), static_cast<int>(OrderState::Queued));
    CHECK_EQ(queue.eventAt(3)->get().orderId, active);
    CHECK_EQ(queue.eventAt(3)->get().reason, std::string("replaced"));
    CHECK_EQ(queue.eventAt(4)->get().orderId, queued);
    CHECK_EQ(queue.eventAt(5)->get().orderId, next);
    CHECK_EQ(static_cast<int>(queue.eventAt(6)->get().to), static_cast<int>(OrderState::Active));
}

TEST_CASE("orders.interrupt.honoursPriority") {
    CommandQueue queue;
    const auto   active = takeId(queue.append("critical", 20));
    CHECK(!queue.interrupt("weak", 19).hasValue());
    CHECK_EQ(queue.current()->get().id, active);

    const auto urgent = takeId(queue.interrupt("urgent", 20));
    CHECK(!urgent.empty());
    CHECK_EQ(static_cast<int>(queue.find(active)->get().state), static_cast<int>(OrderState::Cancelled));
    CHECK_EQ(queue.find(active)->get().reason, std::string("interrupted"));
    CHECK_EQ(queue.current()->get().id, urgent);
}

TEST_CASE("orders.lifecycle.completeFailCancel") {
    CommandQueue queue;
    const auto   one   = takeId(queue.append("one"));
    const auto   two   = takeId(queue.append("two"));
    const auto   three = takeId(queue.append("three"));

    CHECK(queue.complete(one).ok());
    CHECK_EQ(static_cast<int>(queue.find(one)->get().state), static_cast<int>(OrderState::Completed));
    CHECK(queue.fail(two, "policy_rejected").ok());
    CHECK_EQ(static_cast<int>(queue.find(two)->get().state), static_cast<int>(OrderState::Failed));
    CHECK_EQ(queue.find(two)->get().reason, std::string("policy_rejected"));
    CHECK(queue.cancel(three, "owner_removed").ok());
    CHECK_EQ(static_cast<int>(queue.find(three)->get().state), static_cast<int>(OrderState::Cancelled));
    CHECK(!queue.current());
    CHECK(!queue.complete(three).ok());
}

TEST_CASE("orders.timeout.failsAndActivatesNext") {
    CommandQueue queue;
    const auto   timed = takeId(queue.append("timed", 0, 0.5));
    const auto   next  = takeId(queue.append("next"));
    CHECK(queue.update(0.25).ok());
    CHECK_EQ(queue.current()->get().id, timed);
    CHECK(queue.update(0.25).ok());
    CHECK_EQ(static_cast<int>(queue.find(timed)->get().state), static_cast<int>(OrderState::Failed));
    CHECK_EQ(queue.find(timed)->get().reason, std::string("timeout"));
    CHECK_EQ(queue.current()->get().id, next);
}

TEST_CASE("orders.payload.isDeterministicJsonCompatible") {
    CommandQueue queue;
    const auto   id      = takeId(queue.append("custom"));
    auto*        payload = &queue.find(id)->get().payload;
    REQUIRE(payload != nullptr);
    payload->setString("name", "A\nB");
    payload->setNumber("x", 12.5);
    payload->setBool("enabled", true);
    CHECK(payload->setJson("nested", "{\"target\":7}").ok());
    CHECK(!payload->setJson("bad", "not-json").ok());
    CHECK_EQ(payload->toJson(), std::string("{\"enabled\":true,\"name\":\"A\\nB\","
                                            "\"nested\":{\"target\":7},\"x\":12.5}"));
}

TEST_CASE("orders.snapshot.roundTripAndMalformedRestorePreservesState") {
    CommandQueue source;
    const auto   first    = takeId(source.append("first", 1, 3.0));
    const auto   second   = takeId(source.append("second", 5, 4.0));
    auto         firstRef = source.find(first);
    REQUIRE(firstRef);
    firstRef->get().payload.setString("name", "alpha");
    firstRef->get().payload.setNumber("amount", 12.5);
    REQUIRE(source.complete(first).ok());
    const auto third    = takeId(source.append("third", 2, 0.5));
    auto       thirdRef = source.find(third);
    REQUIRE(thirdRef);
    thirdRef->get().payload.setBool("urgent", true);

    auto snapshotResult = source.snapshot();
    REQUIRE(snapshotResult.ok());
    const auto snapshot = std::move(snapshotResult).takeValue();

    CommandQueue restored;
    REQUIRE(restored.restore(snapshot).ok());
    auto restoredSnapshot = restored.snapshot();
    REQUIRE(restoredSnapshot.ok());
    CHECK_EQ(std::move(restoredSnapshot).takeValue(), snapshot);
    CHECK_EQ(restored.orderCount(), 3);
    CHECK_EQ(restored.queuedCount(), 1);
    REQUIRE(restored.current());
    CHECK_EQ(restored.current()->get().id, second);
    CHECK_EQ(restored.eventCount(), 6);
    REQUIRE(restored.find(first));
    CHECK_EQ(restored.find(first)->get().payload.getJson("name"), std::string("\"alpha\""));
    REQUIRE(restored.find(third));
    CHECK_EQ(restored.find(third)->get().payload.getJson("urgent"), std::string("true"));

    auto next = takeId(restored.append("fourth", 9));
    CHECK_EQ(next, std::string("order-0000000000000004"));
    REQUIRE(restored.eventAt(restored.eventCount() - 1));
    CHECK_EQ(restored.eventAt(restored.eventCount() - 1)->get().sequence, uint64_t{7});

    auto beforeResult = restored.snapshot();
    REQUIRE(beforeResult.ok());
    const auto before    = std::move(beforeResult).takeValue();
    auto       malformed = restored.restore("{\"schema\":\"orders.command_queue\"");
    CHECK(!malformed.ok());
    auto afterMalformed = restored.snapshot();
    REQUIRE(afterMalformed.ok());
    CHECK_EQ(std::move(afterMalformed).takeValue(), before);

    auto invalid = restored.restore(
        R"({"schema":"orders.command_queue","version":1,"nextId":"1","nextSequence":"1",
            "active":null,"queued":[],"orders":[],"events":[]})");
    CHECK(!invalid.ok());
    auto afterInvalid = restored.snapshot();
    REQUIRE(afterInvalid.ok());
    CHECK_EQ(std::move(afterInvalid).takeValue(), before);
}

TEST_CASE("orders.script.queueAndPayloadApi") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Orders();
        local queueResult = module.newQueueOwned();
        local queue = queueResult.ok ? queueResult.value : null;
        local idResult = queue == null ? { ok=false } : queue.append("custom_action", 7, 1.0);
        local id = idResult.ok ? idResult.value : "";
        local order = queue.current();
        local payloadResult = order.getPayload().setJson("context", "{\"zone\":3}");
        if (queueResult.ok && idResult.ok && payloadResult.ok && order.getId() == id && order.getKind() == "custom_action" &&
            order.getPayload().getJson("context") == "{\"zone\":3}") {
            local completed = queue.complete(id);
            if (completed.ok) result = order.getState();
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("completed"));
}

TEST_CASE("orders.script.ownedQueueReplace") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Orders();
        local queueResult = module.newQueueOwned();
        if (!queueResult.ok) throw queueResult.status.summary;
        local queue = queueResult.value;
        local first = queue.append("move", 1, 0.0);
        if (!first.ok) throw first.status.summary;
        local replaced = queue.replace("move", 10, 0.0);
        if (!replaced.ok) throw replaced.status.summary;
        local order = queue.current();
        if (order != null && order.getId() == replaced.value && order.getKind() == "move" &&
            order.getState() == "active") {
            order.getPayload().setNumber("x", 400.0);
            order.getPayload().setNumber("y", 360.0);
            if (order.getPayload().has("x") && order.getPayload().has("y"))
                result = "replaced";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("replaced"));
}
