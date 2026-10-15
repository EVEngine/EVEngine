#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gameplay_tags)
#include "PrivateTestScope.h"
#include "editor/GameplayTagPicker.h"
#include "tags/GameplayTag.h"
#include "tags/TagStore.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// gameplay_tags.cpp
namespace test_gameplay_tags {
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
}  // namespace test_gameplay_tags
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tags)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "common/Capability.h"
#include "common/StateAccess.h"
#include "tags/TagStore.h"
#include "tags/Tags.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tags.cpp
namespace test_tags_unit {
using namespace eve;


using eve::tags::Tags;
using eve::tags::TagStore;

TEST_CASE("tags.tagsAreSetsWithDeterministicOrdering") {
    TagStore tags;
    CHECK(tags.addTag("subject:b", "zeta"));
    CHECK(tags.addTag("subject:b", "alpha"));
    CHECK(!tags.addTag("subject:b", "alpha"));
    CHECK(tags.hasTag("subject:b", "alpha"));

    const auto values = tags.tagsOf("subject:b");
    REQUIRE(values.size() == 2);
    CHECK(values[0] == "alpha");
    CHECK(values[1] == "zeta");
    CHECK(tags.removeTag("subject:b", "alpha"));
    CHECK(!tags.removeTag("subject:b", "alpha"));
    CHECK(!tags.hasTag("subject:b", "alpha"));
}

TEST_CASE("tags.reverseTagQueriesAreIndexedAndSorted") {
    TagStore tags;
    tags.addTag("subject:z", "selectable");
    tags.addTag("subject:a", "selectable");
    tags.addTag("subject:m", "other");

    const auto subjects = tags.subjectsWithTag("selectable");
    REQUIRE(subjects.size() == 2);
    CHECK(subjects[0] == "subject:a");
    CHECK(subjects[1] == "subject:z");
    tags.removeTag("subject:a", "selectable");
    REQUIRE(tags.subjectsWithTag("selectable").size() == 1);
    CHECK(tags.subjectsWithTag("selectable")[0] == "subject:z");
}

TEST_CASE("tags.requireAllAndAnyHaveExplicitEmptySetSemantics") {
    TagStore tags;
    tags.addTag("subject", "a");
    tags.addTag("subject", "b");
    tags.addCapability("subject", "x");
    tags.addCapability("subject", "y");

    CHECK(tags.requireAllTags("subject", {"a", "b"}));
    CHECK(!tags.requireAllTags("subject", {"a", "missing"}));
    CHECK(tags.requireAnyTag("subject", {"missing", "b"}));
    CHECK(!tags.requireAnyTag("subject", {}));
    CHECK(tags.requireAllTags("missing", {}));
    CHECK(tags.requireAllCapabilities("subject", {"x", "y"}));
    CHECK(tags.requireAnyCapability("subject", {"missing", "x"}));
    CHECK(!tags.requireAnyCapability("missing", {"x"}));
}

TEST_CASE("tags.capabilitiesAreIndependentFromTags") {
    TagStore tags;
    CHECK(tags.addTag("subject", "shared-name"));
    CHECK(tags.addCapability("subject", "shared-name"));
    CHECK(tags.removeTag("subject", "shared-name"));
    CHECK(!tags.hasTag("subject", "shared-name"));
    CHECK(tags.hasCapability("subject", "shared-name"));
    CHECK(tags.removeCapability("subject", "shared-name"));
    CHECK(!tags.hasCapability("subject", "shared-name"));
}

TEST_CASE("tags.rejectEmptyStableIdentifiersWithoutEvents") {
    TagStore tags;
    CHECK(!tags.addTag("", "tag"));
    CHECK(!tags.addTag("subject", ""));
    CHECK(!tags.addCapability("", "capability"));
    CHECK(!tags.addCapability("subject", ""));
    CHECK(tags.events().empty());
}

TEST_CASE("tags.eventsDescribeOnlySuccessfulMutations") {
    TagStore tags;
    tags.addTag("subject", "tag");
    tags.addTag("subject", "tag");
    tags.addCapability("subject", "capability");
    tags.removeTag("subject", "tag");
    REQUIRE(tags.events().size() == 3);
    CHECK_EQ(tags.events()[0].sequence, std::uint64_t(1));
    CHECK_EQ(tags.events()[1].sequence, std::uint64_t(2));
    CHECK(tags.events()[0].action == "tag_added");
    CHECK(tags.events()[0].subject == "subject");
    CHECK(tags.events()[0].value == "tag");
    CHECK(tags.events()[1].action == "capability_added");
    CHECK(tags.events()[2].action == "tag_removed");
    tags.clearEvents();
    CHECK(tags.events().empty());
    CHECK(tags.hasCapability("subject", "capability"));
}

TEST_CASE("tags.removeSubjectCleansBothSetsAndReverseIndex") {
    TagStore tags;
    tags.addTag("subject", "one");
    tags.addTag("subject", "two");
    tags.addCapability("subject", "ability");
    tags.clearEvents();

    CHECK(tags.removeSubject("subject"));
    CHECK(tags.tagsOf("subject").empty());
    CHECK(tags.capabilitiesOf("subject").empty());
    CHECK(tags.subjectsWithTag("one").empty());
    REQUIRE(tags.events().size() == 4);
    CHECK(tags.events().back().action == "subject_removed");
    CHECK(!tags.removeSubject("subject"));
}

TEST_CASE("tags.clearResetsStateEventsAndSequence") {
    TagStore tags;
    tags.addTag("subject", "tag");
    tags.clear();
    CHECK(tags.tagsOf("subject").empty());
    CHECK(tags.events().empty());
    tags.addCapability("new-subject", "new-capability");
    REQUIRE(tags.events().size() == 1);
    CHECK_EQ(tags.events()[0].sequence, std::uint64_t(1));
}

static const char* kTagsScript = R"SQ(
function testTagsBindings() {
    local tags = eve.Tags();
    if (!tags.add("subject:b", "zeta")) return false;
    if (!tags.add("subject:b", "alpha")) return false;
    if (!tags.add("subject:a", "alpha")) return false;
    if (tags.at("subject:b", 0) != "alpha") return false;
    if (tags.subjectAt("alpha", 0) != "subject:a") return false;
    if (!tags.requireAll("subject:b", ["alpha", "zeta"])) return false;
    if (!tags.requireAny("subject:b", ["missing", "zeta"])) return false;
    if (!tags.addCapability("subject:b", "can.perform")) return false;
    if (!tags.requireAllCapabilities("subject:b", ["can.perform"])) return false;
    if (!tags.hasCapability("subject:b", "can.perform")) return false;
    return tags.eventAction(tags.eventCount() - 1) == "capability_added";
}
)SQ";

UnitSciptTest(TagsScriptTest, kTagsScript);


TEST_CASE("tags.modulePublishesWorldStateListeners") {
    eve::cap::detail::clearAllRaw();
    int listeners = 0;
    {
        Tags tags;
        eve::cap::forEach<eve::IStateQuery>([&](eve::IStateQuery*) { ++listeners; });
        CHECK_EQ(listeners, 1);
        listeners = 0;
        eve::cap::forEach<eve::IStateMutation>([&](eve::IStateMutation*) { ++listeners; });
        CHECK_EQ(listeners, 1);
    }
    listeners = 0;
    eve::cap::forEach<eve::IStateQuery>([&](eve::IStateQuery*) { ++listeners; });
    CHECK_EQ(listeners, 0);
}
}  // namespace test_tags_unit
#endif
