// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "gameplay_tags.cpp",
    "tags.cpp"
  ]
}
*/
// clang-format on

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gameplay_tags)
#include "PrivateTestScope.h"
#include "editor/GameplayTagPicker.h"
#include "tags/GameplayTag.h"
#include "tags/TagStore.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace test_gameplay_tags {
using eve::tags::GameplayTagMatch;
using eve::tags::GameplayTagRegistry;

// Name contract: canonical dot segments map to stable ids. Repeating the same
// definition is a NoOp; conflicting descriptions and malformed names reject.
TEST_CASE("gameplayTags.spec.registry_names_ids_and_conflicts") {
    GameplayTagRegistry registry;
    auto                first = registry.registerTag("Combat.Action.Attack", "offensive action");
    REQUIRE(first.ok());
    CHECK_EQ(first.value().value(), std::uint64_t{0x5fd0e8ccf2e3cf33ull});
    auto repeated = registry.registerTag("Combat.Action.Attack", "offensive action");
    REQUIRE(repeated.ok());
    CHECK_EQ(repeated.code(), eve::StatusCode::NoOp);
    CHECK(!registry.registerTag("Combat..Attack", "bad").ok());
    CHECK(!registry.registerTag("Combat.Action.Attack", "different").ok());
    CHECK_EQ(registry.definitions().size(), std::size_t{1});
}

// Search contract: root uses dot boundaries; text filtering is case
// insensitive, and results are ordered by canonical name.
TEST_CASE("gameplayTags.spec.hierarchy_and_search") {
    GameplayTagRegistry registry;
    REQUIRE(registry.registerTag("Combat", "root").ok());
    REQUIRE(registry.registerTag("Combat.Action", "actions").ok());
    REQUIRE(registry.registerTag("Combat.Action.Attack", "Melee and RANGED attacks").ok());
    REQUIRE(registry.registerTag("Combatant", "different prefix").ok());
    auto descendants = registry.search({}, "Combat");
    REQUIRE_EQ(descendants.size(), std::size_t{3});
    CHECK_EQ(descendants[0].name, "Combat");
    CHECK_EQ(descendants[2].name, "Combat.Action.Attack");
    auto filtered = registry.search("ranged", "Combat");
    REQUIRE_EQ(filtered.size(), std::size_t{1});
    CHECK_EQ(filtered[0].name, "Combat.Action.Attack");
}

// Codec contract: version-one values round trip by value, while altered ids
// or unsupported versions fail without publishing a partial registry.
TEST_CASE("gameplayTags.spec.codec_round_trip_and_validation") {
    GameplayTagRegistry registry;
    REQUIRE(registry.registerTag("Combat.Action.Attack", "Attack").ok());
    REQUIRE(registry.registerTag("State.Stunned", "Stun").ok());
    auto encoded = registry.toValue();
    REQUIRE(encoded.ok());
    auto decoded = GameplayTagRegistry::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().definitions() == registry.definitions());
    eve::Value badId = encoded.value();
    badId.find("tags")->at(0).set("id", "0000000000000000");
    CHECK(!GameplayTagRegistry::fromValue(badId).ok());
    eve::Value badVersion = encoded.value();
    badVersion.set("version", 2);
    CHECK(!GameplayTagRegistry::fromValue(badVersion).ok());
}

// Hierarchical membership follows dot boundaries; similar textual prefixes
// are not descendants, and exact mode excludes descendants.
TEST_CASE("gameplayTags.spec.store_matches_dot_boundary_only") {
    eve::tags::TagStore store;
    CHECK(store.addTag("fighter:a", "Combat.Action.Attack.Light"));
    CHECK(store.addTag("fighter:b", "Combat.Action.AttackHeavy"));
    CHECK(store.hasTagMatching("fighter:a", "Combat.Action.Attack", GameplayTagMatch::IncludeDescendants));
    CHECK(!store.hasTagMatching("fighter:b", "Combat.Action.Attack", GameplayTagMatch::IncludeDescendants));
    CHECK(!store.hasTagMatching("fighter:a", "Combat.Action.Attack", GameplayTagMatch::Exact));
    auto subjects = store.subjectsWithTagMatching("Combat.Action.Attack", GameplayTagMatch::IncludeDescendants);
    REQUIRE_EQ(subjects.size(), std::size_t{1});
    CHECK_EQ(subjects[0], "fighter:a");
    CHECK(store.matchesAllTags("fighter:a", {"Combat", "Combat.Action"}, GameplayTagMatch::IncludeDescendants));
}

// Picker contract: root and filter project an ordered hierarchy; selection
// accepts only a registered tag and rejects malformed roots.
TEST_CASE("gameplayTags.spec.picker_hierarchy_filter_and_selection") {
    GameplayTagRegistry registry;
    REQUIRE(registry.registerTag("Combat", "root").ok());
    REQUIRE(registry.registerTag("Combat.Action", "actions").ok());
    REQUIRE(registry.registerTag("Combat.Action.Attack", "attack").ok());
    REQUIRE(registry.registerTag("State.Stunned", "stun").ok());
    eve::editor::GameplayTagPicker picker(registry);
    REQUIRE(picker.setRoot("Combat").ok());
    auto entries = picker.entries();
    REQUIRE_EQ(entries.size(), std::size_t{3});
    CHECK_EQ(entries[0].depth, 0);
    CHECK(entries[0].hasChildren);
    CHECK_EQ(entries[2].depth, 2);
    picker.setFilter("attack");
    REQUIRE_EQ(picker.entries().size(), std::size_t{1});
    REQUIRE(picker.select("Combat.Action.Attack").ok());
    CHECK_EQ(picker.selected(), "Combat.Action.Attack");
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

namespace test_tags_unit {
using eve::tags::TagStore;

// Set contract: duplicate adds and absent removes are NoOps; enumeration and
// reverse lookup are lexical rather than insertion order.
TEST_CASE("tags.spec.set_membership_and_reverse_indexes") {
    TagStore store;
    CHECK(store.addTag("subject:z", "beta"));
    CHECK(store.addTag("subject:z", "alpha"));
    CHECK(store.addTag("subject:a", "alpha"));
    CHECK(!store.addTag("subject:z", "alpha"));
    CHECK_EQ(store.tagsOf("subject:z")[0], "alpha");
    CHECK_EQ(store.tagsOf("subject:z")[1], "beta");
    CHECK_EQ(store.subjectsWithTag("alpha")[0], "subject:a");
    CHECK_EQ(store.subjectsWithTag("alpha")[1], "subject:z");
    CHECK(store.removeTag("subject:a", "alpha"));
    CHECK(!store.removeTag("subject:a", "alpha"));
    REQUIRE_EQ(store.subjectsWithTag("alpha").size(), std::size_t{1});
}

// Quantifier contract: all(empty) is true and any(empty) is false, for both
// tags and capabilities. Capabilities remain independent from same-name tags.
TEST_CASE("tags.spec.all_any_and_capability_independence") {
    TagStore store;
    CHECK(store.addTag("subject", "shared"));
    CHECK(store.addTag("subject", "other"));
    CHECK(store.addCapability("subject", "shared"));
    CHECK(store.requireAllTags("subject", {"shared", "other"}));
    CHECK(!store.requireAllTags("subject", {"shared", "missing"}));
    CHECK(store.requireAnyTag("subject", {"missing", "other"}));
    CHECK(store.requireAllTags("missing", {}));
    CHECK(!store.requireAnyTag("subject", {}));
    CHECK(store.requireAllCapabilities("subject", {"shared"}));
    CHECK(!store.requireAnyCapability("subject", {}));
    CHECK(store.removeTag("subject", "shared"));
    CHECK(!store.hasTag("subject", "shared"));
    REQUIRE(store.hasCapability("subject", "shared"));
}

// Input/event contract: empty stable ids cannot mutate. Each successful
// mutation emits one sequential event; clearEvents retains state and counter.
TEST_CASE("tags.spec.invalid_input_and_event_sequence") {
    TagStore store;
    CHECK(!store.addTag("", "tag"));
    CHECK(!store.addTag("subject", ""));
    CHECK(!store.addCapability("", "ability"));
    CHECK(store.events().empty());
    CHECK(store.addTag("subject", "tag"));
    CHECK(!store.addTag("subject", "tag"));
    CHECK(store.addCapability("subject", "ability"));
    CHECK(store.removeTag("subject", "tag"));
    REQUIRE_EQ(store.events().size(), std::size_t{3});
    CHECK_EQ(store.events()[0].action, "tag_added");
    CHECK_EQ(store.events()[0].sequence, std::uint64_t{1});
    CHECK_EQ(store.events()[1].action, "capability_added");
    CHECK_EQ(store.events()[2].action, "tag_removed");
    store.clearEvents();
    CHECK(store.events().empty());
    CHECK(store.hasCapability("subject", "ability"));
    CHECK(store.removeCapability("subject", "ability"));
    CHECK_EQ(store.events()[0].sequence, std::uint64_t{4});
}

// Lifecycle contract: removeSubject clears both sets and reverse indexes;
// clear resets the entire store including event sequence.
TEST_CASE("tags.spec.subject_removal_and_full_clear") {
    TagStore store;
    store.addTag("subject", "one");
    store.addTag("subject", "two");
    store.addCapability("subject", "ability");
    store.clearEvents();
    CHECK(store.removeSubject("subject"));
    CHECK(store.tagsOf("subject").empty());
    CHECK(store.capabilitiesOf("subject").empty());
    CHECK(store.subjectsWithTag("one").empty());
    REQUIRE_EQ(store.events().size(), std::size_t{4});
    CHECK_EQ(store.events().back().action, "subject_removed");
    CHECK(!store.removeSubject("subject"));
    store.clear();
    CHECK(store.events().empty());
    CHECK(store.addTag("new", "tag"));
    CHECK_EQ(store.events()[0].sequence, std::uint64_t{1});
}

// Module lifecycle contract: listener capability exists only while the Tags
// module instance is live.
TEST_CASE("tags.spec.module_listener_lifecycle") {
    eve::cap::detail::clearAllRaw();
    int queryCount = 0;
    {
        eve::tags::Tags module;
        eve::cap::forEach<eve::IStateQuery>([&](eve::IStateQuery*) { ++queryCount; });
        CHECK_EQ(queryCount, 1);
        int mutationCount = 0;
        eve::cap::forEach<eve::IStateMutation>([&](eve::IStateMutation*) { ++mutationCount; });
        CHECK_EQ(mutationCount, 1);
    }
    queryCount = 0;
    eve::cap::forEach<eve::IStateQuery>([&](eve::IStateQuery*) { ++queryCount; });
    REQUIRE_EQ(queryCount, 0);
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
    return tags.eventAction(tags.eventCount() - 1) == "capability_added";
}
)SQ";

UnitSciptTest(TagsScriptTest, kTagsScript);
}  // namespace test_tags_unit
#endif
