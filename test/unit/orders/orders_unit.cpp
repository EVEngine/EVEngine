#include "zeroerr/assert.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "orders/CommandQueue.h"
#include "orders_command_queue.pb.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::orders;

namespace {
[[maybe_unused]] std::string takeId(eve::Result<std::string> result) {
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
        R"({"schema":"orders.command_queue","version":1,"nextId":"0","nextSequence":"1",
            "active":null,"queued":[],"orders":[],"events":[]})");
    CHECK(!invalid.ok());
    auto afterInvalid = restored.snapshot();
    REQUIRE(afterInvalid.ok());
    CHECK_EQ(std::move(afterInvalid).takeValue(), before);
}

TEST_CASE("orders.protobuf.binaryRoundTripRejectsInvalidWithoutMutation") {
    CommandQueue source;
    const auto   first   = takeId(source.append("first", 2, 4.0));
    const auto   next    = takeId(source.append("next", 1));
    auto*        payload = &source.find(first)->get().payload;
    REQUIRE(payload->setJson("exact", "9223372036854775807").ok());
    auto saved = source.snapshotBinary();
    REQUIRE(saved.ok());

    CommandQueue restored;
    REQUIRE(restored.restoreBinary(saved.value()).ok());
    auto repeated = restored.snapshotBinary();
    REQUIRE(repeated.ok());
    CHECK_EQ(repeated.value(), saved.value());
    REQUIRE(restored.find(first));
    CHECK_EQ(restored.find(first)->get().payload.getJson("exact"), std::string("9223372036854775807"));
    CHECK_EQ(restored.queuedCount(), 1);
    REQUIRE(restored.find(next));

    eve::orders::proto::CommandQueueSave invalid;
    REQUIRE(invalid.ParseFromString(saved.value()));
    invalid.set_version(3);
    std::string future;
    REQUIRE(invalid.SerializeToString(&future));
    auto rejected = restored.restoreBinary(future);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), eve::DiagnosticCode::UnknownVersion);
    invalid.set_version(2);
    invalid.GetReflection()->MutableUnknownFields(&invalid)->AddVarint(100, 1);
    std::string unknown;
    REQUIRE(invalid.SerializeToString(&unknown));
    CHECK(!restored.restoreBinary(unknown).ok());
    CHECK(!restored.restoreBinary("invalid").ok());
    auto after = restored.snapshotBinary();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), saved.value());
}

TEST_CASE("orders.protobuf.legacyJsonImportRewritesTypedSchema") {
    constexpr std::string_view legacy =
        R"({"schema":"orders.command_queue","version":1,"nextId":"2","nextSequence":"3","active":0,"queued":[],"orders":[{"elapsedSeconds":0,"id":"order-0000000000000001","kind":"work","payload":{"exact":9223372036854775807},"priority":2,"reason":"","state":"active","timeoutSeconds":0}],"events":[{"from":"queued","kind":"work","orderId":"order-0000000000000001","reason":"","sequence":"1","to":"queued"},{"from":"queued","kind":"work","orderId":"order-0000000000000001","reason":"","sequence":"2","to":"active"}]})";
    CommandQueue restored;
    REQUIRE(restored.restore(legacy).ok());
    REQUIRE(restored.current());
    CHECK_EQ(restored.current()->get().payload.getJson("exact"), std::string("9223372036854775807"));
    auto rewritten = restored.snapshot();
    REQUIRE(rewritten.ok());
    CHECK(rewritten.value().find("\"version\":2") != std::string::npos);
    CommandQueue copy;
    REQUIRE(copy.restore(rewritten.value()).ok());
    auto stable   = copy.snapshotBinary();
    auto original = restored.snapshotBinary();
    REQUIRE(stable.ok());
    REQUIRE(original.ok());
    CHECK_EQ(stable.value(), original.value());
}
