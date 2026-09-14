#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSession.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
