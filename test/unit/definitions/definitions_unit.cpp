#include "zeroerr/assert.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "definitions/Definitions.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <string>

using namespace eve::definitions;

TEST_CASE("definitions.registerCanonicalizeAndResolve") {
    DefinitionRegistry registry;
    REQUIRE(registry.insert("unit", "tank", 2, "{ \"speed\": 4, \"armor\": 9 }").ok());
    auto value = registry.resolve("unit", "tank");
    REQUIRE(value.ok());
    CHECK_EQ(value.value().get().version.value(), uint64_t{2});
    CHECK_EQ(value.value().get().generation.value(), uint64_t{1});
    CHECK_EQ(value.value().get().json, std::string("{\"armor\":9,\"speed\":4}"));
    CHECK(!registry.insert("unit", "tank", 3, "{}").ok());
    CHECK(!registry.insert("", "tank", 1, "{}").ok());
    CHECK(!registry.insert("unit", "bad", 0, "{}").ok());
    CHECK(!registry.insert("unit", "bad", 1, "{").ok());
}

TEST_CASE("definitions.handlesInvalidateButReferencesReload") {
    DefinitionRegistry registry;
    REQUIRE(registry.insert("unit", "scout", 1, "{\"speed\":5}").ok());
    auto oldHandleResult = registry.handle("unit", "scout");
    REQUIRE(oldHandleResult.ok());
    const DefinitionHandle oldHandle = std::move(oldHandleResult).takeValue();
    const DefinitionRef    reference = registry.reference("unit", "scout");
    REQUIRE(registry.resolveHandle(oldHandle).ok());

    REQUIRE(registry.replace("unit", "scout", 2, "{\"speed\":7}").ok());
    CHECK(!registry.resolveHandle(oldHandle).ok());
    auto current = registry.resolve(std::string(reference.id().namespaceName()), std::string(reference.id().name()));
    REQUIRE(current.ok());
    CHECK_EQ(current.value().get().version.value(), uint64_t{2});
    CHECK_EQ(current.value().get().generation.value(), uint64_t{2});

    REQUIRE(registry.remove("unit", "scout").ok());
    CHECK(!registry.resolve(std::string(reference.id().namespaceName()), std::string(reference.id().name())).ok());
    REQUIRE(registry.insert("unit", "scout", 3, "{}").ok());
    auto revived = registry.resolve(std::string(reference.id().namespaceName()), std::string(reference.id().name()));
    REQUIRE(revived.ok());
    CHECK_EQ(revived.value().get().generation.value(), uint64_t{4});
}

TEST_CASE("definitions.canonicalRegistryApiKeepsTombstones") {
    DefinitionRegistry registry;
    auto               inserted = registry.insert("unit", "scout", 1, "{}");
    REQUIRE(inserted.ok());
    const auto first = std::move(inserted).takeValue();
    CHECK_EQ(first.generation.value(), uint64_t{1});

    auto replaced = registry.replace("unit", "scout", 2, "{\"speed\":7}");
    REQUIRE(replaced.ok());
    const auto second = std::move(replaced).takeValue();
    CHECK_EQ(second.generation.value(), uint64_t{2});
    CHECK(registry.isStale(first));
    auto stale = registry.resolveHandle(first);
    CHECK(!stale.ok());
    CHECK_EQ(stale.error()->code(), eve::DiagnosticCode::StaleHandle);

    auto removed = registry.remove("unit", "scout");
    REQUIRE(removed.ok());
    const auto tombstone = std::move(removed).takeValue();
    CHECK_EQ(tombstone.generation.value(), uint64_t{3});
    CHECK(registry.isTombstone("unit", "scout"));
    auto generation = registry.generationOf("unit", "scout");
    REQUIRE(generation.ok());
    CHECK_EQ(generation.value().value(), uint64_t{3});

    auto revived = registry.insert("unit", "scout", 3, "{}");
    REQUIRE(revived.ok());
    CHECK_EQ(std::move(revived).takeValue().generation.value(), uint64_t{4});
}

TEST_CASE("definitions.enumerationIsTypeThenIdSorted") {
    DefinitionRegistry registry;
    REQUIRE(registry.insert("weapon", "cannon", 1, "{}").ok());
    REQUIRE(registry.insert("unit", "tank", 1, "{}").ok());
    REQUIRE(registry.insert("unit", "infantry", 1, "{}").ok());
    CHECK_EQ(registry.size(), 3);
    CHECK_EQ(registry.countType("unit"), 2);
    CHECK_EQ(registry.at(0)->type, std::string("unit"));
    CHECK_EQ(registry.at(0)->id, std::string("infantry"));
    CHECK_EQ(registry.atType("unit", 1)->id, std::string("tank"));
    CHECK_EQ(registry.at(2)->type, std::string("weapon"));
}

TEST_CASE("definitions.eventsDescribeReloadAndRemoval") {
    DefinitionRegistry registry;
    REQUIRE(registry.insert("policy", "tax", 1, "{}").ok());
    REQUIRE(registry.replace("policy", "tax", 2, "{}").ok());
    REQUIRE(registry.remove("policy", "tax").ok());
    CHECK_EQ(registry.eventCount(), 3);
    CHECK_EQ(registry.eventAt(0)->name, std::string("definition_reloaded"));
    CHECK_EQ(registry.eventAt(1)->generation.value(), uint64_t{2});
    CHECK_EQ(registry.eventAt(2)->name, std::string("definition_removed"));
    CHECK_EQ(registry.eventAt(2)->sequence.value(), uint64_t{3});
    CHECK_EQ(registry.eventAt(2)->generation.value(), uint64_t{3});
    registry.clearEvents();
    REQUIRE(registry.insert("policy", "tax", 3, "{}").ok());
    CHECK_EQ(registry.eventAt(0)->sequence.value(), uint64_t{4});
}

TEST_CASE("definitions.subscriptionSupportsReentrantReloadObservers") {
    DefinitionRegistry       registry;
    std::vector<std::string> notifications;
    eve::Subscription        first;
    eve::Subscription        replacement;

    first = registry.subscribe([&](const DefinitionEvent &event) {
        notifications.push_back(event.name + ":" + event.id);
        first.dispose();
        replacement = registry.subscribe(
            [&](const DefinitionEvent &next) { notifications.push_back("replacement:" + next.name + ":" + next.id); });
    });

    REQUIRE(registry.insert("unit", "scout", 1, "{}").ok());
    CHECK_EQ(notifications, std::vector<std::string>({"definition_reloaded:scout"}));
    CHECK_EQ(registry.eventCount(), 1);

    REQUIRE(registry.replace("unit", "scout", 2, "{}").ok());
    CHECK_EQ(notifications,
             std::vector<std::string>({"definition_reloaded:scout", "replacement:definition_reloaded:scout"}));
}

TEST_CASE("definitions.mutationResultPreservesObserverWarning") {
    DefinitionRegistry registry;
    auto               subscription = registry.subscribe(
        [](const DefinitionEvent &) { throw std::runtime_error("injected definition observer failure"); });

    auto result = registry.insert("unit", "scout", 1, "{}");
    REQUIRE(result.ok());
    CHECK_EQ(result.code(), eve::StatusCode::Applied);
    REQUIRE_EQ(result.diagnostics().size(), std::size_t{1});
    CHECK_EQ(result.diagnostics().front().code(), eve::DiagnosticCode::CallbackFailure);
    CHECK(registry.resolve("unit", "scout").ok());
}

TEST_CASE("definitions.snapshotRoundTripsAndRestoreIsTransactional") {
    DefinitionRegistry original;
    REQUIRE(original.insert("unit", "tank", 1, "{\"b\":2,\"a\":1}").ok());
    REQUIRE(original.insert("unit", "obsolete", 1, "{}").ok());
    REQUIRE(original.remove("unit", "obsolete").ok());
    const std::string snapshot = original.snapshotJson();

    DefinitionRegistry restored;
    REQUIRE(restored.restoreJson(snapshot).ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    auto restoredTank = restored.resolve("unit", "tank");
    REQUIRE(restoredTank.ok());
    CHECK_EQ(restoredTank.value().get().json, std::string("{\"a\":1,\"b\":2}"));
    REQUIRE(restored.insert("unit", "obsolete", 2, "{}").ok());
    auto restoredObsolete = restored.resolve("unit", "obsolete");
    REQUIRE(restoredObsolete.ok());
    CHECK_EQ(restoredObsolete.value().get().generation.value(), uint64_t{3});

    const std::string before = restored.snapshotJson();
    CHECK(!restored.restoreJson("{\"version\":1}").ok());
    CHECK_EQ(restored.snapshotJson(), before);

    const std::string unknownField   = snapshot.substr(0, snapshot.size() - 1) + ",\"unexpected\":true}";
    auto              schemaRejected = restored.restoreJson(unknownField);
    CHECK(!schemaRejected.ok());
    CHECK_EQ(restored.snapshotJson(), before);
}

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_definitions_protobuf)
#include "PrivateTestScope.h"
#include "definitions/Definitions.h"

#include "common/SnapshotHash.h"
#include "definitions_registry.pb.h"
#include "zeroerr/unittest.h"

#include <string>
#include <utility>
namespace test_definitions_protobuf {
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
    auto *generation = invalid.add_generations();
    generation->set_type("unit");
    generation->set_id("bad");
    generation->set_generation(1);
    auto *definition = invalid.add_definitions();
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
}  // namespace test_definitions_protobuf
#endif
