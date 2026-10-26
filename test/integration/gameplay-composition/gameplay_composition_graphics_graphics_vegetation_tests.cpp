// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "cases": {
    "render-offscreen": [
      "graphics.backendParity.primitive2DAnd3DReadback",
      "graphics.backendParity.vegetationFieldGizmoReadback",
      "graphics.raytracing.probeAndUnsupportedOps",
      "graphics.vegetation.extras_gpu_atlas_tracks_revision_and_rejects_half_overflow",
      "graphics.vegetation.gpu_field_binding_is_coherent_stale_safe_and_atomic",
      "graphics.vegetation.prebaked_layers_upload_bind_and_release_atomically",
      "graphics.vegetation.color_mask_remaps_after_gpu_filtering",
      "graphics.vegetation.global_colors_matches_tve_linear_fragment_formula",
      "graphics.vegetation.detail_layer_matches_tve_replace_formula",
      "graphics.vegetation.normal_gpu_matches_filtered_cpu_reference"
    ],
    "script-binding": [
      "GraphicsPrimitives.scriptSpatialShapesAndReleasedProxy"
    ],
    "platform-file": [
      "particles.skin.filterByIndexAndClearFilter",
      "particles.skin.emitFromSkinRespectsBuffer",
      "particles.attach.cesiumManBoneByNameIfPresent",
      "particles.skin.planeXzYzCesiumMan",
      "particles.skin.clearRestoresBoneOriginSpawn",
      "particles.skin.surfaceEmitCesiumMan",
      "particles.attach.clipSampleMovesEmitter",
      "particles.skin.emptyBoneFilterFallsBack"
    ]
  },
  "module": "graphics",
  "modules": [
    "animation",
    "effects",
    "graphics",
    "graphics_editing",
    "graphics_raytracing",
    "map_editing",
    "npc_ai",
    "particles"
  ],
  "origins": [
    "graphics_authoring_projection.cpp",
    "graphics_primitive_cache.cpp",
    "graphics_raytracing.cpp",
    "graphics_vegetation.cpp",
    "graphics_vegetation_gizmo.cpp",
    "identity_result_audit.cpp",
    "kaykit_combat_assets.cpp",
    "map_tile_editing.cpp",
    "npc_ai_navigation.cpp",
    "particles_attach_extra.cpp",
    "particles_attach_more.cpp",
    "particles_attach_skin.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_authoring_projection)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/RenderTransformProjection.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_authoring_projection.cpp
namespace test_graphics_authoring_projection {
TEST_CASE("graphics.authoring.bounds_use_vertices_and_clear_after_invalid_upload") {
    eve::graphics::Mesh mesh;
    REQUIRE(!mesh.localBounds().ok());
    mesh.boundsRadius = 10.f;
    REQUIRE(!mesh.localBounds().ok());
    const float vertices[]{-2.f, 4.f, 1.f, 3.f, -1.f, 7.f, 0.f, 2.f, -5.f};
    mesh.computeBounds(vertices, 3);
    auto bounds = mesh.localBounds();
    REQUIRE(bounds.ok());
    REQUIRE_EQ(bounds.value()[0], -2.f);
    REQUIRE_EQ(bounds.value()[1], -1.f);
    REQUIRE_EQ(bounds.value()[2], -5.f);
    REQUIRE_EQ(bounds.value()[3], 3.f);
    REQUIRE_EQ(bounds.value()[4], 4.f);
    REQUIRE_EQ(bounds.value()[5], 7.f);
    const float invalid[]{std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f};
    mesh.computeBounds(invalid, 1);
    REQUIRE(!mesh.localBounds().ok());
    mesh.computeBounds(vertices, 3);
    mesh.computeBounds(nullptr, 0);
    REQUIRE(!mesh.localBounds().ok());
}

TEST_CASE("graphics.authoring.scene_link_preserves_reflection_shear_and_direct_setter_mode") {
    using namespace eve::graphics;
    auto created = eve::scene::SceneHost::createHost("authoring-render-proof");
    REQUIRE(created.ok());
    auto *host = created.value();
    host->setTree(eve::scene::node("parent", {eve::scene::node("child")}));
    auto parent = host->findById("parent"), child = host->findById("child");
    REQUIRE(parent.ok());
    REQUIRE(child.ok());
    parent.value()->sx   = -2.f;
    parent.value()->sy   = 3.f;
    parent.value()->sz   = 0.5f;
    parent.value()->yaw  = 0.7f;
    child.value()->pitch = 0.4f;
    child.value()->yaw   = 0.5f;
    child.value()->x     = 2.f;
    auto *renderable     = Renderable3D::create();
    REQUIRE(host->linkRenderable3D("child", renderable));
    host->markSubtreeDirtyById("parent");
    eve::scene::TransformSystem::updateHost(host);
    const auto expected = child.value()->world;
    REQUIRE(renderable->transform()->hasLinkedWorld);
    const auto projected = detail::modelFromTransform(*renderable->transform());
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) REQUIRE(std::abs(projected[c][r] - expected[c][r]) < 1e-6f);
    REQUIRE(glm::determinant(projected) < 0.f);
    const float stretch = detail::sphereStretch(*renderable->transform());
    REQUIRE(stretch >= glm::length(glm::vec3(expected[0])));
    REQUIRE(stretch >= glm::length(glm::vec3(expected[1])));
    REQUIRE(stretch >= glm::length(glm::vec3(expected[2])));
    renderable->setPosition(8.f, 9.f, 10.f);
    REQUIRE(!renderable->transform()->hasLinkedWorld);
    REQUIRE_EQ(detail::modelFromTransform(*renderable->transform())[3][0], 8.f);
    host->markSubtreeDirtyById("child");
    eve::scene::TransformSystem::updateHost(host);
    REQUIRE(renderable->transform()->hasLinkedWorld);
    ecs::DestroyEntity(renderable);
    host->markSubtreeDirtyById("child");
    eve::scene::TransformSystem::updateHost(host);
    ecs::DestroyEntity(host);
}
}  // namespace test_graphics_authoring_projection
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_primitive_cache)
#include "PrivateTestScope.h"
#include <array>
#include <simplesquirrel/simplesquirrel.hpp>
#include "GraphicsParitySupport.h"
#include "common/Module.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/PrimitiveScene.h"
#include "graphics/VegetationField.h"
#include "graphics/editing/PrimitiveGizmoRenderer.h"
#include "graphics/editing/VegetationFieldGizmo.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_primitive_cache.cpp
namespace test_graphics_primitive_cache {
// graphics_primitive_cache.cpp
using namespace eve::graphics;

TEST_CASE("GraphicsPrimitives.sceneCacheOnlyRebuildsUpdatedSlots") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D descriptor;
    descriptor.geometry   = PrimitiveSphere3D{};
    descriptor.paint.mode = PaintMode::Stroke;
    std::vector<PrimitiveHandle> handles;
    for (int index = 0; index < 1024; ++index) {
        auto added = scene.add(descriptor);
        REQUIRE(added.ok());
        handles.push_back(added.value());
    }
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    PrimitiveSceneCanvas3D canvas(context);
    scene.render(canvas);
    REQUIRE_EQ(canvas.statistics().cacheHits, 0u);
    REQUIRE_EQ(canvas.commands().size(), 3072u);
    canvas.reset();
    scene.render(canvas);
    REQUIRE_EQ(canvas.statistics().cacheHits, 1024u);
    descriptor.geometry = PrimitiveSphere3D{{0.f, 0.f, 0.f}, 2.f, 16};
    REQUIRE(scene.update(handles[500], descriptor).ok());
    canvas.reset();
    scene.render(canvas);
    REQUIRE_EQ(canvas.statistics().cacheHits, 1023u);
    REQUIRE_EQ(canvas.commands()[1500].points.size(), 16u);
}

TEST_CASE("GraphicsPrimitives.sceneCacheReusesGeometryAcrossCameras") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D descriptor;
    descriptor.geometry       = PrimitiveSphere3D{};
    descriptor.paint.mode     = PaintMode::Stroke;
    descriptor.transform[3].x = 2.f;
    auto handle               = scene.add(descriptor);
    REQUIRE(handle.ok());
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    PrimitiveSceneCanvas3D first(context);
    scene.render(first);
    CHECK_EQ(first.statistics().cacheHits, 0u);
    context.viewportSize = {1600, 900};
    PrimitiveSceneCanvas3D second(context);
    glm::mat4              parent(1.f);
    parent[3].x = 3.f;
    second.concat(parent);
    scene.render(second);
    CHECK_EQ(second.statistics().cacheHits, 1u);
    REQUIRE_EQ(second.commands().size(), first.commands().size());
    REQUIRE(!second.commands().empty());
    CHECK_EQ(second.commands()[0].points, first.commands()[0].points);
    CHECK_EQ(second.commands()[0].transform[3].x, 5.f);
    CHECK_EQ(first.commands()[0].transform[3].x, 2.f);
    // Removing the owner cannot invalidate a frame's copied geometry.
    REQUIRE(scene.remove(handle.value()).ok());
    CHECK(!second.commands()[0].points.empty());
}

TEST_CASE("GraphicsPrimitives.sceneCacheInvalidationIsTransactional") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D descriptor;
    descriptor.geometry   = PrimitiveSphere3D{};
    descriptor.paint.mode = PaintMode::Stroke;
    auto result           = scene.add(descriptor);
    REQUIRE(result.ok());
    const auto       handle = result.value();
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    PrimitiveSceneCanvas3D canvas(context);
    scene.render(canvas);
    auto invalid               = descriptor;
    invalid.paint.stroke.width = -1.f;
    REQUIRE(!scene.update(handle, invalid).ok());
    const std::array rejected{PrimitiveBatchUpdate{handle, descriptor},
                              PrimitiveBatchUpdate{PrimitiveHandle{}, descriptor}};
    REQUIRE(!scene.updateMany(rejected).ok());
    canvas.reset();
    scene.render(canvas);
    CHECK_EQ(canvas.statistics().cacheHits, 1u);
    descriptor.transform[3].x = 7.f;
    const std::array accepted{PrimitiveBatchUpdate{handle, descriptor}};
    REQUIRE(scene.updateMany(accepted).ok());
    canvas.reset();
    scene.render(canvas);
    CHECK_EQ(canvas.statistics().cacheHits, 0u);
    REQUIRE(!canvas.commands().empty());
    CHECK_EQ(canvas.commands()[0].transform[3].x, 7.f);
    descriptor.visible = false;
    REQUIRE(scene.update(handle, descriptor).ok());
    canvas.reset();
    scene.render(canvas);
    CHECK(canvas.commands().empty());
    scene.clear();
    descriptor.visible = true;
    auto replacement   = scene.add(descriptor);
    REQUIRE(replacement.ok());
    CHECK(scene.isStale(handle));
    scene.render(canvas);
    CHECK_EQ(canvas.statistics().cacheHits, 0u);
}

TEST_CASE("GraphicsPrimitives.sceneCacheRespectsWholePrimitiveBudget") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D descriptor;
    descriptor.geometry   = PrimitiveSphere3D{};
    descriptor.paint.mode = PaintMode::Stroke;
    auto handle           = scene.add(descriptor);
    REQUIRE(handle.ok());
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    PrimitiveSceneCanvas3D small(context, 2);
    REQUIRE(!scene.tryRender(small).ok());
    CHECK(small.commands().empty());
    CHECK_EQ(small.statistics().droppedCommands, 3u);
    PrimitiveSceneCanvas3D enough(context, 3);
    scene.render(enough);
    CHECK_EQ(enough.commands().size(), 3u);
    CHECK_EQ(enough.statistics().cacheHits, 1u);
    REQUIRE(!scene.tryRender(enough).ok());
    CHECK_EQ(enough.commands().size(), 3u);
    CHECK_EQ(enough.statistics().droppedCommands, 3u);
    descriptor.paint.mode = PaintMode::Fill;
    REQUIRE(scene.update(handle.value(), descriptor).ok());
    PrimitiveSceneCanvas3D filled(context);
    scene.render(filled);
    REQUIRE(!filled.triangles().empty());
    PrimitiveSceneCanvas3D replay(context);
    scene.render(replay);
    CHECK_EQ(replay.statistics().cacheHits, 1u);
    CHECK_EQ(replay.triangles().size(), filled.triangles().size());
}

// graphics_primitive_parity.cpp
using namespace eve::graphics;
using namespace eve::graphics_editing;
using namespace eve::graphics::parity_test;

TEST_CASE("graphics.backendParity.primitive2DAnd3DReadback") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);

    Canvas *flatTarget = gfx->newCanvas(64, 64);
    REQUIRE(flatTarget != nullptr);
    gfx->setCanvas(flatTarget);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    PrimitiveCanvas2D flat;
    PrimitivePaint    flatPaint;
    flatPaint.mode         = PaintMode::FillAndStroke;
    flatPaint.color        = Color(1.f, 0.1f, 0.05f, 1.f);
    flatPaint.stroke.width = 3.f;
    flatPaint.stroke.cap   = LineCap::Round;
    flatPaint.stroke.join  = LineJoin::Round;
    flat.drawRoundedRect({10.f, 12.f}, {54.f, 50.f}, {8.f, 8.f}, flatPaint);
    PrimitivePaint dashedPaint;
    dashedPaint.mode         = PaintMode::Stroke;
    dashedPaint.color        = Color(0.1f, 0.65f, 1.f, 0.8f);
    dashedPaint.stroke.width = 4.f;
    dashedPaint.stroke.cap   = LineCap::Round;
    dashedPaint.stroke.dash  = DashPattern{{6.f, 3.f}, 1.5f, DashSpace::ScreenPixels};
    const std::array<glm::vec2, 4> dashedPoints{{{4.f, 8.f}, {20.f, 4.f}, {42.f, 9.f}, {60.f, 5.f}}};
    flat.drawPolyline(dashedPoints, false, dashedPaint);
    gfx->drawPrimitiveCanvas(flat);
    gfx->setCanvas();
    std::unique_ptr<eve::image::ImageData> flatImage(flatTarget->newImageData());
    REQUIRE(flatImage.get() != nullptr);
    REQUIRE(pixel(*flatImage, 32, 32)[0] > 160);
    writeParityArtifact(*flatImage, "primitive_2d_fill_stroke", gfx->getBackendName());

    Canvas *spatialTarget = gfx->newCanvas(64, 64);
    REQUIRE(spatialTarget != nullptr);
    SceneDrawContext context;
    context.viewportSize = {64, 64};
    context.nearPlane    = 0.1f;
    context.farPlane     = 10.f;
    context.projection   = perspectiveVulkanRH_ZO(glm::radians(60.f), 1.f, context.nearPlane, context.farPlane);
    PrimitiveSceneCanvas3D spatial(context);
    ScenePrimitivePaint    spatialPaint;
    spatialPaint.mode         = PaintMode::FillAndStroke;
    spatialPaint.color        = Color(0.05f, 0.9f, 0.2f, 1.f);
    spatialPaint.stroke.width = 3.f;
    spatialPaint.depth        = PrimitiveDepthMode::TestOnly;
    spatialPaint.cull         = PrimitiveCullMode::Back;
    spatial.drawDisk({0.f, 0.f, -2.f}, {0.f, 0.f, 1.f}, 0.55f, spatialPaint, 32);
    ScenePrimitivePaint spatialDash;
    spatialDash.mode         = PaintMode::Stroke;
    spatialDash.color        = Color(0.2f, 0.55f, 1.f, 0.75f);
    spatialDash.stroke.width = 4.f;
    spatialDash.stroke.cap   = LineCap::Round;
    spatialDash.stroke.join  = LineJoin::Bevel;
    spatialDash.stroke.dash  = DashPattern{{7.f, 4.f}, 2.f, DashSpace::ScreenPixels};
    spatialDash.depth        = PrimitiveDepthMode::Ignore;
    const std::array<glm::vec3, 3> spatialPoints{{{-0.9f, 0.65f, -2.4f}, {0.f, 0.85f, -2.f}, {0.9f, 0.65f, -2.4f}}};
    spatial.drawPolyline(spatialPoints, false, spatialDash);
    ScenePrimitivePaint clipped = spatialDash;
    clipped.color               = Color(1.f, 0.75f, 0.1f, 1.f);
    clipped.depth               = PrimitiveDepthMode::TestOnly;
    clipped.stroke.dash         = DashPattern{{0.12f, 0.08f}, 0.f, DashSpace::WorldUnits};
    clipped.stroke.widthSpace   = WidthSpace::WorldUnits;
    clipped.stroke.width        = 0.025f;
    spatial.drawLine({-0.75f, -0.7f, -0.05f}, {0.75f, -0.7f, -2.5f}, clipped);
    gfx->begin3DFrameToCanvas(spatialTarget);
    gfx->drawPrimitiveScene(spatial);
    PrimitiveSceneCanvas3D secondSubmission(context);
    ScenePrimitivePaint    secondPaint;
    secondPaint.mode         = PaintMode::Stroke;
    secondPaint.color        = Color(1.f, 0.f, 1.f, 1.f);
    secondPaint.depth        = PrimitiveDepthMode::Ignore;
    secondPaint.stroke.width = 3.f;
    secondSubmission.drawLine({-0.95f, 0.25f, -2.f}, {-0.65f, 0.25f, -2.f}, secondPaint);
    gfx->drawPrimitiveScene(secondSubmission);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> spatialImage(spatialTarget->newImageData());
    REQUIRE(spatialImage.get() != nullptr);
    const uint8_t *center = pixel(*spatialImage, 32, 32);
    REQUIRE(center[1] > 140);
    REQUIRE(center[1] > center[0] + 60);
    // An asymmetric fixture catches clip-Y inversions that a centered disk hides.
    std::size_t blueAbove = 0;
    std::size_t blueBelow = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const auto *sample = pixel(*spatialImage, x, y);
            if (sample[2] > 100 && sample[2] > sample[0] + 60) {
                if (y < 32)
                    ++blueAbove;
                else
                    ++blueBelow;
            }
        }
    }
    REQUIRE(blueAbove > 20u);
    REQUIRE_EQ(blueBelow, 0u);
    writeParityArtifact(*spatialImage, "primitive_3d_depth_fill_stroke", gfx->getBackendName());
}

TEST_CASE("graphics.backendParity.vegetationFieldGizmoReadback") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    Canvas *target = gfx->newCanvas(96, 96);
    REQUIRE(target != nullptr);

    VegetationElement ellipsoid;
    ellipsoid.channel = VegetationChannel::Color;
    ellipsoid.shape   = VegetationShape::Ellipsoid;
    ellipsoid.center  = {-0.55f, 0.f, -3.f};
    ellipsoid.extents = {0.65f, 0.9f, 0.45f};
    ellipsoid.yaw     = 0.45f;
    VegetationElement box;
    box.channel = VegetationChannel::Motion;
    box.shape   = VegetationShape::Box;
    box.center  = {0.65f, 0.f, -3.f};
    box.extents = {0.55f, 0.75f, 0.4f};
    box.yaw     = -0.55f;
    const std::array elements{ellipsoid, box};
    VegetationField  field;
    REQUIRE(field.replace({}, elements).ok());

    VegetationFieldGizmoBuilder builder;
    auto                        snapshot = builder.build("field-readback", field.revision(), field);
    REQUIRE(snapshot.ok());
    SceneDrawContext context;
    context.viewportSize = {96, 96};
    context.nearPlane    = 0.1f;
    context.farPlane     = 10.f;
    context.projection   = perspectiveVulkanRH_ZO(glm::radians(60.f), 1.f, context.nearPlane, context.farPlane);
    PrimitiveGizmoRenderer renderer;
    gfx->begin3DFrameToCanvas(target);
    auto rendered = renderer.render(snapshot.value(), context, *gfx);
    REQUIRE(rendered.ok());
    gfx->end3DFrameToCanvas();

    std::unique_ptr<eve::image::ImageData> image(target->newImageData());
    REQUIRE(image.get() != nullptr);
    std::size_t colored = 0;
    for (int y = 0; y < 96; ++y)
        for (int x = 0; x < 96; ++x) {
            const auto *sample = pixel(*image, x, y);
            if (sample[0] > 40 || sample[1] > 40 || sample[2] > 40) ++colored;
        }
    REQUIRE(colored > 100u);
    writeParityArtifact(*image, "vegetation_field_gizmo", gfx->getBackendName());
}

// graphics_primitive_script.cpp
TEST_CASE("GraphicsPrimitives.scriptSpatialShapesAndReleasedProxy") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        gfx <- eve.Graphics();
        disk <- gfx.newPrimitiveDisk3D(0.0,0.0,-2.0,0.0,1.0,0.0,1.0,1.0,0.5,0.1,1.0,2.0);
        shape <- disk.value;
        fill <- shape.setPaintMode("fill-stroke");
        cap <- shape.setLineCap("round");
        join <- shape.setLineJoin("bevel");
        width <- shape.setWidthSpace("world");
        blend <- shape.setBlendMode("alpha");
        cull <- shape.setCullMode("back");
        layer <- shape.setLayer(4);
        badMode <- shape.setPaintMode("invalid");
        badMatrix <- shape.setTransform([1.0]);
        matrix <- shape.setTransform([1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                                      0.0,0.0,1.0,0.0, 2.0,0.0,0.0,1.0]);
        batch <- gfx.setPrimitiveTransforms3D([shape],
            [1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,3.0,0.0,0.0,1.0]);
        badBatch <- gfx.setPrimitiveTransforms3D([null],
            [1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,3.0,0.0,0.0,1.0]);
        route <- gfx.newPrimitivePolyline3D([0.0,0.0,-2.0,1.0,1.0,-3.0],false,1.0,1.0,1.0,1.0,2.0);
        badRoute <- gfx.newPrimitivePolyline3D([0.0,0.0],false,1.0,1.0,1.0,1.0,2.0);
        routeUpdate <- route.value.setPolyline([0.0,0.0,-2.0,2.0,1.0,-3.0],false);
        cylinder <- gfx.newPrimitiveCylinder3D(0.0,0.0,0.0,0.0,2.0,0.0,0.5,1.0,1.0,1.0,1.0,2.0);
        capsule <- gfx.newPrimitiveCapsule3D(0.0,0.0,0.0,0.0,2.0,0.0,0.5,1.0,1.0,1.0,1.0,2.0);
        cone <- gfx.newPrimitiveCone3D(0.0,2.0,0.0,0.0,-1.0,0.0,2.0,0.5,1.0,1.0,1.0,1.0,2.0);
        arrow <- gfx.newPrimitiveArrow3D(0.0,0.0,0.0,0.0,2.0,0.0,0.5,0.2,1.0,1.0,1.0,1.0,2.0);
        obb <- gfx.newPrimitiveObb3D([0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0],1.0,1.0,1.0,1.0,2.0);
        grid <- gfx.newPrimitiveGrid3D([0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0],4,4,1.0,1.0,1.0,1.0,2.0);
        arc <- gfx.newPrimitiveArc3D([0.0,0.0,0.0,0.0,1.0,0.0,1.0,0.0,0.0],1.0,0.0,1.5,1.0,1.0,1.0,1.0,2.0);
        removed <- shape.remove();
        stale <- shape.isStale();
        afterRemove <- shape.setLineCap("round");
    )"));
    for (const char *name :
         {"disk",    "fill",        "cap",      "join",    "width", "blend", "cull", "layer", "matrix", "route",
          "removed", "routeUpdate", "cylinder", "capsule", "cone",  "arrow", "obb",  "grid",  "arc",    "batch"})
        CHECK(vm.find(name).toTable().get<bool>("ok"));
    for (const char *name : {"badMode", "badMatrix", "badRoute", "afterRemove", "badBatch"})
        CHECK(!vm.find(name).toTable().get<bool>("ok"));
    CHECK(vm.find("stale").toBool());
}
}  // namespace test_graphics_primitive_cache
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_raytracing)
#include "PrivateTestScope.h"
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/Status.h"
#include "graphics/Graphics.h"
#include "graphics/IRayTracing.h"
#include "graphics/RayTracingCaps.h"
#include "graphics/RenderControl.h"
#include "graphics/raytracing/RayTracingModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_raytracing.cpp
namespace test_graphics_raytracing {
using namespace eve::graphics;

TEST_CASE("graphics.raytracing.capabilityAbsentOrPresent") {
    // The optional module registers IRayTracing at static init when linked.
    // Either the provider is present (full/3d profiles) or absent (trimmed) —
    // both are supported states.
    auto *rt = eve::cap::query<IRayTracing>();
    if (!rt) {
        CHECK(true);  // trimmed build: absence is explicit and OK
        return;
    }
    // Provider linked: querying caps must not throw.
    const RayTracingCaps caps = rt->caps();
    (void)caps;
    CHECK(true);
}

TEST_CASE("graphics.raytracing.probeAndUnsupportedOps") {
    auto *gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(64, 64);

    const bool deviceRt = gfx->supportsRayTracing();
    CHECK_EQ(deviceRt, gfx->rayTracingCaps().rayTracingAvailable());

    auto *rt = eve::cap::query<IRayTracing>();
    if (!rt) {
        CHECK(!deviceRt);  // no provider ⇒ treat as unavailable
        return;
    }

    if (!rt->isAvailable()) {
        // Soft-fail contract: fallible ops return Unsupported, not throw.
        const float    positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        const uint32_t indices[]   = {0, 1, 2};
        auto           added       = rt->addTriangleMesh(positions, 3, indices, 3, glm::mat4(1.f));
        CHECK(!added.ok());
        CHECK_EQ(int(added.status().code()), int(eve::StatusCode::Unsupported));

        auto rebuilt = rt->rebuildScene();
        CHECK(!rebuilt.ok());
        CHECK_EQ(int(rebuilt.status().code()), int(eve::StatusCode::Unsupported));
        return;
    }

    // Hardware path: build a single triangle BLAS/TLAS.
    const float    positions[] = {-1.f, 0.f, -2.f, 1.f, 0.f, -2.f, 0.f, 1.f, -2.f};
    const uint32_t indices[]   = {0, 1, 2};
    auto           added       = rt->addTriangleMesh(positions, 3, indices, 3, glm::mat4(1.f));
    REQUIRE(added.ok());
    CHECK(added.value() == 0u);
    auto rebuilt = rt->rebuildScene();
    CHECK(rebuilt.ok());
    rt->clearScene();
}


TEST_CASE("graphics.raytracing.moduleScriptFacade") {
    auto *rtMod = eve::graphics::raytracing::RayTracing::create();
    REQUIRE(rtMod != nullptr);
    // Must not throw when RT is unavailable.
    (void)rtMod->isAvailable();
    (void)rtMod->caps();
}
}  // namespace test_graphics_raytracing
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_vegetation)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include <memory>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/PbrSurface.h"
#include "graphics/Texture.h"
#include "graphics/VegetationDetails.h"
#include "graphics/VegetationField.h"
#include "graphics/VegetationFieldGpu.h"
#include "graphics/VegetationMotion.h"
#include "graphics/VegetationRender.h"
#include "graphics/editing/VegetationFieldTarget.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_vegetation.cpp
namespace test_graphics_vegetation {
// graphics_vegetation.cpp
using namespace eve::graphics;

TEST_CASE("graphics.vegetation.mesh_highlight_ownership_and_failure") {
    Mesh mesh;
    mesh.gpuVertexCount = 2;
    std::vector<float> values{.2f, .8f};
    ASSERT(mesh.adoptMotionHighlights(std::move(values)).ok());
    ASSERT(values.empty());
    ASSERT(mesh.motionHighlights().size() == 2);
    std::vector<float> invalid{.4f};
    ASSERT(!mesh.adoptMotionHighlights(std::move(invalid)).ok());
    ASSERT(invalid.size() == 1);
    ASSERT(mesh.motionHighlights()[1] == .8f);
    invalid = {0, std::numeric_limits<float>::infinity()};
    ASSERT(!mesh.adoptMotionHighlights(std::move(invalid)).ok());
    ASSERT(mesh.motionHighlights()[0] == .2f);
    invalid = {0, -.1f};
    ASSERT(!mesh.adoptMotionHighlights(std::move(invalid)).ok());
    std::vector<float> clear;
    ASSERT(mesh.adoptMotionHighlights(std::move(clear)).ok());
    ASSERT(mesh.motionHighlights().empty());
    ASSERT(clear.size() == 2);
}

TEST_CASE("graphics.vegetation.extras_gpu_atlas_tracks_revision_and_rejects_half_overflow") {
    using namespace eve::graphics;
    VegetationField   field;
    VegetationGlobals globals;
    globals.extras = glm::vec4(0.5f, -2.f, 3.f, 1.f);
    REQUIRE(field.replace(globals, {}).ok());

    auto *gfx = Graphics::create();
    gfx->initHeadless(16, 16);
    auto atlas = uploadVegetationExtrasAtlas(*gfx, field, {0.13f, 0.f, -0.13f}, {2.f, 1.f, 2.f}, 2, 2);
    REQUIRE(atlas.ok());
    CHECK(atlas.value().texture != nullptr);
    CHECK(atlas.value().fieldRevision == field.revision());
    CHECK(atlas.value().layers == 9);
    CHECK(atlas.value().center.x == 0.f);
    CHECK(atlas.value().center.z == 0.f);
    REQUIRE(gfx->releaseTexture(atlas.value().texture));
    auto motion = uploadVegetationMotionAtlas(*gfx, field, {0, 0, 0}, {1, 1, 1}, 1, 1);
    auto vertex = uploadVegetationVertexAtlas(*gfx, field, {0, 0, 0}, {1, 1, 1}, 1, 1);
    REQUIRE(motion.ok());
    REQUIRE(vertex.ok());
    CHECK(motion.value().layers == 9);
    CHECK(vertex.value().fieldRevision == field.revision());
    REQUIRE(gfx->releaseTexture(motion.value().texture));
    REQUIRE(gfx->releaseTexture(vertex.value().texture));

    globals.extras.x = 65505.f;
    REQUIRE(field.replace(globals, {}).ok());
    auto overflow = uploadVegetationExtrasAtlas(*gfx, field, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, 1, 1);
    REQUIRE(!overflow.ok());
    CHECK(overflow.error()->code() == eve::DiagnosticCode::InvalidArgument);
}

TEST_CASE("graphics.vegetation.gpu_field_binding_is_coherent_stale_safe_and_atomic") {
    VegetationField   field;
    VegetationGlobals globals;
    REQUIRE(field.replace(globals, {}).ok());
    auto *gfx = Graphics::create();
    gfx->initHeadless(16, 16);
    auto fields = uploadVegetationGpuFieldSet(*gfx, field, {2, 0, -4}, {4, 1, 8}, 1, 1, false);
    REQUIRE(fields.ok());
    const std::array<std::uint8_t, 4> noisePixel{128, 128, 128, 255};
    auto                             *noise = gfx->newTexture(1, 1, noisePixel.data(), true, true);
    REQUIRE(noise != nullptr);
    PbrSurface surface;
    surface.vegetationMotion.bending = .37f;
    VegetationGpuRuntime runtime;
    runtime.motionNoise        = noise;
    runtime.motionDirection    = {.6f, -.8f};
    runtime.worldOrigin        = {10, 2, 30};
    runtime.time               = 7.25;
    runtime.globalBending      = .8f;
    runtime.globalBranch       = .7f;
    runtime.globalFlutter      = .6f;
    runtime.noiseTiling        = 2.5f;
    runtime.motionFadeDistance = 75.f;
    REQUIRE(bindVegetationGpuFields(surface, field, fields.value(), runtime).ok());
    REQUIRE_EQ(surface.vegetationExtras.texture, fields.value().extras.texture);
    REQUIRE_EQ(surface.vegetationColors.texture, fields.value().colors.texture);
    REQUIRE_EQ(surface.vegetationMotion.texture, fields.value().motion.texture);
    REQUIRE_EQ(surface.vegetationVertex.texture, fields.value().vertex.texture);
    REQUIRE_EQ(surface.vegetationExtras.coords[0], .125f);
    REQUIRE_EQ(surface.vegetationExtras.coords[1], .0625f);
    REQUIRE_EQ(surface.vegetationExtras.coords[2], .25f);
    REQUIRE_EQ(surface.vegetationExtras.coords[3], .75f);
    REQUIRE_EQ(surface.vegetationMotion.mode, PbrVegetationMotionMode::Object);
    REQUIRE_EQ(surface.vegetationVertex.source, PbrVegetationDeformationSource::GpuFields);
    REQUIRE_EQ(surface.vegetationMotion.bending, .37f);
    REQUIRE_EQ(surface.vegetationMotion.time, 7.25);

    const auto before = surface;
    globals.season    = 1;
    REQUIRE(field.replace(globals, {}).ok());
    REQUIRE(!bindVegetationGpuFields(surface, field, fields.value(), runtime).ok());
    REQUIRE_EQ(surface.vegetationExtras.texture, before.vegetationExtras.texture);
    REQUIRE_EQ(surface.vegetationMotion.time, before.vegetationMotion.time);
    REQUIRE(releaseVegetationGpuFieldSet(*gfx, fields.value()).ok());
    REQUIRE(fields.value().extras.texture == nullptr);
    REQUIRE(fields.value().colors.texture == nullptr);
    REQUIRE(fields.value().motion.texture == nullptr);
    REQUIRE(fields.value().vertex.texture == nullptr);
    REQUIRE(gfx->releaseTexture(noise));
}

TEST_CASE("graphics.vegetation.prebaked_layers_upload_bind_and_release_atomically") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(16, 16);
    std::array<VegetationChannelAtlas, 9> colors, extras, motion, vertex;
    auto initialize = [](auto &layers, std::uint32_t width, std::uint32_t height, glm::vec3 center, glm::vec3 extent,
                         float marker) {
        for (std::size_t layer = 0; layer < layers.size(); ++layer) {
            layers[layer].width  = width;
            layers[layer].height = height;
            layers[layer].center = center;
            layers[layer].extent = extent;
            layers[layer].pixels.assign(std::size_t(width) * height, glm::vec4(float(layer), marker, .5f, 1.f));
        }
    };
    initialize(colors, 1, 1, {2.f, 0.f, -4.f}, {4.f, 1.f, 8.f}, 0.f);
    initialize(extras, 2, 1, {-2.f, 0.f, 4.f}, {2.f, 1.f, 4.f}, 1.f);
    initialize(motion, 1, 2, {1.f, 0.f, 2.f}, {8.f, 1.f, 2.f}, 2.f);
    initialize(vertex, 2, 2, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, 3.f);
    auto fields = uploadVegetationGpuFieldSet(*gfx, colors, extras, motion, vertex, 77);
    REQUIRE(fields.ok());
    CHECK_EQ(fields.value().extras.fieldRevision, std::uint64_t(77));
    CHECK_EQ(fields.value().motion.layers, std::uint32_t(9));
    PbrSurface           surface;
    VegetationGpuRuntime runtime;
    runtime.enableMotion = false;
    REQUIRE(bindVegetationGpuFields(surface, fields.value(), 77, runtime).ok());
    CHECK_EQ(surface.vegetationColors.texture, fields.value().colors.texture);
    CHECK_EQ(fields.value().colors.width, std::uint32_t(1));
    CHECK_EQ(fields.value().extras.width, std::uint32_t(2));
    CHECK_EQ(fields.value().motion.height, std::uint32_t(2));
    CHECK_EQ(fields.value().vertex.width, std::uint32_t(2));
    CHECK_EQ(surface.vegetationColors.coords[0], .125f);
    CHECK_EQ(surface.vegetationExtras.coords[0], .25f);
    CHECK_EQ(surface.vegetationMotion.coords[0], .0625f);
    CHECK_EQ(surface.vegetationVertex.coords[0], .5f);
    const auto before = surface;
    REQUIRE(!bindVegetationGpuFields(surface, fields.value(), 78, runtime).ok());
    CHECK_EQ(surface.vegetationColors.texture, before.vegetationColors.texture);
    REQUIRE(releaseVegetationGpuFieldSet(*gfx, fields.value()).ok());
}

TEST_CASE("graphics.vegetation.deformation_stream_validates_and_publishes_atomically") {
    Mesh mesh;
    mesh.gpuVertexCount = 1;
    std::vector<float> valid{1, 2, 3, .2f, .3f, .4f, .5f, 6, 7};
    REQUIRE(mesh.adoptVegetationDeformationFactors(std::move(valid)).ok());
    const auto before =
        std::vector<float>(mesh.vegetationDeformationFactors().begin(), mesh.vegetationDeformationFactors().end());
    std::vector<float> invalid{1, 2, 3, 1.1f, .3f, .4f, .5f, 6, 7};
    REQUIRE(!mesh.adoptVegetationDeformationFactors(std::move(invalid)).ok());
    REQUIRE_EQ(
        std::vector<float>(mesh.vegetationDeformationFactors().begin(), mesh.vegetationDeformationFactors().end()),
        before);
    std::vector<float> clear;
    REQUIRE(mesh.adoptVegetationDeformationFactors(std::move(clear)).ok());
    REQUIRE(mesh.vegetationDeformationFactors().empty());
}

TEST_CASE("graphics.vegetation.mesh_factor_ownership_and_failure") {
    Mesh mesh;
    mesh.gpuVertexCount = 2;
    std::vector<float> values{.2f, .8f, 1.f, 2.f, -1.f, 1.f, 0.f, .5f, 3.f, 4.f};
    ASSERT(mesh.adoptVegetationFactors(std::move(values)).ok());
    ASSERT(values.empty());
    ASSERT(mesh.vegetationFactors().size() == 10);
    std::vector<float> invalid{.4f, .5f};
    ASSERT(!mesh.adoptVegetationFactors(std::move(invalid)).ok());
    ASSERT(invalid.size() == 2);
    ASSERT(mesh.vegetationFactors()[1] == .8f);
    invalid = {0, 1, 0, std::numeric_limits<float>::infinity(), 0, 0, 1, 0, 0, 0};
    ASSERT(!mesh.adoptVegetationFactors(std::move(invalid)).ok());
    invalid = {0, 1, 1.1f, 0, 0, 0, 1, 0, 0, 0};
    ASSERT(!mesh.adoptVegetationFactors(std::move(invalid)).ok());
    std::vector<float> clear;
    ASSERT(mesh.adoptVegetationFactors(std::move(clear)).ok());
    ASSERT(mesh.vegetationFactors().empty());
    ASSERT(clear.size() == 10);
}

TEST_CASE("graphics.vegetation.layers_and_atomic_replace") {
    VegetationField field;
    ASSERT(field.revision() == 0);
    VegetationElement element;
    element.value  = {0.2f, 0.4f, 0.1f, 0.5f};
    element.layers = 1u << 3;
    ASSERT(field.replace({}, std::span(&element, 1)).ok());
    ASSERT(field.revision() == 1);
    auto defaultLayer = field.sample({0, 0, 0});
    auto selected     = field.sample({0, 0, 0}, {3, 0, 0, 0});
    ASSERT(defaultLayer.ok());
    ASSERT(selected.ok());
    ASSERT(defaultLayer.value().color.x == 1.f);
    ASSERT(selected.value().color.x == 0.2f);
    element.extents.x = 0.f;
    ASSERT(!field.replace({}, std::span(&element, 1)).ok());
    ASSERT(field.revision() == 1);
    auto preserved = field.sample({0, 0, 0}, {3, 0, 0, 0});
    ASSERT(preserved.ok());
    ASSERT(preserved.value().color.x == 0.2f);
    ASSERT(!field.sample({0, 0, 0}, {9, 0, 0, 0}).ok());
}

TEST_CASE("graphics.vegetation.season_wrap_and_mask_ownership") {
    VegetationField   field;
    VegetationElement e;
    e.seasonal = true;
    e.seasons  = {glm::vec4(0.f), glm::vec4(1.f), glm::vec4(2.f), glm::vec4(3.f)};
    e.mask     = {1, 1, {glm::vec4(1.f, 1.f, 1.f, 0.5f)}};
    VegetationGlobals globals;
    globals.season = 3.5f;
    ASSERT(field.replace(globals, std::span(&e, 1)).ok());
    e.mask.pixels[0].a = 0.f;
    auto middle        = field.sample({0, 0, 0});
    ASSERT(middle.ok());
    ASSERT(std::abs(middle.value().color.x - 1.25f) < 1e-6f);
    e.mask         = {};
    globals.season = 4.f;
    ASSERT(field.replace(globals, std::span(&e, 1)).ok());
    auto winter = field.sample({0, 0, 0});
    ASSERT(winter.ok());
    ASSERT(winter.value().color.x == 0.f);
}

TEST_CASE("graphics.vegetation.priority_and_channel_isolation") {
    VegetationField                  field;
    std::array<VegetationElement, 3> elements;
    elements[0].value    = glm::vec4(4.f);
    elements[0].priority = 10;
    elements[1].value    = glm::vec4(2.f);
    elements[1].priority = -10;
    elements[2].channel  = VegetationChannel::Extras;
    elements[2].value    = glm::vec4(0.25f);
    ASSERT(field.replace({}, elements).ok());
    auto value = field.sample({0, 0, 0});
    ASSERT(value.ok());
    ASSERT(value.value().color.x == 4.f);
    ASSERT(value.value().extras.x == 0.25f);
    ASSERT(value.value().motion.z == 0.5f);
    elements[1].priority = 10;
    ASSERT(field.replace({}, elements).ok());
    auto stable = field.sample({0, 0, 0});
    ASSERT(stable.ok());
    ASSERT(stable.value().color.x == 2.f);
}

TEST_CASE("graphics.vegetation.rotated_volume_and_edge") {
    VegetationField   field;
    VegetationElement e;
    e.shape    = VegetationShape::Box;
    e.extents  = {2, 1, 0.5f};
    e.yaw      = 1.57079632679f;
    e.value    = glm::vec4(0.f);
    e.edgeFade = 0.5f;
    ASSERT(field.replace({}, std::span(&e, 1)).ok());
    auto inside  = field.sample({0, 0, 1});
    auto outside = field.sample({1, 0, 0});
    auto edge    = field.sample({0, 0, 1.5f});
    ASSERT(inside.ok());
    ASSERT(outside.ok());
    ASSERT(edge.ok());
    ASSERT(inside.value().color.x == 0.f);
    ASSERT(outside.value().color.x == 1.f);
    ASSERT(std::abs(edge.value().color.x - 0.5f) < 1e-6f);
}

TEST_CASE("graphics.vegetation.atlas_texel_centers_and_snap") {
    VegetationField   field;
    VegetationElement e;
    e.extents = {2, 2, 2};
    e.value   = glm::vec4(0.2f);
    ASSERT(field.replace({}, std::span(&e, 1)).ok());
    auto atlas = field.bake({0.1f, 0, 0.1f}, {2, 1, 2}, 4, 4);
    ASSERT(atlas.ok());
    ASSERT(atlas.value().center.x == 0.f);
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x) {
            auto direct = field.sample({float(x) - 1.5f, 0, float(y) - 1.5f});
            ASSERT(direct.ok());
            ASSERT(atlas.value().channels[0][y * 4 + x] == direct.value().color);
        }
    ASSERT(!field.bake({}, {1, 1, 1}, 0, 4).ok());
}

TEST_CASE("graphics.vegetation.wind_anchor_time_and_interaction") {
    VegetationField                 field;
    std::array<VegetationVertex, 2> vertices;
    vertices[1].position = {0, 1, 0};
    vertices[1].bending  = 1.f;
    VegetationMotion motion;
    auto             first = deformVegetation(field, vertices, motion);
    motion.time            = 0.7;
    auto second            = deformVegetation(field, vertices, motion);
    auto replay            = deformVegetation(field, vertices, motion);
    ASSERT(first.ok());
    ASSERT(second.ok());
    ASSERT(replay.ok());
    ASSERT(first.value().positions[0] == 0.f);
    ASSERT(second.value().positions[0] == 0.f);
    ASSERT(std::abs(first.value().positions[3] - second.value().positions[3]) > 1e-5f);
    ASSERT(second.value().positions == replay.value().positions);
    VegetationGlobals globals;
    globals.motion = {0, 1, 0, 1};
    ASSERT(field.replace(globals, {}).ok());
    auto interaction = deformVegetation(field, vertices, motion);
    ASSERT(interaction.ok());
    const float angle = 2.f * (0.9999f / 1.0001f);
    ASSERT(std::abs(interaction.value().positions[5] - std::sin(angle)) < 1e-5f);
    ASSERT(std::abs(interaction.value().positions[4] - std::cos(angle)) < 1e-5f);
}

TEST_CASE("graphics.vegetation.motion_input_failure_and_zero_size") {
    VegetationField  field;
    VegetationVertex vertex;
    vertex.position = {0, 1, 0};
    VegetationGlobals globals;
    globals.vertex.w = 0.f;
    ASSERT(field.replace(globals, {}).ok());
    auto collapsed = deformVegetation(field, std::span(&vertex, 1), {});
    ASSERT(collapsed.ok());
    ASSERT(collapsed.value().positions[1] == 0.f);
    ASSERT(std::isfinite(collapsed.value().normals[1]));
    vertex.flutter = std::numeric_limits<float>::quiet_NaN();
    ASSERT(!deformVegetation(field, std::span(&vertex, 1), {}).ok());
    ASSERT(!field.sample({std::numeric_limits<float>::infinity(), 0, 0}).ok());
}

TEST_CASE("graphics.vegetation.motion_highlight_uses_rest_height_wind_and_fade") {
    VegetationField  field;
    VegetationMotion motion;
    motion.noise = {1, 1, {glm::vec4(.5f, .5f, .5f, .4f)}};
    std::array<VegetationVertex, 3> vertices;
    vertices[0].bending = 0;
    vertices[1].bending = .5f;
    vertices[2].bending = 1;
    auto near           = deformVegetation(field, vertices, motion);
    ASSERT(near.ok());
    ASSERT(near.value().motionHighlights.size() == vertices.size());
    ASSERT(near.value().motionHighlights[0] == 0);
    ASSERT(std::abs(near.value().motionHighlights[1] - .15f) < 1e-6f);
    ASSERT(std::abs(near.value().motionHighlights[2] - .3f) < 1e-6f);
    // The fade samples the rest vertex, not its wind-deformed position or
    // the finite-difference positions used for the normal Jacobian.
    motion.camera  = {0, 0, 75};
    motion.bending = 2;
    auto middle    = deformVegetation(field, vertices, motion);
    ASSERT(middle.ok());
    const float fadeEnd  = motion.fadeDistance + .01f;
    const float expected = .3f * (75.f - fadeEnd) / (-fadeEnd * .5f + .0001f);
    ASSERT(std::abs(middle.value().motionHighlights[2] - expected) < 1e-6f);
    auto repeated = deformVegetation(field, vertices, motion);
    ASSERT(repeated.ok());
    ASSERT(repeated.value().motionHighlights == middle.value().motionHighlights);
    motion.camera = {0, 0, 200};
    auto far      = deformVegetation(field, vertices, motion);
    ASSERT(far.ok());
    ASSERT(far.value().motionHighlights[2] == 0);
    motion.camera = {};
    VegetationGlobals globals;
    globals.motion.z = 0;
    ASSERT(field.replace(globals, {}).ok());
    auto still = deformVegetation(field, vertices, motion);
    ASSERT(still.ok());
    ASSERT(still.value().motionHighlights[2] == 0);
}

TEST_CASE("graphics.vegetation.snapshot_restore_unknown_fields") {
    VegetationField   field;
    VegetationElement e;
    e.value = {0.4f, 0.5f, 0.6f, 0.f};
    ASSERT(field.replace({}, std::span(&e, 1)).ok());
    auto snapshot = field.snapshot();
    ASSERT(snapshot.ok());
    VegetationField restored;
    ASSERT(restored.restore(snapshot.value()).ok());
    auto sample = restored.sample({0, 0, 0});
    ASSERT(sample.ok());
    ASSERT(sample.value().color == e.value);
    snapshot.value().set("version", 2);
    auto future = restored.restore(snapshot.value());
    ASSERT(!future.ok());
    ASSERT(future.error()->code() == eve::DiagnosticCode::UnknownVersion);
    snapshot.value().set("version", 1);
    snapshot.value().set("unexpected", 7);
    ASSERT(!restored.restore(snapshot.value()).ok());
    auto preserved = restored.sample({0, 0, 0});
    ASSERT(preserved.ok());
    ASSERT(preserved.value().color == e.value);
}

TEST_CASE("graphics.vegetation.surface_idempotence_and_failure") {
    Material          material;
    VegetationSample  sample{{0.5f, 1.f, 0.5f, 0.f}, {1, 1, 0, 0.8f}, {}, {}};
    VegetationSurface base;
    base.albedo = {0.5f, 0.8f, 0.2f, 1.f};
    ASSERT(applyVegetationSurface(material, sample, base).ok());
    const float r = material.getTintR();
    ASSERT(applyVegetationSurface(material, sample, base).ok());
    ASSERT(material.getTintR() == r);
    ASSERT(material.getRoughness() == base.roughness);
    ASSERT(material.pbrSurface().vegetationColor.overlay == 0.f);
    ASSERT(material.pbrSurface().vegetationColor.wetness == 1.f);
    ASSERT(material.pbrSurface().vegetationColor.wetnessContrast == .5f);
    ASSERT(material.pbrSurface().vegetationColor.overlaySubsurface == .5f);
    ASSERT(material.pbrSurface().vegetationColor.overlayColor == base.overlayColor);
    ASSERT(material.pbrSurface().vegetationColor.fieldColor == (std::array<float, 4>{.5f, 1.f, .5f, 0.f}));
    ASSERT(material.getTintR() == base.albedo[0]);
    ASSERT(material.getDoubleSided());
    ASSERT(material.getSurfaceMode() == "masked");
    base.roughness = -1.f;
    ASSERT(!applyVegetationSurface(material, sample, base).ok());
    ASSERT(material.getTintR() == r);
    base.roughness         = .5f;
    base.overlaySubsurface = 1.1f;
    ASSERT(!applyVegetationSurface(material, sample, base).ok());
    ASSERT(material.getTintR() == r);
}

// graphics_vegetation_color.cpp
using namespace eve::graphics;
TEST_CASE("graphics.vegetation.color_mask_requires_linear_binding_and_atomic_validity") {
    Material   material;
    PbrSurface surface;
    REQUIRE(material.setPbrSurface(surface).ok());
    surface.colorMaskEnabled = true;
    REQUIRE(!material.setPbrSurface(surface).ok());
    REQUIRE(!material.pbrSurface().colorMaskEnabled);
    surface.colorMaskEnabled = false;
    surface.colorMaskMin     = -.1f;
    surface.colorMaskMax     = .2f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.colorMaskMin          = 0;
    surface.colorMaskMax          = 1;
    surface.albedoTextureStrength = 2;
    REQUIRE(!validatePbrSurface(surface).ok());
}
TEST_CASE("graphics.vegetation.color_mask_remaps_after_gpu_filtering") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    p[]       = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    n[]       = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]      = {.4f, .5f, .4f, .5f, .4f, .5f, .4f, .5f};
    const uint32_t indices[] = {0, 2, 1, 0, 3, 2};
    auto          *mesh      = gfx->newMeshFromArrays(p, n, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t pixels[] = {255, 255, 0, 0, 255, 255, 0, 255};
    auto         *mask     = gfx->newTexture(2, 1, pixels);
    REQUIRE(mask != nullptr);
    PbrSurface surface;
    surface.unlit              = true;
    surface.colorMaskEnabled   = true;
    surface.colorMaskSecondary = {0, 0, 1};
    surface.colorMaskMin       = .25f;
    surface.colorMaskMax       = .75f;
    auto &binding              = surface.textures[size_t(PbrTextureSlot::MetallicRoughness)];
    binding.texture            = mask;
    binding.srgbDecode         = false;
    binding.minFilter          = 9729;
    binding.magFilter          = 9729;
    REQUIRE(!gfx->Graphics::setMesh3DPbrSurface(&surface).ok());
    auto render = [&](const PbrSurface &current) {
        gfx->setMesh3DViewProj(glm::mat4(1));
        gfx->setMesh3DView(glm::mat4(1));
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DMaterial(0, 1);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        REQUIRE(gfx->setMesh3DPbrSurface(&current).ok());
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(1, 0, 0, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
        REQUIRE(image != nullptr);
        return image->getPixel(32, 32);
    };
    const auto mixed = render(surface);
    // UV .4 filters the two alpha endpoints to .3; remapping afterwards yields .09998.
    CHECK(std::abs(mixed.r - .1f) < .015f);
    CHECK(std::abs(mixed.b - .9f) < .015f);
    surface.colorMaskMin = .75f;
    surface.colorMaskMax = .25f;
    const auto inverted  = render(surface);
    CHECK(std::abs(inverted.r - .9f) < .015f);
    CHECK(std::abs(inverted.b - .1f) < .015f);
    surface.colorMaskMin     = .25f;
    surface.colorMaskMax     = .75f;
    surface.colorMaskEnabled = false;
    const auto primary       = render(surface);
    CHECK(primary.r > .98f);
    CHECK(primary.b < .02f);
    const uint8_t albedoPixel[] = {64, 255, 255, 255};
    auto         *albedo        = gfx->newTexture(1, 1, albedoPixel);
    REQUIRE(albedo != nullptr);
    surface.textures[0].texture    = albedo;
    surface.textures[0].srgbDecode = false;
    surface.albedoTextureStrength  = .5f;
    const auto halfway             = render(surface);
    CHECK(std::abs(halfway.r - .62549f) < .01f);
    surface.albedoTextureStrength = 0;
    CHECK(render(surface).r > .98f);
    surface.albedoTextureStrength = 1;
    CHECK(std::abs(render(surface).r - 64.f / 255.f) < .01f);
    surface.textures[0].texture = nullptr;
    surface.colorMaskEnabled    = true;
    const auto again            = render(surface);
    CHECK(std::abs(again.r - mixed.r) < .001f);
    CHECK(std::abs(again.b - mixed.b) < .001f);
}
TEST_CASE("graphics.vegetation.global_colors_matches_tve_linear_fragment_formula") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    p[]       = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    n[]       = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]      = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[] = {0, 2, 1, 0, 3, 2};
    auto          *mesh      = gfx->newMeshFromArrays(p, n, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    PbrSurface surface;
    surface.unlit                                = true;
    surface.vegetationColor.fieldColor           = {.1f, .2f, .3f, .5f};
    surface.vegetationColor.colorsMask           = 0;
    surface.vegetationColor.colorsVariation      = 0;
    surface.vegetationColor.vertexOcclusionAlpha = 1;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DMaterial(0, 1);
    gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.4f, .4f, .4f, 1));
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image != nullptr);
    const auto pixel = image->getPixel(32, 32);
    CHECK(std::abs(pixel.r - .4f * .1f * 4.594794f) < .015f);
    CHECK(std::abs(pixel.g - .4f * .2f * 4.594794f) < .015f);
    CHECK(std::abs(pixel.b - .4f * .3f * 4.594794f) < .015f);

    surface.vegetationColor.globalColorMaskMinimum = .6f;
    surface.vegetationColor.globalColorMaskMaximum = .8f;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->begin3DFrameToCanvas(canvas);
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.4f, .4f, .4f, 1));
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> remappedImage(canvas->newImageData());
    REQUIRE(remappedImage != nullptr);
    const auto remapped = remappedImage->getPixel(32, 32);
    CHECK(std::abs(remapped.r - .4f) < .015f);
    CHECK(std::abs(remapped.g - .4f) < .015f);
    CHECK(std::abs(remapped.b - .4f) < .015f);
}
TEST_CASE("graphics.vegetation.detail_layer_matches_tve_replace_formula") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    p[]       = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    n[]       = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]      = {.5f, .5f, .5f, .5f, .5f, .5f, .5f, .5f};
    const uint32_t indices[] = {0, 2, 1, 0, 3, 2};
    auto          *mesh      = gfx->newMeshFromArrays(p, n, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t albedoPixel[] = {128, 64, 32, 255};
    const uint8_t normalPixel[] = {128, 128, 255, 255};
    auto         *albedo        = gfx->newTexture(1, 1, albedoPixel);
    auto         *normal        = gfx->newTexture(1, 1, normalPixel);
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);
    PbrSurface surface;
    surface.unlit              = true;
    auto &detail               = surface.vegetationDetail;
    detail.value               = 1;
    detail.blendMode           = 1;
    detail.blendMinimum        = 0;
    detail.blendMaximum        = 0;
    detail.textures[0].texture = albedo;
    detail.textures[1].texture = normal;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DMaterial(0, 1);
    gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.2f, .3f, .4f, 1));
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image != nullptr);
    const auto pixel = image->getPixel(32, 32);
    auto       srgb  = [](float c) { return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f); };
    CHECK(std::abs(pixel.r - srgb(128.f / 255.f)) < .015f);
    CHECK(std::abs(pixel.g - srgb(64.f / 255.f)) < .015f);
    CHECK(std::abs(pixel.b - srgb(32.f / 255.f)) < .015f);
}

// graphics_vegetation_details.cpp
using namespace eve::graphics;

TEST_CASE("graphics.vegetationDetailsProjectsGlobalStateAtomically") {
    PbrSurface baseSurface;
    baseSurface.specularFactor = .25f;
    VegetationMotion baseMotion;
    baseMotion.time = 7;
    VegetationDetailSettings settings;
    settings.colorsLayer          = 2;
    settings.extrasLayer          = 3;
    settings.motionLayer          = 4;
    settings.globalColor          = .6f;
    settings.globalAlpha          = .7f;
    settings.globalOverlay        = .8f;
    settings.globalWetness        = .9f;
    settings.colorMaskMinimum     = .1f;
    settings.colorMaskMaximum     = .2f;
    settings.overlayMaskMinimum   = .3f;
    settings.overlayMaskMaximum   = .4f;
    settings.alphaThreshold       = .75f;
    settings.motionHighlight      = {2, 3, 4};
    settings.bendingAmplitude     = 1.2f;
    settings.bendingSpeed         = 11;
    settings.bendingScale         = 5;
    settings.flutterAmplitude     = .3f;
    settings.flutterSpeed         = 17;
    settings.flutterScale         = 8;
    settings.interactionAmplitude = 1.5f;
    settings.perspectivePush      = 1;
    settings.perspectiveNoise     = 2;
    settings.perspectiveAngle     = 3;
    auto projected                = configureVegetationDetails(baseSurface, baseMotion, settings);
    REQUIRE(projected.ok());
    REQUIRE_EQ(projected.value().surface.vegetationColors.layer, uint32_t(2));
    REQUIRE_EQ(projected.value().surface.vegetationExtras.layer, uint32_t(3));
    REQUIRE_EQ(projected.value().surface.vegetationMotion.layer, uint32_t(4));
    REQUIRE_EQ(projected.value().motion.motionLayer, uint8_t(4));
    REQUIRE_EQ(projected.value().surface.vegetationColor.colorsCoverage, .6f);
    REQUIRE_EQ(projected.value().surface.vegetationAlpha.global, .7f);
    REQUIRE_EQ(projected.value().surface.motionHighlightColor[2], 4.f);
    REQUIRE_EQ(projected.value().surface.vegetationMotion.bending, 1.2f);
    REQUIRE_EQ(projected.value().motion.flutterSpeed, 17.f);
    REQUIRE_EQ(projected.value().motion.interaction, 1.5f);
    REQUIRE_EQ(projected.value().colorMaskMinimum, .1f);
    REQUIRE_EQ(projected.value().overlayMaskMaximum, .4f);
    REQUIRE_EQ(projected.value().alphaThresholdOffset, .25f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalColorMaskMinimum, .1f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalColorMaskMaximum, .2f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalOverlayMaskMinimum, .3f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalOverlayMaskMaximum, .4f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalAlphaThresholdOffset, .25f);
    REQUIRE_EQ(projected.value().surface.specularFactor, .25f);
    REQUIRE_EQ(projected.value().motion.time, 7.0);

    settings.motionLayer = 9;
    REQUIRE(!configureVegetationDetails(baseSurface, baseMotion, settings).ok());
    REQUIRE_EQ(baseSurface.specularFactor, .25f);
    REQUIRE_EQ(baseMotion.time, 7.0);
}

TEST_CASE("graphics.vegetationDetailsPersistenceIsExactAndTransactional") {
    VegetationDetailSettings settings;
    settings.colorsLayer          = 8;
    settings.extrasLayer          = 7;
    settings.motionLayer          = 6;
    settings.globalColor          = .25f;
    settings.overlayMaskMinimum   = .15f;
    settings.overlayMaskMaximum   = .85f;
    settings.motionHighlight      = {2.f, 3.f, 4.f};
    settings.interactionAmplitude = 1.25f;
    auto document                 = snapshotVegetationDetails(settings);
    REQUIRE(document.ok());
    auto restored = restoreVegetationDetails(document.value());
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().colorsLayer, uint8_t(8));
    CHECK_EQ(restored.value().extrasLayer, uint8_t(7));
    CHECK_EQ(restored.value().motionLayer, uint8_t(6));
    CHECK_EQ(restored.value().globalColor, .25f);
    CHECK_EQ(restored.value().overlayMaskMinimum, .15f);
    CHECK_EQ(restored.value().overlayMaskMaximum, .85f);
    CHECK_EQ(restored.value().motionHighlight[2], 4.f);
    CHECK_EQ(restored.value().interactionAmplitude, 1.25f);

    auto unknown                                     = document.value();
    (*unknown.getIf<eve::Value::Object>())["future"] = eve::Value(true);
    CHECK(!restoreVegetationDetails(unknown).ok());
    (*unknown.getIf<eve::Value::Object>()).erase("future");
    (*unknown.getIf<eve::Value::Object>())["version"] = eve::Value(2);
    auto future                                       = restoreVegetationDetails(unknown);
    REQUIRE(!future.ok());
    CHECK_EQ(future.error()->code(), eve::DiagnosticCode::UnknownVersion);

    settings.globalAlpha = 2.f;
    CHECK(!snapshotVegetationDetails(settings).ok());
}

// graphics_vegetation_field_target.cpp
using namespace eve::editing;
using namespace eve::graphics;
using namespace eve::graphics_editing;

namespace {
SelectionSnapshot select(const VegetationFieldTarget &target, const StableId &id) {
    SelectionSnapshot result;
    result.items.push_back({SelectionDomain::Custom, target.targetId(), id, "vegetation-field-element"});
    return result;
}
}  // namespace

TEST_CASE("graphics.vegetation.field_target_property_and_gizmo_share_atomic_transform") {
    VegetationField   field;
    VegetationElement first;
    first.priority = 10;
    first.center   = {1.f, 2.f, 3.f};
    VegetationElement second;
    second.priority = -2;
    REQUIRE(field.replace({}, std::vector<VegetationElement>{first, second}).ok());
    auto created = VegetationFieldTarget::create("field", field);
    REQUIRE(created.ok());
    auto      &target = *created.value();
    const auto ids    = target.elementIds();
    REQUIRE_EQ(ids.size(), 2U);
    const auto selection = select(target, ids[0]);

    auto center = target.makeSet(selection, PropertyPath("element.center"), Value::Array{4.0, 5.0, 6.0},
                                 PropertySetMode::Absolute);
    REQUIRE(center.ok());
    auto staged = target.cloneDomainState();
    REQUIRE(staged->applyDomainOperation(center.value()).ok());
    CHECK_EQ(field.revision(), 1U);
    REQUIRE(target.commitDomainState(std::move(staged)).ok());
    CHECK_EQ(field.revision(), 2U);

    auto transform = target.makeTransform(selection, {7.f, 8.f, 9.f}, {2.f, 3.f, 4.f}, 0.75f);
    REQUIRE(transform.ok());
    REQUIRE(target.applyDomainOperation(transform.value()).ok());
    auto elements = field.snapshotElements();
    REQUIRE(elements.ok());
    REQUIRE_EQ(elements.value().size(), 2U);
    CHECK_EQ(elements.value()[0].center.x, 7.f);
    CHECK_EQ(elements.value()[0].extents.z, 4.f);
    CHECK_EQ(elements.value()[0].yaw, 0.75f);

    DomainOperation undo = transform.value();
    undo.payload         = transform.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    elements = field.snapshotElements();
    REQUIRE(elements.ok());
    CHECK_EQ(elements.value()[0].center.x, 4.f);
}

TEST_CASE("graphics.vegetation.field_target_rejects_stale_runtime_and_invalid_extents") {
    VegetationField field;
    REQUIRE(field.replace({}, std::vector<VegetationElement>{VegetationElement{}}).ok());
    auto created = VegetationFieldTarget::create("field", field);
    REQUIRE(created.ok());
    auto      &target    = *created.value();
    const auto selection = select(target, target.elementIds().front());

    auto invalid = target.makeSet(selection, PropertyPath("element.extents"), Value::Array{1.0, 0.0, 1.0},
                                  PropertySetMode::Absolute);
    CHECK(!invalid.ok());
    REQUIRE(field.replace({}, std::vector<VegetationElement>{VegetationElement{}}).ok());
    auto stale = target.makeTransform(selection, {}, {1.f, 1.f, 1.f}, 0.f);
    CHECK(!stale.ok());
    CHECK(!target.currentRevision(selection).ok());
}
}  // namespace test_graphics_vegetation
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_vegetation_gizmo)
#include "PrivateTestScope.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <memory>
#include "asset/graphics/EvpackImageLoader.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/PbrSurface.h"
#include "graphics/Texture.h"
#include "graphics/VegetationField.h"
#include "graphics/VegetationMaskPacking.h"
#include "graphics/VegetationMotion.h"
#include "graphics/VegetationNormal.h"
#include "graphics/VegetationPacking.h"
#include "graphics/editing/VegetationFieldGizmo.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_vegetation_gizmo.cpp
namespace test_graphics_vegetation_gizmo {
// graphics_vegetation_gizmo.cpp
using namespace eve;
using namespace eve::graphics;
using namespace eve::graphics_editing;

TEST_CASE("graphics.vegetation.gizmo_preserves_rotated_shapes_channels_and_revision") {
    VegetationElement box;
    box.channel  = VegetationChannel::Motion;
    box.shape    = VegetationShape::Box;
    box.center   = {1.f, 2.f, 3.f};
    box.extents  = {4.f, 5.f, 6.f};
    box.yaw      = 0.75f;
    box.opacity  = 0.6f;
    box.priority = 2;
    box.mask     = {1, 1, {{1.f, 1.f, 1.f, 1.f}}};

    VegetationElement ellipsoid;
    ellipsoid.channel  = VegetationChannel::Color;
    ellipsoid.shape    = VegetationShape::Ellipsoid;
    ellipsoid.center   = {-2.f, 0.5f, 7.f};
    ellipsoid.extents  = {1.f, 2.f, 3.f};
    ellipsoid.yaw      = -0.25f;
    ellipsoid.priority = 1;

    VegetationField  field;
    const std::array elements{box, ellipsoid};
    REQUIRE(field.replace({}, elements).ok());
    VegetationFieldGizmoBuilder builder;
    auto                        result = builder.build("vegetation-volume", field.revision(), field, 2);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().status, StatusCode::Applied);
    CHECK_EQ(result.value().target, std::string("vegetation-volume"));
    CHECK_EQ(result.value().targetRevision, field.revision());
    REQUIRE_EQ(result.value().primitives.size(), 2u);

    const auto &first = result.value().primitives[0];
    CHECK_EQ(first.kind, std::string("ellipsoid"));
    CHECK_EQ(first.position[0], -2.0);
    CHECK_EQ(first.size[1], 4.0);
    CHECK_EQ(first.yaw, -0.25);
    CHECK_EQ(first.color[1], 0.85);
    CHECK(!first.dashed);

    const auto &second = result.value().primitives[1];
    CHECK_EQ(second.kind, std::string("obb"));
    CHECK_EQ(second.position[2], 3.0);
    CHECK_EQ(second.size[0], 8.0);
    CHECK_EQ(second.yaw, 0.75);
    CHECK_EQ(second.color[2], 1.0);
    CHECK(second.dashed);
}

TEST_CASE("graphics.vegetation.gizmo_rejects_stale_revision_and_budget_without_partial_output") {
    VegetationElement element;
    VegetationField   field;
    REQUIRE(field.replace({}, std::span(&element, 1)).ok());
    VegetationFieldGizmoBuilder builder;

    auto stale = builder.build("vegetation-volume", field.revision() - 1, field);
    CHECK_EQ(stale.code(), StatusCode::Conflict);
    auto overBudget = builder.build("vegetation-volume", field.revision(), field, 0);
    CHECK_EQ(overBudget.code(), StatusCode::Rejected);
}

// graphics_vegetation_mask.cpp
using namespace eve::graphics;
TEST_CASE("graphics.vegetation.mask_orm_uses_green_ao_alpha_smoothness_and_retains_blue") {
    VegetationMask source{2, 1, {glm::vec4(.9f, .2f, .4f, .7f), glm::vec4(.1f, .8f, .6f, .3f)}};
    auto           encoded = packVegetationOrm(source, .5f, .8f);
    REQUIRE(encoded.ok());
    REQUIRE_EQ(encoded.value().width, 2u);
    const auto a = encoded.value().pixels[0];
    REQUIRE(std::abs(a.r - .6f) < 1e-6f);
    REQUIRE(std::abs(a.g - .44f) < 1e-6f);
    REQUIRE_EQ(a.b, 0.f);
    REQUIRE_EQ(a.a, .4f);
    REQUIRE(std::abs(encoded.value().pixels[1].r - .9f) < 1e-6f);
    REQUIRE(std::abs(encoded.value().pixels[1].g - .76f) < 1e-6f);
    auto disabled = packVegetationOrm(source, 0, 0);
    REQUIRE(disabled.ok());
    REQUIRE_EQ(disabled.value().pixels[0], glm::vec4(1, 1, 0, .4f));
    source.pixels[0] = glm::vec4(0);
    REQUIRE_EQ(encoded.value().pixels[0], a);
}
TEST_CASE("graphics.vegetation.mask_orm_rejects_malformed_linear_inputs") {
    VegetationMask source{1, 1, {glm::vec4(1)}};
    REQUIRE(!packVegetationOrm(source, -.1f, 1).ok());
    REQUIRE(!packVegetationOrm(source, 1, 1.1f).ok());
    source.width = 2;
    REQUIRE(!packVegetationOrm(source, 1, 1).ok());
    source.width       = 1;
    source.pixels[0].g = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!packVegetationOrm(source, 1, 1).ok());
    source.width  = UINT32_MAX;
    source.height = UINT32_MAX;
    REQUIRE(!packVegetationOrm(source, 1, 1).ok());
}

// graphics_vegetation_motion.cpp
using namespace eve::graphics;
namespace {
VegetationMotion isolated() {
    VegetationMotion motion;
    motion.noise   = {1, 1, {glm::vec4(0.5f, 0.5f, 1.f, 1.f)}};
    motion.bending = motion.branch = motion.rolling = motion.flutter = 0.f;
    motion.facing                                                    = 0.f;
    return motion;
}
VegetationField fullWind() {
    VegetationField   field;
    VegetationGlobals globals;
    globals.motion = {1, 0, 1, 0};
    field.replace(globals, {}).expect("test full wind");
    return field;
}
glm::vec3 evaluate(const VegetationField &field, const VegetationVertex &vertex, const VegetationMotion &motion) {
    auto result = deformVegetation(field, std::span(&vertex, 1), motion);
    REQUIRE(result.ok());
    return {result.value().positions[0], result.value().positions[1], result.value().positions[2]};
}
void near(glm::vec3 actual, glm::vec3 expected, float tolerance = 1e-5f) {
    CHECK(glm::length(actual - expected) < tolerance);
}
}  // namespace

TEST_CASE("graphics.vegetation.motion_bending_rotates_about_pivot") {
    auto field     = fullWind();
    auto motion    = isolated();
    motion.bending = 0.25f;
    VegetationVertex v;
    v.pivot    = {2, 0, 3};
    v.position = {2, 2, 3};
    v.bending  = 1.f;
    // Neutral RG noise, full wind: source angle = 0.6 * 2 * amplitude.
    auto actual = evaluate(field, v, motion);
    near(actual, v.pivot + glm::vec3(2.f * std::sin(0.3f), 2.f * std::cos(0.3f), 0));
    CHECK(std::abs(glm::length(actual - v.pivot) - 2.f) < 1e-5f);
}

TEST_CASE("graphics.vegetation.motion_global_wind_is_not_local_interaction_direction") {
    auto field     = fullWind();
    auto motion    = isolated();
    motion.bending = 0.25f;
    VegetationElement element;
    element.channel = VegetationChannel::Motion;
    element.value   = {0, 1, 1, 0};
    REQUIRE(field.replace(field.globalValues(), std::span(&element, 1)).ok());
    VegetationVertex v;
    v.position = {0, 1, 0};
    v.bending  = 1;
    near(evaluate(field, v, motion), {std::sin(0.3f), std::cos(0.3f), 0});
}

TEST_CASE("graphics.vegetation.motion_interaction_uses_fourth_power") {
    VegetationField   field;
    VegetationGlobals globals;
    globals.motion = {0, 1, 0, 0.5f};
    REQUIRE(field.replace(globals, {}).ok());
    auto             motion = isolated();
    VegetationVertex v;
    v.position        = {0, 1, 0};
    v.bending         = 0.5f;
    const float angle = (1.f / 1.0001f) / 16.f;
    near(evaluate(field, v, motion), {0, std::cos(angle), std::sin(angle)});
}

TEST_CASE("graphics.vegetation.motion_branch_squash_then_rolling") {
    auto field         = fullWind();
    auto motion        = isolated();
    motion.branch      = 0.4f;
    motion.rolling     = 0.3f;
    motion.branchScale = 0;
    motion.time        = 3.141592653589793 / 12.;
    VegetationVertex v;
    v.position     = {1, 2, 0};
    v.branch       = 1;
    v.boundsRadius = 2;
    near(evaluate(field, v, motion), {1.8f * std::cos(0.3f), 2.24f, -1.8f * std::sin(0.3f)});
}

TEST_CASE("graphics.vegetation.motion_flutter_texture_and_distance_fade") {
    auto field     = fullWind();
    auto motion    = isolated();
    motion.flutter = 0.2f;
    motion.noise   = {1, 1, {glm::vec4(1, 0, 0.25f, 1)}};
    VegetationVertex v;
    v.position            = {0, 1, 0};
    v.flutter             = 0.5f;
    const float amplitude = 0.1f * std::pow(0.25f, 0.6f);
    near(evaluate(field, v, motion), v.position + glm::vec3(1, -1, -0.5f) * amplitude);
    motion.camera = {0, 1000, 0};
    near(evaluate(field, v, motion), v.position);
}

TEST_CASE("graphics.vegetation.motion_batched_height_offsets_ignore_object_size") {
    auto field       = fullWind();
    auto globals     = field.globalValues();
    globals.vertex.w = 0;
    REQUIRE(field.replace(globals, {}).ok());
    auto motion      = isolated();
    motion.bending   = 0.25f;
    motion.batchMode = VegetationBatchMode::Batched;
    VegetationVertex v;
    v.position     = {0, 1, 0};
    v.bending      = 0.5f;
    v.boundsHeight = 3;
    near(evaluate(field, v, motion), {0.225f, 1, 0});
    motion.batchMode = VegetationBatchMode::Object;
    near(evaluate(field, v, motion), {0, 0, 0});
}

TEST_CASE("graphics.vegetation.motion_time_override_and_object_transform") {
    auto             field = fullWind();
    VegetationMotion motion;
    VegetationVertex v;
    v.position          = {0.1f, 1, 0.2f};
    v.bending           = 0.8f;
    v.branch            = 0.5f;
    v.flutter           = 0.5f;
    motion.time         = 4.5;
    const auto expected = evaluate(field, v, motion);
    motion.time         = 2;
    motion.timeScale    = 2;
    motion.timeOffset   = 0.5;
    motion.timeOverride = 1;
    near(evaluate(field, v, motion), expected);
    motion               = isolated();
    motion.objectToWorld = glm::translate(glm::mat4(1.f), glm::vec3(100000, 200000, 300000));
    motion.objectToWorld = glm::scale(motion.objectToWorld, glm::vec3(2, 3, 4));
    auto output          = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(output.ok());
    near({output.value().positions[0], output.value().positions[1], output.value().positions[2]},
         {100000.2f, 200003.f, 300000.8f}, 0.05f);
    near({output.value().normals[0], output.value().normals[1], output.value().normals[2]}, {0, 1, 0});
    motion.objectToWorld[1] = glm::vec4(0.f);
    REQUIRE(!deformVegetation(field, std::span(&v, 1), motion).ok());
}

TEST_CASE("graphics.vegetation.packing_retains_masks_bounds_uvs_and_pivots") {
    TvePackedVertex packed;
    packed.color     = {0.3f, 0.4f, 0.5f, 0.6f};
    packed.position  = {1, 2, 3};
    packed.texcoord0 = {0.2f, 0.7f, 1048063.f, 4194303.f};
    packed.texcoord1 = {0.1f, 0.2f, 0.3f, 0.4f};
    packed.texcoord3 = {7, 9, 8, 0};
    auto decoded     = decodeTveVegetationVertices(std::span(&packed, 1));
    REQUIRE(decoded.ok());
    const auto &v = decoded.value()[0];
    near(v.pivot, {7, 8, 9});
    CHECK(v.bending == 0.6f);
    CHECK(v.variation == 0.3f);
    CHECK(v.occlusion == 0.4f);
    CHECK(v.detail == 0.5f);
    CHECK(v.branch == 511.f / 2047.f);
    CHECK(v.flutter == 1535.f / 2047.f);
    CHECK(v.boundsHeight == 100.f);
    CHECK(v.boundsRadius == 100.f);
    CHECK(v.texcoord.x == 0.2f);
    CHECK(v.secondaryTexcoord.y == 0.2f);
    CHECK(v.detailCoord.x == 0.3f);
    packed.texcoord0.z = 1.5f;
    CHECK(!decodeTveVegetationVertices(std::span(&packed, 1)).ok());
    packed.texcoord0.z = 4194304.f;
    CHECK(!decodeTveVegetationVertices(std::span(&packed, 1)).ok());
}

TEST_CASE("graphics.vegetation.motion_invalid_noise_and_arithmetic_overflow") {
    auto             field  = fullWind();
    auto             motion = isolated();
    VegetationVertex v;
    motion.noise.width = 2;
    CHECK(!deformVegetation(field, std::span(&v, 1), motion).ok());
    motion               = isolated();
    v.position           = glm::vec3(std::numeric_limits<float>::max());
    motion.objectToWorld = glm::scale(glm::mat4(1.f), glm::vec3(100.f));
    CHECK(!deformVegetation(field, std::span(&v, 1), motion).ok());
}

TEST_CASE("graphics.vegetation.motion_authored_frame_follows_deformation_and_handedness") {
    auto             field  = fullWind();
    auto             motion = isolated();
    VegetationVertex v;
    v.position = {0, 2, 0};
    v.normal   = {0, 0, 1};
    v.bending  = 1;
    v.tangent  = glm::vec4(1, 0, 0, -1);
    auto rest  = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(rest.ok());
    REQUIRE_EQ(rest.value().tangents.size(), size_t(3));
    near({rest.value().tangents[0], rest.value().tangents[1], rest.value().tangents[2]}, {1, 0, 0}, .001f);
    near({rest.value().bitangents[0], rest.value().bitangents[1], rest.value().bitangents[2]}, {0, -1, 0}, .001f);
    motion.bending = .4f;
    auto bent      = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(bent.ok());
    glm::vec3 t(bent.value().tangents[0], bent.value().tangents[1], bent.value().tangents[2]);
    glm::vec3 b(bent.value().bitangents[0], bent.value().bitangents[1], bent.value().bitangents[2]);
    glm::vec3 n(bent.value().normals[0], bent.value().normals[1], bent.value().normals[2]);
    CHECK(std::abs(glm::dot(n, t)) < .002f);
    CHECK(std::abs(glm::dot(n, b)) < .002f);
    CHECK(glm::dot(glm::cross(t, b), n) < -.99f);
    CHECK(glm::length(t - glm::vec3(1, 0, 0)) + glm::length(b - glm::vec3(0, -1, 0)) > .05f);
    auto repeat = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(repeat.ok());
    REQUIRE_EQ(repeat.value().tangents, bent.value().tangents);
    v.tangent->w = 0;
    REQUIRE(!deformVegetation(field, std::span(&v, 1), motion).ok());
    v.tangent.reset();
    auto absent = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(absent.ok());
    REQUIRE(absent.value().tangents.empty());
}

// graphics_vegetation_normal.cpp
using namespace eve::graphics;
TEST_CASE("graphics.vegetation.normal_gpu_matches_filtered_cpu_reference") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    p[]{-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    n[]{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]{.4f, .5f, .4f, .5f, .4f, .5f, .4f, .5f};
    const uint32_t ix[]{0, 2, 1, 0, 3, 2};
    auto          *mesh = gfx->newMeshFromArrays(p, n, uv, 4, ix, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t packed[]{255, 128, 0, 0, 0, 128, 0, 255};
    auto         *texture = gfx->newTexture(2, 1, packed);
    REQUIRE(texture != nullptr);
    Lighting3DPack lighting{};
    lighting.count               = 1;
    lighting.ambient             = {.1f, .1f, .1f, 0};
    lighting.lights[0].posRadius = {.7f, .2f, 1, 0};
    lighting.lights[0].color     = {1, 1, 1, 0};
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->setMesh3DCameraPos({0, 0, 2});
    auto render = [&](const PbrSurface &surface) {
        auto drawSurface                               = surface;
        drawSurface.vegetationColor.backfaceNormalMode = PbrVegetationBackfaceNormalMode::Same;
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DLighting(lighting);
        gfx->setMesh3DMaterial(0, 1);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        REQUIRE(gfx->setMesh3DPbrSurface(&drawSurface).ok());
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.6f, .6f, .6f, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
        REQUIRE(image != nullptr);
        return image->getPixel(32, 32);
    };
    float minimum = 1, maximum = 0;
    for (unsigned layout = 0; layout < 3; ++layout)
        for (float strength : {-2.f, 0.f, 1.f, 2.f}) {
            auto decoded = decodeVegetationNormal({.7f, 128.f / 255.f, 0, .3f},
                                                  static_cast<VegetationNormalEncoding>(layout), strength);
            REQUIRE(decoded.ok());
            auto    direction = glm::normalize(decoded.value());
            uint8_t referencePixel[4]{0, 0, 0, 255};
            for (unsigned c = 0; c < 3; ++c)
                referencePixel[c] = uint8_t(std::lround((direction[c] * .5f + .5f) * 255.f));
            auto *referenceTexture = gfx->newTexture(1, 1, referencePixel);
            REQUIRE(referenceTexture != nullptr);
            PbrSurface surface;
            auto      &binding  = surface.textures[size_t(PbrTextureSlot::Normal)];
            binding.texture     = texture;
            binding.srgbDecode  = false;
            binding.magFilter   = 9729;
            binding.minFilter   = 9729;
            surface.normalMode  = static_cast<PbrNormalMode>(layout + 1);
            surface.normalScale = strength;
            REQUIRE(!gfx->Graphics::setMesh3DPbrSurface(&surface).ok());
            const auto actual   = render(surface);
            minimum             = std::min(minimum, actual.r);
            maximum             = std::max(maximum, actual.r);
            surface.normalMode  = PbrNormalMode::TangentXYZ;
            surface.normalScale = 1;
            binding.texture     = referenceTexture;
            const auto expected = render(surface);
            CHECK(std::abs(actual.r - expected.r) < .015f);
            CHECK(std::abs(actual.g - expected.g) < .015f);
            CHECK(std::abs(actual.b - expected.b) < .015f);
            surface.normalMode = PbrNormalMode::VegetationRG;
            binding.srgbDecode = true;
            REQUIRE(!validatePbrSurface(surface).ok());
            binding.srgbDecode = false;
            binding.texture    = nullptr;
            REQUIRE(!validatePbrSurface(surface).ok());
        }
    CHECK(maximum - minimum > .05f);
    // A material UV mirror/rotation changes sampling, not the source TVE tangent frame.
    const float directionalUv[]{0, 0, 1, 0, 1, 1, 0, 1};
    mesh = gfx->newMeshFromArrays(p, n, directionalUv, 4, ix, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t directionalNormal[]{230, 128, 255, 255};
    auto         *directionalTexture = gfx->newTexture(1, 1, directionalNormal);
    REQUIRE(directionalTexture != nullptr);
    PbrSurface directional;
    directional.normalMode   = PbrNormalMode::VegetationRG;
    auto &normalBinding      = directional.textures[size_t(PbrTextureSlot::Normal)];
    normalBinding.texture    = directionalTexture;
    normalBinding.srgbDecode = false;
    const auto original      = render(directional);
    normalBinding.scale      = {-1, 2};
    const auto mirrored      = render(directional);
    CHECK(std::abs(original.r - mirrored.r) < .005f);
    normalBinding.rotation = 1.57079632679f;
    const auto rotated     = render(directional);
    CHECK(std::abs(original.r - rotated.r) < .005f);
    // The fixture is sensitive to the previous transformed-UV basis.
    normalBinding.rotation      = 0;
    directional.normalMode      = PbrNormalMode::TangentXYZ;
    const auto transformedBasis = render(directional);
    CHECK(std::abs(original.r - transformedBasis.r) > .05f);
    std::vector<float> tangents, bitangents;
    for (unsigned i = 0; i < 4; ++i) {
        tangents.insert(tangents.end(), {-1, 0, 0});
        bitangents.insert(bitangents.end(), {0, 1, 0});
    }
    REQUIRE(mesh->setTangentFrame(tangents, bitangents).ok());
    directional.normalMode = PbrNormalMode::VegetationRG;
    normalBinding.scale    = {1, 1};
    const auto authored    = render(directional);
    CHECK(std::abs(authored.r - transformedBasis.r) < .005f);
    CHECK(std::abs(authored.r - original.r) > .05f);
    auto invalid = tangents;
    invalid[0]   = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!mesh->setTangentFrame(invalid, bitangents).ok());
    REQUIRE_EQ(mesh->importedTangents(), tangents);
    CHECK(std::abs(render(directional).r - authored.r) < .005f);
    REQUIRE(!mesh->setTangentFrame(tangents, tangents).ok());
    REQUIRE(!mesh->setTangentFrame({}, bitangents).ok());
    REQUIRE(mesh->setTangentFrame({}, {}).ok());
    CHECK(std::abs(render(directional).r - original.r) < .005f);
    // An out-of-range UV must use the imported sampler without changing the tangent basis.
    const uint8_t strip[]{40, 128, 255, 255, 90, 128, 255, 255, 180, 128, 255, 255, 230, 128, 255, 255};
    auto         *stripTexture = gfx->newTexture(4, 1, strip);
    REQUIRE(stripTexture != nullptr);
    normalBinding.scale     = {0, 0};
    normalBinding.offset    = {1.375f, .5f};
    normalBinding.minFilter = normalBinding.magFilter = 9728;
    for (const auto wrap : {10497u, 33071u, 33648u}) {
        normalBinding.wrapS            = wrap;
        normalBinding.texture          = stripTexture;
        const auto     sampled         = render(directional);
        const unsigned pixel           = wrap == 10497u ? 1u : wrap == 33071u ? 3u : 2u;
        auto          *expectedTexture = gfx->newTexture(1, 1, strip + pixel * 4);
        REQUIRE(expectedTexture != nullptr);
        normalBinding.texture = expectedTexture;
        const auto expected   = render(directional);
        CHECK(std::abs(sampled.r - expected.r) < .005f);
        CHECK(std::abs(sampled.g - expected.g) < .005f);
        CHECK(std::abs(sampled.b - expected.b) < .005f);
    }
    std::vector<uint8_t> mipBytes;
    for (const auto level : {0, 1, 2}) {
        const unsigned size = 4u >> level;
        for (unsigned pixel = 0; pixel < size * size; ++pixel)
            mipBytes.insert(mipBytes.end(), {uint8_t(level == 2 ? 230 : 40), 128, 255, 255});
    }
    eve::asset_graphics::GraphicsImageFactoryAdapter imageFactory(*gfx);
    auto                                             mipped = imageFactory.uploadRgba8MipChain(4, 4, 3, mipBytes);
    REQUIRE(mipped.ok());
    REQUIRE_EQ(mipped.value()->getMipmapCount(), 3);
    normalBinding.texture   = mipped.value();
    normalBinding.scale     = {1024, 1024};
    normalBinding.offset    = {0, 0};
    normalBinding.minFilter = 9987;
    normalBinding.magFilter = 9729;
    const auto distant      = render(directional);
    normalBinding.texture   = directionalTexture;
    const auto lastLevel    = render(directional);
    CHECK(std::abs(distant.r - lastLevel.r) < .005f);
    CHECK(std::abs(distant.g - lastLevel.g) < .005f);
    CHECK(std::abs(distant.b - lastLevel.b) < .005f);
    normalBinding.texture   = mipped.value();
    normalBinding.scale     = {0, 0};
    normalBinding.minFilter = 9729;
    const auto baseLevel    = render(directional);
    CHECK(std::abs(baseLevel.r - distant.r) > .05f);
    REQUIRE(!imageFactory.uploadRgba8MipChain(4, 4, 2, mipBytes).ok());
    REQUIRE(!imageFactory.uploadRgba8MipChain(4, 4, 3, std::span(mipBytes).first(mipBytes.size() - 1)).ok());
    REQUIRE(!imageFactory.uploadRgba8MipChain(UINT32_MAX, 4, 3, mipBytes).ok());
    REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
    REQUIRE(imageFactory.releaseImage(mipped.value()).ok());
}
TEST_CASE("graphics.vegetation.normal_source_layout_strength_and_unnormalized_z") {
    const glm::vec4 sample(.75f, .25f, .1f, .5f);
    for (float strength : {-8.f, -1.f, 0.f, .2f, 1.f, 8.f}) {
        auto rg  = decodeVegetationNormal(sample, VegetationNormalEncoding::RedGreen, strength);
        auto rag = decodeVegetationNormal(sample, VegetationNormalEncoding::RedAlphaGreen, strength);
        auto ag  = decodeVegetationNormal(sample, VegetationNormalEncoding::AlphaGreen, strength);
        REQUIRE(rg.ok());
        REQUIRE(rag.ok());
        REQUIRE(ag.ok());
        REQUIRE_EQ(rg.value(), glm::vec3(.5f * strength, -.5f * strength, 1));
        REQUIRE_EQ(rag.value(), glm::vec3(-.25f * strength, -.5f * strength, 1));
        REQUIRE_EQ(ag.value(), glm::vec3(0, -.5f * strength, 1));
    }
    // Filtering must happen before the nonlinear R*A decode.
    auto filtered = decodeVegetationNormal(glm::vec4(.5f, .5f, 0, .5f), VegetationNormalEncoding::RedAlphaGreen, 1);
    REQUIRE(filtered.ok());
    REQUIRE_EQ(filtered.value(), glm::vec3(-.5f, 0, 1));
    auto left  = decodeVegetationNormal(glm::vec4(1, .5f, 0, 0), VegetationNormalEncoding::RedAlphaGreen, 1);
    auto right = decodeVegetationNormal(glm::vec4(0, .5f, 0, 1), VegetationNormalEncoding::RedAlphaGreen, 1);
    REQUIRE(left.ok());
    REQUIRE(right.ok());
    REQUIRE_EQ((left.value().x + right.value().x) * .5f, -1.f);
}
TEST_CASE("graphics.vegetation.normal_rejects_invalid_input_without_clamping") {
    const auto layout = VegetationNormalEncoding::RedGreen;
    for (float value : {-8.01f, 8.01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        REQUIRE(!decodeVegetationNormal(glm::vec4(.5f), layout, value).ok());
    for (unsigned c = 0; c < 4; ++c)
        for (float value : {-.01f, 1.01f, std::numeric_limits<float>::quiet_NaN()}) {
            glm::vec4 sample(.5f);
            sample[c] = value;
            REQUIRE(!decodeVegetationNormal(sample, layout, 1).ok());
        }
    REQUIRE(!decodeVegetationNormal(glm::vec4(.5f), static_cast<VegetationNormalEncoding>(99), 1).ok());
}

TEST_CASE("graphics.vegetation.explicit_mip_provider_absence_is_observable") {
    struct BaseOnly final : IImageResourceFactory {
        unsigned               calls = 0;
        eve::Result<Texture *> uploadRgba8(uint32_t, uint32_t, const uint8_t *, bool) override {
            ++calls;
            return eve::Result<Texture *>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "fixture"));
        }
        eve::Result<void> releaseImage(Texture *) override { return eve::Result<void>::success(); }
    } factory;
    const std::array<uint8_t, 4> pixel{128, 128, 255, 255};
    auto                         result = factory.uploadRgba8MipChain(1, 1, 1, pixel);
    REQUIRE(!result.ok());
    REQUIRE_EQ(result.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
    REQUIRE_EQ(factory.calls, 0u);
}
}  // namespace test_graphics_vegetation_gizmo
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_identity_result_audit)
#include "PrivateTestScope.h"
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include "common/Identity.h"
#include "common/RuntimeHandle.h"
#include "common/UuidV7.h"
#include "effects/EffectContainer.h"
#include "transaction/Transaction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// identity_result_audit.cpp
namespace test_identity_result_audit {
namespace {

template <class Id>
Id parsedId(const char *text) {
    const auto parsed = Id::parse(text);
    return parsed ? *parsed : Id::nil();
}

eve::UuidEntropySource deterministicEntropy() {
    auto cursor = std::make_shared<std::uint8_t>(1);
    return [cursor](std::span<std::uint8_t> bytes) {
        for (auto &byte : bytes) byte = (*cursor)++;
        return true;
    };
}

}  // namespace

static_assert(!std::is_convertible_v<eve::AssetGuid, eve::DocumentId>);
static_assert(!std::is_convertible_v<eve::EventId, eve::TransactionId>);
static_assert(!std::is_convertible_v<eve::TransactionId, eve::OperationId>);
static_assert(!std::is_convertible_v<eve::SceneObjectId, eve::PersistentId>);

TEST_CASE("identity.audit.domainIdsShareOneUuidImplementation") {
    constexpr const char *text  = "01020304-0506-0708-090a-0b0c0d0e0f10";
    const auto            asset = parsedId<eve::AssetGuid>(text);
    REQUIRE(!asset.isNil());
    CHECK_EQ(asset.format(), text);
    CHECK_EQ(eve::DocumentId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::SceneObjectId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::ArtifactId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::EventId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::TransactionId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::OperationId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::EffectId::fromUuid(asset).format(), text);

    CHECK(!eve::AssetGuid::parse("asset-01020304-0506-0708-090a-0b0c0d0e0f10").has_value());
    CHECK(!eve::AssetGuid::parse("{01020304-0506-0708-090a-0b0c0d0e0f10}").has_value());
}

TEST_CASE("identity.audit.logicalNamespaceIsCanonicalAndNamesRemainCaseSensitive") {
    REQUIRE(eve::LogicalId::parse("rpg:Skill.Fire").has_value());
    CHECK(!eve::LogicalId::parse("RPG:Skill.Fire").has_value());
    CHECK(!eve::LogicalId::parse("rpg_:Skill.Fire").has_value());
    CHECK(!eve::LogicalId::parse("rpg:skill/fire").has_value());
    CHECK(eve::LogicalId::fromParts("game_event", "Damage.Applied").has_value());
}

TEST_CASE("identity.audit.canonicalTransactionRestoresWithoutStringAllocators") {
    using namespace std::chrono_literals;
    const auto clock = [] { return std::chrono::system_clock::time_point{std::chrono::milliseconds{0x010203040506}}; };
    const auto entropy = deterministicEntropy();

    eve::transaction::Ledger source(eve::PersistentId::nil(), entropy, clock);
    auto                     created = source.create("combat", "command");
    REQUIRE(created.ok());
    auto *plan = std::move(created).takeValue();
    REQUIRE(plan != nullptr);
    const auto transactionId = plan->identity();
    CHECK(!transactionId.isNil());
    CHECK_EQ(plan->id(), transactionId.format());

    auto staged = plan->stage("debit", "treasury", "{\"amount\":10}");
    REQUIRE(staged.ok());
    const auto operationId = std::move(staged).takeValue();
    CHECK(!operationId.isNil());
    const auto *operation = plan->findOperation(operationId);
    REQUIRE(operation != nullptr);
    CHECK_EQ(operation->id, operationId.format());
    CHECK(plan->markValid(operationId).ok());
    CHECK(plan->validate().ok());
    CHECK(plan->commit().ok());

    const auto snapshot = source.snapshotJson();
    CHECK(snapshot.find("transaction-0000000000000001") == std::string::npos);
    CHECK(snapshot.find("operation-0000000000000001") == std::string::npos);

    eve::transaction::Ledger restored(eve::PersistentId::nil(), deterministicEntropy(), clock);
    auto                     restoredResult = restored.restore(snapshot);
    REQUIRE(restoredResult.ok());
    auto *restoredPlan = restored.find(transactionId);
    REQUIRE(restoredPlan != nullptr);
    CHECK_EQ(restoredPlan->identity(), transactionId);
    CHECK(restoredPlan->findOperation(operationId) != nullptr);

    const auto before          = restored.snapshotJson();
    auto       failedCanonical = restored.restore("{\"version\":1,\"nextTransaction\":\"2\",\"plans\":[");
    CHECK(!failedCanonical.ok());
    CHECK_EQ(restored.snapshotJson(), before);
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("identity.audit.runtimeHandleGenerationMakesStaleReuseObservable") {
    struct RegistryTag {};
    using Handle = eve::RuntimeHandle<RegistryTag>;

    constexpr Handle oldHandle(4u, 1u);
    const auto       next = oldHandle.nextGeneration();
    REQUIRE(next.has_value());
    CHECK(next.value() != oldHandle);
    CHECK_EQ(next->index(), oldHandle.index());
    CHECK_EQ(next->generation(), 2u);
    CHECK(!Handle(4u, std::numeric_limits<Handle::generation_type>::max()).nextGeneration().has_value());
    CHECK_EQ(Handle::fromPacked(oldHandle.packed()), oldHandle);
}

TEST_CASE("identity.audit.canonicalEffectUsesUuidAndLegacyFacadeProjectsLocally") {
    using namespace std::chrono_literals;
    const auto clock = [] { return std::chrono::system_clock::time_point{std::chrono::milliseconds{0x010203040506}}; };
    eve::effects::EffectDefinition definition;
    definition.id               = "combat:burn";
    definition.policy.stackMode = eve::effects::StackMode::NewInstance;

    eve::effects::EffectContainer canonical(deterministicEntropy(), clock);
    auto                          applied = canonical.applyCanonical(definition, "actor:1", "spell:1");
    REQUIRE(applied.ok());
    const auto effectId = std::move(applied).takeValue();
    CHECK(!effectId.isNil());
    const auto *effect = canonical.find(effectId);
    REQUIRE(effect != nullptr);
    CHECK_EQ(effect->identity, effectId);
    REQUIRE(canonical.eventAt(0) != nullptr);
    CHECK_EQ(canonical.eventAt(0)->effectIdentity, effectId);
    CHECK(canonical.remove(effectId).ok());

    eve::effects::EffectContainer legacy;
    auto                          legacyResult = legacy.apply(definition, "actor:2", "spell:1");
    REQUIRE(legacyResult.ok());
    CHECK(legacyResult.value().starts_with("effect-"));
    REQUIRE(legacy.find(legacyResult.value()) != nullptr);
    CHECK(legacy.find(legacyResult.value())->identity.isNil());
}
}  // namespace test_identity_result_audit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_kaykit_combat_assets)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "PathBesideSource.h"
#include "action/ActionNotifyRegistry.h"
#include "action/editor/ActionPreviewController.h"
#include "action/editor/ActionTimelineEditor.h"
#include "action/editor/AnimationRootMotionPreviewSource.h"
#include "animation/AnimClip.h"
#include "animation/AnimClipRegistry.h"
#include "animation/AnimImporter.h"
#include "animation/AnimSkeleton.h"
#include "editor/EditorWorkspace.h"
#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// kaykit_combat_assets.cpp
namespace test_kaykit_combat_assets {
namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::Duration seconds(double value) {
    auto converted = eve::Duration::fromSeconds(value);
    REQUIRE(converted.ok());
    return converted.value();
}

int animationIndex(const eve::model3d::ModelData &model, std::string_view name) {
    for (int index = 0; index < model.getAnimationCount(); ++index)
        if (model.getAnimationName(index) == name) return index;
    return -1;
}

int firstSkinnedMesh(const eve::model3d::ModelData &model) {
    for (int index = 0; index < model.getMeshCount(); ++index)
        if (model.hasBones(index)) return index;
    return -1;
}

std::unique_ptr<eve::animation::AnimClip> loadClip(eve::model3d::Model3D              &module,
                                                   const eve::animation::AnimSkeleton &skeleton,
                                                   const std::string &library, std::string_view clipName) {
    eve::model3d::ModelData *model = module.newModelDataFromFile(library);
    REQUIRE(model != nullptr);
    const int index = animationIndex(*model, clipName);
    REQUIRE(index >= 0);
    std::unique_ptr<eve::animation::AnimClip> clip(
        eve::animation::AnimImporter::loadClipFromModel(model, &skeleton, index));
    REQUIRE(clip.get() != nullptr);
    REQUIRE_EQ(clip->getName(), clipName);
    REQUIRE(clip->getDuration() > 0.0f);
    REQUIRE(clip->getTrackCount() >= skeleton.getBoneCount());
    return clip;
}

eve::action::ActionTimeline lightAttackTimeline(float clipDuration) {
    eve::action::ActionTimeline timeline;
    timeline.actionId     = id("combat:light-attack-kaykit");
    timeline.duration     = seconds(clipDuration);
    timeline.animationUri = "asset://kaykit/Rig_Medium_CombatMelee.glb#Melee_1H_Attack_Chop";
    timeline.metadata.emplace("character", eve::Value("asset://kaykit/Knight.glb"));
    timeline.metadata.emplace("weapon", eve::Value("asset://kaykit/sword_1handed.gltf"));
    timeline.metadata.emplace("sourceLicense", eve::Value("CC0-1.0"));

    eve::action::ActionTrack gameplay;
    gameplay.id    = id("kaykit-track:gameplay");
    gameplay.label = "Gameplay";
    gameplay.kind  = eve::action::ActionTrackKind::Gameplay;
    gameplay.states.push_back({id("kaykit-state:hitbox"),
                               id("combat:hitbox-window"),
                               seconds(clipDuration * 0.30),
                               seconds(clipDuration * 0.62),
                               {{"hitbox", eve::Value("weapon.main")}}});
    gameplay.states.push_back({id("kaykit-state:combo"),
                               id("input:combo-window"),
                               seconds(clipDuration * 0.55),
                               seconds(clipDuration * 0.82),
                               {{"input", eve::Value("Ability.Combat.Attack.Light")}}});
    gameplay.notifies.push_back({id("kaykit-notify:damage"),
                                 id("combat:damage"),
                                 seconds(clipDuration * 0.46),
                                 {{"damageType", eve::Value("Damage.Physical.Slash")}, {"amount", eve::Value(18)}}});

    eve::action::ActionTrack presentation;
    presentation.id    = id("kaykit-track:presentation");
    presentation.label = "Presentation";
    presentation.kind  = eve::action::ActionTrackKind::Effect;
    presentation.notifies.push_back({id("kaykit-notify:swing-audio"),
                                     id("presentation:audio"),
                                     seconds(clipDuration * 0.32),
                                     {{"uri", eve::Value("asset://audio/sword-whoosh")}}});
    presentation.notifies.push_back(
        {id("kaykit-notify:swing-vfx"),
         id("presentation:vfx"),
         seconds(clipDuration * 0.34),
         {{"uri", eve::Value("asset://vfx/sword-arc")}, {"lifetimeSeconds", eve::Value(0.45)}}});
    presentation.notifies.push_back({id("kaykit-notify:impact-camera"),
                                     id("presentation:camera"),
                                     seconds(clipDuration * 0.46),
                                     {{"cue", eve::Value("combat:light-impact")}}});

    eve::action::ActionTrack movement;
    movement.id    = id("kaykit-track:movement");
    movement.label = "Root Motion";
    movement.kind  = eve::action::ActionTrackKind::Movement;
    movement.states.push_back({id("kaykit-state:root-motion"),
                               id("movement:root-motion-window"),
                               eve::Duration::zero(),
                               timeline.duration,
                               {{"mode", eve::Value("animation")}}});

    timeline.tracks.push_back(std::move(gameplay));
    timeline.tracks.push_back(std::move(presentation));
    timeline.tracks.push_back(std::move(movement));
    return timeline;
}

class RecordingPreviewSink final : public eve::action::IActionPreviewSink {
public:
    [[nodiscard]] eve::Result<void> prepare(const eve::action::ActionPreviewFrame &frame) override {
        prepared = frame;
        return eve::Result<void>::success();
    }

    void present(const eve::action::ActionPreviewFrame &frame) noexcept override { presented = frame; }
    void discardPrepared() noexcept override { discarded = true; }

    std::optional<eve::action::ActionPreviewFrame> prepared;
    std::optional<eve::action::ActionPreviewFrame> presented;
    bool                                           discarded = false;
};

}  // namespace

TEST_CASE("kaykitCombatAssets.importRigClipsAndDriveActionEditorPreview") {
    const std::string assetDir =
        eve_test_path::pathBesideTestDir(__FILE__, "../examples/combat-action-editor/assets/kaykit");
    auto *filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem != nullptr);
    REQUIRE(filesystem->setIdentity("ev_ut_kaykit_combat", true));
    REQUIRE(filesystem->setupWriteDirectory());
    filesystem->allowMountingForPath(assetDir);
    REQUIRE(filesystem->mount(assetDir, "", false));

    auto *modelModule = eve::model3d::Model3D::create();
    REQUIRE(modelModule != nullptr);
    eve::model3d::ModelData *knight = modelModule->newModelDataFromFile("Knight.glb");
    REQUIRE(knight != nullptr);
    REQUIRE(knight->getMeshCount() > 0);
    const int skinnedMesh = firstSkinnedMesh(*knight);
    REQUIRE(skinnedMesh >= 0);
    REQUIRE(knight->getBoneCount(skinnedMesh) >= 20);

    eve::model3d::ModelData *sword = modelModule->newModelDataFromFile("sword_1handed.gltf");
    REQUIRE(sword != nullptr);
    REQUIRE(sword->getMeshCount() > 0);

    std::unique_ptr<eve::animation::AnimSkeleton> skeleton(eve::animation::AnimImporter::loadSkeletonFromModel(knight));
    REQUIRE(skeleton.get() != nullptr);
    REQUIRE(skeleton->getBoneCount() >= knight->getBoneCount(skinnedMesh));

    struct RequiredClip {
        const char *library;
        const char *name;
    };
    const std::array<RequiredClip, 10> coverage = {{
        {"Rig_Medium_General.glb", "Idle_A"},
        {"Rig_Medium_General.glb", "Hit_A"},
        {"Rig_Medium_General.glb", "Death_A"},
        {"Rig_Medium_MovementBasic.glb", "Running_A"},
        {"Rig_Medium_MovementBasic.glb", "Jump_Full_Short"},
        {"Rig_Medium_MovementAdvanced.glb", "Dodge_Forward"},
        {"Rig_Medium_CombatMelee.glb", "Melee_1H_Attack_Chop"},
        {"Rig_Medium_CombatMelee.glb", "Melee_Block"},
        {"Rig_Medium_CombatRanged.glb", "Ranged_Bow_Release"},
        {"Rig_Medium_CombatRanged.glb", "Ranged_Magic_Shoot"},
    }};
    for (const auto &required : coverage) {
        auto clip = loadClip(*modelModule, *skeleton, required.library, required.name);
        CHECK(clip->getDuration() > 0.0f);
    }

    auto attack   = loadClip(*modelModule, *skeleton, "Rig_Medium_CombatMelee.glb", "Melee_1H_Attack_Chop");
    auto timeline = lightAttackTimeline(attack->getDuration());
    REQUIRE(timeline.validate().ok());

    auto notifyRegistry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(notifyRegistry.ok());
    auto events = timeline.sample(eve::Duration::zero(), timeline.duration, true);
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), 10u);
    for (const auto &event : events.value()) REQUIRE(notifyRegistry.value().validate(event).ok());

    eve::editor::ActionTimelineEditor editor("asset.kaykit.light-attack", timeline);
    eve::editor::EditorWorkspace      workspace("kaykit.combat", "KayKit Combat Action Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    CHECK_EQ(workspace.getActivePanel(), "action.timeline");

    const auto originalHitStart = timeline.tracks[0].states[0].start;
    REQUIRE(editor
                .resizeState(id("kaykit-state:hitbox"), seconds(attack->getDuration() * 0.28),
                             seconds(attack->getDuration() * 0.64))
                .ok());
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.target().timeline().tracks[0].states[0].start, originalHitStart);

    eve::animation::AnimClipRegistry::registerPath(timeline.animationUri, attack.get());
    eve::editor::AnimationRootMotionPreviewSource rootMotion;
    RecordingPreviewSink                          sink;
    eve::editor::ActionPreviewController          preview(editor, sink, &rootMotion);
    REQUIRE(preview.setRootMotionSampleCount(16).ok());
    REQUIRE(preview.refresh().ok());
    REQUIRE(sink.presented.has_value());
    CHECK(static_cast<int>(sink.presented->rootMotionState) ==
          static_cast<int>(eve::action::RootMotionPreviewState::Available));
    REQUIRE_EQ(sink.presented->rootMotionPath.size(), 16u);

    editor.play();
    auto advanced = preview.update(timeline.duration);
    REQUIRE(advanced.ok());
    REQUIRE_EQ(advanced.value(), 10u);
    REQUIRE(sink.presented.has_value());
    REQUIRE_EQ(sink.presented->cues.size(), 10u);
    CHECK(!sink.discarded);

    CHECK(filesystem->unmount(assetDir));
}
}  // namespace test_kaykit_combat_assets
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_map_tile_editing)
#include "PrivateTestScope.h"
#include <bit>
#include <cstdint>
#include "map/TileLayer.h"
#include "map/editing/TileLayerTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// map_tile_editing.cpp
namespace test_map_tile_editing {
TEST_CASE("map.editing.preservesFlipBitsWhenRestoringFieldValue") {
    auto *layer = eve::map::TileLayer::createLayer(2, 2, 16.f, 16.f);
    layer->setVisible(false);
    const int flipped = std::bit_cast<int>(uint32_t{0x80000001});
    layer->setTile(0, 0, flipped);
    eve::map_editing::TileLayerTarget target("tile", layer);
    const int                         before = target.readInt(0, 0);
    CHECK_EQ(before, flipped);
    CHECK(target.writeInt(0, 0, 2) == eve::editing::FieldWriteStatus::Applied);
    CHECK(target.writeInt(0, 0, before) == eve::editing::FieldWriteStatus::Applied);
    CHECK_EQ(uint32_t(layer->getTile(0, 0)), uint32_t{0x80000001});
    layer->fillRect(0, 0, 2, 2, flipped);
    CHECK_EQ(layer->getTile(1, 1), flipped);
    layer->fill(flipped);
    CHECK_EQ(layer->getTile(1, 0), flipped);
}

TEST_CASE("map.editing.terrainBrushPreservesManualNeighbors") {
    auto *layer = eve::map::TileLayer::createLayer(3, 3, 16.f, 16.f);
    layer->setVisible(false);
    layer->fill(42);
    layer->setTerrainRule(7, 1, 0);
    layer->paintTerrain(1, 1, 1);
    CHECK_EQ(layer->getTile(1, 1), 7);
    CHECK_EQ(layer->getTile(0, 0), 42);
    CHECK_EQ(layer->getTile(2, 1), 42);
    layer->eraseTerrainRect(1, 1, 1, 1);
    CHECK_EQ(layer->getTile(1, 1), 0);
    CHECK_EQ(layer->getTile(0, 0), 42);
}
}  // namespace test_map_tile_editing
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_npc_ai_navigation)
#include "PrivateTestScope.h"
#include <map>
#include "npc_ai/Navigation.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// npc_ai_navigation.cpp
namespace test_npc_ai_navigation {
using namespace eve::npc_ai;

namespace {
class FixedRequestFactory final : public INavigationRequestFactory {
public:
    eve::Result<NavigationRequest> create(const TaskContext &context, const TaskSpec &) const override {
        return eve::Result<NavigationRequest>::success(
            {context.agent, {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, 0.5, 0.25, "walk", context.simulationTick});
    }
};

class FakeNavigationProvider final : public INavigationProvider {
public:
    eve::Result<NavigationTicket> begin(const NavigationRequest &request) override {
        ++begins;
        lastRequest = request;
        const NavigationTicket ticket(nextIndex++, 1);
        polls.emplace(ticket, 0);
        return eve::Result<NavigationTicket>::success(ticket);
    }

    eve::Result<NavigationProgress> poll(NavigationTicket ticket, std::uint64_t) override {
        auto found = polls.find(ticket);
        if (found == polls.end()) {
            return eve::Result<NavigationProgress>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "injected stale navigation ticket", {}, {}, "npc_ai.test"));
        }
        ++found->second;
        ++pollCount;
        if (arriveOnSecondPoll && found->second >= 2) {
            polls.erase(found);
            return eve::Result<NavigationProgress>::success({NavigationPhase::Arrived, {0.0, 0.0, 0.0}, 0.0});
        }
        return eve::Result<NavigationProgress>::success({NavigationPhase::Moving, {1.0, 0.0, 0.0}, 10.0});
    }

    void abandon(NavigationTicket ticket) noexcept override {
        ++abandons;
        polls.erase(ticket);
    }

    bool              arriveOnSecondPoll = true;
    int               begins             = 0;
    int               pollCount          = 0;
    int               abandons           = 0;
    NavigationRequest lastRequest;

private:
    std::uint32_t                   nextIndex = 0;
    std::map<NavigationTicket, int> polls;
};

BehaviorDefinition movingBehavior() {
    BehaviorDefinition behavior;
    behavior.id           = "moving";
    behavior.initialState = "move";
    StateDefinition move;
    move.id = "move";
    move.tasks.push_back({"go", "move_to"});
    move.transitions.push_back({"done", "task.succeeded.go", {}, 1});
    move.transitions.push_back({"idle", "interrupt", {}, 2});
    StateDefinition done;
    done.id = "done";
    StateDefinition idle;
    idle.id         = "idle";
    behavior.states = {move, done, idle};
    return behavior;
}
}  // namespace

TEST_CASE("npc_ai.navigationTaskCompletesAsynchronously") {
    NpcAiWorld world;
    auto       provider = std::make_unique<FakeNavigationProvider>();
    auto      *observed = provider.get();
    auto       service  = NavigationTaskService::create(std::move(provider), std::make_unique<FixedRequestFactory>());
    REQUIRE(service.ok());
    REQUIRE(world.registerTaskService("move_to", std::move(service.value())).ok());
    REQUIRE(world.registerBehavior(movingBehavior()).ok());
    auto agent = world.createAgent("moving");
    REQUIRE(agent.ok());

    REQUIRE(world.tick({1, 0.016, 1, 4}).ok());
    CHECK_EQ(observed->begins, 1);
    CHECK_EQ(observed->pollCount, 1);
    REQUIRE(world.tick({2, 0.016, 1, 4}).ok());
    CHECK_EQ(observed->pollCount, 2);
    REQUIRE(world.tick({3, 0.016, 1, 4}).ok());
    auto snapshot = world.snapshot(agent.value());
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().activeState, std::string("done"));
    CHECK_EQ(observed->lastRequest.requestedTick, 1u);
}

TEST_CASE("npc_ai.navigationTicketIsAbandonedWhenStateExits") {
    NpcAiWorld world;
    auto       provider          = std::make_unique<FakeNavigationProvider>();
    provider->arriveOnSecondPoll = false;
    auto *observed               = provider.get();
    auto  service = NavigationTaskService::create(std::move(provider), std::make_unique<FixedRequestFactory>());
    REQUIRE(service.ok());
    REQUIRE(world.registerTaskService("move_to", std::move(service.value())).ok());
    REQUIRE(world.registerBehavior(movingBehavior()).ok());
    auto agent = world.createAgent("moving");
    REQUIRE(agent.ok());
    REQUIRE(world.tick({1, 0.016, 1, 4}).ok());
    REQUIRE(world.signal(agent.value(), "interrupt").ok());
    REQUIRE(world.tick({2, 0.016, 1, 4}).ok());
    CHECK_EQ(observed->abandons, 1);
    auto snapshot = world.snapshot(agent.value());
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().activeState, std::string("idle"));
}
}  // namespace test_npc_ai_navigation
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_attach_extra)
#include "PrivateTestScope.h"
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "PathBesideSource.h"
#include "ResourceTestSupport.h"
#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/Animation.h"
#include "animation/SpineAnim.h"
#include "animation/SpineSkeleton.h"
#include "animation/SpineSkeletonData.h"
#include "filesystem/Filesystem.h"
#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/Solver2D.h"
#include "ik/Solver3D.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// particles_attach_extra.cpp
namespace test_particles_attach_extra {
using namespace eve::particles;
using namespace eve::animation;
using namespace eve::ik;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {


EVE_DEFINE_PATH_BESIDE_SOURCE()

bool fileExists(const std::string &path) { return std::filesystem::is_regular_file(path); }

std::string cesiumManDir() { return pathBesideThisSource("assets/skinned/cesium_man/glTF"); }

bool ensureSkinnedAssets() {
    const std::string gltf = cesiumManDir() + "/CesiumMan.gltf";
    if (fileExists(gltf)) return true;
    std::printf("particles.attach_extra: missing %s\n", gltf.c_str());
    return false;
}

eve::model3d::ModelData *loadCesiumMan(const char *fsIdentity) {
    const std::string dir = cesiumManDir();
    auto             *fs  = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity(fsIdentity, true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));
    return eve::model3d::Model3D::create()->newModelDataFromFile("CesiumMan.gltf");
}

int findFirstSkinnedMesh(const eve::model3d::ModelData *model) {
    for (int i = 0; i < model->getMeshCount(); ++i) {
        if (model->hasBones(i)) return i;
    }
    return -1;
}

const char *kSpineJson = R"JSON({
  "skeleton": { "spine": "4.1.00" },
  "bones": [
    { "name": "root" },
    { "name": "hip", "parent": "root", "y": 10 },
    { "name": "chest", "parent": "hip", "y": 20 },
    { "name": "head", "parent": "chest", "y": 15 }
  ],
  "slots": [ { "name": "chest", "bone": "chest", "attachment": "c" } ],
  "skins": [ { "name": "default", "attachments": {
    "chest": { "c": { "width": 8, "height": 8 } }
  }} ],
  "animations": {
    "bob": {
      "bones": {
        "chest": {
          "translate": [
            { "time": 0, "x": 0, "y": 0 },
            { "time": 0.5, "x": 0, "y": 12 },
            { "time": 1.0, "x": 0, "y": 0 }
          ]
        }
      }
    }
  }
})JSON";

}  // namespace

TEST_CASE("particles.attach.resetClearsParticlesKeepsAttach") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(11.f, 22.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(8);
    CHECK_EQ(e->getCount(), 8);
    CHECK(e->isAttached());

    e->reset();
    CHECK_EQ(e->getCount(), 0);
    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachKind(), std::string("ik2d"));
    CHECK(std::fabs(e->getX() - 11.f) < 1e-2f);

    // The emitter lives in a process-wide ECS; detach before the local
    // skeleton is destroyed so later ParticleSimSystem::update() calls in
    // other tests don't dereference the freed skeleton.
    e->detach();
}

TEST_CASE("particles.attach.moveToOverwrittenBySync") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(5.f, 6.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(4);
    e->attachToSkeleton2D(sk.get(), 0);
    e->moveTo(999.f, 888.f);
    CHECK(std::fabs(e->getX() - 999.f) < 1e-3f);

    e->setEmissionRate(0.f);
    e->start();
    ParticleSimSystem::update(0.016f);
    CHECK(std::fabs(e->getX() - 5.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 6.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.applyConfigPreservesAttach") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(30.f, 40.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(32);
    e->attachToSkeleton2D(sk.get(), 0);
    CHECK(e->applyConfig(R"({"emissionRate":40,"particleLifetime":[1,1],"speed":[0,0]})"));
    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachKind(), std::string("ik2d"));
    CHECK(std::fabs(e->getX() - 30.f) < 1e-2f);
    CHECK(std::fabs(e->getEmissionRate() - 40.f) < 1e-3f);

    e->start();
    ParticleSimSystem::update(0.1f);
    CHECK_GE(e->getCount(), 3);
    e->detach();
}

TEST_CASE("particles.attach.layerAndVisibleIndependent") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    sk->initStraightPose(1.f, 2.f, 3.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    e->setAttachPlane("xy");
    e->attachToSkeleton3D(sk.get(), 0);
    e->setLayer(7);
    e->setVisible(false);
    CHECK_EQ(e->getLayer(), 7);
    CHECK(!e->isVisible());
    CHECK(e->isAttached());
    ParticleSimSystem::update(0.016f);
    CHECK(std::fabs(e->getX() - 1.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 2.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.ellipseAreaOnBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(50.f, 50.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(80);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionArea("ellipse", 8.f, 4.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(60);

    int inside = 0;
    for (int i = 0; i < e->getCount(); ++i) {
        const auto &p  = e->sim()->particles[size_t(i)];
        const float dx = (p.x - 50.f) / 8.f;
        const float dy = (p.y - 50.f) / 4.f;
        if (dx * dx + dy * dy <= 1.01f) ++inside;
    }
    CHECK_EQ(inside, e->getCount());
    e->detach();
}

TEST_CASE("particles.attach.tangentialForceUsesBoneOrigin") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(4);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setTangentialAcceleration(30.f, 30.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(1);
    e->sim()->particles[0].x  = 5.f;
    e->sim()->particles[0].y  = 0.f;
    e->sim()->particles[0].vx = 0.f;
    e->sim()->particles[0].vy = 0.f;

    ParticleSimSystem::update(0.1f);
    // Tangential to radial (+X) is +Y (rdx,rdy)=(1,0) → (-rdy, rdx)=(0,1)
    CHECK(e->sim()->particles[0].vy > 0.f);
    e->detach();
}

TEST_CASE("particles.attach.particleLifetimeExpiresOnBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    e->setParticleLifetime(0.05f, 0.05f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(0.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(5);
    CHECK_EQ(e->getCount(), 5);
    ParticleSimSystem::update(0.1f);
    CHECK_EQ(e->getCount(), 0);
    CHECK(e->isAttached());
    e->detach();
}

TEST_CASE("particles.attach.largeDtBurstOnBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(7.f, 8.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(100);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(50.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->start();
    ParticleSimSystem::update(0.5f);  // ~25 particles
    CHECK_GE(e->getCount(), 20);
    CHECK_LE(e->getCount(), 30);
    for (int i = 0; i < e->getCount(); ++i) {
        CHECK(std::fabs(e->sim()->particles[size_t(i)].x - 7.f) < 1e-2f);
        CHECK(std::fabs(e->sim()->particles[size_t(i)].y - 8.f) < 1e-2f);
    }
    e->detach();
}

TEST_CASE("particles.attach.zeroDtStillSyncs") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(1.f, 1.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(100.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->start();
    e->emit(2);
    CHECK_EQ(e->getCount(), 2);

    sk->setPosition(0, 15.f, 16.f);
    sk->forwardKinematics();
    ParticleSimSystem::update(0.f);  // sync runs; sim early-outs
    CHECK(std::fabs(e->getX() - 15.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 16.f) < 1e-2f);
    CHECK_EQ(e->getCount(), 2);  // no new spawns at dt=0
    e->detach();
}

TEST_CASE("particles.attach.cycleThroughAllKinds") {
    std::unique_ptr<AnimSkeleton> ask(new AnimSkeleton());
    const int                     root = ask->addBone("root", -1);
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    ask->applyBindPose(pose.get());
    pose->setLocalPosition(root, 1.f, 0.f, 0.f);
    pose->computeWorld(ask.get());

    auto                              *animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    std::unique_ptr<SpineSkeleton>     spine(animMod->newSpineSkeleton(data.get()));
    spine->setToSetupPose();
    spine->updateWorldTransform();

    std::unique_ptr<Skeleton2D> sk2(new Skeleton2D());
    sk2->initStraightPose(3.f, 4.f);
    std::unique_ptr<Skeleton3D> sk3(new Skeleton3D());
    sk3->initStraightPose(5.f, 6.f, 7.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(4);

    e->attachToBone(pose.get(), root);
    CHECK_EQ(e->getAttachKind(), std::string("anim"));
    e->attachToSpineBoneByName(spine.get(), "head");
    CHECK_EQ(e->getAttachKind(), std::string("spine"));
    CHECK(e->attach()->pose == nullptr);
    e->attachToSkeleton2D(sk2.get(), 0);
    CHECK_EQ(e->getAttachKind(), std::string("ik2d"));
    CHECK(e->attach()->spine == nullptr);
    e->attachToSkeleton3D(sk3.get(), 0);
    CHECK_EQ(e->getAttachKind(), std::string("ik3d"));
    CHECK(e->attach()->ik2d == nullptr);
    e->detach();
    CHECK_EQ(e->getAttachKind(), std::string("none"));
}

TEST_CASE("particles.attach.scaleChangeMidLifetimeResyncs") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 2.f, 4.f, 0.f);
    pose->computeWorld(sk.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(4);
    e->setAttachScale(1.f);
    e->attachToBone(pose.get(), root);
    CHECK(std::fabs(e->getX() - 2.f) < 1e-3f);

    e->setAttachScale(3.f);
    CHECK(std::fabs(e->getX() - 6.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 12.f) < 1e-3f);
    e->detach();
}

TEST_CASE("particles.attach.spinAndSizeVariationOnBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(32);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setSpin(1.f, 3.f);
    e->setSizeVariation(0.5f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(20);

    bool  spinOk     = false;
    bool  sizeVaried = false;
    float firstSize  = e->sim()->particles[0].size;
    for (int i = 0; i < e->getCount(); ++i) {
        const auto &p = e->sim()->particles[size_t(i)];
        if (p.spin >= 1.f && p.spin <= 3.f) spinOk = true;
        if (std::fabs(p.size - firstSize) > 1e-4f) sizeVaried = true;
    }
    CHECK(spinOk);
    CHECK(sizeVaried);
    e->detach();
}

TEST_CASE("particles.attach.ik2dConstraintsStillEmits") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int                         b1  = sk->createBone(0, 40.f);
    int                         tip = sk->createBone(b1, 40.f);
    sk->initStraightPose(0.f, 0.f);
    sk->setConstraints(b1, -0.2f, 0.2f);

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(32);
    solver->addTarget(tip, 0.f, 80.f, 1.f);
    solver->solve(sk.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->attachToSkeleton2D(sk.get(), tip);
    e->emit(5);
    CHECK_EQ(e->getCount(), 5);
    for (int i = 0; i < 5; ++i) {
        CHECK(std::fabs(e->sim()->particles[size_t(i)].x - sk->getX(tip)) < 1e-1f);
        CHECK(std::fabs(e->sim()->particles[size_t(i)].y - sk->getY(tip)) < 1e-1f);
    }
    e->detach();
}

TEST_CASE("particles.attach.twoEmittersDifferentPlanes") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    sk->initStraightPose(2.f, 4.f, 6.f);

    auto            *mod = Particles::create();
    ParticleEmitter *xy  = mod->newEmitter(4);
    ParticleEmitter *xz  = mod->newEmitter(4);
    xy->setAttachPlane("xy");
    xz->setAttachPlane("xz");
    xy->setAttachScale(1.f);
    xz->setAttachScale(1.f);
    xy->attachToSkeleton3D(sk.get(), 0);
    xz->attachToSkeleton3D(sk.get(), 0);
    CHECK(std::fabs(xy->getX() - 2.f) < 1e-3f);
    CHECK(std::fabs(xy->getY() - 4.f) < 1e-3f);
    CHECK(std::fabs(xz->getX() - 2.f) < 1e-3f);
    CHECK(std::fabs(xz->getY() - 6.f) < 1e-3f);
    xy->detach();
    xz->detach();
}

TEST_CASE("particles.attach.animChainOffsetThreeBones") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     a = sk->addBone("a", -1);
    const int                     b = sk->addBone("b", a);
    const int                     c = sk->addBone("c", b);
    sk->setBindPosition(b, 1.f, 0.f, 0.f);
    sk->setBindPosition(c, 1.f, 0.f, 0.f);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(a, 10.f, 0.f, 0.f);
    pose->computeWorld(sk.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(4);
    e->setAttachOffset(0.5f, 0.f, 0.f);
    e->setAttachScale(1.f);
    e->attachToBone(pose.get(), c);
    // world c = 10+1+1=12, + local offset 0.5 → 12.5
    CHECK(std::fabs(e->getX() - 12.5f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 0.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.spineBobAnimContinuousRate") {
    auto                              *animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    std::unique_ptr<SpineSkeleton>     spine(animMod->newSpineSkeleton(data.get()));
    std::unique_ptr<SpineAnim>         anim(animMod->newSpineAnim(spine.get()));
    REQUIRE(anim->play("bob"));

    const int        chest = data->findBone("chest");
    auto            *mod   = Particles::create();
    ParticleEmitter *e     = mod->newEmitter(128);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(100.f);
    e->attachToSpineBone(spine.get(), chest);
    e->start();

    float minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < 10; ++i) {
        anim->setTime(float(i) * 0.1f);
        anim->apply();
        ParticleSimSystem::update(0.05f);
        minY = std::min(minY, e->getY());
        maxY = std::max(maxY, e->getY());
    }
    CHECK_GE(e->getCount(), 20);
    CHECK_GT(maxY - minY, 5.f);
    e->detach();
}

TEST_CASE("particles.attach.ik3dSolverStepContinuous") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    int                         b1  = sk->createBone(0, 1.f);
    int                         tip = sk->createBone(b1, 1.f);
    sk->initStraightPose(0.f, 0.f, 0.f);

    std::unique_ptr<Solver3D> solver(new Solver3D());
    solver->setMaxIterations(2);
    solver->setForce(0.4f);
    solver->addTarget(tip, 1.f, 0.5f, 0.f, 1.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    e->setAttachPlane("xy");
    e->attachToSkeleton3D(sk.get(), tip);

    float prev  = e->getX();
    int   moved = 0;
    for (int i = 0; i < 10; ++i) {
        solver->step(sk.get(), 1.f);
        e->syncAttach();
        if (std::fabs(e->getX() - prev) > 1e-3f) ++moved;
        prev = e->getX();
        CHECK(std::fabs(e->getX() - sk->getX(tip)) < 1e-2f);
    }
    CHECK_GE(moved, 1);
    e->detach();
}

TEST_CASE("particles.skin.filterByIndexAndClearFilter") {
    if (!ensureSkinnedAssets()) return;

    auto      model     = eve::test::pinResource(loadCesiumMan("ev_ut_particles_skin_filter_idx"));
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    std::unique_ptr<AnimSkin>     skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    skeleton->applyBindPose(pose.get());
    pose->computeWorld(skeleton.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(64);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->setSkinScale(50.f);
    e->setSkinSource(skin.get(), pose.get());

    REQUIRE(skin->getBoneCount() > 0);
    const int skeletonBone = skin->getSkeletonBone(0);
    e->setSkinBoneFilter(skeletonBone, 0.05f);
    e->emitFromSkin(16);
    CHECK_GE(e->getCount(), 1);

    e->reset();
    e->setSkinBoneFilter(-1, 0.f);  // clear filter → all verts
    e->emitFromSkin(32);
    CHECK_EQ(e->getCount(), 32);
    e->clearSkinSource();
    e->detach();
}

TEST_CASE("particles.skin.emitFromSkinRespectsBuffer") {
    if (!ensureSkinnedAssets()) return;

    auto      model     = eve::test::pinResource(loadCesiumMan("ev_ut_particles_skin_buffer"));
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    std::unique_ptr<AnimSkin>     skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    skeleton->applyBindPose(pose.get());
    pose->computeWorld(skeleton.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(10);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->setSkinScale(1.f);
    e->setSkinSource(skin.get(), pose.get());
    e->emitFromSkin(100);
    CHECK_EQ(e->getCount(), 10);
    CHECK_EQ(e->getBufferSize(), 10);
    e->clearSkinSource();
    e->detach();
}

TEST_CASE("particles.attach.cesiumManBoneByNameIfPresent") {
    if (!ensureSkinnedAssets()) return;

    auto                          model = eve::test::pinResource(loadCesiumMan("ev_ut_particles_attach_byname"));
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(skeleton.get() != nullptr);
    REQUIRE(skeleton->getBoneCount() > 1);

    // Collect any bone name and attach by name.
    const std::string         name = skeleton->getBoneName(1);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    skeleton->applyBindPose(pose.get());
    pose->computeWorld(skeleton.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    e->setAttachScale(10.f);
    e->attachToBoneByName(pose.get(), skeleton.get(), name);
    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachBone(), 1);
    CHECK_EQ(e->getAttachKind(), std::string("anim"));

    e->attachToBoneByName(pose.get(), skeleton.get(), "__no_such_bone__");
    CHECK(!e->isAttached());
}

TEST_CASE("particles.attach.smokeAndFirePresetsOnIk3d") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    sk->initStraightPose(0.f, 1.f, 0.f);

    auto *mod = Particles::create();
    for (const char *preset : {"smoke", "fire"}) {
        ParticleEmitter *e = mod->newEmitter(64);
        e->setAttachPlane("xy");
        e->setAttachScale(1.f);
        e->attachToSkeleton3D(sk.get(), 0);
        e->applyPreset(preset);
        CHECK(e->isAttached());
        CHECK(e->getEmissionRate() > 0.f);
        e->start();
        ParticleSimSystem::update(0.05f);
        CHECK_GE(e->getCount(), 1);
        e->detach();
    }
}
}  // namespace test_particles_attach_extra
#undef M_PI
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_attach_more)
#include "PrivateTestScope.h"
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include "PathBesideSource.h"
#include "ResourceTestSupport.h"
#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/Animation.h"
#include "animation/SpineAnim.h"
#include "animation/SpineSkeleton.h"
#include "animation/SpineSkeletonData.h"
#include "filesystem/Filesystem.h"
#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/Solver2D.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// particles_attach_more.cpp
namespace test_particles_attach_more {
using namespace eve::particles;
using namespace eve::animation;
using namespace eve::ik;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {


EVE_DEFINE_PATH_BESIDE_SOURCE()

bool fileExists(const std::string &path) { return std::filesystem::is_regular_file(path); }

std::string cesiumManDir() { return pathBesideThisSource("assets/skinned/cesium_man/glTF"); }

bool ensureSkinnedAssets() {
    const std::string gltf = cesiumManDir() + "/CesiumMan.gltf";
    if (fileExists(gltf)) return true;
    std::printf("particles.attach_more: missing %s\n", gltf.c_str());
    return false;
}

eve::model3d::ModelData *loadCesiumMan(const char *fsIdentity) {
    const std::string dir = cesiumManDir();
    auto             *fs  = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity(fsIdentity, true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));
    auto *mod = eve::model3d::Model3D::create();
    return mod->newModelDataFromFile("CesiumMan.gltf");
}

int findFirstSkinnedMesh(const eve::model3d::ModelData *model) {
    for (int i = 0; i < model->getMeshCount(); ++i) {
        if (model->hasBones(i)) return i;
    }
    return -1;
}

const char *kSpineJson = R"JSON({
  "skeleton": { "spine": "4.1.00" },
  "bones": [
    { "name": "root" },
    { "name": "arm", "parent": "root", "x": 20 },
    { "name": "hand", "parent": "arm", "x": 20 }
  ],
  "slots": [ { "name": "arm", "bone": "arm", "attachment": "arm" } ],
  "skins": [ { "name": "default", "attachments": {
    "arm": { "arm": { "width": 10, "height": 10 } }
  }} ],
  "animations": {
    "spin": {
      "bones": {
        "arm": {
          "rotate": [
            { "time": 0, "value": 0 },
            { "time": 0.5, "value": 90 }
          ]
        }
      }
    }
  }
})JSON";

}  // namespace

TEST_CASE("particles.attach.emitterLifeExpiresWhileAttached") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(10.f, 20.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(64);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(200.f);
    e->setEmitterLifetime(0.05f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->start();
    CHECK(e->isActive());

    ParticleSimSystem::update(0.06f);
    CHECK(!e->isActive());
    const int n = e->getCount();
    CHECK_GE(n, 1);

    // Further updates while attached must not spawn more after life ends.
    sk->setPosition(0, 99.f, 99.f);
    sk->forwardKinematics();
    ParticleSimSystem::update(0.1f);
    CHECK_EQ(e->getCount(), n);
    // Attach sync still updates origin even when inactive.
    CHECK(std::fabs(e->getX() - sk->getX(0)) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.bufferCapUnderRateOnBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    e->setParticleLifetime(10.f, 10.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(1000.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->start();
    ParticleSimSystem::update(1.f);
    CHECK_EQ(e->getCount(), 8);
    CHECK_EQ(e->getBufferSize(), 8);
    e->detach();
}

TEST_CASE("particles.attach.rebindDifferentBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int                         mid = sk->createBone(0, 25.f);
    int                         tip = sk->createBone(mid, 25.f);
    sk->initStraightPose(0.f, 0.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(4);
    e->attachToSkeleton2D(sk.get(), mid);
    CHECK_EQ(e->getAttachBone(), mid);
    CHECK(std::fabs(e->getX() - sk->getX(mid)) < 1e-2f);

    e->attachToSkeleton2D(sk.get(), tip);
    CHECK_EQ(e->getAttachBone(), tip);
    CHECK_EQ(e->getAttachKind(), std::string("ik2d"));
    CHECK(std::fabs(e->getX() - sk->getX(tip)) < 1e-2f);
    CHECK(std::fabs(e->getX() - sk->getX(mid)) > 1.f);
    e->detach();
}

TEST_CASE("particles.attach.zeroAndNegativeScale") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 2.f, 3.f, 0.f);
    pose->computeWorld(sk.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(4);
    e->attachToBone(pose.get(), root);
    e->setAttachScale(0.f);
    CHECK(std::fabs(e->getX()) < 1e-4f);
    CHECK(std::fabs(e->getY()) < 1e-4f);

    e->setAttachScale(-2.f);
    CHECK(std::fabs(e->getX() - (-4.f)) < 1e-3f);
    CHECK(std::fabs(e->getY() - (-6.f)) < 1e-3f);
    e->detach();
}

TEST_CASE("particles.attach.stopStartPreservesAttach") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    sk->initStraightPose(1.f, 2.f, 3.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(80.f);
    e->setAttachPlane("xy");
    e->setAttachScale(1.f);
    e->attachToSkeleton3D(sk.get(), 0);
    e->start();
    ParticleSimSystem::update(0.05f);
    CHECK_GE(e->getCount(), 3);

    e->stop();
    CHECK(e->isStopped());
    CHECK(e->isAttached());
    const int n = e->getCount();
    ParticleSimSystem::update(0.05f);
    CHECK_EQ(e->getCount(), n);

    e->start();
    ParticleSimSystem::update(0.05f);
    CHECK_GT(e->getCount(), n);
    CHECK_EQ(e->getAttachKind(), std::string("ik3d"));
    e->detach();
}

TEST_CASE("particles.attach.emissionAreaAroundBone") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(100.f, 100.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(64);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionArea("rect", 10.f, 5.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(40);

    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < e->getCount(); ++i) {
        const auto &p = e->sim()->particles[size_t(i)];
        minX          = std::min(minX, p.x);
        maxX          = std::max(maxX, p.x);
        minY          = std::min(minY, p.y);
        maxY          = std::max(maxY, p.y);
        CHECK(p.x >= 90.f - 1e-2f);
        CHECK(p.x <= 110.f + 1e-2f);
        CHECK(p.y >= 95.f - 1e-2f);
        CHECK(p.y <= 105.f + 1e-2f);
    }
    CHECK_GT(maxX - minX, 5.f);
    CHECK_GT(maxY - minY, 2.f);
    e->detach();
}

TEST_CASE("particles.attach.followRotationAffectsSpawnVelocity") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    sk->applyBindPose(pose.get());
    // 90° Z: +X → +Y
    const float half = 0.70710678f;
    pose->setLocalRotation(root, 0.f, 0.f, half, half);
    pose->computeWorld(sk.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(10.f, 10.f);
    e->setSpread(0.f);
    e->setFollowBoneRotation(true);
    e->attachToBone(pose.get(), root);
    e->emit(1);
    CHECK_EQ(e->getCount(), 1);
    const auto &p = e->sim()->particles[0];
    // Direction ≈ π/2 → velocity mostly +Y
    CHECK(std::fabs(p.vx) < 1.f);
    CHECK(p.vy > 8.f);
    e->detach();
}

TEST_CASE("particles.attach.radialForceUsesMovingOrigin") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setRadialAcceleration(20.f, 20.f);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(1);
    // Place particle away from origin manually.
    e->sim()->particles[0].x = 10.f;
    e->sim()->particles[0].y = 0.f;

    ParticleSimSystem::update(0.1f);
    // Radial push away from Config origin (0,0) → +vx
    CHECK(e->sim()->particles[0].vx > 0.f);

    // Move bone far to the right; radial origin follows → pull/push relative to new origin.
    sk->setPosition(0, 100.f, 0.f);
    sk->forwardKinematics();
    e->sim()->particles[0].vx = 0.f;
    e->sim()->particles[0].vy = 0.f;
    e->sim()->particles[0].x  = 10.f;  // left of new origin 100
    e->sim()->particles[0].y  = 0.f;
    ParticleSimSystem::update(0.1f);
    // Away from origin at 100: particle at 10 → direction -X → negative vx
    CHECK(e->sim()->particles[0].vx < 0.f);
    e->detach();
}

TEST_CASE("particles.attach.getAttachBoneAfterDetach") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(2);
    e->attachToSkeleton2D(sk.get(), 0);
    CHECK_EQ(e->getAttachBone(), 0);
    e->detach();
    CHECK_EQ(e->getAttachBone(), -1);
    CHECK_EQ(e->getAttachKind(), std::string("none"));
}

TEST_CASE("particles.attach.stableWhenPoseUnchanged") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 5.f, 6.f, 7.f);
    pose->computeWorld(sk.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(4);
    e->setAttachPlane("xz");
    e->setAttachScale(1.f);
    e->attachToBone(pose.get(), root);
    const float x0 = e->getX();
    const float y0 = e->getY();
    for (int i = 0; i < 10; ++i) ParticleSimSystem::update(0.016f);
    CHECK(std::fabs(e->getX() - x0) < 1e-5f);
    CHECK(std::fabs(e->getY() - y0) < 1e-5f);
    // xz of (5,6,7) → (5,7)
    CHECK(std::fabs(x0 - 5.f) < 1e-3f);
    CHECK(std::fabs(y0 - 7.f) < 1e-3f);
    e->detach();
}

TEST_CASE("particles.attach.spineRotatedParentOffset") {
    auto                              *animMod = Animation::create();
    std::unique_ptr<SpineSkeletonData> data(animMod->newSpineSkeletonDataFromJson(kSpineJson));
    std::unique_ptr<SpineSkeleton>     spine(animMod->newSpineSkeleton(data.get()));
    std::unique_ptr<SpineAnim>         anim(animMod->newSpineAnim(spine.get()));
    REQUIRE(anim->play("spin"));
    anim->setTime(0.5f);
    anim->apply();

    const int        hand = data->findBone("hand");
    auto            *mod  = Particles::create();
    ParticleEmitter *e    = mod->newEmitter(4);
    e->setAttachOffset(0.f, 5.f, 0.f);
    e->attachToSpineBone(spine.get(), hand);

    float a, b, c, d;
    spine->getBoneWorldMatrix(hand, a, b, c, d);
    const float ex = spine->getBoneWorldX(hand) + b * 5.f;
    const float ey = spine->getBoneWorldY(hand) + d * 5.f;
    CHECK(std::fabs(e->getX() - ex) < 1e-2f);
    CHECK(std::fabs(e->getY() - ey) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.ik2dSolverStepTracksTip") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int                         b1  = sk->createBone(0, 40.f);
    int                         tip = sk->createBone(b1, 40.f);
    sk->initStraightPose(0.f, 0.f);

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(4);
    solver->setForce(0.5f);
    solver->addTarget(tip, 50.f, 30.f, 1.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    e->attachToSkeleton2D(sk.get(), tip);
    e->setEmissionRate(0.f);
    e->start();

    float prev  = e->getX() + e->getY();
    int   moved = 0;
    for (int i = 0; i < 8; ++i) {
        solver->step(sk.get(), 1.f);
        ParticleSimSystem::update(0.016f);
        const float cur = e->getX() + e->getY();
        if (std::fabs(cur - prev) > 0.1f) ++moved;
        prev = cur;
        CHECK(std::fabs(e->getX() - sk->getX(tip)) < 1e-2f);
        CHECK(std::fabs(e->getY() - sk->getY(tip)) < 1e-2f);
    }
    CHECK_GE(moved, 1);
    e->detach();
}

TEST_CASE("particles.attach.ik3dOffsetAlongBoneProjected") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    int                         tip = sk->createBone(0, 1.f);
    sk->initStraightPose(0.f, 0.f, 0.f);
    // tip at (1,0,0), orientation +X

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(4);
    e->setAttachPlane("xy");
    e->setAttachScale(1.f);
    e->setAttachOffset(2.f, 0.f, 0.f);  // +2 along bone → world (3,0,0)
    e->attachToSkeleton3D(sk.get(), tip);
    CHECK(std::fabs(e->getX() - 3.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 0.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.animByNameAfterDetach") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    sk->addBone("Hips", -1);
    const int head = sk->addBone("Head", 0);
    sk->setBindPosition(head, 0.f, 2.f, 0.f);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->computeWorld(sk.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(4);
    e->attachToBoneByName(pose.get(), sk.get(), "Head");
    CHECK(e->isAttached());
    e->detach();
    e->attachToBoneByName(pose.get(), sk.get(), "Head");
    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachBone(), head);
    CHECK(std::fabs(e->getY() - 2.f) < 1e-3f);
    e->detach();
}

TEST_CASE("particles.attach.presetStillWorksWhenAttached") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(40.f, 50.f);

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(32);
    e->attachToSkeleton2D(sk.get(), 0);
    e->applyPreset("spark");
    // Preset must not clear attach.
    CHECK(e->isAttached());
    CHECK(std::fabs(e->getX() - 40.f) < 1e-2f);
    e->start();
    ParticleSimSystem::update(0.05f);
    CHECK_GE(e->getCount(), 1);
    e->detach();
}

TEST_CASE("particles.attach.emitZeroAndNegativeNoOp") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(0.f, 0.f);
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    e->attachToSkeleton2D(sk.get(), 0);
    e->emit(0);
    e->emit(-5);
    CHECK_EQ(e->getCount(), 0);
    e->detach();
}

TEST_CASE("particles.skin.planeXzYzCesiumMan") {
    if (!ensureSkinnedAssets()) return;

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_particles_skin_planes"));
    REQUIRE(model.get() != nullptr);
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    std::unique_ptr<AnimSkin>     skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    skeleton->applyBindPose(pose.get());
    pose->computeWorld(skeleton.get());
    REQUIRE(skin->updateSkinnedPositions(pose.get()));

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(64);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->setSkinScale(10.f);
    e->setSkinSource(skin.get(), pose.get());

    e->setSkinPlane("xz");
    e->emitFromSkin(32);
    CHECK_EQ(e->getCount(), 32);
    float spreadXZ = 0.f;
    {
        float minX = e->sim()->particles[0].x, maxX = minX;
        float minY = e->sim()->particles[0].y, maxY = minY;
        for (int i = 0; i < e->getCount(); ++i) {
            const auto &p = e->sim()->particles[size_t(i)];
            minX          = std::min(minX, p.x);
            maxX          = std::max(maxX, p.x);
            minY          = std::min(minY, p.y);
            maxY          = std::max(maxY, p.y);
        }
        spreadXZ = (maxX - minX) + (maxY - minY);
    }
    CHECK_GT(spreadXZ, 0.5f);

    e->reset();
    e->setSkinPlane("yz");
    e->emitFromSkin(32);
    CHECK_EQ(e->getCount(), 32);
    float spreadYZ = 0.f;
    {
        float minX = e->sim()->particles[0].x, maxX = minX;
        float minY = e->sim()->particles[0].y, maxY = minY;
        for (int i = 0; i < e->getCount(); ++i) {
            const auto &p = e->sim()->particles[size_t(i)];
            minX          = std::min(minX, p.x);
            maxX          = std::max(maxX, p.x);
            minY          = std::min(minY, p.y);
            maxY          = std::max(maxY, p.y);
        }
        spreadYZ = (maxX - minX) + (maxY - minY);
    }
    CHECK_GT(spreadYZ, 0.5f);
    e->clearSkinSource();
    e->detach();
}

TEST_CASE("particles.skin.clearRestoresBoneOriginSpawn") {
    if (!ensureSkinnedAssets()) return;

    auto      model     = eve::test::pinResource(loadCesiumMan("ev_ut_particles_skin_clear"));
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    std::unique_ptr<AnimSkin>     skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    skeleton->applyBindPose(pose.get());
    pose->setLocalPosition(0, 1.f, 2.f, 0.f);
    pose->computeWorld(skeleton.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(32);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->setAttachScale(1.f);
    e->attachToBone(pose.get(), 0);
    e->setSkinScale(1.f);
    e->setSkinSource(skin.get(), pose.get());
    CHECK(e->hasSkinSource());
    CHECK(e->isAttached());

    e->clearSkinSource();
    CHECK(!e->hasSkinSource());
    CHECK(e->isAttached());
    e->emit(4);
    CHECK_EQ(e->getCount(), 4);
    for (int i = 0; i < 4; ++i) {
        CHECK(std::fabs(e->sim()->particles[size_t(i)].x - e->getX()) < 1e-2f);
        CHECK(std::fabs(e->sim()->particles[size_t(i)].y - e->getY()) < 1e-2f);
    }
    e->detach();
}

TEST_CASE("particles.attach.manualSyncAttachWithoutSim") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    sk->initStraightPose(1.f, 2.f);
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(2);
    e->attachToSkeleton2D(sk.get(), 0);
    sk->setPosition(0, 8.f, 9.f);
    sk->forwardKinematics();
    // Without ParticleSimSystem — only manual sync.
    e->syncAttach();
    CHECK(std::fabs(e->getX() - 8.f) < 1e-2f);
    CHECK(std::fabs(e->getY() - 9.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.followRotationToggleResyncs") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    sk->applyBindPose(pose.get());
    const float half = 0.70710678f;
    pose->setLocalRotation(root, 0.f, 0.f, half, half);
    pose->computeWorld(sk.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(2);
    e->setDirection(0.1f);
    e->setFollowBoneRotation(false);
    e->attachToBone(pose.get(), root);
    CHECK(std::fabs(e->getDirection() - 0.1f) < 1e-4f);

    e->setFollowBoneRotation(true);  // triggers syncAttach
    CHECK(std::fabs(e->getDirection() - float(M_PI * 0.5)) < 0.05f);

    e->setFollowBoneRotation(false);
    e->setDirection(0.3f);
    e->syncAttach();
    CHECK(std::fabs(e->getDirection() - 0.3f) < 1e-4f);
    e->detach();
}
}  // namespace test_particles_attach_more
#undef M_PI
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_attach_skin)
#include "PrivateTestScope.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include "PathBesideSource.h"
#include "ResourceTestSupport.h"
#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// particles_attach_skin.cpp
namespace test_particles_attach_skin {
using namespace eve::particles;
using namespace eve::animation;

namespace {


EVE_DEFINE_PATH_BESIDE_SOURCE()

bool fileExists(const std::string &path) { return std::filesystem::is_regular_file(path); }

std::string cesiumManDir() { return pathBesideThisSource("assets/skinned/cesium_man/glTF"); }

std::string cesiumManGltfPath() { return cesiumManDir() + "/CesiumMan.gltf"; }

bool ensureSkinnedAssets() {
    const std::string gltf = cesiumManGltfPath();
    if (fileExists(gltf)) return true;
    std::printf("particles.attach_skin: missing %s — run scripts/download_skinned_character.sh\n", gltf.c_str());
    return false;
}

eve::model3d::ModelData *loadCesiumMan(const char *fsIdentity) {
    const std::string dir = cesiumManDir();
    REQUIRE(fileExists(dir + "/CesiumMan.gltf"));
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    REQUIRE(fs->setIdentity(fsIdentity, true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));
    auto *mod = eve::model3d::Model3D::create();
    return mod->newModelDataFromFile("CesiumMan.gltf");
}

int findFirstSkinnedMesh(const eve::model3d::ModelData *model) {
    for (int i = 0; i < model->getMeshCount(); ++i) {
        if (model->hasBones(i)) return i;
    }
    return -1;
}

}  // namespace

TEST_CASE("particles.attach.boneFollowsPose") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("root", -1);
    const int                     hand = sk->addBone("hand", root);
    sk->setBindPosition(hand, 0.f, 1.f, 0.f);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 10.f, 20.f, 5.f);
    pose->computeWorld(sk.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(32);
    e->setAttachScale(1.f);
    e->setAttachPlane("xy");
    e->attachToBone(pose.get(), hand);
    e->setEmissionRate(0.f);
    e->start();

    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachBone(), hand);
    // hand world = root(10,20,5) + local(0,1,0) → (10, 21, 5); xy → (10, 21)
    CHECK(std::fabs(e->getX() - 10.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 21.f) < 1e-3f);

    // Move bone and sync via ParticleSimSystem.
    pose->setLocalPosition(root, 30.f, 40.f, 0.f);
    pose->computeWorld(sk.get());
    ParticleSimSystem::update(0.016f);
    CHECK(std::fabs(e->getX() - 30.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 41.f) < 1e-3f);

    e->setAttachPlane("xz");
    e->syncAttach();
    // world (30, 41, 0) on xz → (30, 0)
    CHECK(std::fabs(e->getX() - 30.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 0.f) < 1e-3f);

    // yz plane maps (x,y,z) → (y,z).
    e->setAttachPlane("yz");
    e->setAttachScale(1.f);
    e->syncAttach();
    CHECK(std::fabs(e->getX() - 41.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 0.f) < 1e-3f);

    e->setAttachScale(2.f);
    e->setAttachPlane("xy");
    e->syncAttach();
    CHECK(std::fabs(e->getX() - 60.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 82.f) < 1e-3f);

    CHECK_EQ(e->getAttachKind(), std::string("anim"));
    e->detach();
    CHECK(!e->isAttached());
    CHECK_EQ(e->getAttachKind(), std::string("none"));
}

TEST_CASE("particles.attach.byNameAndOffset") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("Hips", -1);
    const int                     tip  = sk->addBone("Head", root);
    sk->setBindPosition(tip, 0.f, 2.f, 0.f);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->computeWorld(sk.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    e->attachToBoneByName(pose.get(), sk.get(), "Head");
    CHECK(e->isAttached());
    CHECK_EQ(e->getAttachBone(), tip);
    CHECK(std::fabs(e->getY() - 2.f) < 1e-3f);

    e->setAttachOffset(1.f, 0.f, 0.f);
    e->syncAttach();
    CHECK(std::fabs(e->getX() - 1.f) < 1e-3f);
    CHECK(std::fabs(e->getY() - 2.f) < 1e-3f);

    e->attachToBoneByName(pose.get(), sk.get(), "MissingBone");
    CHECK(!e->isAttached());
}

TEST_CASE("particles.attach.followRotation") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("root", -1);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    // 90° around Z: +X maps to +Y in XY plane → direction ≈ π/2
    const float half = 0.70710678f;
    pose->setLocalRotation(root, 0.f, 0.f, half, half);
    pose->computeWorld(sk.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(4);
    e->setDirection(0.f);
    e->setFollowBoneRotation(true);
    e->attachToBone(pose.get(), root);
    CHECK(std::fabs(e->getDirection() - (3.14159265f * 0.5f)) < 0.05f);
    e->detach();

    // Off keeps the configured direction unchanged under the same rotation.
    ParticleEmitter *e2 = mod->newEmitter(4);
    e2->setDirection(0.25f);
    e2->setFollowBoneRotation(false);
    e2->attachToBone(pose.get(), root);
    CHECK(std::fabs(e2->getDirection() - 0.25f) < 1e-4f);
    e2->detach();
}

TEST_CASE("particles.attach.emitsAtBone") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("root", -1);
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 50.f, 60.f, 0.f);
    pose->computeWorld(sk.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    e->setParticleLifetime(2.f, 2.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(100.f);
    e->attachToBone(pose.get(), root);
    e->start();
    ParticleSimSystem::update(0.05f);
    CHECK_GE(e->getCount(), 4);
    auto sim = e->sim();
    for (int i = 0; i < sim->alive; ++i) {
        CHECK(std::fabs(sim->particles[size_t(i)].x - 50.f) < 1e-2f);
        CHECK(std::fabs(sim->particles[size_t(i)].y - 60.f) < 1e-2f);
    }
    e->detach();
}

TEST_CASE("particles.skin.surfaceEmitCesiumMan") {
    if (!ensureSkinnedAssets()) return;

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_particles_skin_surface"));
    REQUIRE(model.get() != nullptr);
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);

    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(skeleton.get() != nullptr);
    std::unique_ptr<AnimClip> clip(AnimImporter::loadClipFromModel(model.get(), skeleton.get(), 0));
    REQUIRE(clip.get() != nullptr);
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    REQUIRE(skin.get() != nullptr);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    clip->sample(0.f, pose.get(), skeleton.get());
    pose->computeWorld(skeleton.get());
    CHECK(!skin->hasSkinnedPositions());
    REQUIRE(skin->updateSkinnedPositions(pose.get()));
    CHECK(skin->hasSkinnedPositions());
    CHECK(std::isfinite(skin->getSkinnedPositionX(0)));
    CHECK(std::isfinite(skin->getSkinnedPositionY(0)));
    CHECK(std::isfinite(skin->getSkinnedPositionZ(0)));

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(256);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionArea("none", 0.f, 0.f);
    e->setSkinScale(100.f);  // meters → pixels-ish
    e->setSkinPlane("xy");
    e->setSkinSource(skin.get(), pose.get());
    CHECK(e->hasSkinSource());

    e->emitFromSkin(64);
    CHECK_EQ(e->getCount(), 64);

    // Spawn positions should not all collapse to a single point.
    auto  sim  = e->sim();
    float minX = sim->particles[0].x, maxX = minX;
    float minY = sim->particles[0].y, maxY = minY;
    for (int i = 0; i < sim->alive; ++i) {
        const auto &p = sim->particles[size_t(i)];
        minX          = std::min(minX, p.x);
        maxX          = std::max(maxX, p.x);
        minY          = std::min(minY, p.y);
        maxY          = std::max(maxY, p.y);
    }
    CHECK_GT(maxX - minX + maxY - minY, 1.f);

    // Continuous emission via rate also samples the skin.
    e->reset();
    e->setEmissionRate(200.f);
    e->start();
    ParticleSimSystem::update(0.1f);
    CHECK_GE(e->getCount(), 15);

    // Bone filter should narrow candidate set (or keep emitting if bone unused).
    const std::string boneName = skin->getSkinBoneName(0);
    e->setSkinBoneFilterByName(skeleton.get(), boneName, 0.1f);
    e->reset();
    e->emitFromSkin(32);
    CHECK_GE(e->getCount(), 1);

    e->clearSkinSource();
    CHECK(!e->hasSkinSource());
}

TEST_CASE("particles.attach.animPoseDynamicEmitAcrossFrames") {
    // Emit while the bone is moving — particles should form a trail of spawn positions.
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int                     root = sk->addBone("root", -1);
    const int                     tip  = sk->addBone("tip", root);
    sk->setBindPosition(tip, 1.f, 0.f, 0.f);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    // The attach sync reads the pose's world transforms — compute them first
    // (matches the documented frame order: computeWorld → attach/emit).
    pose->computeWorld(sk.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(64);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(0.f);
    e->setAttachScale(10.f);
    e->setAttachPlane("xy");
    e->attachToBone(pose.get(), tip);
    CHECK_EQ(e->getAttachKind(), std::string("anim"));

    for (int frame = 0; frame < 5; ++frame) {
        pose->setLocalPosition(root, float(frame) * 2.f, 0.f, 0.f);
        pose->computeWorld(sk.get());
        e->emit(1);
        ParticleSimSystem::update(0.016f);
    }
    CHECK_EQ(e->getCount(), 5);
    auto sim = e->sim();
    // tip world x = root.x + 1 → scaled ×10; frames 0..4 → x = 10,30,50,70,90
    CHECK(std::fabs(sim->particles[0].x - 10.f) < 1e-2f);
    CHECK(std::fabs(sim->particles[4].x - 90.f) < 1e-2f);
    e->detach();
}

TEST_CASE("particles.attach.clipSampleMovesEmitter") {
    if (!ensureSkinnedAssets()) return;

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_particles_attach_clip"));
    REQUIRE(model.get() != nullptr);
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(skeleton.get() != nullptr);
    std::unique_ptr<AnimClip> clip(AnimImporter::loadClipFromModel(model.get(), skeleton.get(), 0));
    REQUIRE(clip.get() != nullptr);
    REQUIRE(clip->getDuration() > 0.1f);

    // Use an animated limb; the last imported node can be the static mesh node.
    int bone = skeleton->findBone("Left_arm");
    if (bone < 0) bone = skeleton->findBone("LeftArm");
    if (bone < 0) bone = skeleton->findBone("Skeleton_arm_joint_L__4_");
    REQUIRE(bone >= 0);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    auto                     *mod = Particles::create();
    ParticleEmitter          *e   = mod->newEmitter(64);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setAttachScale(50.f);
    e->setAttachPlane("xy");
    e->attachToBone(pose.get(), bone);
    CHECK_EQ(e->getAttachKind(), std::string("anim"));

    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < 8; ++i) {
        const float t = clip->getDuration() * (float(i) / 7.f);
        clip->sample(t, pose.get(), skeleton.get());
        pose->computeWorld(skeleton.get());
        e->emit(1);
        ParticleSimSystem::update(0.016f);
        const auto &p = e->sim()->particles[size_t(e->getCount() - 1)];
        minX          = std::min(minX, p.x);
        maxX          = std::max(maxX, p.x);
        minY          = std::min(minY, p.y);
        maxY          = std::max(maxY, p.y);
    }
    CHECK_EQ(e->getCount(), 8);
    // Animated bone should move enough that spawn positions spread out.
    CHECK_GT(maxX - minX + maxY - minY, 1.f);
    e->detach();
}

TEST_CASE("particles.skin.emptyBoneFilterFallsBack") {
    if (!ensureSkinnedAssets()) return;

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_particles_skin_empty_filter"));
    REQUIRE(model.get() != nullptr);
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    std::unique_ptr<AnimSkin>     skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    std::unique_ptr<AnimPose>     pose(new AnimPose());
    skeleton->applyBindPose(pose.get());
    pose->computeWorld(skeleton.get());

    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(32);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->setPosition(12.f, 34.f);
    e->setSkinScale(1.f);
    e->setSkinSource(skin.get(), pose.get());
    // Impossible weight threshold → no candidates → spawn falls back to Config.x/y.
    e->setSkinBoneFilter(0, 2.f);
    e->emitFromSkin(5);
    // emitFromSkin breaks when sample fails, so count may be 0.
    CHECK_LE(e->getCount(), 5);

    e->reset();
    e->setEmissionRate(100.f);
    e->start();
    ParticleSimSystem::update(0.05f);
    CHECK_GE(e->getCount(), 4);
    // Fallback spawn at emitter position.
    CHECK(std::fabs(e->sim()->particles[0].x - 12.f) < 1e-2f);
    CHECK(std::fabs(e->sim()->particles[0].y - 34.f) < 1e-2f);
    e->clearSkinSource();
    e->detach();
}
}  // namespace test_particles_attach_skin
#endif
