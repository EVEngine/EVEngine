// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "script-binding",
  "module": "crowd",
  "origins": [
    "crowd_contacts.cpp",
    "crowd_spawn.cpp",
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

namespace {
struct NativeRequestRestore {
    std::optional<Crowd::Snapshot>    frozen;
    std::optional<Crowd::RestorePlan> candidate;
    ssq::VM                           vm{1024, ssq::Libs::ALL};
    bool                              persistent  = false;
    std::uint64_t                     sourceOwner = 0;
    explicit NativeRequestRestore(bool usePersistent = false) : persistent(usePersistent) {
        auto eve = vm.addTable("eve");
        auto cls = eve.addClass<Crowd>("Crowd");
        Crowd::expose(cls);
        vm.addFunc("freezeNative", [this](Crowd* source) {
            frozen.emplace(source->captureSnapshot().expect("freeze native registry/workstate"));
            sourceOwner = source->getFieldStatus().owner;
            if (persistent) {
                const auto text =
                    frozen->encodeState().expect("seal backend/request graph").toJson().expect("snapshot file");
                frozen.emplace(Crowd::Snapshot::prepareState(eve::Value::fromJson(text).expect("owning backend file"))
                                   .expect("admit backend/request graph"));
                REQUIRE_EQ(frozen->encodeState().expect("repeat snapshot").toJson().expect("repeat bytes"), text);
            }
        });
        vm.addFunc("prepareNative", [this](Crowd* target) {
            candidate.emplace(target->prepareRestore(*frozen).expect("prepare native registry/workstate"));
        });
        vm.addFunc("remapNative", [this](std::int64_t owner, int index, int generation) -> std::int64_t {
            if (!candidate || owner <= 0 || index < 0 || generation <= 0) return 0;
            FlowRequestRef source{RuntimeHandle<CrowdFlowRequestTag>(static_cast<std::uint32_t>(index),
                                                                     static_cast<std::uint32_t>(generation)),
                                  static_cast<std::uint64_t>(owner)};
            if (persistent) {
                if (!frozen || static_cast<std::uint64_t>(owner) != sourceOwner) return 0;
                auto archived =
                    frozen->requestReference(static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(generation));
                if (!archived) return 0;
                source = std::move(archived).takeValue();
            }
            auto remapped = candidate->remapFlowRequest(source);
            return remapped ? static_cast<std::int64_t>(remapped.value().owner) : 0;
        });
        vm.addFunc("commitNative",
                   [this](Crowd* target) { return candidate && target->commitRestore(std::move(*candidate)).ok(); });
        vm.addFunc("dropNativeCapture", [this]() { frozen.reset(); });
        vm.addFunc("residentBytesNative", [](Crowd* target) -> std::int64_t {
            return static_cast<std::int64_t>(target->getFlowCacheStatus().residentBytes);
        });
        vm.addFunc("evictNative", [](Crowd* target) { return target->configureFlowCache(0).ok(); });
    }
    void run(const char* script) { vm.run(vm.compileSource(script)); }
};
}  // namespace

TEST_CASE("crowd.snapshot.persistentRegistryRejectsForgedWorkWithoutChangingSource") {
    NativeRequestRestore fixture;
    fixture.vm.addFunc("rejectWorkNative", [&](Crowd* source) {
        auto       snapshot = source->captureSnapshot().expect("actual work");
        const auto value    = snapshot.encodeState().expect("full registry");
        auto       request  = [](eve::Value& v) -> eve::Value::Array& {
            return *v.getIf<eve::Value::Object>()
                        ->at("requests")
                        .getIf<eve::Value::Array>()
                        ->front()
                        .getIf<eve::Value::Array>();
        };
        auto plan   = [&](eve::Value& v) -> eve::Value::Array& { return *request(v)[2].getIf<eve::Value::Array>(); };
        auto reject = [&](auto edit) {
            auto bad = value;
            edit(bad);
            REQUIRE(!Crowd::Snapshot::prepareState(bad).ok());
        };
        reject([&](eve::Value& v) { request(v)[0] = 0; });
        reject([&](eve::Value& v) { request(v)[1] = false; });
        reject([&](eve::Value& v) { plan(v)[0] = 99; });
        reject([&](eve::Value& v) { plan(v)[1] = false; });
        reject([&](eve::Value& v) { plan(v)[2] = "0"; });
        reject([&](eve::Value& v) { plan(v)[3] = eve::Value{}; });
        reject([&](eve::Value& v) { plan(v)[3].set("flowCursor", 1000000); });
        reject([&](eve::Value& v) {
            auto& rows = *v.getIf<eve::Value::Object>()->at("requests").getIf<eve::Value::Array>();
            rows[1].getIf<eve::Value::Array>()->at(2).getIf<eve::Value::Array>()->at(0) = 5;
        });  // Ready field is reachable.
        REQUIRE_EQ(snapshot.encodeState().expect("source unchanged").toJson().expect("bytes"),
                   value.toJson().expect("original bytes"));
    });
    fixture.run(R"(
        local c=eve.Crowd();c.resizeField(8,8,1.0,0.0,0.0);
        local p=c.requestFlowBuild(7,7).value;assert(c.advanceFlowRequest(p,5).ok);
        local ready=c.requestFlowBuild(6,6).value;while(c.getFlowRequest(ready).value.state=="pending")assert(c.advanceFlowRequest(ready,100).ok);
        rejectWorkNative(c);assert(c.getFlowRequest(p).value.state=="pending" && c.getFlowRequest(ready).value.state=="ready");
    )");
}
TEST_CASE("crowd.snapshot.requestRegistryResumesExactWorkAndRemapsTokens") {
    for (bool persistent : {false, true}) {
        NativeRequestRestore fixture(persistent);
        fixture.run(R"(
        function remapped(token) {
            local result = clone token;
            result.owner = remapNative(token.owner,token.index,token.generation);
            assert(result.owner != 0 && result.owner != token.owner);
            return result;
        }
        function finish(c, token) {
            while(c.getFlowRequest(token).value.state=="pending") assert(c.advanceFlowRequest(token,1000000).ok);
        }
        local c=eve.Crowd(); c.resizeField(32,32,1.0,0.0,0.0); c.setBlocked(0,0,true);
        local pending=c.requestFlowBuild(31,31).value;
        assert(c.advanceFlowRequest(pending,11).value.heapPops > 0);
        local ready=c.requestFlowBuild(2,2).value; finish(c,ready);
        local unreachable=c.requestFlowBuild(0,0).value; finish(c,unreachable);
        assert(c.getFlowRequest(unreachable).value.state=="unreachable");
        local cancelled=c.requestFlowBuild(4,4).value; assert(c.cancelFlowRequest(cancelled).ok);
        local committed=c.requestFlowBuild(5,5).value; finish(c,committed); assert(c.commitFlowRequest(committed).ok);
        local leased=c.requestFlowBuild(6,6).value; finish(c,leased); assert(c.acquireFlowRequestLease(leased).ok);
        local stale=c.requestFlowBuild(7,7).value; assert(c.releaseFlowRequest(stale).ok);
        local reused=c.requestFlowBuild(8,8).value;
        assert(stale.index==reused.index && stale.generation!=reused.generation);
        local expectedSample=c.sampleFlowRequestLease(leased,1.5,1.5).value;
        freezeNative(c);
        local expected=[];
        while(c.getFlowRequest(pending).value.state=="pending") {
            expected.append(c.advanceFlowRequest(pending,3).value);
            assert(expected.len()<10000);
        }
        c=null; collectgarbage();
        local restored=eve.Crowd(), wrong=eve.Crowd();
        prepareNative(restored);
        assert(remapNative(stale.owner,stale.index,stale.generation)==0);
        assert(remapNative(pending.owner+100000,pending.index,pending.generation)==0);
        assert(remapNative(pending.owner,-1,pending.generation)==0);
        local a=remapped(pending), b=remapped(ready), u=remapped(unreachable),
              d=remapped(cancelled), e=remapped(committed), f=remapped(leased), g=remapped(reused);
        assert(!restored.getFlowRequest(a).ok);
        assert(!commitNative(wrong));
        dropNativeCapture();
        assert(commitNative(restored));
        assert(remapNative(pending.owner,pending.index,pending.generation)==0);
        assert(!commitNative(restored));
        assert(!restored.getFlowRequest(pending).ok);
        assert(restored.getFlowRequest(a).value.state=="pending");
        assert(restored.getFlowRequest(b).value.state=="ready");
        assert(restored.getFlowRequest(u).value.state=="unreachable");
        assert(restored.getFlowRequest(d).value.state=="cancelled");
        assert(restored.getFlowRequest(e).value.state=="committed");
        assert(restored.getFlowRequest(f).value.state=="committed");
        assert(restored.getFlowRequest(g).value.state=="pending");
        local actualSample=restored.sampleFlowRequestLease(f,1.5,1.5).value;
        assert(actualSample.x==expectedSample.x && actualSample.y==expectedSample.y && actualSample.state==expectedSample.state);
        foreach(progress in expected) {
            local actual=restored.advanceFlowRequest(a,3).value;
            assert(actual.state==progress.state && actual.heapPops==progress.heapPops && actual.directionCells==progress.directionCells);
        }
        assert(restored.getFlowRequest(a).value.state=="ready");
        assert(restored.commitFlowRequest(a).ok);
        foreach(token in [a,b,u,d,e,f,g]) assert(restored.releaseFlowRequest(token).ok);
        assert(!restored.getFlowRequest(g).ok);
        local next=restored.requestFlowBuild(1,1).value;
        assert(next.index==a.index && next.generation==a.generation+1);
    )");
    }
}

TEST_CASE("crowd.snapshot.requestAndAgentPinsAreIndependentAcrossRestoredWorlds") {
    for (bool persistent : {false, true}) {
        NativeRequestRestore fixture(persistent);
        fixture.run(R"(
        function remapped(token) {
            local result=clone token;
            result.owner=remapNative(token.owner,token.index,token.generation);
            assert(result.owner!=0); return result;
        }
        local c=eve.Crowd(); c.resizeField(8,8,1.0,0.0,0.0);
        local request=c.requestFlowBuild(7,7).value;
        while(c.getFlowRequest(request).value.state=="pending") assert(c.advanceFlowRequest(request,3).ok);
        assert(c.acquireFlowRequestLease(request).ok);
        local agent=c.agentRef(c.addNamedAgent("linked",1.5,1.5,0.0,0.1)).value;
        assert(c.updateAgents([{reference=agent,flowRequest=request}]).ok);
        freezeNative(c);
        local first=eve.Crowd(), second=eve.Crowd();
        prepareNative(first); local a=remapped(request); assert(commitNative(first));
        prepareNative(second); local b=remapped(request); assert(commitNative(second));
        assert(a.owner!=b.owner);
        assert(!first.sampleFlowRequestLease(b,1.5,1.5).ok);
        assert(!evictNative(first));
        assert(first.releaseFlowRequest(a).ok);
        assert(!evictNative(first));
        local restoredAgent=first.agentRef(0).value;
        assert(first.updateAgents([{reference=restoredAgent,useDefaultFlow=true}]).ok);
        assert(evictNative(first));
        assert(second.sampleFlowRequestLease(b,1.5,1.5).value.state=="reachable");
        assert(c.sampleFlowRequestLease(request,1.5,1.5).value.state=="reachable");
        assert(!evictNative(second));
        assert(second.releaseFlowRequest(b).ok);
        second.clearAgents(); assert(evictNative(second));
        c=null; collectgarbage();
        local third=eve.Crowd(); prepareNative(third); local t=remapped(request);
        dropNativeCapture(); assert(commitNative(third));
        assert(!evictNative(third));
        assert(third.sampleFlowRequestLease(t,1.5,1.5).ok);
        assert(third.releaseFlowRequest(t).ok); third.clearAgents(); assert(evictNative(third));
    )");
    }
}

TEST_CASE("crowd.snapshot.staleRequestInputsStayStaleAndRetiredTokensCannotRevive") {
    for (bool persistent : {false, true}) {
        NativeRequestRestore fixture(persistent);
        fixture.run(R"(
        local c=eve.Crowd(); c.resizeField(8,8,1.0,0.0,0.0);
        local pending=c.requestFlowBuild(7,7).value;
        assert(c.advanceFlowRequest(pending,5).ok);
        local leased=c.requestFlowBuild(6,6).value;
        while(c.getFlowRequest(leased).value.state=="pending") assert(c.advanceFlowRequest(leased,3).ok);
        assert(c.acquireFlowRequestLease(leased).ok);
        local retired=c.requestFlowBuild(5,5).value; assert(c.releaseFlowRequest(retired).ok);
        c.setBlocked(0,0,true);
        freezeNative(c);
        assert(c.getFlowRequest(pending).value.state=="cancelled");
        assert(!c.sampleFlowRequestLease(leased,1.5,1.5).ok);
        c=null; collectgarbage();
        local t=eve.Crowd(); prepareNative(t);
        local p=clone pending, l=clone leased;
        p.owner=remapNative(p.owner,p.index,p.generation);
        l.owner=remapNative(l.owner,l.index,l.generation);
        assert(p.owner!=0 && l.owner==p.owner);
        assert(remapNative(retired.owner,retired.index,retired.generation)==0);
        dropNativeCapture(); assert(commitNative(t));
        assert(t.getFlowRequest(p).value.state=="cancelled");
        assert(!t.advanceFlowRequest(p,3).ok);
        assert(!t.sampleFlowRequestLease(l,1.5,1.5).ok);
        assert(t.releaseFlowRequest(p).ok); assert(t.releaseFlowRequest(l).ok);
    )");
    }
}


TEST_CASE("crowd.snapshot.requestWorkingCapacityPreservesLaterCacheAdmissionCost") {
    for (bool persistent : {false, true}) {
        NativeRequestRestore fixture(persistent);
        fixture.run(R"(
        local c=eve.Crowd(); c.resizeField(32,32,1.0,0.0,0.0);
        local request=c.requestFlowBuild(31,31).value;
        local steps=0;
        while(true) {
            local progress=c.advanceFlowRequest(request,1).value;
            assert(++steps<10000);
            if(progress.directionCells>0) break;
        }
        assert(c.getFlowRequest(request).value.state=="pending");
        freezeNative(c);
        while(c.getFlowRequest(request).value.state=="pending") assert(c.advanceFlowRequest(request,1000000).ok);
        assert(c.acquireFlowRequestLease(request).ok);
        local expectedBytes=residentBytesNative(c);
        c=null; collectgarbage();
        local t=eve.Crowd(); prepareNative(t);
        local mapped=clone request;
        mapped.owner=remapNative(request.owner,request.index,request.generation);
        assert(mapped.owner!=0);
        dropNativeCapture(); assert(commitNative(t));
        while(t.getFlowRequest(mapped).value.state=="pending") assert(t.advanceFlowRequest(mapped,1000000).ok);
        assert(t.acquireFlowRequestLease(mapped).ok);
        assert(residentBytesNative(t)==expectedBytes);
    )");
    }
}

// Contract section: crowd contacts
#include "PrivateTestScope.h"

#include <limits>
#include "crowd/Crowd.h"
#include "crowd/CrowdField.h"
// crowd_contacts.cpp
namespace test_crowd_contacts {
using namespace eve::crowd;


TEST_CASE("crowd.contacts.scriptAdvanceUsesCanonicalResult") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    vm.run(vm.compileSource(R"(
        local crowd = eve.Crowd();
        crowd.addAgent(3.0, 4.0, 0.0, 1.0);
        assert(crowd.configureAvoidance(true, 2.0, 0.1, 32).ok);
        assert(crowd.getAvoidanceSettings().enabled);
        assert(!crowd.configureAvoidance(true, -1.0, 0.1, 32).ok);
        assert(crowd.setAgentInteraction(0, 0.25, true, 1, 3).ok);
        local policy = crowd.getAgentInteraction(0);
        assert(policy.ok && policy.hasValue && policy.value.holdPosition);
        assert(policy.value.pushability == 0.25 && policy.value.mask == 3);
        assert(!crowd.setAgentInteraction(0, -1.0, false, 1, 3).ok);
        assert(!crowd.getAgentInteraction(123).ok);
        local rejected = crowd.advance(-1.0);
        assert(!rejected.ok && !rejected.hasValue);
        assert(rejected.diagnostics.len() > 0);
        local result = crowd.advance(0.016);
        assert(result.ok && result.hasValue);
        assert(result.value.substeps >= 1);
        assert(result.value.avoidanceChecks == 0);
        assert(result.value.avoidanceTruncations == 0);
        assert(result.value.unresolvedContacts == 0);
        assert(result.value.unresolvedWalls == 0);
    )"));
}
}  // namespace test_crowd_contacts


// Contract section: crowd spawn
#include "PrivateTestScope.h"

// crowd_spawn.cpp
namespace test_crowd_spawn {
using namespace eve::crowd;


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
}  // namespace test_crowd_spawn
