#include "zeroerr/assert.h"
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"
using namespace eve::crowd;
namespace {
NavigationSurfaceSnapshot surface() {
    NavigationSurfaceSnapshot result;
    result.stamp = {NavigationSurfaceId(1), MovementClassId(1), 1, 1, 1, 0.5f};
    result.width = result.height = 5;
    result.cellSize              = 1.f;
    result.terrainCosts.assign(25, 1.f);
    result.navigationCosts.assign(25, 1.f);
    result.terrainCosts[12]    = 0.f;
    result.navigationCosts[12] = result.navigationCosts[11] = 0.f;
    return result;
}
}  // namespace
TEST_CASE("crowd.surface.atomicPublicationRejectsConflictsAndSeparatesPhysicalWalls") {
    Crowd crowd, other;
    auto  input = surface();
    auto  plan  = crowd.prepareNavigationSurface(input).takeValue();
    CHECK(crowd.getFieldStatus().surface.surface.isZero());
    REQUIRE(!other.commitNavigationSurface(std::move(plan)).ok());
    REQUIRE(crowd.commitNavigationSurface(std::move(plan)).ok());
    REQUIRE(!crowd.commitNavigationSurface(std::move(plan)).ok());
    CHECK_EQ(crowd.getFieldStatus().surface.surface.value(), 1);
    auto       identical = crowd.prepareNavigationSurface(input).takeValue();
    const auto revision  = crowd.getFieldStatus().revision;
    REQUIRE(crowd.commitNavigationSurface(std::move(identical)).ok());
    CHECK_EQ(crowd.getFieldStatus().revision, revision);
    auto conflict            = input;
    conflict.terrainCosts[0] = conflict.navigationCosts[0] = 2.f;
    REQUIRE(!crowd.prepareNavigationSurface(conflict).ok());
    conflict                     = input;
    conflict.navigationCosts[12] = 1.f;
    REQUIRE(!crowd.prepareNavigationSurface(conflict).ok());
    crowd.setDefaultSpeed(0.f);
    crowd.setSeparationWeight(0.f);
    const int agent = crowd.addAgent(1.5f, 2.5f, 0.f, 0.1f);
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK_EQ(crowd.getAgentState(agent).x, 1.5f);
    CHECK_EQ(crowd.getAgentState(agent).y, 2.5f);
    crowd.buildFlowField(4, 2);
    CHECK_EQ(crowd.flowAtCell(1, 2).x, 0.f);
    CHECK_EQ(crowd.flowAtCell(1, 2).y, 0.f);
    auto newer                  = input;
    newer.stamp.surfaceRevision = 2;
    auto ready                  = crowd.prepareNavigationSurface(newer).takeValue();
    auto stale                  = crowd.prepareNavigationSurface(newer).takeValue();
    REQUIRE(crowd.commitNavigationSurface(std::move(ready)).ok());
    REQUIRE(!crowd.commitNavigationSurface(std::move(stale)).ok());
    REQUIRE(!crowd.prepareNavigationSurface(input).ok());
    auto snapshot = crowd.captureSnapshot().takeValue();
    crowd.setBlocked(0, 0, true);
    CHECK(crowd.getFieldStatus().surface.surface.isZero());
    auto restored = crowd.prepareRestore(snapshot).takeValue();
    REQUIRE(crowd.commitRestore(std::move(restored)).ok());
    CHECK_EQ(crowd.getFieldStatus().surface.surfaceRevision, 2);
}
TEST_CASE("crowd.surface.oversizedFootprintDoesNotUseImportedNavigation") {
    Crowd crowd;
    auto  input = surface();
    auto  plan  = crowd.prepareNavigationSurface(input).takeValue();
    REQUIRE(crowd.commitNavigationSurface(std::move(plan)).ok());
    crowd.setClampToField(false);
    crowd.setSeparationWeight(0.f);
    crowd.setDefaultSpeed(2.f);
    crowd.buildFlowField(4, 0);
    const int agent    = crowd.addAgent(0.5f, 0.5f, 0.f, 1.f);
    auto      advanced = crowd.advance(0.01f);
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value().navigationClassMismatches, 1);
    CHECK_EQ(crowd.getAgentState(agent).vx, 0.f);
    CHECK_EQ(crowd.getAgentState(agent).vy, 0.f);
}

TEST_CASE("crowd.surface.perAgentMovementBindingsSurviveSwapAndRestore") {
    Crowd crowd;
    auto  input = surface();
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
    crowd.setClampToField(false);
    crowd.setSeparationWeight(0.f);
    crowd.setDefaultSpeed(2.f);
    crowd.buildFlowField(4, 0);
    const int   first  = crowd.addNamedAgent("first", 0.5f, 0.5f, 0.f, 0.1f);
    const int   second = crowd.addNamedAgent("second", 0.5f, 4.5f, 0.f, 0.1f);
    const auto  a = crowd.agentRef(first).takeValue(), b = crowd.agentRef(second).takeValue();
    AgentUpdate ua, ub;
    ua.reference = a;
    ua.movement  = MovementBinding{MovementClassId(1), 1};
    ub.reference = b;
    ub.movement  = MovementBinding{MovementClassId(2), 1};
    REQUIRE(crowd.updateAgents({{ua, ub}}).ok());
    auto advanced = crowd.advance(0.01f);
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value().navigationClassMismatches, 1);
    CHECK(crowd.readAgent(a).value().speed > 0.f);
    CHECK_EQ(crowd.readAgent(b).value().speed, 0.f);
    REQUIRE(crowd.removeAgents({{a}}).ok());
    CHECK(crowd.readAgent(b).value().movement == *ub.movement);
    auto saved  = crowd.captureSnapshot().takeValue();
    ub.movement = MovementBinding{MovementClassId(1), 0};
    REQUIRE(!crowd.updateAgents({{ub}}).ok());
    ub.movement = MovementBinding{};
    REQUIRE(crowd.updateAgents({{ub}}).ok());
    REQUIRE(crowd.commitRestore(crowd.prepareRestore(saved).takeValue()).ok());
    auto restored = crowd.resolveNamedAgent("second").takeValue();
    CHECK_EQ(crowd.readAgent(restored).value().movement.movement.value(), 2);
    ub.reference = restored;
    ub.movement  = MovementBinding{MovementClassId(1), 2};
    REQUIRE(crowd.updateAgents({{ub}}).ok());
    CHECK_EQ(crowd.advance(0.f).value().navigationClassMismatches, 1);
    input.stamp.movementRevision = 2;
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
    crowd.buildFlowField(4, 0);
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK(crowd.readAgent(restored).value().speed > 0.f);
}
TEST_CASE("crowd.surface.explicitMovementRequiresImportedProviderAndAtomicCreation") {
    Crowd crowd;
    crowd.resizeField(6, 6, 1.f, 0.f, 0.f);
    crowd.setClampToField(false);
    crowd.setSeparationWeight(0.f);
    crowd.setDefaultSpeed(2.f);
    crowd.buildFlowField(5, 5);
    SpawnBatch batch;
    batch.policy = SpawnPolicy::RejectOverlap;
    SpawnRequest request;
    request.stableId = "explicit";
    request.x = request.y = 2.5f;
    request.radius        = 0.1f;
    request.movement      = {MovementClassId(5), 0};
    batch.agents.push_back(request);
    REQUIRE(!crowd.createAgents(batch).ok());
    CHECK_EQ(crowd.getAgentCount(), 0);
    batch.agents[0].movement.revision = 1;
    auto receipt                      = crowd.createAgents(batch);
    REQUIRE(receipt.ok());
    auto reference = receipt.value().created[0].reference;
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK_EQ(crowd.readAgent(reference).value().speed, 0.f);
    AgentUpdate update;
    update.reference = reference;
    update.movement  = MovementBinding{};
    REQUIRE(crowd.updateAgents({{update}}).ok());
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK(crowd.readAgent(reference).value().speed > 0.f);
}
