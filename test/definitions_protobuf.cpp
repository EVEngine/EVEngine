#include "definitions/Definitions.h"

#include "common/SnapshotHash.h"
#include "definitions_registry.pb.h"
#include "zeroerr/unittest.h"

#include <string>
#include <utility>

TEST_CASE("definitions-protobuf.binaryJsonAndEnvelopeRoundTrip") {
    eve::definitions::DefinitionRegistry source;
    REQUIRE(source.insert("unit", "scout", 1, "{\"hp\":9223372036854775807}").ok());
    REQUIRE(source.replace("unit", "scout", 2, "{\"hp\":7,\"tags\":[\"fast\"]}").ok());
    REQUIRE(source.insert("unit", "retired", 1, "null").ok());
    REQUIRE(source.remove("unit", "retired").ok());

    auto binary = source.snapshotBinary();
    REQUIRE(binary.ok());
    eve::definitions::proto::DefinitionRegistrySave saved;
    REQUIRE(saved.ParseFromString(binary.value()));
    CHECK_EQ(saved.schema(), std::string("definitions:registry"));
    CHECK_EQ(saved.version(), 2u);
    CHECK_EQ(saved.definitions_size(), 1);
    CHECK_EQ(saved.generations_size(), 2);
    CHECK_EQ(saved.events_size(), 4);

    eve::definitions::DefinitionRegistry fromBinary;
    REQUIRE(fromBinary.restoreBinary(binary.value()).ok());
    CHECK_EQ(fromBinary.snapshotJson(), source.snapshotJson());
    eve::definitions::DefinitionRegistry fromJson;
    REQUIRE(fromJson.restoreJson(source.snapshotJson()).ok());
    CHECK_EQ(fromJson.snapshotJson(), source.snapshotJson());

    const auto hash     = eve::snapshotContentHashProvider();
    auto       envelope = source.snapshot(hash);
    REQUIRE(envelope.ok());
    CHECK_EQ(envelope.value().schemaVersion.value(), 2u);
    eve::definitions::DefinitionRegistry fromEnvelope;
    REQUIRE(fromEnvelope.restoreSnapshot(envelope.value(), hash).ok());
    CHECK_EQ(fromEnvelope.snapshotJson(), source.snapshotJson());
}

TEST_CASE("definitions-protobuf.legacyEnvelopeAndInvalidBinaryAreAtomic") {
    const std::string legacy =
        R"({"version":1,"definitions":[{"type":"unit","id":"legacy","version":1,"generation":"1","json":{"v":7}}],"generations":[{"type":"unit","id":"legacy","generation":"1"}],"events":[],"nextEventSequence":"1"})";
    eve::definitions::DefinitionRegistry restored;
    REQUIRE(restored.restoreJson(legacy).ok());
    const auto before = restored.snapshotJson();
    const auto schema = eve::LogicalId::parse("definitions:registry");
    REQUIRE(schema.has_value());
    auto legacyValue = eve::Value::fromJson(legacy);
    REQUIRE(legacyValue.ok());
    const auto hash = eve::snapshotContentHashProvider();
    auto       envelope =
        eve::makeSnapshotEnvelope("definitions.registry", *schema, eve::SchemaVersion(1), eve::PersistentId{},
                                  eve::Revision{}, eve::SimulationTick{}, std::move(legacyValue).takeValue(), hash);
    REQUIRE(envelope.ok());
    eve::definitions::DefinitionRegistry fromEnvelope;
    REQUIRE(fromEnvelope.restoreSnapshot(envelope.value(), hash).ok());
    CHECK_EQ(fromEnvelope.snapshotJson(), before);

    eve::definitions::proto::DefinitionRegistrySave invalid;
    invalid.set_schema("definitions:registry");
    invalid.set_version(2);
    invalid.set_next_event_sequence(1);
    auto* generation = invalid.add_generations();
    generation->set_type("unit");
    generation->set_id("bad");
    generation->set_generation(1);
    auto* definition = invalid.add_definitions();
    definition->set_type("unit");
    definition->set_id("bad");
    definition->set_version(1);
    definition->set_generation(1);
    definition->mutable_body();
    std::string bytes;
    REQUIRE(invalid.SerializeToString(&bytes));
    CHECK(!restored.restoreBinary(bytes).ok());
    CHECK_EQ(restored.snapshotJson(), before);

    invalid.clear_definitions();
    invalid.GetReflection()->MutableUnknownFields(&invalid)->AddVarint(99, 1);
    REQUIRE(invalid.SerializeToString(&bytes));
    CHECK(!restored.restoreBinary(bytes).ok());
    CHECK_EQ(restored.snapshotJson(), before);
}
