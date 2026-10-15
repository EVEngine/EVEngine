#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "authority/Authority.h"
#include "common/Snapshot.h"
#include "definitions/Definitions.h"

#include <cstdint>
#include <string_view>
#include <utility>

using eve::ContentId;
using eve::DiagnosticCode;
using eve::LogicalId;
using eve::PersistentId;
using eve::Result;
using eve::SchemaVersion;
using eve::SimulationTick;
using eve::SnapshotEnvelope;
using eve::SnapshotHashProvider;
using eve::SnapshotMigrationChain;
using eve::Value;

namespace {

PersistentId persistentId(std::string_view text) {
    const auto parsed = PersistentId::parse(text);
    return parsed ? *parsed : PersistentId::nil();
}

LogicalId logicalId(std::string_view text) {
    const auto parsed = LogicalId::parse(text);
    return parsed ? *parsed : LogicalId{};
}

/** Test-only deterministic non-cryptographic digest provider. */
SnapshotHashProvider testHashProvider() {
    return [](std::string_view input) -> Result<ContentId> {
        std::uint64_t left  = 14695981039346656037ull;
        std::uint64_t right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left ^= byte;
            left *= 1099511628211ull;
            right ^= static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull;
            right *= 14029467366897019727ull;
        }
        ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)]     = static_cast<std::uint8_t>(left >> (56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)] = static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return Result<ContentId>::success(ContentId(bytes));
    };
}

SnapshotEnvelope makeEnvelope(std::string type, LogicalId schema, SchemaVersion version, PersistentId instanceId,
                              Value payload, const SnapshotHashProvider& hashProvider) {
    auto result = eve::makeSnapshotEnvelope(std::move(type), std::move(schema), version, instanceId, eve::Revision(7),
                                            SimulationTick(11), std::move(payload), hashProvider);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

}  // namespace

TEST_CASE("snapshot.envelope.canonicalHashRoundTripAndNoWallClock") {
    const auto    hash = testHashProvider();
    Value::Object payloadObject;
    payloadObject.emplace("z", Value(std::int64_t(2)));
    payloadObject.emplace("a", Value("stable"));
    const auto envelope =
        makeEnvelope("test.snapshot", logicalId("test:payload"), SchemaVersion(1),
                     persistentId("01234567-89ab-4cde-8fab-0123456789ab"), Value(std::move(payloadObject)), hash);

    auto hashInput = eve::snapshotHashInput(envelope);
    REQUIRE(hashInput.ok());
    CHECK(hashInput.value().find("createdAt") == std::string::npos);
    CHECK(hashInput.value().find("wallClock") == std::string::npos);
    CHECK(hashInput.value().find("\"a\"") < hashInput.value().find("\"z\""));

    auto serialized = eve::serializeSnapshotEnvelope(envelope);
    REQUIRE(serialized.ok());
    auto parsed = eve::parseSnapshotEnvelope(std::string_view(serialized.value()), hash);
    REQUIRE(parsed.ok());
    CHECK_EQ(parsed.value().type, envelope.type);
    CHECK_EQ(parsed.value().schema, envelope.schema);
    CHECK_EQ(parsed.value().schemaVersion, envelope.schemaVersion);
    CHECK_EQ(parsed.value().instanceId, envelope.instanceId);
    CHECK_EQ(parsed.value().revision, envelope.revision);
    CHECK_EQ(parsed.value().tick, envelope.tick);
    CHECK_EQ(parsed.value().contentHash, envelope.contentHash);
    CHECK_EQ(parsed.value().payload, envelope.payload);
}

TEST_CASE("snapshot.migration.rejectsUnknownNewVersionAndHashMismatch") {
    const auto             hash   = testHashProvider();
    const auto             schema = logicalId("test:payload");
    SnapshotMigrationChain chain;
    auto                   registration =
        chain.add(schema, SchemaVersion(0), SchemaVersion(1), [](const Value& payload) -> Result<Value> {
            const auto* object = payload.getIf<Value::Object>();
            if (!object)
                return Result<Value>::failure(
                    eve::Diagnostic::error(DiagnosticCode::ParseError, "payload must be an object"));
            Value::Object migrated = *object;
            migrated["version"]    = Value(std::int64_t(1));
            return Result<Value>::success(Value(std::move(migrated)));
        });
    REQUIRE(registration.ok());

    Value::Object oldPayload;
    oldPayload.emplace("version", Value(std::int64_t(0)));
    auto old      = makeEnvelope("test.snapshot", schema, SchemaVersion(0), PersistentId::nil(),
                                 Value(std::move(oldPayload)), hash);
    auto migrated = chain.migrate(std::move(old), SchemaVersion(1), hash);
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value().schemaVersion, SchemaVersion(1));
    CHECK_EQ(migrated.value().payload.getIf<Value::Object>()->at("version"), Value(std::int64_t(1)));

    Value::Object newPayload;
    newPayload.emplace("version", Value(std::int64_t(2)));
    auto newer    = makeEnvelope("test.snapshot", schema, SchemaVersion(2), PersistentId::nil(),
                                 Value(std::move(newPayload)), hash);
    auto rejected = chain.migrate(std::move(newer), SchemaVersion(1), hash);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.code()), static_cast<int>(eve::StatusCode::Failed));
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(DiagnosticCode::UnknownVersion));

    auto valid =
        makeEnvelope("test.snapshot", schema, SchemaVersion(1), PersistentId::nil(), Value(Value::Object{}), hash);
    valid.contentHash = ContentId::nil();
    auto mismatch     = eve::verifySnapshotEnvelope(valid, hash);
    CHECK(!mismatch.ok());
    CHECK_EQ(static_cast<int>(mismatch.error()->code()), static_cast<int>(DiagnosticCode::HashMismatch));
}

TEST_CASE("snapshot.consumers.authorityAndDefinitionsRoundTripAndMetadata") {
    const auto            hash        = testHashProvider();
    const auto            authorityId = persistentId("11111111-2222-4333-8444-555555555555");
    eve::authority::Store authority(authorityId);
    const auto            ruleId = authority.grant("actor", "scope", "read", "test", 3);
    REQUIRE(!ruleId.empty());
    auto authoritySnapshot = authority.snapshot(hash);
    REQUIRE(authoritySnapshot.ok());
    CHECK_EQ(authoritySnapshot.value().instanceId, authorityId);
    CHECK_EQ(authoritySnapshot.value().revision, eve::Revision(1));

    eve::authority::Store authorityCopy(authorityId);
    auto                  authorityRestored = authorityCopy.restoreSnapshot(authoritySnapshot.value(), hash);
    REQUIRE(authorityRestored.ok());
    CHECK(authorityCopy.can("actor", "scope", "read"));
    CHECK_EQ(authorityCopy.snapshot(hash).value().contentHash, authoritySnapshot.value().contentHash);

    const auto                           definitionsId = persistentId("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    eve::definitions::DefinitionRegistry definitions(definitionsId);
    REQUIRE(definitions.insert("unit", "tank", 1, "{\"speed\":4}").ok());
    auto definitionsSnapshot = definitions.snapshot(hash);
    REQUIRE(definitionsSnapshot.ok());
    CHECK_EQ(definitionsSnapshot.value().instanceId, definitionsId);

    eve::definitions::DefinitionRegistry definitionsCopy(definitionsId);
    auto definitionsRestored = definitionsCopy.restoreSnapshot(definitionsSnapshot.value(), hash);
    REQUIRE(definitionsRestored.ok());
    auto tank = definitionsCopy.resolve("unit", "tank");
    REQUIRE(tank.ok());
    CHECK_EQ(tank.value().get().json, std::string("{\"speed\":4}"));
}

TEST_CASE("snapshot.consumers.malformedPayloadAndUnknownVersionLeaveStateUnchanged") {
    const auto            hash        = testHashProvider();
    const auto            authorityId = persistentId("99999999-8888-4777-8666-555555555555");
    eve::authority::Store authority(authorityId);
    REQUIRE(!authority.grant("actor", "scope", "read", "before").empty());
    const std::string authorityBefore = authority.snapshotJson();

    Value::Object malformedAuthority;
    malformedAuthority.emplace("version", Value(std::int64_t(1)));
    auto malformedAuthorityEnvelope = makeEnvelope("authority.store", logicalId("authority:store"), SchemaVersion(1),
                                                   authorityId, Value(std::move(malformedAuthority)), hash);
    auto authorityFailure           = authority.restoreSnapshot(malformedAuthorityEnvelope, hash);
    CHECK(!authorityFailure.ok());
    CHECK_EQ(authority.snapshotJson(), authorityBefore);

    Value::Object newerAuthority;
    newerAuthority.emplace("version", Value(std::int64_t(2)));
    auto newerAuthorityEnvelope = makeEnvelope("authority.store", logicalId("authority:store"), SchemaVersion(2),
                                               authorityId, Value(std::move(newerAuthority)), hash);
    auto unknownAuthority       = authority.restoreSnapshot(newerAuthorityEnvelope, hash);
    CHECK(!unknownAuthority.ok());
    CHECK_EQ(static_cast<int>(unknownAuthority.error()->code()), static_cast<int>(DiagnosticCode::UnknownVersion));
    CHECK_EQ(authority.snapshotJson(), authorityBefore);

    const auto                           definitionsId = persistentId("12345678-1234-4123-8123-123456789012");
    eve::definitions::DefinitionRegistry definitions(definitionsId);
    REQUIRE(definitions.insert("unit", "before", 1, "{}").ok());
    const std::string definitionsBefore = definitions.snapshotJson();
    Value::Object     malformedDefinitions;
    malformedDefinitions.emplace("version", Value(std::int64_t(1)));
    auto malformedDefinitionsEnvelope =
        makeEnvelope("definitions.registry", logicalId("definitions:registry"), SchemaVersion(1), definitionsId,
                     Value(std::move(malformedDefinitions)), hash);
    auto definitionsFailure = definitions.restoreSnapshot(malformedDefinitionsEnvelope, hash);
    CHECK(!definitionsFailure.ok());
    CHECK_EQ(definitions.snapshotJson(), definitionsBefore);
}
