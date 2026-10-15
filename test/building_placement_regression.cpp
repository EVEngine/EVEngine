#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSession.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "building/fx/BuildingFx.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// building_placement_regression.cpp
using namespace eve::building;

TEST_CASE("building.surfaceMissCannotCommitPreviousPose") {
    for (const auto kind : {"cell", "free"}) {
        BuildingDefinition definition;
        definition.id            = "surface-miss-regression";
        definition.placementKind = kind;
        BuildingRegistry::registerBuilding(definition);
        PlacementWorld world(8, 8, 1.f);
        world.setGridPlane("xz");
        PlacementSession session;
        REQUIRE(session.startPlacement(&world, definition.id));
        REQUIRE(session.updateFromSurface(&world, "plane", 2.f, 2.f));
        REQUIRE(session.isValid());
        REQUIRE(session.updateFromSurface(&world, "missing-regression-surface", 3.f, 3.f));
        REQUIRE(!session.isValid());
        REQUIRE_EQ(session.getReason(), "no_surface_hit");
        REQUIRE_EQ(session.execute(), 0);
        REQUIRE_EQ(world.getBuildingCount(), 0);
        REQUIRE(session.updateFromSurface(&world, "plane", 3.f, 3.f));
        REQUIRE(session.isValid());
        REQUIRE(session.execute() > 0);
    }
    BuildingRegistry::clear();
}

TEST_CASE("building.directPoseDiscardsStaleSurfaceAfterRotation") {
    BuildingDefinition definition;
    definition.id = "direct-pose-regression";
    BuildingRegistry::registerBuilding(definition);
    PlacementWorld world(8, 8, 1.f);
    world.setGridPlane("xz");
    Ghost ghost;
    ghost.setBuildingId(definition.id);
    ghost.setFromSurface(&world, "plane", 1.f, 1.f);
    REQUIRE(ghost.validate(&world));
    ghost.rotateBy(90.f);
    ghost.setFromWorld(&world, 4.f, 5.f);
    REQUIRE(ghost.validate(&world));
    REQUIRE_EQ(ghost.getCellX(), 4);
    REQUIRE_EQ(ghost.getCellY(), 5);
    REQUIRE(ghost.getSurfaceId().empty());
    BuildingRegistry::clear();
}

TEST_CASE("building.freeRotationResamplesSurfaceFootprint") {
    BuildingDefinition definition;
    definition.id                       = "free-rotation-regression";
    definition.placementKind            = "free";
    definition.snapMode                 = "free";
    definition.freeFootprintWidthCells  = 4.f;
    definition.freeFootprintHeightCells = 1.f;
    definition.maxSurfaceHeightDelta    = 1.5f;
    BuildingRegistry::registerBuilding(definition);
    PlacementWorld world(12, 12, 1.f);
    world.setGridPlane("xz");
    PlacementSystem::registerSurfaceProvider(
        "rotation-regression-surface", [](const PlacementWorld&, float x, float z) {
            PlacementSystem::PlacementHit hit;
            hit.worldX    = x;
            hit.worldY    = z;
            hit.worldZ    = z;
            hit.normalY   = 1.f;
            hit.normalZ   = -1.f;
            hit.surfaceId = "rotation-regression-surface";
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    Ghost ghost;
    ghost.setBuildingId(definition.id);
    ghost.setFromSurface(&world, "rotation-regression-surface", 5.f, 5.f);
    REQUIRE(ghost.validate(&world));
    ghost.rotateBy(90.f);
    REQUIRE(!ghost.validate(&world));
    REQUIRE_EQ(ghost.getReason(), "surface_height_delta");
    REQUIRE_EQ(world.placeGhost(&ghost), 0);
    REQUIRE_EQ(world.getBuildingCount(), 0);
    PlacementSystem::unregisterSurface("rotation-regression-surface");
    BuildingRegistry::clear();
}

// building_visual_placement.cpp
TEST_CASE("buildingfx.rectangularMeshMatchesRotatedCellFootprint") {
    using namespace eve::building;
    using eve::graphics::Renderable3D;
    BuildingDefinition barn;
    barn.id               = "rectangular-visual-regression";
    barn.renderMode       = "3d";
    barn.footprintW       = 3;
    barn.footprintH       = 2;
    barn.visual3d["mesh"] = "barn";
    BuildingRegistry::registerBuilding(barn);
    PlacementWorld world(12, 12, 1.f);
    world.setGridPlane("xz");
    world.setOrigin(-6.f, -6.f);
    const int id = world.placeAt(barn.id, 2, 3, 90.f);
    REQUIRE(id > 0);
    eve::graphics::Mesh mesh;
    auto*               fx = eve::buildingfx::BuildingFx::create();
    fx->setMeshResolver([&](const std::string&) { return &mesh; });
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    bool found = false;
    auto view  = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [transform, renderer] = *it;
        if (renderer->mesh != &mesh) continue;
        found = true;
        REQUIRE(std::abs(transform->x - (-3.f)) < 1e-4f);
        REQUIRE(std::abs(transform->z - (-1.5f)) < 1e-4f);
        REQUIRE(std::abs(transform->sx - 3.f) < 1e-4f);
        REQUIRE(std::abs(transform->sz - 2.f) < 1e-4f);
        REQUIRE(std::abs(transform->yaw - 1.5707963f) < 1e-4f);
    }
    REQUIRE(found);
    REQUIRE(fx->detach(&world));
    fx->clearMeshResolver();
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.scriptMeshResolverValidatesAndCopiesResources") {
    eve::graphics::Mesh mesh;
    ssq::VM             vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    auto eveTable = vm.find("eve").toTable();
    eve::graphics::Graphics::expose(eveTable);
    vm.addFunc("testHouseMesh", [&]() { return &mesh; });
    vm.run(vm.compileSource(R"(
        b <- eve.Building();
        b.registerBuildingsFromJson("[{\"id\":\"script-house\",\"renderMode\":\"3d\",\"visual3d\":{\"mesh\":\"house.obj\"}}]");
        world <- b.newWorld(4, 4, 1.0);
        fx <- eve.BuildingFx();
        resources <- { "house.obj": testHouseMesh() };
        fx.setMeshResolver(resources);
        resources.clear();
        rejected <- false;
        try { fx.setMeshResolver({ "house.obj": world }); }
        catch (error) { rejected = true; }
        fx.attach(world);
        id <- world.placeAt("script-house", 1, 1, 0.0);
        fx.sync(world);
        resolved <- fx.getVisualFallbackReason(world, id) == "";
        fx.clearMeshResolver();
        fx.sync(world);
        missing <- fx.getVisualFallbackReason(world, id) == "resolver_unavailable";
        fx.detach(world);
        world.destroy();
    )"));
    REQUIRE(vm.find("rejected").toBool());
    REQUIRE(vm.find("resolved").toBool());
    REQUIRE(vm.find("missing").toBool());
    eve::building::BuildingRegistry::clear();
}
