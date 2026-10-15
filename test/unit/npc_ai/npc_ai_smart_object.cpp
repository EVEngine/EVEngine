#include "npc_ai/SmartObject.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::npc_ai;

namespace {
SmartObjectDefinition cover(std::string id, double x) {
    return {std::move(id), {{"left", "cover", {"high", "outdoor"}, {x, 0.0, 0.0}}}};
}
}  // namespace

TEST_CASE("npc_ai.smartObjectQueryAndLeaseAreDeterministic") {
    SmartObjectWorld world;
    auto             farObject  = world.registerObject(cover("cover-b", 8.0));
    auto             nearObject = world.registerObject(cover("cover-a", 2.0));
    REQUIRE(farObject.ok());
    REQUIRE(nearObject.ok());
    SmartObjectQuery query{"cover", {"high"}, {0.0, 0.0, 0.0}, 10.0};
    auto             candidates = world.query(query);
    REQUIRE(candidates.ok());
    CHECK_EQ(candidates.value().size(), 2u);
    CHECK_EQ(candidates.value()[0].logicalId, std::string("cover-a"));

    const AgentHandle agent(4, 1);
    auto              claim = world.claim(agent, nearObject.value(), "left", 10, 5);
    REQUIRE(claim.ok());
    auto unavailable = world.query(query);
    REQUIRE(unavailable.ok());
    CHECK_EQ(unavailable.value().size(), 1u);
    auto conflict = world.claim(AgentHandle(5, 1), nearObject.value(), "left", 10, 5);
    CHECK(!conflict.ok());
    REQUIRE(world.renew(claim.value(), agent, 12, 10).ok());
    auto snapshot = world.snapshot(claim.value());
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().expiresAtTick, 22u);
    auto expiry = world.expire(22);
    REQUIRE(expiry.ok());
    CHECK_EQ(expiry.value().claimsExpired, 1u);
    auto stale = world.snapshot(claim.value());
    CHECK(!stale.ok());
}

TEST_CASE("npc_ai.smartObjectDefinesBothDestructionOrders") {
    SmartObjectWorld world;
    auto             object = world.registerObject(cover("door", 1.0));
    REQUIRE(object.ok());
    const AgentHandle agent(7, 2);
    auto              claim = world.claim(agent, object.value(), "left", 1, 100);
    REQUIRE(claim.ok());
    auto rejected = world.removeObject(object.value(), SmartObjectRemoval::RejectClaimed);
    CHECK(!rejected.ok());
    REQUIRE(world.removeObject(object.value(), SmartObjectRemoval::CancelClaims).ok());
    auto staleClaim = world.snapshot(claim.value());
    CHECK(!staleClaim.ok());

    auto secondObject = world.registerObject(cover("chair", 3.0));
    REQUIRE(secondObject.ok());
    auto secondClaim = world.claim(agent, secondObject.value(), "left", 2, 100);
    REQUIRE(secondClaim.ok());
    auto released = world.releaseAgentClaims(agent);
    REQUIRE(released.ok());
    CHECK_EQ(released.value(), 1u);
    REQUIRE(world.removeObject(secondObject.value(), SmartObjectRemoval::RejectClaimed).ok());
}
