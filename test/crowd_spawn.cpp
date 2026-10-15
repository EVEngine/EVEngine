#include <simplesquirrel/simplesquirrel.hpp>
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve::crowd;

TEST_CASE("crowd.spawn.pushesConnectedNeighborsWithoutAdvancingTime") {
    Crowd crowd;
    crowd.setSeparationWeight(0.f);
    const int a         = crowd.addNamedAgent("a", 1.5f, 0.f, 0.f, 1.f);
    const int b         = crowd.addNamedAgent("b", 3.5f, 0.f, 0.f, 1.f);
    const int untouched = crowd.addNamedAgent("far", 100.f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentAction(a, "seek"));
    REQUIRE(crowd.setAgentTarget(a, 200.f, 0.f));
    SpawnBatch batch;
    batch.agents.push_back({"spawn", 0.f, 0.f, 0.f, 1.f, {}});
    auto result = crowd.applySpawnBatch(batch);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().created.size(), size_t(1));
    CHECK_EQ(result.value().displacedAgents, 2);
    CHECK(crowd.getAgentState(a).x >= 1.998f);
    CHECK(crowd.getAgentState(b).x >= 3.997f);
    CHECK_EQ(crowd.getAgentState(a).vx, 0.f);
    CHECK_EQ(crowd.getAgentAction(a), "seek");
    CHECK_EQ(crowd.getAgentState(untouched).x, 100.f);
    CHECK_EQ(crowd.getAgentState(crowd.getNamedAgentIndex("spawn")).x, 0.f);
}

TEST_CASE("crowd.spawn.fixedBlockerRollsBackWholeBatch") {
    Crowd     crowd;
    const int a     = crowd.addNamedAgent("a", 1.5f, 0.f, 0.f, 1.f);
    const int fixed = crowd.addNamedAgent("fixed", 10.f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentInteraction(fixed, {0.f, false, 1, 1}).ok());
    SpawnBatch batch;
    batch.agents = {{"first", 0.f, 0.f, 0.f, 1.f, {}}, {"second", 10.f, 0.f, 0.f, 1.f, {}}};
    auto result  = crowd.applySpawnBatch(batch);
    CHECK(!result.ok());
    CHECK_EQ(crowd.getAgentCount(), 2);
    CHECK_EQ(crowd.getAgentState(a).x, 1.5f);
    CHECK(!crowd.hasNamedAgent("first"));
    CHECK(!crowd.hasNamedAgent("second"));
}

TEST_CASE("crowd.spawn.nearestFreeIsBoundedAndLeavesNeighborsAlone") {
    Crowd      crowd;
    const int  a = crowd.addNamedAgent("a", 0.f, 0.f, 0.f, 1.f);
    SpawnBatch batch;
    batch.policy        = SpawnPolicy::NearestFree;
    batch.maxDistance   = 3.f;
    batch.searchSpacing = 0.5f;
    batch.agents        = {{"new", 0.f, 0.f, 0.f, 1.f, {}}};
    auto result         = crowd.applySpawnBatch(batch);
    REQUIRE(result.ok());
    CHECK_EQ(crowd.getAgentState(a).x, 0.f);
    const auto position = crowd.getAgentState(crowd.getNamedAgentIndex("new"));
    CHECK(std::hypot(position.x, position.y) >= 1.999f);
    CHECK(std::hypot(position.x, position.y) <= 3.001f);
    CHECK_EQ(result.value().displacedAgents, 0);
}

TEST_CASE("crowd.spawn.rejectBudgetAndDistanceFailuresAreAtomic") {
    Crowd crowd;
    crowd.addNamedAgent("a", 0.f, 0.f, 0.f, 1.f);
    SpawnBatch batch;
    batch.agents = {{"new", 0.f, 0.f, 0.f, 1.f, {}}};
    batch.policy = SpawnPolicy::RejectOverlap;
    CHECK(!crowd.applySpawnBatch(batch).ok());
    batch.policy      = SpawnPolicy::PushNeighbors;
    batch.maxDistance = 0.1f;
    CHECK(!crowd.applySpawnBatch(batch).ok());
    batch.maxDistance = 10.f;
    batch.maxChecks   = 1;
    CHECK(!crowd.applySpawnBatch(batch).ok());
    CHECK_EQ(crowd.getAgentCount(), 1);
    CHECK_EQ(crowd.getAgentState(0).x, 0.f);
    CHECK(!crowd.hasNamedAgent("new"));
}

TEST_CASE("crowd.spawn.respectsWallsAndInteractionLayers") {
    Crowd crowd;
    crowd.resizeField(10, 10, 1.f, -5.f, -5.f);
    crowd.setBlocked(5, 5, true);
    SpawnBatch batch;
    batch.agents = {{"wall", 0.5f, 0.5f, 0.f, 0.2f, {}}};
    CHECK(!crowd.applySpawnBatch(batch).ok());
    CHECK_EQ(crowd.getAgentCount(), 0);
    const int original = crowd.addNamedAgent("old", -2.f, -2.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentInteraction(original, {0.f, true, 1, 1}).ok());
    batch.agents = {{"otherLayer", -2.f, -2.f, 0.f, 1.f, {1.f, false, 2, 2}}};
    auto result  = crowd.applySpawnBatch(batch);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().displacedAgents, 0);
    CHECK_EQ(crowd.getAgentCount(), 2);
}

TEST_CASE("crowd.spawn.identityCapacityAndIntraBatchConflicts") {
    Crowd      crowd;
    SpawnBatch batch;
    batch.agents = {{"same", 0.f, 0.f, 0.f, 1.f, {}}, {"same", 5.f, 0.f, 0.f, 1.f, {}}};
    CHECK(!crowd.applySpawnBatch(batch).ok());
    batch.agents[1].stableId = "other";
    crowd.setMaxAgents(1);
    CHECK(!crowd.applySpawnBatch(batch).ok());
    crowd.setMaxAgents(10);
    batch.agents[1].x = 0.f;
    CHECK(!crowd.applySpawnBatch(batch).ok());
    CHECK_EQ(crowd.getAgentCount(), 0);
    batch.agents[1].x = 5.f;
    REQUIRE(crowd.applySpawnBatch(batch).ok());
    CHECK_EQ(crowd.getAgentCount(), 2);
    CHECK(!crowd.applySpawnBatch(batch).ok());
}

TEST_CASE("crowd.spawn.wallCannotBeCrossedToMakeRoom") {
    Crowd crowd;
    crowd.resizeField(10, 5, 1.f, 0.f, 0.f);
    for (int y = 0; y < 5; ++y) crowd.setBlocked(5, y, true);
    const int  original = crowd.addNamedAgent("old", 4.5f, 2.5f, 0.f, 0.4f);
    SpawnBatch batch;
    batch.agents = {{"new", 4.f, 2.5f, 0.f, 0.4f, {}}};
    auto result  = crowd.applySpawnBatch(batch);
    CHECK(!result.ok());
    CHECK_EQ(crowd.getAgentCount(), 1);
    CHECK_EQ(crowd.getAgentState(original).x, 4.5f);
}

TEST_CASE("crowd.spawn.unrelatedPreexistingOverlapIsNotModified") {
    Crowd crowd;
    crowd.addNamedAgent("farA", 50.f, 50.f, 0.f, 1.f);
    crowd.addNamedAgent("farB", 50.f, 50.f, 0.f, 1.f);
    SpawnBatch batch;
    batch.agents = {{"new", 0.f, 0.f, 0.f, 1.f, {}}};
    auto result  = crowd.applySpawnBatch(batch);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().displacedAgents, 0);
    CHECK_EQ(crowd.getAgentState(0).x, 50.f);
    CHECK_EQ(crowd.getAgentState(1).x, 50.f);
}

TEST_CASE("crowd.spawn.scriptTransactionAndMalformedInput") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    vm.run(vm.compileSource(R"(
        local crowd = eve.Crowd();
        crowd.addNamedAgent("old", 1.5, 0.0, 0.0, 1.0);
        local options = { policy = "pushNeighbors", maxDistance = 10.0,
                          searchSpacing = 1.0, maxPasses = 64, maxChecks = 10000 };
        local agents = [{ stableId = "new", x = 0.0, y = 0.0, heading = 0.0, radius = 1.0,
                          pushability = 1.0, holdPosition = false, layer = 1, mask = 1 }];
        local result = crowd.applySpawnBatch(agents, options);
        assert(result.ok && result.hasValue);
        assert(result.value.created[0].stableId == "new");
        assert(result.value.displacedAgents == 1);
        assert(crowd.getAgentCount() == 2);
        local duplicate = crowd.applySpawnBatch(agents, options);
        assert(!duplicate.ok && duplicate.diagnostics.len() > 0);
        local malformed = crowd.applySpawnBatch([{ stableId = "bad" }], options);
        assert(!malformed.ok && !malformed.hasValue);
        assert(crowd.getAgentCount() == 2 && !crowd.hasNamedAgent("bad"));
    )"));
}

TEST_CASE("crowd.spawn.preservesMovingAgentVelocityAndTarget") {
    Crowd crowd;
    crowd.setSeparationWeight(0.f);
    const int original = crowd.addNamedAgent("moving", 0.f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentAction(original, "seek"));
    REQUIRE(crowd.setAgentTarget(original, 100.f, 0.f));
    REQUIRE(crowd.advance(0.1f).ok());
    const auto before = crowd.getAgentState(original);
    REQUIRE(before.vx > 0.f);
    SpawnBatch batch;
    batch.agents = {{"new", before.x - 1.5f, before.y, 0.f, 1.f, {}}};
    auto result  = crowd.applySpawnBatch(batch);
    REQUIRE(result.ok());
    const auto after = crowd.getAgentState(original);
    CHECK_EQ(after.vx, before.vx);
    CHECK_EQ(after.vy, before.vy);
    CHECK_EQ(after.speed, before.speed);
    CHECK_EQ(after.action, before.action);
    CHECK(after.x > before.x);
    REQUIRE(crowd.advance(0.1f).ok());
    CHECK(crowd.getAgentState(original).x > after.x);
}
