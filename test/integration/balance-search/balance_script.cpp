// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "script-binding",
  "module": "agent"
}
*/
// clang-format on

#include "agent/Balance.h"
#include "simplesquirrel/simplesquirrel.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

TEST_CASE("agent.balance.scriptBuilderCallbackFailureAndRecovery") {
    ssq::VM vm(4096, ssq::Libs::ALL);
    eve::agent::balance::BalanceSearch::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "pending";
        local search = BalanceSearch();
        local spec = {schema="evengine.balance.problem", version=1,
            parameters=[{name="damage", initial=1.0, min=0.0, max=10.0}],
            targets=[{name="dps", min=6.0, max=7.0, scale=1.0}], candidates=32};
        local setup = search.configure(spec);
        if (!setup.ok) throw setup.code;
        local failed = search.run({evaluate=function(p, seed, dt, steps) { throw "injected"; }});
        if (failed.ok) throw "callback failure was hidden";
        local missing = search.run({});
        if (missing.ok) throw "missing callback accepted";
        local wrong = search.run({evaluate=function(p, seed, dt, steps) { return {wrong=1.0}; }});
        if (wrong.ok) throw "missing metric accepted";
        local measured = search.run({evaluate=function(p, seed, dt, steps) {
            if (typeof seed != "string") throw "seed truncated";
            local nested = search.run({});
            if (nested.ok) throw "reentry accepted";
            local edit = search.configure(spec);
            if (edit.ok) throw "mutation during run accepted";
            return {dps=p.damage};
        }});
        if (!measured.ok) throw measured.code;
        if (measured.value.tuned.loss >= measured.value.baseline.loss) throw "no improvement";
        if (spec.parameters[0].initial != 1.0) throw "input mutated";
        result = "ok";
    )"));
    REQUIRE_EQ(vm.find("result").toString(), std::string("ok"));
}
