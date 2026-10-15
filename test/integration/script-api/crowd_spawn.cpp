#include <simplesquirrel/simplesquirrel.hpp>
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"

#include <cmath>

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
