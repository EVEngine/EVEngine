#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "statepatch/StatePatch.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::statepatch;

TEST_CASE("statepatch.batch.commitsCanonicalValuesAtomically") {
    Store store;
    auto* batch = store.newBatch();
    REQUIRE(batch != nullptr);
    CHECK(batch->set("entity-b", "health", "{ \"max\": 10, \"now\": 8 }"));
    CHECK(batch->set("entity-a", "active", "true"));
    REQUIRE(store.commit(batch));
    CHECK_EQ(store.revision(), uint64_t{1});
    CHECK_EQ(store.get("entity-b", "health"), std::string("{\"max\":10,\"now\":8}"));
    CHECK_EQ(store.valueRevision("entity-a", "active"), uint64_t{1});
    CHECK_EQ(batch->result().changedCount, 2);
}

TEST_CASE("statepatch.batch.conflictProducesNoPartialWrites") {
    Store store;
    auto* seed = store.newBatch();
    seed->set("one", "value", "1");
    REQUIRE(store.commit(seed));
    auto* batch = store.newBatch();
    batch->set("two", "value", "2");
    batch->setExpected("one", "value", "3", "99");
    CHECK(!store.commit(batch));
    CHECK(!store.has("two", "value"));
    CHECK_EQ(store.get("one", "value"), std::string("1"));
    CHECK_EQ(store.revision(), uint64_t{1});
    REQUIRE(batch->result().errors.size() == 1);
    CHECK_EQ(batch->result().errors[0].code, std::string("conflict"));
}

TEST_CASE("statepatch.batch.validatesEverythingBeforeCommit") {
    Store store;
    auto* batch = store.newBatch();
    CHECK(!batch->set("ok", "bad-json", "{"));
    batch->set("", "missing-subject", "1");
    batch->remove("ok", "");
    CHECK(!store.commit(batch));
    CHECK_EQ(batch->result().errors.size(), size_t{3});
    CHECK_EQ(store.revision(), uint64_t{0});
}

TEST_CASE("statepatch.batch.sequentialCasAndNoopRevision") {
    Store store;
    auto* batch = store.newBatch();
    batch->set("subject", "key", "1");
    batch->setExpected("subject", "key", "2", "1");
    REQUIRE(store.commit(batch));
    CHECK_EQ(store.get("subject", "key"), std::string("2"));
    CHECK_EQ(store.revision(), uint64_t{1});
    auto* noop = store.newBatch();
    noop->set("subject", "key", "2");
    noop->remove("subject", "absent");
    REQUIRE(store.commit(noop));
    CHECK_EQ(store.revision(), uint64_t{1});
    CHECK_EQ(noop->result().changedCount, 0);
}

TEST_CASE("statepatch.query.dirtyAndEventsAreDeterministic") {
    Store store;
    auto* batch = store.newBatch();
    batch->set("z", "b", "2");
    batch->set("a", "c", "3");
    batch->set("a", "a", "1");
    REQUIRE(store.commit(batch));
    CHECK_EQ(store.querySubjects(), 2);
    CHECK_EQ(store.queryAt(0), std::string("a"));
    CHECK_EQ(store.queryKeys("a"), 2);
    CHECK_EQ(store.queryAt(0), std::string("a"));
    CHECK_EQ(store.queryDirty(), 3);
    CHECK_EQ(store.dirtySubjectAt(0), std::string("a"));
    CHECK_EQ(store.dirtyKeyAt(0), std::string("a"));
    CHECK_EQ(store.eventCount(), 3);
    CHECK_EQ(store.eventAt(0)->subject, std::string("a"));
    CHECK_EQ(store.eventAt(0)->sequence, uint64_t{1});
    store.clearDirty();
    CHECK_EQ(store.queryDirty(), 0);
}

TEST_CASE("statepatch.remove.recordsOldValue") {
    Store store;
    auto* seed = store.newBatch();
    seed->set("s", "k", "{\"x\":1}");
    REQUIRE(store.commit(seed));
    store.clearEvents();
    auto* removal = store.newBatch();
    removal->removeExpected("s", "k", "{ \"x\": 1 }");
    REQUIRE(store.commit(removal));
    CHECK(!store.has("s", "k"));
    REQUIRE(store.eventCount() == 1);
    CHECK(store.eventAt(0)->removed);
    CHECK_EQ(store.eventAt(0)->oldJson, std::string("{\"x\":1}"));
    CHECK_EQ(store.eventAt(0)->newJson, std::string(""));
}

TEST_CASE("statepatch.snapshot.roundTripsAndRestoreIsTransactional") {
    Store store;
    auto* batch = store.newBatch();
    batch->set("b", "x", "[3,2,1]");
    batch->set("a", "x", "{\"z\":0,\"a\":true}");
    REQUIRE(store.commit(batch));
    const std::string snapshot = store.snapshotJson();
    Store             restored;
    REQUIRE(restored.restoreJson(snapshot));
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK_EQ(restored.revision(), uint64_t{1});
    const std::string before = restored.snapshotJson();
    CHECK(!restored.restoreJson("{\"version\":1}"));
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("statepatch.script.batchConflictQueryAndSnapshot") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.StatePatch();
        local store = module.newStore();
        local first = store.newBatch();
        first.set("actor-1", "rank", "3");
        first.set("actor-1", "loyalty", "0.75");
        if (store.commit(first)) {
            local conflict = store.newBatch();
            conflict.set("actor-2", "rank", "1");
            conflict.setExpected("actor-1", "rank", "4", "2");
            if (!store.commit(conflict) && conflict.result().errorAt(0).getCode() == "conflict" &&
                !store.has("actor-2", "rank")) {
                local copy = module.newStore();
                if (copy.restoreJson(store.snapshotJson()) && copy.queryKeys("actor-1") == 2)
                    result = copy.get("actor-1", "rank");
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("3"));
}
