#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "common/ProtobufSnapshot.h"
#include "common/ScriptComponentSnapshot.h"
#include "common/ScriptComponentStore.h"
#include "common/SnapshotHash.h"
#include "script_component_store.pb.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>

namespace test_script_component_protobuf {

constexpr const char* kScriptComponentProtobufScript = R"SQ(
class Position extends eve.Component { x = 1.5; y = 2.5 }
class Probe extends eve.Entity { pos = Position }
function makeProbe() {
    if (!eve.component("Position", 1, Position).ok) return false
    local e = Probe.create()
    probeId <- e.getId()
    probe <- e
    return true
}
)SQ";

UnitSciptTest(ScriptComponentProtobufTest, kScriptComponentProtobufScript);

TEST_CASE_FIXTURE(ScriptComponentProtobufTest, "ScriptECS.protobufSnapshotRoundTripsAndRejectsCorruptionAtomically") {
    REQUIRE(callScript("makeProbe"));
    const auto                         entityId = static_cast<std::uint64_t>(vm.find("probeId").toInt());
    const eve::ScriptComponentStoreKey key{entityId, "Position", eve::SchemaVersion(1)};
    REQUIRE(eve::setScriptComponentField(key, "x", eve::Value::integer(9223372036854775807LL)).ok());

    const auto identity = eve::PersistentId::parse("22222222-2222-7222-8222-222222222222");
    REQUIRE(identity.has_value());
    const auto hash = eve::snapshotContentHashProvider();
    auto saved = eve::serializeScriptComponentSnapshotBinary(*identity, eve::Revision(3), eve::SimulationTick(5), hash);
    REQUIRE(saved.ok());
    eve::clearScriptComponentStore();
    REQUIRE(eve::restoreScriptComponentSnapshotBinary(saved.value(), hash).ok());
    REQUIRE_EQ(eve::scriptComponentField(key, "x").value().asInt(), 9223372036854775807LL);
    auto repeated =
        eve::serializeScriptComponentSnapshotBinary(*identity, eve::Revision(3), eve::SimulationTick(5), hash);
    REQUIRE(repeated.ok());
    REQUIRE_EQ(repeated.value(), saved.value());

    auto corrupt = saved.value();
    corrupt.back() ^= 1;
    REQUIRE(!eve::restoreScriptComponentSnapshotBinary(corrupt, hash).ok());
    REQUIRE(!eve::restoreScriptComponentSnapshotBinary("malformed protobuf", hash).ok());

    auto envelope = eve::proto_support::parseBinarySnapshot(saved.value(), hash);
    REQUIRE(envelope.ok());
    eve::script::proto::ScriptComponentStoreSave payload;
    REQUIRE(payload.ParseFromString(envelope.value().payloadWire));
    REQUIRE_EQ(payload.components_size(), 1);
    auto* fields = payload.mutable_components(0)->mutable_fields();
    fields->GetReflection()->MutableUnknownFields(fields)->AddVarint(100, 1);
    REQUIRE(payload.SerializeToString(&envelope.value().payloadWire));
    auto unknown = eve::proto_support::serializeBinarySnapshot(envelope.value(), hash);
    REQUIRE(unknown.ok());
    REQUIRE(!eve::restoreScriptComponentSnapshotBinary(unknown.value(), hash).ok());

    envelope.value().schemaVersion = eve::SchemaVersion(3);
    auto future                    = eve::proto_support::serializeBinarySnapshot(envelope.value(), hash);
    REQUIRE(future.ok());
    auto rejectedFuture = eve::restoreScriptComponentSnapshotBinary(future.value(), hash);
    REQUIRE(!rejectedFuture.ok());
    REQUIRE_EQ(rejectedFuture.error()->code(), eve::DiagnosticCode::UnknownVersion);
    REQUIRE_EQ(eve::scriptComponentField(key, "x").value().asInt(), 9223372036854775807LL);
}

}  // namespace test_script_component_protobuf
