#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "orders/Orders.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::orders;

TEST_CASE("orders.append.priorityAndStableIds") {
    OrderQueue queue;
    const auto first = queue.append("first", 1);
    const auto low   = queue.append("low", 1);
    const auto high  = queue.append("high", 10);

    CHECK_EQ(first, std::string("order-0000000000000001"));
    CHECK_EQ(low, std::string("order-0000000000000002"));
    CHECK_EQ(high, std::string("order-0000000000000003"));
    REQUIRE(queue.current() != nullptr);
    CHECK_EQ(queue.current()->kind, std::string("first"));
    CHECK(queue.complete(first));
    CHECK_EQ(queue.current()->kind, std::string("high"));
    CHECK(queue.complete(high));
    CHECK_EQ(queue.current()->kind, std::string("low"));
}

TEST_CASE("orders.replace.cancelsUnfinishedDeterministically") {
    OrderQueue queue;
    const auto active = queue.append("work");
    const auto queued = queue.append("later");
    const auto next   = queue.replace("replacement", 5);

    CHECK_EQ(static_cast<int>(queue.find(active)->state), static_cast<int>(OrderState::Cancelled));
    CHECK_EQ(static_cast<int>(queue.find(queued)->state), static_cast<int>(OrderState::Cancelled));
    CHECK_EQ(queue.current()->id, next);
    REQUIRE(queue.eventCount() == 7);
    CHECK_EQ(queue.eventAt(0)->sequence, uint64_t{1});
    CHECK_EQ(static_cast<int>(queue.eventAt(0)->to), static_cast<int>(OrderState::Queued));
    CHECK_EQ(queue.eventAt(3)->orderId, active);
    CHECK_EQ(queue.eventAt(3)->reason, std::string("replaced"));
    CHECK_EQ(queue.eventAt(4)->orderId, queued);
    CHECK_EQ(queue.eventAt(5)->orderId, next);
    CHECK_EQ(static_cast<int>(queue.eventAt(6)->to), static_cast<int>(OrderState::Active));
}

TEST_CASE("orders.interrupt.honoursPriority") {
    OrderQueue queue;
    const auto active = queue.append("critical", 20);
    CHECK(queue.interrupt("weak", 19).empty());
    CHECK_EQ(queue.current()->id, active);

    const auto urgent = queue.interrupt("urgent", 20);
    CHECK(!urgent.empty());
    CHECK_EQ(static_cast<int>(queue.find(active)->state), static_cast<int>(OrderState::Cancelled));
    CHECK_EQ(queue.find(active)->reason, std::string("interrupted"));
    CHECK_EQ(queue.current()->id, urgent);
}

TEST_CASE("orders.lifecycle.completeFailCancel") {
    OrderQueue queue;
    const auto one   = queue.append("one");
    const auto two   = queue.append("two");
    const auto three = queue.append("three");

    CHECK(queue.complete(one));
    CHECK_EQ(static_cast<int>(queue.find(one)->state), static_cast<int>(OrderState::Completed));
    CHECK(queue.fail(two, "policy_rejected"));
    CHECK_EQ(static_cast<int>(queue.find(two)->state), static_cast<int>(OrderState::Failed));
    CHECK_EQ(queue.find(two)->reason, std::string("policy_rejected"));
    CHECK(queue.cancel(three, "owner_removed"));
    CHECK_EQ(static_cast<int>(queue.find(three)->state), static_cast<int>(OrderState::Cancelled));
    CHECK(queue.current() == nullptr);
    CHECK(!queue.complete(three));
}

TEST_CASE("orders.timeout.failsAndActivatesNext") {
    OrderQueue queue;
    const auto timed = queue.append("timed", 0, 0.5);
    const auto next  = queue.append("next");
    queue.update(0.25);
    CHECK_EQ(queue.current()->id, timed);
    queue.update(0.25);
    CHECK_EQ(static_cast<int>(queue.find(timed)->state), static_cast<int>(OrderState::Failed));
    CHECK_EQ(queue.find(timed)->reason, std::string("timeout"));
    CHECK_EQ(queue.current()->id, next);
}

TEST_CASE("orders.payload.isDeterministicJsonCompatible") {
    OrderQueue queue;
    const auto id      = queue.append("custom");
    auto*      payload = &queue.find(id)->payload;
    REQUIRE(payload != nullptr);
    payload->setString("name", "A\nB");
    payload->setNumber("x", 12.5);
    payload->setBool("enabled", true);
    CHECK(payload->setJson("nested", "{\"target\":7}"));
    CHECK(!payload->setJson("bad", "not-json"));
    CHECK_EQ(payload->toJson(), std::string("{\"enabled\":true,\"name\":\"A\\nB\","
                                            "\"nested\":{\"target\":7},\"x\":12.5}"));
}

TEST_CASE("orders.script.queueAndPayloadApi") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Orders();
        local queue = module.newQueue();
        local id = queue.append("custom_action", 7, 1.0);
        local order = queue.current();
        order.getPayload().setJson("context", "{\"zone\":3}");
        if (order.getId() == id && order.getKind() == "custom_action" &&
            order.getPayload().getJson("context") == "{\"zone\":3}") {
            queue.complete(id);
            result = order.getState();
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("completed"));
}
