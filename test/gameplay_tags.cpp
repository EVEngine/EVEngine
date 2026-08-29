#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/GameplayTagPicker.h"
#include "tags/GameplayTag.h"
#include "tags/TagStore.h"

using eve::tags::GameplayTagMatch;
using eve::tags::GameplayTagRegistry;

TEST_CASE("gameplayTags.registryValidatesStableNamesAndIds") {
    GameplayTagRegistry registry;
    auto                attack = registry.registerTag("Combat.Action.Attack", "An offensive combat action");
    REQUIRE(attack.ok());
    CHECK_EQ(attack.value().value(), std::uint64_t(0x1b587da31236aa15ull));
    auto duplicate = registry.registerTag("Combat.Action.Attack", "An offensive combat action");
    REQUIRE(duplicate.ok());
    CHECK(duplicate.status().code() == eve::StatusCode::NoOp);
    CHECK(!registry.registerTag("Combat..Attack", "invalid").ok());
    CHECK(!registry.registerTag("Combat.Action.Attack", "different metadata").ok());
}

TEST_CASE("gameplayTags.registrySearchesHierarchyDeterministically") {
    GameplayTagRegistry registry;
    REQUIRE(registry.registerTag("Combat", "Combat root").ok());
    REQUIRE(registry.registerTag("Combat.Action", "Actions").ok());
    REQUIRE(registry.registerTag("Combat.Action.Attack", "Melee and ranged attacks").ok());
    REQUIRE(registry.registerTag("State.Stunned", "Cannot act").ok());

    const auto combat = registry.search({}, "Combat");
    REQUIRE(combat.size() == 3);
    CHECK(combat[0].name == "Combat");
    CHECK(combat[2].name == "Combat.Action.Attack");
    const auto filtered = registry.search("RANGED", "Combat");
    REQUIRE(filtered.size() == 1);
    CHECK(filtered[0].name == "Combat.Action.Attack");
}

TEST_CASE("gameplayTags.registryCodecRoundTripsAndRejectsTamperedIds") {
    GameplayTagRegistry registry;
    REQUIRE(registry.registerTag("Combat.Action.Attack", "Attack").ok());
    REQUIRE(registry.registerTag("State.Stunned", "Stun").ok());
    auto encoded = registry.toValue();
    REQUIRE(encoded.ok());
    auto decoded = GameplayTagRegistry::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().definitions() == registry.definitions());

    eve::Value tampered = encoded.value();
    tampered.find("tags")->at(0).set("id", "0000000000000000");
    CHECK(!GameplayTagRegistry::fromValue(tampered).ok());
    tampered = encoded.value();
    tampered.set("version", 2);
    CHECK(!GameplayTagRegistry::fromValue(tampered).ok());
}

TEST_CASE("gameplayTags.storeMatchesOnlyDotBoundaryDescendants") {
    eve::tags::TagStore store;
    store.addTag("fighter:a", "Combat.Action.Attack.Light");
    store.addTag("fighter:b", "Combat.Action.AttackHeavy");
    store.addTag("fighter:c", "State.Stunned");

    CHECK(store.hasTagMatching("fighter:a", "Combat.Action.Attack", GameplayTagMatch::IncludeDescendants));
    CHECK(!store.hasTagMatching("fighter:b", "Combat.Action.Attack", GameplayTagMatch::IncludeDescendants));
    CHECK(!store.hasTagMatching("fighter:a", "Combat.Action.Attack", GameplayTagMatch::Exact));
    const auto subjects = store.subjectsWithTagMatching("Combat.Action.Attack", GameplayTagMatch::IncludeDescendants);
    REQUIRE(subjects.size() == 1);
    CHECK(subjects[0] == "fighter:a");
    CHECK(store.matchesAllTags("fighter:a", {"Combat", "Combat.Action"}, GameplayTagMatch::IncludeDescendants));
    CHECK(store.matchesAnyTag("fighter:c", {"Combat", "State"}, GameplayTagMatch::IncludeDescendants));
}

TEST_CASE("gameplayTagPicker.projectsSearchRootDepthAndSelection") {
    GameplayTagRegistry registry;
    REQUIRE(registry.registerTag("Combat", "Combat root").ok());
    REQUIRE(registry.registerTag("Combat.Action", "Action root").ok());
    REQUIRE(registry.registerTag("Combat.Action.Attack", "Attack").ok());
    REQUIRE(registry.registerTag("State.Stunned", "Stun").ok());

    eve::editor::GameplayTagPicker picker(registry);
    REQUIRE(picker.setRoot("Combat").ok());
    auto entries = picker.entries();
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].depth == 0);
    CHECK(entries[0].hasChildren);
    CHECK(entries[2].depth == 2);
    CHECK(!entries[2].hasChildren);
    picker.setFilter("attack");
    entries = picker.entries();
    REQUIRE(entries.size() == 1);
    REQUIRE(picker.select("Combat.Action.Attack").ok());
    CHECK(picker.selected() == "Combat.Action.Attack");
    CHECK(!picker.select("Combat.Action.Missing").ok());
    CHECK(!picker.setRoot("Combat..Invalid").ok());
}
