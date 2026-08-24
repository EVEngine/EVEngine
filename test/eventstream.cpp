#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "eventstream/EventStream.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::eventstream;

TEST_CASE("eventstream.emit.monotonicEnvelope") {
    Stream stream;
    CHECK_EQ(stream.emit("created", "system", "entity-7", "", "flow-2", 41, 3, "{\"hp\":9}"), uint64_t{1});
    CHECK_EQ(stream.emit("changed", "rule", "entity-7", "1", "flow-2", 42, 0, "null"), uint64_t{2});
    REQUIRE(stream.find(2) != nullptr);
    CHECK_EQ(stream.find(2)->causation, std::string("1"));
    CHECK_EQ(stream.find(2)->tick, int64_t{42});
    CHECK_EQ(stream.find(2)->flags, uint32_t{0});
    CHECK_EQ(stream.find(1)->payload, std::string("{\"hp\":9}"));
    CHECK_EQ(stream.emit("", "", "", "", "", 0, 0, "null"), uint64_t{0});
    CHECK_EQ(stream.emit("bad", "", "", "", "", 0, 0, "{"), uint64_t{0});
}

TEST_CASE("eventstream.query.filtersInSequenceOrder") {
    Stream stream;
    stream.emit("a", "one", "x", "", "", 0, 0);
    stream.emit("b", "two", "x", "", "", 0, 0);
    stream.emit("a", "two", "y", "", "", 0, 0);
    CHECK_EQ(stream.queryType("a"), 2);
    CHECK_EQ(stream.queryAt(0)->sequence, uint64_t{1});
    CHECK_EQ(stream.queryAt(1)->sequence, uint64_t{3});
    CHECK_EQ(stream.querySource("two"), 2);
    CHECK_EQ(stream.querySubject("x"), 2);
    CHECK_EQ(stream.querySequence(2), 2);
}

TEST_CASE("eventstream.consumer.cursorReadsBatches") {
    Stream stream;
    for (int i = 0; i < 5; ++i) stream.emit("tick", "clock", "", "", "", i, 0);
    auto* consumer = stream.newConsumer(2);
    REQUIRE(consumer != nullptr);
    CHECK_EQ(consumer->read(2), 2);
    CHECK_EQ(consumer->batchAt(0)->sequence, uint64_t{2});
    CHECK_EQ(consumer->batchAt(1)->sequence, uint64_t{3});
    CHECK_EQ(consumer->position(), uint64_t{4});
    CHECK_EQ(consumer->read(10), 2);
    CHECK_EQ(consumer->position(), uint64_t{6});
}

TEST_CASE("eventstream.retention.clearAndResetSemantics") {
    Stream stream;
    stream.emit("a", "", "", "", "", 0, 0);
    stream.emit("b", "", "", "", "", 0, 0);
    stream.emit("c", "", "", "", "", 0, 0);
    stream.clearBefore(3);
    REQUIRE(stream.size() == 1);
    CHECK_EQ(stream.at(0)->sequence, uint64_t{3});
    stream.clear();
    CHECK_EQ(stream.emit("d", "", "", "", "", 0, 0), uint64_t{4});
    stream.reset();
    CHECK_EQ(stream.emit("e", "", "", "", "", 0, 0), uint64_t{1});
}

TEST_CASE("eventstream.snapshot.roundTripsDeterministically") {
    Stream original;
    original.emit("created", "admin", "base-1", "", "campaign", -17, 5, "{ \"rank\": 3, \"general\": \"g-1\" }");
    original.emit("assigned", "admin", "general-1", "1", "campaign", 18, 0, "[\"unit-2\",\"unit-3\"]");
    const std::string snapshot = original.snapshotJson();

    Stream restored;
    REQUIRE(restored.restoreJson(snapshot));
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK_EQ(restored.find(1)->tick, int64_t{-17});
    CHECK_EQ(restored.find(1)->payload, std::string("{\"general\":\"g-1\",\"rank\":3}"));
    CHECK_EQ(restored.emit("next", "", "", "", "", 19, 0), uint64_t{3});

    const std::string before = restored.snapshotJson();
    CHECK(!restored.restoreJson("{\"version\":1}"));
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("eventstream.script.emitQueryCursorAndSnapshot") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.EventStream();
        local stream = module.newStream();
        local seq = stream.emit("promoted", "administration", "general-4", "", "career-4", 12, 1, "{\"rank\":4}");
        local cursor = stream.newConsumer(1);
        if (seq == 1 && stream.querySubject("general-4") == 1 && cursor.read(8) == 1) {
            local copy = module.newStream();
            if (copy.restoreJson(stream.snapshotJson()) && copy.find(1).getType() == "promoted")
                result = cursor.batchAt(0).getCorrelation();
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("career-4"));
}
