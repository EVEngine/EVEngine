#include "procgen/BuildLayerStack.h"
#include "procgen/Semantic.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

TEST_CASE("procgen.buildLayers.executesOrderedTileAndObjectDefinitions") {
    Grid2D grid;
    grid.resize(2, 1);
    grid.setCell(0, 0, int(Semantic::Floor));
    grid.setCell(1, 0, int(Semantic::Floor));

    PointSet points;
    points.add(2.f, 0.f, 3.f);
    ObjectBuildLayer objects;
    REQUIRE(objects.addAsset("props/crate", 1.f).ok());
    objects.setSeed(15);

    BuildLayerStack stack;
    REQUIRE(stack.addTileLayer("floor", true, 2.f, 0.5f, "floor").ok());
    REQUIRE(stack.addObjectLayer("props", true, objects).ok());
    REQUIRE(stack.addTileLayer("disabled", false, 1.f, 1.f, "disabled").ok());
    CHECK_EQ(stack.getLayerCount(), 3);
    CHECK_EQ(stack.getLayerId(1), "props");
    CHECK_EQ(stack.getLayerType(0), "tiles");
    CHECK(!stack.isLayerEnabled(2));

    auto result = stack.execute(grid, points);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().getCount(), 2);
    CHECK_EQ(result.value().getId(0), "floor");
    CHECK_EQ(result.value().getType(0), "mesh");
    CHECK_EQ(result.value().getType(1), "points");
    auto mesh = result.value().getMesh(0);
    REQUIRE(mesh.ok());
    CHECK_EQ(mesh.value().getIndexCount(), 48);
    auto built = result.value().getPoints(1);
    REQUIRE(built.ok());
    CHECK_EQ(built.value().getStringAttribute(0, "asset", ""), "props/crate");
    CHECK(!result.value().getPoints(0).ok());
    CHECK(!result.value().getMesh(1).ok());
}

TEST_CASE("procgen.buildLayers.roundTripsAndRejectsDamageAtomically") {
    ObjectBuildLayer objects;
    REQUIRE(objects.addAsset("props/a", 2.f).ok());
    REQUIRE(objects.addAsset("props/b", 1.f).ok());
    REQUIRE(objects.setPositionRadius(0.4f).ok());
    REQUIRE(objects.addChild("props/child", 2, 1.f, 0.5f, 1.5f, -20.f, 20.f).ok());
    const auto       objectDefinition = objects.serializeDefinition();
    ObjectBuildLayer restoredObject;
    REQUIRE(restoredObject.deserializeDefinition(objectDefinition).ok());
    CHECK_EQ(restoredObject.serializeDefinition(), objectDefinition);
    CHECK(!restoredObject.deserializeDefinition("EVPCG_OBJECT_LAYER 1\nUNKNOWN 1\nEND\n").ok());
    CHECK_EQ(restoredObject.serializeDefinition(), objectDefinition);

    BuildLayerStack stack;
    REQUIRE(stack.addObjectLayer("objects", true, objects).ok());
    REQUIRE(stack.addTileLayer("tiles", false, 1.25f, 0.3f, "terrain main").ok());
    const auto      definition = stack.serializeDefinition();
    BuildLayerStack restored;
    REQUIRE(restored.deserializeDefinition(definition).ok());
    CHECK_EQ(restored.serializeDefinition(), definition);
    CHECK(!restored.deserializeDefinition("EVPCG_BUILD_LAYERS 1\nOBJECT \"bad\" 1 xyz\nEND\n").ok());
    CHECK_EQ(restored.serializeDefinition(), definition);
    CHECK(!restored.addTileLayer("objects", true, 1.f, 0.f, "duplicate").ok());
    CHECK(!restored.setEnabled("missing", true).ok());
}
