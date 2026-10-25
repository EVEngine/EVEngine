// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "building_placement_regression.cpp"
  ]
}
*/
// clang-format on

#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSession.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "building/fx/BuildingFx.h"
#include "common/ECS.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <string>
#include <utility>

namespace {
using namespace eve::building;

BuildingDefinition definition(std::string id, std::string kind = "cell") {
    BuildingDefinition result;
    result.id            = std::move(id);
    result.placementKind = std::move(kind);
    return result;
}

void near(float actual, float expected) { CHECK(std::abs(actual - expected) < 0.0001f); }
}  // namespace

TEST_CASE("building.placement.surface_miss_never_commits_stale_cell_or_free_pose") {
    for (const char* kind : {"cell", "free"}) {
        BuildingRegistry::clear();
        const auto building = definition(std::string("surface-") + kind, kind);
        BuildingRegistry::registerBuilding(building);
        PlacementWorld world(8, 8, 1.f);
        world.setGridPlane("xz");
        PlacementSession session;
        REQUIRE(session.startPlacement(&world, building.id));

        REQUIRE(session.updateFromSurface(&world, "missing-surface", 2.f, 2.f));
        CHECK(!session.isValid());
        CHECK_EQ(session.getReason(), std::string("no_surface_hit"));
        CHECK_EQ(session.execute(), 0);
        CHECK_EQ(world.getBuildingCount(), 0);

        REQUIRE(session.updateFromSurface(&world, "plane", 2.f, 2.f));
        REQUIRE(session.isValid());
        REQUIRE(session.updateFromSurface(&world, "missing-surface", 3.f, 3.f));
        CHECK(!session.isValid());
        CHECK_EQ(session.getReason(), std::string("no_surface_hit"));
        CHECK_EQ(session.execute(), 0);
        CHECK_EQ(world.getBuildingCount(), 0);

        REQUIRE(session.updateFromSurface(&world, "plane", 3.f, 3.f));
        REQUIRE(session.isValid());
        CHECK(session.execute() > 0);
        CHECK_EQ(world.getBuildingCount(), 1);
    }
    BuildingRegistry::clear();
}

TEST_CASE("building.ghost.direct_pose_discards_surface_binding_after_rotation") {
    BuildingRegistry::clear();
    const auto building = definition("direct-pose");
    BuildingRegistry::registerBuilding(building);
    PlacementWorld world(8, 8, 1.f);
    world.setGridPlane("xz");

    Ghost ghost;
    ghost.setBuildingId(building.id);
    ghost.setFromSurface(&world, "plane", 1.f, 1.f);
    REQUIRE(ghost.validate(&world));
    CHECK(!ghost.getSurfaceId().empty());
    ghost.rotateBy(90.f);
    ghost.setFromWorld(&world, 4.f, 5.f);
    REQUIRE(ghost.validate(&world));
    CHECK_EQ(ghost.getCellX(), 4);
    CHECK_EQ(ghost.getCellY(), 5);
    CHECK_EQ(ghost.getSurfaceId(), std::string(""));
    BuildingRegistry::clear();
}

TEST_CASE("building.ghost.rotation_resamples_free_surface_footprint_before_commit") {
    BuildingRegistry::clear();
    auto building                     = definition("wide-free", "free");
    building.snapMode                 = "free";
    building.freeFootprintWidthCells  = 4.f;
    building.freeFootprintHeightCells = 1.f;
    building.maxSurfaceHeightDelta    = 1.5f;
    BuildingRegistry::registerBuilding(building);
    PlacementWorld world(12, 12, 1.f);
    world.setGridPlane("xz");
    PlacementSystem::registerSurfaceProvider("sloped-surface", [](const PlacementWorld&, float x, float z) {
        PlacementSystem::PlacementHit hit;
        hit.worldX    = x;
        hit.worldY    = z;
        hit.worldZ    = z;
        hit.normalY   = 1.f;
        hit.normalZ   = -1.f;
        hit.surfaceId = "sloped-surface";
        return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
    });

    Ghost ghost;
    ghost.setBuildingId(building.id);
    ghost.setFromSurface(&world, "sloped-surface", 5.f, 5.f);
    REQUIRE(ghost.validate(&world));
    ghost.rotateBy(90.f);
    CHECK(!ghost.validate(&world));
    CHECK_EQ(ghost.getReason(), std::string("surface_height_delta"));
    CHECK_EQ(world.placeGhost(&ghost), 0);
    CHECK_EQ(world.getBuildingCount(), 0);

    PlacementSystem::unregisterSurface("sloped-surface");
    BuildingRegistry::clear();
}

TEST_CASE("building.fx.rotated_rectangular_footprint_sets_world_transform") {
    BuildingRegistry::clear();
    auto building             = definition("rectangular-visual");
    building.renderMode       = "3d";
    building.footprintW       = 3;
    building.footprintH       = 2;
    building.visual3d["mesh"] = "barn";
    BuildingRegistry::registerBuilding(building);
    PlacementWorld world(12, 12, 1.f);
    world.setGridPlane("xz");
    world.setOrigin(-6.f, -6.f);
    REQUIRE(world.placeAt(building.id, 2, 3, 90.f) > 0);

    eve::graphics::Mesh mesh;
    auto*               fx = eve::buildingfx::BuildingFx::create();
    fx->setMeshResolver([&](const std::string&) { return &mesh; });
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    bool found = false;
    auto view  = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::Transform3D,
                           eve::graphics::Renderable3D::MeshRenderer>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [transform, renderer] = *it;
        if (renderer->mesh != &mesh) continue;
        found = true;
        near(transform->x, -3.f);
        near(transform->z, -1.5f);
        near(transform->sx, 3.f);
        near(transform->sz, 2.f);
        near(transform->yaw, 1.5707963f);
    }
    CHECK(found);
    REQUIRE(fx->detach(&world));
    fx->clearMeshResolver();
    BuildingRegistry::clear();
}
