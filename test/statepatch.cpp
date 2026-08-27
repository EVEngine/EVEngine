#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "statepatch/StatePatch.h"
#include "StatePatchTestSupport.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::statepatch;

namespace {

}  // namespace

TEST_CASE("statepatch.batch.commitsCanonicalValuesAtomically") {
    Store store;
    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    CHECK(batch.view->set("entity-b", "health", "{ \"max\": 10, \"now\": 8 }"));
    CHECK(batch.view->set("entity-a", "active", "true"));
    REQUIRE(store.commit(batch.view.get()));
    CHECK_EQ(store.revision(), uint64_t{1});
    CHECK_EQ(store.get("entity-b", "health"), std::string("{\"max\":10,\"now\":8}"));
    CHECK_EQ(store.valueRevision("entity-a", "active"), uint64_t{1});
    CHECK_EQ(batch.view->result().changedCount, 2);
}

TEST_CASE("statepatch.batch.conflictProducesNoPartialWrites") {
    Store store;
    auto seed = eve::test_support::openStatePatchBatch(store);
    REQUIRE(seed.view.isBound());
    seed.view->set("one", "value", "1");
    REQUIRE(store.commit(seed.view.get()));
    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    batch.view->set("two", "value", "2");
    batch.view->setExpected("one", "value", "3", "99");
    CHECK(!store.commit(batch.view.get()));
    CHECK(!store.has("two", "value"));
    CHECK_EQ(store.get("one", "value"), std::string("1"));
    CHECK_EQ(store.revision(), uint64_t{1});
    REQUIRE(batch.view->result().errors.size() == 1);
    CHECK_EQ(batch.view->result().errors[0].code, std::string("conflict"));
}

TEST_CASE("statepatch.batch.validatesEverythingBeforeCommit") {
    Store store;
    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    CHECK(!batch.view->set("ok", "bad-json", "{"));
    batch.view->set("", "missing-subject", "1");
    batch.view->remove("ok", "");
    CHECK(!store.commit(batch.view.get()));
    CHECK_EQ(batch.view->result().errors.size(), size_t{3});
    CHECK_EQ(store.revision(), uint64_t{0});
}

TEST_CASE("statepatch.batch.sequentialCasAndNoopRevision") {
    Store store;
    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    batch.view->set("subject", "key", "1");
    batch.view->setExpected("subject", "key", "2", "1");
    REQUIRE(store.commit(batch.view.get()));
    CHECK_EQ(store.get("subject", "key"), std::string("2"));
    CHECK_EQ(store.revision(), uint64_t{1});
    auto noop = eve::test_support::openStatePatchBatch(store);
    REQUIRE(noop.view.isBound());
    noop.view->set("subject", "key", "2");
    noop.view->remove("subject", "absent");
    REQUIRE(store.commit(noop.view.get()));
    CHECK_EQ(store.revision(), uint64_t{1});
    CHECK_EQ(noop.view->result().changedCount, 0);
}

TEST_CASE("statepatch.query.dirtyAndEventsAreDeterministic") {
    Store store;
    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    batch.view->set("z", "b", "2");
    batch.view->set("a", "c", "3");
    batch.view->set("a", "a", "1");
    REQUIRE(store.commit(batch.view.get()));
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
    auto seed = eve::test_support::openStatePatchBatch(store);
    REQUIRE(seed.view.isBound());
    seed.view->set("s", "k", "{\"x\":1}");
    REQUIRE(store.commit(seed.view.get()));
    store.clearEvents();
    auto removal = eve::test_support::openStatePatchBatch(store);
    REQUIRE(removal.view.isBound());
    removal.view->removeExpected("s", "k", "{ \"x\": 1 }");
    REQUIRE(store.commit(removal.view.get()));
    CHECK(!store.has("s", "k"));
    REQUIRE(store.eventCount() == 1);
    CHECK(store.eventAt(0)->removed);
    CHECK_EQ(store.eventAt(0)->oldJson, std::string("{\"x\":1}"));
    CHECK_EQ(store.eventAt(0)->newJson, std::string(""));
}

TEST_CASE("statepatch.snapshot.roundTripsAndRestoreIsTransactional") {
    Store store;
    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    batch.view->set("b", "x", "[3,2,1]");
    batch.view->set("a", "x", "{\"z\":0,\"a\":true}");
    REQUIRE(store.commit(batch.view.get()));
    const std::string snapshot = store.snapshotJson();
    Store             restored;
    auto              restoredResult = restored.restoreJson(snapshot);
    REQUIRE(restoredResult.ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK_EQ(restored.revision(), uint64_t{1});
    const std::string before = restored.snapshotJson();
    auto              rejected = restored.restoreJson("{\"version\":1}");
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.error() != nullptr);
    CHECK_EQ(rejected.error()->code(), eve::DiagnosticCode::ParseError);
    CHECK_EQ(rejected.error()->path(), std::string("$.revision"));
    CHECK_EQ(rejected.error()->source(), std::string("statepatch.store.restoreJson"));
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("statepatch.script.batchConflictQueryAndSnapshot") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.StatePatch();
        local storeResult = module.newStore();
        local store = storeResult.ok ? storeResult.value : null;
        local firstResult = store != null ? store.newBatch() : { ok = false };
        local first = firstResult.ok ? firstResult.value : null;
        if (store != null && first != null && first.set("actor-1", "rank", "3").ok &&
            first.set("actor-1", "loyalty", "0.75").ok && store.commit(first)) {
            local conflictResult = store.newBatch();
            local conflict = conflictResult.ok ? conflictResult.value : null;
            if (conflict != null && conflict.set("actor-2", "rank", "1").ok &&
                conflict.setExpected("actor-1", "rank", "4", "2").ok &&
                !store.commit(conflict) && conflict.result().errorAt(0).getCode() == "conflict" &&
                !store.has("actor-2", "rank")) {
                local copyResult = module.newStore();
                local copy = copyResult.ok ? copyResult.value : null;
                local restoredResult = copy != null ? copy.restoreJson(store.snapshotJson()) : { ok = false };
                if (copy != null && restoredResult.ok && copy.queryKeys("actor-1") == 2)
                    result = copy.get("actor-1", "rank");
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("3"));
}
