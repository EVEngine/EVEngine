// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "authority.cpp",
    "script_ownership.cpp"
  ]
}
*/
// clang-format on

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_authority)
#include "PrivateTestScope.h"
#include "authority/Authority.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>
#include <string>

namespace test_authority_unit {
using namespace eve::authority;

std::string id(eve::Result<std::string>&& result) { return std::move(result).expect("valid authority rule"); }

// Decision contract: absent rules deny with an explanation; higher priority
// wins, equal priority prefers deny, and wildcards are exact dimension matches.
TEST_CASE("authority.spec.decision_priority_deny_and_wildcards") {
    Store store;
    auto  empty = store.explain("a", "scope", "edit");
    CHECK(!empty.allowed);
    CHECK_EQ(empty.reason, "no_matching_rule");
    CHECK(empty.winningRuleId.empty());
    const auto broad = id(store.grant("*", "*", "read", "role:all", 1));
    CHECK(store.can("a", "scope", "read"));
    CHECK(!store.can("a", "scope", "write"));
    const auto localDeny = id(store.deny("a", "scope", "read", "policy:local", 1));
    CHECK(!store.can("a", "scope", "read"));
    CHECK_EQ(store.explain("a", "scope", "read").winningRuleId, localDeny);
    CHECK(store.can("b", "scope", "read"));
    const auto override = id(store.grant("a", "scope", "read", "override", 2));
    CHECK(store.can("a", "scope", "read"));
    CHECK_EQ(store.explain("a", "scope", "read").winningRuleId, override);
    REQUIRE(store.find(broad) != nullptr);
    CHECK_EQ(store.find(broad)->source, "role:all");
}

// Lifecycle contract: injected simulation time expires finite rules and emits
// an expiration event; revocation by source/dimension removes only its matches.
TEST_CASE("authority.spec.expiry_and_targeted_revocation") {
    Store      store;
    const auto temporary = id(store.grant("a", "s1", "read", "temp", 1, .5));
    id(store.grant("a", "s2", "write", "permanent", 1));
    id(store.deny("b", "s1", "read", "policy", 1));
    store.update(.25);
    CHECK(store.find(temporary) != nullptr);
    store.update(.25);
    CHECK(store.find(temporary) == nullptr);
    REQUIRE_EQ(store.eventCount(), 4);
    CHECK_EQ(eventKindName(store.eventAt(3)->kind), "expired");
    CHECK_EQ(store.eventAt(3)->reason, "duration_elapsed");
    CHECK_EQ(store.revokeBySource("policy"), 1);
    CHECK_EQ(store.revokeByActor("a"), 1);
    CHECK_EQ(store.revokeByScope("s1"), 0);
    CHECK(!store.can("a", "s2", "write"));
}

// Query contract: reverse dimensions return rules in creation order and a
// later query replaces the borrowed result view.
TEST_CASE("authority.spec.query_dimensions_and_bounds") {
    Store store;
    const auto first  = id(store.grant("a", "s2", "read", "x"));
    const auto second = id(store.grant("a", "s1", "write", "y"));
    const auto third  = id(store.deny("b", "s1", "read", "z"));
    REQUIRE_EQ(store.queryActor("a"), 2);
    CHECK_EQ(store.queryAt(0)->id, first);
    CHECK_EQ(store.queryAt(1)->id, second);
    REQUIRE_EQ(store.queryScope("s1"), 2);
    CHECK_EQ(store.queryAt(0)->id, second);
    CHECK_EQ(store.queryAt(1)->id, third);
    REQUIRE_EQ(store.queryCapability("read"), 2);
    CHECK_EQ(store.queryAt(0)->id, first);
    CHECK_EQ(store.queryAt(1)->id, third);
    CHECK(store.queryAt(-1) == nullptr);
    CHECK(store.queryAt(2) == nullptr);
}

// Input contract: empty required fields, nonfinite duration, missing id and
// negative event capacity reject without adding rules or events.
TEST_CASE("authority.spec.invalid_input_is_nonmutating") {
    Store store;
    CHECK(!store.grant("", "scope", "read", "source").ok());
    CHECK(!store.deny("actor", "", "read", "source").ok());
    CHECK(!store.grant("actor", "scope", "", "source").ok());
    CHECK(!store.grant("actor", "scope", "read", "source", 1, std::numeric_limits<double>::infinity()).ok());
    CHECK(!store.revoke("missing", "gone").ok());
    CHECK(!store.setEventCapacity(-1).ok());
    CHECK_EQ(store.query("", "", ""), 0);
    REQUIRE_EQ(store.eventCount(), 0);
}

// Retention and snapshot contract: a positive capacity trims oldest events;
// snapshots round trip deterministically, while malformed restore is atomic.
TEST_CASE("authority.spec.event_capacity_and_snapshot_restore") {
    Store source;
    REQUIRE(source.setEventCapacity(2).ok());
    id(source.grant("a", "s", "read", "role", 3));
    id(source.deny("a", "s", "write", "policy", 4, 5.0));
    id(source.grant("*", "s", "inspect", "role", 1));
    CHECK_EQ(source.eventCount(), 2);
    const auto snapshot = source.snapshotJson();
    Store restored;
    REQUIRE(restored.restoreJson(snapshot).ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK(restored.can("a", "s", "read"));
    CHECK(!restored.can("a", "s", "write"));
    CHECK(restored.can("other", "s", "inspect"));
    const auto before   = restored.snapshotJson();
    auto       rejected = restored.restoreJson("{\"version\":1}");
    CHECK(!rejected.ok());
    CHECK_EQ(restored.snapshotJson(), before);
    CHECK(!restored.restoreJson("{").ok());
    CHECK_EQ(restored.snapshotJson(), before);
}
}  // namespace test_authority_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_script_ownership)
#include "PrivateTestScope.h"
#include "common/SquirrelOwnership.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <type_traits>

namespace test_script_ownership_unit {
struct Tag {};
struct Item {
    int  value     = 7;
    int* destroyed = nullptr;
    ~Item() {
        if (destroyed) ++*destroyed;
    }
};

// RuntimePin stores an address into its owner; the slot store must remain at
// a stable address throughout the pin's lifetime.
static_assert(!std::is_move_constructible_v<eve::script::detail::RuntimeSlotStore>);
static_assert(!std::is_move_assignable_v<eve::script::detail::RuntimeSlotStore>);

// Handle contract: erase invalidates the old generation; slot reuse cannot
// revive a stale handle, and clear changes the owner epoch.
TEST_CASE("scriptOwnership.spec.generation_and_owner_epoch") {
    eve::script::RuntimeObjectRegistry<Item, Tag> registry;
    auto                                          created = registry.emplace(std::make_unique<Item>());
    REQUIRE(created.ok());
    const auto first = std::move(created).takeValue();
    CHECK(registry.resolve(first).isBound());
    CHECK(!registry.isStale(first));
    REQUIRE(registry.erase(first).ok());
    CHECK(registry.isStale(first));
    CHECK(!registry.resolve(first).isBound());
    auto replacement = registry.emplace(std::make_unique<Item>());
    REQUIRE(replacement.ok());
    const auto next = std::move(replacement).takeValue();
    CHECK_EQ(next.handle.index(), first.handle.index());
    CHECK(next.handle.generation() != first.handle.generation());
    const auto epoch = registry.ownerEpoch();
    registry.clear();
    CHECK(registry.isStale(next));
    eve::script::RuntimeObjectRegistry<Item, Tag> reloaded;
    CHECK(reloaded.ownerEpoch() != epoch);
    CHECK(reloaded.isStale(next));
}

// Pin contract: erasing the owner invalidates borrowing immediately but
// retains the payload until the final pin releases. Pinned slots are not reused.
TEST_CASE("scriptOwnership.spec.erase_defers_pinned_destruction") {
    int                                           destroyed = 0;
    eve::script::RuntimeObjectRegistry<Item, Tag> registry;
    auto                                          item = std::make_unique<Item>();
    item->destroyed = &destroyed;
    auto created                                       = registry.emplace(std::move(item));
    REQUIRE(created.ok());
    const auto handle = std::move(created).takeValue();
    auto       pin    = registry.pin(handle);
    REQUIRE(pin.ok());
    CHECK_EQ(pin.value().get()->value, 7);
    REQUIRE(registry.erase(handle).ok());
    CHECK(registry.isStale(handle));
    CHECK(!registry.resolve(handle).isBound());
    CHECK_EQ(destroyed, 0);
    auto replacement = registry.emplace(std::make_unique<Item>());
    REQUIRE(replacement.ok());
    CHECK(replacement.value().handle.index() != handle.handle.index());
    auto moved = std::move(pin).takeValue();
    CHECK(moved.isBound());
    moved = eve::script::RuntimePin<Item, Tag>{};
    CHECK_EQ(destroyed, 1);
}

// Clear contract: unpinned payloads die immediately; a pinned payload lives
// until release even though every old handle is stale.
TEST_CASE("scriptOwnership.spec.clear_defers_only_pinned_payload") {
    int                                           heldDestroyed = 0, freeDestroyed = 0;
    eve::script::RuntimeObjectRegistry<Item, Tag> registry;
    auto                                          heldItem = std::make_unique<Item>();
    heldItem->destroyed                                    = &heldDestroyed;
    auto heldRef = std::move(registry.emplace(std::move(heldItem))).takeValue();
    auto pin     = registry.pin(heldRef);
    REQUIRE(pin.ok());
    auto freeItem       = std::make_unique<Item>();
    freeItem->destroyed = &freeDestroyed;
    auto freeRef        = std::move(registry.emplace(std::move(freeItem))).takeValue();
    registry.clear();
    CHECK_EQ(freeDestroyed, 1);
    CHECK_EQ(heldDestroyed, 0);
    CHECK(registry.isStale(heldRef));
    CHECK(registry.isStale(freeRef));
    pin.value() = eve::script::RuntimePin<Item, Tag>{};
    CHECK_EQ(heldDestroyed, 1);
}
}  // namespace test_script_ownership_unit
#endif
