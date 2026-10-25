// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "script-binding",
  "module": "crowd",
  "origins": [
    "CrowdScript.cpp"
  ]
}
*/
// clang-format on

#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::crowd;

TEST_CASE("crowd.script.runtimeBindingContracts") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    const auto sharedVm = std::ref(vm);

    SUB_CASE("crowd.advance.scriptClockHasLosslessMetadataAndStrictIntegerInputs") {
        auto& vm = sharedVm.get();
    vm.run(vm.compileSource(R"(
            local crowd = eve.Crowd();
            local result = crowd.advanceTick({tick=1,deltaNanoseconds=16666667});
            assert(result.ok);
            assert(result.value.tick == "1");
            assert(result.value.elapsedNanoseconds == 16666667);
            assert(crowd.copyAgentFrame().value.tick == "1");
            assert(!crowd.advanceTick({tick=2.0,deltaNanoseconds=16666667}).ok);
            assert(!crowd.advanceTick({tick=2,deltaNanoseconds=1.5}).ok);
            assert(crowd.advanceTick({tick=2,deltaNanoseconds=16666667}).ok);
        )"));
    };

    SUB_CASE("crowd.contactPairs.scriptExtensionsAreOptionalAndStrictInteger") {
        auto& vm = sharedVm.get();
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
    };

    SUB_CASE("crowd.navigation.scriptTokensBoundWorkAndRejectWrongKindsOwnersAndGenerations") {
        auto& vm = sharedVm.get();
    vm.run(vm.compileSource(R"(
            local c = eve.Crowd(), other = eve.Crowd();
            c.resizeField(5,5,1.0,0.0,0.0);
            local token = c.requestFlowBuild(4,4).value;
            assert(token.kind == "crowd.flowRequest" && token.schema == 1);
            assert(c.getFlowRequest(token).value.state == "pending");
            assert(!c.commitFlowRequest(token).ok);
            assert(!other.advanceFlowRequest(token,3).ok);
            local fake = clone token;
            fake.index = token.index + 0.5;
            assert(!c.getFlowRequest(fake).ok);
            fake = clone token; fake.kind = "crowd.agent";
            assert(!c.getFlowRequest(fake).ok);
            local calls = 0;
            while (c.getFlowRequest(token).value.state == "pending") {
                local result = c.advanceFlowRequest(token,2);
                assert(result.ok);
                assert(result.value.heapPops + result.value.directionCells <= 2);
                assert(++calls < 1000);
            }
            assert(c.commitFlowRequest(token).ok);
            assert(c.getFlowRequest(token).value.state == "committed");
            assert(!c.commitFlowRequest(token).ok);
            assert(c.releaseFlowRequest(token).ok);
            assert(!c.getFlowRequest(token).ok);
            local reused = c.requestFlowBuild(0,4).value;
            assert(reused.index == token.index && reused.generation != token.generation);
            c.setBlocked(2,2,true);
            assert(c.getFlowRequest(reused).value.state == "cancelled");
            assert(c.releaseFlowRequest(reused).ok);
        )"));
    };

    SUB_CASE("crowd.navigation.scriptRegistryCapacityIsRecoverableAfterRelease") {
        auto& vm = sharedVm.get();
    vm.run(vm.compileSource(R"(
            local c = eve.Crowd(); c.resizeField(2,2,1.0,0.0,0.0);
            local tokens = [];
            for(local i=0; i<64; ++i) tokens.append(c.requestFlowBuild(1,1).value);
            assert(!c.requestFlowBuild(1,1).ok);
            assert(c.cancelFlowRequest(tokens[0]).ok);
            assert(c.cancelFlowRequest(tokens[0]).ok);
            assert(c.releaseFlowRequest(tokens[0]).ok);
            assert(c.requestFlowBuild(1,1).ok);
            assert(!c.getFlowRequest(tokens[0]).ok);
        )"));
    };

    SUB_CASE("crowd.navigation.restoreInvalidatesTransientScriptRequests") {
        auto& vm = sharedVm.get();
    vm.addFunc("restoreCurrentWorld", [](Crowd* crowd) {
            auto snapshot = crowd->captureSnapshot().expect("capture test world");
            auto plan     = crowd->prepareRestore(snapshot).expect("prepare test restore");
            crowd->commitRestore(std::move(plan)).expect("commit test restore");
        });
        vm.run(vm.compileSource(R"(
            local c = eve.Crowd(); c.resizeField(3,3,1.0,0.0,0.0);
            local token = c.requestFlowBuild(2,2).value;
            restoreCurrentWorld(c);
            assert(!c.getFlowRequest(token).ok);
            assert(!c.advanceFlowRequest(token,1).ok);
            local fresh = c.requestFlowBuild(2,2).value;
            assert(fresh.owner != token.owner);
            assert(c.getFlowRequest(fresh).value.state == "pending");
        )"));
    };

    SUB_CASE("crowd.navigation.scriptRequestsRemainValidAcrossOtherGoalPublications") {
        auto& vm = sharedVm.get();
    vm.run(vm.compileSource(R"(
            local c=eve.Crowd(); c.resizeField(3,3,1.0,0.0,0.0);
            local input=c.getFieldStatus().value.inputRevision;
            local a=c.requestFlowBuild(0,1).value, b=c.requestFlowBuild(2,1).value;
            while(c.getFlowRequest(a).value.state=="pending") assert(c.advanceFlowRequest(a,2).ok);
            assert(c.commitFlowRequest(a).ok);
            assert(c.getFieldStatus().value.inputRevision==input);
            while(c.getFlowRequest(b).value.state=="pending") assert(c.advanceFlowRequest(b,1).ok);
            assert(c.commitFlowRequest(b).ok);
            assert(c.getFieldStatus().value.inputRevision==input);
            assert(c.releaseFlowRequest(a).ok); assert(c.releaseFlowRequest(b).ok);
        )"));
    };

    SUB_CASE("crowd.navigation.scriptRequestOwnedLeaseSamplesAndReleasesSafely") {
        auto& vm = sharedVm.get();
    vm.run(vm.compileSource(R"(
            local c=eve.Crowd(), other=eve.Crowd();
            c.resizeField(3,3,1.0,0.0,0.0);
            local request=c.requestFlowBuild(2,1).value;
            assert(!c.acquireFlowRequestLease(request).ok);
            while(c.getFlowRequest(request).value.state=="pending") assert(c.advanceFlowRequest(request,2).ok);
            assert(c.acquireFlowRequestLease(request).ok);
            assert(!c.acquireFlowRequestLease(request).ok);
            assert(c.sampleFlowRequestLease(request,1.5,1.5).value.state=="reachable");
            assert(c.sampleFlowRequestLease(request,100.0,100.0).value.state=="unreachable");
            assert(!other.sampleFlowRequestLease(request,1.5,1.5).ok);
            c.setBlocked(0,0,true);
            assert(!c.sampleFlowRequestLease(request,1.5,1.5).ok);
            assert(c.releaseFlowRequest(request).ok);
            assert(!c.sampleFlowRequestLease(request,1.5,1.5).ok);
        )"));
    };

    SUB_CASE("crowd.navigation.scriptAgentsKeepLeaseAfterRequestReleaseAndRejectAtomicBadBindings") {
        auto& vm = sharedVm.get();
    vm.run(vm.compileSource(R"(
            local c=eve.Crowd(); c.resizeField(5,5,1.0,0.0,0.0);
            c.setDefaultSpeed(2.0);c.setSeparationWeight(0.0);c.setClampToField(false);
            local request=c.requestFlowBuild(4,2).value;
            while(c.getFlowRequest(request).value.state=="pending") assert(c.advanceFlowRequest(request,2).ok);
            local generation=c.acquireFlowRequestLease(request).value.generation;
            local ref=c.agentRef(c.addNamedAgent("linked",2.5,2.5,0.0,0.1)).value;
            assert(c.updateAgents([{reference=ref,flowRequest=request}]).ok);
            assert(c.readAgent(ref).value.flowGeneration==generation);
            local calls=0;
            local hostile={}; hostile.setdelegate({_get=function(key) {calls++;return 1;}});
            assert(!c.updateAgents([{reference=ref,flowRequest=hostile}]).ok);
            assert(calls==0);
            local fake=clone request; fake.generation+=1;
            assert(!c.updateAgents([{reference=ref,data=7,flowRequest=fake}]).ok);
            assert(c.readAgent(ref).value.data==0);
            assert(!c.updateAgents([{reference=ref,flowRequest=request,useDefaultFlow=true}]).ok);
            assert(c.releaseFlowRequest(request).ok);
            assert(c.advance(0.01).ok);
            assert(c.readAgent(ref).value.vx>0.0);
            assert(!c.updateAgents([{reference=ref,flowRequest=request}]).ok);
            assert(c.updateAgents([{reference=ref,useDefaultFlow=true}]).ok);
            assert(c.readAgent(ref).value.flowGeneration=="0");
        )"));
    };

    SUB_CASE("crowd.policy.scriptCanonicalPolicyAndAtomicBatchBoundary") {
        auto& vm = sharedVm.get();
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
    };

    SUB_CASE("crowd.registry.scriptReferencesAndBatchesHaveCheckedBoundary") {
        auto& vm = sharedVm.get();
    vm.run(vm.compileSource(R"(
            local crowd = eve.Crowd();
            local slot = crowd.addNamedAgent("a", 1.0, 2.0, 0.0, 1.0);
            local ref = crowd.agentRef(slot).value;
            assert(crowd.resolveNamedAgent("a").value.generation == ref.generation);
            assert(crowd.isAgentAlive(ref));
            assert(crowd.readAgent(ref).value.x == 1.0);
            assert(!crowd.readAgent({ owner = ref.owner.tofloat(), index = ref.index, generation = ref.generation }).ok);
            local changed = crowd.updateAgents([{ reference = ref, data = 19, holdPosition = true }]);
            assert(changed.ok && changed.value.applied == 1);
            assert(crowd.readAgent(ref).value.data == 19);
            assert(crowd.relocateAgents([{ reference = ref, x = 4.0, y = 5.0 }], 100).ok);
            assert(crowd.readAgent(ref).value.x == 4.0);
            assert(!crowd.relocateAgents([{ reference = ref, x = 6.0, y = 5.0 }], 1).ok);
            assert(!crowd.updateAgents([{ reference = ref, data = 4 }, { reference = ref, data = 6 }]).ok);
            assert(crowd.readAgent(ref).value.data == 19);
            local frame = crowd.copyAgentStates([ref]);
            assert(frame.ok && frame.value.agents[0].state.data == 19);
            assert(crowd.copyAgentFrame().value.agents.len() == 1);
            assert(crowd.removeAgents([ref]).ok);
            assert(!crowd.readAgent(ref).ok && !crowd.isAgentAlive(ref));
            assert(frame.value.agents[0].state.x == 4.0);
        )"));
    };

    SUB_CASE("crowd.registry.scriptMovementBindingRejectsCoercionAndPartialUpdates") {
        auto& vm = sharedVm.get();
    vm.run(vm.compileSource(R"(
            local c=eve.Crowd();
            local ref=c.agentRef(c.addNamedAgent("class",1.0,1.0,0.0,0.1)).value;
            assert(c.updateAgents([{reference=ref,movement={classId=7,revision=2}}]).ok);
            assert(c.readAgent(ref).value.movement.classId=="7");
            assert(c.copyAgentFrame().value.agents[0].state.movement.revision=="2");
            assert(!c.updateAgents([{reference=ref,data=10,movement={classId=7.0,revision=2}}]).ok);
            assert(!c.updateAgents([{reference=ref,movement={classId=7}}]).ok);
            assert(!c.updateAgents([{reference=ref,movement={classId=0,revision=2}}]).ok);
            assert(c.readAgent(ref).value.data==0);
            assert(c.updateAgents([{reference=ref,movement={classId=0,revision=0}}]).ok);
            assert(c.readAgent(ref).value.movement.classId=="0");
        )"));
    };

    SUB_CASE("crowd.revision.scriptStatusAndStepReportExposeStaleField") {
        auto& vm = sharedVm.get();
    vm.run(vm.compileSource(R"(
            local crowd = eve.Crowd();
            assert(crowd.getFieldStatus().value.state == "unconfigured");
            crowd.resizeField(8,8,1.0,0.0,0.0);
            assert(crowd.getFieldStatus().value.state == "unbuilt");
            crowd.buildFlowField(7,0);
            local built = crowd.getFieldStatus().value;
            assert(built.state == "ready");
            assert(built.revision == built.builtRevision);
            crowd.setBlocked(4,4,true);
            local stale = crowd.getFieldStatus().value;
            assert(stale.state == "stale");
            assert(stale.revision != stale.builtRevision);
            assert(typeof stale.revision == "string");
            local report = crowd.advanceTick({tick=1,deltaNanoseconds=10000000});
            assert(report.ok);
            assert(report.value.navigationState == 3);
            assert(report.value.navigationRevision == stale.revision);
            assert(crowd.advanceTick({tick=1,deltaNanoseconds=0}).value.navigationState == 3);
            crowd.buildFlowField(7,0);
            assert(crowd.getFieldStatus().value.state == "ready");
        )"));
    };

    SUB_CASE("crowd.scheduler.preferredScriptBudgetsAreOptionalStrictIntegers") {
        auto& vm = sharedVm.get();
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
    };

    SUB_CASE("crowd.spatial.scriptBudgetExtensionIsOptionalAndStrictInteger") {
        auto& vm = sharedVm.get();
    vm.run(vm.compileSource(R"(
            local crowd = eve.Crowd();
            crowd.setClampToField(false);
            crowd.setDefaultSpeed(0.0);
            crowd.addAgent(0.0,0.0,0.0,0.5);
            local budget = {maxSubsteps=1024,maxContactPasses=12,maxGridCells=10,
                maxNeighborVisits=10000,maxPredictionChecks=10000,maxContactChecks=10000};
            local first = crowd.advanceTick({tick=1,deltaNanoseconds=10000000,budget=budget});
            assert(first.ok && first.value.spatialVisits > 0 && first.value.gridEntriesPeak == 1);
            budget.maxSpatialVisits <- 0;
            assert(!crowd.advanceTick({tick=2,deltaNanoseconds=10000000,budget=budget}).ok);
            budget.maxSpatialVisits = 1.5;
            assert(!crowd.advanceTick({tick=2,deltaNanoseconds=10000000,budget=budget}).ok);
            budget.maxSpatialVisits = 10000;
            assert(crowd.advanceTick({tick=2,deltaNanoseconds=10000000,budget=budget}).ok);
        )"));
    };
}
