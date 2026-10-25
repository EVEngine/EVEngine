#include "ScriptTest.h"
#include "social/SocialGraph.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <string>

namespace {
using eve::social::SocialGraph;

// Ownership contract: one owner per entity, reverse indexes sorted by entity;
// control is an independent relation and changing owner leaves it intact.
TEST_CASE("social.spec.owner_and_controller_indexes") {
    SocialGraph graph;
    CHECK(!graph.setOwner("", "red"));
    CHECK(graph.setOwner("asset:b", "red"));
    CHECK(graph.setOwner("asset:a", "red"));
    CHECK(graph.setController("asset:a", "player"));
    CHECK_EQ(graph.ownedBy("red").size(), std::size_t{2});
    CHECK_EQ(graph.ownedBy("red")[0], "asset:a");
    CHECK_EQ(graph.ownedBy("red")[1], "asset:b");
    CHECK_EQ(graph.controlledBy("player")[0], "asset:a");
    CHECK(graph.setOwner("asset:a", "blue"));
    CHECK_EQ(graph.ownerOf("asset:a"), "blue");
    CHECK_EQ(graph.ownedBy("red").size(), std::size_t{1});
    CHECK_EQ(graph.controllerOf("asset:a"), "player");
    CHECK(!graph.setOwner("asset:a", "blue"));
    CHECK(graph.setOwner("asset:a", ""));
    CHECK(graph.ownerOf("asset:a").empty());
    REQUIRE_EQ(graph.controllerOf("asset:a"), "player");
}

// Assignment contract: role forms part of the key; duplicate assignment and
// removal are no-ops. Forward and reverse indexes agree after mutations.
TEST_CASE("social.spec.role_assignments_and_reverse_lookup") {
    SocialGraph graph;
    CHECK(!graph.assign("", "operator", "asset"));
    CHECK(!graph.assign("actor", "", "asset"));
    CHECK(!graph.assign("actor", "operator", ""));
    CHECK(graph.assign("actor:b", "operator", "asset"));
    CHECK(graph.assign("actor:a", "operator", "asset"));
    CHECK(graph.assign("actor:a", "auditor", "asset"));
    CHECK(!graph.assign("actor:a", "operator", "asset"));
    CHECK_EQ(graph.assigneesOf("asset", "operator")[0], "actor:a");
    CHECK_EQ(graph.assigneesOf("asset", "operator")[1], "actor:b");
    CHECK_EQ(graph.targetsOf("actor:a", "operator")[0], "asset");
    CHECK(graph.unassign("actor:a", "operator", "asset"));
    CHECK(!graph.unassign("actor:a", "operator", "asset"));
    CHECK(!graph.isAssigned("actor:a", "operator", "asset"));
    REQUIRE(graph.isAssigned("actor:a", "auditor", "asset"));
}

// Relation contract: directed type and threshold are independent dimensions.
TEST_CASE("social.spec.directed_weighted_relations") {
    SocialGraph graph;
    CHECK(!graph.setRelation("", "b", "trust", 1.0));
    CHECK(graph.setRelation("a", "c", "trust", 25.0));
    CHECK(graph.setRelation("a", "b", "trust", 75.0));
    CHECK(graph.link("b", "a", "member"));
    CHECK(!graph.hasRelation("b", "a", "trust"));
    CHECK_EQ(graph.relation("b", "a", "trust", -1.0), -1.0);
    const auto updatedWeight = graph.addRelation("a", "b", "trust", -10.0);
    CHECK_EQ(updatedWeight, 65.0);
    const auto outgoing = graph.relationTargets("a", "trust", 50.0);
    REQUIRE_EQ(outgoing.size(), std::size_t{1});
    CHECK_EQ(outgoing[0], "b");
    const auto incoming = graph.relationSources("b", "trust", 60.0);
    REQUIRE_EQ(incoming.size(), std::size_t{1});
    CHECK_EQ(incoming[0], "a");
    CHECK(graph.removeRelation("a", "b", "trust"));
    CHECK(!graph.removeRelation("a", "b", "trust"));
    const auto remaining = graph.relationTargets("a", "trust");
    REQUIRE_EQ(remaining.size(), std::size_t{1});
    CHECK_EQ(remaining[0], "c");
}

// Lifetime contract: removing an entity clears incoming and outgoing owner,
// controller, assignment, and relation references in one operation.
TEST_CASE("social.spec.entity_removal_cleans_all_indexes") {
    SocialGraph graph;
    graph.setOwner("asset", "entity");
    graph.setOwner("entity", "faction");
    graph.setController("asset", "entity");
    graph.assign("entity", "role:a", "target");
    graph.assign("source", "role:b", "entity");
    graph.setRelation("entity", "target", "out", 2.0);
    graph.setRelation("source", "entity", "in", 3.0);
    graph.clearEvents();
    CHECK(graph.removeEntity("entity"));
    CHECK(graph.ownerOf("asset").empty());
    CHECK(graph.ownerOf("entity").empty());
    CHECK(graph.controllerOf("asset").empty());
    CHECK(!graph.isAssigned("entity", "role:a", "target"));
    CHECK(!graph.isAssigned("source", "role:b", "entity"));
    CHECK(!graph.hasRelation("entity", "target", "out"));
    CHECK(!graph.hasRelation("source", "entity", "in"));
    REQUIRE(!graph.events().empty());
    CHECK_EQ(graph.events().back().action, "entity_removed");
    CHECK(!graph.removeEntity("entity"));
}

// Event contract: successful mutation increments the sequence; clearing the
// queue does not reset the counter or undo graph state.
TEST_CASE("social.spec.event_sequence_and_clear") {
    SocialGraph graph;
    CHECK(graph.setRelation("a", "b", "score", 10.0));
    CHECK(!graph.setRelation("a", "b", "score", 10.0));
    CHECK(graph.setRelation("a", "b", "score", 12.0));
    CHECK(graph.removeRelation("a", "b", "score"));
    REQUIRE_EQ(graph.events().size(), std::size_t{3});
    CHECK_EQ(graph.events()[0].sequence, std::uint64_t{1});
    CHECK_EQ(graph.events()[1].sequence, std::uint64_t{2});
    CHECK_EQ(graph.events()[1].oldValue, 10.0);
    CHECK_EQ(graph.events()[1].newValue, 12.0);
    graph.clearEvents();
    CHECK(graph.events().empty());
    CHECK(graph.setOwner("asset", "org"));
    REQUIRE_EQ(graph.events().size(), std::size_t{1});
    CHECK_EQ(graph.events()[0].sequence, std::uint64_t{4});
    CHECK_EQ(graph.ownerOf("asset"), "org");
}

// Identity contract: integer ids use a reserved encoding and never alias a
// textual spelling of the same number.
TEST_CASE("social.spec.integer_ids_are_disjoint_from_strings") {
    SocialGraph graph;
    const auto  encoded = SocialGraph::integerId(12);
    CHECK(encoded != "12");
    CHECK(graph.setOwner(encoded, "integer-owner"));
    CHECK(graph.setOwner("12", "string-owner"));
    CHECK_EQ(graph.ownerOf(encoded), "integer-owner");
    REQUIRE_EQ(graph.ownerOf("12"), "string-owner");
}
}  // namespace

// Script contract: the module facade exposes the same ordering, integer-id,
// relation, and event semantics as the native graph.
static const char* kSocialScript = R"SQ(
function testSocialBindings() {
    local social = eve.Social();
    social.clear();
    if (!social.setOwner("asset:b", "org")) return false;
    if (!social.setOwner("asset:a", "org")) return false;
    if (social.ownedAt("org", 0) != "asset:a") return false;
    if (!social.setControllerInt(7, 9)) return false;
    if (social.controllerOfInt(7) != social.idFromInt(9)) return false;
    if (!social.assign("person", "operator", "asset:a")) return false;
    if (social.assigneeAt("asset:a", "operator", 0) != "person") return false;
    if (!social.setRelation("person", "org", "affinity", 30.0)) return false;
    if (social.addRelation("person", "org", "affinity", 15.0) != 45.0) return false;
    if (social.relationSourceAt("org", "affinity", 40.0, 0) != "person") return false;
    if (social.eventCount() < 5) return false;
    return social.eventAction(social.eventCount() - 1) == "relation_changed";
}
)SQ";

UnitSciptTest(SocialScriptTest, kSocialScript);
