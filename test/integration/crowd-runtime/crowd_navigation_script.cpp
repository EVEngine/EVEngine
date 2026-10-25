// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "cases": {
    "script-binding": [
      "crowd.navigation.scriptTokensBoundWorkAndRejectWrongKindsOwnersAndGenerations",
      "crowd.navigation.scriptRegistryCapacityIsRecoverableAfterRelease",
      "crowd.navigation.scriptRequestsRemainValidAcrossOtherGoalPublications",
      "crowd.navigation.scriptRequestOwnedLeaseSamplesAndReleasesSafely",
      "crowd.navigation.scriptAgentsKeepLeaseAfterRequestReleaseAndRejectAtomicBadBindings"
    ]
  },
  "module": "crowd",
  "origins": [
    "crowd_navigation_script.cpp"
  ]
}
*/
// clang-format on

#include <simplesquirrel/simplesquirrel.hpp>
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"
using namespace eve::crowd;
TEST_CASE("crowd.navigation.scriptTokensBoundWorkAndRejectWrongKindsOwnersAndGenerations") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
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
}
TEST_CASE("crowd.navigation.scriptRegistryCapacityIsRecoverableAfterRelease") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
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
}
TEST_CASE("crowd.navigation.restoreInvalidatesTransientScriptRequests") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
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
}

TEST_CASE("crowd.navigation.scriptRequestsRemainValidAcrossOtherGoalPublications") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
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
}

TEST_CASE("crowd.navigation.scriptRequestOwnedLeaseSamplesAndReleasesSafely") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
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
}

TEST_CASE("crowd.navigation.scriptAgentsKeepLeaseAfterRequestReleaseAndRejectAtomicBadBindings") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
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
}
