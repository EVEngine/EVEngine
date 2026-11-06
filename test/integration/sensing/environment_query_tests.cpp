// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "script-binding",
  "module": "sensing",
  "origins": [
    "sensing_environment.cpp"
  ]
}
*/
// clang-format on

#include "common/Module.h"
#include "sensing/Sensing.h"
#include "simplesquirrel/simplesquirrel.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

using namespace eve;

TEST_CASE("sensing.environment.scriptPositionsUseCanonicalExecutor") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local world = eve.Sensing().newWorld().value;
        local query = world.queryPositions(0.0, 0.0, 10.0, 0.0, 3.0, 8, 1, false);
        local invalid = world.queryPositions(0.0, 0.0, 10.0, 0.0, -1.0, 8, 1, false);
        local trace = world.debugLastQueryJson();
        if (query.ok && !invalid.ok && trace.ok && query.value.ranked.len() == 1 &&
            query.value.ranked[0].subject == null && query.value.ranked[0].location.space == "world2d") result = "ok";
        eve.result.ignore(world.release(), "test cleanup");
    )"));
    REQUIRE_EQ(vm.find("result").toString(), std::string("ok"));
}
