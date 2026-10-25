#include "policyregistry/PolicyRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using eve::policyregistry::PolicyEvent;
using eve::policyregistry::PolicyRegistry;

// Registration contract: valid input publishes a canonical descriptor;
// malformed names, versions, kind or metadata do not publish a slot.
TEST_CASE("policyregistry.spec.registration_validation_and_canonical_metadata") {
    PolicyRegistry registry;
    auto inserted = registry.insert("movement", "ground", 2, 10, true, "builtin", "move.schema", "{\"z\":2,\"a\":1}");
    REQUIRE(inserted.ok());
    auto resolved = registry.resolve("movement", "ground");
    REQUIRE(resolved.ok());
    const auto& descriptor = resolved.value().get();
    CHECK_EQ(descriptor.version.value(), std::uint64_t{2});
    CHECK_EQ(descriptor.priority, 10);
    CHECK(descriptor.enabled);
    CHECK_EQ(descriptor.metadataJson, "{\"a\":1,\"z\":2}");
    CHECK(!registry.insert("movement", "ground", 1, 0, true, "script", "", "{}").ok());
    CHECK(!registry.insert("", "bad", 1, 0, true, "script", "", "{}").ok());
    CHECK(!registry.insert("movement", "", 1, 0, true, "script", "", "{}").ok());
    CHECK(!registry.insert("movement", "bad", 0, 0, true, "script", "", "{}").ok());
    CHECK(!registry.insert("movement", "bad", 1, 0, true, "remote", "", "{}").ok());
    CHECK(!registry.insert("movement", "bad", 1, 0, true, "script", "", "[]").ok());
    CHECK_EQ(registry.size(), 1);
    CHECK_EQ(registry.eventCount(), 1);
}

// Selection contract: only enabled policies compete; priority wins, then
// lexical name breaks ties. Unknown domains and disabled-only domains are empty.
TEST_CASE("policyregistry.spec.selection_and_sorted_enumeration") {
    PolicyRegistry registry;
    REQUIRE(registry.insert("target", "zeta", 1, 20, true, "script", "", "{}").ok());
    REQUIRE(registry.insert("target", "alpha", 1, 20, true, "batch", "", "{}").ok());
    REQUIRE(registry.insert("target", "disabled", 1, 99, false, "builtin", "", "{}").ok());
    REQUIRE(registry.insert("other", "first", 1, 0, false, "script", "", "{}").ok());
    REQUIRE(registry.select("target") != nullptr);
    CHECK_EQ(registry.select("target")->name, "alpha");
    CHECK(registry.select("other") == nullptr);
    CHECK(registry.select("missing") == nullptr);
    REQUIRE(registry.enable("target", "disabled", true).ok());
    CHECK_EQ(registry.select("target")->name, "disabled");
    REQUIRE(registry.enable("target", "disabled", false).ok());
    CHECK_EQ(registry.select("target")->name, "alpha");
    CHECK_EQ(registry.size(), 4);
    CHECK_EQ(registry.countDomain("target"), 3);
    CHECK_EQ(registry.at(0)->domain, "other");
    CHECK_EQ(registry.atDomain("target", 0)->name, "alpha");
    CHECK_EQ(registry.atDomain("target", 2)->name, "zeta");
    CHECK(registry.at(-1) == nullptr);
    CHECK(registry.atDomain("target", 3) == nullptr);
}

// Handle contract: each replacement, enabled toggle, and tombstone increments
// generation. A stale handle never resolves to a revived policy with same key.
TEST_CASE("policyregistry.spec.generation_staleness_and_tombstone") {
    PolicyRegistry registry;
    auto           first = registry.insert("economy", "tax", 1, 0, true, "script", "tax.v1", "{}");
    REQUIRE(first.ok());
    CHECK_EQ(first.value().generation.value(), std::uint64_t{1});
    auto noOp = registry.enable("economy", "tax", true);
    REQUIRE(noOp.ok());
    CHECK_EQ(noOp.code(), eve::StatusCode::NoOp);
    CHECK_EQ(noOp.value().generation.value(), std::uint64_t{1});
    auto disabled = registry.enable("economy", "tax", false);
    REQUIRE(disabled.ok());
    CHECK_EQ(disabled.value().generation.value(), std::uint64_t{2});
    CHECK(registry.isStale(first.value()));
    CHECK(!registry.resolveHandle(first.value()).ok());
    auto replaced = registry.replace("economy", "tax", 2, 3, true, "batch", "tax.v2", "{}");
    REQUIRE(replaced.ok());
    CHECK_EQ(replaced.value().generation.value(), std::uint64_t{3});
    auto removed = registry.remove("economy", "tax");
    REQUIRE(removed.ok());
    CHECK_EQ(removed.value().generation.value(), std::uint64_t{4});
    CHECK(registry.isTombstone("economy", "tax"));
    CHECK(!registry.resolve("economy", "tax").ok());
    auto revived = registry.insert("economy", "tax", 3, 0, true, "script", "tax.v3", "{}");
    REQUIRE(revived.ok());
    CHECK_EQ(revived.value().generation.value(), std::uint64_t{5});
    CHECK(!registry.resolveHandle(replaced.value()).ok());
}

// Event contract: successful mutations are sequenced; clearing retained events
// does not reset the next sequence. Observer exceptions surface as diagnostics
// without rolling back an already-applied mutation.
TEST_CASE("policyregistry.spec.events_and_observer_failure") {
    PolicyRegistry registry;
    auto           failing =
        registry.subscribe([](const PolicyEvent&) { throw std::runtime_error("injected observer failure"); });
    auto inserted = registry.insert("orders", "safe", 1, 0, true, "script", "", "{}");
    REQUIRE(inserted.ok());
    REQUIRE_EQ(inserted.diagnostics().size(), std::size_t{1});
    CHECK_EQ(inserted.diagnostics().front().code(), eve::DiagnosticCode::CallbackFailure);
    CHECK(registry.resolve("orders", "safe").ok());
    failing.dispose();
    REQUIRE(registry.enable("orders", "safe", false).ok());
    REQUIRE_EQ(registry.eventCount(), 2);
    CHECK_EQ(registry.eventAt(0)->name, "policy_registered");
    CHECK_EQ(registry.eventAt(1)->name, "policy_enabled");
    CHECK_EQ(registry.eventAt(1)->sequence.value(), std::uint64_t{2});
    registry.clearEvents();
    REQUIRE(registry.enable("orders", "safe", true).ok());
    REQUIRE_EQ(registry.eventCount(), 1);
    CHECK_EQ(registry.eventAt(0)->sequence.value(), std::uint64_t{3});
    CHECK(registry.eventAt(-1) == nullptr);
}

// Snapshot contract: serialization is deterministic; restore retains
// tombstones and rejects malformed input without partially changing state.
TEST_CASE("policyregistry.spec.snapshot_round_trip_and_atomic_rejection") {
    PolicyRegistry source;
    REQUIRE(source.insert("orders", "safe", 1, 5, true, "script", "orders.policy", "{\"label\":\"Safe\"}").ok());
    REQUIRE(source.insert("orders", "old", 1, 0, true, "builtin", "", "{}").ok());
    REQUIRE(source.remove("orders", "old").ok());
    const auto     json = source.snapshotJson();
    PolicyRegistry restored;
    REQUIRE(restored.restoreJson(json).ok());
    CHECK_EQ(restored.snapshotJson(), json);
    CHECK_EQ(restored.select("orders")->name, "safe");
    CHECK(restored.isTombstone("orders", "old"));
    auto revived = restored.insert("orders", "old", 2, 0, true, "batch", "", "{}");
    REQUIRE(revived.ok());
    CHECK_EQ(revived.value().generation.value(), std::uint64_t{3});
    const auto before = restored.snapshotJson();
    CHECK(!restored.restoreJson("{\"version\":1}").ok());
    CHECK_EQ(restored.snapshotJson(), before);
}

// Common envelope contract: a verified envelope transfers descriptor state
// into another registry without requiring per-entry mutation callbacks.
TEST_CASE("policyregistry.spec.verified_envelope_restore") {
    PolicyRegistry source;
    REQUIRE(source.insert("orders", "safe", 1, 1, false, "script", "", "{}").ok());
    const auto hash = [](std::string_view) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        bytes[0] = 0x42;
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
    auto envelope = source.snapshotEnvelopeJson(hash);
    REQUIRE(envelope.ok());
    PolicyRegistry target;
    REQUIRE(target.restoreSnapshotJson(envelope.value(), hash).ok());
    auto resolved = target.resolve("orders", "safe");
    REQUIRE(resolved.ok());
    CHECK(!resolved.value().get().enabled);
    CHECK_EQ(target.size(), 1);
}
}  // namespace
