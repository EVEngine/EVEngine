#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"
#include "social/Social.h"
#include "social/SocialGraph.h"

using eve::social::Social;
using eve::social::SocialGraph;

TEST_CASE("social.ownershipAndControlAreIndependentAndIndexed") {
    SocialGraph graph;
    CHECK(graph.setOwner("asset:b", "faction:red"));
    CHECK(graph.setOwner("asset:a", "faction:red"));
    CHECK(graph.setController("asset:a", "player:one"));
    CHECK(graph.ownerOf("asset:a") == "faction:red");
    CHECK(graph.controllerOf("asset:a") == "player:one");

    const auto owned = graph.ownedBy("faction:red");
    REQUIRE(owned.size() == 2);
    CHECK(owned[0] == "asset:a");
    CHECK(owned[1] == "asset:b");
    CHECK(graph.controlledBy("player:one")[0] == "asset:a");

    CHECK(graph.setOwner("asset:a", "faction:blue"));
    CHECK(graph.ownedBy("faction:red").size() == 1);
    CHECK(graph.ownedBy("faction:blue")[0] == "asset:a");
    CHECK(!graph.setOwner("asset:a", "faction:blue"));
}

TEST_CASE("social.assignmentSupportsRolesDuplicatesAndReverseQueries") {
    SocialGraph graph;
    CHECK(graph.assign("actor:b", "operator", "asset:1"));
    CHECK(graph.assign("actor:a", "operator", "asset:1"));
    CHECK(graph.assign("actor:a", "auditor", "asset:1"));
    CHECK(!graph.assign("actor:a", "operator", "asset:1"));
    CHECK(graph.isAssigned("actor:a", "operator", "asset:1"));

    const auto assignees = graph.assigneesOf("asset:1", "operator");
    REQUIRE(assignees.size() == 2);
    CHECK(assignees[0] == "actor:a");
    CHECK(assignees[1] == "actor:b");
    CHECK(graph.targetsOf("actor:a", "operator")[0] == "asset:1");

    CHECK(graph.unassign("actor:a", "operator", "asset:1"));
    CHECK(!graph.unassign("actor:a", "operator", "asset:1"));
    CHECK(!graph.isAssigned("actor:a", "operator", "asset:1"));
    CHECK(graph.isAssigned("actor:a", "auditor", "asset:1"));
}

TEST_CASE("social.relationsAreDirectedTypedWeightedAndThresholdIndexed") {
    SocialGraph graph;
    CHECK(graph.setRelation("a", "c", "trust", 25.0));
    CHECK(graph.setRelation("a", "b", "trust", 75.0));
    CHECK(graph.link("b", "a", "member"));
    CHECK(graph.hasRelation("a", "b", "trust"));
    CHECK(!graph.hasRelation("b", "a", "trust"));
    CHECK_EQ(graph.relation("a", "b", "trust"), 75.0);
    const double adjustedTrust = graph.addRelation("a", "b", "trust", -10.0);
    CHECK_EQ(adjustedTrust, 65.0);

    const auto strong = graph.relationTargets("a", "trust", 50.0);
    REQUIRE(strong.size() == 1);
    CHECK(strong[0] == "b");
    const auto trustedBy = graph.relationSources("b", "trust", 60.0);
    REQUIRE(trustedBy.size() == 1);
    CHECK(trustedBy[0] == "a");
    CHECK(graph.removeRelation("a", "b", "trust"));
    CHECK(!graph.hasRelation("a", "b", "trust"));
    CHECK(graph.relationTargets("a", "trust").size() == 1);
}

TEST_CASE("social.entityRemovalCleansIncomingAndOutgoingReferences") {
    SocialGraph graph;
    graph.setOwner("asset", "entity");
    graph.setOwner("entity", "faction");
    graph.setController("asset", "entity");
    graph.setController("entity", "player");
    graph.assign("entity", "role:a", "target");
    graph.assign("source", "role:b", "entity");
    graph.setRelation("entity", "target", "out", 2.0);
    graph.setRelation("source", "entity", "in", 3.0);
    graph.clearEvents();

    CHECK(graph.removeEntity("entity"));
    CHECK(graph.ownerOf("entity").empty());
    CHECK(graph.ownerOf("asset").empty());
    CHECK(graph.controllerOf("entity").empty());
    CHECK(graph.controllerOf("asset").empty());
    CHECK(!graph.isAssigned("entity", "role:a", "target"));
    CHECK(!graph.isAssigned("source", "role:b", "entity"));
    CHECK(!graph.hasRelation("entity", "target", "out"));
    CHECK(!graph.hasRelation("source", "entity", "in"));
    REQUIRE(!graph.events().empty());
    CHECK(graph.events().back().action == "entity_removed");
    CHECK(!graph.removeEntity("missing"));
}

TEST_CASE("social.eventsAreOrderedAndCarryMutationData") {
    SocialGraph graph;
    graph.setRelation("a", "b", "score", 10.0);
    graph.setRelation("a", "b", "score", 12.0);
    graph.removeRelation("a", "b", "score");
    REQUIRE(graph.events().size() == 3);
    CHECK_EQ(graph.events()[0].sequence, std::uint64_t(1));
    CHECK_EQ(graph.events()[1].sequence, std::uint64_t(2));
    CHECK(graph.events()[0].action == "relation_added");
    CHECK(graph.events()[1].action == "relation_changed");
    CHECK_EQ(graph.events()[1].oldValue, 10.0);
    CHECK_EQ(graph.events()[1].newValue, 12.0);
    CHECK(graph.events()[2].action == "relation_removed");
    graph.clearEvents();
    CHECK(graph.events().empty());
}

TEST_CASE("social.integerAndStringIdsCannotCollide") {
    SocialGraph graph;
    const auto  integer = SocialGraph::integerId(12);
    CHECK(integer != "12");
    graph.setOwner(integer, "integer-owner");
    graph.setOwner("12", "string-owner");
    CHECK(graph.ownerOf(integer) == "integer-owner");
    CHECK(graph.ownerOf("12") == "string-owner");
}

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

TEST_CASE_FIXTURE(SocialScriptTest, "social.script.bindings") {
    CHECK(vm.callFunc(vm.findFunc("testSocialBindings"), vm).toBool());
}
