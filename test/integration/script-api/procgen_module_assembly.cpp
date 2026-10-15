#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "procgen/ModuleAssembly.h"

#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>

using namespace eve::procgen;


TEST_CASE("procgen.moduleAssembly.squirrelBuildsValidatedPlacementTransforms") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        local plan = eve.ProcgenModuleAssemblyPlan();
        assert(plan.configure(4.0, 4.0, -4, 4, -4, 4, 3, true, true).ok);
        assert(plan.registerModule("wall", 1, 1).ok);
        assert(plan.addConnector("wall", 0, 0, 1, "wall", "wall").ok);
        assert(plan.addConnector("wall", 0, 0, 3, "wall", "wall").ok);
        assert(plan.place("wall", 0, 0, 0, 0).ok);
        assert(plan.place("wall", 1, 0, 0, 0).ok);
        local rejected = plan.place("wall", 3, 3, 0, 0);
        assert(!rejected.ok && rejected.status.code == "rejected");
        assert(plan.getPlacementCount() == 2);
        assert(plan.getPlacementModule(1) == "wall");
        assert(plan.getPlacementX(1) == 4.0);
        assert(plan.getPlacementY(1) == 0.0);
        assert(plan.getPlacementZ(1) == 0.0);
        assert(plan.getPlacementYawDegrees(1) == 0.0);

        local volume = eve.ProcgenModuleAssemblyPlan();
        assert(volume.configure(4.0, 4.0, 0, 2, 0, 2, 4, true, true).ok);
        assert(volume.setAllowedQuarterTurns(true, false, true, false).ok);
        assert(volume.setMinimumSupportRatio(1.0).ok);
        assert(volume.registerVolumeModule("base", 1, 1, 1).ok);
        assert(volume.registerVolumeModule("upper", 1, 1, 2).ok);
        assert(volume.addVolumeConnector("base", 0, 0, 0, 4, "support", "load").ok);
        assert(volume.addVolumeConnector("upper", 0, 0, 0, 5, "load", "support").ok);
        assert(volume.place("base", 0, 0, 0, 0).ok);
        assert(volume.place("upper", 0, 0, 1, 0).ok);
    )"));
}
