#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "GameEventTestSupport.h"
#include "common/Module.h"
#include "game_event/GameEvent.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::game_event;

namespace {

std::uint64_t appendSuccess(GameEventLog& stream, std::uint64_t serial, std::string type, std::string source,
                            std::string subject, std::string causation, std::string correlation, std::uint64_t tick,
                            std::uint32_t flags, std::string payload = "null") {
    auto result =
        stream.append(test::envelope(serial, std::move(type), std::move(source), std::move(subject),
                                     std::move(causation), std::move(correlation), tick, flags, std::move(payload)));
    REQUIRE(result.ok());
    return result.value().value();
}

}  // namespace

TEST_CASE("game_event.emit.monotonicEnvelope") {
    GameEventLog stream;
    CHECK_EQ(appendSuccess(stream, 1, "created", "system", "entity-7", "", "flow-2", 41, 3, "{\"hp\":9}"), uint64_t{1});
    CHECK_EQ(appendSuccess(stream, 2, "changed", "rule", "entity-7", "1", "flow-2", 42, 0), uint64_t{2});
    REQUIRE(stream.find(2) != nullptr);
    CHECK(!stream.find(1)->eventId.isNil());
    CHECK(!stream.find(2)->eventId.isNil());
    CHECK_EQ(stream.find(2)->causation.format(), std::string("1"));
    CHECK_EQ(stream.find(2)->tick.value(), uint64_t{42});
    CHECK_EQ(stream.find(2)->flags, uint32_t{0});
    CHECK_EQ(stream.find(1)->payload, std::string("{\"hp\":9}"));
    auto emptyType = stream.append(test::envelope(3, "", "", "", "", "", 0, 0));
    REQUIRE(!emptyType.ok());
    auto invalidPayload = stream.append(test::envelope(4, "bad", "", "", "", "", 0, 0, "{"));
    REQUIRE(!invalidPayload.ok());
}

TEST_CASE("game_event.query.filtersInSequenceOrder") {
    GameEventLog stream;
    CHECK_EQ(appendSuccess(stream, 1, "a", "one", "x", "", "", 0, 0), uint64_t{1});
    CHECK_EQ(appendSuccess(stream, 2, "b", "two", "x", "", "", 0, 0), uint64_t{2});
    CHECK_EQ(appendSuccess(stream, 3, "a", "two", "y", "", "", 0, 0), uint64_t{3});
    CHECK_EQ(stream.queryType("a"), 2);
    CHECK_EQ(stream.queryAt(0)->sequence.value(), uint64_t{1});
    CHECK_EQ(stream.queryAt(1)->sequence.value(), uint64_t{3});
    CHECK_EQ(stream.querySource("two"), 2);
    CHECK_EQ(stream.querySubject("x"), 2);
    CHECK_EQ(stream.querySequence(2), 2);
}

TEST_CASE("game_event.consumer.cursorReadsBatches") {
    GameEventLog stream;
    for (int i = 0; i < 5; ++i)
        CHECK_EQ(appendSuccess(stream, static_cast<std::uint64_t>(i + 1), "tick", "clock", "", "", "",
                               static_cast<std::uint64_t>(i), 0),
                 static_cast<uint64_t>(i + 1));
    auto* consumer = stream.newConsumer(2);
    REQUIRE(consumer != nullptr);
    CHECK_EQ(consumer->read(2), 2);
    CHECK_EQ(consumer->batchAt(0)->sequence.value(), uint64_t{2});
    CHECK_EQ(consumer->batchAt(1)->sequence.value(), uint64_t{3});
    CHECK_EQ(consumer->position().value(), uint64_t{4});
    CHECK_EQ(consumer->read(10), 2);
    CHECK_EQ(consumer->position().value(), uint64_t{6});
}

TEST_CASE("game_event.retention.clearAndResetSemantics") {
    GameEventLog stream;
    const auto   a = appendSuccess(stream, 1, "a", "", "", "", "", 0, 0);
    const auto   b = appendSuccess(stream, 2, "b", "", "", "", "", 0, 0);
    const auto   c = appendSuccess(stream, 3, "c", "", "", "", "", 0, 0);
    CHECK_EQ(a, uint64_t{1});
    CHECK_EQ(b, uint64_t{2});
    CHECK_EQ(c, uint64_t{3});
    stream.clearBefore(3);
    REQUIRE(stream.size() == 1);
    CHECK_EQ(stream.at(0)->sequence.value(), uint64_t{3});
    stream.clear();
    const auto d = appendSuccess(stream, 4, "d", "", "", "", "", 0, 0);
    CHECK_EQ(d, uint64_t{4});
    stream.reset();
    const auto e = appendSuccess(stream, 5, "e", "", "", "", "", 0, 0);
    CHECK_EQ(e, uint64_t{1});
}

TEST_CASE("game_event.snapshot.roundTripsDeterministically") {
    GameEventLog original;
    CHECK_EQ(appendSuccess(original, 1, "created", "admin", "base-1", "", "campaign", 17, 5,
                           "{ \"rank\": 3, \"general\": \"g-1\" }"),
             uint64_t{1});
    CHECK_EQ(
        appendSuccess(original, 2, "assigned", "admin", "general-1", "1", "campaign", 18, 0, "[\"unit-2\",\"unit-3\"]"),
        uint64_t{2});
    const std::string snapshot = original.snapshotJson();

    GameEventLog restored;
    REQUIRE(restored.restore(snapshot).ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK_EQ(restored.find(1)->tick.value(), uint64_t{17});
    CHECK_EQ(restored.find(1)->payload, std::string("{\"general\":\"g-1\",\"rank\":3}"));
    CHECK_EQ(appendSuccess(restored, 3, "next", "", "", "", "", 19, 0), uint64_t{3});

    const std::string before = restored.snapshotJson();
    CHECK(!restored.restore("{\"version\":1}").ok());
    CHECK_EQ(restored.snapshotJson(), before);

    const std::string unknownField   = snapshot.substr(0, snapshot.size() - 1) + ",\"unexpected\":true}";
    auto              schemaRejected = restored.restore(unknownField);
    CHECK(!schemaRejected.ok());
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("game_event.script.emitQueryCursorAndSnapshot") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.GameEvent();
        local stream = module.newLog();
        local seq = stream.append("00000000-0000-4000-8000-000000000001", "promoted", "administration", "general-4", "", "career-4", 12, 1, "{\"rank\":4}");
        local cursor = stream.newConsumer(1);
        if (seq.ok && seq.value == 1 && stream.querySubject("general-4") == 1 && cursor.read(8) == 1) {
            local copy = module.newLog();
            if (copy.restore(stream.snapshotJson()).ok && copy.find(1).getType() == "promoted")
                result = cursor.batchAt(0).getCorrelation();
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("career-4"));
}
