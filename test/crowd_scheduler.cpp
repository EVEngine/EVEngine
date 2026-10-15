#include <simplesquirrel/simplesquirrel.hpp>
#include "crowd/Crowd.h"
#include "crowd/CrowdWorkScheduler.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::crowd;
namespace {
class FailedScheduler final : public CrowdWorkScheduler {
public:
    Result<void> execute(int partitions, const std::function<void(int)>& body) override {
        for (int index = 0; index < partitions; ++index) body(index);
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "Injected joined failure", "scheduler"));
    }
};
}  // namespace
TEST_CASE("crowd.scheduler.parallelWithoutProviderRejectsAndSerialRemainsAvailable") {
    Crowd crowd;
    REQUIRE(!crowd.setWorkScheduler(CrowdExecution::Parallel, {}).ok());
    REQUIRE(!crowd.setWorkScheduler(static_cast<CrowdExecution>(99), {}).ok());
    REQUIRE(crowd.setWorkScheduler(CrowdExecution::Serial, {}).ok());
    REQUIRE(crowd.addAgent(0.f, 0.f, 0.f, 1.f) >= 0);
    REQUIRE(crowd.advance(CrowdStep{SimulationTick(1), Duration::fromNanoseconds(10000000), {}}).ok());
}
TEST_CASE("crowd.scheduler.joinedFailureRollsBackAndSnapshotPreservesCurrentConfiguration") {
    Crowd crowd;
    crowd.setClampToField(false);
    REQUIRE(crowd.addAgent(0.f, 0.f, 0.f, 1.f) >= 0);
    REQUIRE(crowd.addAgent(0.3f, 0.f, 0.f, 1.f) >= 0);
    auto snapshot = crowd.captureSnapshot();
    REQUIRE(snapshot.ok());
    REQUIRE(crowd.setWorkScheduler(CrowdExecution::Parallel, std::make_shared<FailedScheduler>()).ok());
    auto restore = crowd.prepareRestore(snapshot.value());
    REQUIRE(restore.ok());
    REQUIRE(crowd.commitRestore(std::move(restore).takeValue()).ok());
    const CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
    REQUIRE(!crowd.advance(step).ok());
    CHECK_EQ(crowd.getAgentState(0).x, 0.f);
    CHECK_EQ(crowd.getAgentState(1).x, 0.3f);
    REQUIRE(crowd.setWorkScheduler(CrowdExecution::Serial, {}).ok());
    REQUIRE(crowd.advance(step).ok());
}

TEST_CASE("crowd.scheduler.predictionBudgetPreflightIsExactAndRetryable") {
    Crowd oracle, bounded;
    for (auto* crowd : {&oracle, &bounded}) {
        crowd->setClampToField(false);
        crowd->setResolveOverlaps(false);
        crowd->setDefaultSpeed(4.f);
        crowd->setSeparationWeight(0.f);
        REQUIRE(crowd->configureAvoidance({true, 2.f, 0.1f, 16}).ok());
        REQUIRE(crowd->addAgent(0.f, 0.f, 0.f, 0.2f) >= 0);
        REQUIRE(crowd->addAgent(2.f, 0.f, 0.f, 0.2f) >= 0);
        REQUIRE(crowd->setAgentAction(0, "seek"));
        REQUIRE(crowd->setAgentAction(1, "seek"));
        REQUIRE(crowd->setAgentTarget(0, 10.f, 0.f));
        REQUIRE(crowd->setAgentTarget(1, -10.f, 0.f));
    }
    CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
    auto      expected = oracle.advance(step);
    REQUIRE(expected.ok());
    CHECK_EQ(expected.value().avoidanceChecks, 90);
    step.budget.maxPredictionChecks = expected.value().avoidanceChecks - 1;
    REQUIRE(!bounded.advance(step).ok());
    CHECK_EQ(bounded.getAgentState(0).x, 0.f);
    CHECK_EQ(bounded.getAgentState(1).x, 2.f);
    step.budget.maxPredictionChecks += 1;
    auto retry = bounded.advance(step);
    REQUIRE(retry.ok());
    CHECK_EQ(retry.value().avoidanceChecks, expected.value().avoidanceChecks);
    CHECK_EQ(bounded.getAgentState(0).vx, oracle.getAgentState(0).vx);
    CHECK_EQ(bounded.getAgentState(1).vx, oracle.getAgentState(1).vx);
}

TEST_CASE("crowd.scheduler.preferredCapacityAndWorkBudgetRejectAtomically") {
    Crowd oracle, bounded;
    for (auto* crowd : {&oracle, &bounded}) {
        crowd->setClampToField(false);
        crowd->setResolveOverlaps(false);
        crowd->setDefaultSpeed(4.f);
        crowd->setSeparationWeight(1.f);
        crowd->setSeparationRadius(1.f);
        REQUIRE(crowd->addAgent(0.f, 0.f, 0.f, 0.2f) >= 0);
        REQUIRE(crowd->addAgent(0.3f, 0.f, 0.f, 0.2f) >= 0);
    }
    CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
    auto      expected = oracle.advance(step);
    REQUIRE(expected.ok());
    CHECK_EQ(expected.value().preferredNeighborsPeak, 2);
    CHECK(expected.value().preferredVisits > 0);
    step.budget.maxPreferredNeighbors = 1;
    REQUIRE(!bounded.advance(step).ok());
    CHECK_EQ(bounded.getAgentState(0).x, 0.f);
    CHECK_EQ(bounded.getAgentState(1).x, 0.3f);
    step.budget.maxPreferredNeighbors = 2;
    step.budget.maxPreferredVisits    = expected.value().preferredVisits - 1;
    REQUIRE(!bounded.advance(step).ok());
    CHECK_EQ(bounded.getAgentState(0).x, 0.f);
    CHECK_EQ(bounded.getAgentState(1).x, 0.3f);
    step.budget.maxPreferredVisits += 1;
    auto retry = bounded.advance(step);
    REQUIRE(retry.ok());
    CHECK_EQ(retry.value().preferredVisits, expected.value().preferredVisits);
    CHECK_EQ(bounded.getAgentState(0).vx, oracle.getAgentState(0).vx);
    CHECK_EQ(bounded.getAgentState(1).vx, oracle.getAgentState(1).vx);
}
TEST_CASE("crowd.scheduler.preferredScriptBudgetsAreOptionalStrictIntegers") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    vm.run(vm.compileSource(R"(
        local c=eve.Crowd(); c.setClampToField(false); c.setSeparationRadius(1.0); c.setSeparationWeight(1.0);
        c.addAgent(0.0,0.0,0.0,0.2); c.addAgent(0.3,0.0,0.0,0.2);
        local budget={maxSubsteps=1024,maxContactPasses=12,maxGridCells=100,
            maxNeighborVisits=10000,maxPredictionChecks=10000,maxContactChecks=10000};
        budget.maxPreferredNeighbors <- 1;
        assert(!c.advanceTick({tick=1,deltaNanoseconds=10000000,budget=budget}).ok);
        budget.maxPreferredNeighbors=2.0;
        assert(!c.advanceTick({tick=1,deltaNanoseconds=10000000,budget=budget}).ok);
        budget.maxPreferredNeighbors=2;
        budget.maxPreferredVisits <- 0;
        assert(!c.advanceTick({tick=1,deltaNanoseconds=10000000,budget=budget}).ok);
        budget.maxPreferredVisits=10000;
        local r=c.advanceTick({tick=1,deltaNanoseconds=10000000,budget=budget});
        assert(r.ok && r.value.preferredNeighborsPeak == 2 && r.value.preferredVisits > 0);
        local d=eve.Crowd(); d.setClampToField(false);
        d.addAgent(0.0,0.0,0.0,0.2); d.addAgent(0.3,0.0,0.0,0.2);
        local legacy=d.advance(0.01);
        assert(legacy.ok && legacy.value.preferredNeighborsPeak == 2 && legacy.value.preferredVisits > 0);
    )"));
}

TEST_CASE("crowd.scheduler.preferredRetainsFloatForceBoundarySemantics") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setResolveOverlaps(false);
    crowd.setDefaultSpeed(4.f);
    crowd.setSeparationWeight(0.f);
    crowd.setSeparationRadius(2.f);
    crowd.setCohesionWeight(1.f);
    crowd.setPerceptionRadius(1.f);
    REQUIRE(crowd.addAgent(-0.0000000298023223876953125f, 0.f, 0.f, 0.2f) >= 0);
    REQUIRE(crowd.addAgent(1.f, 0.f, 0.f, 0.2f) >= 0);
    // Double query distance exceeds one, but the existing float force subtraction rounds to one.
    auto result = crowd.advance(CrowdStep{SimulationTick(1), Duration::fromNanoseconds(10000000), {}});
    REQUIRE(result.ok());
    CHECK_EQ(result.value().preferredNeighborsPeak, 2);
    CHECK(crowd.getAgentState(0).vx > 0.f);
    CHECK(crowd.getAgentState(1).vx < 0.f);
}
