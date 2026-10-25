// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "cases": {
    "platform-file": [
      "climbing.motionMatchingDemoLoadsRedistributableMannequinAndActionClips"
    ],
    "script-binding": [
      "climbing.motionMatchingDemoScriptCompilesAndDocumentsUeOnlyBoundary"
    ],
    "physics-backend": [
      "climbing.vault.runningJumpUsesLowVaultAndProducesStableConstrainedLanding",
      "climbing.vault.destroyedTargetCancelsWithStaleEvidence",
      "container.zoneUsesStrongCoordinateSpacesAndValidatesShapes"
    ]
  },
  "module": "buildingfx",
  "modules": [
    "buildingfx",
    "card",
    "climbing",
    "combat",
    "combat_navigation",
    "common",
    "definitions",
    "dialogue",
    "inventory",
    "rpg"
  ],
  "origins": [
    "buildingfx.cpp",
    "card_payment_composition.cpp",
    "climbing_animation.cpp",
    "combat_action_damage.cpp",
    "combat_action_windows.cpp",
    "combat_arena_foundation.cpp",
    "combat_cancel_resolver.cpp",
    "combat_damage.cpp",
    "combat_feel_cancel_guard.cpp",
    "combat_framework_composition.cpp",
    "combat_loop.cpp",
    "combat_navigation.cpp",
    "combat_target_enemy_ai.cpp",
    "common_result_header.cpp",
    "container_transfer.cpp",
    "definition_runtime.cpp",
    "definition_runtime_skill_vehicle.cpp",
    "definition_runtime_weapon.cpp",
    "dialogue_localization.cpp",
    "dialogue_payment.cpp",
    "dialogue_proc.cpp"
  ]
}
*/
// clang-format on

// Check header isolation before any test assertion headers are loaded.
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_common_result_header)
#include "PrivateTestScope.h"
#include "common/Result.h"
#include "editing/EditingResult.h"
#if defined(EV_ASSERT) || defined(ZEROERR_ASSERT_EXP)
#error "Result headers must not expose the zeroerr assertion backend"
#endif

#include <zeroerr/unittest.h>
#include <type_traits>
// common_result_header.cpp
namespace test_common_result_header {


#if defined(ZEROERR_NO_ASSERT)
static_assert(std::is_nothrow_destructible_v<eve::Result<int>>);
#else
static_assert(!std::is_nothrow_destructible_v<eve::Result<int>>);
#endif

TEST_CASE("common.result.headersKeepDiagnosticsInImplementation") {
    auto value = eve::Result<int>::success(7);
    CHECK(value.value() == 7);
    auto unchanged = eve::editing::noOp();
    CHECK(unchanged.ok());
    auto failed = eve::editing::rejected<int>(eve::editing::RuleId("header.contract"), "expected");
    CHECK(!failed.ok());
    CHECK(failed.status().code() == eve::StatusCode::Rejected);
}
}  // namespace test_common_result_header
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_buildingfx)
#include "PrivateTestScope.h"
#include <cmath>
#include "building/Building.h"
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "building/fx/BuildingFx.h"
#include "common/ECS.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// buildingfx.cpp
namespace test_buildingfx {
using namespace eve::building;
using namespace eve::buildingfx;

TEST_CASE("buildingfx.sync.lifecycle") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition hut;
    hut.id                 = "hut";
    hut.displayName        = "Hut";
    hut.footprintW         = 2;
    hut.footprintH         = 1;
    hut.renderMode         = "2d";
    hut.visual2d["colorR"] = "0.8";
    hut.visual2d["colorG"] = "0.4";
    hut.visual2d["colorB"] = "0.2";
    BuildingRegistry::registerBuilding(hut);

    PlacementWorld world(8, 8, 32.f);
    BuildingFx*    fx = BuildingFx::create();
    REQUIRE(fx != nullptr);

    REQUIRE(fx->attach(&world));
    REQUIRE(fx->isAttached(&world));
    REQUIRE_EQ(fx->getVisualCount(&world), 0);

    const int a = world.placeAt("hut", 1, 1, 0.f);
    const int b = world.placeAt("hut", 4, 2, 0.f);
    REQUIRE(a > 0);
    REQUIRE(b > 0);
    fx->sync(&world);
    REQUIRE_EQ(fx->getVisualCount(&world), 2);

    REQUIRE(world.removeBuilding(a));
    fx->sync(&world);
    REQUIRE_EQ(fx->getVisualCount(&world), 1);

    Ghost ghost;
    ghost.setBuildingId("hut");
    ghost.setFromWorld(&world, 140.f, 80.f);
    fx->updateGhost(&world, &ghost);
    fx->hideGhost(&world);

    fx->setGridVisible(&world, true);
    REQUIRE(fx->getGridVisible(&world));
    fx->setGridVisible(&world, false);
    REQUIRE(!fx->getGridVisible(&world));

    REQUIRE(fx->detach(&world));
    REQUIRE(!fx->isAttached(&world));
    REQUIRE_EQ(fx->getVisualCount(&world), 0);

    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.surfacePoseUsesExactAnchorAndFrame") {
    BuildingRegistry::clear();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition ramp;
    ramp.id         = "ramp";
    ramp.renderMode = "3d";
    ramp.snapMode   = "free";
    BuildingRegistry::registerBuilding(ramp);

    PlacementWorld world(8, 8, 1.f);
    world.setGridPlane("xz");
    PlacementSystem::registerSurfaceProvider("ramp.surface", [](const PlacementWorld&, float x, float y) {
        PlacementSystem::PlacementHit hit;
        hit.worldX    = x;
        hit.worldY    = 1.5f;
        hit.worldZ    = y;
        hit.normalY   = 1.f;
        hit.normalZ   = 1.f;
        hit.tangentX  = 1.f;
        hit.surfaceId = "ramp.mesh";
        return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
    });

    Ghost ghost;
    ghost.setBuildingId(ramp.id);
    ghost.setFromSurface(&world, "ramp.surface", 2.25f, 3.75f);
    REQUIRE(ghost.validate(&world));
    REQUIRE(world.placeGhost(&ghost) > 0);

    BuildingFx* fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);

    auto view        = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::Transform3D>();
    int  visualCount = 0;
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [transform] = *it;
        REQUIRE(std::abs(transform->x - 2.25f) < 0.0001f);
        REQUIRE(std::abs(transform->y - 1.5f) < 0.0001f);
        REQUIRE(std::abs(transform->z - 3.75f) < 0.0001f);
        REQUIRE(std::abs(transform->pitch - 0.7853982f) < 0.0001f);
        ++visualCount;
    }
    REQUIRE_EQ(visualCount, 1);

    REQUIRE(fx->detach(&world));
    PlacementSystem::unregisterSurface("ramp.surface");
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.edgeVisualUsesMidpointLengthAndThickness") {
    BuildingRegistry::clear();

    BuildingDefinition wall;
    wall.id                             = "wall";
    wall.placementKind                  = "edge";
    wall.renderMode                     = "3d";
    wall.visual3d["height"]             = "3";
    wall.visual3d["thickness"]          = "0.5";
    wall.visual3d["variant.end.height"] = "4";
    BuildingRegistry::registerBuilding(wall);

    PlacementWorld world(4, 4, 10.f);
    world.setGridPlane("xz");
    REQUIRE(world.placeEdge("wall", 1, 2, "north") > 0);

    BuildingFx* fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);

    int visualCount = 0;
    {
        auto view = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::Transform3D>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [transform] = *it;
            REQUIRE(std::abs(transform->x - 15.f) < 0.0001f);
            REQUIRE(std::abs(transform->z - 20.f) < 0.0001f);
            REQUIRE(std::abs(transform->sx - 10.f) < 0.0001f);
            REQUIRE(std::abs(transform->sy - 3.f) < 0.0001f);
            REQUIRE(std::abs(transform->sz - 0.5f) < 0.0001f);
            ++visualCount;
        }
    }
    REQUIRE_EQ(visualCount, 1);

    REQUIRE(world.placeEdge("wall", 2, 2, "north") > 0);
    fx->sync(&world);
    REQUIRE_EQ(fx->getVisualCount(&world), 2);
    visualCount      = 0;
    auto updatedView = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::Transform3D>();
    for (auto it = updatedView.begin(); it != updatedView.end(); ++it) {
        auto [transform] = *it;
        REQUIRE(std::abs(transform->sy - 4.f) < 0.0001f);
        ++visualCount;
    }
    REQUIRE(visualCount >= 1);
    REQUIRE(fx->detach(&world));
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.edgeCurveRasterUsesAuthoritativeTopologyVisuals") {
    BuildingRegistry::clear();
    BuildingDefinition wall;
    wall.id                              = "curve-visual-wall";
    wall.placementKind                   = "edge";
    wall.connectionGroup                 = "curve-visual-wall";
    wall.channel                         = "walls";
    wall.renderMode                      = "3d";
    wall.visual3d["variant.corner.mesh"] = "wall/curve-corner.mesh";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(10, 10, 10.f);
    world.setGridPlane("xz");
    const std::vector<PlacementSystem::EdgeCurvePoint> controls{{1.f, 1.f}, {1.f, 6.f}, {7.f, 6.f}, {7.f, 1.f}};
    auto placed = PlacementSystem::placeEdgeCubicBezier(&world, wall.id, controls, 24);
    REQUIRE(placed.ok());

    BuildingFx* fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    CHECK_EQ(fx->getVisualCount(&world), static_cast<int>(placed.value().instanceIds.size()));
    CHECK_EQ(fx->getCurveGroupCount(&world), 1);
    CHECK_EQ(fx->getContinuousCurveVisualCount(&world), 0);
    CHECK_EQ(fx->getCurveVisualFallbackReason(&world, placed.value().instanceIds.front()), "graphics_unavailable");
    int cornerCount = 0;
    for (int instanceId : placed.value().instanceIds) {
        if (fx->getVisualVariant(&world, instanceId) != "corner") continue;
        CHECK_EQ(fx->getVisualResource(&world, instanceId), "wall/curve-corner.mesh");
        ++cornerCount;
    }
    CHECK(cornerCount > 0);
    auto continuousMesh =
        BuildingFx::buildEdgeCurveGroupMeshForInstance(world, placed.value().instanceIds.front(), 2.f, 3.f, 4.f);
    REQUIRE(continuousMesh.ok());
    CHECK_EQ(continuousMesh.value().sampleCount, 25);
    REQUIRE(world.removeBuilding(placed.value().instanceIds.front()));
    fx->sync(&world);
    CHECK_EQ(fx->getCurveGroupCount(&world), 0);
    CHECK_EQ(fx->getContinuousCurveVisualCount(&world), 0);
    CHECK_EQ(fx->getCurveVisualFallbackReason(&world, placed.value().instanceIds.back()), "curve_group_not_found");
    CHECK(
        !BuildingFx::buildEdgeCurveGroupMeshForInstance(world, placed.value().instanceIds.back(), 2.f, 3.f, 4.f).ok());
    REQUIRE(fx->detach(&world));
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.edgeCurveMeshBuildsContinuousWorldSpaceExtrusion") {
    PlacementWorld world(10, 10, 10.f);
    world.setOrigin(3.f, 7.f);
    world.setGridPlane("xz");
    const std::vector<BuildingFx::CurveControlPoint> controls{{1.f, 1.f}, {1.f, 6.f}, {7.f, 6.f}, {7.f, 1.f}};
    auto mesh = BuildingFx::buildEdgeCurveMesh(world, controls, 24, 2.f, 3.f, 4.f);
    REQUIRE(mesh.ok());
    CHECK_EQ(mesh.value().positions.size(), size_t{((24 + 1) * 8 + 8) * 3});
    CHECK_EQ(mesh.value().normals.size(), mesh.value().positions.size());
    CHECK_EQ(mesh.value().uvs.size(), size_t{((24 + 1) * 8 + 8) * 2});
    CHECK_EQ(mesh.value().indices.size(), size_t{24 * 24 + 12});
    CHECK_EQ(mesh.value().sampleCount, 25);
    CHECK(mesh.value().length > 60.f);
    for (size_t index : {size_t{0}, size_t{3}, size_t{mesh.value().positions.size() - 1}})
        CHECK(std::isfinite(mesh.value().positions[index]));
    CHECK(!BuildingFx::buildEdgeCurveMesh(world, controls, 1, 2.f, 3.f, 4.f).ok());
    CHECK(!BuildingFx::buildEdgeCurveMesh(world, controls, 24, 0.f, 3.f, 4.f).ok());

    PlacementWorld xyWorld(10, 10, 10.f);
    xyWorld.setGridPlane("xy");
    auto xyMesh = BuildingFx::buildEdgeCurveMesh(xyWorld, controls, 8, 1.f, 2.f, 5.f);
    REQUIRE(xyMesh.ok());
    CHECK_EQ(xyMesh.value().sampleCount, 9);
    CHECK(std::abs(xyMesh.value().positions[2] - 5.f) < 0.0001f);
    CHECK(std::abs(xyMesh.value().positions[5] - 7.f) < 0.0001f);

    PlacementWorld hexWorld(10, 10, 10.f);
    hexWorld.setGridLayout("hexagon");
    CHECK(!BuildingFx::buildEdgeCurveMesh(hexWorld, controls, 8, 1.f, 2.f, 0.f).ok());
}

TEST_CASE("buildingfx.edgeCurvePreviewOwnsCpuMeshAndReportsBackendFallback") {
    BuildingRegistry::clear();
    BuildingDefinition wall;
    wall.id                    = "curve-preview-wall";
    wall.placementKind         = "edge";
    wall.channel               = "walls";
    wall.renderMode            = "3d";
    wall.visual3d["thickness"] = "0.5";
    wall.visual3d["height"]    = "3";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(10, 10, 10.f);
    world.setGridPlane("xz");
    BuildingFx* fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    const std::vector<BuildingFx::CurveControlPoint> controls{{1.f, 1.f}, {1.f, 6.f}, {7.f, 6.f}, {7.f, 1.f}};
    auto updated = fx->updateEdgeCurvePreview(&world, wall.id, controls, 24, 2);
    REQUIRE(updated.ok());
    CHECK(fx->hasEdgeCurvePreview(&world));
    CHECK_EQ(fx->getEdgeCurvePreviewFallbackReason(&world), "graphics_unavailable");
    fx->clearEdgeCurvePreview(&world);
    CHECK(!fx->hasEdgeCurvePreview(&world));
    CHECK_EQ(fx->getEdgeCurvePreviewFallbackReason(&world), "curve_preview_not_active");
    REQUIRE(fx->detach(&world));
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.edgeCurveCommitsAndExtrudesCustomSurfaceFrames") {
    BuildingRegistry::clear();
    BuildingDefinition wall;
    wall.id            = "surface-curve-wall";
    wall.placementKind = "edge";
    wall.channel       = "walls";
    wall.renderMode    = "3d";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(12, 12, 10.f);
    world.setGridPlane("xz");
    PlacementSystem::registerSurfaceProvider("curve.terrain", [](const PlacementWorld&, float x, float y) {
        PlacementSystem::PlacementHit hit;
        hit.worldX          = x;
        hit.worldY          = x * 0.1f;
        hit.worldZ          = y;
        hit.normalX         = -0.1f;
        hit.normalY         = 1.f;
        hit.surfaceId       = "terrain.curve";
        hit.surfaceRevision = 12;
        return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
    });
    const std::vector<PlacementSystem::EdgeCurvePoint> controls{{1.f, 1.f}, {1.f, 6.f}, {7.f, 6.f}, {7.f, 1.f}};
    auto placed = PlacementSystem::placeEdgeCubicBezierOnSurface(&world, wall.id, controls, 16, "curve.terrain");
    REQUIRE(placed.ok());
    auto group = world.edgeCurveGroupForInstance(placed.value().instanceIds.front());
    REQUIRE(group.ok());
    CHECK_EQ(group.value().surfaceProviderName, "curve.terrain");
    CHECK_EQ(group.value().surfaceId, "terrain.curve");
    CHECK_EQ(group.value().surfaceRevision, uint64_t{12});
    CHECK_EQ(group.value().surfaceSamples.size(), size_t{17});
    auto mesh = BuildingFx::buildEdgeCurveGroupMeshForInstance(world, placed.value().instanceIds.front(), 1.f, 3.f);
    REQUIRE(mesh.ok());
    CHECK_EQ(mesh.value().sampleCount, 17);
    CHECK(mesh.value().positions[1] < mesh.value().positions[mesh.value().positions.size() - 23]);

    PlacementWorld rejectedWorld(12, 12, 10.f);
    rejectedWorld.setGridPlane("xz");
    PlacementSystem::registerSurfaceProvider("curve.split", [](const PlacementWorld&, float x, float y) {
        PlacementSystem::PlacementHit hit;
        hit.worldX          = x;
        hit.worldY          = 0.f;
        hit.worldZ          = y;
        hit.surfaceId       = x < 40.f ? "terrain.left" : "terrain.right";
        hit.surfaceRevision = 1;
        return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
    });
    CHECK(!PlacementSystem::placeEdgeCubicBezierOnSurface(&rejectedWorld, wall.id, controls, 16, "curve.split").ok());
    CHECK_EQ(rejectedWorld.getBuildingCount(), 0);
    CHECK_EQ(rejectedWorld.getEdgeCurveGroupCount(), 0);
    PlacementSystem::unregisterSurface("curve.terrain");
    PlacementSystem::unregisterSurface("curve.split");
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.sessionCurvePreviewFollowsSurfaceAndKeepsLastValidDraft") {
    BuildingRegistry::clear();
    BuildingDefinition wall;
    wall.id                    = "session-surface-preview-wall";
    wall.placementKind         = "edge";
    wall.channel               = "walls";
    wall.renderMode            = "3d";
    wall.visual3d["thickness"] = "0.5";
    wall.visual3d["height"]    = "3";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(10, 10, 10.f);
    world.setGridPlane("xz");
    PlacementSystem::registerSurfaceProvider("preview.terrain", [](const PlacementWorld&, float x, float y) {
        PlacementSystem::PlacementHit hit;
        hit.worldX          = x;
        hit.worldY          = x * 0.1f;
        hit.worldZ          = y;
        hit.normalX         = -0.1f;
        hit.normalY         = 1.f;
        hit.surfaceId       = "terrain.preview";
        hit.surfaceRevision = 8;
        return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
    });
    PlacementSession session;
    REQUIRE(session.startPlacement(&world, wall.id));
    REQUIRE(static_cast<int>(session.beginEdgeCurve(1.f, 1.f)) == static_cast<int>(EdgePathUpdateStatus::Updated));
    REQUIRE(static_cast<int>(session.appendEdgeCurveControlPoint(1.f, 6.f)) ==
            static_cast<int>(EdgePathUpdateStatus::Updated));
    REQUIRE(static_cast<int>(session.appendEdgeCurveControlPoint(7.f, 6.f)) ==
            static_cast<int>(EdgePathUpdateStatus::Updated));
    REQUIRE(static_cast<int>(session.appendEdgeCurveControlPoint(7.f, 1.f)) ==
            static_cast<int>(EdgePathUpdateStatus::Updated));
    BuildingFx* fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    REQUIRE(fx->updateEdgeCurveSurfacePreview(&world, &session, 16, "preview.terrain").ok());
    CHECK(fx->hasEdgeCurvePreview(&world));
    CHECK_EQ(fx->getEdgeCurvePreviewSurfaceId(&world), "terrain.preview");
    CHECK_EQ(fx->getEdgeCurvePreviewSurfaceRevision(&world), uint64_t{8});
    CHECK(!fx->updateEdgeCurveSurfacePreview(&world, &session, 16, "missing.provider").ok());
    CHECK_EQ(fx->getEdgeCurvePreviewSurfaceId(&world), "terrain.preview");
    CHECK_EQ(fx->getEdgeCurvePreviewSurfaceRevision(&world), uint64_t{8});
    REQUIRE(fx->detach(&world));
    PlacementSystem::unregisterSurface("preview.terrain");
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.cornerVisualUsesVertexAnchorAndExplicitSize") {
    BuildingRegistry::clear();
    BuildingDefinition post;
    post.id                 = "visual-post";
    post.placementKind      = "corner";
    post.channel            = "posts";
    post.renderMode         = "3d";
    post.visual3d["width"]  = "0.25";
    post.visual3d["depth"]  = "0.35";
    post.visual3d["height"] = "3";
    BuildingRegistry::registerBuilding(post);

    PlacementWorld world(4, 4, 10.f);
    world.setGridPlane("xz");
    REQUIRE(world.placeCorner(post.id, 2, 3) > 0);
    BuildingFx* fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);

    int  visualCount = 0;
    auto view        = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::Transform3D>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [transform] = *it;
        CHECK(std::abs(transform->x - 20.f) < 0.0001f);
        CHECK(std::abs(transform->z - 30.f) < 0.0001f);
        CHECK(std::abs(transform->sx - 0.25f) < 0.0001f);
        CHECK(std::abs(transform->sy - 3.f) < 0.0001f);
        CHECK(std::abs(transform->sz - 0.35f) < 0.0001f);
        ++visualCount;
    }
    CHECK_EQ(visualCount, 1);
    REQUIRE(fx->detach(&world));
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.freeVisualUsesExactAnchorAndCollisionDiameterFallback") {
    BuildingRegistry::clear();
    BuildingDefinition prop;
    prop.id                 = "visual-free-prop";
    prop.placementKind      = "free";
    prop.snapMode           = "free";
    prop.rotationMode       = "free";
    prop.freeRadiusCells    = 0.3f;
    prop.renderMode         = "3d";
    prop.visual3d["height"] = "2";
    BuildingRegistry::registerBuilding(prop);
    PlacementWorld world(4, 4, 10.f);
    world.setGridPlane("xz");
    REQUIRE(world.placeFree(prop.id, 12.5f, 23.75f, 1.f, 27.f) > 0);
    BuildingFx* fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    int  count = 0;
    auto view  = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::Transform3D>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [transform] = *it;
        CHECK(std::abs(transform->x - 12.5f) < 0.0001f);
        CHECK(std::abs(transform->y - 1.f) < 0.0001f);
        CHECK(std::abs(transform->z - 23.75f) < 0.0001f);
        CHECK(std::abs(transform->sx - 6.f) < 0.0001f);
        CHECK(std::abs(transform->sz - 6.f) < 0.0001f);
        ++count;
    }
    CHECK_EQ(count, 1);
    REQUIRE(fx->detach(&world));
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.freeCustomVisualUsesCommittedFootprintBounds") {
    BuildingRegistry::clear();
    BuildingDefinition prop;
    prop.id                       = "visual-free-obb";
    prop.placementKind            = "free";
    prop.snapMode                 = "free";
    prop.rotationMode             = "free";
    prop.freeFootprintWidthCells  = 2.f;
    prop.freeFootprintHeightCells = 0.5f;
    prop.freeFootprintVertices    = {-1.f, 0.f, 0.f, -0.25f, 1.f, 0.f, 0.f, 0.25f};
    prop.renderMode               = "3d";
    BuildingRegistry::registerBuilding(prop);
    PlacementWorld world(4, 4, 10.f);
    world.setGridPlane("xz");
    REQUIRE(world.placeFree(prop.id, 12.5f, 23.75f, 1.f, 27.f) > 0);
    BuildingFx* fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    int  count = 0;
    auto view  = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::Transform3D>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [transform] = *it;
        CHECK(std::abs(transform->sx - 20.f) < 0.0001f);
        CHECK(std::abs(transform->sz - 5.f) < 0.0001f);
        ++count;
    }
    CHECK_EQ(count, 1);
    REQUIRE(fx->detach(&world));
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.edgeTopologySelectsMaskClassAndBaseResources") {
    BuildingRegistry::clear();
    BuildingDefinition wall;
    wall.id                                      = "resource-wall";
    wall.placementKind                           = "edge";
    wall.connectionGroup                         = "resource-boundary";
    wall.channel                                 = "boundary";
    wall.renderMode                              = "3d";
    wall.visual3d["mesh"]                        = "wall/base.mesh";
    wall.visual3d["variant.end.mesh"]            = "wall/end.mesh";
    wall.visual3d["variant.end.rotationDeg"]     = "15";
    wall.visual3d["variant.corner.mesh"]         = "wall/corner.mesh";
    wall.visual3d["variant.corner.mask.34.mesh"] = "wall/corner-34.mesh";
    wall.visual3d["variant.tee.mesh"]            = "wall/tee.mesh";
    wall.visual3d["variant.cross.mesh"]          = "wall/cross.mesh";
    BuildingRegistry::registerBuilding(wall);

    struct Case {
        int         mask;
        const char* variant;
        const char* resource;
    };
    const Case  cases[] = {{0, "isolated", "wall/base.mesh"}, {1, "end", "wall/end.mesh"},
                           {3, "straight", "wall/base.mesh"}, {34, "corner", "wall/corner-34.mesh"},
                           {42, "tee", "wall/tee.mesh"},      {43, "cross", "wall/cross.mesh"}};
    BuildingFx* fx      = BuildingFx::create();
    for (const Case& test : cases) {
        PlacementWorld world(8, 8, 10.f);
        world.setGridPlane("xz");
        const int center = world.placeEdge(wall.id, 2, 2, "north");
        REQUIRE(center > 0);
        if (test.mask & 0x01) REQUIRE(world.placeEdge(wall.id, 1, 2, "north") > 0);
        if (test.mask & 0x02) REQUIRE(world.placeEdge(wall.id, 3, 2, "north") > 0);
        if (test.mask & 0x04) REQUIRE(world.placeEdge(wall.id, 2, 1, "west") > 0);
        if (test.mask & 0x08) REQUIRE(world.placeEdge(wall.id, 2, 2, "west") > 0);
        if (test.mask & 0x10) REQUIRE(world.placeEdge(wall.id, 3, 1, "west") > 0);
        if (test.mask & 0x20) REQUIRE(world.placeEdge(wall.id, 3, 2, "west") > 0);
        REQUIRE_EQ(world.getEdgeConnectionMask(center), test.mask);
        REQUIRE(fx->attach(&world));
        fx->sync(&world);
        CHECK_EQ(fx->getVisualVariant(&world, center), test.variant);
        CHECK_EQ(fx->getVisualResource(&world, center), test.resource);
        CHECK_EQ(fx->getVisualFallbackReason(&world, center), "resolver_unavailable");
        REQUIRE(fx->detach(&world));
    }
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.edgeTopologyRefreshesResourceRotationAndMirror") {
    BuildingRegistry::clear();
    BuildingDefinition wall;
    wall.id                                  = "dynamic-wall";
    wall.placementKind                       = "edge";
    wall.renderMode                          = "3d";
    wall.visual3d["mesh"]                    = "wall/base.mesh";
    wall.visual3d["variant.end.mesh"]        = "wall/end.mesh";
    wall.visual3d["variant.end.rotationDeg"] = "15";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(8, 8, 10.f);
    world.setGridPlane("xz");
    const int center = world.placeEdge(wall.id, 2, 2, "north");
    REQUIRE(center > 0);
    BuildingFx* fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    CHECK_EQ(fx->getVisualVariant(&world, center), "isolated");
    CHECK_EQ(fx->getVisualResource(&world, center), "wall/base.mesh");

    REQUIRE(world.placeEdge(wall.id, 1, 2, "north") > 0);
    fx->sync(&world);
    CHECK_EQ(fx->getVisualVariant(&world, center), "end");
    CHECK_EQ(fx->getVisualResource(&world, center), "wall/end.mesh");
    bool foundCenter = false;
    auto view        = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::Transform3D>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [transform] = *it;
        if (std::abs(transform->x - 25.f) > 0.0001f || std::abs(transform->z - 20.f) > 0.0001f) continue;
        CHECK(transform->sx < 0.f);
        CHECK(std::abs(transform->yaw - 0.2617994f) < 0.0001f);
        foundCenter = true;
    }
    REQUIRE(foundCenter);
    REQUIRE(fx->detach(&world));
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.meshResolverPublishesBorrowedTopologyResource") {
    BuildingRegistry::clear();
    BuildingDefinition wall;
    wall.id               = "resolved-wall";
    wall.placementKind    = "edge";
    wall.renderMode       = "3d";
    wall.visual3d["mesh"] = "wall/resolved.mesh";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(4, 4, 1.f);
    const int      instanceId = world.placeEdge(wall.id, 1, 1, "north");
    REQUIRE(instanceId > 0);
    eve::graphics::Mesh mesh;
    int                 resolverCalls = 0;
    BuildingFx*         fx            = BuildingFx::create();
    fx->setMeshResolver([&](const std::string& resourceId) {
        ++resolverCalls;
        return resourceId == "wall/resolved.mesh" ? &mesh : nullptr;
    });
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    CHECK(resolverCalls > 0);
    CHECK_EQ(fx->getVisualResource(&world, instanceId), "wall/resolved.mesh");
    CHECK(fx->getVisualFallbackReason(&world, instanceId).empty());
    bool usesResolvedMesh = false;
    auto view             = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::MeshRenderer>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [renderer] = *it;
        if (renderer->mesh == &mesh) usesResolvedMesh = true;
    }
    CHECK(usesResolvedMesh);
    REQUIRE(fx->detach(&world));
    fx->clearMeshResolver();
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.areaPreviewProjection") {
    BuildingRegistry::clear();
    BuildingDefinition tile;
    tile.id = "heat-tile";
    BuildingRegistry::registerBuilding(tile);
    PlacementWorld world(4, 4, 1.f);
    REQUIRE(world.placeAt(tile.id, 1, 1) > 0);
    PlacementSession session;
    REQUIRE(session.startPlacement(&world, tile.id));
    CHECK_EQ(static_cast<int>(session.previewRectangle(0, 1, 2, 1)), static_cast<int>(AreaExecuteStatus::Rejected));

    BuildingFx* fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->updateAreaPreview(&world, &session);
    CHECK_EQ(fx->getAreaPreviewCount(&world), 3);
    CHECK(fx->getAreaPreviewAccepted(&world, 0));
    CHECK(!fx->getAreaPreviewAccepted(&world, 1));
    fx->clearAreaPreview(&world);
    CHECK_EQ(fx->getAreaPreviewCount(&world), 0);
    REQUIRE(fx->detach(&world));
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.projectsDiscreteFloorAlongGridNormal") {
    BuildingRegistry::clear();
    BuildingDefinition marker;
    marker.id         = "floor-marker";
    marker.renderMode = "3d";
    BuildingRegistry::registerBuilding(marker);
    PlacementWorld world(4, 4, 1.f);
    world.setGridPlane("xz");
    world.setFloorHeight(3.25f);
    const int groundId = world.placeAt(marker.id, 2, 1);
    REQUIRE(groundId > 0);
    world.setActiveLevel(2);
    const int instanceId = world.placeAt(marker.id, 2, 1);
    REQUIRE(instanceId > 0);
    BuildingFx* fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    bool found = false;
    auto view  = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::Transform3D>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [transform] = *it;
        if (std::abs(transform->x - 2.f) < 0.0001f && std::abs(transform->z - 1.f) < 0.0001f &&
            std::abs(transform->y - 6.5f) < 0.0001f)
            found = true;
    }
    CHECK(found);
    CHECK_EQ(fx->getLevelVisibilityMode(&world), "all");
    CHECK(fx->isVisualVisible(&world, groundId));
    CHECK(fx->isVisualVisible(&world, instanceId));
    fx->setLevelVisibilityMode(&world, "active");
    CHECK(!fx->isVisualVisible(&world, groundId));
    CHECK(fx->isVisualVisible(&world, instanceId));
    world.setActiveLevel(0);
    fx->sync(&world);
    CHECK(fx->isVisualVisible(&world, groundId));
    CHECK(!fx->isVisualVisible(&world, instanceId));
    fx->setLevelVisibilityMode(&world, "active_and_below");
    CHECK(fx->isVisualVisible(&world, groundId));
    CHECK(!fx->isVisualVisible(&world, instanceId));
    world.setActiveLevel(2);
    fx->sync(&world);
    CHECK(fx->isVisualVisible(&world, groundId));
    CHECK(fx->isVisualVisible(&world, instanceId));
    REQUIRE(fx->detach(&world));
    BuildingRegistry::clear();
}
}  // namespace test_buildingfx
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_card_payment_composition)
#include "PrivateTestScope.h"
#include <array>
#include <limits>
#include "card/Card.h"
#include "card/CardPlay.h"
#include "economy/EconomyLedger.h"
#include "rts/RTSEconomy.h"
#include "rts/RTSProductionAction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// card_payment_composition.cpp
namespace test_card_payment_composition {
namespace {
class Effect final : public eve::transaction::ITransactionParticipant {
public:
    int               value = 0;
    bool              fail  = false;
    std::string_view  name() const noexcept override { return "composition-effect"; }
    eve::Result<void> prepare(const eve::transaction::TransactionContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
        if (fail)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected failure", "test"));
        ++value;
        return eve::Result<void>::success();
    }
    eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
        --value;
        return eve::Result<void>::success();
    }
};
}  // namespace

TEST_CASE("card.customResourceCostComposesWithRtsAccountAndEffects") {
    eve::card::Card cards;
    REQUIRE_EQ(cards.registerCardsFromJson(R"([{"id":"deploy","cost":99}])"), 1);
    auto* card = cards.newCard("deploy");
    REQUIRE(card != nullptr);
    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("composition_ore", 10), 10);
    CHECK_EQ(ledger.credit("composition_supply", 4), 4);
    eve::rts::RTSEconomyAdapter    economy(ledger);
    Effect                         first, second;
    eve::card::CardPlayComposition composition;
    composition.paymentCost =
        eve::resource::CostSpec::from({{"composition_ore", 3}, {"composition_supply", 2}}).expect("valid cost");
    composition.effect            = &first;
    composition.additionalEffects = {&second};
    second.fail                   = true;
    const auto previous           = card->state()->phase;
    auto       rejected           = cards.play(*card, economy.account(), composition);
    CHECK(!rejected.ok());
    CHECK_EQ(first.value, 0);
    CHECK_EQ(ledger.get("composition_ore"), 10);
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(previous));
    second.fail = false;
    auto played = cards.play(*card, economy.account(), composition);
    REQUIRE(played.ok());
    CHECK_EQ(first.value, 1);
    CHECK_EQ(second.value, 1);
    CHECK_EQ(ledger.get("composition_ore"), 7);
    CHECK_EQ(ledger.get("composition_supply"), 2);
    CHECK_EQ(ledger.get("mana"), 0);
}

TEST_CASE("card.rtsPaymentRejectsDuplicateEffectsBeforeMutation") {
    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("composition_ore", 10), 10);
    eve::rts::RTSEconomyAdapter economy(ledger);
    auto                        cost = eve::resource::CostSpec::single("composition_ore", 3).expect("valid cost");
    Effect                      effect;
    std::array<eve::transaction::ITransactionParticipant*, 2> effects{&effect, &effect};
    auto                                                      rejected = economy.pay(cost, effects);
    CHECK(!rejected.ok());
    CHECK_EQ(effect.value, 0);
    CHECK_EQ(ledger.get("composition_ore"), 10);
    Effect second;
    effects[1] = &second;
    auto paid  = economy.pay(cost, effects);
    REQUIRE(paid.ok());
    CHECK_EQ(effect.value, 1);
    CHECK_EQ(second.value, 1);
    CHECK_EQ(ledger.get("composition_ore"), 7);
}

TEST_CASE("card.customPaymentRejectsInvalidCostAndMissingAccount") {
    eve::card::Card cards;
    REQUIRE_EQ(cards.registerCardsFromJson(R"([{"id":"free","cost":0}])"), 1);
    auto* card = cards.newCard("free");
    REQUIRE(card != nullptr);
    eve::card::CardDefinition definition;
    definition.id = "free";
    eve::card::CardPlayRequest request;
    request.card                    = card;
    request.definition              = &definition;
    request.composition.paymentCost = eve::resource::CostSpec::single("ore", 1).expect("cost");
    auto missing                    = eve::card::CardPlayPaymentAdapter::play(request);
    CHECK(!missing.ok());
    eve::economy::EconomyLedger ledger;
    eve::rts::RTSEconomyAdapter economy(ledger);
    request.playerAccount           = &economy.account();
    request.composition.paymentCost = eve::resource::CostSpec{};
    auto invalid                    = eve::card::CardPlayPaymentAdapter::play(request);
    CHECK(!invalid.ok());
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(eve::card::CardState::Deck));
    request.playerAccount = nullptr;
    request.composition.paymentCost.reset();
    auto free = eve::card::CardPlayPaymentAdapter::play(request);
    REQUIRE(free.ok());
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(eve::card::CardState::Played));
}

TEST_CASE("card.economyAccountingOverflowRejectsWithoutPublishing") {
    eve::economy::EconomyLedger ledger;
    auto                        snapshot = ledger.snapshot();
    snapshot.current["composition_ore"]  = 10;
    snapshot.income["composition_ore"]   = std::numeric_limits<int>::max();
    snapshot.expense["composition_ore"]  = std::numeric_limits<int>::max();
    ledger.restore(snapshot);
    eve::rts::RTSEconomyAdapter economy(ledger);
    auto                        cost     = eve::resource::CostSpec::single("composition_ore", 1).expect("valid cost");
    auto                        credited = economy.credit(cost);
    CHECK(!credited.ok());
    Effect effect;
    auto   paid = economy.pay(cost, effect);
    CHECK(!paid.ok());
    CHECK_EQ(effect.value, 0);
    CHECK_EQ(ledger.get("composition_ore"), 10);
    CHECK_EQ(ledger.getIncome("composition_ore"), std::numeric_limits<int>::max());
    CHECK_EQ(ledger.getExpense("composition_ore"), std::numeric_limits<int>::max());
}

TEST_CASE("card.productionAndCardConsumptionShareOnePaymentAndRollback") {
    eve::card::Card cards;
    REQUIRE_EQ(cards.registerCardsFromJson(R"([{"id":"reinforcement","cost":0}])"), 1);
    auto* card = cards.newCard("reinforcement");
    REQUIRE(card != nullptr);
    const auto                  original = card->state()->phase;
    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("composition_ore", 10), 10);
    eve::rts::RTSEconomyAdapter economy(ledger);
    eve::production::WorkQueue  production;
    eve::orders::CommandQueue   orders;
    eve::action::ActionRuntime  actions;
    eve::rts::RTSBuildRequest   request;
    request.production     = &production;
    request.orders         = &orders;
    request.action         = &actions;
    request.account        = &economy.account();
    request.cost           = eve::resource::CostSpec::single("composition_ore", 3).expect("valid cost");
    request.owner          = "factory";
    request.product        = "soldier";
    request.productionKind = "unit";
    request.duration       = eve::Duration::fromSeconds(1).expect("duration");
    Effect effect;
    effect.fail = true;
    eve::card::CardPlayParticipant rejectedCard(*card);
    request.additionalEffects = {&rejectedCard, &effect};
    auto rejected             = eve::rts::RTSProductionActionAdapter::build(request);
    CHECK(!rejected.ok());
    CHECK_EQ(production.taskCount(), 0u);
    CHECK_EQ(orders.orderCount(), 0u);
    CHECK_EQ(ledger.get("composition_ore"), 10);
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(original));
    effect.fail = false;
    eve::card::CardPlayParticipant acceptedCard(*card);
    request.additionalEffects = {&acceptedCard, &effect};
    auto accepted             = eve::rts::RTSProductionActionAdapter::build(request);
    REQUIRE(accepted.ok());
    CHECK_EQ(production.taskCount(), 1u);
    CHECK_EQ(orders.orderCount(), 1u);
    CHECK_EQ(ledger.get("composition_ore"), 7);
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(eve::card::CardState::Played));
}
}  // namespace test_card_payment_composition
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_animation)
#include "PrivateTestScope.h"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "PathBesideSource.h"
#include "animation/AnimClip.h"
#include "animation/AnimGraph.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"
#include "climbing/Climbing.h"
#include "climbing/ClimbingAnimation.h"
#include "climbing/ClimbingPose.h"
#include "common/Runtime.h"
#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// climbing_animation.cpp
namespace test_climbing_animation {
// climbing_animation.cpp
namespace {

eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return std::move(duration).takeValue();
}

}  // namespace

TEST_CASE("climbing.animation.extractsRootMotionAndSemanticNotifies") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     clip("mantle");
    clip.setDuration(1.f);
    clip.setLoop(false);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(root, 1.f, 1.f, 0.f, 0.f);
    clip.addEvent(0.1f, "contact.left_hand");
    clip.addEvent(0.15f, "collision.compact");
    clip.addEvent(0.2f, "contact.right_hand");
    clip.addEvent(0.22f, "studio.camera_cue");

    eve::animation::AnimPlayer player(&skeleton);
    REQUIRE(eve::climbing::beginClimbingAnimation(player, clip, root).ok());
    auto frame =
        eve::climbing::advanceClimbingAnimation(player, {eve::SimulationTick(1), seconds(0.25)}, {0.f, 0.f, 1.f});
    REQUIRE(frame.ok());
    CHECK(std::fabs(frame.value().motion.rootTranslation.x - 0.25f) < 0.001f);
    CHECK(frame.value().motion.hasRootMotion);
    CHECK_EQ(frame.value().recognizedNotifyCount, std::uint32_t(3));
    CHECK_EQ(frame.value().ignoredNotifyCount, std::uint32_t(1));
    REQUIRE_EQ(frame.value().motion.notifies.size(), std::size_t(3));
}

TEST_CASE("climbing.animation.graphProjectionTriggersOnceAndDegradesExplicitly") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     idle("idle");
    idle.setDuration(1.f);
    idle.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    eve::animation::AnimClip shot("climb");
    shot.setDuration(0.5f);
    shot.setLoop(false);
    shot.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    shot.addPositionKey(root, 0.5f, 0.f, 1.f, 0.f);

    eve::animation::AnimGraph graph(&skeleton);
    const int                 base    = graph.addClip(&idle);
    const int                 action  = graph.addClip(&shot);
    const int                 oneShot = graph.addOneShot(base, action, 0.05f, 0.05f);
    graph.setRoot(oneShot);
    eve::climbing::ClimbingGraphState state;
    auto applied = eve::climbing::driveClimbingGraph(graph, oneShot, eve::climbing::ClimbingExecutionId(9), state);
    REQUIRE(applied.ok());
    CHECK(graph.isOneShotActive(oneShot));
    auto duplicate = eve::climbing::driveClimbingGraph(graph, oneShot, eve::climbing::ClimbingExecutionId(9), state);
    REQUIRE(duplicate.ok());
    CHECK_EQ(static_cast<int>(duplicate.code()), static_cast<int>(eve::StatusCode::NoOp));

    CHECK_EQ(static_cast<int>(eve::climbing::useDirectClimbingPose(eve::climbing::ClimbingExecutionId(10), state)),
             static_cast<int>(eve::climbing::ClimbingGraphProvider::DirectPoseConstraints));
    CHECK_EQ(state.activeExecutionId.value(), std::uint64_t(10));
}

TEST_CASE("climbing.animation.bindingValidatesClipIdentityAndResolvesRootBone") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("Root", -1);
    eve::animation::AnimClip     clip("anim:mantle");
    clip.setDuration(0.5f);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    eve::animation::AnimPlayer player(&skeleton);

    eve::climbing::ClimbingAnimationBinding binding{"anim:mantle", "climbing.mantle", "Root", true};
    REQUIRE(eve::climbing::beginClimbingAnimation(player, clip, binding).ok());

    binding.clipId = "anim:other";
    auto wrongClip = eve::climbing::beginClimbingAnimation(player, clip, binding);
    CHECK(!wrongClip.ok());
    binding.clipId   = "anim:mantle";
    binding.rootBone = "Missing";
    auto missingRoot = eve::climbing::beginClimbingAnimation(player, clip, binding);
    CHECK(!missingRoot.ok());
}

TEST_CASE("climbing.animation.rejectsInvalidGraphAndDuplicatePlayerTicks") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     clip("climb");
    clip.setDuration(1.f);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    eve::animation::AnimPlayer player(&skeleton);
    REQUIRE(eve::climbing::beginClimbingAnimation(player, clip, root).ok());
    REQUIRE(
        eve::climbing::advanceClimbingAnimation(player, {eve::SimulationTick(3), seconds(0.1)}, {0.f, 0.f, 1.f}).ok());
    auto duplicate =
        eve::climbing::advanceClimbingAnimation(player, {eve::SimulationTick(3), seconds(0.1)}, {0.f, 0.f, 1.f});
    CHECK(!duplicate.ok());

    eve::animation::AnimGraph graph(&skeleton);
    const int                 clipNode = graph.addClip(&clip);
    graph.setRoot(clipNode);
    eve::climbing::ClimbingGraphState state;
    auto invalid = eve::climbing::driveClimbingGraph(graph, clipNode, eve::climbing::ClimbingExecutionId(1), state);
    CHECK(!invalid.ok());
    CHECK(state.activeExecutionId.isZero());
}

TEST_CASE("climbing.animation.motionMatchingYieldsToActionAndResumesByTick") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     locomotion("run");
    locomotion.setDuration(1.f);
    locomotion.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    locomotion.addPositionKey(root, 1.f, 0.f, 0.f, 2.f);
    eve::animation::MotionDatabase database(&skeleton);
    database.addClip(&locomotion);
    database.bake();
    eve::animation::MotionMatcher     matcher(&skeleton, &database);
    eve::climbing::ClimbingGraphState state;
    auto                              locomotionProvider = eve::climbing::driveClimbingMotionMatching(
        matcher, {eve::SimulationTick(1), seconds(0.1)}, {0.f, 0.f, 2.f}, 0.f, state);
    REQUIRE(locomotionProvider.ok());
    CHECK_EQ(static_cast<int>(state.provider), static_cast<int>(eve::climbing::ClimbingGraphProvider::MotionMatching));
    CHECK(state.activeExecutionId.isZero());

    eve::animation::AnimGraph graph(&skeleton);
    const int                 base    = graph.addClip(&locomotion);
    const int                 action  = graph.addClip(&locomotion);
    const int                 oneShot = graph.addOneShot(base, action, 0.05f, 0.05f);
    graph.setRoot(oneShot);
    REQUIRE(eve::climbing::driveClimbingGraph(graph, oneShot, eve::climbing::ClimbingExecutionId(42), state).ok());
    CHECK_EQ(static_cast<int>(state.provider), static_cast<int>(eve::climbing::ClimbingGraphProvider::AnimGraph));

    auto resumed = eve::climbing::driveClimbingMotionMatching(matcher, {eve::SimulationTick(3), seconds(0.1)},
                                                              {1.f, 0.f, 0.f}, 1.5707963f, state);
    REQUIRE(resumed.ok());
    CHECK_EQ(static_cast<int>(state.provider), static_cast<int>(eve::climbing::ClimbingGraphProvider::MotionMatching));
    CHECK(state.activeExecutionId.isZero());
    CHECK(std::fabs(matcher.getDesiredVelocityX() - 1.f) < 0.0001f);
}

// climbing_motion_matching_demo.cpp
namespace {
int animationIndex(const eve::model3d::ModelData& model, std::string_view name) {
    for (int index = 0; index < model.getAnimationCount(); ++index)
        if (model.getAnimationName(index) == name) return index;
    return -1;
}
}  // namespace

TEST_CASE("climbing.motionMatchingDemoLoadsRedistributableMannequinAndActionClips") {
    const std::filesystem::path sourceRoot =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path();
    const std::filesystem::path example = sourceRoot / "examples" / "climbing-motion-matching";
    const std::filesystem::path assets  = example / "assets" / "kaykit";

    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem != nullptr);
    REQUIRE(filesystem->setIdentity("ev_ut_climbing_motion_matching", true));
    REQUIRE(filesystem->setupWriteDirectory());
    filesystem->allowMountingForPath(assets.string());
    REQUIRE(filesystem->mount(assets.string(), "", false));

    auto* models = eve::model3d::Model3D::create();
    REQUIRE(models != nullptr);
    std::unique_ptr<eve::model3d::ModelData> basic(models->newModelDataFromFile("Rig_Medium_MovementBasic.glb"));
    std::unique_ptr<eve::model3d::ModelData> advanced(models->newModelDataFromFile("Rig_Medium_MovementAdvanced.glb"));
    REQUIRE(basic.get() != nullptr);
    REQUIRE(advanced.get() != nullptr);
    REQUIRE(basic->getMeshCount() >= 6);
    REQUIRE(basic->getAnimationCount() >= 10);
    REQUIRE(advanced->getAnimationCount() >= 10);

    std::unique_ptr<eve::animation::AnimSkeleton> skeleton(
        eve::animation::AnimImporter::loadSkeletonFromModel(basic.get()));
    REQUIRE(skeleton.get() != nullptr);
    CHECK(skeleton->findBone("root") >= 0);
    CHECK(skeleton->findBone("foot.l") >= 0);
    CHECK(skeleton->findBone("foot.r") >= 0);

    for (const auto name : {"Walking_A", "Running_A", "Jump_Full_Short", "Jump_Full_Long"}) {
        const int index = animationIndex(*basic, name);
        REQUIRE(index >= 0);
        std::unique_ptr<eve::animation::AnimClip> clip(
            eve::animation::AnimImporter::loadClipFromModel(basic.get(), skeleton.get(), index));
        REQUIRE(clip.get() != nullptr);
        CHECK(clip->getDuration() > 0.0f);
    }
    for (const auto name : {"Running_Strafe_Left", "Running_Strafe_Right", "Dodge_Forward"})
        CHECK(animationIndex(*advanced, name) >= 0);

    CHECK(std::filesystem::exists(example / "assets" / "kaykit" / "LICENSE-KAYKIT-ANIMATIONS.txt"));
    CHECK(!std::filesystem::exists(example / "assets" / "ue-only" / "GameAnimationSample.uproject"));
    CHECK(filesystem->unmount(assets.string()));
}

TEST_CASE("climbing.motionMatchingDemoScriptCompilesAndDocumentsUeOnlyBoundary") {
    const std::filesystem::path sourceRoot =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path();
    const std::filesystem::path scriptPath = sourceRoot / "examples" / "climbing-motion-matching" / "main.nut";
    std::ifstream               input(scriptPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();
    CHECK(source.str().find("eve.Climbing()") != std::string::npos);
    CHECK(source.str().find("newMotionMatcher") != std::string::npos);
    CHECK(source.str().find("OrientationWarping") != std::string::npos);
    CHECK(source.str().find("displayPose.copyFrom") != std::string::npos);
    CHECK(source.str().find("assets/ue-only") == std::string::npos);

    eve::Runtime runtime(4096, ssq::Libs::ALL);
    bool         compiled = true;
    try {
        runtime.compileSource(source.str(), "examples/climbing-motion-matching/main.nut");
    } catch (...) {
        compiled = false;
    }
    CHECK(compiled);
}

// climbing_pose.cpp
TEST_CASE("climbing.pose.appliesBuiltInTwoBoneHandContacts") {
    eve::animation::AnimSkeleton skeleton;
    const int                    leftRoot  = skeleton.addBone("left_upper", -1);
    const int                    leftMid   = skeleton.addBone("left_lower", leftRoot);
    const int                    leftTip   = skeleton.addBone("left_hand", leftMid);
    const int                    rightRoot = skeleton.addBone("right_upper", -1);
    const int                    rightMid  = skeleton.addBone("right_lower", rightRoot);
    const int                    rightTip  = skeleton.addBone("right_hand", rightMid);
    skeleton.setBindPosition(leftMid, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(leftTip, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(rightMid, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(rightTip, 0.f, 0.5f, 0.f);

    eve::animation::AnimPose pose(skeleton.getBoneCount());
    skeleton.applyBindPose(&pose);
    eve::climbing::ClimbingAdvance advance;
    advance.phase           = eve::climbing::ClimbingPhase::Hanging;
    advance.leftHandAnchor  = {-0.2f, 0.9f, 0.2f};
    advance.rightHandAnchor = {0.2f, 0.9f, 0.2f};
    advance.contactWeight   = 1.f;
    eve::climbing::ClimbingPoseBinding binding;
    binding.leftArm  = {leftRoot, leftMid, leftTip};
    binding.rightArm = {rightRoot, rightMid, rightTip};

    auto applied = eve::climbing::applyClimbingPose(skeleton, pose, advance, binding);
    REQUIRE(applied.ok());
    CHECK(applied.value().leftHandApplied);
    CHECK(applied.value().rightHandApplied);
    CHECK_EQ(applied.value().provider, std::string("animation.two_bone"));
    CHECK(std::fabs(applied.value().weight - 1.f) < 1e-6f);
}

TEST_CASE("climbing.pose.reportsInvalidBoneChainsAndNoOpWeight") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root", -1);
    eve::animation::AnimPose pose(skeleton.getBoneCount());
    skeleton.applyBindPose(&pose);
    eve::climbing::ClimbingAdvance     advance;
    eve::climbing::ClimbingPoseBinding invalid;
    invalid.leftArm         = {0, 1, 2};
    invalid.enableRightHand = false;
    auto rejected           = eve::climbing::applyClimbingPose(skeleton, pose, advance, invalid);
    CHECK(!rejected.ok());

    invalid.enableLeftHand = false;
    auto noOp              = eve::climbing::applyClimbingPose(skeleton, pose, advance, invalid);
    REQUIRE(noOp.ok());
    CHECK_EQ(static_cast<int>(noOp.code()), static_cast<int>(eve::StatusCode::NoOp));
}

TEST_CASE("climbing.pose.appliesOnlyTheHandWhoseContactWindowIsActive") {
    eve::animation::AnimSkeleton skeleton;
    const int                    leftRoot  = skeleton.addBone("left_upper", -1);
    const int                    leftMid   = skeleton.addBone("left_lower", leftRoot);
    const int                    leftTip   = skeleton.addBone("left_hand", leftMid);
    const int                    rightRoot = skeleton.addBone("right_upper", -1);
    const int                    rightMid  = skeleton.addBone("right_lower", rightRoot);
    const int                    rightTip  = skeleton.addBone("right_hand", rightMid);
    skeleton.setBindPosition(leftMid, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(leftTip, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(rightMid, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(rightTip, 0.f, 0.5f, 0.f);
    eve::animation::AnimPose pose(skeleton.getBoneCount());
    skeleton.applyBindPose(&pose);

    eve::climbing::ClimbingAdvance advance;
    advance.leftHandAnchor  = {-0.2f, 0.9f, 0.2f};
    advance.rightHandAnchor = {0.2f, 0.9f, 0.2f};
    advance.leftHandWeight  = 1.f;
    advance.rightHandWeight = 0.f;
    eve::climbing::ClimbingPoseBinding binding;
    binding.leftArm  = {leftRoot, leftMid, leftTip};
    binding.rightArm = {rightRoot, rightMid, rightTip};
    auto applied     = eve::climbing::applyClimbingPose(skeleton, pose, advance, binding);
    REQUIRE(applied.ok());
    CHECK(applied.value().leftHandApplied);
    CHECK(!applied.value().rightHandApplied);
}

// climbing_vault.cpp
namespace {

eve::climbing::ClimbingActionDefinition vaultAction(std::string id, float minHeight, float maxHeight,
                                                    int selectionBias = 0) {
    eve::climbing::ClimbingActionDefinition action;
    action.id                        = std::move(id);
    action.kind                      = eve::climbing::ClimbingActionKind::Vault;
    action.minHeight                 = minHeight;
    action.maxHeight                 = maxHeight;
    action.minSpeed                  = 3.f;
    action.duration                  = eve::Duration::fromNanoseconds(600000000);
    action.landingForward            = 0.75f;
    action.apexHeight                = 0.55f;
    action.selectionBias             = selectionBias;
    action.maxTranslationWarpPerTick = 0.2f;
    action.horizontalWarpBudget      = 1.2f;
    action.verticalWarpBudget        = 1.2f;
    action.requiredNotifies          = {"contact.left_hand", "land"};
    return action;
}

struct VaultWorld {
    VaultWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        player = world->newBody("kinematic", 0.f, 0.9f, 0.f);
        player->newCapsuleShape(1.8f, 0.3f);
        obstacle = world->newBody("static", 0.f, 0.3f, 1.f);
        obstacle->newBoxShape(1.35f, 0.6f, 0.65f);
    }

    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  player   = nullptr;
    eve::physics::Body3D*                  obstacle = nullptr;
};

struct VaultRun {
    std::vector<eve::climbing::ClimbingAdvance> advances;
    std::vector<eve::climbing::ClimbingEvent>   events;
    bool                                        sawConstraint = false;
    bool                                        landingClear  = false;
    std::uint64_t                               replayHash    = 1469598103934665603ull;
};

void hashValue(std::uint64_t& hash, std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= (bits >> (byte * 8)) & 0xffu;
        hash *= 1099511628211ull;
    }
}

std::int64_t millimeters(float value) {
    return static_cast<std::int64_t>(std::llround(static_cast<double>(value) * 1000.0));
}

VaultRun runVault() {
    VaultWorld                               fixture;
    eve::climbing::ClimbingRuntime           runtime;
    eve::climbing::ClimbingProfileDefinition profile;
    profile.maxWarpResidual = 5.f;
    profile.actions.push_back(vaultAction("parkour:vault_low", 0.4f, 0.78f));
    profile.actions.push_back(vaultAction("parkour:vault_high", 0.79f, 1.2f, 1));
    REQUIRE(runtime.setProfile(profile).ok());

    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     clip("vault_low");
    clip.setDuration(0.6f);
    clip.setLoop(false);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(root, 0.6f, 0.f, 0.6f, 1.8f);
    clip.addEvent(0.15f, "contact.left_hand");
    clip.addEvent(0.55f, "land");
    REQUIRE(runtime.validateAnimationBinding("parkour:vault_low", clip).ok());

    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 4.5f, fixture.player->getId(), 0.f, true};
    auto                              candidates = runtime.probe(*fixture.world, pose);
    REQUIRE(candidates.ok());
    REQUIRE_EQ(candidates.value().size(), std::size_t{1});
    CHECK_EQ(candidates.value().front().actionId, std::string("parkour:vault_low"));
    CHECK(candidates.value().front().obstacleBody == fixture.obstacle->runtimeHandle());
    REQUIRE(runtime.tryBegin(*fixture.world, pose, eve::SimulationTick(100)).ok());

    eve::animation::AnimPlayer player(&skeleton);
    REQUIRE(eve::climbing::beginClimbingAnimation(player, clip, root).ok());
    VaultRun output;
    for (std::uint64_t frame = 1; frame <= 6; ++frame) {
        const eve::SimulationStep step{eve::SimulationTick(100 + frame), eve::Duration::fromNanoseconds(100000000)};
        auto animationFrame = eve::climbing::advanceClimbingAnimation(player, step, {0.f, 0.f, 1.f});
        REQUIRE(animationFrame.ok());
        auto advanced = runtime.advance(*fixture.world, step, animationFrame.value().motion);
        REQUIRE(advanced.ok());
        output.sawConstraint = output.sawConstraint || advanced.value().constrained ||
                               std::fabs(advanced.value().desiredDelta.x - advanced.value().actualDelta.x) > 1e-4f ||
                               std::fabs(advanced.value().desiredDelta.y - advanced.value().actualDelta.y) > 1e-4f ||
                               std::fabs(advanced.value().desiredDelta.z - advanced.value().actualDelta.z) > 1e-4f;
        hashValue(output.replayHash, millimeters(advanced.value().feet.x));
        hashValue(output.replayHash, millimeters(advanced.value().feet.y));
        hashValue(output.replayHash, millimeters(advanced.value().feet.z));
        output.advances.push_back(std::move(advanced).takeValue());
    }
    auto events = runtime.drainEvents();
    REQUIRE(events.ok());
    output.events = std::move(events).takeValue();
    for (const auto& event : output.events) {
        hashValue(output.replayHash, static_cast<std::int64_t>(event.kind));
        hashValue(output.replayHash, static_cast<std::int64_t>(event.tick.value()));
    }

    const auto&                 landing = output.advances.back().feet;
    eve::physics::QueryFilter3D filter;
    filter.ignoredBodyId = fixture.player->getId();
    auto overlap = fixture.world->queryCapsuleOwned(landing.x, landing.y + profile.capsuleRadius, landing.z, landing.x,
                                                    landing.y + profile.capsuleHeight - profile.capsuleRadius,
                                                    landing.z, profile.capsuleRadius, filter);
    REQUIRE(overlap.ok());
    output.landingClear = overlap.value().bodyCount == 0;
    return output;
}

}  // namespace

TEST_CASE("climbing.vault.runningJumpUsesLowVaultAndProducesStableConstrainedLanding") {
    const VaultRun first = runVault();
    REQUIRE_EQ(first.advances.size(), std::size_t{6});
    CHECK(first.sawConstraint);
    CHECK(first.landingClear);
    CHECK_EQ(static_cast<int>(first.advances.back().phase), static_cast<int>(eve::climbing::ClimbingPhase::Completed));

    REQUIRE_EQ(first.events.size(), std::size_t{4});
    CHECK_EQ(static_cast<int>(first.events[0].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Started));
    CHECK_EQ(static_cast<int>(first.events[1].kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::ContactLeftHand));
    CHECK_EQ(static_cast<int>(first.events[2].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Landed));
    CHECK_EQ(static_cast<int>(first.events[3].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Completed));

    const VaultRun replay = runVault();
    CHECK_EQ(replay.replayHash, first.replayHash);
}

TEST_CASE("climbing.vault.destroyedTargetCancelsWithStaleEvidence") {
    VaultWorld                               fixture;
    eve::climbing::ClimbingRuntime           runtime;
    auto                                     low = vaultAction("parkour:vault_low", 0.4f, 0.78f);
    eve::climbing::ClimbingProfileDefinition profile;
    profile.maxWarpResidual = 5.f;
    profile.actions.push_back(low);
    profile.actions.push_back(vaultAction("parkour:vault_high", 0.79f, 1.2f, 1));
    REQUIRE(runtime.setProfile(profile).ok());
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     clip("vault_low_stale");
    clip.setDuration(0.6f);
    clip.setLoop(false);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(root, 0.6f, 0.f, 0.6f, 1.8f);
    clip.addEvent(0.15f, "contact.left_hand");
    clip.addEvent(0.55f, "land");
    REQUIRE(runtime.validateAnimationBinding("parkour:vault_low", clip).ok());
    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 4.5f, fixture.player->getId(), 0.f, true};
    auto                              candidates = runtime.probe(*fixture.world, pose);
    REQUIRE(candidates.ok());
    REQUIRE_EQ(candidates.value().size(), std::size_t{1});
    CHECK(candidates.value().front().obstacleBody == fixture.obstacle->runtimeHandle());
    REQUIRE(runtime.tryBegin(*fixture.world, pose, eve::SimulationTick(5)).ok());
    fixture.world->destroyBody(fixture.obstacle);
    fixture.obstacle = nullptr;

    auto advanced = runtime.advance(*fixture.world, {eve::SimulationTick(6), eve::Duration::fromNanoseconds(16666667)});
    CHECK(!advanced.ok());
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Cancelled));
    CHECK_EQ(runtime.inspect().terminalCode, std::string("climbing.anchor.stale"));
    auto events = runtime.drainEvents();
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t{2});
    CHECK_EQ(static_cast<int>(events.value().back().kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::Cancelled));
}
}  // namespace test_climbing_animation
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_action_damage)
#include "PrivateTestScope.h"
#include <functional>
#include <string_view>
#include "action/ActionDamageBlock.h"
#include "action/ActionNotifyRegistry.h"
#include "combat/ActionDamageSink.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_action_damage.cpp
namespace test_combat_action_damage {
namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::SubjectRef subject(std::string_view value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

}  // namespace

TEST_CASE("combatActionDamage.validatesRoutesAndAtomicallyMutatesResolvedState") {
    eve::Value::Object payload{{"damageType", "Damage.Physical.Slash"},
                               {"amount", 18.0},
                               {"poiseAmount", 7.0},
                               {"targetIndex", 0},
                               {"knockback", eve::Value::Array{1.0, 0.5, -2.0}}};
    auto               binding = eve::action::ActionDamageBinding::fromPayload(payload);
    REQUIRE(binding.ok());
    CHECK_EQ(binding.value().amount, 18.0);
    CHECK_EQ(binding.value().poiseAmount, 7.0);
    CHECK_EQ(binding.value().knockback.z, -2.0);

    auto invalid      = payload;
    invalid["amount"] = -1.0;
    auto rejected     = eve::action::ActionDamageBinding::fromPayload(invalid);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.status().diagnostics().front().path(), "amount");

    ecs::EntityHandle        sourceHandle{nullptr, typeid(void), 1, 1};
    ecs::EntityHandle        targetHandle{nullptr, typeid(void), 2, 1};
    eve::combat::CombatState source{subject("11121314-1516-1718-991a-1b1c1d1e1f20"), 80.0, 80.0, 20.0, 20.0};
    eve::combat::CombatState target{subject("01020304-0506-0708-890a-0b0c0d0e0f10"), 100.0, 100.0, 40.0, 40.0};
    eve::combat::CombatActionDamageSink sink(
        [&](ecs::EntityHandle handle) -> eve::OptionalRef<eve::combat::CombatState> {
            if (handle.id == sourceHandle.id) return std::ref(source);
            if (handle.id == targetHandle.id) return std::ref(target);
            return std::nullopt;
        });

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionTimelineEvent event{eve::action::ActionTimelineEventKind::Notify,
                                           id("combat-track:damage"),
                                           id("combat-notify:damage"),
                                           id("combat:damage"),
                                           eve::Duration::zero(),
                                           payload};
    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId{404};
    context.source      = sourceHandle;
    context.targets.push_back(targetHandle);

    auto missing = registry.value().dispatch(event, context);
    CHECK(!missing.ok());
    CHECK_EQ(missing.status().code(), eve::StatusCode::NotFound);
    CHECK_EQ(target.health, 100.0);

    sink.setEnabled(true);
    REQUIRE(sink.enabled());
    REQUIRE(registry.value().dispatch(event, context).ok());
    CHECK_EQ(target.health, 82.0);
    CHECK_EQ(target.poise, 33.0);
    auto outcome = sink.lastOutcome();
    REQUIRE(outcome.has_value());
    CHECK_EQ(outcome->source, source.subject);
    CHECK_EQ(outcome->target, target.subject);
    CHECK_EQ(outcome->knockback.z, -2.0);

    sink.setEnabled(false);
    CHECK(!sink.enabled());
}
}  // namespace test_combat_action_damage
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_action_windows)
#include "PrivateTestScope.h"
#include <string_view>
#include "action/ActionBlockRuntime.h"
#include "action/ActionDamageBlock.h"
#include "action/ActionNotifyRegistry.h"
#include "action/ActionStateWindowBlock.h"
#include "combat/ActionDamageSink.h"
#include "combat/ActionWindowState.h"
#include "combat/MeleeHit.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_action_windows.cpp
namespace test_combat_action_windows {
namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::SubjectRef subject(std::string_view value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

eve::action::ActionActiveBlock active(const eve::action::ActionTimelineEvent& event) {
    return {event.trackId, event.itemId, event.type, eve::Duration::zero(), eve::Duration::fromSeconds(1.0).takeValue(),
            event.payload};
}

}  // namespace

TEST_CASE("combatActionWindows.pairHitboxInvulnerabilityAndInterruptCleanup") {
    CHECK(!eve::action::ActionStateWindowBinding::fromPayload("combat:hitbox-window", {}).ok());
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    const eve::action::ActionExecutionId execution(501);
    ecs::EntityHandle                    sourceHandle{nullptr, typeid(void), 1, 3};
    ecs::EntityHandle                    targetHandle{nullptr, typeid(void), 2, 4};
    const auto                           sourceSubject = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto                           targetSubject = subject("01020304-0506-0708-890a-0b0c0d0e0f10");
    bool                                 resolverAlive = true;
    eve::combat::CombatActionWindowState windows([&](ecs::EntityHandle handle) -> eve::Result<eve::SubjectRef> {
        if (!resolverAlive)
            return eve::Result<eve::SubjectRef>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "entity is stale", "entity"));
        if (handle.id == sourceHandle.id) return eve::Result<eve::SubjectRef>::success(sourceSubject);
        if (handle.id == targetHandle.id) return eve::Result<eve::SubjectRef>::success(targetSubject);
        return eve::Result<eve::SubjectRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "entity is unknown", "entity"));
    });

    eve::action::ActionTimelineEvent hitbox{eve::action::ActionTimelineEventKind::StateEnter,
                                            id("combat-track:windows"),
                                            id("combat-window:hitbox"),
                                            id("combat:hitbox-window"),
                                            eve::Duration::zero(),
                                            {{"hitbox", eve::Value("weapon.main")}}};
    eve::action::ActionTimelineEvent invulnerability{eve::action::ActionTimelineEventKind::StateEnter,
                                                     id("combat-track:windows"),
                                                     id("combat-window:invulnerability"),
                                                     id("combat:invulnerability-window"),
                                                     eve::Duration::zero(),
                                                     {{"targetIndex", eve::Value(std::int64_t{0})}}};
    auto                             overlappingInvulnerability = invulnerability;
    overlappingInvulnerability.itemId                           = id("combat-window:invulnerability-overlap");
    eve::action::ActionNotifyContext context;
    context.executionId = execution;
    context.source      = sourceHandle;
    context.targets.push_back(targetHandle);
    CHECK(!registry.value().dispatch(hitbox, context).ok());

    windows.setEnabled(true);
    eve::action::ActionBlockRuntime blockRuntime(registry.value());
    eve::action::ActionAdvance      advance;
    advance.id             = execution;
    advance.phase          = eve::action::ActionPhase::Active;
    advance.timelineEvents = {hitbox, invulnerability, overlappingInvulnerability};
    advance.activeBlocks   = {active(hitbox), active(invulnerability), active(overlappingInvulnerability)};
    REQUIRE(blockRuntime.apply(advance, context).ok());
    CHECK_EQ(windows.activeCount(), 3U);
    REQUIRE_EQ(windows.activeHitboxes(sourceSubject).size(), 1U);
    CHECK_EQ(windows.activeHitboxes(sourceSubject).front(), std::string("weapon.main"));
    CHECK(windows.isInvulnerable(targetSubject));
    auto firstInvulnerabilityExit = invulnerability;
    firstInvulnerabilityExit.kind = eve::action::ActionTimelineEventKind::StateExit;
    REQUIRE(registry.value().dispatch(firstInvulnerabilityExit, context).ok());
    CHECK_EQ(windows.activeCount(), 2U);
    CHECK(windows.isInvulnerable(targetSubject));

    eve::combat::CombatState            source{sourceSubject, 80.0, 80.0, 20.0, 20.0};
    eve::combat::CombatState            target{targetSubject, 100.0, 100.0, 40.0, 40.0};
    eve::combat::CombatActionDamageSink damage(
        [&](ecs::EntityHandle handle) -> eve::OptionalRef<eve::combat::CombatState> {
            if (handle.id == sourceHandle.id) return std::ref(source);
            if (handle.id == targetHandle.id) return std::ref(target);
            return std::nullopt;
        });
    damage.setWindowState(windows);
    damage.setEnabled(true);
    eve::action::ActionTimelineEvent damageEvent{
        eve::action::ActionTimelineEventKind::Notify,
        id("combat-track:damage"),
        id("combat-notify:damage"),
        id("combat:damage"),
        eve::Duration::zero(),
        {{"damageType", eve::Value("Damage.Physical.Slash")}, {"amount", eve::Value(25.0)}}};
    auto blocked = registry.value().dispatch(damageEvent, context);
    REQUIRE(blocked.ok());
    CHECK_EQ(blocked.status().code(), eve::StatusCode::NoOp);
    CHECK_EQ(target.health, 100.0);
    CHECK(!damage.lastOutcome().has_value());

    resolverAlive = false;
    REQUIRE(blockRuntime.interrupt(context).ok());
    CHECK_EQ(windows.activeCount(), 0U);
    CHECK(!windows.isInvulnerable(targetSubject));
    resolverAlive = true;
    REQUIRE(registry.value().dispatch(damageEvent, context).ok());
    CHECK_EQ(target.health, 75.0);

    damage.clearWindowState();
    damage.setEnabled(false);
    windows.setEnabled(false);
}

TEST_CASE("combatActionWindows.armAndDisarmMeleeHitboxRuntime") {
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    const eve::action::ActionExecutionId execution(77);
    ecs::EntityHandle                    sourceHandle{nullptr, typeid(void), 3, 1};
    const auto                           sourceSubject = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    eve::combat::CombatActionWindowState windows(
        [&](ecs::EntityHandle) { return eve::Result<eve::SubjectRef>::success(sourceSubject); });
    windows.setEnabled(true);

    class FixedPose final : public eve::combat::IMeleePoseSource {
    public:
        [[nodiscard]] eve::Result<eve::combat::MeleePose> pose(eve::SubjectRef, std::string_view) const override {
            return eve::Result<eve::combat::MeleePose>::success({{0.0, 1.0, 0.0}, 0.0});
        }
    } poses;
    eve::combat::MeleeHitRuntime melee;
    melee.setPoseSource(poses);
    REQUIRE(melee
                .registerHitbox({"weapon.main",
                                 {eve::combat::MeleeShapeKind::Sphere, 0.4, 0.0},
                                 {},
                                 10.0,
                                 0.0,
                                 "Damage.Physical.Slash"})
                .ok());
    windows.setMeleeHitRuntime(melee);

    eve::action::ActionNotifyContext context;
    context.executionId = execution;
    context.source      = sourceHandle;
    eve::action::ActionTimelineEvent enter{eve::action::ActionTimelineEventKind::StateEnter,
                                           id("combat-track:windows"),
                                           id("combat-window:hitbox"),
                                           id("combat:hitbox-window"),
                                           eve::Duration::zero(),
                                           {{"hitbox", eve::Value("weapon.main")}}};
    REQUIRE(registry.value().dispatch(enter, context).ok());
    CHECK_EQ(melee.armedCount(), 1u);

    eve::action::ActionTimelineEvent exit = enter;
    exit.kind                             = eve::action::ActionTimelineEventKind::StateExit;
    REQUIRE(registry.value().dispatch(exit, context).ok());
    CHECK_EQ(melee.armedCount(), 0u);
    windows.clearMeleeHitRuntime();
    windows.setEnabled(false);
}
}  // namespace test_combat_action_windows
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_arena_foundation)
#include "PrivateTestScope.h"
#include "action/AbilityController.h"
#include "action/input/ActionCancelWindowState.h"
#include "action/input/ActionInputBuffer.h"
#include "combat/BodyPartDamage.h"
#include "combat/CombatCharacter.h"
#include "combat/CombatEnemyAI.h"
#include "combat/CombatTarget.h"
#include "combat/ComboGraph.h"
#include "combat/Damage.h"
#include "combat/HitFeel.h"
#include "combat/MeleeHit.h"
#include "combat/StandardAbilities.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_arena_foundation.cpp
namespace test_combat_arena_foundation {
namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

class ArenaPose final : public eve::combat::IMeleePoseSource {
public:
    eve::combat::MeleePose                            poseValue{{0.5, 1.0, 0.0}, 0.0};
    [[nodiscard]] eve::Result<eve::combat::MeleePose> pose(eve::SubjectRef, std::string_view) const override {
        return eve::Result<eve::combat::MeleePose>::success(poseValue);
    }
};

}  // namespace

TEST_CASE("combatArenaFoundation.composesSlicesABCDEF") {
    const auto player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto enemy  = subject("01020304-0506-0708-890a-0b0c0d0e0f10");

    eve::combat::CombatCharacterRuntime characters;
    REQUIRE(characters.registerSubject({player, "fighter:player", {0.0, 0.0, 0.0}}).ok());
    REQUIRE(characters.registerSubject({enemy, "fighter:enemy", {1.2, 0.0, 0.0}}).ok());
    REQUIRE(characters.dodge(player).ok());
    CHECK(characters.state(player).value().invulnerable);

    ArenaPose                    poses;
    eve::combat::MeleeHitRuntime melee;
    melee.setPoseSource(poses);
    REQUIRE(melee
                .registerHitbox({"weapon.main",
                                 {eve::combat::MeleeShapeKind::Sphere, 0.5, 0.0},
                                 {},
                                 10.0,
                                 5.0,
                                 "Damage.Physical.Slash"})
                .ok());
    REQUIRE(melee.registerHurtbox({enemy, "torso", "torso", {eve::combat::MeleeShapeKind::Sphere, 0.4, 0.0}}).ok());
    REQUIRE(melee.setHurtboxPose(enemy, "torso", {{1.0, 1.0, 0.0}, 0.0}).ok());
    REQUIRE(melee.armHitbox(player, "weapon.main", eve::action::ActionExecutionId(1)).ok());
    auto hits = melee.advance(eve::SimulationTick(1));
    REQUIRE(hits.ok());
    REQUIRE_EQ(hits.value().hits.size(), 1u);

    std::map<std::string, eve::combat::CombatState, std::less<>> states;
    states.emplace(enemy.format(), eve::combat::CombatState{enemy, 100.0, 100.0, 30.0, 30.0});
    eve::combat::BodyPartDamageRule parts;
    REQUIRE(parts.setMultiplier("torso", 1.0).ok());
    eve::combat::DamageRuntime damage(&parts);
    auto                       outcomes = melee.applyHits(damage, states, hits.value().hits, {{"torso", 1.0}});
    REQUIRE(outcomes.ok());
    eve::combat::HitFeelRuntime feel;
    REQUIRE(feel.applyFromOutcome(outcomes.value().front()).ok());

    eve::action::input::ActionInputBuffer buffer(4, 5);
    REQUIRE(buffer.push({player, "heavy-attack", eve::SimulationTick(1), 1}).ok());
    auto light = eve::LogicalId::fromParts("combat-ability", "light-attack");
    auto heavy = eve::LogicalId::fromParts("combat-ability", "heavy-attack");
    REQUIRE(light.has_value());
    REQUIRE(heavy.has_value());
    eve::combat::ComboGraph graph;
    REQUIRE(graph.addEdge({*light, *heavy, "heavy-attack", 1, true, false}).ok());
    REQUIRE(graph.match(*light, "heavy-attack", true, false).ok());

    eve::combat::CombatTargetRuntime targets;
    REQUIRE(targets.registerOwner(player).ok());
    REQUIRE(targets.registerOwner(enemy).ok());
    REQUIRE(targets.hardLock(enemy, player).ok());
    REQUIRE(targets.softLock(player, {{enemy, 1.0, 1.2, 0.0, 0.0}}).ok());

    auto catalog = eve::combat::standardCombatAbilities();
    REQUIRE(catalog.ok());
    const auto lightDef = std::find_if(catalog.value().begin(), catalog.value().end(), [](const auto& definition) {
        return definition.id.format() == "combat-ability:light-attack";
    });
    REQUIRE(lightDef != catalog.value().end());
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime abilities(actions);
    REQUIRE(abilities.registerDefinition(*lightDef).ok());
    auto grant = abilities.grant("fighter:enemy", lightDef->id);
    REQUIRE(grant.ok());
    eve::combat::CombatEnemyIntentSource ai;
    ai.setTargetRuntime(targets);
    REQUIRE(ai.registerEnemy({enemy, "fighter:enemy", grant.value()}).ok());
    REQUIRE(ai.setLightAction(enemy, lightDef->action.id).ok());
    REQUIRE(ai.setPosition(enemy, 1.2, 0.0, 0.0).ok());
    REQUIRE(ai.setPosition(player, 0.0, 0.0, 0.0).ok());
    auto intent = ai.nextIntent(eve::SimulationTick(2));
    REQUIRE(intent.ok());
    REQUIRE(intent.value().has_value());

    CHECK(states.at(enemy.format()).health < 100.0);
    CHECK(feel.activeCount() >= 1u);
}
}  // namespace test_combat_arena_foundation
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_cancel_resolver)
#include "PrivateTestScope.h"
#include "action/input/ActionCancelWindowState.h"
#include "action/input/ActionComboWindowState.h"
#include "action/input/ActionInputBuffer.h"
#include "combat/CombatCancelResolver.h"
#include "combat/ComboGraph.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_cancel_resolver.cpp
namespace test_combat_cancel_resolver {
namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

}  // namespace

TEST_CASE("combatCancelResolver.consumesBufferAgainstCancelAndComboGraph") {
    const auto        player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    ecs::EntityHandle handle{nullptr, typeid(void), 1, 1};

    eve::action::input::ActionCancelWindowState cancels(
        [&](ecs::EntityHandle) { return eve::Result<eve::SubjectRef>::success(player); });
    cancels.setEnabled(true);
    eve::action::input::ActionComboWindowState combos(
        [&](ecs::EntityHandle) { return eve::Result<eve::SubjectRef>::success(player); });
    combos.setEnabled(true);

    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId(3);
    context.source      = handle;
    eve::action::ActionTimelineEvent cancelEnter{
        eve::action::ActionTimelineEventKind::StateEnter,
        id("track:input"),
        id("item:cancel"),
        id("input:cancel-window"),
        eve::Duration::zero(),
        {{"allows", eve::Value("heavy-attack")}, {"priority", eve::Value(std::int64_t{5})}}};
    REQUIRE(
        cancels
            .enter({eve::action::ActionStateWindowKind::Cancel, "heavy-attack", std::nullopt, 5}, cancelEnter, context)
            .ok());
    eve::action::ActionTimelineEvent comboEnter{eve::action::ActionTimelineEventKind::StateEnter,
                                                id("track:input"),
                                                id("item:combo"),
                                                id("input:combo-window"),
                                                eve::Duration::zero(),
                                                {{"input", eve::Value("heavy-attack")}}};
    REQUIRE(
        combos.enter({eve::action::ActionStateWindowKind::Combo, "heavy-attack", std::nullopt, 0}, comboEnter, context)
            .ok());

    eve::action::input::ActionInputBuffer buffer(4, 8);
    REQUIRE(buffer.push({player, "heavy-attack", eve::SimulationTick(2), 1}).ok());

    auto light = eve::LogicalId::fromParts("combat-ability", "light-attack");
    auto heavy = eve::LogicalId::fromParts("combat-ability", "heavy-attack");
    REQUIRE(light.has_value());
    REQUIRE(heavy.has_value());
    eve::combat::ComboGraph graph;
    REQUIRE(graph.addEdge({*light, *heavy, "heavy-attack", 1, true, true}).ok());

    eve::combat::CombatCancelResolver resolver;
    resolver.setBuffer(buffer);
    resolver.setCancels(cancels);
    resolver.setCombos(&combos);
    resolver.setGraph(&graph);

    auto resolved = resolver.resolve({player, *light, eve::SimulationTick(2)});
    REQUIRE(resolved.ok());
    CHECK_EQ(resolved.value().consumed.input, std::string("heavy-attack"));
    REQUIRE(resolved.value().graph.has_value());
    CHECK_EQ(resolved.value().graph->to.format(), heavy->format());
    REQUIRE(resolved.value().comboWindow.has_value());
    CHECK_EQ(buffer.size(), 0u);
}
}  // namespace test_combat_cancel_resolver
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_damage)
#include "PrivateTestScope.h"
#include "combat/Damage.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_damage.cpp
namespace test_combat_damage {
namespace {

eve::SubjectRef subject(const char* uuid) {
    auto id = eve::PersistentId::parse(uuid);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::combat::CombatState state() { return {subject("01020304-0506-0708-890a-0b0c0d0e0f10"), 100.0, 100.0, 50.0, 50.0}; }

eve::combat::DamageRequest requestFor(const eve::combat::CombatState& target) {
    eve::combat::DamageRequest request;
    request.source       = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    request.target       = target.subject;
    request.damageType   = "Damage.Physical.Slash";
    request.healthDamage = 10.0;
    request.poiseDamage  = 5.0;
    request.knockback    = {2.0, 0.0, -1.0};
    return request;
}

class ArmorRule final : public eve::combat::IDamageRule {
public:
    eve::Result<eve::combat::DamageAmounts> evaluate(const eve::combat::DamageRequest& request,
                                                     const eve::combat::CombatState&) const override {
        ++calls;
        return eve::Result<eve::combat::DamageAmounts>::success(
            {request.healthDamage * 0.5, request.poiseDamage * 2.0, 0.25});
    }

    mutable int calls = 0;
};

class InvalidRule final : public eve::combat::IDamageRule {
public:
    eve::Result<eve::combat::DamageAmounts> evaluate(const eve::combat::DamageRequest&,
                                                     const eve::combat::CombatState&) const override {
        return eve::Result<eve::combat::DamageAmounts>::success({-1.0, 0.0, 1.0});
    }
};

}  // namespace

TEST_CASE("combatDamage.defaultRuleCommitsHealthPoiseReactionAndKnockback") {
    auto                       target  = state();
    auto                       request = requestFor(target);
    eve::combat::DamageRuntime damage;
    auto                       outcome = damage.apply(target, request);
    REQUIRE(outcome.ok());
    CHECK_EQ(target.health, 90.0);
    CHECK_EQ(target.poise, 45.0);
    CHECK_EQ(outcome.value().appliedHealthDamage, 10.0);
    CHECK_EQ(outcome.value().knockback.x, 2.0);
    CHECK(static_cast<int>(outcome.value().reaction) == static_cast<int>(eve::combat::HitReaction::Flinch));
    CHECK(static_cast<int>(outcome.value().ruleSource) == static_cast<int>(eve::combat::DamageRuleSource::Default));
}

TEST_CASE("combatDamage.providerPathIsObservableAndPreparedBeforeCommit") {
    auto                       target  = state();
    auto                       request = requestFor(target);
    ArmorRule                  armor;
    eve::combat::DamageRuntime damage(&armor);
    auto                       outcome = damage.apply(target, request);
    REQUIRE(outcome.ok());
    CHECK(armor.calls == 1);
    CHECK_EQ(target.health, 95.0);
    CHECK_EQ(target.poise, 40.0);
    CHECK_EQ(outcome.value().knockback.x, 0.5);
    CHECK(static_cast<int>(outcome.value().ruleSource) == static_cast<int>(eve::combat::DamageRuleSource::Provider));

    InvalidRule                invalid;
    eve::combat::DamageRuntime rejected(&invalid);
    const auto                 before = target;
    CHECK(!rejected.apply(target, request).ok());
    CHECK_EQ(target.health, before.health);
    CHECK_EQ(target.poise, before.poise);
}

TEST_CASE("combatDamage.settlementRulesDriveTheRuntimeUsedByRts") {
    eve::settlement::SettlementRule vulnerability;
    vulnerability.id                  = "rts.burning-oil";
    vulnerability.source              = "effect:burning-oil";
    vulnerability.stage               = eve::settlement::StageKind::SourceModifiers;
    vulnerability.operation           = eve::settlement::RuleOperation::Multiply;
    vulnerability.value               = 1.5;
    vulnerability.filter.kinds        = {"damage"};
    vulnerability.filter.requiredTags = {"Damage.Physical.Slash"};

    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({vulnerability}).ok());
    eve::combat::DamageRuntime damage;
    REQUIRE(damage.configureSettlementRules(rules).ok());

    auto target  = state();
    auto request = requestFor(target);
    auto preview = damage.previewSettlement(target, request);
    REQUIRE(preview.ok());
    CHECK_EQ(preview.value().appliedHealthDamage, 15.0);
    CHECK_EQ(target.health, 100.0);
    auto outcome = damage.apply(target, request);
    REQUIRE(outcome.ok());
    CHECK_EQ(outcome.value().appliedHealthDamage, 15.0);
    CHECK_EQ(target.health, 85.0);
    CHECK_EQ(target.poise, 45.0);
}

TEST_CASE("combatDamage.shieldIsResolvedInsideSharedSettlement") {
    eve::combat::CombatState target;
    target.subject   = subject("00000000-0000-0000-0000-000000000002");
    target.maxHealth = 100.0;
    target.health    = 100.0;
    target.maxPoise  = 10.0;
    target.poise     = 10.0;
    eve::combat::DamageRequest request;
    request.source          = subject("00000000-0000-0000-0000-000000000001");
    request.target          = target.subject;
    request.damageType      = "damage.energy";
    request.healthDamage    = 30.0;
    request.availableShield = 12.0;
    eve::combat::DamageRuntime damage;
    auto                       outcome = damage.apply(target, request);
    REQUIRE(outcome.ok());
    CHECK_EQ(outcome.value().absorbedShieldDamage, 12.0);
    CHECK_EQ(outcome.value().appliedHealthDamage, 18.0);
    CHECK_EQ(target.health, 82.0);
    REQUIRE_EQ(outcome.value().settlementResult.derived.size(), 1u);
    CHECK_EQ(outcome.value().settlementResult.derived[0].trigger, std::string("shield_break"));
    CHECK_EQ(outcome.value().settlementResult.derived[0].magnitude, 0.0);
}

TEST_CASE("combatDamage.standardTransitionsAreStableAndEmittedOnce") {
    eve::combat::DamageRuntime damage;
    auto                       target  = state();
    auto                       request = requestFor(target);
    request.healthDamage               = 200.0;
    request.availableShield            = 25.0;

    auto preview = damage.previewSettlement(target, request);
    REQUIRE(preview.ok());
    CHECK_EQ(target.health, 100.0);
    REQUIRE_EQ(preview.value().settlementResult.derived.size(), 3u);
    CHECK_EQ(preview.value().settlementResult.derived[0].trigger, std::string("shield_break"));
    CHECK_EQ(preview.value().settlementResult.derived[1].trigger, std::string("death"));
    CHECK_EQ(preview.value().settlementResult.derived[2].trigger, std::string("kill"));

    auto committed = damage.apply(target, request);
    REQUIRE(committed.ok());
    CHECK_EQ(target.health, 0.0);
    REQUIRE_EQ(committed.value().settlementResult.derived.size(), 3u);
    for (std::size_t index = 0; index < 3; ++index)
        CHECK_EQ(committed.value().settlementResult.derived[index].trigger,
                 preview.value().settlementResult.derived[index].trigger);

    request.availableShield = 0.0;
    auto alreadyDead        = damage.apply(target, request);
    REQUIRE(alreadyDead.ok());
    CHECK(alreadyDead.value().settlementResult.derived.empty());
}

TEST_CASE("combatDamage.previewUsesCanonicalRuleWithoutMutatingTarget") {
    auto                       target  = state();
    auto                       request = requestFor(target);
    ArmorRule                  armor;
    eve::combat::DamageRuntime damage(&armor);

    auto preview = damage.preview(target, request);
    REQUIRE(preview.ok());
    CHECK_EQ(armor.calls, 1);
    CHECK_EQ(preview.value().healthDamage, 5.0);
    CHECK_EQ(preview.value().poiseDamage, 10.0);
    CHECK_EQ(preview.value().knockbackScale, 0.25);
    CHECK_EQ(target.health, 100.0);
    CHECK_EQ(target.poise, 50.0);

    auto outcome = damage.apply(target, request);
    REQUIRE(outcome.ok());
    CHECK_EQ(armor.calls, 2);
    CHECK_EQ(outcome.value().appliedHealthDamage, preview.value().healthDamage);
}

TEST_CASE("combatDamage.reactionPriorityIsDeathKnockdownStaggerFlinch") {
    eve::combat::HitReactionPolicy policy;
    policy.flinchDamageThreshold = 1.0;
    policy.staggerPoiseFraction  = 0.5;
    eve::combat::DamageRuntime damage(nullptr, policy);

    auto staggered              = state();
    auto staggerRequest         = requestFor(staggered);
    staggerRequest.healthDamage = 1.0;
    staggerRequest.poiseDamage  = 25.0;
    auto stagger                = damage.apply(staggered, staggerRequest);
    REQUIRE(stagger.ok());
    CHECK(static_cast<int>(stagger.value().reaction) == static_cast<int>(eve::combat::HitReaction::Stagger));

    auto knocked              = state();
    auto knockRequest         = requestFor(knocked);
    knockRequest.healthDamage = 1.0;
    knockRequest.poiseDamage  = 100.0;
    auto knockdown            = damage.apply(knocked, knockRequest);
    REQUIRE(knockdown.ok());
    CHECK(static_cast<int>(knockdown.value().reaction) == static_cast<int>(eve::combat::HitReaction::Knockdown));

    auto killed         = state();
    auto lethal         = requestFor(killed);
    lethal.healthDamage = 200.0;
    lethal.poiseDamage  = 100.0;
    auto death          = damage.apply(killed, lethal);
    REQUIRE(death.ok());
    CHECK(static_cast<int>(death.value().reaction) == static_cast<int>(eve::combat::HitReaction::Death));
}

TEST_CASE("combatDamage.poiseRecoveryIsExplicitCheckedAndClamped") {
    auto target  = state();
    target.poise = 10.0;
    eve::combat::DamageRuntime damage;
    auto                       recovered = damage.recoverPoise(target, 15.0);
    REQUIRE(recovered.ok());
    CHECK_EQ(recovered.value(), 25.0);
    recovered = damage.recoverPoise(target, 100.0);
    REQUIRE(recovered.ok());
    CHECK_EQ(recovered.value(), 50.0);
    CHECK(!damage.recoverPoise(target, -1.0).ok());
    CHECK_EQ(target.poise, 50.0);
}

TEST_CASE("combatDamage.rejectsMismatchedTargetAndInvalidPayloadAtomically") {
    auto target    = state();
    auto request   = requestFor(target);
    request.target = subject("21222324-2526-2728-a92a-2b2c2d2e2f30");
    eve::combat::DamageRuntime damage;
    CHECK(!damage.apply(target, request).ok());
    CHECK_EQ(target.health, 100.0);
    request.target     = target.subject;
    request.damageType = "Damage..Invalid";
    CHECK(!damage.apply(target, request).ok());
    CHECK_EQ(target.health, 100.0);
}
}  // namespace test_combat_damage
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_feel_cancel_guard)
#include "PrivateTestScope.h"
#include "action/ActionNotifyRegistry.h"
#include "action/input/ActionCancelWindowState.h"
#include "action/input/ActionInputBuffer.h"
#include "combat/ComboGraph.h"
#include "combat/GuardWindowState.h"
#include "combat/HitFeel.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_feel_cancel_guard.cpp
namespace test_combat_feel_cancel_guard {
namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

}  // namespace

TEST_CASE("combatFeelCancelGuardAndComboGraphCompose") {
    const auto        player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto        enemy  = subject("01020304-0506-0708-890a-0b0c0d0e0f10");
    ecs::EntityHandle playerHandle{nullptr, typeid(void), 1, 1};

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::input::ActionCancelWindowState cancels(
        [&](ecs::EntityHandle) { return eve::Result<eve::SubjectRef>::success(player); });
    cancels.setEnabled(true);
    eve::combat::GuardWindowState guards(
        [&](ecs::EntityHandle) { return eve::Result<eve::SubjectRef>::success(player); });
    guards.setEnabled(true);

    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId(7);
    context.source      = playerHandle;
    eve::action::ActionTimelineEvent cancelEnter{
        eve::action::ActionTimelineEventKind::StateEnter,
        id("track:input"),
        id("item:cancel"),
        id("input:cancel-window"),
        eve::Duration::zero(),
        {{"allows", eve::Value("dodge;heavy-attack")}, {"priority", eve::Value(std::int64_t{10})}}};
    REQUIRE(registry.value().dispatch(cancelEnter, context).ok());
    auto cancelMatch = cancels.match(player, "dodge");
    REQUIRE(cancelMatch.ok());
    CHECK_EQ(cancelMatch.value().priority, 10);

    eve::action::input::ActionInputBuffer buffer(4, 8);
    REQUIRE(buffer.push({player, "dodge", eve::SimulationTick(1), 5}).ok());
    auto consumed = buffer.consume(player, {"dodge"});
    REQUIRE(consumed.ok());
    CHECK_EQ(consumed.value().input, std::string("dodge"));

    eve::action::ActionTimelineEvent guardEnter{eve::action::ActionTimelineEventKind::StateEnter,
                                                id("track:guard"),
                                                id("item:parry"),
                                                id("combat:guard-window"),
                                                eve::Duration::zero(),
                                                {{"mode", eve::Value("parry")}}};
    REQUIRE(registry.value().dispatch(guardEnter, context).ok());
    eve::combat::DamageRequest request;
    request.source       = enemy;
    request.target       = player;
    request.damageType   = "Damage.Physical.Slash";
    request.healthDamage = 20.0;
    request.poiseDamage  = 10.0;
    auto guarded         = guards.mitigate(player, request);
    REQUIRE(guarded.ok());
    CHECK(guarded.value().result == eve::combat::GuardResult::Parried);
    CHECK_EQ(request.healthDamage, 0.0);

    eve::combat::CombatState   victim{player, 100.0, 100.0, 40.0, 40.0};
    eve::combat::DamageRuntime damage;
    request.healthDamage = 12.0;
    request.poiseDamage  = 20.0;
    auto outcome         = damage.apply(victim, request);
    REQUIRE(outcome.ok());
    eve::combat::HitFeelRuntime feel;
    REQUIRE(feel.applyFromOutcome(outcome.value()).ok());
    CHECK(feel.activeCount() >= 1u);

    auto light = eve::LogicalId::fromParts("combat-ability", "light-attack");
    auto heavy = eve::LogicalId::fromParts("combat-ability", "heavy-attack");
    REQUIRE(light.has_value());
    REQUIRE(heavy.has_value());
    eve::combat::ComboGraph graph;
    REQUIRE(graph.addEdge({*light, *heavy, "heavy-attack", 1, true, false}).ok());
    auto matched = graph.match(*light, "heavy-attack", true, false);
    REQUIRE(matched.ok());
    CHECK_EQ(matched.value().to.format(), heavy->format());
    CHECK(!graph.match(*light, "heavy-attack", false, false).ok());

    cancels.setEnabled(false);
    guards.setEnabled(false);
}
}  // namespace test_combat_feel_cancel_guard
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_framework_composition)
#include "PrivateTestScope.h"
#include <algorithm>
#include "action/AbilityController.h"
#include "combat/CombatAttributes.h"
#include "combat/Damage.h"
#include "combat/StandardAbilities.h"
#include "effects/EffectContainer.h"
#include "tags/GameplayTag.h"
#include "weapon/ProjectileRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_framework_composition.cpp
namespace test_combat_framework_composition {
namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

}  // namespace

TEST_CASE("combatFramework.composesTagsPlayerAbilityBuffDamageAttributesAndProjectile") {
    eve::tags::GameplayTagRegistry tags;
    REQUIRE(tags.registerTag("Ability.Combat.Attack.Light").ok());
    REQUIRE(tags.registerTag("Effect.Buff.Attack").ok());
    REQUIRE(tags.registerTag("Attribute.Vital.Stamina").ok());

    auto catalog = eve::combat::standardCombatAbilities();
    REQUIRE(catalog.ok());
    const auto light = std::find_if(catalog.value().begin(), catalog.value().end(), [](const auto& definition) {
        return definition.id.format() == "combat-ability:light-attack";
    });
    REQUIRE(light != catalog.value().end());

    eve::action::ActionRuntime            actions;
    eve::action::AbilityRuntime           abilities(actions);
    eve::action::AbilityControllerRuntime controller(abilities);
    REQUIRE(abilities.registerDefinition(*light).ok());
    auto grant = abilities.grant("fighter:player", light->id);
    REQUIRE(grant.ok());
    eve::action::PlayerAbilityIntentQueue player;
    eve::action::AbilityIntent            intent;
    intent.grantId          = grant.value();
    intent.request.actionId = light->action.id;
    REQUIRE(player.enqueue(std::move(intent)).ok());
    auto activated = controller.processNext(player, eve::SimulationTick(10));
    REQUIRE(activated.ok());
    REQUIRE(activated.value().has_value());

    eve::effects::EffectContainer effects;
    auto                          buff = effects.apply("fighter:player", "buff.attack", "combat-demo", 1, 5.0, "attack",
                                                       eve::effects::StackPolicy::Refresh);
    REQUIRE(buff.ok());
    REQUIRE(effects.find(buff.value()) != nullptr);
    REQUIRE(effects.find(buff.value())->addTag("Effect.Buff.Attack").ok());

    eve::combat::CombatAttributeRuntime attributes("fighter:player");
    REQUIRE(attributes.registerAttribute({"Attribute.Vital.Stamina", 100.0, 0.0, 100.0, 10.0}).ok());
    auto spent = attributes.modify("Attribute.Vital.Stamina", -20.0);
    REQUIRE(spent.ok());
    REQUIRE_EQ(spent.value().size(), 1u);

    eve::combat::CombatState   target{subject("01020304-0506-0708-890a-0b0c0d0e0f10"), 100.0, 100.0, 40.0, 40.0};
    eve::combat::DamageRequest damageRequest;
    damageRequest.source          = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    damageRequest.target          = target.subject;
    damageRequest.actionExecution = activated.value()->executionId;
    damageRequest.damageType      = "Damage.Physical.Slash";
    damageRequest.healthDamage    = 12.0;
    damageRequest.poiseDamage     = 8.0;
    eve::combat::DamageRuntime damage;
    auto                       outcome = damage.apply(target, damageRequest);
    REQUIRE(outcome.ok());
    CHECK_EQ(target.health, 88.0);

    eve::weapon::ProjectileDefinition projectile;
    auto projectileId = eve::LogicalId::fromParts("combat-projectile", "light-attack-tracer");
    REQUIRE(projectileId.has_value());
    projectile.id       = *projectileId;
    projectile.mode     = eve::weapon::ProjectileMode::Linear;
    projectile.speed    = 20.0;
    projectile.lifetime = eve::Duration::fromNanoseconds(1000000000);
    eve::weapon::ProjectileRuntime projectiles;
    auto                           spawned = projectiles.spawn(projectile, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    REQUIRE(projectiles.update(eve::Duration::fromNanoseconds(100000000)).ok());
    REQUIRE(projectiles.find(spawned.value()).has_value());
    CHECK_EQ(projectiles.find(spawned.value())->position.x, 2.0);

    REQUIRE(attributes.advance(eve::Duration::fromNanoseconds(1000000000)).ok());
    CHECK_EQ(attributes.value("Attribute.Vital.Stamina").value(), 90.0);
    REQUIRE(effects.advance({eve::SimulationTick(11), eve::Duration::fromNanoseconds(1000000000)}).ok());
}
}  // namespace test_combat_framework_composition
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_loop)
#include "PrivateTestScope.h"
#include <cmath>
#include <map>
#include <string>
#include "combat/CombatCamera.h"
#include "combat/CombatCharacterPose.h"
#include "combat/CombatLoop.h"
#include "combat/CombatTarget.h"
#include "combat/HitFeel.h"
#include "combat/MeleeHit.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_loop.cpp
namespace test_combat_loop {
namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

}  // namespace

TEST_CASE("combatLoop.requiresCharacterAndMelee") {
    eve::combat::CombatLoopRuntime loop;
    auto missing = loop.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(16000000)});
    CHECK(!missing.ok());
}

TEST_CASE("combatLoop.warpsAttackerTowardLockAndFramesCamera") {
    const auto player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto enemy  = subject("01020304-0506-0708-890a-0b0c0d0e0f10");

    eve::combat::CombatCharacterRuntime characters;
    REQUIRE(characters.registerSubject({player, "fighter:player", {0.0, 0.0, 0.0}}).ok());
    REQUIRE(characters.registerSubject({enemy, "fighter:enemy", {4.0, 0.0, 0.0}}).ok());
    REQUIRE(characters.beginAttack(player).ok());

    eve::combat::CombatTargetRuntime targets;
    REQUIRE(targets.registerOwner(player).ok());
    REQUIRE(targets.hardLock(player, enemy).ok());

    eve::combat::MeleeHitRuntime   melee;
    eve::combat::CombatLoopRuntime loop;
    loop.setCharacters(characters);
    loop.setMelee(melee);
    loop.setTargets(&targets);
    loop.setCameraFocus(player);

    auto frame = loop.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(16000000)});
    REQUIRE(frame.ok());
    auto state = characters.state(player);
    REQUIRE(state.ok());
    CHECK(std::abs(state.value().position.x - 0.12) < 1e-9);
    CHECK(state.value().facing.x > 0.9);
    CHECK(std::abs(frame.value().camera.eye.x + 5.88) < 1e-6);
    CHECK(frame.value().camera.lookAt.x > 1.0);
}

TEST_CASE("combatLoop.appliesMeleeDamageFeelAndHitstopFreeze") {
    const auto player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto enemy  = subject("01020304-0506-0708-890a-0b0c0d0e0f10");

    eve::combat::CombatCharacterRuntime characters;
    REQUIRE(characters.registerSubject({player, "fighter:player", {0.0, 0.0, 0.0}}).ok());
    REQUIRE(characters.registerSubject({enemy, "fighter:enemy", {1.4, 0.0, 0.0}}).ok());

    eve::combat::CombatCharacterPoseSource poses(characters);
    eve::combat::MeleeHitRuntime           melee;
    melee.setPoseSource(poses);
    REQUIRE(melee
                .registerHitbox({"weapon.main",
                                 {eve::combat::MeleeShapeKind::Sphere, 0.5, 0.0},
                                 {0.0, 0.0, 0.6},
                                 12.0,
                                 4.0,
                                 "Damage.Physical.Slash",
                                 3.0,
                                 2.0})
                .ok());
    REQUIRE(melee.registerHurtbox({enemy, "torso", "torso", {eve::combat::MeleeShapeKind::Sphere, 0.4, 0.0}}).ok());
    REQUIRE(melee.armHitbox(player, "weapon.main", eve::action::ActionExecutionId(9)).ok());

    std::map<std::string, eve::combat::CombatState, std::less<>> states;
    states.emplace(enemy.format(), eve::combat::CombatState{enemy, 100.0, 100.0, 40.0, 40.0});
    eve::combat::HitFeelRuntime feel;

    eve::combat::CombatLoopRuntime loop;
    loop.setCharacters(characters);
    loop.setMelee(melee);
    loop.setFeel(&feel);
    loop.setDamageStates(&states);
    loop.setCameraFocus(player);

    auto frame = loop.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(16000000)});
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().melee.hits.size(), 1u);
    REQUIRE_EQ(frame.value().outcomes.size(), 1u);
    CHECK_EQ(states.at(enemy.format()).health, 88.0);
    CHECK(feel.isFrozen(player));
    auto enemyState = characters.state(enemy);
    REQUIRE(enemyState.ok());
    CHECK(enemyState.value().mode == eve::combat::CombatCharacterMode::Stunned);
    CHECK(enemyState.value().velocity.x > 0.0);
    CHECK(enemyState.value().velocity.y > 0.0);

    REQUIRE(characters.setMoveIntent(player, {1.0, 0.0, 0.0}, 1.0).ok());
    const double frozenX = characters.state(player).value().position.x;
    REQUIRE(loop.advance({eve::SimulationTick(2), eve::Duration::fromNanoseconds(16000000)}).ok());
    CHECK_EQ(characters.state(player).value().position.x, frozenX);
}

TEST_CASE("combatLoop.recordsPerfectDodgeWithoutDamage") {
    const auto player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto enemy  = subject("01020304-0506-0708-890a-0b0c0d0e0f10");

    eve::combat::CombatCharacterRuntime characters;
    REQUIRE(characters.registerSubject({player, "fighter:player", {0.0, 0.0, 0.0}}).ok());
    REQUIRE(characters.registerSubject({enemy, "fighter:enemy", {1.4, 0.0, 0.0}}).ok());
    REQUIRE(characters.dodge(player, eve::combat::CombatVector3{0.0, 0.0, 1.0}).ok());

    eve::combat::CombatCharacterPoseSource poses(characters);
    eve::combat::MeleeHitRuntime           melee;
    melee.setPoseSource(poses);
    REQUIRE(melee
                .registerHitbox({"weapon.main",
                                 {eve::combat::MeleeShapeKind::Sphere, 0.5, 0.0},
                                 {0.0, 0.0, -0.6},
                                 12.0,
                                 4.0,
                                 "Damage.Physical.Slash"})
                .ok());
    REQUIRE(melee.registerHurtbox({player, "torso", "torso", {eve::combat::MeleeShapeKind::Sphere, 0.4, 0.0}}).ok());
    REQUIRE(melee.armHitbox(enemy, "weapon.main", eve::action::ActionExecutionId(11)).ok());

    std::map<std::string, eve::combat::CombatState, std::less<>> states;
    states.emplace(player.format(), eve::combat::CombatState{player, 100.0, 100.0, 40.0, 40.0});

    eve::combat::CombatLoopRuntime loop;
    loop.setCharacters(characters);
    loop.setMelee(melee);
    loop.setDamageStates(&states);

    auto frame = loop.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(16000000)});
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().melee.hits.size(), 1u);
    REQUIRE_EQ(frame.value().perfectDodges.size(), 1u);
    CHECK_EQ(frame.value().perfectDodges.front().defender.format(), player.format());
    CHECK_EQ(frame.value().outcomes.size(), 0u);
    CHECK_EQ(states.at(player.format()).health, 100.0);
    CHECK(characters.state(player).value().mode == eve::combat::CombatCharacterMode::Dodging);
}

TEST_CASE("combatCharacterPoseSource.samplesChestHeightAndYaw") {
    const auto                          player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    eve::combat::CombatCharacterRuntime characters;
    REQUIRE(characters.registerSubject({player, "fighter:player", {2.0, 0.5, -1.0}}).ok());
    REQUIRE(characters.setFacing(player, {0.0, 0.0, 1.0}).ok());
    eve::combat::CombatCharacterPoseSource poses(characters, 1.25);
    auto                                   pose = poses.pose(player, "weapon.main");
    REQUIRE(pose.ok());
    CHECK_EQ(pose.value().position.x, 2.0);
    CHECK_EQ(pose.value().position.y, 1.75);
    CHECK_EQ(pose.value().position.z, -1.0);
    CHECK(std::abs(pose.value().yawRadians) < 1e-9);
}
}  // namespace test_combat_loop
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_navigation)
#include "PrivateTestScope.h"
#include <cmath>
#include <utility>
#include "combat/navigation/PathfinderCombatNavigation.h"
#include "map/Pathfinder.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_navigation.cpp
namespace test_combat_navigation {
namespace {

eve::SubjectRef fighterSubject() {
    auto parsed = eve::PersistentId::parse("51525354-5556-5758-d95a-5b5c5d5e5f60");
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

std::unique_ptr<eve::combat::navigation::PathfinderCombatNavigationProvider> provider(
    eve::map::Pathfinder& pathfinder) {
    auto created = eve::combat::navigation::PathfinderCombatNavigationProvider::create(pathfinder, {});
    REQUIRE(created.ok());
    return std::move(created).takeValue();
}

eve::combat::CombatLocomotionState stateAt(double x, double z) {
    eve::combat::CombatLocomotionState state;
    state.subject  = fighterSubject();
    state.position = {x, z};
    return state;
}

}  // namespace

TEST_CASE("combatNavigation.pathfinderRoutesAroundBlockedCells") {
    eve::map::Pathfinder pathfinder(5, 3);
    pathfinder.setTopology("ortho4");
    pathfinder.setBlocked(2, 0, true);
    pathfinder.setBlocked(2, 1, true);
    auto navigation = provider(pathfinder);

    auto steering = navigation->steer(stateAt(1.5, 1.5), {{4.5, 1.5}, 0.1}, eve::SimulationTick{1});
    REQUIRE(steering.ok());
    CHECK(steering.value().phase == eve::combat::CombatNavigationPhase::Moving);
    CHECK_EQ(steering.value().direction.x, 0.0);
    CHECK(steering.value().direction.z > 0.99);
}

TEST_CASE("combatNavigation.locomotionFollowsDetourAndArrives") {
    eve::map::Pathfinder pathfinder(5, 3);
    pathfinder.setTopology("ortho4");
    pathfinder.setBlocked(2, 0, true);
    pathfinder.setBlocked(2, 1, true);
    auto                                 navigation = provider(pathfinder);
    eve::combat::CombatLocomotionRuntime locomotion(*navigation);
    const auto                           fighter = fighterSubject();
    REQUIRE(locomotion.registerSubject({fighter, "fighter:path", {1.5, 1.5}, 2.0, 100.0}).ok());
    REQUIRE(locomotion.navigateTo(fighter, {{4.5, 1.5}, 0.05}).ok());

    bool arrived = false;
    for (std::uint64_t tick = 1; tick <= 64 && !arrived; ++tick) {
        auto advanced = locomotion.advance({eve::SimulationTick{tick}, eve::Duration::fromNanoseconds(250'000'000)});
        REQUIRE(advanced.ok());
        auto current = locomotion.state(fighter);
        REQUIRE(current.ok());
        const int cellX = static_cast<int>(std::floor(current.value().position.x));
        const int cellY = static_cast<int>(std::floor(current.value().position.z));
        CHECK(pathfinder.isWalkable(cellX, cellY));
        arrived = !current.value().navigationGoal.has_value();
    }
    REQUIRE(arrived);
    auto current = locomotion.state(fighter);
    REQUIRE(current.ok());
    CHECK_EQ(current.value().position.x, 4.5);
    CHECK_EQ(current.value().position.z, 1.5);
}

TEST_CASE("combatNavigation.replansAfterCanonicalGridMutation") {
    eve::map::Pathfinder pathfinder(5, 3);
    pathfinder.setTopology("ortho4");
    auto       navigation = provider(pathfinder);
    const auto current    = stateAt(1.5, 1.5);

    auto direct = navigation->steer(current, {{4.5, 1.5}, 0.1}, eve::SimulationTick{1});
    REQUIRE(direct.ok());
    CHECK(direct.value().direction.x > 0.99);
    CHECK_EQ(direct.value().direction.z, 0.0);

    pathfinder.setBlocked(2, 1, true);
    auto replanned = navigation->steer(current, {{4.5, 1.5}, 0.1}, eve::SimulationTick{2});
    REQUIRE(replanned.ok());
    CHECK(replanned.value().direction.x < 0.01);
    CHECK(std::abs(replanned.value().direction.z) > 0.99);
}

TEST_CASE("combatNavigation.unreachableGoalAbortsLocomotionAtomically") {
    eve::map::Pathfinder pathfinder(5, 3);
    pathfinder.setTopology("ortho4");
    for (int y = 0; y < 3; ++y) pathfinder.setBlocked(2, y, true);
    auto                                 navigation = provider(pathfinder);
    eve::combat::CombatLocomotionRuntime locomotion(*navigation);
    const auto                           fighter = fighterSubject();
    REQUIRE(locomotion.registerSubject({fighter, "fighter:navigator", {1.5, 1.5}, 4.0, 8.0}).ok());
    REQUIRE(locomotion.navigateTo(fighter, {{4.5, 1.5}, 0.1}).ok());

    auto advanced = locomotion.advance({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(250'000'000)});
    REQUIRE(!advanced.ok());
    REQUIRE(advanced.status().diagnostics().front().code() == eve::DiagnosticCode::NotFound);
    auto current = locomotion.state(fighter);
    REQUIRE(current.ok());
    CHECK_EQ(current.value().position.x, 1.5);
    CHECK_EQ(current.value().position.z, 1.5);
    CHECK(current.value().navigationGoal.has_value());
}

TEST_CASE("combatNavigation.rejectsInvalidGridProjection") {
    eve::map::Pathfinder pathfinder(2, 2);
    auto invalid = eve::combat::navigation::PathfinderCombatNavigationProvider::create(pathfinder, {{}, 0.0});
    CHECK(!invalid.ok());
}
}  // namespace test_combat_navigation
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_target_enemy_ai)
#include "PrivateTestScope.h"
#include <algorithm>
#include "combat/BodyPartDamage.h"
#include "combat/CombatEnemyAI.h"
#include "combat/CombatTarget.h"
#include "combat/StandardAbilities.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_target_enemy_ai.cpp
namespace test_combat_target_enemy_ai {
namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

}  // namespace

TEST_CASE("combatTargetEnemyAiAndBodyPartDamage") {
    const auto player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto enemy  = subject("01020304-0506-0708-890a-0b0c0d0e0f10");

    eve::combat::CombatTargetRuntime targets;
    REQUIRE(targets.registerOwner(player).ok());
    REQUIRE(targets.registerOwner(enemy).ok());
    auto soft = targets.softLock(player, {{enemy, 2.0, 1.0, 0.0, 0.0}});
    REQUIRE(soft.ok());
    REQUIRE(soft.value().target.has_value());
    CHECK_EQ(soft.value().target->format(), enemy.format());
    REQUIRE(targets.hardLock(enemy, player).ok());

    auto catalog = eve::combat::standardCombatAbilities();
    REQUIRE(catalog.ok());
    const auto light = std::find_if(catalog.value().begin(), catalog.value().end(), [](const auto& definition) {
        return definition.id.format() == "combat-ability:light-attack";
    });
    REQUIRE(light != catalog.value().end());
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime abilities(actions);
    REQUIRE(abilities.registerDefinition(*light).ok());
    auto grant = abilities.grant("fighter:enemy", light->id);
    REQUIRE(grant.ok());

    eve::combat::CombatEnemyIntentSource ai;
    ai.setTargetRuntime(targets);
    REQUIRE(ai.registerEnemy({enemy, "fighter:enemy", grant.value(), 2.0, 5.0, 10}).ok());
    REQUIRE(ai.setLightAction(enemy, light->action.id).ok());
    REQUIRE(ai.setPosition(enemy, 0.0, 0.0, 0.0).ok());
    REQUIRE(ai.setPosition(player, 1.0, 0.0, 0.0).ok());
    CHECK(ai.band(enemy) == eve::combat::CombatEnemyBand::Near);
    auto intent = ai.nextIntent(eve::SimulationTick(1));
    REQUIRE(intent.ok());
    REQUIRE(intent.value().has_value());
    CHECK_EQ(intent.value()->grantId, grant.value());

    eve::combat::BodyPartDamageRule parts;
    REQUIRE(parts.setMultiplier("head", 2.0).ok());
    eve::combat::CombatState   victim{player, 100.0, 100.0, 20.0, 20.0};
    eve::combat::DamageRequest request;
    request.source       = enemy;
    request.target       = player;
    request.damageType   = "Damage.Physical.Slash";
    request.healthDamage = 10.0;
    auto amounts         = parts.evaluateForPart(request, victim, "head");
    REQUIRE(amounts.ok());
    CHECK_EQ(amounts.value().healthDamage, 20.0);
}

TEST_CASE("combatEnemyAi.telegraphThenRecoverArePunishable") {
    const auto player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto enemy  = subject("01020304-0506-0708-890a-0b0c0d0e0f10");

    eve::combat::CombatTargetRuntime targets;
    REQUIRE(targets.registerOwner(enemy).ok());
    REQUIRE(targets.hardLock(enemy, player).ok());

    auto catalog = eve::combat::standardCombatAbilities();
    REQUIRE(catalog.ok());
    const auto light = std::find_if(catalog.value().begin(), catalog.value().end(), [](const auto& definition) {
        return definition.id.format() == "combat-ability:light-attack";
    });
    REQUIRE(light != catalog.value().end());
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime abilities(actions);
    REQUIRE(abilities.registerDefinition(*light).ok());
    auto grant = abilities.grant("fighter:enemy", light->id);
    REQUIRE(grant.ok());

    eve::combat::CombatEnemyDefinition definition;
    definition.subject             = enemy;
    definition.ownerId             = "fighter:enemy";
    definition.lightGrant          = grant.value();
    definition.nearRadius          = 2.0;
    definition.midRadius           = 5.0;
    definition.attackCooldownTicks = 20;
    definition.telegraphTicks      = 3;
    definition.recoverTicks        = 4;

    eve::combat::CombatEnemyIntentSource ai;
    ai.setTargetRuntime(targets);
    REQUIRE(ai.registerEnemy(definition).ok());
    REQUIRE(ai.setLightAction(enemy, light->action.id).ok());
    REQUIRE(ai.setPosition(enemy, 0.0, 0.0, 0.0).ok());
    REQUIRE(ai.setPosition(player, 1.0, 0.0, 0.0).ok());

    auto first = ai.nextIntent(eve::SimulationTick(1));
    REQUIRE(first.ok());
    CHECK(!first.value().has_value());
    CHECK(ai.phase(enemy) == eve::combat::CombatEnemyPhase::Telegraph);
    CHECK(ai.isPunishable(enemy));

    auto waiting = ai.nextIntent(eve::SimulationTick(3));
    REQUIRE(waiting.ok());
    CHECK(!waiting.value().has_value());

    auto strike = ai.nextIntent(eve::SimulationTick(4));
    REQUIRE(strike.ok());
    REQUIRE(strike.value().has_value());
    CHECK_EQ(strike.value()->grantId, grant.value());
    CHECK(ai.phase(enemy) == eve::combat::CombatEnemyPhase::Recover);
    CHECK(ai.isPunishable(enemy));

    auto recovering = ai.nextIntent(eve::SimulationTick(7));
    REQUIRE(recovering.ok());
    CHECK(!recovering.value().has_value());
    CHECK(ai.phase(enemy) == eve::combat::CombatEnemyPhase::Recover);

    auto idle = ai.nextIntent(eve::SimulationTick(8));
    REQUIRE(idle.ok());
    CHECK(!idle.value().has_value());
    CHECK(ai.phase(enemy) == eve::combat::CombatEnemyPhase::Idle);
    CHECK(!ai.isPunishable(enemy));
}

TEST_CASE("combatEnemyAi.midAndFarBandsSteerTowardTarget") {
    const auto player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto enemy  = subject("01020304-0506-0708-890a-0b0c0d0e0f10");

    eve::combat::CombatTargetRuntime targets;
    REQUIRE(targets.registerOwner(enemy).ok());
    REQUIRE(targets.hardLock(enemy, player).ok());

    eve::combat::CombatEnemyDefinition definition;
    definition.subject             = enemy;
    definition.ownerId             = "fighter:enemy";
    definition.nearRadius          = 2.0;
    definition.midRadius           = 5.0;
    definition.attackCooldownTicks = 10;

    eve::combat::CombatEnemyIntentSource ai;
    ai.setTargetRuntime(targets);
    REQUIRE(ai.registerEnemy(definition).ok());
    REQUIRE(ai.setPosition(enemy, 0.0, 0.0, 0.0).ok());
    REQUIRE(ai.setPosition(player, 3.0, 0.0, 0.0).ok());
    CHECK(ai.band(enemy) == eve::combat::CombatEnemyBand::Mid);

    auto mid = ai.nextSteering(eve::SimulationTick(1));
    REQUIRE(mid.ok());
    REQUIRE_EQ(mid.value().size(), 1u);
    CHECK_EQ(mid.value().front().subject.format(), enemy.format());
    CHECK(mid.value().front().moveDirection.x > 0.9);
    CHECK_EQ(mid.value().front().speedFraction, 0.7);

    REQUIRE(ai.setPosition(player, 8.0, 0.0, 0.0).ok());
    CHECK(ai.band(enemy) == eve::combat::CombatEnemyBand::Far);
    auto far = ai.nextSteering(eve::SimulationTick(2));
    REQUIRE(far.ok());
    REQUIRE_EQ(far.value().size(), 1u);
    CHECK_EQ(far.value().front().speedFraction, 1.0);

    REQUIRE(ai.setPosition(player, 1.0, 0.0, 0.0).ok());
    CHECK(ai.band(enemy) == eve::combat::CombatEnemyBand::Near);
    auto near = ai.nextSteering(eve::SimulationTick(3));
    REQUIRE(near.ok());
    CHECK_EQ(near.value().size(), 0u);
}
}  // namespace test_combat_target_enemy_ai
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_container_transfer)
#include "PrivateTestScope.h"
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "card/Card.h"
#include "card/CardContainers.h"
#include "common/Container.h"
#include "inventory/ContainerAdapters.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// container_transfer.cpp
namespace test_container_transfer {
namespace {

using eve::Revision;
using eve::container::Capacity;
using eve::container::ContainerId;
using eve::container::ContainerObject;
using eve::container::ContainerSnapshot;
using eve::container::IContainer;
using eve::container::Membership;
using eve::container::MembershipEntry;
using eve::container::MembershipId;
using eve::container::SlotIndex;

ContainerId  cid(const char* value) { return ContainerId(value); }
MembershipId mid(const char* value) { return MembershipId(value); }

/** @brief Failing second participant used to prove prepare has no half-state. */
class FailingPrepareContainer final : public IContainer {
public:
    explicit FailingPrepareContainer(ContainerId id) : descriptor_{std::move(id)} {}

    [[nodiscard]] const eve::container::ContainerDescriptor& descriptor() const noexcept override {
        return descriptor_;
    }
    [[nodiscard]] eve::Result<ContainerSnapshot> snapshot() const override {
        return eve::Result<ContainerSnapshot>::success(ContainerSnapshot{descriptor_.id, revision_, entries_});
    }
    [[nodiscard]] eve::Result<void> validateInsert(const ContainerObject&, std::optional<SlotIndex>,
                                                   std::optional<MembershipId>) const override {
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<std::unique_ptr<IContainer::PreparedState>> prepare(const ContainerSnapshot&,
                                                                                  const ContainerSnapshot&) override {
        ++prepareCalls_;
        return eve::Result<std::unique_ptr<IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected destination prepare failure"));
    }

    [[nodiscard]] int prepareCalls() const noexcept { return prepareCalls_; }

private:
    eve::container::ContainerDescriptor descriptor_;
    Revision                            revision_ = Revision(0);
    std::vector<MembershipEntry>        entries_;
    int                                 prepareCalls_ = 0;
};

}  // namespace

TEST_CASE("container.zoneUsesStrongCoordinateSpacesAndValidatesShapes") {
    using namespace eve::container;
    static_assert(!std::is_convertible_v<Coordinate<ScreenSpace>, Coordinate<World2DSpace>>);
    static_assert(!std::is_convertible_v<Coordinate<World2DSpace>, Coordinate<World3DSpace>>);

    auto screen = Zone<ScreenSpace>::create(Rectangle<ScreenSpace>{{10.f, 20.f}, 100.f, 50.f}, Capacity::fixed(2));
    REQUIRE(screen.ok());
    CHECK(screen.value().contains({20.f, 30.f}));
    CHECK(!screen.value().contains({0.f, 0.f}));

    auto invalid2d = Zone<World2DSpace>::create(Rectangle<World2DSpace>{{0.f, 0.f}, -1.f, 2.f});
    CHECK(!invalid2d.ok());
    CHECK_EQ(invalid2d.code(), eve::StatusCode::Rejected);

    auto invalid3d = Zone<World3DSpace>::create(Box3D{{0.f, 0.f, 0.f}, 1.f, 2.f, -3.f});
    CHECK(!invalid3d.ok());

    auto sphere = Zone<World3DSpace>::create(Sphere3D{{0.f, 0.f, 0.f}, 2.f});
    REQUIRE(sphere.ok());
    CHECK(sphere.value().contains({1.f, 1.f, 1.f}));
    CHECK(!sphere.value().contains({2.f, 2.f, 2.f}));
}

TEST_CASE("container.cardDeckHandDiscardTransferEmitsOneStrongEvent") {
    eve::card::Card card;
    REQUIRE_EQ(card.registerCardsFromJson(R"([{"id":"creature","kind":"creature"},{"id":"spell","kind":"spell"}])"), 2);
    auto* deck = card.newDeck();
    auto* hand = card.newHand(card.newConfig());
    REQUIRE(deck != nullptr);
    REQUIRE(hand != nullptr);
    auto* creature = card.newCard("creature");
    REQUIRE(creature != nullptr);
    deck->push(creature);

    eve::card::CardContainerAdapter source(cid("card:deck"), eve::card::CardContainerKind::Deck, deck);
    eve::card::CardContainerAdapter destination(cid("card:hand"), eve::card::CardContainerKind::Hand, nullptr, hand,
                                                nullptr, Capacity::fixed(2));
    std::vector<eve::container::TransferEvent> events;
    eve::container::TransferRequest            request{
        &source,           &destination,          mid(creature->identity()->id.c_str()), SlotIndex(0), SlotIndex(0),
        source.revision(), destination.revision()};
    auto transferred = eve::container::TransferService::transfer(
        request, [&](const eve::container::TransferEvent& event) { events.push_back(event); });
    REQUIRE(transferred.ok());
    CHECK_EQ(deck->count(), 0);
    CHECK_EQ(hand->count(), 1);
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events.front().source.value(), std::string("card:deck"));
    CHECK_EQ(events.front().destination.value(), std::string("card:hand"));

    std::vector<eve::card::CardData*> discard;
    eve::card::CardContainerAdapter discardAdapter(cid("card:discard"), eve::card::CardContainerKind::Discard, nullptr,
                                                   nullptr, &discard);
    auto                            toDiscard = eve::container::TransferService::transfer(
        eve::container::TransferRequest{&destination, &discardAdapter, mid(creature->identity()->id.c_str()),
                                        SlotIndex(0), std::nullopt, destination.revision(), discardAdapter.revision()},
        [](const eve::container::TransferEvent&) { throw std::runtime_error("observer failure"); });
    REQUIRE(toDiscard.ok());
    CHECK_EQ(toDiscard.code(), eve::StatusCode::Applied);
    CHECK(toDiscard.status().hasDiagnostics());
    REQUIRE(toDiscard.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(toDiscard.status().primaryDiagnostic()->code(), eve::DiagnosticCode::CallbackFailure);
    CHECK_EQ(hand->count(), 0);
    CHECK_EQ(discard.size(), std::size_t(1));
}

TEST_CASE("container.cardTransferRejectsFilterAndDuplicateWithoutMutation") {
    eve::card::Card card;
    REQUIRE_EQ(card.registerCardsFromJson(R"([{"id":"creature","kind":"creature"},{"id":"spell","kind":"spell"}])"), 2);
    auto* deck     = card.newDeck();
    auto* hand     = card.newHand(card.newConfig());
    auto* creature = card.newCard("creature");
    REQUIRE(deck != nullptr);
    REQUIRE(hand != nullptr);
    REQUIRE(creature != nullptr);
    deck->push(creature);
    eve::card::CardContainerAdapter source(cid("card:deck-filter"), eve::card::CardContainerKind::Deck, deck);
    eve::card::CardContainerAdapter destination(cid("card:spell-hand"), eve::card::CardContainerKind::Hand, nullptr,
                                                hand, nullptr, Capacity::fixed(2), {"spell"});
    auto                            rejected = eve::container::TransferService::transfer(
        eve::container::TransferRequest{&source, &destination, mid(creature->identity()->id.c_str()), SlotIndex(0),
                                        SlotIndex(0), source.revision(), destination.revision()});
    CHECK(!rejected.ok());
    CHECK_EQ(deck->count(), 1);
    CHECK_EQ(hand->count(), 0);

    hand->addCard(creature);
    eve::card::CardContainerAdapter unrestrictedDestination(
        cid("card:duplicate-hand"), eve::card::CardContainerKind::Hand, nullptr, hand, nullptr, Capacity::fixed(2));
    auto duplicate = eve::container::TransferService::transfer(eve::container::TransferRequest{
        &source, &unrestrictedDestination, mid(creature->identity()->id.c_str()), SlotIndex(0), SlotIndex(0),
        source.revision(), unrestrictedDestination.revision()});
    CHECK(!duplicate.ok());
    CHECK_EQ(deck->count(), 1);
    CHECK_EQ(hand->count(), 1);
}

TEST_CASE("container.transferRestoresSourceAfterApplyFailure") {
    eve::card::Card card;
    REQUIRE_EQ(card.registerCardsFromJson(R"([{"id":"creature","kind":"creature"}])"), 1);
    auto* deck     = card.newDeck();
    auto* creature = card.newCard("creature");
    REQUIRE(deck != nullptr);
    REQUIRE(creature != nullptr);
    deck->push(creature);
    eve::card::CardContainerAdapter source(cid("card:rollback"), eve::card::CardContainerKind::Deck, deck);
    FailingPrepareContainer         destination(cid("failing:destination"));
    auto                            failed = eve::container::TransferService::transfer(
        eve::container::TransferRequest{&source, &destination, mid(creature->identity()->id.c_str()), SlotIndex(0),
                                        std::nullopt, source.revision(), destination.snapshot().value().revision});
    CHECK(!failed.ok());
    CHECK_EQ(destination.prepareCalls(), 1);
    CHECK_EQ(deck->count(), 1);
    CHECK_EQ(deck->peek(), creature);
    auto destinationAfter = destination.snapshot();
    REQUIRE(destinationAfter.ok());
    CHECK(destinationAfter.value().entries.empty());
}

TEST_CASE("container.inventoryBagAndEquipmentTransferRejectsStaleAndFits") {
    using namespace eve::inventory;
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    ItemDefinition sword;
    sword.id        = "sword";
    sword.maxStack  = 1;
    sword.tags      = {"weapon"};
    sword.equipSlot = "mainhand";
    ItemRegistry::registerItem(sword);

    Bag       bag(2);
    const int added = bag.addItem("sword", 1);
    REQUIRE_EQ(added, 1);
    EquipmentSet equipment;
    equipment.defineSlot("mainhand");
    InventoryContainerAdapter bagAdapter(cid("inventory:bag"), &bag);
    InventoryContainerAdapter equipmentAdapter(cid("inventory:equipment"), &equipment);
    const auto                oldRevision = bagAdapter.revision();
    const auto                object      = MembershipId("inventory:" + std::to_string(bag.getSlotInstanceId(0)));

    auto equipped = eve::container::TransferService::transfer(eve::container::TransferRequest{
        &bagAdapter, &equipmentAdapter, object, SlotIndex(0), SlotIndex(0), oldRevision, equipmentAdapter.revision()});
    REQUIRE(equipped.ok());
    CHECK(bag.isSlotEmpty(0));
    CHECK_EQ(equipment.getSlotItemId("mainhand"), std::string("sword"));

    auto stale = eve::container::TransferService::transfer(eve::container::TransferRequest{
        &bagAdapter, &equipmentAdapter, object, SlotIndex(0), SlotIndex(0), oldRevision, equipmentAdapter.revision()});
    CHECK(!stale.ok());
    CHECK_EQ(stale.code(), eve::StatusCode::Rejected);

    auto unequipped = eve::container::TransferService::transfer(
        eve::container::TransferRequest{&equipmentAdapter, &bagAdapter, object, SlotIndex(0), SlotIndex(1),
                                        equipmentAdapter.revision(), bagAdapter.revision()});
    REQUIRE(unequipped.ok());
    CHECK_EQ(bag.getSlotInstanceId(1), std::stoi(object.value().substr(std::string("inventory:").size())));
    CHECK(equipment.isSlotEmpty("mainhand"));

    ItemRegistry::clear();
}

TEST_CASE("container.sameContainerMoveUsesFinalSlotAndRejectsOccupiedSlot") {
    using namespace eve::inventory;
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    ItemDefinition item;
    item.id       = "token";
    item.maxStack = 1;
    ItemRegistry::registerItem(item);
    Bag       bag(3);
    const int firstAdded  = bag.addItem("token", 1);
    const int secondAdded = bag.addItem("token", 1);
    REQUIRE_EQ(firstAdded, 1);
    REQUIRE_EQ(secondAdded, 1);
    InventoryContainerAdapter adapter(cid("inventory:same"), &bag);
    const auto                first    = MembershipId("inventory:" + std::to_string(bag.getSlotInstanceId(0)));
    const auto                revision = adapter.revision();
    auto                      moved    = eve::container::TransferService::transfer(
        eve::container::TransferRequest{&adapter, &adapter, first, SlotIndex(0), SlotIndex(2), revision, revision});
    REQUIRE(moved.ok());
    CHECK_EQ(bag.getSlotInstanceId(2), std::stoi(first.value().substr(10)));
    CHECK(bag.isSlotEmpty(0));
    const auto second            = MembershipId("inventory:" + std::to_string(bag.getSlotInstanceId(1)));
    const auto afterMoveRevision = adapter.revision();
    auto       occupied          = eve::container::TransferService::transfer(eve::container::TransferRequest{
        &adapter, &adapter, second, SlotIndex(1), SlotIndex(2), afterMoveRevision, afterMoveRevision});
    CHECK(!occupied.ok());
    CHECK_EQ(bag.getSlotInstanceId(2), std::stoi(first.value().substr(10)));
    ItemRegistry::clear();
}
}  // namespace test_container_transfer
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_definition_runtime)
#include "PrivateTestScope.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include "card/CardDefinitionRuntime.h"
#include "common/definitions/DefinitionRuntime.h"
#include "definitions/Definitions.h"
#include "effects/EffectDefinitionRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// definition_runtime.cpp
namespace test_definition_runtime {
namespace {

struct TypedState {
    int         value = 0;
    std::string label;
};

eve::PersistentId testId() {
    const auto parsed = eve::PersistentId::parse("11111111-1111-7111-8111-111111111111");
    return *parsed;
}

eve::DefinitionRef testReference(const char* text) {
    auto parsed = eve::DefinitionRef::parse(text);
    if (!parsed.ok()) return {};
    return std::move(parsed).takeValue();
}

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint8_t          value = 29;
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            for (const unsigned char byte : input) value = static_cast<std::uint8_t>(value * 33u + byte);
            bytes[index] = static_cast<std::uint8_t>(value + index);
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

}  // namespace

TEST_CASE("definitionRuntime.policiesAndGenerationQualifiedStaleChecks") {
    const auto reference = testReference("test:unit");
    auto       created   = eve::definition::RuntimeInstance<TypedState>::create(testId(), reference, eve::Generation(1),
                                                                                TypedState{7, "original"});
    REQUIRE(created.ok());
    auto runtime = std::move(created).takeValue();

    auto wrongGeneration = runtime.checkDefinition(eve::definition::DefinitionHandle{reference, eve::Generation(2)});
    CHECK(!wrongGeneration.ok());
    CHECK(static_cast<int>(wrongGeneration.error()->code()) == static_cast<int>(eve::DiagnosticCode::StaleHandle));

    auto kept = runtime.reload(eve::definition::DefinitionHandle{reference, eve::Generation(2)},
                               eve::definition::ReloadPolicy::KeepInstanceValues, TypedState{99, "new defaults"});
    REQUIRE(kept.ok());
    CHECK(static_cast<int>(kept.value().disposition) == static_cast<int>(eve::definition::ReloadDisposition::Kept));
    CHECK_EQ(runtime.state().value, 7);
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{2});

    auto reapplied = runtime.reload(eve::definition::DefinitionHandle{reference, eve::Generation(3)},
                                    eve::definition::ReloadPolicy::ReapplyDefaults, TypedState{42, "defaults"});
    REQUIRE(reapplied.ok());
    CHECK(static_cast<int>(reapplied.value().disposition) ==
          static_cast<int>(eve::definition::ReloadDisposition::DefaultsReapplied));
    CHECK_EQ(runtime.state().value, 42);
    CHECK_EQ(runtime.state().label, std::string("defaults"));

    auto rebuilt =
        runtime.reload(eve::definition::DefinitionHandle{reference, eve::Generation(4)},
                       eve::definition::ReloadPolicy::RebuildInstance, TypedState{100, "ignored"},
                       [](const TypedState& oldState, const eve::definition::InstanceIdentity& identity,
                          const eve::definition::DefinitionHandle& handle) -> eve::Result<TypedState> {
                           if (!identity.isValid() || !handle.isValid())
                               return eve::Result<TypedState>::failure(eve::Diagnostic::error(
                                   eve::DiagnosticCode::InvalidArgument, "test rebuild received invalid identity"));
                           return eve::Result<TypedState>::success(TypedState{oldState.value + 1, "rebuilt"});
                       });
    REQUIRE(rebuilt.ok());
    CHECK(static_cast<int>(rebuilt.value().disposition) ==
          static_cast<int>(eve::definition::ReloadDisposition::Rebuilt));
    CHECK_EQ(runtime.state().value, 43);

    auto failedRebuild = runtime.reload(eve::definition::DefinitionHandle{reference, eve::Generation(5)},
                                        eve::definition::ReloadPolicy::RebuildInstance, TypedState{0, "must not apply"},
                                        [](const TypedState&, const eve::definition::InstanceIdentity&,
                                           const eve::definition::DefinitionHandle&) -> eve::Result<TypedState> {
                                            return eve::Result<TypedState>::failure(eve::Diagnostic::error(
                                                eve::DiagnosticCode::Failed, "injected rebuild failure"));
                                        });
    CHECK(!failedRebuild.ok());
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{4});
    CHECK_EQ(runtime.state().value, 43);

    auto rejected = runtime.reload(eve::definition::DefinitionHandle{reference, eve::Generation(5)},
                                   eve::definition::ReloadPolicy::RejectWhileActive, TypedState{0, "must not apply"});
    CHECK(!rejected.ok());
    CHECK(static_cast<int>(rejected.error()->code()) == static_cast<int>(eve::DiagnosticCode::Conflict));
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{4});
    CHECK_EQ(runtime.state().value, 43);

    runtime.setActive(false);
    auto inactiveReject = runtime.reload(eve::definition::DefinitionHandle{reference, eve::Generation(5)},
                                         eve::definition::ReloadPolicy::RejectWhileActive, TypedState{0, "still kept"});
    REQUIRE(inactiveReject.ok());
    CHECK(static_cast<int>(inactiveReject.value().disposition) ==
          static_cast<int>(eve::definition::ReloadDisposition::Kept));
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{5});
    CHECK_EQ(runtime.state().value, 43);
}

TEST_CASE("definitionRuntime.cardAdapterProjectsTypedIdentityAndReloads") {
    eve::definitions::DefinitionRegistry registry;
    auto                                 inserted = registry.insert(
        "card", "scout", 1, R"({"name":"Scout","kind":"creature","cost":2,"attack":3,"health":5,"tags":["unit"]})");
    REQUIRE(inserted.ok());

    auto runtimeResult = eve::card::CardDefinitionRuntime::create(registry, testReference("card:scout"), testId(),
                                                                  eve::definition::ReloadPolicy::RebuildInstance);
    REQUIRE(runtimeResult.ok());
    auto runtime = std::move(runtimeResult).takeValue();
    CHECK_EQ(runtime.state().attack, 3);
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{1});
    runtime.state().currentHealth = 2;

    auto snapshot = runtime.snapshot(eve::Revision(1), eve::SimulationTick(2), testHash());
    REQUIRE(snapshot.ok());
    runtime.state().currentHealth = 1;
    auto restored                 = runtime.restore(snapshot.value(), testHash());
    REQUIRE(restored.ok());
    CHECK_EQ(runtime.state().currentHealth, 2);

    auto card = eve::card::CardData::createCard();
    REQUIRE(card != nullptr);
    auto projected = runtime.applyTo(card);
    REQUIRE(projected.ok());
    CHECK_EQ(card->stats()->health, 2);
    CHECK_EQ(card->definitionBinding()->identity.instanceId, testId());
    CHECK_EQ(card->definitionBinding()->identity.definitionGeneration.value(), uint64_t{1});
    card->release();

    auto replaced = registry.replace("card", "scout", 2,
                                     R"({"name":"Veteran Scout","kind":"creature","cost":3,"attack":6,"health":4})");
    REQUIRE(replaced.ok());
    auto staleSnapshot = runtime.restore(snapshot.value(), testHash());
    CHECK(!staleSnapshot.ok());
    CHECK_EQ(runtime.state().currentHealth, 2);
    auto reloaded = runtime.reload(eve::definition::ReloadPolicy::RebuildInstance);
    REQUIRE(reloaded.ok());
    CHECK(static_cast<int>(reloaded.value().disposition) ==
          static_cast<int>(eve::definition::ReloadDisposition::Rebuilt));
    CHECK_EQ(runtime.state().attack, 6);
    CHECK_EQ(runtime.state().currentHealth, 2);
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{2});
}

TEST_CASE("definitionRuntime.effectAdapterKeepsTypedPolicyAndRejectsActiveReload") {
    eve::definitions::DefinitionRegistry registry;
    auto                                 inserted = registry.insert(
        "effect", "burn", 1,
        R"({"stackKey":"burn","priority":4,"duration":8.0,"magnitude":3.5,"stackCount":1,"maxStacks":3,"stackMode":"reuse","stackCountPolicy":"increment","payload":{"damageType":"fire"}})");
    REQUIRE(inserted.ok());

    auto runtimeResult = eve::effects::EffectDefinitionRuntime::create(
        registry, testReference("effect:burn"), "actor:target", "spell:ember", testId(),
        eve::definition::ReloadPolicy::RejectWhileActive);
    REQUIRE(runtimeResult.ok());
    auto runtime = std::move(runtimeResult).takeValue();
    CHECK(static_cast<int>(runtime.state().policy.stackMode) == static_cast<int>(eve::effects::StackMode::Reuse));
    CHECK(static_cast<int>(runtime.state().policy.stackCount) ==
          static_cast<int>(eve::effects::StackCountPolicy::Increment));

    eve::effects::EffectInstance instance;
    auto                         projected = runtime.applyTo(&instance);
    REQUIRE(projected.ok());
    CHECK(static_cast<bool>(instance.definitionIdentity == runtime.identity()));
    CHECK(static_cast<int>(instance.policy.stackMode) == static_cast<int>(eve::effects::StackMode::Reuse));
    CHECK_EQ(instance.payload.getJson("damageType"), std::string("\"fire\""));

    auto replaced = registry.replace(
        "effect", "burn", 2,
        R"({"stackKey":"burn","priority":5,"duration":10.0,"magnitude":4.0,"stackCount":1,"maxStacks":3})");
    REQUIRE(replaced.ok());
    auto rejected = runtime.reload(eve::definition::ReloadPolicy::RejectWhileActive);
    CHECK(!rejected.ok());
    CHECK(static_cast<int>(rejected.error()->code()) == static_cast<int>(eve::DiagnosticCode::Conflict));
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{1});
}
}  // namespace test_definition_runtime
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_definition_runtime_skill_vehicle)
#include "PrivateTestScope.h"
#include <cstdint>
#include <string_view>
#include "common/ECS.h"
#include "common/definitions/RuntimeSnapshot.h"
#include "definitions/Definitions.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "rpg/SkillConditionCodec.h"
#include "rpg/SkillDefinitionRuntime.h"
#include "vehicle/VehicleDefinitionRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// definition_runtime_skill_vehicle.cpp
namespace test_definition_runtime_skill_vehicle {
namespace {

eve::PersistentId testId() {
    const auto parsed = eve::PersistentId::parse("22222222-2222-7222-8222-222222222222");
    return *parsed;
}

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t first  = 14695981039346656037ull;
        std::uint64_t second = 1099511628211ull;
        for (const unsigned char byte : input) {
            first ^= byte;
            first *= 1099511628211ull;
            second ^= static_cast<std::uint64_t>(byte + 0x9du);
            second *= 14029467366897019727ull;
        }
        eve::ContentId::Bytes bytes{};
        for (std::size_t index = 0; index < sizeof(first); ++index) {
            bytes[index]     = static_cast<std::uint8_t>((first >> (index * 8u)) & 0xffu);
            bytes[8 + index] = static_cast<std::uint8_t>((second >> (index * 8u)) & 0xffu);
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

}  // namespace

TEST_CASE("definitionRuntime.skillAdapterUsesCanonicalRegistryAndAtomicSnapshot") {
    eve::rpg::SkillRegistry::clear();
    eve::rpg::SkillDefinition definition;
    definition.id         = "fireball";
    definition.cooldown   = 8.f;
    definition.castTime   = 1.25f;
    definition.targetType = "single";
    definition.tags       = {"fire"};
    definition.castCondition =
        eve::condition::Condition::compare("skill.cooldown", eve::condition::CompareOperator::Equal, eve::Value(0));
    eve::rpg::SkillRegistry::registerSkill(definition);

    auto runtimeResult = eve::rpg::SkillDefinitionRuntime::create(testId(), "fireball");
    REQUIRE(runtimeResult.ok());
    auto       runtime            = std::move(runtimeResult).takeValue();
    const auto originalGeneration = runtime.identity().definitionGeneration;
    auto       typed              = runtime.definition();
    REQUIRE(typed.ok());
    CHECK_EQ(typed.value().targetType, std::string("single"));
    CHECK_EQ(static_cast<int>(typed.value().castCondition.kind()),
             static_cast<int>(eve::condition::ConditionKind::Compare));
    runtime.state().cooldownRemaining = 4.f;

    auto snapshot = runtime.snapshot(eve::Revision(3), eve::SimulationTick(12), testHash());
    REQUIRE(snapshot.ok());
    runtime.state().cooldownRemaining = 0.f;
    auto restored                     = runtime.restore(snapshot.value(), testHash());
    REQUIRE(restored.ok());
    CHECK_EQ(runtime.state().cooldownRemaining, 4.f);

    eve::rpg::SkillDefinition replacement = definition;
    replacement.cooldown                  = 3.f;
    eve::rpg::SkillRegistry::registerSkill(replacement);
    auto rejectedRestore = runtime.restore(snapshot.value(), testHash());
    CHECK(!rejectedRestore.ok());
    CHECK_EQ(static_cast<int>(rejectedRestore.error()->code()), static_cast<int>(eve::DiagnosticCode::StaleHandle));
    CHECK_EQ(runtime.state().cooldownRemaining, 4.f);

    runtime.setActive(true);
    auto rejectedReload = runtime.reload(eve::definition::ReloadPolicy::RejectWhileActive);
    CHECK(!rejectedReload.ok());
    CHECK_EQ(runtime.identity().definitionGeneration, originalGeneration);
    runtime.setActive(false);
    auto reloaded = runtime.reload(eve::definition::ReloadPolicy::RebuildInstance);
    REQUIRE(reloaded.ok());
    CHECK_EQ(runtime.state().cooldownRemaining, 3.f);

    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto*            actor = eve::rpg::RPGActor::createActor();
    REQUIRE(actor != nullptr);
    auto projected = runtime.applyTo(actor);
    REQUIRE(projected.ok());
    CHECK(actor->knowsSkill("fireball"));
    CHECK_EQ(actor->getSkillCooldown("fireball"), 3.f);
    actor->release();
}

TEST_CASE("definitionRuntime.vehicleAdapterProjectsEntityAndRejectsStaleSnapshot") {
    eve::definitions::DefinitionRegistry registry;
    auto                                 inserted = registry.insert(
        "vehicle", "scout", 1,
        R"({"category":"vehicle","mobility":"kinematic","maxSpeed":80,"radius":6,"maxHealth":100,"tags":["recon"]})");
    REQUIRE(inserted.ok());
    auto reference = eve::DefinitionRef::parse("vehicle:scout");
    REQUIRE(reference.ok());
    auto runtimeResult =
        eve::vehicle::VehicleDefinitionRuntime::create(registry, std::move(reference).takeValue(), testId());
    REQUIRE(runtimeResult.ok());
    auto runtime            = std::move(runtimeResult).takeValue();
    runtime.state().x       = -12.f;
    runtime.state().y       = 4.f;
    runtime.state().health  = 75.f;
    runtime.state().faction = "blue";

    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto*            entity = eve::vehicle::VehicleEntity::createVehicle();
    REQUIRE(entity != nullptr);
    auto projected = runtime.applyTo(entity);
    REQUIRE(projected.ok());
    CHECK_EQ(entity->motion()->x, -12.f);
    CHECK_EQ(entity->health()->hp, 75.f);
    CHECK_EQ(entity->identity()->faction, std::string("blue"));
    CHECK_EQ(entity->definitionBinding()->identity.definitionGeneration, runtime.identity().definitionGeneration);
    CHECK_EQ(entity->definition()->def->category, std::string("vehicle"));

    auto snapshot = runtime.snapshot(eve::Revision(4), eve::SimulationTick(20), testHash());
    REQUIRE(snapshot.ok());
    runtime.state().x = 99.f;
    auto restored     = runtime.restore(snapshot.value(), testHash());
    REQUIRE(restored.ok());
    CHECK_EQ(runtime.state().x, -12.f);

    auto replaced =
        registry.replace("vehicle", "scout", 2,
                         R"({"category":"vehicle","mobility":"kinematic","maxSpeed":90,"radius":6,"maxHealth":120})");
    REQUIRE(replaced.ok());
    auto stale = runtime.restore(snapshot.value(), testHash());
    CHECK(!stale.ok());
    CHECK_EQ(static_cast<int>(stale.error()->code()), static_cast<int>(eve::DiagnosticCode::StaleHandle));
    CHECK_EQ(runtime.state().x, -12.f);
    entity->release();
}
}  // namespace test_definition_runtime_skill_vehicle
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_definition_runtime_weapon)
#include "PrivateTestScope.h"
#include <cstdint>
#include <string_view>
#include <utility>
#include "common/definitions/RuntimeSnapshot.h"
#include "definitions/Definitions.h"
#include "weapon/WeaponDefinitionRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// definition_runtime_weapon.cpp
namespace test_definition_runtime_weapon {
namespace {

eve::PersistentId testId() {
    const auto parsed = eve::PersistentId::parse("33333333-3333-7333-8333-333333333333");
    return *parsed;
}

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint8_t          value = 41;
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            for (const unsigned char byte : input) value = static_cast<std::uint8_t>(value * 33u + byte + index);
            bytes[index] = static_cast<std::uint8_t>(value + index);
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

eve::DefinitionRef weaponRef() {
    auto result = eve::DefinitionRef::parse("weapon:rifle");
    if (!result.ok()) return {};
    return std::move(result).takeValue();
}

}  // namespace

TEST_CASE("definitionRuntime.weaponTypedStateSnapshotReloadAndStale") {
    eve::definitions::DefinitionRegistry registry;
    const std::string                    original = R"JSON({
        "id":"rifle", "kind":"ranged", "logic":"hitscan", "damage":25,
        "range":500, "spreadMin":1, "spreadMax":8,
        "ammo":{"mag":30,"reserve":90,"reload":2},
        "resource":{"kind":"ammo","max":30,"cost":1,"infinite":false},
        "projectile":{"type":"bullet","speed":800,"pelletCount":1}
    })JSON";
    auto                                 inserted = registry.insert("weapon", "rifle", 1, original);
    REQUIRE(inserted.ok());

    auto created = eve::weapon::WeaponDefinitionRuntime::create(registry, weaponRef(), testId(),
                                                                eve::definition::ReloadPolicy::RebuildInstance);
    REQUIRE(created.ok());
    auto       runtime   = std::move(created).takeValue();
    const auto oldHandle = runtime.definitionHandle();
    auto       typed     = runtime.definition();
    REQUIRE(typed.ok());
    CHECK_EQ(typed.value().damage, 25.f);
    CHECK_EQ(typed.value().projectile.type, std::string("bullet"));
    CHECK_EQ(runtime.state().resource.value, 30.f);
    runtime.state().resource.value = 7.f;
    runtime.state().currentSpread  = 4.f;

    auto snapshot = runtime.snapshot(eve::Revision(2), eve::SimulationTick(17), testHash());
    REQUIRE(snapshot.ok());
    runtime.state().resource.value = 1.f;
    runtime.state().currentSpread  = 0.f;
    auto restored                  = runtime.restore(snapshot.value(), testHash());
    REQUIRE(restored.ok());
    CHECK_EQ(runtime.state().resource.value, 7.f);
    CHECK_EQ(runtime.state().currentSpread, 4.f);

    const std::string replacement = R"JSON({
        "id":"rifle", "kind":"ranged", "logic":"hitscan", "damage":40,
        "range":700, "spreadMin":2, "spreadMax":3,
        "ammo":{"mag":10,"reserve":20,"reload":1}
    })JSON";
    auto              replaced    = registry.replace("weapon", "rifle", 1, replacement);
    REQUIRE(replaced.ok());
    auto reloaded = runtime.reload(eve::definition::ReloadPolicy::RebuildInstance);
    REQUIRE(reloaded.ok());
    CHECK(runtime.definitionHandle().generation != oldHandle.generation);
    CHECK_EQ(runtime.state().resource.value, 7.f);
    CHECK_EQ(runtime.state().currentSpread, 3.f);
    auto stale = runtime.restore(snapshot.value(), testHash());
    CHECK(!stale.ok());
    CHECK_EQ(runtime.state().resource.value, 7.f);
}

TEST_CASE("definitionRuntime.weaponRejectWhileActiveIsAtomic") {
    eve::definitions::DefinitionRegistry registry;
    const auto                           first =
        registry.insert("weapon", "rifle", 1, R"JSON({"id":"rifle","kind":"ranged","ammo":{"mag":4}})JSON");
    REQUIRE(first.ok());
    auto created = eve::weapon::WeaponDefinitionRuntime::create(registry, weaponRef(), testId());
    REQUIRE(created.ok());
    auto runtime = std::move(created).takeValue();
    runtime.setActive(true);
    const auto before = runtime.definitionHandle();
    const auto replacement =
        registry.replace("weapon", "rifle", 1, R"JSON({"id":"rifle","kind":"ranged","ammo":{"mag":8}})JSON");
    REQUIRE(replacement.ok());
    auto rejected = runtime.reload(eve::definition::ReloadPolicy::RejectWhileActive);
    CHECK(!rejected.ok());
    CHECK_EQ(runtime.definitionHandle().generation, before.generation);
}
}  // namespace test_definition_runtime_weapon
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_dialogue_localization)
#include "PrivateTestScope.h"
#include "dialogue/ConversationLocalization.h"
#include "dialogue/DialogueFlow.h"
#include "i18n/I18n.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// dialogue_localization.cpp
namespace test_dialogue_localization {
using namespace eve::dialogue;

TEST_CASE("dialogueLocalization.roundtripMissingAndVoiceManifest") {
    eve::dnut::SequenceAsset asset;
    asset.id    = "intro";
    asset.entry = "welcome";
    eve::dnut::SequenceNode line;
    line.id   = "welcome";
    line.type = "line";
    line.payload.set("text", eve::Value("Welcome"));
    line.payload.set("speaker", eve::Value("Guide"));
    line.payload.set("i18n", eve::Value("intro.welcome"));
    line.payload.set("voice", eve::Value("voice/default"));
    line.next   = "end";
    asset.nodes = {line, {"end", "end"}};

    ConversationLocalizationCatalog     catalog;
    std::vector<ConversationDiagnostic> diagnostics;
    const std::string                   csv =
        "i18n_key,locale,translation,voice,status,duration\r\n"
        "\"intro.welcome\",\"zh\",\"欢迎你，旅行者\",\"voice/zh/001\",\"recorded\",\"1.25\"\r\n";
    CHECK(catalog.importCsv(csv, "", diagnostics) == 1);
    CHECK(catalog.resolveText("intro.welcome", "zh-CN", "Welcome") == "欢迎你，旅行者");
    CHECK(catalog.resolveVoice("intro.welcome", "zh-CN", "default") == "voice/zh/001");
    CHECK(catalog.resolveStatus("intro.welcome", "zh-CN") == "recorded");
    CHECK(catalog.resolveDuration("intro.welcome", "zh-CN") == 1.25);
    CHECK(catalog.exportMissingCsv({asset}, "zh-CN").find("intro.welcome") == std::string::npos);
    CHECK(catalog.exportMissingCsv({asset}, "ja").find("intro.welcome") != std::string::npos);
    const std::string manifest = catalog.exportVoiceRecordingCsv({asset}, "zh-CN");
    CHECK(manifest.find("recorded") != std::string::npos);
    CHECK(manifest.find("voice/zh/001") != std::string::npos);
}

TEST_CASE("dialogueLocalization.rejectsPartialDuration") {
    ConversationLocalizationCatalog     catalog;
    std::vector<ConversationDiagnostic> diagnostics;
    const std::string                   csv =
        "i18n_key,locale,translation,duration\n"
        "intro.invalid,en,Hello,1.25seconds\n";
    CHECK(catalog.importCsv(csv, "", diagnostics) == 1);
    CHECK(catalog.resolveDuration("intro.invalid", "en") == 0.0);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message == "invalid duration was ignored");
}

TEST_CASE("dialogueLocalization.validatesExactRuntimeLocaleCoverage") {
    auto* localization = eve::i18n::I18n::create();
    auto* flow         = DialogueFlow::create();
    REQUIRE(localization != nullptr);
    REQUIRE(flow != nullptr);
    localization->clear();
    flow->clear();
    auto bundle = localization->replaceBundleFromJson(R"({
      "schema":"eve.i18n.bundle","version":1,"defaultLocale":"en",
      "locales":{
        "en":{"intro":{"welcome":"Welcome"}},
        "zh-CN":{"intro":{"welcome":"欢迎"}}
      }
    })");
    REQUIRE(bundle.ok());
    REQUIRE(flow->loadDnutChecked("schema \"eve.dnut\"\nversion 1\nconversation intro version=1 entry=welcome {\n"
                                  "node welcome line speaker=guide i18n=intro.welcome next=end\n"
                                  "node end end\n}\n",
                                  "localized.dnut")
                .ok());
    auto validated = flow->validateLocalization(*localization, "zh-CN");
    REQUIRE(validated.ok());
    CHECK_EQ(validated.value(), 1);
    auto missingLocale = flow->validateLocalization(*localization, "fr");
    CHECK(!missingLocale.ok());

    REQUIRE(flow->loadDnutChecked("schema \"eve.dnut\"\nversion 1\nconversation broken version=1 entry=line {\n"
                                  "node line line speaker=guide i18n=intro.missing next=end\n"
                                  "node end end\n}\n",
                                  "missing.dnut")
                .ok());
    auto missingKey = flow->validateLocalization(*localization, "en");
    CHECK(!missingKey.ok());
    flow->clear();
    localization->clear();
}
}  // namespace test_dialogue_localization
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_dialogue_payment)
#include "PrivateTestScope.h"
#include <array>
#include <cstdint>
#include <simplesquirrel/simplesquirrel.hpp>
#include <span>
#include <utility>
#include "attributes/AttributeSetResourceAccount.h"
#include "dialogue/ConversationAuthoring.h"
#include "dialogue/DialogueFlow.h"
#include "dialogue/DialoguePayment.h"
#include "dialogue/DialogueSequence.h"
#include "economy/EconomyLedgerResourceAccount.h"
#include "statepatch/StateAccessAdapter.h"
#include "statepatch/StatePatch.h"
#include "zeroerr/unittest.h"
// dialogue_payment.cpp
namespace test_dialogue_payment {
namespace {

using eve::StateMutation;
using eve::Value;
using eve::dialogue::DialogueAccountBindings;
using eve::dialogue::DialoguePaymentAdapter;
using eve::dialogue::PaymentSpec;

DialoguePaymentAdapter makeAdapter(eve::economy::EconomyLedgerResourceAccount&   money,
                                   eve::attributes::AttributeSetResourceAccount& reputation) {
    DialogueAccountBindings bindings;
    bindings.money      = &money;
    bindings.reputation = &reputation;
    return DialoguePaymentAdapter(std::move(bindings));
}

}  // namespace

TEST_CASE("dialoguePayment.composesMoneyReputationAndStatePatch") {
    eve::economy::EconomyLedger ledger;
    CHECK(ledger.credit("gold", 20) == 20);
    eve::attributes::AttributeSet attributes("actor");
    attributes.setBase("reputation", 10.0);
    eve::economy::EconomyLedgerResourceAccount   money(ledger);
    eve::attributes::AttributeSetResourceAccount reputation(attributes);
    DialoguePaymentAdapter                       adapter = makeAdapter(money, reputation);

    eve::statepatch::Store                  store;
    eve::statepatch::StatePatchStateAdapter state(store);
    PaymentSpec                             payment{5, 2};
    const std::array<StateMutation, 1>      mutations{{
        StateMutation{"actor", "accepted", Value(true), eve::MutationKind::Set, true},
    }};

    auto result = adapter.executeStateMutation("dialogue-payment-success", payment, state, mutations);
    REQUIRE(result.ok());
    (void)std::move(result).takeValue();
    CHECK(ledger.get("gold") == 15);
    CHECK(attributes.getBase("reputation") == 8.0);
    CHECK(store.has("actor", "accepted"));
    CHECK(store.get("actor", "accepted") == "true");
}

TEST_CASE("dialoguePayment.failureDoesNotDebitOrPublishState") {
    eve::economy::EconomyLedger ledger;
    CHECK(ledger.credit("gold", 20) == 20);
    eve::attributes::AttributeSet attributes("actor");
    attributes.setBase("reputation", 10.0);
    eve::economy::EconomyLedgerResourceAccount   money(ledger);
    eve::attributes::AttributeSetResourceAccount reputation(attributes);
    DialoguePaymentAdapter                       adapter = makeAdapter(money, reputation);

    eve::statepatch::Store                  store;
    eve::statepatch::StatePatchStateAdapter state(store);
    const std::array<StateMutation, 2>      invalidMutations{{
        StateMutation{"actor", "must_not_publish", Value(true), eve::MutationKind::Set, true},
        StateMutation{"actor", "not_a_number", Value("bad"), eve::MutationKind::AddNumber, true},
    }};
    auto result = adapter.executeStateMutation("dialogue-payment-failure", PaymentSpec{5, 2}, state, invalidMutations);
    REQUIRE(!result.ok());
    (void)result.status();
    CHECK(ledger.get("gold") == 20);
    CHECK(attributes.getBase("reputation") == 10.0);
    CHECK(!store.has("actor", "must_not_publish"));

    auto insufficient = adapter.executeStateMutation("dialogue-payment-insufficient", PaymentSpec{100, 1}, state,
                                                     std::span<const StateMutation>{});
    REQUIRE(!insufficient.ok());
    (void)insufficient.status();
    CHECK(ledger.get("gold") == 20);
    CHECK(attributes.getBase("reputation") == 10.0);
}

TEST_CASE("dialoguePayment.specRejectsUnknownAndNonPositiveFields") {
    const auto unknown = PaymentSpec::fromValue(Value::Object{{"money", Value(1)}, {"tokens", Value(2)}});
    REQUIRE(!unknown.ok());
    (void)unknown.status();

    const auto negative = PaymentSpec::fromValue(Value::Object{{"reputation", Value(0)}});
    REQUIRE(!negative.ok());
    (void)negative.status();

    PaymentSpec payment{3, 4};
    const Value serialized = payment.toValue();
    auto        roundTrip  = PaymentSpec::fromValue(serialized);
    REQUIRE(roundTrip.ok());
    const PaymentSpec restored = std::move(roundTrip).takeValue();
    CHECK(restored.money == 3);
    CHECK(restored.reputation == 4);
}

TEST_CASE("dialoguePayment.choiceUsesTheSameAtomicFacade") {
    eve::economy::EconomyLedger ledger;
    CHECK(ledger.credit("gold", 20) == 20);
    eve::attributes::AttributeSet attributes("actor");
    attributes.setBase("reputation", 10.0);
    eve::economy::EconomyLedgerResourceAccount   money(ledger);
    eve::attributes::AttributeSetResourceAccount reputation(attributes);
    eve::statepatch::Store                       store;
    eve::statepatch::StatePatchStateAdapter      state(store);

    eve::dnut::SequenceAsset asset;
    asset.id    = "dialogue.payment-choice";
    asset.entry = "choice";
    eve::dnut::SequenceNode choice;
    choice.id   = "choice";
    choice.type = "choice";
    choice.routes.push_back({"buy", {}, "end"});
    choice.routes.front().payload.set("payment", PaymentSpec{5, 2}.toValue());
    const std::array<StateMutation, 1> choiceMutations{{
        StateMutation{"actor", "accepted", Value(true), eve::MutationKind::Set, true},
    }};
    choice.routes.front().payload.set("stateMutations", eve::dialogue::encodeSequenceStateMutations(choiceMutations));
    eve::dnut::SequenceNode end;
    end.id      = "end";
    end.type    = "end";
    asset.nodes = {choice, end};
    eve::dialogue::ConversationDocument document(asset);

    eve::dialogue::DialogueFlow                    flow;
    eve::dialogue::DialogueFlow::IntegrationConfig config;
    config.stateMutation       = &state;
    config.accounts.money      = &money;
    config.accounts.reputation = &reputation;
    flow.configureIntegration(std::move(config));
    REQUIRE(flow.applyDocumentChecked(&document).ok());
    CHECK(flow.startChecked(asset.id, ssq::Object{}).ok());
    CHECK(flow.select("buy").ok());
    CHECK(ledger.get("gold") == 15);
    CHECK(attributes.getBase("reputation") == 8.0);
    CHECK(store.has("actor", "accepted"));
}
}  // namespace test_dialogue_payment
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_dialogue_proc)
#include "PrivateTestScope.h"
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "avatar/Avatar.h"
#include "dialogue/Dialogue.h"
#include "dialogue/DnutParser.h"
#include "i18n/I18n.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// dialogue_proc.cpp
namespace test_dialogue_proc {
using namespace eve::dialogue;
using eve::avatar::Avatar;
using eve::avatar::AvatarInstance;
using eve::i18n::I18n;

namespace {

using Vars = std::unordered_map<std::string, Dialogue::VarValue>;

DataValue O(DataValue::Object v) { return DataValue::object(std::move(v)); }

DataValue A(std::vector<DataValue> v) { return DataValue::array(std::move(v)); }

DataValue S(const std::string& v) { return DataValue::string(v); }

DataValue I(long long v) { return DataValue::integer(v); }

DataValue F(double v) { return DataValue::number(v); }

DataValue B(bool v) { return DataValue::boolean(v); }

DataValue poolsRoot(DataValue pools) { return O({{"pools", std::move(pools)}}); }

void resetDialogue(Dialogue* dlg) {
    dlg->reset();
    dlg->clearPools();
    dlg->clearVars("all");
}

}  // namespace

TEST_CASE("dialogueProc.varsScopes") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);

    CHECK(dlg->setVarValue("hp", Dialogue::VarValue::integer(100), "global"));
    CHECK(dlg->setVarValue("mood", Dialogue::VarValue::string("happy"), "scene"));
    CHECK(dlg->setVarValue("ratio", Dialogue::VarValue::number(1.5), "global"));
    CHECK(dlg->setVarValue("flag", Dialogue::VarValue::boolean(true), "scene"));

    CHECK(dlg->hasVar("hp", "global"));
    CHECK(!dlg->hasVar("hp", "scene"));
    CHECK_EQ(dlg->getVarType("mood", "scene"), std::string("string"));
    CHECK_EQ(dlg->getVarType("hp", "global"), std::string("int"));
    CHECK_EQ(dlg->getVarType("ratio", "global"), std::string("float"));
    CHECK_EQ(dlg->getVarType("flag", "scene"), std::string("bool"));
    CHECK_EQ(dlg->getVarType("missing", "global"), std::string(""));

    CHECK_EQ(dlg->getVarInt("hp", -1, "global"), 100);
    CHECK_EQ(dlg->getVarInt("hp", -1, "scene"), -1);
    CHECK_EQ(dlg->getVarFloat("ratio", 0.f, "global"), 1.5f);
    CHECK(dlg->getVarBool("flag", false, "scene"));
    CHECK_EQ(dlg->getVarString("mood", "", "scene"), std::string("happy"));
    CHECK_EQ(dlg->getVarString("mood", "", "global"), std::string(""));

    dlg->clearVars("scene");
    CHECK(!dlg->hasVar("mood", "scene"));
    CHECK(dlg->hasVar("hp", "global"));
    dlg->clearVars("global");
    CHECK(!dlg->hasVar("hp", "global"));

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.conditions") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);

    dlg->setVarValue("mood", Dialogue::VarValue::string("happy"), "global");
    dlg->setVarValue("hour", Dialogue::VarValue::integer(20), "global");
    dlg->setVarValue("flag", Dialogue::VarValue::boolean(true), "global");

    CHECK(dlg->evalConditionData(O({
        {"var", S("mood")},
        {"op", S("eq")},
        {"value", S("happy")},
    })));
    CHECK(!dlg->evalConditionData(O({
        {"var", S("mood")},
        {"op", S("eq")},
        {"value", S("sad")},
    })));
    CHECK(dlg->evalConditionData(O({
        {"var", S("hour")},
        {"op", S("ge")},
        {"value", I(18)},
    })));
    CHECK(!dlg->evalConditionData(O({
        {"var", S("hour")},
        {"op", S("lt")},
        {"value", I(18)},
    })));
    CHECK(dlg->evalConditionData(O({{"var", S("flag")}, {"op", S("eq")}, {"value", B(true)}})));
    CHECK(dlg->evalConditionData(O({{"var", S("flag")}, {"op", S("has")}})));
    CHECK(dlg->evalConditionData(O({{"var", S("nope")}, {"op", S("missing")}})));
    CHECK(!dlg->evalConditionData(O({{"var", S("nope")}, {"op", S("eq")}, {"value", S("x")}})));

    CHECK(dlg->evalConditionData(O({
        {"all", A({
                    O({{"var", S("mood")}, {"op", S("eq")}, {"value", S("happy")}}),
                    O({{"var", S("hour")}, {"op", S("ge")}, {"value", I(18)}}),
                })},
    })));
    CHECK(dlg->evalConditionData(O({
        {"any", A({
                    O({{"var", S("mood")}, {"op", S("eq")}, {"value", S("angry")}}),
                    O({{"var", S("hour")}, {"op", S("ge")}, {"value", I(18)}}),
                })},
    })));
    CHECK(dlg->evalConditionData(O({
        {"not", O({{"var", S("flag")}, {"op", S("eq")}, {"value", B(false)}})},
    })));

    // Script predicates need a VM; without one they evaluate false.
    CHECK(!dlg->evalConditionData(O({{"script", S("missingPred")}})));

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.poolLoading") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);

    const DataValue root = poolsRoot(O({
        {"alice.greet", O({
                            {"noRepeat", I(4)},
                            {"lines", A({
                                          O({{"id", S("a1")}, {"speaker", S("alice")}, {"text", S("你好")}}),
                                          O({{"speaker", S("alice")},
                                             {"i18n", S("line.hi")},
                                             {"weight", I(2)},
                                             {"when", O({{"var", S("mood")}, {"op", S("eq")}, {"value", S("happy")}})},
                                             {"meta", O({{"expression", S("happy")}, {"voice", S("vo/a1.ogg")}})},
                                             {"tags", A({S("greet"), S("evening")})}}),
                                      })},
                        })},
    }));

    CHECK_EQ(dlg->loadPoolsFromData(root), 1);
    CHECK(dlg->hasPool("alice.greet"));
    CHECK_EQ(dlg->getPoolCount(), 1);
    CHECK_EQ(dlg->getPoolId(0), std::string("alice.greet"));
    // Missing id -> auto-generated "poolId.序号".
    CHECK(dlg->playLineWithParams("alice.greet.2", {}));
    CHECK(dlg->getLastPoolsError().empty());

    // Same pool id replaces the previous definition (no duplication).
    CHECK_EQ(dlg->loadPoolsFromData(root), 1);
    CHECK_EQ(dlg->getPoolCount(), 1);

    // Invalid root / missing pools object.
    CHECK_EQ(dlg->loadPoolsFromData(S("oops")), 0);
    CHECK(!dlg->getLastPoolsError().empty());
    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.pickLine") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);

    const DataValue root = poolsRoot(O({
        {"pool.abc", O({
                         {"lines", A({
                                       O({{"id", S("p1")}, {"speaker", S("a")}, {"text", S("one")}, {"weight", I(1)}}),
                                       O({{"id", S("p2")}, {"speaker", S("a")}, {"text", S("two")}, {"weight", I(1)}}),
                                   })},
                     })},
        {"pool.filt", O({
                          {"lines", A({
                                        O({{"id", S("f1")},
                                           {"speaker", S("a")},
                                           {"text", S("happy line")},
                                           {"when", O({{"var", S("mood")}, {"op", S("eq")}, {"value", S("happy")}})}}),
                                        O({{"id", S("f2")},
                                           {"speaker", S("a")},
                                           {"text", S("sad line")},
                                           {"when", O({{"var", S("mood")}, {"op", S("eq")}, {"value", S("sad")}})}}),
                                    })},
                      })},
        {"pool.zero",
         O({
             {"lines", A({
                           O({{"id", S("z0")}, {"speaker", S("a")}, {"text", S("zero")}, {"weight", I(0)}}),
                           O({{"id", S("z1")}, {"speaker", S("a")}, {"text", S("one")}, {"weight", I(1)}}),
                       })},
         })},
    }));
    CHECK_EQ(dlg->loadPoolsFromData(root), 3);

    // Determinism: same seed reproduces the same pick sequence.
    dlg->setRandomSeed(42);
    const std::string first = dlg->pickLineWithParams("pool.abc", {});
    CHECK(!first.empty());
    dlg->setRandomSeed(42);
    const std::string second = dlg->pickLineWithParams("pool.abc", {});
    CHECK_EQ(second, first);

    // Both lines appear across many picks (50/50 weights).
    bool sawP1 = false, sawP2 = false;
    dlg->setRandomSeed(7);
    for (int i = 0; i < 200; ++i) {
        const std::string id = dlg->pickLineWithParams("pool.abc", {});
        if (id == "p1") sawP1 = true;
        if (id == "p2") sawP2 = true;
    }
    CHECK(sawP1);
    CHECK(sawP2);

    // Unknown pool -> "".
    CHECK_EQ(dlg->pickLineWithParams("nope", {}), std::string(""));

    // when filtering by variables.
    dlg->setVarValue("mood", Dialogue::VarValue::string("happy"), "global");
    for (int i = 0; i < 10; ++i) CHECK_EQ(dlg->pickLineWithParams("pool.filt", {}), std::string("f1"));
    dlg->setVarValue("mood", Dialogue::VarValue::string("sad"), "global");
    for (int i = 0; i < 10; ++i) CHECK_EQ(dlg->pickLineWithParams("pool.filt", {}), std::string("f2"));
    dlg->clearVars("global");
    CHECK_EQ(dlg->pickLineWithParams("pool.filt", {}), std::string(""));

    // weight <= 0 lines never win.
    dlg->setRandomSeed(3);
    for (int i = 0; i < 50; ++i) CHECK_EQ(dlg->pickLineWithParams("pool.zero", {}), std::string("z1"));

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.noRepeat") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);

    // noRepeat=1 with two lines: consecutive picks must differ.
    const DataValue root = poolsRoot(O({
        {"pool.alt", O({
                         {"noRepeat", I(1)},
                         {"lines", A({
                                       O({{"id", S("n1")}, {"speaker", S("a")}, {"text", S("one")}}),
                                       O({{"id", S("n2")}, {"speaker", S("a")}, {"text", S("two")}}),
                                   })},
                     })},
        // noRepeat=2 with two lines: third pick falls back to the full pool.
        {"pool.fb", O({
                        {"noRepeat", I(2)},
                        {"lines", A({
                                      O({{"id", S("m1")}, {"speaker", S("a")}, {"text", S("one")}}),
                                      O({{"id", S("m2")}, {"speaker", S("a")}, {"text", S("two")}}),
                                  })},
                    })},
    }));
    CHECK_EQ(dlg->loadPoolsFromData(root), 2);

    dlg->setRandomSeed(11);
    std::string prev = dlg->pickLineWithParams("pool.alt", {});
    for (int i = 1; i < 40; ++i) {
        const std::string id = dlg->pickLineWithParams("pool.alt", {});
        CHECK(id != prev);
        CHECK((id == "n1" || id == "n2"));
        prev = id;
    }

    dlg->setRandomSeed(22);
    for (int i = 0; i < 5; ++i) {
        const std::string id = dlg->pickLineWithParams("pool.fb", {});
        CHECK((id == "m1" || id == "m2"));
    }

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.playLineAndMeta") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);
    Avatar* avmod = Avatar::create();
    CHECK(dlg->registerCharacter("alice", "Alice"));
    AvatarInstance* alice = avmod->newImageAvatar();
    alice->addLayer("face", nullptr, 0);
    alice->defineExpression("happy", "face=1");
    CHECK(dlg->bindAvatar("alice", alice));

    const DataValue root = poolsRoot(O({
        {"pool.play",
         O({
             {"lines",
              A({
                  O({{"id", S("pl1")},
                     {"speaker", S("alice")},
                     {"text", S("你好，{name}！")},
                     {"meta", O({{"expression", S("happy")}, {"motion", S("wave")}, {"voice", S("vo/alice.ogg")}})},
                     {"tags", A({S("greet"), S("night")})}}),
                  O({{"id", S("pl2")}, {"speaker", S("")}, {"text", S("旁白")}}),
                  O({{"id", S("pl3")}, {"speaker", S("alice")}, {"i18n", S("greet.key")}}),
              })},
         })},
    }));
    CHECK_EQ(dlg->loadPoolsFromData(root), 1);

    // Literal text: params override global vars in {var} substitution.
    dlg->setVarValue("name", Dialogue::VarValue::string("Alice"), "global");
    Vars params;
    params["name"] = Dialogue::VarValue::string("Bob");
    CHECK(dlg->playLineWithParams("pl1", params));
    CHECK_EQ(dlg->getCurrentLineId(), std::string("pl1"));
    CHECK_EQ(dlg->getFullText(), std::string("你好，Bob！"));
    CHECK_EQ(dlg->getCurrentLineMeta("voice"), std::string("vo/alice.ogg"));
    CHECK_EQ(dlg->getCurrentLineMeta("expression"), std::string("happy"));
    CHECK_EQ(dlg->getCurrentLineMeta("missing"), std::string(""));
    CHECK_EQ(dlg->getCurrentLineTags().size(), size_t(2));
    CHECK_EQ(dlg->getCurrentLineTags()[0], std::string("greet"));
    // Expression was auto-applied to the bound avatar.
    CHECK_EQ(alice->getExpression(), std::string("happy"));
    CHECK_EQ(alice->getMotion(), std::string("wave"));

    // Narration line (empty speaker).
    CHECK(dlg->playLineWithParams("pl2", {}));
    CHECK_EQ(dlg->getSpeakerId(), std::string(""));
    CHECK_EQ(dlg->getFullText(), std::string("旁白"));

    // i18n key resolution through a real I18n instance.
    I18n* i18n = I18n::create();
    CHECK(i18n->loadFromJson("en", R"({"greet": { "key": "Hi, {name}!" } })"));
    CHECK(i18n->setLanguage("en"));
    dlg->setVarValue("name", Dialogue::VarValue::string("Carol"), "global");
    CHECK(dlg->playLineWithParams("pl3", {}));
    CHECK_EQ(dlg->getFullText(), std::string("Hi, Carol!"));

    resetDialogue(dlg);
    dlg->bindAvatar("alice", nullptr);  // 解除绑定，避免后续 reset 触碰已释放的 avatar
    alice->release();
    delete alice;
}

TEST_CASE("dialogueProc.playPool") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);

    const DataValue root = poolsRoot(O({
        {"pool.greet", O({
                           {"lines", A({
                                         O({{"id", S("g1")}, {"speaker", S("npc")}, {"text", S("Hi {name}")}}),
                                     })},
                       })},
    }));
    CHECK_EQ(dlg->loadPoolsFromData(root), 1);

    Vars params;
    params["name"] = Dialogue::VarValue::string("Alex");
    CHECK(dlg->playPoolWithParams("pool.greet", params));
    CHECK_EQ(dlg->getCurrentLineId(), std::string("g1"));
    CHECK_EQ(dlg->getFullText(), std::string("Hi Alex"));
    CHECK(!dlg->playPoolWithParams("pool.gone", params));

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.dnutParser") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);

    const std::string src =
        "schema \"eve.dnut\"\n"
        "version 1\n"
        "pool alice.greet noRepeat=4 {\n"
        "    when mood == \"happy\" && hour >= 18 {\n"
        "        alice: \"晚上好，{name}！\" weight=2 meta(expression=\"happy\", motion=\"wave\") "
        "tags=[\"greet\",\"evening\"]\n"
        "        alice: \"你来啦。\" weight=1\n"
        "    }\n"
        "    when mood == \"shy\" {\n"
        "        alice: \"那个……你好。\" meta(expression=\"shy\")\n"
        "    }\n"
        "    - \"远处传来钟声。\"\n"
        "}\n";

    std::vector<ConversationDiagnostic> diagnostics;
    auto                                parsed = parseDnutDocument(src, "pools.dnut", diagnostics);
    REQUIRE(parsed.ok());
    DnutDocument document = std::move(parsed).takeValue();
    CHECK_EQ(dlg->loadPoolsFromData(document.poolRoot), 1);
    CHECK(dlg->hasPool("alice.greet"));
    CHECK(dlg->getLastPoolsError().empty());

    dlg->setVarValue("mood", Dialogue::VarValue::string("happy"), "global");
    dlg->setVarValue("hour", Dialogue::VarValue::integer(20), "global");

    // Line 1: compiled when = all(mood eq happy, hour ge 18); meta + tags.
    CHECK(dlg->playLineWithParams("alice.greet.1", {}));
    CHECK_EQ(dlg->getCurrentLineMeta("expression"), std::string("happy"));
    CHECK_EQ(dlg->getCurrentLineMeta("motion"), std::string("wave"));
    CHECK_EQ(dlg->getCurrentLineTags().size(), size_t(2));
    CHECK_EQ(dlg->getCurrentLineTags()[0], std::string("greet"));

    // When filtering: hour < 18 -> only the narration line matches.
    dlg->setVarValue("hour", Dialogue::VarValue::integer(8), "global");
    for (int i = 0; i < 10; ++i) {
        const std::string id = dlg->pickLineWithParams("alice.greet", {});
        CHECK_EQ(id, std::string("alice.greet.4"));
    }

    // Narration line: empty speaker.
    CHECK(dlg->playLineWithParams("alice.greet.4", {}));
    CHECK_EQ(dlg->getSpeakerId(), std::string(""));
    CHECK_EQ(dlg->getFullText(), std::string("远处传来钟声。"));

    // shy branch: line 3 命中；旁白行 4 无条件也合格，但 noRepeat 历史会先排除它。
    dlg->setVarValue("mood", Dialogue::VarValue::string("shy"), "global");
    dlg->setVarValue("hour", Dialogue::VarValue::integer(20), "global");
    const std::string firstShy = dlg->pickLineWithParams("alice.greet", {});
    CHECK_EQ(firstShy, std::string("alice.greet.3"));
    for (int i = 0; i < 9; ++i) {
        const std::string id = dlg->pickLineWithParams("alice.greet", {});
        CHECK((id == "alice.greet.3" || id == "alice.greet.4"));
    }

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.dnutAttrsAndErrors") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);

    const std::string src =
        "schema \"eve.dnut\"\n"
        "version 1\n"
        "pool x {\n"
        "    alice: \"hi\" weight=3 i18n=\"line.hi\" id=\"custom\"\n"
        "}\n";
    std::vector<ConversationDiagnostic> diagnostics;
    auto                                parsed = parseDnutDocument(src, "a.dnut", diagnostics);
    REQUIRE(parsed.ok());
    DnutDocument document = std::move(parsed).takeValue();
    CHECK_EQ(dlg->loadPoolsFromData(document.poolRoot), 1);
    CHECK(dlg->playLineWithParams("custom", {}));
    CHECK_EQ(dlg->getCurrentLineId(), std::string("custom"));
    // weight=3 参与随机（相对池内其它行权重更高）；这里单行池直接命中。
    CHECK_EQ(dlg->pickLineWithParams("x", {}), std::string("custom"));

    // 语法错误：行号与路径出现在 getLastPoolsError。
    CHECK(!parseDnutDocument("schema \"eve.dnut\"\nversion 1\npool x {\n  alice: hi\n}\n", "bad.dnut", diagnostics));
    REQUIRE(!diagnostics.empty());
    CHECK(diagnostics.back().message.find("bad.dnut:4") != std::string::npos);

    // 未知属性。
    diagnostics.clear();
    CHECK(!parseDnutDocument("schema \"eve.dnut\"\nversion 1\npool x { alice: \"hi\" bogus=1 }\n", "bad2.dnut",
                             diagnostics));
    CHECK(!diagnostics.empty());

    // 未闭合的 pool。
    diagnostics.clear();
    CHECK(!parseDnutDocument("schema \"eve.dnut\"\nversion 1\npool x {\n  alice: \"hi\"\n", "bad3.dnut", diagnostics));
    REQUIRE(!diagnostics.empty());
    CHECK(diagnostics.back().message.find("bad3.dnut") != std::string::npos);

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.sceneVarsAutoClear") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);
    dlg->setVarValue("keep", Dialogue::VarValue::integer(1), "global");

    eve::scene::Scene* scn = eve::scene::Scene::create();
    scn->mountAs("dialogueSceneA", eve::scene::node("root")).ignore("test setup");
    scn->mountAs("dialogueSceneB", eve::scene::node("root")).ignore("test setup");
    scn->select("dialogueSceneA");

    dlg->update(0.016f);  // record current scene name
    dlg->setVarValue("tmp", Dialogue::VarValue::integer(2), "scene");
    CHECK(dlg->hasVar("tmp", "scene"));

    scn->select("dialogueSceneB");
    dlg->update(0.016f);  // scene changed -> scene vars cleared
    CHECK(!dlg->hasVar("tmp", "scene"));
    CHECK(dlg->hasVar("keep", "global"));

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.stateCaptureRestoreRoundtrip") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);
    dlg->setVarValue("hp", Dialogue::VarValue::integer(100), "global");
    dlg->setVarValue("mood", Dialogue::VarValue::string("happy"), "scene");
    dlg->setRandomSeed(12345);
    dlg->say("hero", "Hello world");
    dlg->skipTyping();
    CHECK(dlg->isWaitingAdvance());

    eve::StateValue captured;
    REQUIRE(dlg->captureState(captured));

    // Mutate, then restore.
    dlg->reset();
    dlg->setVarValue("hp", Dialogue::VarValue::integer(1), "global");

    std::string err;
    CHECK(dlg->restoreState(captured, &err));
    CHECK(err.empty());
    CHECK_EQ(dlg->getVarInt("hp", 0, "global"), 100);
    CHECK_EQ(dlg->getVarString("mood", "", "scene"), std::string("happy"));
    CHECK_EQ(dlg->getRandomSeed(), 12345);
    CHECK(dlg->isWaitingAdvance());
    CHECK_EQ(dlg->getFullText(), std::string("Hello world"));
    CHECK_EQ(dlg->getSpeakerId(), std::string("hero"));

    resetDialogue(dlg);
}

TEST_CASE("dialogueProc.stateRestoreRejectsMalformed") {
    Dialogue* dlg = Dialogue::create();
    resetDialogue(dlg);

    eve::StateValue bad = eve::StateValue::object();
    bad.set("phase", eve::StateValue::string("no_such_phase"));
    std::string err;
    CHECK(!dlg->restoreState(bad, &err));
    CHECK(!err.empty());

    eve::StateValue missing = eve::StateValue::object();
    CHECK(!dlg->restoreState(missing, &err));
    CHECK(!err.empty());

    resetDialogue(dlg);
}
}  // namespace test_dialogue_proc
#endif
