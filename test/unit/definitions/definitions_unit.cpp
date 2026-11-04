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
