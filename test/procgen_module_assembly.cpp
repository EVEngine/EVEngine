#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "procgen/ModuleAssembly.h"

#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>

using namespace eve::procgen;

TEST_CASE("procgen.moduleAssembly.rejectsCoordinateOverflowAtomically") {
    ModuleAssemblyConstraints constraints;
    constraints.requireConnection = false;
    constraints.maxLevels         = std::numeric_limits<int>::max();
    ModuleAssemblyPlan plan(constraints);
    REQUIRE(plan.registerVolumeModuleType("wide", 2, 1, 2).ok());
    CHECK(!plan.place({"wide", std::numeric_limits<int>::max(), 0, 0, 0}).ok());
    CHECK(!plan.place({"wide", 0, std::numeric_limits<int>::max(), 0, 1}).ok());
    CHECK(!plan.place({"wide", 0, 0, std::numeric_limits<int>::max(), 0}).ok());
    CHECK_EQ(plan.placements().size(), size_t(0));
    REQUIRE(plan.place({"wide", std::numeric_limits<int>::min(), 0, 0, 0}).ok());
    CHECK_EQ(plan.placements().size(), size_t(1));
}

TEST_CASE("procgen.moduleAssembly.rejectsOverlapDisconnectionAndWrongConnector") {
    ModuleAssemblyConstraints constraints;
    constraints.bounded  = true;
    constraints.minCellX = 0;
    constraints.maxCellX = 4;
    constraints.minCellZ = 0;
    constraints.maxCellZ = 4;
    ModuleAssemblyPlan plan(constraints);
    auto               registered = plan.registerModule({"street",
                                                         1,
                                                         1,
                                                         {{0, 0, ModuleFacing::North, "street", "street"},
                                                          {0, 0, ModuleFacing::East, "street", "street"},
                                                          {0, 0, ModuleFacing::South, "street", "street"},
                                                          {0, 0, ModuleFacing::West, "street", "street"}}});
    REQUIRE(registered.ok());
    auto first = plan.place({"street", 2, 2, 0, 0});
    REQUIRE(first.ok());
    auto connected = plan.place({"street", 3, 2, 0, 0});
    REQUIRE(connected.ok());
    auto overlap = plan.place({"street", 3, 2, 0, 0});
    CHECK(!overlap.ok());
    CHECK_EQ(overlap.code(), eve::StatusCode::Conflict);
    auto disconnected = plan.place({"street", 0, 0, 0, 0});
    CHECK(!disconnected.ok());
    CHECK_EQ(disconnected.code(), eve::StatusCode::Rejected);

    ModuleAssemblyPlan mismatch(constraints);
    auto               a = mismatch.registerModule({"road", 1, 1, {{0, 0, ModuleFacing::East, "road", "road"}}});
    auto               b = mismatch.registerModule({"wall", 1, 1, {{0, 0, ModuleFacing::West, "wall", "wall"}}});
    REQUIRE(eve::everyResultValid(a, b));
    auto seed = mismatch.place({"road", 1, 1, 0, 0});
    REQUIRE(seed.ok());
    auto wrong = mismatch.place({"wall", 2, 1, 0, 0});
    CHECK(!wrong.ok());
    CHECK_EQ(wrong.code(), eve::StatusCode::Conflict);
    CHECK_EQ(mismatch.placements().size(), size_t(1));
}

TEST_CASE("procgen.moduleAssembly.requiresEveryTouchingCellEdgeToHaveAConnector") {
    ModuleAssemblyConstraints constraints;
    constraints.bounded  = true;
    constraints.minCellX = -2;
    constraints.maxCellX = 2;
    constraints.minCellZ = 0;
    constraints.maxCellZ = 4;
    ModuleAssemblyPlan plan(constraints);
    REQUIRE(plan.registerModule({"street",
                                 1,
                                 1,
                                 {{0, 0, ModuleFacing::North, "street", "street"},
                                  {0, 0, ModuleFacing::East, "street", "street"},
                                  {0, 0, ModuleFacing::South, "street", "street"},
                                  {0, 0, ModuleFacing::West, "street", "street"}}})
                .ok());
    REQUIRE(plan.registerModule(
                    {"building",
                     2,
                     2,
                     {{1, 0, ModuleFacing::East, "street", "street"}, {1, 1, ModuleFacing::East, "street", "street"}}})
                .ok());
    REQUIRE(plan.place({"street", 0, 0, 0, 0}).ok());
    REQUIRE(plan.place({"street", 0, 1, 0, 0}).ok());
    REQUIRE(plan.place({"building", -2, 0, 0, 0}).ok());

    auto unsupportedTouch = plan.place({"building", -2, 2, 0, 0});
    CHECK(!unsupportedTouch.ok());
    CHECK_EQ(unsupportedTouch.code(), eve::StatusCode::Conflict);
    CHECK_EQ(plan.placements().size(), size_t(3));
}

TEST_CASE("procgen.moduleAssembly.validatesVolumeOverlapAndVerticalSupport") {
    ModuleAssemblyConstraints constraints;
    constraints.maxLevels = 4;
    ModuleAssemblyPlan plan(constraints);

    ModuleDefinition foundation{"foundation", 1, 1, {{0, 0, ModuleFacing::Up, "support", "load", 0}}};
    foundation.heightLevels = 1;
    ModuleDefinition tower{"tower", 1, 1, {{0, 0, ModuleFacing::Down, "load", "support", 0}}};
    tower.heightLevels = 2;
    REQUIRE(eve::everyResultValid(plan.registerModule(std::move(foundation)), plan.registerModule(std::move(tower))));
    REQUIRE(plan.place({"foundation", 0, 0, 0, 0}).ok());
    REQUIRE(plan.place({"tower", 0, 0, 1, 0}).ok());

    auto volumeOverlap = plan.place({"foundation", 0, 0, 2, 0});
    CHECK(!volumeOverlap.ok());
    CHECK_EQ(volumeOverlap.code(), eve::StatusCode::Conflict);

    ModuleAssemblyPlan unsupported(constraints);
    REQUIRE(unsupported.registerVolumeModuleType("upper", 1, 1, 2).ok());
    REQUIRE(unsupported.place({"upper", 2, 2, 0, 0}).ok());
    auto floating = unsupported.place({"upper", 2, 2, 2, 0});
    CHECK(!floating.ok());
    CHECK_EQ(floating.code(), eve::StatusCode::Conflict);
    CHECK_EQ(unsupported.placements().size(), size_t(1));
}

TEST_CASE("procgen.moduleAssembly.appliesConfiguredRotationWhitelistAtomically") {
    ModuleAssemblyPlan plan;
    REQUIRE(plan.setAllowedQuarterTurns(true, false, true, false).ok());
    REQUIRE(plan.registerModuleType("facade", 1, 1).ok());
    REQUIRE(plan.place({"facade", 0, 0, 0, 0}).ok());
    auto rejected = plan.place({"facade", 1, 0, 0, 1});
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Rejected);
    CHECK_EQ(plan.placements().size(), size_t(1));
    CHECK(!plan.setAllowedQuarterTurns(true, true, true, true).ok());
}

TEST_CASE("procgen.moduleAssembly.appliesConfiguredVerticalSupportRatioAtomically") {
    ModuleAssemblyConstraints constraints;
    constraints.maxLevels = 3;
    ModuleAssemblyPlan plan(constraints);
    REQUIRE(plan.setMinimumSupportRatio(1.0f).ok());

    ModuleDefinition base{"base", 1, 1, {{0, 0, ModuleFacing::Up, "support", "load", 0}}};
    ModuleDefinition platform{
        "platform",
        2,
        1,
        {{0, 0, ModuleFacing::Down, "load", "support", 0}, {1, 0, ModuleFacing::West, "walk", "walk", 0}}};
    REQUIRE(eve::everyResultValid(plan.registerModule(std::move(base)), plan.registerModule(std::move(platform))));
    REQUIRE(plan.place({"base", 0, 0, 0, 0}).ok());

    auto insufficient = plan.place({"platform", 0, 0, 1, 0});
    CHECK(!insufficient.ok());
    CHECK_EQ(insufficient.code(), eve::StatusCode::Rejected);
    CHECK_EQ(plan.placements().size(), size_t(1));

    CHECK(!plan.setMinimumSupportRatio(0.5f).ok());
}

TEST_CASE("procgen.moduleAssembly.loadsStrictVersionedJsonAtomically") {
    constexpr auto     config = R"({
      "schema":"eve.procgen.module-assembly","version":1,"unknownFields":"reject",
      "constraints":{"cellSize":12.0,"floorHeight":4.0,
        "bounds":{"enabled":true,"minCellX":-2,"maxCellX":2,"minCellZ":0,"maxCellZ":9},
        "maxLevels":3,"allowedQuarterTurns":[true,false,true,false],
        "requireConnection":true,"minimumSupportRatio":1.0},
      "modules":[
        {"id":"street","widthCells":1,"depthCells":1,"heightLevels":1,"connectors":[
          {"cellX":0,"cellZ":0,"level":0,"facing":"east","tag":"street","accepts":"street"}]},
        {"id":"building","widthCells":2,"depthCells":2,"heightLevels":3,"connectors":[
          {"cellX":0,"cellZ":0,"level":0,"facing":"west","tag":"street","accepts":"street"}]}
      ]})";
    ModuleAssemblyPlan plan;
    REQUIRE(plan.applyConfigJson(config).ok());
    CHECK_EQ(plan.constraints().cellSize, 12.f);
    CHECK_EQ(plan.constraints().allowedQuarterTurns, (std::array<bool, 4>{true, false, true, false}));
    REQUIRE(plan.place({"street", 0, 0, 0, 0}).ok());
    REQUIRE(plan.place({"building", 1, 0, 0, 0}).ok());
    CHECK(!plan.place({"street", 0, 1, 0, 1}).ok());
    CHECK(!plan.applyConfigJson(config).ok());

    ModuleAssemblyPlan unchanged;
    std::string        invalid(config);
    invalid.replace(invalid.find("\"modules\""), 9, "\"unexpected\"");
    CHECK(!unchanged.applyConfigJson(invalid).ok());
    REQUIRE(unchanged.registerModuleType("still-empty", 1, 1).ok());
}

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
