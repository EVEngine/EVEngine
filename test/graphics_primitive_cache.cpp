#include "graphics/PrimitiveScene.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
