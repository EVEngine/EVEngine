#include "zeroerr/assert.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::crowd;

TEST_CASE("crowd.contactPairs.namedClusterIsBitExactAcrossCreationOrderAndRestore") {
    Crowd first, second;
    for (auto* crowd : {&first, &second}) {
        crowd->setClampToField(false);
        crowd->setDefaultSpeed(0.f);
        crowd->setSeparationWeight(0.f);
    }
    for (int i = 0; i < 10; ++i) {
        REQUIRE(first.addNamedAgent("pair/" + std::to_string(i), float((i * 7) % 13) * 0.021f,
                                    float((i * 5) % 11) * 0.023f, 0.f, 1.f) >= 0);
        const int j = 9 - i;
        REQUIRE(second.addNamedAgent("pair/" + std::to_string(j), float((j * 7) % 13) * 0.021f,
                                     float((j * 5) % 11) * 0.023f, 0.f, 1.f) >= 0);
    }
    const CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
    auto            saved = first.captureSnapshot();
    REQUIRE(saved.ok());
    auto a = first.advance(step), b = second.advance(step);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value().contactPairsPeak > 0);
    CHECK(a.value().pairVisits > a.value().contactPairsChecked);
    for (int i = 0; i < 10; ++i) {
        const auto name  = "pair/" + std::to_string(i);
        auto       left  = first.readAgent(first.resolveNamedAgent(name).takeValue()).takeValue();
        auto       right = second.readAgent(second.resolveNamedAgent(name).takeValue()).takeValue();
        CHECK_EQ(left.x, right.x);
        CHECK_EQ(left.y, right.y);
    }
    const auto expected = first.readAgent(first.resolveNamedAgent("pair/0").takeValue()).takeValue();
    auto       restored = first.prepareRestore(saved.value());
    REQUIRE(restored.ok());
    REQUIRE(first.commitRestore(std::move(restored).takeValue()).ok());
    REQUIRE(first.advance(step).ok());
    auto replayed = first.readAgent(first.resolveNamedAgent("pair/0").takeValue()).takeValue();
    CHECK_EQ(replayed.x, expected.x);
    CHECK_EQ(replayed.y, expected.y);
}

TEST_CASE("crowd.contactPairs.capacityAndConstructionBudgetRejectAtomically") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(0.f);
    crowd.setSeparationWeight(0.f);
    REQUIRE(crowd.addAgent(0.f, 0.f, 0.f, 1.f) >= 0);
    REQUIRE(crowd.addAgent(0.3f, 0.f, 0.f, 1.f) >= 0);
    CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
    step.budget.maxContactPairs = 0;
    REQUIRE(!crowd.advance(step).ok());
    CHECK_EQ(crowd.getAgentState(0).x, 0.f);
    CHECK_EQ(crowd.getAgentState(1).x, 0.3f);
    step.budget.maxContactPairs = 1;
    step.budget.maxPairVisits   = 0;
    REQUIRE(!crowd.advance(step).ok());
    CHECK_EQ(crowd.getAgentState(0).x, 0.f);
    CHECK_EQ(crowd.getAgentState(1).x, 0.3f);
    step.budget.maxPairVisits = 10000;
    auto accepted             = crowd.advance(step);
    REQUIRE(accepted.ok());
    CHECK_EQ(accepted.value().contactPairsPeak, 1);
    CHECK(accepted.value().pairVisits > 0);
    CHECK(crowd.getAgentState(0).x < 0.f);
    // Fail one unit below the measured complete construction/gather cost, then retry the same tick.
    Crowd bounded;
    bounded.setClampToField(false);
    bounded.setDefaultSpeed(0.f);
    bounded.setSeparationWeight(0.f);
    REQUIRE(bounded.addAgent(0.f, 0.f, 0.f, 1.f) >= 0);
    REQUIRE(bounded.addAgent(0.3f, 0.f, 0.f, 1.f) >= 0);
    step.budget.maxPairVisits = accepted.value().pairVisits - 1;
    REQUIRE(!bounded.advance(step).ok());
    CHECK_EQ(bounded.getAgentState(0).x, 0.f);
    CHECK_EQ(bounded.getAgentState(1).x, 0.3f);
    step.budget.maxPairVisits = accepted.value().pairVisits;
    auto retry                = bounded.advance(step);
    REQUIRE(retry.ok());
    CHECK_EQ(retry.value().pairVisits, accepted.value().pairVisits);
    CHECK_EQ(bounded.getAgentState(0).x, crowd.getAgentState(0).x);
    CHECK_EQ(bounded.getAgentState(1).x, crowd.getAgentState(1).x);
}

TEST_CASE("crowd.contactPairs.filteredPairsDoNotUsePairBufferCapacity") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(0.f);
    crowd.setSeparationWeight(0.f);
    const int a = crowd.addAgent(0.f, 0.f, 0.f, 1.f), b = crowd.addAgent(0.3f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentInteraction(a, {1.f, false, 1, 1}).ok());
    REQUIRE(crowd.setAgentInteraction(b, {1.f, false, 2, 2}).ok());
    CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
    step.budget.maxContactPairs = 0;
    auto result                 = crowd.advance(step);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().contactPairsPeak, 0);
    CHECK_EQ(result.value().contactPairsChecked, 0);
}

TEST_CASE("crowd.contactPairs.scriptExtensionsAreOptionalAndStrictInteger") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    vm.run(vm.compileSource(R"(
        local c = eve.Crowd(); c.setClampToField(false); c.setDefaultSpeed(0.0);
        c.addAgent(0.0,0.0,0.0,1.0); c.addAgent(0.3,0.0,0.0,1.0);
        local budget={maxSubsteps=1024,maxContactPasses=12,maxGridCells=100,
            maxNeighborVisits=10000,maxPredictionChecks=10000,maxContactChecks=10000};
        budget.maxContactPairs <- 0;
        assert(!c.advanceTick({tick=1,deltaNanoseconds=10000000,budget=budget}).ok);
        budget.maxContactPairs=1.0;
        assert(!c.advanceTick({tick=1,deltaNanoseconds=10000000,budget=budget}).ok);
        budget.maxContactPairs=1;
        local r=c.advanceTick({tick=1,deltaNanoseconds=10000000,budget=budget});
        assert(r.ok && r.value.contactPairsPeak == 1 && r.value.pairVisits > 0);
        budget.maxPairVisits <- 0;
        assert(!c.advanceTick({tick=2,deltaNanoseconds=10000000,budget=budget}).ok);
        budget.maxPairVisits=10000;
        assert(c.advanceTick({tick=2,deltaNanoseconds=10000000,budget=budget}).ok);
    )"));
}
