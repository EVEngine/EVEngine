#include "StatePatchTestSupport.h"
#include "statepatch/StatePatch.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <string>

namespace {
using eve::statepatch::Store;

auto batchFor(Store& store) {
    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    return batch;
}
}  // namespace

TEST_CASE("statepatch.commit.canonicalizationValidationConflictAndCasContract") {
    SUB_CASE("canonical JSON and one revision per batch") {
        Store store;
        auto  batch = batchFor(store);
        REQUIRE(batch.view->set("z", "health", "{ \"max\": 10, \"now\": 8 }"));
        REQUIRE(batch.view->set("a", "enabled", "true"));
        const auto result = store.commit(batch.view.get());
        REQUIRE(result.ok());
        CHECK_EQ(result.value().changedCount, 2);
        CHECK_EQ(result.value().revisionBefore, uint64_t{0});
        CHECK_EQ(result.value().revisionAfter, uint64_t{1});
        CHECK_EQ(store.get("z", "health"), std::string("{\"max\":10,\"now\":8}"));
        CHECK_EQ(store.valueRevision("z", "health"), uint64_t{1});
        CHECK_EQ(store.valueRevision("a", "enabled"), uint64_t{1});
        CHECK_EQ(store.eventCount(), 2);
        CHECK_EQ(store.eventAt(0)->subject, std::string("a"));
        CHECK_EQ(store.eventAt(1)->subject, std::string("z"));
    };
    SUB_CASE("null and invalid operations leave state unchanged") {
        Store store;
        CHECK(!store.commit(nullptr).ok());
        auto batch = batchFor(store);
        CHECK(!batch.view->set("good", "bad_json", "{"));
        REQUIRE(batch.view->set("", "missing_subject", "1"));
        REQUIRE(batch.view->remove("good", ""));
        const auto result = store.commit(batch.view.get());
        CHECK(!result.ok());
        CHECK_EQ(batch.view->result().errors.size(), std::size_t{3});
        CHECK_EQ(store.revision(), uint64_t{0});
        CHECK_EQ(store.querySubjects(), 0);
        CHECK_EQ(store.eventCount(), 0);
    };
    SUB_CASE("conflict rolls back earlier staged operations") {
        Store store;
        auto  seed = batchFor(store);
        REQUIRE(seed.view->set("one", "value", "1"));
        REQUIRE(store.commit(seed.view.get()).ok());
        const auto before = store.snapshotJson();

        auto batch = batchFor(store);
        REQUIRE(batch.view->set("two", "value", "2"));
        REQUIRE(batch.view->setExpected("one", "value", "3", "99"));
        CHECK(!store.commit(batch.view.get()).ok());
        CHECK_EQ(store.snapshotJson(), before);
        CHECK_EQ(batch.view->result().errors.size(), std::size_t{1});
        CHECK_EQ(batch.view->result().errors.front().code, std::string("conflict"));
    };
    SUB_CASE("compare and swap reads earlier operation in same batch") {
        Store store;
        auto  batch = batchFor(store);
        REQUIRE(batch.view->set("subject", "key", "1"));
        REQUIRE(batch.view->setExpected("subject", "key", "2", "1"));
        const auto result = store.commit(batch.view.get());
        REQUIRE(result.ok());
        CHECK_EQ(result.value().changedCount, 1);
        CHECK_EQ(store.get("subject", "key"), std::string("2"));
        CHECK_EQ(store.revision(), uint64_t{1});

        auto noop = batchFor(store);
        REQUIRE(noop.view->set("subject", "key", "2"));
        REQUIRE(noop.view->remove("subject", "absent"));
        const auto noopResult = store.commit(noop.view.get());
        REQUIRE(noopResult.ok());
        CHECK_EQ(noopResult.code(), eve::StatusCode::NoOp);
        CHECK_EQ(noopResult.value().changedCount, 0);
        CHECK_EQ(store.revision(), uint64_t{1});
    };
}

TEST_CASE("statepatch.query.lexical_order_dirty_markers_and_event_sequence") {
    Store store;
    auto  batch = batchFor(store);
    REQUIRE(batch.view->set("z", "b", "2"));
    REQUIRE(batch.view->set("a", "c", "3"));
    REQUIRE(batch.view->set("a", "a", "1"));
    REQUIRE(store.commit(batch.view.get()).ok());

    REQUIRE_EQ(store.querySubjects(), 2);
    CHECK_EQ(store.queryAt(0), std::string("a"));
    CHECK_EQ(store.queryAt(1), std::string("z"));
    REQUIRE_EQ(store.queryKeys("a"), 2);
    CHECK_EQ(store.queryAt(0), std::string("a"));
    CHECK_EQ(store.queryAt(1), std::string("c"));
    REQUIRE_EQ(store.queryDirty(), 3);
    CHECK_EQ(store.dirtySubjectAt(0), std::string("a"));
    CHECK_EQ(store.dirtyKeyAt(0), std::string("a"));
    CHECK_EQ(store.dirtyKeyAt(1), std::string("c"));
    CHECK_EQ(store.dirtySubjectAt(2), std::string("z"));
    REQUIRE_EQ(store.eventCount(), 3);
    CHECK_EQ(store.eventAt(0)->sequence, uint64_t{1});
    CHECK_EQ(store.eventAt(2)->sequence, uint64_t{3});

    store.clearDirty();
    CHECK_EQ(store.queryDirty(), 0);
    CHECK_EQ(store.eventCount(), 3);
    store.clearEvents();
    CHECK_EQ(store.eventCount(), 0);
    auto later = batchFor(store);
    REQUIRE(later.view->set("z", "b", "4"));
    REQUIRE(store.commit(later.view.get()).ok());
    REQUIRE_EQ(store.eventCount(), 1);
    CHECK_EQ(store.eventAt(0)->sequence, uint64_t{4});
}

TEST_CASE("statepatch.remove.records_old_value_and_removes_value_revision") {
    Store store;
    auto  seed = batchFor(store);
    REQUIRE(seed.view->set("subject", "key", "{\"x\":1}"));
    REQUIRE(store.commit(seed.view.get()).ok());
    store.clearEvents();

    auto removal = batchFor(store);
    REQUIRE(removal.view->removeExpected("subject", "key", "{ \"x\": 1 }"));
    REQUIRE(store.commit(removal.view.get()).ok());
    CHECK(!store.has("subject", "key"));
    CHECK_EQ(store.valueRevision("subject", "key"), uint64_t{0});
    REQUIRE_EQ(store.eventCount(), 1);
    CHECK(store.eventAt(0)->removed);
    CHECK_EQ(store.eventAt(0)->oldJson, std::string("{\"x\":1}"));
    CHECK_EQ(store.eventAt(0)->newJson, std::string(""));
}

TEST_CASE("statepatch.snapshot.round_trip_preserves_values_and_rejects_partial_restore") {
    Store original;
    auto  batch = batchFor(original);
    REQUIRE(batch.view->set("b", "x", "[3,2,1]"));
    REQUIRE(batch.view->set("a", "x", "{\"z\":0,\"a\":true}"));
    REQUIRE(original.commit(batch.view.get()).ok());
    const std::string snapshot = original.snapshotJson();

    Store restored;
    REQUIRE(restored.restoreJson(snapshot).ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK_EQ(restored.get("a", "x"), std::string("{\"a\":true,\"z\":0}"));
    const auto rejected = restored.restoreJson("{\"version\":1}");
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.error() != nullptr);
    CHECK_EQ(rejected.error()->code(), eve::DiagnosticCode::ParseError);
    CHECK_EQ(rejected.error()->path(), std::string("$.revision"));
    CHECK_EQ(restored.snapshotJson(), snapshot);
}
