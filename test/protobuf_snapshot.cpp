#include "common/ProtobufSnapshot.h"

#include "common/SnapshotHash.h"
#include "snapshot_envelope.pb.h"
#include "zeroerr/unittest.h"

#include <string>

TEST_CASE("protobuf-snapshot.roundTripsTypedPayloadAndRejectsTampering") {
    const auto schema = eve::LogicalId::parse("sample:state");
    REQUIRE(schema.has_value());
    const auto identity = eve::PersistentId::parse("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    REQUIRE(identity.has_value());
    const auto                                   hash = eve::snapshotContentHashProvider();
    const eve::proto_support::BinarySnapshotData source{"sample.state",
                                                        *schema,
                                                        eve::SchemaVersion(3),
                                                        *identity,
                                                        eve::Revision(7),
                                                        eve::SimulationTick(9),
                                                        std::string("\0typed\xff", 7)};
    auto                                         wire = eve::proto_support::serializeBinarySnapshot(source, hash);
    REQUIRE(wire.ok());
    auto restored = eve::proto_support::parseBinarySnapshot(wire.value(), hash);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().type, source.type);
    CHECK_EQ(restored.value().schema, source.schema);
    CHECK_EQ(restored.value().schemaVersion, source.schemaVersion);
    CHECK_EQ(restored.value().instanceId, source.instanceId);
    CHECK_EQ(restored.value().revision, source.revision);
    CHECK_EQ(restored.value().tick, source.tick);
    CHECK_EQ(restored.value().payloadWire, source.payloadWire);

    eve::persistence::proto::BinarySnapshotEnvelope tampered;
    REQUIRE(tampered.ParseFromString(wire.value()));
    tampered.set_payload_wire("changed");
    std::string bytes;
    REQUIRE(tampered.SerializeToString(&bytes));
    auto rejected = eve::proto_support::parseBinarySnapshot(bytes, hash);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), eve::DiagnosticCode::HashMismatch);

    tampered.GetReflection()->MutableUnknownFields(&tampered)->AddVarint(99, 1);
    REQUIRE(tampered.SerializeToString(&bytes));
    CHECK(!eve::proto_support::parseBinarySnapshot(bytes, hash).ok());
}
