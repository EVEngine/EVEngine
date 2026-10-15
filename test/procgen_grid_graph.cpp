#include "procgen/GeneratorRegistry.h"
#include "procgen/GridGraph.h"
#include "procgen/MeshGraph.h"
#include "procgen/Params.h"
#include "procgen/PointGraph.h"
#include "procgen/Semantic.h"
#include "procgen/TilePreset.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

TEST_CASE("procgen.tilePreset.matchesReferenceStandardGridTables") {
    const auto isolated = resolveTilePreset(0);
    CHECK_EQ(isolated.configuration, 16);
    CHECK_EQ(isolated.kind, TilePresetKind::Single);
    CHECK_EQ(isolated.rotationDegrees, 0);
    CHECK(!isolated.mirrorX);

    const auto north = resolveTilePreset(1u << 0u);
    CHECK_EQ(north.configuration, 18);
    CHECK_EQ(north.kind, TilePresetKind::DeadEnd);
    CHECK_EQ(north.rotationDegrees, 90);

    const auto east = resolveTilePreset(1u << 1u);
    CHECK_EQ(east.configuration, 48);
    CHECK_EQ(east.kind, TilePresetKind::DeadEnd);
    CHECK_EQ(east.rotationDegrees, 180);

    const auto asymmetric = resolveTilePreset(139);
    CHECK_EQ(asymmetric.configuration, 59);
    CHECK_EQ(asymmetric.kind, TilePresetKind::EdgeCornerFill);
    CHECK_EQ(asymmetric.rotationDegrees, 90);
    CHECK(asymmetric.mirrorX);

    const auto filled = resolveTilePreset(255);
    CHECK_EQ(filled.configuration, 511);
    CHECK_EQ(filled.kind, TilePresetKind::Fill);

    // The source 4.3.5 table omits configuration 350; preserve that compatibility edge case.
    const auto unclassified = resolveTilePreset(121);
    CHECK_EQ(unclassified.configuration, 350);
    CHECK_EQ(unclassified.kind, TilePresetKind::None);

    for (int mask = 0; mask <= 255; ++mask) {
        const auto variant = resolveTilePreset(std::uint8_t(mask));
        CHECK((variant.configuration & 16u) != 0u);
        const bool validRotation = variant.rotationDegrees == 0 || variant.rotationDegrees == 90 ||
                                   variant.rotationDegrees == 180 || variant.rotationDegrees == 270;
        CHECK(validRotation);
    }
}

TEST_CASE("procgen.gridGraph.composesGridAndPointGraphs") {
    Grid2D source;
    source.resize(5, 5);
    source.setCell(2, 2, int(Semantic::Grass));

    PointGraph points;
    REQUIRE(points.addNode("input", "input"));
    REQUIRE(points.addNode("move", "transform"));
    REQUIRE(points.connect("input", "move"));
    REQUIRE(points.setNodeFloat("move", "y", 3.f));

    GridGraph graph;
    REQUIRE(graph.addNode("source", "grid.input").ok());
    REQUIRE(graph.addNode("grow", "grid.expand").ok());
    REQUIRE(graph.addNode("points", "convert.grid_to_points").ok());
    REQUIRE(graph.addNode("point_pass", "point.subgraph").ok());
    REQUIRE(graph.connect("source", "grow").ok());
    REQUIRE(graph.connect("grow", "points").ok());
    REQUIRE(graph.connect("points", "point_pass").ok());
    REQUIRE(graph.setNodeGrid("source", source).ok());
    REQUIRE(graph.setNodeInt("grow", "radius", 1).ok());
    REQUIRE(graph.setNodeFloat("points", "cellSize", 2.f).ok());
    REQUIRE(graph.setNodePointSubgraph("point_pass", points, "input", "move").ok());

    auto result = graph.execute("point_pass");
    REQUIRE(result.ok());
    const auto* output = std::get_if<PointSet>(&result.value());
    REQUIRE(output != nullptr);
    CHECK_EQ(output->getCount(), 9);
    CHECK_EQ(output->getY(0), 3.f);
    CHECK(output->hasIntAttribute(0, "cell_x"));
    CHECK(output->hasIntAttribute(0, "semantic"));

    auto again = graph.execute("point_pass");
    REQUIRE(again.ok());
    CHECK_EQ(std::get<PointSet>(again.value()).getCount(), 9);
}

TEST_CASE("procgen.gridGraph.rejectsTypeMismatchCyclesAndDimensionMismatch") {
    GridGraph graph;
    REQUIRE(graph.addNode("a", "grid.input").ok());
    REQUIRE(graph.addNode("b", "grid.input").ok());
    REQUIRE(graph.addNode("merge", "grid.union").ok());
    REQUIRE(graph.addNode("points", "convert.grid_to_points").ok());
    REQUIRE(graph.connect("a", "merge", 0).ok());
    REQUIRE(graph.connect("b", "merge", 1).ok());
    REQUIRE(graph.connect("merge", "points").ok());

    auto badType = graph.connect("points", "merge", 0);
    CHECK(!badType.ok());

    Grid2D small;
    small.resize(2, 2);
    Grid2D large;
    large.resize(3, 3);
    REQUIRE(graph.setNodeGrid("a", small).ok());
    REQUIRE(graph.setNodeGrid("b", large).ok());
    auto mismatch = graph.execute("merge");
    CHECK(!mismatch.ok());

    GridGraph cycle;
    REQUIRE(cycle.addNode("left", "grid.invert").ok());
    REQUIRE(cycle.addNode("right", "grid.invert").ok());
    REQUIRE(cycle.connect("left", "right").ok());
    REQUIRE(cycle.connect("right", "left").ok());
    auto cyclic = cycle.validate("left");
    CHECK(!cyclic.ok());
}

TEST_CASE("procgen.gridGraph.coversGeneratorsSelectorsPathAndSerialization") {
    GridGraph graph;
    REQUIRE(graph.addNode("maze", "generate.maze").ok());
    REQUIRE(graph.setNodeInt("maze", "width", 17).ok());
    REQUIRE(graph.setNodeInt("maze", "height", 17).ok());
    REQUIRE(graph.setNodeInt("maze", "seed", 42).ok());
    REQUIRE(graph.addNode("border", "select.border").ok());
    REQUIRE(graph.connect("maze", "border").ok());

    auto first = graph.execute("border");
    REQUIRE(first.ok());
    const auto* selected = std::get_if<Grid2D>(&first.value());
    REQUIRE(selected != nullptr);
    CHECK_EQ(selected->getWidth(), 17);

    GridGraph restored;
    REQUIRE(restored.deserializeDefinition(graph.serializeDefinition()).ok());
    auto second = restored.execute("border");
    REQUIRE(second.ok());
    CHECK_EQ(std::get<Grid2D>(second.value()).cells(), selected->cells());

    Grid2D start;
    start.resize(17, 17);
    start.setCell(1, 1, int(Semantic::Floor));
    Grid2D target;
    target.resize(17, 17);
    target.setCell(15, 15, int(Semantic::Floor));
    REQUIRE(restored.addNode("start", "grid.input").ok());
    REQUIRE(restored.addNode("target", "grid.input").ok());
    REQUIRE(restored.addNode("path", "grid.path").ok());
    REQUIRE(restored.setNodeGrid("start", start).ok());
    REQUIRE(restored.setNodeGrid("target", target).ok());
    REQUIRE(restored.connect("maze", "path", 0).ok());
    REQUIRE(restored.connect("start", "path", 1).ok());
    REQUIRE(restored.connect("target", "path", 2).ok());
    CHECK(restored.execute("path").ok());
}

TEST_CASE("procgen.gridGraph.reusesRegisteredRoguelikeAndExtractsSemanticLayers") {
    GeneratorRegistry::instance().registerBuiltins();
    for (const int seed : {7, 42, 436}) {
        Params params;
        params.setSize(31, 23);
        params.setSeed(std::uint32_t(seed));
        params.setInt("roomCount", 9);
        params.setString("layoutStyle", "clustered");
        params.setString("connectionStyle", "nearest");
        params.setString("corridorStyle", "l");

        Grid2D      expected;
        std::string error;
        REQUIRE(GeneratorRegistry::instance().generate("level.roguelike", params, expected, error));

        GridGraph graph;
        REQUIRE(graph.addNode("level", "generate.registry").ok());
        REQUIRE(graph.setNodeString("level", "algorithm", "level.roguelike").ok());
        REQUIRE(graph.setNodeInt("level", "width", 31).ok());
        REQUIRE(graph.setNodeInt("level", "height", 23).ok());
        REQUIRE(graph.setNodeInt("level", "seed", seed).ok());
        REQUIRE(graph.setNodeInt("level", "roomCount", 9).ok());
        REQUIRE(graph.setNodeString("level", "layoutStyle", "clustered").ok());
        REQUIRE(graph.setNodeString("level", "connectionStyle", "nearest").ok());
        REQUIRE(graph.setNodeString("level", "corridorStyle", "l").ok());
        REQUIRE(graph.addNode("floor", "select.semantic").ok());
        REQUIRE(graph.addNode("corridor", "select.semantic").ok());
        REQUIRE(graph.setNodeInt("floor", "semantic", int(Semantic::Floor)).ok());
        REQUIRE(graph.setNodeInt("corridor", "semantic", int(Semantic::Corridor)).ok());
        REQUIRE(graph.connect("level", "floor").ok());
        REQUIRE(graph.connect("level", "corridor").ok());

        auto generated = graph.execute("level");
        REQUIRE(generated.ok());
        const auto& actual = std::get<Grid2D>(generated.value());
        CHECK_EQ(actual.cells(), expected.cells());
        CHECK_EQ(actual.detail(), expected.detail());
        CHECK_EQ(actual.getObjectCount(), expected.getObjectCount());

        auto floor    = graph.execute("floor");
        auto corridor = graph.execute("corridor");
        REQUIRE(floor.ok());
        REQUIRE(corridor.ok());
        int selected = 0;
        for (const auto cell : std::get<Grid2D>(floor.value()).cells()) {
            const bool valid = cell == Semantic::Empty || cell == Semantic::Floor;
            CHECK(valid);
            if (cell == Semantic::Floor) ++selected;
        }
        for (const auto cell : std::get<Grid2D>(corridor.value()).cells()) {
            const bool valid = cell == Semantic::Empty || cell == Semantic::Corridor;
            CHECK(valid);
            if (cell == Semantic::Corridor) ++selected;
        }
        CHECK(selected > 0);

        GridGraph restored;
        REQUIRE(restored.deserializeDefinition(graph.serializeDefinition()).ok());
        auto restoredLevel = restored.execute("level");
        REQUIRE(restoredLevel.ok());
        CHECK_EQ(std::get<Grid2D>(restoredLevel.value()).cells(), expected.cells());
    }

    GridGraph missing;
    REQUIRE(missing.addNode("level", "generate.registry").ok());
    CHECK(!missing.execute("level").ok());
    REQUIRE(missing.setNodeString("level", "algorithm", "missing.generator").ok());
    CHECK(!missing.execute("level").ok());
}

TEST_CASE("procgen.meshGraph.composesGridPointAndMeshDomains") {
    Grid2D grid;
    grid.resize(2, 1);
    grid.setCell(0, 0, int(Semantic::Floor));
    grid.setCell(1, 0, int(Semantic::Floor));
    GridGraph autotile;
    REQUIRE(autotile.addNode("grid", "grid.input").ok());
    REQUIRE(autotile.addNode("autotile", "grid.autotile").ok());
    REQUIRE(autotile.setNodeGrid("grid", grid).ok());
    REQUIRE(autotile.connect("grid", "autotile").ok());
    auto autotiled = autotile.execute("autotile");
    REQUIRE(autotiled.ok());
    grid = std::get<Grid2D>(autotiled.value());

    MeshGraph tiles;
    REQUIRE(tiles.addNode("grid", "grid.input").ok());
    REQUIRE(tiles.addNode("tiles", "mesh.grid_tiles").ok());
    REQUIRE(tiles.setNodeGrid("grid", grid).ok());
    REQUIRE(tiles.connect("grid", "tiles").ok());
    auto tiled = tiles.execute("tiles");
    REQUIRE(tiled.ok());
    CHECK_EQ(tiled.value().getIndexCount(), 48);
    CHECK_EQ(tiled.value().getGroupCount(), 2);
    CHECK(tiled.value().getGroupName(0).find("/dead_end/r180/normal/48") != std::string::npos);
    CHECK(tiled.value().getGroupName(1).find("/dead_end/r0/normal/24") != std::string::npos);

    MeshGraph restoredTiles;
    REQUIRE(restoredTiles.deserializeDefinition(tiles.serializeDefinition()).ok());
    REQUIRE(restoredTiles.setNodeGrid("grid", grid).ok());
    auto restoredMesh = restoredTiles.execute("tiles");
    REQUIRE(restoredMesh.ok());
    CHECK_EQ(restoredMesh.value().getIndexCount(), tiled.value().getIndexCount());
    CHECK(!restoredTiles.setNodeFloat("tiles", "misspelled", 1.f).ok());

    PointSet points;
    points.add(0.f, 0.f, 0.f);
    points.add(4.f, 0.f, 0.f);
    MeshGraph instances;
    REQUIRE(instances.addNode("mesh", "mesh.input").ok());
    REQUIRE(instances.addNode("points", "point.input").ok());
    REQUIRE(instances.addNode("instances", "mesh.instance_points").ok());
    REQUIRE(instances.setNodeMesh("mesh", tiled.value()).ok());
    REQUIRE(instances.setNodePoints("points", points).ok());
    REQUIRE(instances.connect("mesh", "instances", 0).ok());
    REQUIRE(instances.connect("points", "instances", 1).ok());
    auto result = instances.execute("instances");
    REQUIRE(result.ok());
    CHECK_EQ(result.value().getIndexCount(), tiled.value().getIndexCount() * 2);
}
