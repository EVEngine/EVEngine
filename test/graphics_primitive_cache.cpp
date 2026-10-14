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
    Graphics* gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);

    Canvas* flatTarget = gfx->newCanvas(64, 64);
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

    Canvas* spatialTarget = gfx->newCanvas(64, 64);
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
    const uint8_t* center = pixel(*spatialImage, 32, 32);
    REQUIRE(center[1] > 140);
    REQUIRE(center[1] > center[0] + 60);
    // An asymmetric fixture catches clip-Y inversions that a centered disk hides.
    std::size_t blueAbove = 0;
    std::size_t blueBelow = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const auto* sample = pixel(*spatialImage, x, y);
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
    Graphics* gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    Canvas* target = gfx->newCanvas(96, 96);
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
            const auto* sample = pixel(*image, x, y);
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
    for (const char* name :
         {"disk",    "fill",        "cap",      "join",    "width", "blend", "cull", "layer", "matrix", "route",
          "removed", "routeUpdate", "cylinder", "capsule", "cone",  "arrow", "obb",  "grid",  "arc",    "batch"})
        CHECK(vm.find(name).toTable().get<bool>("ok"));
    for (const char* name : {"badMode", "badMatrix", "badRoute", "afterRemove", "badBatch"})
        CHECK(!vm.find(name).toTable().get<bool>("ok"));
    CHECK(vm.find("stale").toBool());
}
