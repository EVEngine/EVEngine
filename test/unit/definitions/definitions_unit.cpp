// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "definitions.cpp"
  ]
}
*/
// clang-format on

#include "definitions/DefinitionRegistryCapture.h"
#include "definitions/Definitions.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using eve::definitions::DefinitionEvent;
using eve::definitions::DefinitionRegistry;
}  // namespace

TEST_CASE("definitions.insert.canonicalizationAndValidationContract") {
    SUB_CASE("valid input receives first generation and canonical JSON") {
        DefinitionRegistry registry;
        auto               inserted = registry.insert("unit", "tank", 2, "{ \"speed\": 4, \"armor\": 9 }");
        REQUIRE(inserted.ok());
        CHECK_EQ(inserted.value().generation.value(), uint64_t{1});
        CHECK_EQ(registry.size(), 1);
        auto resolved = registry.resolve("unit", "tank");
        REQUIRE(resolved.ok());
        CHECK_EQ(resolved.value().get().version.value(), uint64_t{2});
        CHECK_EQ(resolved.value().get().json, std::string("{\"armor\":9,\"speed\":4}"));
    };
    SUB_CASE("invalid inputs and duplicate leave registry unchanged") {
        DefinitionRegistry registry;
        REQUIRE(registry.insert("unit", "tank", 1, "{}").ok());
        const auto before = registry.snapshotJson();
        CHECK(!registry.insert("unit", "tank", 2, "{}").ok());
        CHECK(!registry.insert("", "tank", 1, "{}").ok());
        CHECK(!registry.insert("unit", "", 1, "{}").ok());
        CHECK(!registry.insert("unit", "bad", 0, "{}").ok());
        CHECK(!registry.insert("unit", "bad", 1, "{").ok());
        CHECK_EQ(registry.snapshotJson(), before);
    };
}

TEST_CASE("definitions.identity.replaceRemoveAndReinsertContract") {
    SUB_CASE("replace invalidates old handle but reference resolves current value") {
        DefinitionRegistry registry;
        REQUIRE(registry.insert("unit", "scout", 1, "{\"speed\":5}").ok());
        const auto old = registry.handle("unit", "scout");
        REQUIRE(old.ok());
        const auto reference = registry.reference("unit", "scout");

        auto replaced = registry.replace("unit", "scout", 2, "{\"speed\":7}");
        REQUIRE(replaced.ok());
        CHECK_EQ(replaced.value().generation.value(), uint64_t{2});
        CHECK(registry.isStale(old.value()));
        const auto stale = registry.resolveHandle(old.value());
        REQUIRE(!stale.ok());
        CHECK_EQ(stale.error()->code(), eve::DiagnosticCode::StaleHandle);
        auto current =
            registry.resolve(std::string(reference.id().namespaceName()), std::string(reference.id().name()));
        REQUIRE(current.ok());
        CHECK_EQ(current.value().get().json, std::string("{\"speed\":7}"));
    };
    SUB_CASE("remove retains tombstone generation and reinsert advances it") {
        DefinitionRegistry registry;
        REQUIRE(registry.insert("unit", "scout", 1, "{}").ok());
        const auto removed = registry.remove("unit", "scout");
        REQUIRE(removed.ok());
        CHECK_EQ(removed.value().generation.value(), uint64_t{2});
        CHECK(registry.isTombstone("unit", "scout"));
        CHECK(!registry.resolve("unit", "scout").ok());
        const auto generation = registry.generationOf("unit", "scout");
        REQUIRE(generation.ok());
        CHECK_EQ(generation.value().value(), uint64_t{2});

        auto reinserted = registry.insert("unit", "scout", 3, "{}");
        REQUIRE(reinserted.ok());
        CHECK_EQ(reinserted.value().generation.value(), uint64_t{3});
        CHECK(!registry.isTombstone("unit", "scout"));
    };
}

TEST_CASE("definitions.enumeration_orders_type_then_id_and_filters_by_type") {
    DefinitionRegistry registry;
    REQUIRE(registry.insert("weapon", "cannon", 1, "{}").ok());
    REQUIRE(registry.insert("unit", "tank", 1, "{}").ok());
    REQUIRE(registry.insert("unit", "infantry", 1, "{}").ok());
    REQUIRE_EQ(registry.size(), 3);
    CHECK_EQ(registry.at(0)->type, std::string("unit"));
    CHECK_EQ(registry.at(0)->id, std::string("infantry"));
    CHECK_EQ(registry.at(1)->id, std::string("tank"));
    CHECK_EQ(registry.at(2)->type, std::string("weapon"));
    REQUIRE_EQ(registry.countType("unit"), 2);
    CHECK_EQ(registry.atType("unit", 0)->id, std::string("infantry"));
    CHECK_EQ(registry.atType("unit", 1)->id, std::string("tank"));
}

TEST_CASE("definitions.events_describe_each_mutation_and_sequence_survives_clear") {
    DefinitionRegistry registry;
    REQUIRE(registry.insert("policy", "tax", 1, "{}").ok());
    REQUIRE(registry.replace("policy", "tax", 2, "{}").ok());
    REQUIRE(registry.remove("policy", "tax").ok());
    REQUIRE_EQ(registry.eventCount(), 3);
    CHECK_EQ(registry.eventAt(0)->name, std::string("definition_reloaded"));
    CHECK_EQ(registry.eventAt(0)->sequence.value(), uint64_t{1});
    CHECK_EQ(registry.eventAt(1)->generation.value(), uint64_t{2});
    CHECK_EQ(registry.eventAt(2)->name, std::string("definition_removed"));
    CHECK_EQ(registry.eventAt(2)->sequence.value(), uint64_t{3});

    registry.clearEvents();
    CHECK_EQ(registry.eventCount(), 0);
    REQUIRE(registry.insert("policy", "tax", 3, "{}").ok());
    REQUIRE_EQ(registry.eventCount(), 1);
    CHECK_EQ(registry.eventAt(0)->sequence.value(), uint64_t{4});
}

TEST_CASE("definitions.observer.subscriptionAndFailureContract") {
    SUB_CASE("subscription changes take effect on next notification") {
        DefinitionRegistry       registry;
        std::vector<std::string> notifications;
        eve::Subscription        first;
        eve::Subscription        later;
        first = registry.subscribe([&](const DefinitionEvent& event) {
            notifications.push_back("first:" + event.id);
            first.dispose();
            later =
                registry.subscribe([&](const DefinitionEvent& next) { notifications.push_back("later:" + next.id); });
        });
        REQUIRE(registry.insert("unit", "scout", 1, "{}").ok());
        CHECK_EQ(notifications, std::vector<std::string>({"first:scout"}));
        REQUIRE(registry.replace("unit", "scout", 2, "{}").ok());
        CHECK_EQ(notifications, std::vector<std::string>({"first:scout", "later:scout"}));
    };
    SUB_CASE("observer exception is reported after committed mutation") {
        DefinitionRegistry registry;
        auto               subscription =
            registry.subscribe([](const DefinitionEvent&) { throw std::runtime_error("injected callback failure"); });
        const auto result = registry.insert("unit", "scout", 1, "{}");
        REQUIRE(result.ok());
        CHECK_EQ(result.code(), eve::StatusCode::Applied);
        REQUIRE_EQ(result.diagnostics().size(), std::size_t{1});
        CHECK_EQ(result.diagnostics().front().code(), eve::DiagnosticCode::CallbackFailure);
        CHECK(registry.resolve("unit", "scout").ok());
    };
}

TEST_CASE("definitions.snapshot_preserves_tombstones_and_rejects_invalid_schema_atomically") {
    DefinitionRegistry original;
    REQUIRE(original.insert("unit", "tank", 1, "{\"b\":2,\"a\":1}").ok());
    REQUIRE(original.insert("unit", "old", 1, "{}").ok());
    REQUIRE(original.remove("unit", "old").ok());
    const std::string snapshot = original.snapshotJson();

    DefinitionRegistry restored;
    REQUIRE(restored.restoreJson(snapshot).ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    const auto tank = restored.resolve("unit", "tank");
    REQUIRE(tank.ok());
    CHECK_EQ(tank.value().get().json, std::string("{\"a\":1,\"b\":2}"));
    const auto revived = restored.insert("unit", "old", 2, "{}");
    REQUIRE(revived.ok());
    CHECK_EQ(revived.value().generation.value(), uint64_t{3});
    const auto before = restored.snapshotJson();
    CHECK(!restored.restoreJson("{\"version\":1}").ok());
    CHECK_EQ(restored.snapshotJson(), before);
    const auto unknownField = snapshot.substr(0, snapshot.size() - 1) + ",\"unexpected\":true}";
    CHECK(!restored.restoreJson(unknownField).ok());
    CHECK_EQ(restored.snapshotJson(), before);
}

namespace test_definitions_capture {
// Test-only deterministic digest; no security or collision-strength claim.
eve::Result<eve::ContentId> digest(std::string_view text) {
    eve::ContentId::Bytes bytes{};
    std::uint64_t         state = 14695981039346656037ULL;
    for (unsigned char byte : text) state = (state ^ byte) * 1099511628211ULL;
    for (auto& byte : bytes) {
        byte  = static_cast<std::uint8_t>(state);
        state = (state ^ byte) * 1099511628211ULL;
    }
    return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
}
TEST_CASE("definitions.capture.exactMetadataTombstonesHistoryAndIndependentObserverScope") {
    const auto identity = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000d11");
    auto       original = std::make_unique<DefinitionRegistry>(identity);
    original->insert("unit", "live", 2, R"({"health":77})").expect("actual content");
    original->insert("unit", "retired", 1, "{}").expect("future tombstone");
    original->remove("unit", "retired").expect("actual tombstone");
    auto envelope = original->snapshot(digest).takeValue();
    auto imported = eve::makeSnapshotEnvelope(envelope.type, envelope.schema, envelope.schemaVersion, identity,
                                              eve::Revision(88), eve::SimulationTick(99), envelope.payload, digest)
                        .takeValue();
    original->restoreSnapshot(imported, digest).expect("actual non-default registry metadata");
    const auto expected      = eve::serializeSnapshotEnvelope(imported).takeValue();
    const auto content       = original->snapshotJson();
    int        notifications = 0;
    auto       subscription  = original->subscribe([&](const DefinitionEvent&) { ++notifications; });
    auto       frozen        = original->capture().takeValue();
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(frozen.seal(digest).takeValue()).takeValue(), expected);
    auto wire = eve::serializeSnapshotEnvelope(frozen.seal(digest).takeValue()).takeValue();
    frozen =
        eve::definitions::DefinitionRegistryCapture::parse(eve::parseSnapshotEnvelope(wire, digest).takeValue(), digest)
            .takeValue();
    auto first  = DefinitionRegistry::prepareRestored(frozen).takeValue();
    auto second = DefinitionRegistry::prepareRestored(frozen).takeValue();
    REQUIRE_EQ(notifications, 0);
    REQUIRE_EQ(first->snapshotJson(), content);
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(first->snapshot(digest).takeValue()).takeValue(), expected);
    auto revived = first->insert("unit", "retired", 3, "{}").takeValue();
    REQUIRE_EQ(revived.generation.value(), std::uint64_t(3));
    REQUIRE_EQ(first->snapshot(digest).takeValue().revision.value(), std::uint64_t(89));
    REQUIRE_EQ(first->snapshot(digest).takeValue().tick.value(), std::uint64_t(99));
    REQUIRE(second->isTombstone("unit", "retired"));
    REQUIRE_EQ(second->snapshotJson(), content);
    REQUIRE_EQ(notifications, 0);
    original->replace("unit", "live", 3, R"({"health":1})").expect("source mutation after capture");
    REQUIRE_EQ(notifications, 1);
    original.reset();
    subscription.dispose();
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(frozen.seal(digest).takeValue()).takeValue(), expected);
    auto moved = std::move(frozen);
    REQUIRE(!frozen.seal(digest).ok());
    REQUIRE(!DefinitionRegistry::prepareRestored(frozen).ok());
    auto afterRetirement = DefinitionRegistry::prepareRestored(moved).takeValue();
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(afterRetirement->snapshot(digest).takeValue()).takeValue(), expected);
    eve::SnapshotHashProvider rejecting = [](std::string_view) {
        return eve::Result<eve::ContentId>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected digest failure"));
    };
    REQUIRE(!moved.seal(rejecting).ok());
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(moved.seal(digest).takeValue()).takeValue(), expected);
}
}  // namespace test_definitions_capture

namespace test_definitions_capture {
TEST_CASE("definitions.capture.fileAdmissionPreservesLegacyMigrationAndRejectsCorruptHistory") {
    using namespace eve;
    const auto         id = *PersistentId::parse("01900000-0000-7000-8000-000000000d13");
    DefinitionRegistry source(id);
    source.insert("unit", "fighter", 1, R"({"health":7})").expect("actual definition");
    source.replace("unit", "fighter", 2, R"({"health":9})").expect("actual revision history");
    const auto base = source.snapshot(digest).takeValue();
    for (int mode = 0; mode < 4; ++mode) {
        auto  payload = base.payload;
        auto& root    = *payload.getIf<Value::Object>();
        if (mode == 0) root.emplace("unknown", true);
        if (mode == 1) root.at("nextEventSequence") = "1";
        if (mode == 2)
            root.at("generations").getIf<Value::Array>()->front().getIf<Value::Object>()->at("generation") = "0";
        if (mode == 3) root.at("events").getIf<Value::Array>()->back().getIf<Value::Object>()->at("sequence") = "1";
        auto bad = makeSnapshotEnvelope(base.type, base.schema, base.schemaVersion, id, base.revision, base.tick,
                                        std::move(payload), digest)
                       .takeValue();
        REQUIRE(!definitions::DefinitionRegistryCapture::parse(std::move(bad), digest).ok());
    }
    auto oldPayload                                  = base.payload;
    oldPayload.getIf<Value::Object>()->at("version") = std::int64_t(0);
    auto legacy = makeSnapshotEnvelope(base.type, base.schema, SchemaVersion(0), id, base.revision, base.tick,
                                       std::move(oldPayload), digest)
                      .takeValue();
    auto admitted = definitions::DefinitionRegistryCapture::parse(std::move(legacy), digest).takeValue();
    CHECK_EQ(serializeSnapshotEnvelope(admitted.seal(digest).takeValue()).takeValue(),
             serializeSnapshotEnvelope(base).takeValue());
    auto future = makeSnapshotEnvelope(base.type, base.schema, SchemaVersion(2), id, base.revision, base.tick,
                                       base.payload, digest)
                      .takeValue();
    REQUIRE(!definitions::DefinitionRegistryCapture::parse(std::move(future), digest).ok());
    DefinitionRegistry anonymous;
    auto               empty =
        definitions::DefinitionRegistryCapture::parse(anonymous.snapshot(digest).takeValue(), digest).takeValue();
    auto restored = DefinitionRegistry::prepareRestored(empty).takeValue();
    CHECK_EQ(restored->size(), 0);
    CHECK_EQ(serializeSnapshotEnvelope(restored->snapshot(digest).takeValue()).takeValue(),
             serializeSnapshotEnvelope(anonymous.snapshot(digest).takeValue()).takeValue());
}
TEST_CASE("definitions.capture.digestCallbackMayRetireTheSourceCapture") {
    using namespace eve;
    DefinitionRegistry source;
    source.insert("unit", "fighter", 1, "{}").expect("actual definition");
    std::optional<definitions::DefinitionRegistryCapture> saved(source.capture().takeValue());
    const auto                 expected = serializeSnapshotEnvelope(saved->seal(digest).takeValue()).takeValue();
    const SnapshotHashProvider retiring = [&](std::string_view input) {
        saved.reset();
        return digest(input);
    };
    auto envelope = saved->seal(retiring).takeValue();
    REQUIRE(!saved);
    auto read = definitions::DefinitionRegistryCapture::parse(std::move(envelope), digest).takeValue();
    CHECK_EQ(serializeSnapshotEnvelope(read.seal(digest).takeValue()).takeValue(), expected);
    const SnapshotHashProvider unavailable = [](std::string_view) {
        return Result<ContentId>::failure(Diagnostic::error(DiagnosticCode::Failed, "digest unavailable"));
    };
    REQUIRE(!definitions::DefinitionRegistryCapture::parse(read.seal(digest).takeValue(), unavailable).ok());
    auto retry = DefinitionRegistry::prepareRestored(read).takeValue();
    CHECK_EQ(retry->snapshotJson(), source.snapshotJson());
}
}  // namespace test_definitions_capture
