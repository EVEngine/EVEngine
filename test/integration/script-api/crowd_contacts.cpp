#include <simplesquirrel/simplesquirrel.hpp>
#include "crowd/Crowd.h"
#include "crowd/CrowdField.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

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
