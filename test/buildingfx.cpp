#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "building/Building.h"
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "buildingfx/BuildingFx.h"
#include "common/ECS.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Mesh.h"

#include <cmath>

using namespace eve::building;
using namespace eve::buildingfx;

TEST_CASE("buildingfx.sync.lifecycle") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition hut;
    hut.id = "hut";
    hut.displayName = "Hut";
    hut.footprintW = 2;
    hut.footprintH = 1;
    hut.renderMode = "2d";
    hut.visual2d["colorR"] = "0.8";
    hut.visual2d["colorG"] = "0.4";
    hut.visual2d["colorB"] = "0.2";
    BuildingRegistry::registerBuilding(hut);

    PlacementWorld world(8, 8, 32.f);
    BuildingFx *fx = BuildingFx::create();
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
    ramp.id = "ramp";
    ramp.renderMode = "3d";
    ramp.snapMode = "free";
    BuildingRegistry::registerBuilding(ramp);

    PlacementWorld world(8, 8, 1.f);
    world.setGridPlane("xz");
    PlacementSystem::registerSurfaceProvider(
        "ramp.surface", [](const PlacementWorld &, float x, float y) {
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = 1.5f;
            hit.worldZ = y;
            hit.normalY = 1.f;
            hit.normalZ = 1.f;
            hit.tangentX = 1.f;
            hit.surfaceId = "ramp.mesh";
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });

    Ghost ghost;
    ghost.setBuildingId(ramp.id);
    ghost.setFromSurface(&world, "ramp.surface", 2.25f, 3.75f);
    REQUIRE(ghost.validate(&world));
    REQUIRE(world.placeGhost(&ghost) > 0);

    BuildingFx *fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);

    auto view = ecs::View<eve::graphics::Renderable3D,
                          eve::graphics::Renderable3D::Transform3D>();
    int visualCount = 0;
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
    wall.id = "wall";
    wall.placementKind = "edge";
    wall.renderMode = "3d";
    wall.visual3d["height"] = "3";
    wall.visual3d["thickness"] = "0.5";
    wall.visual3d["variant.end.height"] = "4";
    BuildingRegistry::registerBuilding(wall);

    PlacementWorld world(4, 4, 10.f);
    world.setGridPlane("xz");
    REQUIRE(world.placeEdge("wall", 1, 2, "north") > 0);

    BuildingFx *fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);

    int visualCount = 0;
    {
        auto view = ecs::View<eve::graphics::Renderable3D,
                              eve::graphics::Renderable3D::Transform3D>();
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
    visualCount = 0;
    auto updatedView = ecs::View<eve::graphics::Renderable3D,
                                 eve::graphics::Renderable3D::Transform3D>();
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
    wall.id = "curve-visual-wall";
    wall.placementKind = "edge";
    wall.connectionGroup = "curve-visual-wall";
    wall.channel = "walls";
    wall.renderMode = "3d";
    wall.visual3d["variant.corner.mesh"] = "wall/curve-corner.mesh";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(10, 10, 10.f);
    world.setGridPlane("xz");
    const std::vector<PlacementSystem::EdgeCurvePoint> controls{
        {1.f, 1.f}, {1.f, 6.f}, {7.f, 6.f}, {7.f, 1.f}};
    auto placed = PlacementSystem::placeEdgeCubicBezier(&world, wall.id, controls, 24);
    REQUIRE(placed.ok());

    BuildingFx *fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    CHECK_EQ(fx->getVisualCount(&world),
             static_cast<int>(placed.value().instanceIds.size()));
    CHECK_EQ(fx->getCurveGroupCount(&world), 1);
    CHECK_EQ(fx->getContinuousCurveVisualCount(&world), 0);
    CHECK_EQ(fx->getCurveVisualFallbackReason(&world, placed.value().instanceIds.front()),
             "graphics_unavailable");
    int cornerCount = 0;
    for (int instanceId : placed.value().instanceIds) {
        if (fx->getVisualVariant(&world, instanceId) != "corner") continue;
        CHECK_EQ(fx->getVisualResource(&world, instanceId), "wall/curve-corner.mesh");
        ++cornerCount;
    }
    CHECK(cornerCount > 0);
    auto continuousMesh = BuildingFx::buildEdgeCurveGroupMeshForInstance(
        world, placed.value().instanceIds.front(), 2.f, 3.f, 4.f);
    REQUIRE(continuousMesh.ok());
    CHECK_EQ(continuousMesh.value().sampleCount, 25);
    REQUIRE(world.removeBuilding(placed.value().instanceIds.front()));
    fx->sync(&world);
    CHECK_EQ(fx->getCurveGroupCount(&world), 0);
    CHECK_EQ(fx->getContinuousCurveVisualCount(&world), 0);
    CHECK_EQ(fx->getCurveVisualFallbackReason(&world, placed.value().instanceIds.back()),
             "curve_group_not_found");
    CHECK(!BuildingFx::buildEdgeCurveGroupMeshForInstance(
               world, placed.value().instanceIds.back(), 2.f, 3.f, 4.f)
               .ok());
    REQUIRE(fx->detach(&world));
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.edgeCurveMeshBuildsContinuousWorldSpaceExtrusion") {
    PlacementWorld world(10, 10, 10.f);
    world.setOrigin(3.f, 7.f);
    world.setGridPlane("xz");
    const std::vector<BuildingFx::CurveControlPoint> controls{
        {1.f, 1.f}, {1.f, 6.f}, {7.f, 6.f}, {7.f, 1.f}};
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
    wall.id = "curve-preview-wall";
    wall.placementKind = "edge";
    wall.channel = "walls";
    wall.renderMode = "3d";
    wall.visual3d["thickness"] = "0.5";
    wall.visual3d["height"] = "3";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(10, 10, 10.f);
    world.setGridPlane("xz");
    BuildingFx *fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    const std::vector<BuildingFx::CurveControlPoint> controls{
        {1.f, 1.f}, {1.f, 6.f}, {7.f, 6.f}, {7.f, 1.f}};
    auto updated =
        fx->updateEdgeCurvePreview(&world, wall.id, controls, 24, 2);
    REQUIRE(updated.ok());
    CHECK(fx->hasEdgeCurvePreview(&world));
    CHECK_EQ(fx->getEdgeCurvePreviewFallbackReason(&world),
             "graphics_unavailable");
    fx->clearEdgeCurvePreview(&world);
    CHECK(!fx->hasEdgeCurvePreview(&world));
    CHECK_EQ(fx->getEdgeCurvePreviewFallbackReason(&world),
             "curve_preview_not_active");
    REQUIRE(fx->detach(&world));
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.edgeCurveCommitsAndExtrudesCustomSurfaceFrames") {
    BuildingRegistry::clear();
    BuildingDefinition wall;
    wall.id = "surface-curve-wall";
    wall.placementKind = "edge";
    wall.channel = "walls";
    wall.renderMode = "3d";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(12, 12, 10.f);
    world.setGridPlane("xz");
    PlacementSystem::registerSurfaceProvider(
        "curve.terrain", [](const PlacementWorld &, float x, float y) {
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = x * 0.1f;
            hit.worldZ = y;
            hit.normalX = -0.1f;
            hit.normalY = 1.f;
            hit.surfaceId = "terrain.curve";
            hit.surfaceRevision = 12;
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    const std::vector<PlacementSystem::EdgeCurvePoint> controls{
        {1.f, 1.f}, {1.f, 6.f}, {7.f, 6.f}, {7.f, 1.f}};
    auto placed = PlacementSystem::placeEdgeCubicBezierOnSurface(
        &world, wall.id, controls, 16, "curve.terrain");
    REQUIRE(placed.ok());
    auto group = world.edgeCurveGroupForInstance(placed.value().instanceIds.front());
    REQUIRE(group.ok());
    CHECK_EQ(group.value().surfaceProviderName, "curve.terrain");
    CHECK_EQ(group.value().surfaceId, "terrain.curve");
    CHECK_EQ(group.value().surfaceRevision, uint64_t{12});
    CHECK_EQ(group.value().surfaceSamples.size(), size_t{17});
    auto mesh = BuildingFx::buildEdgeCurveGroupMeshForInstance(
        world, placed.value().instanceIds.front(), 1.f, 3.f);
    REQUIRE(mesh.ok());
    CHECK_EQ(mesh.value().sampleCount, 17);
    CHECK(mesh.value().positions[1] <
          mesh.value().positions[mesh.value().positions.size() - 23]);

    PlacementWorld rejectedWorld(12, 12, 10.f);
    rejectedWorld.setGridPlane("xz");
    PlacementSystem::registerSurfaceProvider(
        "curve.split", [](const PlacementWorld &, float x, float y) {
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = 0.f;
            hit.worldZ = y;
            hit.surfaceId = x < 40.f ? "terrain.left" : "terrain.right";
            hit.surfaceRevision = 1;
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    CHECK(!PlacementSystem::placeEdgeCubicBezierOnSurface(
               &rejectedWorld, wall.id, controls, 16, "curve.split")
               .ok());
    CHECK_EQ(rejectedWorld.getBuildingCount(), 0);
    CHECK_EQ(rejectedWorld.getEdgeCurveGroupCount(), 0);
    PlacementSystem::unregisterSurface("curve.terrain");
    PlacementSystem::unregisterSurface("curve.split");
    BuildingRegistry::clear();
}

TEST_CASE("buildingfx.sessionCurvePreviewFollowsSurfaceAndKeepsLastValidDraft") {
    BuildingRegistry::clear();
    BuildingDefinition wall;
    wall.id = "session-surface-preview-wall";
    wall.placementKind = "edge";
    wall.channel = "walls";
    wall.renderMode = "3d";
    wall.visual3d["thickness"] = "0.5";
    wall.visual3d["height"] = "3";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(10, 10, 10.f);
    world.setGridPlane("xz");
    PlacementSystem::registerSurfaceProvider(
        "preview.terrain", [](const PlacementWorld &, float x, float y) {
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = x * 0.1f;
            hit.worldZ = y;
            hit.normalX = -0.1f;
            hit.normalY = 1.f;
            hit.surfaceId = "terrain.preview";
            hit.surfaceRevision = 8;
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    PlacementSession session;
    REQUIRE(session.startPlacement(&world, wall.id));
    REQUIRE(static_cast<int>(session.beginEdgeCurve(1.f, 1.f)) ==
            static_cast<int>(EdgePathUpdateStatus::Updated));
    REQUIRE(static_cast<int>(session.appendEdgeCurveControlPoint(1.f, 6.f)) ==
            static_cast<int>(EdgePathUpdateStatus::Updated));
    REQUIRE(static_cast<int>(session.appendEdgeCurveControlPoint(7.f, 6.f)) ==
            static_cast<int>(EdgePathUpdateStatus::Updated));
    REQUIRE(static_cast<int>(session.appendEdgeCurveControlPoint(7.f, 1.f)) ==
            static_cast<int>(EdgePathUpdateStatus::Updated));
    BuildingFx *fx = BuildingFx::create();
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
    post.id = "visual-post";
    post.placementKind = "corner";
    post.channel = "posts";
    post.renderMode = "3d";
    post.visual3d["width"] = "0.25";
    post.visual3d["depth"] = "0.35";
    post.visual3d["height"] = "3";
    BuildingRegistry::registerBuilding(post);

    PlacementWorld world(4, 4, 10.f);
    world.setGridPlane("xz");
    REQUIRE(world.placeCorner(post.id, 2, 3) > 0);
    BuildingFx *fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);

    int visualCount = 0;
    auto view = ecs::View<eve::graphics::Renderable3D,
                          eve::graphics::Renderable3D::Transform3D>();
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
    prop.id = "visual-free-prop";
    prop.placementKind = "free";
    prop.snapMode = "free";
    prop.rotationMode = "free";
    prop.freeRadiusCells = 0.3f;
    prop.renderMode = "3d";
    prop.visual3d["height"] = "2";
    BuildingRegistry::registerBuilding(prop);
    PlacementWorld world(4, 4, 10.f);
    world.setGridPlane("xz");
    REQUIRE(world.placeFree(prop.id, 12.5f, 23.75f, 1.f, 27.f) > 0);
    BuildingFx *fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    int count = 0;
    auto view = ecs::View<eve::graphics::Renderable3D,
                          eve::graphics::Renderable3D::Transform3D>();
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
    prop.id = "visual-free-obb";
    prop.placementKind = "free";
    prop.snapMode = "free";
    prop.rotationMode = "free";
    prop.freeFootprintWidthCells = 2.f;
    prop.freeFootprintHeightCells = 0.5f;
    prop.freeFootprintVertices = {-1.f, 0.f, 0.f, -0.25f,
                                  1.f, 0.f, 0.f, 0.25f};
    prop.renderMode = "3d";
    BuildingRegistry::registerBuilding(prop);
    PlacementWorld world(4, 4, 10.f);
    world.setGridPlane("xz");
    REQUIRE(world.placeFree(prop.id, 12.5f, 23.75f, 1.f, 27.f) > 0);
    BuildingFx *fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    int count = 0;
    auto view = ecs::View<eve::graphics::Renderable3D,
                          eve::graphics::Renderable3D::Transform3D>();
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
    wall.id = "resource-wall";
    wall.placementKind = "edge";
    wall.connectionGroup = "resource-boundary";
    wall.channel = "boundary";
    wall.renderMode = "3d";
    wall.visual3d["mesh"] = "wall/base.mesh";
    wall.visual3d["variant.end.mesh"] = "wall/end.mesh";
    wall.visual3d["variant.end.rotationDeg"] = "15";
    wall.visual3d["variant.corner.mesh"] = "wall/corner.mesh";
    wall.visual3d["variant.corner.mask.34.mesh"] = "wall/corner-34.mesh";
    wall.visual3d["variant.tee.mesh"] = "wall/tee.mesh";
    wall.visual3d["variant.cross.mesh"] = "wall/cross.mesh";
    BuildingRegistry::registerBuilding(wall);

    struct Case {
        int mask;
        const char *variant;
        const char *resource;
    };
    const Case cases[] = {{0, "isolated", "wall/base.mesh"},
                          {1, "end", "wall/end.mesh"},
                          {3, "straight", "wall/base.mesh"},
                          {34, "corner", "wall/corner-34.mesh"},
                          {42, "tee", "wall/tee.mesh"},
                          {43, "cross", "wall/cross.mesh"}};
    BuildingFx *fx = BuildingFx::create();
    for (const Case &test : cases) {
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
    wall.id = "dynamic-wall";
    wall.placementKind = "edge";
    wall.renderMode = "3d";
    wall.visual3d["mesh"] = "wall/base.mesh";
    wall.visual3d["variant.end.mesh"] = "wall/end.mesh";
    wall.visual3d["variant.end.rotationDeg"] = "15";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(8, 8, 10.f);
    world.setGridPlane("xz");
    const int center = world.placeEdge(wall.id, 2, 2, "north");
    REQUIRE(center > 0);
    BuildingFx *fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    CHECK_EQ(fx->getVisualVariant(&world, center), "isolated");
    CHECK_EQ(fx->getVisualResource(&world, center), "wall/base.mesh");

    REQUIRE(world.placeEdge(wall.id, 1, 2, "north") > 0);
    fx->sync(&world);
    CHECK_EQ(fx->getVisualVariant(&world, center), "end");
    CHECK_EQ(fx->getVisualResource(&world, center), "wall/end.mesh");
    bool foundCenter = false;
    auto view = ecs::View<eve::graphics::Renderable3D,
                          eve::graphics::Renderable3D::Transform3D>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [transform] = *it;
        if (std::abs(transform->x - 25.f) > 0.0001f ||
            std::abs(transform->z - 20.f) > 0.0001f)
            continue;
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
    wall.id = "resolved-wall";
    wall.placementKind = "edge";
    wall.renderMode = "3d";
    wall.visual3d["mesh"] = "wall/resolved.mesh";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(4, 4, 1.f);
    const int instanceId = world.placeEdge(wall.id, 1, 1, "north");
    REQUIRE(instanceId > 0);
    eve::graphics::Mesh mesh;
    int resolverCalls = 0;
    BuildingFx *fx = BuildingFx::create();
    fx->setMeshResolver([&](const std::string &resourceId) {
        ++resolverCalls;
        return resourceId == "wall/resolved.mesh" ? &mesh : nullptr;
    });
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    CHECK(resolverCalls > 0);
    CHECK_EQ(fx->getVisualResource(&world, instanceId), "wall/resolved.mesh");
    CHECK(fx->getVisualFallbackReason(&world, instanceId).empty());
    bool usesResolvedMesh = false;
    auto view = ecs::View<eve::graphics::Renderable3D,
                          eve::graphics::Renderable3D::MeshRenderer>();
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
    CHECK_EQ(static_cast<int>(session.previewRectangle(0, 1, 2, 1)),
             static_cast<int>(AreaExecuteStatus::Rejected));

    BuildingFx *fx = BuildingFx::create();
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
    marker.id = "floor-marker";
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
    BuildingFx *fx = BuildingFx::create();
    REQUIRE(fx->attach(&world));
    fx->sync(&world);
    bool found = false;
    auto view = ecs::View<eve::graphics::Renderable3D,
                          eve::graphics::Renderable3D::Transform3D>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [transform] = *it;
        if (std::abs(transform->x - 2.f) < 0.0001f &&
            std::abs(transform->z - 1.f) < 0.0001f &&
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
