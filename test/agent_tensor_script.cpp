#include <simplesquirrel/simplesquirrel.hpp>
#include "agent/Agent.h"
#include "agent/AgentModule.h"
#include "agent/tensor/TensorBackend.h"
#include "common/Capability.h"
#include "zeroerr/unittest.h"

TEST_CASE("agent.scriptTensorTrainingLoop") {
    using namespace eve::agent;
    auto made = makeTensorBackend();
    REQUIRE(made.ok());
    auto provider = std::move(made).takeValue();
    struct Scope {
        IPolicyBackend& backend;
        explicit Scope(IPolicyBackend& b) : backend(b) { eve::cap::provide<IPolicyBackend>(&b); }
        ~Scope() { eve::cap::revoke<IPolicyBackend>(&backend); }
    } scope(*provider);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Agent>("Agent");
    Agent::expose(cls);
    vm.run(vm.compileSource(R"(
        local a = eve.Agent();
        local e = { reset=function(seed) {return {features=[0.0],legalActions=[0,1]};},
                    step=function(action,dt) {return {features=[0.5],legalActions=[],reward=1.0,outcome="success"};} };
        local r = a.run({backend="tensor",population=2,elites=1,generations=2,randomProbability=0.0}, e);
        assert(r.ok);
        assert(r.value.backend == "tensor-cpu");
        assert(r.value.trainingSamples > 0);
        local o = {features=[0.1],legalActions=[1]};
        local p = a.inferTensor(r.value.policy,o);
        assert(p.ok && p.value[0] == 0.0 && p.value[1] == 1.0);
        local action = a.actTensor(r.value.policy,o);
        assert(action.ok && action.value == 1);
    )"));
}
