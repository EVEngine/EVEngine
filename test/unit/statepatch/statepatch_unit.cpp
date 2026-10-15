#include "zeroerr/assert.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "StatePatchTestSupport.h"
#include "common/Module.h"
#include "statepatch/StatePatch.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::statepatch;

namespace {}  // namespace

TEST_CASE("statepatch.batch.commitsCanonicalValuesAtomically") {
    Store store;
    auto  batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    CHECK(batch.view->set("entity-b", "health", "{ \"max\": 10, \"now\": 8 }"));
    CHECK(batch.view->set("entity-a", "active", "true"));
    REQUIRE(store.commit(batch.view.get()).ok());
    CHECK_EQ(store.revision(), uint64_t{1});
    CHECK_EQ(store.get("entity-b", "health"), std::string("{\"max\":10,\"now\":8}"));
    CHECK_EQ(store.valueRevision("entity-a", "active"), uint64_t{1});
    CHECK_EQ(batch.view->result().changedCount, 2);
}

TEST_CASE("statepatch.batch.conflictProducesNoPartialWrites") {
    Store store;
    auto  seed = eve::test_support::openStatePatchBatch(store);
    REQUIRE(seed.view.isBound());
    seed.view->set("one", "value", "1");
    REQUIRE(store.commit(seed.view.get()).ok());
    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    batch.view->set("two", "value", "2");
    batch.view->setExpected("one", "value", "3", "99");
    CHECK(!store.commit(batch.view.get()).ok());
    CHECK(!store.has("two", "value"));
    CHECK_EQ(store.get("one", "value"), std::string("1"));
    CHECK_EQ(store.revision(), uint64_t{1});
    REQUIRE(batch.view->result().errors.size() == 1);
    CHECK_EQ(batch.view->result().errors[0].code, std::string("conflict"));
}

TEST_CASE("statepatch.batch.validatesEverythingBeforeCommit") {
    Store store;
    auto  batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    CHECK(!batch.view->set("ok", "bad-json", "{"));
    batch.view->set("", "missing-subject", "1");
    batch.view->remove("ok", "");
    CHECK(!store.commit(batch.view.get()).ok());
    CHECK_EQ(batch.view->result().errors.size(), size_t{3});
    CHECK_EQ(store.revision(), uint64_t{0});
}

TEST_CASE("statepatch.batch.sequentialCasAndNoopRevision") {
    Store store;
    auto  batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    batch.view->set("subject", "key", "1");
    batch.view->setExpected("subject", "key", "2", "1");
    REQUIRE(store.commit(batch.view.get()).ok());
    CHECK_EQ(store.get("subject", "key"), std::string("2"));
    CHECK_EQ(store.revision(), uint64_t{1});
    auto noop = eve::test_support::openStatePatchBatch(store);
    REQUIRE(noop.view.isBound());
    noop.view->set("subject", "key", "2");
    noop.view->remove("subject", "absent");
    REQUIRE(store.commit(noop.view.get()).ok());
    CHECK_EQ(store.revision(), uint64_t{1});
    CHECK_EQ(noop.view->result().changedCount, 0);
}

TEST_CASE("statepatch.query.dirtyAndEventsAreDeterministic") {
    Store store;
    auto  batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    batch.view->set("z", "b", "2");
    batch.view->set("a", "c", "3");
    batch.view->set("a", "a", "1");
    REQUIRE(store.commit(batch.view.get()).ok());
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
    auto  seed = eve::test_support::openStatePatchBatch(store);
    REQUIRE(seed.view.isBound());
    seed.view->set("s", "k", "{\"x\":1}");
    REQUIRE(store.commit(seed.view.get()).ok());
    store.clearEvents();
    auto removal = eve::test_support::openStatePatchBatch(store);
    REQUIRE(removal.view.isBound());
    removal.view->removeExpected("s", "k", "{ \"x\": 1 }");
    REQUIRE(store.commit(removal.view.get()).ok());
    CHECK(!store.has("s", "k"));
    REQUIRE(store.eventCount() == 1);
    CHECK(store.eventAt(0)->removed);
    CHECK_EQ(store.eventAt(0)->oldJson, std::string("{\"x\":1}"));
    CHECK_EQ(store.eventAt(0)->newJson, std::string(""));
}

TEST_CASE("statepatch.snapshot.roundTripsAndRestoreIsTransactional") {
    Store store;
    auto  batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    batch.view->set("b", "x", "[3,2,1]");
    batch.view->set("a", "x", "{\"z\":0,\"a\":true}");
    REQUIRE(store.commit(batch.view.get()).ok());
    const std::string snapshot = store.snapshotJson();
    Store             restored;
    auto              restoredResult = restored.restoreJson(snapshot);
    REQUIRE(restoredResult.ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK_EQ(restored.revision(), uint64_t{1});
    const std::string before   = restored.snapshotJson();
    auto              rejected = restored.restoreJson("{\"version\":1}");
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.error() != nullptr);
    CHECK_EQ(rejected.error()->code(), eve::DiagnosticCode::ParseError);
    CHECK_EQ(rejected.error()->path(), std::string("$.revision"));
    CHECK_EQ(rejected.error()->source(), std::string("statepatch.store.restoreJson"));
    CHECK_EQ(restored.snapshotJson(), before);
}

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_statepatch_protobuf)
#include "PrivateTestScope.h"
#include "common/SnapshotHash.h"
#include "statepatch/StatePatch.h"

#include "statepatch_store.pb.h"
#include "zeroerr/unittest.h"

#include <string>
namespace test_statepatch_protobuf {
TEST_CASE("statepatch-protobuf.binaryJsonAndEnvelopeRoundTrip") {
    eve::statepatch::Store      source;
    eve::statepatch::PatchBatch batch;
    REQUIRE(batch.set("subject", "large", "9223372036854775807"));
    REQUIRE(batch.set("subject", "nested", "{\"flag\":true,\"items\":[null,1.5]}"));
    REQUIRE(source.commit(&batch).ok());

    auto binary = source.snapshotBinary();
    REQUIRE(binary.ok());
    eve::statepatch::proto::StatePatchStoreSave saved;
    REQUIRE(saved.ParseFromString(binary.value()));
    CHECK_EQ(saved.schema(), std::string("statepatch:store"));
    CHECK_EQ(saved.version(), 2u);
    CHECK_EQ(saved.values_size(), 2);

    eve::statepatch::Store fromBinary;
    REQUIRE(fromBinary.restoreBinary(binary.value()).ok());
    CHECK_EQ(fromBinary.get("subject", "large"), std::string("9223372036854775807"));
    CHECK_EQ(fromBinary.snapshotJson(), source.snapshotJson());

    eve::statepatch::Store fromJson;
    REQUIRE(fromJson.restoreJson(source.snapshotJson()).ok());
    CHECK_EQ(fromJson.snapshotJson(), source.snapshotJson());

    const auto hash     = eve::snapshotContentHashProvider();
    auto       envelope = source.snapshot(hash);
    REQUIRE(envelope.ok());
    CHECK_EQ(envelope.value().schemaVersion.value(), 2u);
    eve::statepatch::Store fromEnvelope;
    REQUIRE(fromEnvelope.restoreSnapshot(envelope.value(), hash).ok());
    CHECK_EQ(fromEnvelope.snapshotJson(), source.snapshotJson());
}

TEST_CASE("statepatch-protobuf.legacyJsonAndRejectedBinaryKeepStateAtomic") {
    eve::statepatch::Store store;
    const std::string      legacy =
        R"({"version":1,"revision":"1","nextSequence":"2","values":[{"subject":"old","key":"n","revision":"1","value":7}],"dirty":[["old","n"]]})";
    REQUIRE(store.restoreJson(legacy).ok());
    CHECK_EQ(store.get("old", "n"), std::string("7"));
    const std::string before = store.snapshotJson();

    auto legacyValue = eve::Value::fromJson(legacy);
    REQUIRE(legacyValue.ok());
    const auto schema = eve::LogicalId::parse("statepatch:store");
    REQUIRE(schema.has_value());
    const auto hash = eve::snapshotContentHashProvider();
    auto       legacyEnvelope =
        eve::makeSnapshotEnvelope("statepatch.store", *schema, eve::SchemaVersion(1), eve::PersistentId{},
                                  eve::Revision(1), eve::SimulationTick{}, std::move(legacyValue).takeValue(), hash);
    REQUIRE(legacyEnvelope.ok());
    eve::statepatch::Store fromLegacyEnvelope;
    REQUIRE(fromLegacyEnvelope.restoreSnapshot(legacyEnvelope.value(), hash).ok());
    CHECK_EQ(fromLegacyEnvelope.snapshotJson(), before);

    eve::statepatch::proto::StatePatchStoreSave malformed;
    malformed.set_schema("statepatch:store");
    malformed.set_version(2);
    malformed.set_revision(1);
    malformed.set_next_sequence(2);
    for (int index = 0; index < 2; ++index) {
        auto* entry = malformed.add_values();
        entry->set_subject("same");
        entry->set_key("key");
        entry->set_revision(1);
        entry->mutable_value()->set_int_value(index);
    }
    std::string wire;
    REQUIRE(malformed.SerializeToString(&wire));
    CHECK(!store.restoreBinary(wire).ok());
    CHECK_EQ(store.snapshotJson(), before);

    malformed.clear_values();
    malformed.GetReflection()->MutableUnknownFields(&malformed)->AddVarint(99, 1);
    REQUIRE(malformed.SerializeToString(&wire));
    CHECK(!store.restoreBinary(wire).ok());
    CHECK_EQ(store.snapshotJson(), before);
}
}  // namespace test_statepatch_protobuf
#endif
