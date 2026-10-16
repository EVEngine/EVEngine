#include "zeroerr/assert.h"
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <limits>

using namespace eve::crowd;

TEST_CASE("crowd.policy.noPredictiveAvoidanceStillResolvesBodyContact") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(4.f);
    crowd.setSeparationWeight(0.f);
    REQUIRE(crowd.configureAvoidance({true, 2.f, 0.1f, 32}).ok());
    const auto a = crowd.agentRef(crowd.addAgent(-0.1f, 0.f, 0.f, 0.5f)).takeValue();
    const auto b = crowd.agentRef(crowd.addAgent(0.1f, 0.f, 0.f, 0.5f)).takeValue();
    REQUIRE(crowd.setInteractionPolicy(a, {1.f, false, 1, 0, 1, 1}).ok());
    REQUIRE(crowd.setInteractionPolicy(b, {1.f, false, 1, 0, 1, 1}).ok());
    auto advanced = crowd.advance(1.f / 60.f);
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value().avoidanceChecks, 0);
    CHECK(advanced.value().contactPairsChecked > 0);
    CHECK_EQ(advanced.value().unresolvedContacts, 0);
    CHECK(std::abs(crowd.readAgent(a).value().x - crowd.readAgent(b).value().x) >= 0.999f);
}

TEST_CASE("crowd.policy.contactFilteringDoesNotDisablePredictiveSteering") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(4.f);
    crowd.setSeparationWeight(0.f);
    REQUIRE(crowd.configureAvoidance({true, 2.f, 0.1f, 32}).ok());
    const auto a = crowd.agentRef(crowd.addAgent(-0.1f, 0.f, 0.f, 0.5f)).takeValue();
    const auto b = crowd.agentRef(crowd.addAgent(0.1f, 0.f, 0.f, 0.5f)).takeValue();
    REQUIRE(crowd.setInteractionPolicy(a, {1.f, false, 1, 1, 0, 0}).ok());
    REQUIRE(crowd.setInteractionPolicy(b, {1.f, false, 1, 1, 0, 0}).ok());
    auto advanced = crowd.advance(1.f / 60.f);
    REQUIRE(advanced.ok());
    CHECK(advanced.value().avoidanceChecks > 0);
    CHECK_EQ(advanced.value().contactPairsChecked, 0);
    CHECK_EQ(advanced.value().unresolvedContacts, 0);
}

TEST_CASE("crowd.policy.spawnAndRelocationRespectContactInsteadOfAvoidanceMasks") {
    Crowd crowd;
    crowd.setClampToField(false);
    const auto held = crowd.agentRef(crowd.addNamedAgent("held", 0.f, 0.f, 0.f, 1.f)).takeValue();
    REQUIRE(crowd.setInteractionPolicy(held, {0.f, true, 1, 0, 1, 1}).ok());
    AgentCreateBatch batch;
    batch.policy           = SpawnPolicy::RejectOverlap;
    batch.agents           = {{"new", 0.f, 0.f, 0.f, 1.f, {}}};
    batch.agents[0].policy = InteractionPolicy{1.f, false, 1, 0, 1, 1};
    REQUIRE(!crowd.createAgents(batch).ok());
    CHECK(!crowd.hasNamedAgent("new"));
    batch.agents[0].x = 4.f;
    auto born         = crowd.createAgents(batch);
    REQUIRE(born.ok());
    const auto ref = born.value().created[0].reference;
    REQUIRE(!crowd.relocateAgents({{{ref, 0.f, 0.f}}, 100}).ok());
    CHECK_EQ(crowd.readAgent(ref).value().x, 4.f);
    CHECK_EQ(crowd.readInteractionPolicy(ref).value().avoidanceMask, 0);
}

TEST_CASE("crowd.policy.legacyFacadeAndHoldUpdatesHaveExplicitSemantics") {
    Crowd      crowd;
    const int  slot = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
    const auto ref  = crowd.agentRef(slot).takeValue();
    REQUIRE(crowd.setInteractionPolicy(ref, {0.25f, false, 1, 0, 2, 2}).ok());
    AgentUpdate hold;
    hold.reference    = ref;
    hold.holdPosition = true;
    REQUIRE(crowd.updateAgents({{hold}}).ok());
    auto modern = crowd.readInteractionPolicy(ref).takeValue();
    CHECK_EQ(modern.avoidanceMask, 0);
    CHECK_EQ(modern.contactLayer, 2);
    CHECK(modern.holdPosition);
    const auto compatibility = crowd.getAgentInteraction(slot).takeValue();
    CHECK_EQ(compatibility.layer, 2);
    REQUIRE(crowd.setAgentInteraction(slot, {1.f, false, 4, 4}).ok());
    modern = crowd.readInteractionPolicy(ref).takeValue();
    CHECK_EQ(modern.contactLayer, 4);
    CHECK_EQ(modern.avoidanceLayer, 4);
    auto bad          = modern;
    bad.avoidanceMask = -1;
    REQUIRE(!crowd.setInteractionPolicy(ref, bad).ok());
    bad             = modern;
    bad.pushability = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!crowd.setInteractionPolicy(ref, bad).ok());
    CHECK_EQ(crowd.readInteractionPolicy(ref).value().pushability, 1.f);
}

TEST_CASE("crowd.policy.scriptCanonicalPolicyAndAtomicBatchBoundary") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    vm.run(vm.compileSource(R"(
        local crowd = eve.Crowd();
        local ref = crowd.agentRef(crowd.addAgent(0.0,0.0,0.0,1.0)).value;
        local policy = {pushability=0.5,holdPosition=false,avoidanceLayer=1,avoidanceMask=0,
                        contactLayer=2,contactMask=2};
        assert(crowd.setInteractionPolicy(ref,policy).ok);
        assert(crowd.readInteractionPolicy(ref).value.contactLayer == 2);
        assert(crowd.updateAgents([{reference=ref,holdPosition=true}]).ok);
        assert(crowd.readInteractionPolicy(ref).value.avoidanceMask == 0);
        assert(!crowd.updateAgents([{reference=ref,policy=policy,holdPosition=false}]).ok);
        assert(crowd.readInteractionPolicy(ref).value.holdPosition);
        policy.avoidanceMask = 1.5;
        assert(!crowd.setInteractionPolicy(ref,policy).ok);
        assert(crowd.readInteractionPolicy(ref).value.avoidanceMask == 0);
    )"));
}
