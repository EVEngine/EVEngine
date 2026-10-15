#include "common/SnapshotHash.h"
#include "statepatch/StatePatch.h"

#include "statepatch_store.pb.h"
#include "zeroerr/unittest.h"

#include <string>

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
