// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "orders.cpp"
  ]
}
*/
// clang-format on

#include "orders/CommandQueue.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>
#include <string>
#include <utility>

using namespace eve::orders;

namespace {
std::string append(CommandQueue& queue, const char* kind, int priority = 0, double timeout = 0.) {
    auto result = queue.append(kind, priority, timeout);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

const Order& active(const CommandQueue& queue) {
    auto current = queue.current();
    REQUIRE(current);
    return current->get();
}
}

TEST_CASE("Orders scheduling and replacement contract") {
    SUB_CASE("orders contract: append assigns stable IDs and schedules by priority then insertion") {
        CommandQueue queue;
        REQUIRE(!queue.current());
        const auto first = append(queue, "first", 1);
        const auto low   = append(queue, "low", 1);
        const auto high  = append(queue, "high", 10);
        REQUIRE_EQ(first, "order-0000000000000001");
        REQUIRE_EQ(low, "order-0000000000000002");
        REQUIRE_EQ(high, "order-0000000000000003");
        REQUIRE_EQ(active(queue).id, first);
        REQUIRE_EQ(queue.queuedCount(), 2);
        REQUIRE_EQ(queue.orderCount(), 3);
        REQUIRE(queue.complete(first).ok());
        REQUIRE_EQ(active(queue).id, high);
        REQUIRE(queue.complete(high).ok());
        REQUIRE_EQ(active(queue).id, low);
        REQUIRE(queue.complete(low).ok());
        REQUIRE(!queue.current());
        REQUIRE(queue.find(first));
        REQUIRE(!queue.orderAt(-1));
        REQUIRE(!queue.orderAt(3));
    };

    SUB_CASE("orders contract: invalid commands and time are rejected without consuming identities") {
        CommandQueue queue;
        REQUIRE(!queue.append("").ok());
        REQUIRE(!queue.append("bad", 0, -1.).ok());
        REQUIRE(!queue.replace("bad", 0, std::numeric_limits<double>::infinity()).ok());
        REQUIRE(!queue.interrupt("bad", 0, std::numeric_limits<double>::quiet_NaN()).ok());
        REQUIRE(!queue.update(-1.).ok());
        REQUIRE(!queue.update(std::numeric_limits<double>::infinity()).ok());
        REQUIRE_EQ(queue.orderCount(), 0);
        REQUIRE_EQ(queue.eventCount(), 0);
        const auto validId = append(queue, "valid");
        REQUIRE_EQ(validId, "order-0000000000000001");
        REQUIRE(!queue.complete("").ok());
        REQUIRE(!queue.fail("missing", "reason").ok());
        REQUIRE(!queue.cancel("missing").ok());
    };

    SUB_CASE("orders contract: replace cancels all unfinished work and starts one replacement") {
        CommandQueue queue;
        const auto   first       = append(queue, "first");
        const auto   waiting     = append(queue, "waiting");
        auto         replacement = queue.replace("replacement", 5);
        REQUIRE(replacement.ok());
        REQUIRE(queue.find(first)->get().state == OrderState::Cancelled);
        REQUIRE_EQ(queue.find(first)->get().reason, "replaced");
        REQUIRE(queue.find(waiting)->get().state == OrderState::Cancelled);
        REQUIRE_EQ(active(queue).id, replacement.value());
        REQUIRE_EQ(queue.queuedCount(), 0);
        REQUIRE_EQ(queue.eventCount(), 7);
        for (int i = 0; i < queue.eventCount(); ++i) REQUIRE_EQ(queue.eventAt(i)->get().sequence, std::uint64_t(i + 1));
    };

    SUB_CASE("orders contract: interrupt requires equal or greater priority and leaves history") {
        CommandQueue queue;
        const auto   first = append(queue, "critical", 20);
        REQUIRE(!queue.interrupt("weak", 19).ok());
        REQUIRE_EQ(active(queue).id, first);
        auto urgent = queue.interrupt("urgent", 20);
        REQUIRE(urgent.ok());
        REQUIRE(queue.find(first)->get().state == OrderState::Cancelled);
        REQUIRE_EQ(queue.find(first)->get().reason, "interrupted");
        REQUIRE_EQ(active(queue).id, urgent.value());
    };
}

TEST_CASE("Orders lifecycle time and payload contract") {
    SUB_CASE("orders contract: complete fail and cancel accept only allowed lifecycle states") {
        CommandQueue queue;
        const auto   one   = append(queue, "one");
        const auto   two   = append(queue, "two");
        const auto   three = append(queue, "three");
        REQUIRE(!queue.complete(two).ok());
        REQUIRE(queue.complete(one).ok());
        REQUIRE(queue.find(one)->get().state == OrderState::Completed);
        REQUIRE(!queue.complete(one).ok());
        REQUIRE(queue.fail(two, "policy_rejected").ok());
        REQUIRE(queue.find(two)->get().state == OrderState::Failed);
        REQUIRE_EQ(queue.find(two)->get().reason, "policy_rejected");
        REQUIRE(queue.cancel(three, "owner_removed").ok());
        REQUIRE(queue.find(three)->get().state == OrderState::Cancelled);
        REQUIRE(!queue.current());
        REQUIRE(!queue.cancel(three).ok());
    };

    SUB_CASE("orders contract: simulation time reaches timeout exactly and activates next") {
        CommandQueue queue;
        const auto   timed = append(queue, "timed", 0, .5);
        const auto   next  = append(queue, "next");
        REQUIRE(queue.update(0.).ok());
        REQUIRE_EQ(active(queue).elapsedSeconds, 0.);
        REQUIRE(queue.update(.25).ok());
        REQUIRE_EQ(active(queue).id, timed);
        REQUIRE(queue.update(.25).ok());
        REQUIRE(queue.find(timed)->get().state == OrderState::Failed);
        REQUIRE_EQ(queue.find(timed)->get().reason, "timeout");
        REQUIRE_EQ(active(queue).id, next);
    };

    SUB_CASE("orders contract: payload owns typed JSON and failed parse leaves prior value") {
        OrderPayload payload;
        payload.setString("name", "A\nB");
        payload.setNumber("x", 12.5);
        payload.setBool("enabled", true);
        payload.setNull("none");
        REQUIRE(payload.setJson("nested", "{\"target\":7}").ok());
        const auto before = payload.toJson();
        REQUIRE(!payload.setJson("nested", "not-json").ok());
        REQUIRE_EQ(payload.toJson(), before);
        REQUIRE(!payload.set("", eve::Value(true)).ok());
        REQUIRE(payload.has("nested"));
        auto removed = payload.erase("nested");
        REQUIRE(removed.ok());
        REQUIRE(removed.value());
        REQUIRE(!payload.has("nested"));
        payload.clear();
        REQUIRE_EQ(payload.toJson(), "{}");
    };
}

TEST_CASE("Orders persistence and reset contract") {
    SUB_CASE("orders contract: snapshot restores active queue payload and counters") {
        CommandQueue source;
        const auto   first  = append(source, "first", 1, 3.);
        const auto   second = append(source, "second", 5, 4.);
        source.find(first)->get().payload.setString("name", "alpha");
        REQUIRE(source.complete(first).ok());
        const auto third = append(source, "third", 2, .5);
        source.find(third)->get().payload.setBool("urgent", true);
        auto snapshot = source.snapshot();
        REQUIRE(snapshot.ok());

        CommandQueue restored;
        REQUIRE(restored.restore(snapshot.value()).ok());
        auto roundTrip = restored.snapshot();
        REQUIRE(roundTrip.ok());
        REQUIRE_EQ(roundTrip.value(), snapshot.value());
        REQUIRE_EQ(active(restored).id, second);
        REQUIRE_EQ(restored.queuedCount(), 1);
        REQUIRE_EQ(restored.find(first)->get().payload.getJson("name"), "\"alpha\"");
        REQUIRE_EQ(restored.find(third)->get().payload.getJson("urgent"), "true");
        const auto fourthId = append(restored, "fourth");
        REQUIRE_EQ(fourthId, "order-0000000000000004");
        REQUIRE_EQ(restored.eventAt(restored.eventCount() - 1)->get().sequence, 7u);
    };

    SUB_CASE("orders contract: malformed restore is atomic and clear resets identity allocation") {
        CommandQueue queue;
        append(queue, "existing");
        auto before = queue.snapshot();
        REQUIRE(before.ok());
        REQUIRE(!queue.restore("{\"schema\":\"orders.command_queue\"").ok());
        REQUIRE(
            !queue
                 .restore(
                     R"({"schema":"orders.command_queue","version":1,"nextId":"0","nextSequence":"1","active":null,"queued":[],"orders":[],"events":[]})")
                 .ok());
        auto after = queue.snapshot();
        REQUIRE(after.ok());
        REQUIRE_EQ(after.value(), before.value());
        queue.clearEvents();
        REQUIRE_EQ(queue.eventCount(), 0);
        queue.clear();
        REQUIRE_EQ(queue.orderCount(), 0);
        const auto freshId = append(queue, "fresh");
        REQUIRE_EQ(freshId, "order-0000000000000001");
        REQUIRE_EQ(queue.eventAt(0)->get().sequence, 1u);
    };
}
