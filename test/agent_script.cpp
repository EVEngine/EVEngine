#include <filesystem>
#include <simplesquirrel/simplesquirrel.hpp>
#include "agent/AgentModule.h"
#include "zeroerr/unittest.h"

namespace {
void exposeAgent(ssq::VM& vm) {
    auto eve = vm.addTable("eve");
    auto cls = eve.addClass<eve::agent::Agent>("Agent");
    eve::agent::Agent::expose(cls);
}
}  // namespace
TEST_CASE("agent.scriptTrainInferReplayExample") {
    ssq::VM vm(1024);
    exposeAgent(vm);
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() / "examples/agent/train.nut";
    vm.run(vm.compileFile(path.string().c_str()));
    vm.run(vm.compileSource(R"(
        assert(agentExampleReport.policy.schemaId == "evengine.agent.policy");
        assert(agentExampleReport.trainingSamples > 0);
        assert(agentExampleReport.best.environmentSeed == "42");
        local a = eve.Agent();
        local unavailable = a.inferTensor(agentExampleReport.policy, {features=[0.0],legalActions=[0,1]});
        assert(!unavailable.ok && unavailable.code == "unsupported");
        local untouched = {resets=0,reset=function(seed){resets++;},step=function(action,dt){}};
        local unavailableRun = a.run({backend="tensor"}, untouched);
        assert(!unavailableRun.ok && unavailableRun.code == "unsupported" && untouched.resets == 0);
        local gpuRun = a.run({backend="gpu"}, untouched);
        assert(!gpuRun.ok && gpuRun.code == "unsupported" && untouched.resets == 0);
        local gpuInfer = a.inferGpu(agentExampleReport.policy, {features=[0.0],legalActions=[0,1]});
        assert(!gpuInfer.ok && gpuInfer.code == "unsupported");
        local gpuAct = a.actGpu(agentExampleReport.policy, {features=[0.0],legalActions=[0,1]});
        assert(!gpuAct.ok && gpuAct.code == "unsupported");
        local bad = a.run({unknown=1}, {});
        assert(!bad.ok);
    )"));
}
TEST_CASE("agent.scriptCallbackFailureAndReentry") {
    ssq::VM vm(1024);
    exposeAgent(vm);
    vm.run(vm.compileSource(R"(
        local a = eve.Agent();
        local e = { reset=function(seed) { throw "injected callback error"; }, step=function(action,dt) {} };
        local r = a.run({}, e);
        assert(!r.ok && r.diagnostics[0].code != 0);
        e.reset = function(seed) {
            local nested = a.run({}, this);
            assert(!nested.ok && nested.code == "conflict");
            return {features=[0.0],legalActions=[],outcome="success"};
        };
        local recovered = a.run({population=1,elites=1,generations=1}, e);
        assert(recovered.ok);
        assert(recovered.value.steps == 0);
    )"));
}
