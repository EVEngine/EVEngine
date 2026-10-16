#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_authority)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "authority/Authority.h"
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// authority.cpp
namespace test_authority_unit {
using namespace eve::authority;

namespace {

std::string requireId(eve::Result<std::string>&& result) {
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

}  // namespace

TEST_CASE("authority.decision.priorityThenDenyThenCreationOrder") {
    Store      store;
    const auto grant = requireId(store.grant("actor:1", "scope:1", "edit", "role:a", 10));
    const auto deny  = requireId(store.deny("actor:1", "scope:1", "edit", "policy:a", 10));
    CHECK(!store.can("actor:1", "scope:1", "edit"));
    CHECK_EQ(store.explain("actor:1", "scope:1", "edit").winningRuleId, deny);

    const auto elevated = requireId(store.grant("actor:1", "scope:1", "edit", "override", 11));
    const auto decision = store.explain("actor:1", "scope:1", "edit");
    CHECK(decision.allowed);
    CHECK_EQ(decision.winningRuleId, elevated);
    CHECK_EQ(decision.reason, std::string("granted"));
    CHECK_EQ(store.find(grant)->source, std::string("role:a"));
}

TEST_CASE("authority.decision.defaultDenyIsExplainable") {
    Store      store;
    const auto result = store.explain("unknown", "scope", "read");
    CHECK(!result.allowed);
    CHECK_EQ(result.reason, std::string("no_matching_rule"));
    CHECK(result.winningRuleId.empty());
}

TEST_CASE("authority.decision.wildcardMatchesAndLosesToExactDeny") {
    Store store;
    requireId(store.grant(std::string(kWildcard), std::string(kWildcard), "read", "role:global", 1));
    CHECK(store.can("actor:1", "scope:1", "read"));
    CHECK(store.can("actor:2", "scope:9", "read"));
    CHECK(!store.can("actor:1", "scope:1", "write"));

    const auto deny = requireId(store.deny("actor:1", "scope:1", "read", "policy:local", 1));
    CHECK(!store.can("actor:1", "scope:1", "read"));
    CHECK_EQ(store.explain("actor:1", "scope:1", "read").winningRuleId, deny);
    CHECK(store.can("actor:2", "scope:1", "read"));
}

TEST_CASE("authority.lifecycle.durationRevokeAndEvents") {
    Store      store;
    const auto temporary = requireId(store.grant("a", "s", "c", "temporary", 1, 0.5));
    requireId(store.deny("b", "s", "c", "policy", 1));
    store.update(0.5);
    CHECK(store.find(temporary) == nullptr);
    REQUIRE(store.eventCount() == 3);
    CHECK_EQ(eventKindName(store.eventAt(2)->kind), std::string("expired"));
    CHECK_EQ(store.eventAt(2)->reason, std::string("duration_elapsed"));
    CHECK_EQ(store.revokeBySource("policy"), 1);
    CHECK_EQ(eventKindName(store.eventAt(3)->kind), std::string("revoked"));
}

TEST_CASE("authority.lifecycle.revokeByDimensionAndEventCapacity") {
    Store store;
    requireId(store.grant("actor:a", "scope:1", "read", "src", 1));
    requireId(store.grant("actor:a", "scope:2", "write", "src", 1));
    requireId(store.grant("actor:b", "scope:1", "read", "other", 1));
    CHECK_EQ(store.revokeByActor("actor:a", "actor_gone"), 2);
    CHECK(!store.can("actor:a", "scope:1", "read"));
    CHECK(store.can("actor:b", "scope:1", "read"));
    CHECK_EQ(store.revokeByScope("scope:1", "scope_gone"), 1);
    CHECK(!store.can("actor:b", "scope:1", "read"));

    requireId(store.grant("x", "y", "z", "cap-src", 1));
    requireId(store.deny("x", "y", "z", "cap-src", 2));
    CHECK_EQ(store.revokeByCapability("z", "capability_retired"), 2);

    auto capacity = store.setEventCapacity(2);
    REQUIRE(capacity.ok());
    CHECK_EQ(store.eventCapacity(), 2);
    CHECK_EQ(store.eventCount(), 2);
    CHECK(!store.setEventCapacity(-1).ok());
}

TEST_CASE("authority.query.reverseDimensionsAreDeterministic") {
    Store      store;
    const auto first  = requireId(store.grant("a", "s2", "read", "x"));
    const auto second = requireId(store.grant("a", "s1", "write", "y"));
    const auto third  = requireId(store.deny("b", "s1", "read", "z"));
    REQUIRE(store.queryActor("a") == 2);
    CHECK_EQ(store.queryAt(0)->id, first);
    CHECK_EQ(store.queryAt(1)->id, second);
    REQUIRE(store.queryScope("s1") == 2);
    CHECK_EQ(store.queryAt(0)->id, second);
    CHECK_EQ(store.queryAt(1)->id, third);
    REQUIRE(store.queryCapability("read") == 2);
    CHECK_EQ(store.queryAt(0)->id, first);
    CHECK_EQ(store.queryAt(1)->id, third);
}

TEST_CASE("authority.snapshot.restoreIsDeterministicAndTransactional") {
    Store original;
    requireId(original.grant("a", "s", "read", "role", 3));
    requireId(original.deny("a", "s", "write", "policy", 4, 5.0));
    requireId(original.grant(std::string(kWildcard), "s", "inspect", "role", 1));
    original.update(1.0);
    const auto snapshot = original.snapshotJson();

    Store restored;
    auto  restoredResult = restored.restoreJson(snapshot);
    REQUIRE(restoredResult.ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK(restored.can("a", "s", "read"));
    CHECK(!restored.can("a", "s", "write"));
    CHECK(restored.can("other", "s", "inspect"));
    const auto before   = restored.snapshotJson();
    auto       rejected = restored.restoreJson("{\"version\":1}");
    CHECK(!rejected.ok());
    const auto* diagnostic = rejected.error();
    REQUIRE(diagnostic != nullptr);
    CHECK_EQ(diagnostic->code(), eve::DiagnosticCode::ParseError);
    CHECK_EQ(restored.snapshotJson(), before);

    auto malformed = restored.restoreJson("{");
    CHECK(!malformed.ok());
    const auto* parseDiagnostic = malformed.error();
    REQUIRE(parseDiagnostic != nullptr);
    CHECK_EQ(parseDiagnostic->code(), eve::DiagnosticCode::ParseError);
    CHECK(!parseDiagnostic->message().empty());
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("authority.input.invalidFactsDoNotMutate") {
    Store store;
    CHECK(!store.grant("", "scope", "read", "source").ok());
    CHECK(!store.deny("actor", "", "read", "source").ok());
    CHECK(!store.grant("actor", "scope", "", "source").ok());
    CHECK(!store.revoke("missing", "gone").ok());
    CHECK_EQ(store.query("", "", ""), 0);
    CHECK_EQ(store.eventCount(), 0);
}
}  // namespace test_authority_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_script_ownership)
#include "PrivateTestScope.h"
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "authority/Authority.h"
#include "common/SquirrelOwnership.h"
#include "effects/Effects.h"
#include "orders/CommandQueue.h"
#include "procgen/Procgen.h"
#include "production/Production.h"
#include "statepatch/StatePatch.h"
#include "zeroerr/unittest.h"
// script_ownership.cpp
namespace test_script_ownership_unit {
/**
 * @file script_ownership.cpp
 * @brief Contract tests for Value/Owned/Borrowed script object semantics.
 */


// A RuntimePin references its producing store by address, so that store must never be
// relocated while a pin can exist. RuntimeObjectRegistry keeps it behind a stable heap
// address and stays movable itself (statepatch::Store move-assigns one).
static_assert(!std::is_move_constructible_v<eve::script::detail::RuntimeSlotStore>);
static_assert(!std::is_move_assignable_v<eve::script::detail::RuntimeSlotStore>);


TEST_CASE("scriptOwnership.commonSemanticsAndRegistryStaleGeneration") {
    struct Tag {};
    struct Item {
        int value = 7;
    };

    CHECK_EQ(std::string(eve::script::objectSemanticName(eve::script::ObjectSemantic::Value)), std::string("value"));
    CHECK_EQ(std::string(eve::script::objectSemanticName(eve::script::ObjectSemantic::Owned)), std::string("owned"));
    CHECK_EQ(std::string(eve::script::objectSemanticName(eve::script::ObjectSemantic::Borrowed)),
             std::string("borrowed"));

    eve::script::RuntimeObjectRegistry<Item, Tag> registry;
    auto                                          created = registry.emplace(std::make_unique<Item>());
    REQUIRE(created.ok());
    const auto reference = std::move(created).takeValue();
    auto       borrowed  = registry.resolve(reference);
    REQUIRE(borrowed.isBound());
    CHECK_EQ(borrowed->value, 7);
    CHECK(!registry.isStale(reference));

    auto released = registry.erase(reference);
    REQUIRE(released.ok());
    CHECK(registry.isStale(reference));
    CHECK(!registry.resolve(reference).isBound());

    auto replacement = registry.emplace(std::make_unique<Item>());
    REQUIRE(replacement.ok());
    const auto replacementReference = std::move(replacement).takeValue();
    CHECK(replacementReference.handle.index() == reference.handle.index());
    CHECK(replacementReference.handle.generation() != reference.handle.generation());
    CHECK(registry.isStale(reference));
    CHECK(!registry.isStale(replacementReference));

    const auto                         oldEpoch          = registry.ownerEpoch();
    eve::script::RuntimeHandleRef<Tag> unloadedReference = replacementReference;
    registry.clear();
    CHECK(registry.isStale(unloadedReference));
    CHECK(!registry.resolve(unloadedReference).isBound());
    {
        eve::script::RuntimeObjectRegistry<Item, Tag> reloadedRegistry;
        CHECK(reloadedRegistry.ownerEpoch() != oldEpoch);
        CHECK(reloadedRegistry.isStale(unloadedReference));
    }
}

TEST_CASE("scriptOwnership.pinKeepsErasedPayloadAliveUntilRelease") {
    struct Tag {};
    struct Tracked {
        int  value     = 7;
        int* destroyed = nullptr;
        ~Tracked() {
            if (destroyed != nullptr) ++*destroyed;
        }
    };

    int                                              destroyed = 0;
    eve::script::RuntimeObjectRegistry<Tracked, Tag> registry;

    auto item       = std::make_unique<Tracked>();
    item->destroyed = &destroyed;
    auto created    = registry.emplace(std::move(item));
    REQUIRE(created.ok());
    const auto reference = std::move(created).takeValue();

    auto pinned = registry.pin(reference);
    REQUIRE(pinned.ok());
    CHECK(pinned.value().isBound());
    CHECK_EQ(pinned.value().get()->value, 7);
    CHECK(pinned.value().borrow().isBound());

    // Destroy order A: the holder is released after the owner erased the entry.
    auto released = registry.erase(reference);
    REQUIRE(released.ok());
    CHECK(registry.isStale(reference));
    CHECK(!registry.resolve(reference).isBound());
    CHECK_EQ(destroyed, 0);
    CHECK_EQ(pinned.value().get()->value, 7);
    CHECK(!registry.pin(reference).ok());

    // A slot with a live pin is never handed out again, even though the handle is stale.
    auto replacement = registry.emplace(std::make_unique<Tracked>());
    REQUIRE(replacement.ok());
    const auto replacementReference = std::move(replacement).takeValue();
    CHECK(replacementReference.handle.index() != reference.handle.index());

    // Moving the pin transfers the keep-alive; releasing the source is a no-op.
    {
        auto moved = std::move(pinned).takeValue();
        CHECK(moved.isBound());
        CHECK_EQ(moved.get()->value, 7);
        CHECK_EQ(destroyed, 0);
    }
    CHECK_EQ(destroyed, 1);
}

TEST_CASE("scriptOwnership.clearDefersPinnedPayloadDestruction") {
    struct Tag {};
    struct Tracked {
        int* destroyed = nullptr;
        ~Tracked() {
            if (destroyed != nullptr) ++*destroyed;
        }
    };

    int                                              kept    = 0;
    int                                              dropped = 0;
    eve::script::RuntimeObjectRegistry<Tracked, Tag> registry;

    auto pinnedItem       = std::make_unique<Tracked>();
    pinnedItem->destroyed = &kept;
    auto pinnedRef        = std::move(registry.emplace(std::move(pinnedItem))).takeValue();
    auto held             = registry.pin(pinnedRef);
    REQUIRE(held.ok());

    auto droppedItem       = std::make_unique<Tracked>();
    droppedItem->destroyed = &dropped;
    auto droppedRef        = std::move(registry.emplace(std::move(droppedItem))).takeValue();

    registry.clear();

    // Destroy order B: the owner was cleared first, the holder is still alive.
    CHECK_EQ(dropped, 1);
    CHECK_EQ(kept, 0);
    CHECK(registry.isStale(pinnedRef));
    CHECK(registry.isStale(droppedRef));
    CHECK(!registry.resolve(pinnedRef).isBound());

    // Releasing the last pin destroys the object the clear() had kept alive; the
    // move-assignment shape also covers releasing through a moved-from pin.
    held.value() = eve::script::RuntimePin<Tracked, Tag>{};
    CHECK(!held.value().isBound());
    CHECK_EQ(kept, 1);
}
}  // namespace test_script_ownership_unit
#endif
