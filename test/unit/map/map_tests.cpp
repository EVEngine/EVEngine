// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "map_dual_grid_masks.cpp",
    "map_fov.cpp",
    "map_object_contract.cpp",
    "resource_format_map.cpp",
    "map.cpp"
  ]
}
*/
// clang-format on

#include "map/DualGrid.h"
#include "map/FlowField.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/MapObjectContract.h"
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "map/TileConfig.h"
#include "map/TileLayer.h"
#include "map/TileProjection.h"
#include "map/TileSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace eve::map;

namespace {
bool reaches(const Path& path, int x, int y) {
    return path.getLength() > 0 && path.getX(path.getLength() - 1) == x && path.getY(path.getLength() - 1) == y;
}
int visibleCells(const Fov& fov) {
    int count = 0;
    for (int y = 0; y < fov.getHeight(); ++y) {
        for (int x = 0; x < fov.getWidth(); ++x) {
            if (fov.isVisible(x, y)) ++count;
        }
    }
    return count;
}
}  // namespace

// TileLayer stores sparse chunks, publishes revisions on real changes and clips out-of-range writes.
TEST_CASE("map.layer.factoryDimensionsAndTileMutation") {
    auto* module = Map::create();
    auto* layer  = module->newLayer(3, 2, 32.f, 32.f);
    REQUIRE(layer != nullptr);
    CHECK_EQ(layer->getMapWidth(), 3);
    CHECK_EQ(layer->getMapHeight(), 2);
    CHECK_EQ(layer->getTileWidth(), 32.f);
    CHECK_EQ(layer->getTile(1, 1), 0);
    layer->setTile(1, 1, 5);
    CHECK_EQ(layer->getTile(1, 1), 5);
    const int changed = layer->getRevision();
    layer->setTile(1, 1, 5);
    CHECK_EQ(layer->getRevision(), changed);
    layer->setTile(-1, 0, 9);
    CHECK_EQ(layer->getTile(0, 0), 0);
    layer->fill(7);
    CHECK_EQ(layer->getTile(2, 1), 7);
    layer->clear();
    CHECK_EQ(layer->getTile(1, 1), 0);
}

TEST_CASE("map.layer.sparseChunkCountResizeAndFillRect") {
    auto* layer = Map::create()->newLayer(96, 65, 8.f, 8.f);
    REQUIRE(layer != nullptr);
    CHECK_EQ(layer->getChunkSize(), 32);
    CHECK_EQ(layer->getChunkCount(), 9);
    CHECK_EQ(layer->getNonEmptyChunkCount(), 0);
    layer->setTile(0, 0, 1);
    layer->setTile(64, 64, 2);
    CHECK_EQ(layer->getNonEmptyChunkCount(), 2);
    layer->setTile(0, 0, 0);
    CHECK_EQ(layer->getNonEmptyChunkCount(), 1);
    layer->fillRect(-1, 0, 3, 1, 5);
    CHECK_EQ(layer->getTile(0, 0), 5);
    layer->resize(4, 1);
    CHECK_EQ(layer->getMapWidth(), 4);
    CHECK_EQ(layer->getMapHeight(), 1);
    CHECK_EQ(layer->getTile(0, 0), 0);
}

TEST_CASE("map.layer.unsignedFlipBitsSurviveSetFillAndConfig") {
    auto*              layer   = Map::create()->newLayer(2, 2, 16.f, 16.f);
    constexpr uint32_t flipped = 0x80000001u;
    layer->setTile(0, 0, static_cast<int>(flipped));
    CHECK_EQ(static_cast<uint32_t>(layer->getTile(0, 0)), flipped);
    layer->clear();
    layer->fillRect(0, 0, 2, 1, static_cast<int>(flipped));
    CHECK_EQ(static_cast<uint32_t>(layer->getTile(1, 0)), flipped);
    layer->fill(static_cast<int>(flipped));
    CHECK_EQ(static_cast<uint32_t>(layer->getTile(1, 1)), flipped);
    layer->fill(static_cast<int>(0x80000000u));
    CHECK_EQ(layer->getNonEmptyChunkCount(), 0);
    REQUIRE(layer->applyConfig(R"({"width":2,"height":1,"data":[2147483649,536870913]})"));
    CHECK_EQ(tileGid(static_cast<uint32_t>(layer->getTile(0, 0))), uint32_t{1});
}

TEST_CASE("map.layer.failedImportPreservesDimensionsTilesAndRevision") {
    auto* layer = Map::create()->newLayer(2, 1, 16.f, 16.f);
    layer->setTile(0, 0, 7);
    const auto  revision = layer->getRevision();
    std::string error;
    REQUIRE(!applyConfigText(layer, R"({"width":4,"height":4,"data":[1]})", &error));
    CHECK(!error.empty());
    CHECK_EQ(layer->getMapWidth(), 2);
    CHECK_EQ(layer->getMapHeight(), 1);
    CHECK_EQ(layer->getTile(0, 0), 7);
    CHECK_EQ(layer->getRevision(), revision);
}

// Fov algorithm and topology settings accept known names; opaque cells stop sight behind them.
TEST_CASE("map.fov.defaultsAlgorithmSelectionAndInvalidName") {
    Fov fov(4, 4);
    REQUIRE_EQ(fov.getAlgorithm(), std::string("shadowcast"));
    CHECK_EQ(fov.getRadiusMetric(), std::string("euclidean"));
    fov.setRadiusMetric("chebyshev");
    CHECK_EQ(fov.getRadiusMetric(), std::string("chebyshev"));
    fov.setRadiusMetric("unknown");
    CHECK_EQ(fov.getRadiusMetric(), std::string("chebyshev"));
    for (const char* mode : {"raycast", "permissive", "rectangle"}) {
        fov.setAlgorithm(mode);
        CHECK_EQ(fov.getAlgorithm(), std::string(mode));
    }
    fov.setAlgorithm("unknown");
    CHECK_EQ(fov.getAlgorithm(), std::string("rectangle"));
    CHECK_EQ(fov.getMode(), std::string("grid2d"));
}

TEST_CASE("map.fov.chebyshevRadiusBoundaryAndWallOcclusion") {
    Fov open(11, 11);
    open.setBlockEmpty(false);
    open.setRadiusMetric("chebyshev");
    REQUIRE(open.addRevealer(5, 5, 2) >= 1);
    open.compute();
    CHECK(open.isVisible(7, 7));
    CHECK(!open.isVisible(8, 5));
    CHECK_EQ(visibleCells(open), 25);
    Fov blocked(9, 5);
    blocked.setBlockEmpty(false);
    blocked.setRadiusMetric("chebyshev");
    for (int y = 0; y < 5; ++y) blocked.setOpaque(4, y, true);
    blocked.addRevealer(1, 2, 6);
    blocked.compute();
    CHECK(blocked.isVisible(4, 2));
    CHECK(!blocked.isVisible(6, 2));
}

TEST_CASE("map.fov.exploredMemoryAndSnapshotRoundTrip") {
    Fov fov(7, 7);
    fov.setBlockEmpty(false);
    fov.setRadiusMetric("chebyshev");
    int revealer = fov.addRevealer(2, 2, 1);
    fov.compute();
    fov.setRevealerPosition(revealer, 5, 5);
    fov.compute();
    CHECK(fov.isExplored(2, 2));
    CHECK(!fov.isVisible(2, 2));
    CHECK(fov.isVisible(5, 5));
    auto saved = fov.snapshot();
    fov.clearMemory();
    CHECK(!fov.isExplored(2, 2));
    REQUIRE(fov.restore(saved).ok());
    CHECK(fov.isExplored(2, 2));
    CHECK(fov.isVisible(5, 5));
    auto invalid = saved;
    invalid.width += 1;
    CHECK(!fov.restore(invalid).ok());
    CHECK(fov.isExplored(2, 2));
}

TEST_CASE("map.fov.boundLayerUsesNavigationProfileOpacity") {
    auto* layer = Map::create()->newLayer(3, 1, 16.f, 16.f);
    layer->setTileNavigationProfile(7, false, 1.f, 0xff, 0xff, true, 0x20);
    layer->setTile(1, 0, 7);
    Fov fov(layer);
    fov.syncFromLayer();
    REQUIRE(fov.isOpaque(1, 0));
}

TEST_CASE("map.fov.hexRadiusWallAndPerceptionThreshold") {
    Fov fov(11, 11);
    fov.setTopology("hex");
    fov.setBlockEmpty(false);
    fov.setPerceptionRadiusScale(1.f);
    int id = fov.addRevealer(5, 5, 2);
    fov.setRevealerPerception(id, 2.f);
    REQUIRE_EQ(fov.getTopology(), std::string("hex"));
    CHECK_EQ(fov.getEffectiveRadius(id), 4);
    fov.compute();
    CHECK(fov.isVisible(6, 5));
    CHECK(!fov.isVisible(0, 0));
    fov.setDetectionMargin(0.f);
    CHECK(fov.canDetect(id, 5, 5, 1.f));
    CHECK(!fov.canDetect(id, 5, 5, 3.f));
    for (int y = 0; y < 11; ++y) fov.setOpaque(7, y, true);
    fov.compute();
    CHECK(!fov.isVisible(9, 5));
}

// Pathfinder and FlowField use the layer grid as authority and return empty paths for unreachable goals.
TEST_CASE("map.path.topologySelectionAndUnreachableWall") {
    auto* layer = Map::create()->newLayer(5, 3, 8.f, 8.f);
    layer->fill(2);
    for (int y = 0; y < 3; ++y) layer->setTile(2, y, 1);
    Pathfinder finder(layer);
    finder.blockGid(1);
    finder.setTopology("ortho4");
    CHECK_EQ(finder.getTopology(), std::string("ortho4"));
    finder.setTopology("invalid");
    CHECK_EQ(finder.getTopology(), std::string("ortho4"));
    std::unique_ptr<Path> path(finder.findPath(0, 1, 4, 1));
    REQUIRE(path != nullptr);
    CHECK_EQ(path->getLength(), 0);
    layer->setTile(2, 1, 2);
    path.reset(finder.findPath(0, 1, 4, 1));
    REQUIRE(path != nullptr);
    CHECK(reaches(*path, 4, 1));
    for (int index = 0; index < path->getLength(); ++index) {
        CHECK(finder.isWalkable(path->getX(index), path->getY(index)));
    }
}

TEST_CASE("map.path.navigationProfileBlocksPathAndFovTogether") {
    auto* layer = Map::create()->newLayer(3, 1, 16.f, 16.f);
    layer->setTileNavigationProfile(7, false, 1.f, 0xff, 0xff, true, 0x20);
    layer->setTile(1, 0, 7);
    Pathfinder            finder(layer);
    std::unique_ptr<Path> path(finder.findPath(0, 0, 2, 0));
    REQUIRE(path != nullptr);
    CHECK_EQ(path->getLength(), 0);
    Fov fov(layer);
    fov.syncFromLayer();
    CHECK(fov.isOpaque(1, 0));
}

TEST_CASE("map.path.flowFieldAndAstarAgreeOnGoal") {
    auto* layer = Map::create()->newLayer(10, 8, 16.f, 16.f);
    layer->fill(2);
    for (int y = 0; y < 6; ++y) layer->setTile(4, y, 1);
    Pathfinder finder(layer);
    finder.blockGid(1);
    finder.setTopology("ortho4");
    std::unique_ptr<FlowField> field(finder.buildFlowField(9, 1));
    REQUIRE(field != nullptr);
    CHECK(field->isReachable(0, 0));
    std::unique_ptr<Path> aStar(finder.findPath(0, 0, 9, 1));
    std::unique_ptr<Path> flow(finder.followFlow(field.get(), 0, 0));
    REQUIRE(aStar != nullptr);
    REQUIRE(flow != nullptr);
    CHECK(reaches(*aStar, 9, 1));
    CHECK(reaches(*flow, 9, 1));
}

TEST_CASE("map.path.dynamicPenaltyDetoursWithoutChangingBaseCost") {
    Pathfinder            finder(5, 3);
    std::unique_ptr<Path> direct(finder.findPath(0, 1, 4, 1));
    REQUIRE(direct != nullptr);
    CHECK_EQ(direct->getLength(), 5);
    std::unique_ptr<Path> detour(
        finder.findPath(0, 1, 4, 1, [](int x, int y) { return x == 2 && y == 1 ? 20.f : 0.f; }));
    REQUIRE(detour != nullptr);
    CHECK_GT(detour->getLength(), direct->getLength());
    for (int index = 0; index < detour->getLength(); ++index) {
        const bool entersBlockedCell = detour->getX(index) == 2 && detour->getY(index) == 1;
        CHECK(!entersBlockedCell);
    }
    CHECK_EQ(finder.getCellCost(2, 1), 1.f);
}

TEST_CASE("map.path.layerRevisionAutomaticallyRebuildsObstacles") {
    auto* layer = Map::create()->newLayer(5, 1, 8.f, 8.f);
    layer->fill(2);
    Pathfinder finder(layer);
    finder.blockGid(1);
    std::unique_ptr<Path> initial(finder.findPath(0, 0, 4, 0));
    REQUIRE(initial != nullptr);
    CHECK(reaches(*initial, 4, 0));
    layer->setTile(2, 0, 1);
    std::unique_ptr<Path> blocked(finder.findPath(0, 0, 4, 0));
    REQUIRE(blocked != nullptr);
    CHECK_EQ(blocked->getLength(), 0);
    layer->setTile(2, 0, 2);
    std::unique_ptr<Path> reopened(finder.findPath(0, 0, 4, 0));
    REQUIRE(reopened != nullptr);
    CHECK(reaches(*reopened, 4, 0));
}

// Map object contracts validate data shape and geometry before consumers see objects.
TEST_CASE("map.objectContract.requiredEnumKindAndBounds") {
    const std::string contract = R"({
      "schema":"eve.map.object-contract","version":1,
      "types":[{"type":"portal","properties":[
        {"name":"target","kind":"string","required":true,"enum":["forest"]},
        {"name":"x","kind":"int","required":true,"min":0,"max":10},
        {"name":"enabled","kind":"bool","required":true}]}]})";
    MapObject         portal;
    portal.name       = "gate";
    portal.type       = "portal";
    portal.properties = {{"target", "forest"}, {"x", "4"}, {"enabled", "true"}};
    std::vector<MapObject> objects{portal};
    REQUIRE(validateMapObjects(objects, contract).ok());
    objects[0].properties["x"] = "4.5";
    CHECK(!validateMapObjects(objects, contract).ok());
    objects[0].properties["x"] = "11";
    CHECK(!validateMapObjects(objects, contract).ok());
    objects[0].properties["x"]      = "4";
    objects[0].properties["target"] = "village";
    CHECK(!validateMapObjects(objects, contract).ok());
}

TEST_CASE("map.objectContract.duplicateNamesNonfiniteGeometryAndBadSchema") {
    const std::string contract =
        R"({"schema":"eve.map.object-contract","version":1,"types":[{"type":"spawn","properties":[]}]})";
    MapObject spawn;
    spawn.name = "spawn";
    spawn.type = "spawn";
    std::vector<MapObject> objects{spawn, spawn};
    REQUIRE(!validateMapObjects(objects, contract).ok());
    objects.resize(1);
    objects[0].width = -1.f;
    CHECK(!validateMapObjects(objects, contract).ok());
    objects[0].width = 0.f;
    objects[0].x     = std::numeric_limits<float>::infinity();
    CHECK(!validateMapObjects(objects, contract).ok());
    CHECK(!validateMapObjects({}, R"({"schema":"eve.map.object-contract","version":2,"types":[]})").ok());
}

// Projection converts between map coordinates and world coordinates for each supported layout.
TEST_CASE("map.projection.orthogonalRoundTripAndDepth") {
    TileLayer::Config config;
    config.mapW        = 10;
    config.mapH        = 10;
    config.tileW       = 32.f;
    config.tileH       = 32.f;
    config.originX     = 10.f;
    config.originY     = 20.f;
    config.orientation = MapOrientation::Orthogonal;
    float worldX = 0.f, worldY = 0.f;
    tileToWorld(config, 2, 3, worldX, worldY);
    REQUIRE_EQ(worldX, 74.f);
    CHECK_EQ(worldY, 116.f);
    CHECK_EQ(tileToDepthY(config, 2, 3), 148.f);
    int tileX = -1, tileY = -1;
    worldToTile(config, worldX + 1.f, worldY + 1.f, tileX, tileY);
    CHECK_EQ(tileX, 2);
    CHECK_EQ(tileY, 3);
}

TEST_CASE("map.projection.isometricAndStaggeredCoordinates") {
    TileLayer::Config isometric;
    isometric.mapW        = 8;
    isometric.mapH        = 8;
    isometric.tileW       = 64.f;
    isometric.tileH       = 32.f;
    isometric.cellGapX    = 16.f;
    isometric.cellGapY    = 8.f;
    isometric.originX     = 100.f;
    isometric.originY     = 20.f;
    isometric.orientation = MapOrientation::Isometric;
    float x = 0.f, y = 0.f;
    tileToWorld(isometric, 3, 2, x, y);
    REQUIRE_EQ(x, 140.f);
    CHECK_EQ(y, 120.f);
    int tileX = -1, tileY = -1;
    worldToTile(isometric, x, y, tileX, tileY);
    CHECK_EQ(tileX, 3);
    CHECK_EQ(tileY, 2);
    TileLayer::Config staggered;
    staggered.tileW        = 64.f;
    staggered.tileH        = 32.f;
    staggered.orientation  = MapOrientation::Staggered;
    staggered.staggerAxis  = StaggerAxis::Y;
    staggered.staggerIndex = StaggerIndex::Odd;
    tileToWorld(staggered, 0, 1, x, y);
    CHECK_EQ(x, 32.f);
    CHECK_EQ(y, 16.f);
}

TEST_CASE("map.config.tiledLayersTilesetAndUnknownOrientation") {
    auto*             layer = Map::create()->newLayer(2, 1, 64.f, 32.f);
    const std::string tiled = R"({
      "width":2,"height":1,"tilewidth":64,"tileheight":32,"orientation":"isometric",
      "layers":[
        {"type":"tilelayer","width":2,"height":1,"data":[7,0]},
        {"type":"objectgroup","objects":[]},
        {"type":"tilelayer","width":2,"height":1,"data":[1,1]}
      ],
      "tileset":{"image":"missing.png","firstGid":1,
        "tiles":[{"gid":7,"region":[10,20,96,128],"pivot":[48,104],
                  "footprint":[2,1],"walkable":false,"cost":4,"sortBias":3}]}
    })";
    REQUIRE(layer->applyConfig(tiled));
    CHECK_EQ(layer->getTile(0, 0), 7);
    CHECK_EQ(layer->getTile(1, 0), 0);
    CHECK(static_cast<int>(layer->config()->orientation) == static_cast<int>(MapOrientation::Isometric));
    REQUIRE(layer->getTileVisualCount() == 1);
    const auto& visual = layer->tileset()->visuals[0];
    CHECK_EQ(visual.gid, 7);
    CHECK_EQ(visual.width, 96);
    CHECK_EQ(visual.pivotY, 104.f);
    CHECK_EQ(visual.footprintW, 2);
    CHECK(!visual.walkable);
    const int before = layer->getRevision();
    CHECK(!layer->applyConfig(R"({"orientation":"unknown","width":1,"height":1,"data":[1]})"));
    CHECK_EQ(layer->getRevision(), before);
}

TEST_CASE("map.dualGrid.cornerMaskAndDisplayOffset") {
    CHECK_EQ(dualGridMaskFromCorners(false, false, false, false), 0);
    CHECK_EQ(dualGridMaskFromCorners(true, false, false, false), 1);
    CHECK_EQ(dualGridMaskFromCorners(false, true, false, false), 2);
    CHECK_EQ(dualGridMaskFromCorners(false, false, true, false), 4);
    CHECK_EQ(dualGridMaskFromCorners(false, false, false, true), 8);
    CHECK_EQ(dualGridMaskFromCorners(true, false, false, true), 9);
    CHECK_EQ(dualGridMaskFromCorners(true, true, true, true), 15);
    CHECK_EQ(dualGridDefaultFrame(0), -1);
    CHECK_EQ(dualGridDefaultFrame(15), 6);
    auto* module  = Map::create();
    auto* logic   = module->newLayer(2, 2, 32.f, 32.f);
    auto* display = module->newLayer(1, 1, 8.f, 8.f);
    logic->setOrigin(10.f, 20.f);
    logic->setTile(0, 0, 1);
    logic->setTile(1, 1, 1);
    DualGridOptions options;
    options.useDefaultFrameTable = false;
    options.firstDisplayGid      = 10;
    options.hideLogic            = true;
    std::string error;
    REQUIRE(resolveDualGrid(logic, display, options, &error));
    CHECK(error.empty());
    CHECK_EQ(display->getMapWidth(), 3);
    CHECK_EQ(display->getMapHeight(), 3);
    CHECK_EQ(display->getTileWidth(), 32.f);
    CHECK_EQ(display->getX(), -6.f);
    CHECK_EQ(display->getY(), 4.f);
    CHECK(!logic->isVisible());
    CHECK_EQ(dualGridMaskAt(*logic, 1, 1, 0), 9);
    CHECK_EQ(display->getTile(1, 1), 19);
    CHECK_EQ(display->getTile(2, 0), 0);
}

TEST_CASE("map.dualGrid.sameLayerRejectedWithoutMutation") {
    auto* layer = Map::create()->newLayer(2, 2, 16.f, 16.f);
    layer->setTile(0, 0, 1);
    const int   revision = layer->getRevision();
    std::string error;
    REQUIRE(!resolveDualGrid(layer, layer, DualGridOptions{}, &error));
    CHECK(!error.empty());
    CHECK_EQ(layer->getTile(0, 0), 1);
    CHECK_EQ(layer->getRevision(), revision);
}

#include <limits>
#include "map/Pathfinder.h"
#include "zeroerr/unittest.h"
namespace test_map_navigation_snapshot_migrated {
using namespace eve::map;
TEST_CASE("map.navigation.snapshotOwnsInputsAndOnlyVersionsChangedContent") {
    NavigationGridSnapshot captured;
    {
        Pathfinder map(3, 3);
        auto       first = map.captureNavigationGrid();
        REQUIRE(first.ok());
        captured = std::move(first).takeValue();
        CHECK_EQ(captured.width(), 3);
        CHECK_EQ(captured.costs().size(), 9u);
        const auto revision = captured.revision();
        CHECK_EQ(map.captureNavigationGrid().value().revision(), revision);
        map.setBlocked(1, 1, true);
        auto changed = map.captureNavigationGrid().takeValue();
        CHECK(changed.revision() > revision);
        CHECK_EQ(changed.costs()[4], 0.f);
        CHECK_EQ(captured.costs()[4], 1.f);
        map.setBlocked(1, 1, true);
        CHECK_EQ(map.captureNavigationGrid().value().revision(), changed.revision());
        map.setTopology("hex");
        auto hex = map.captureNavigationGrid().takeValue();
        CHECK(hex.topology() == NavigationTopology::Hex);
        CHECK(hex.revision() > changed.revision());
    }
    CHECK(captured.valid());
    CHECK_EQ(captured.costs()[4], 1.f);
}
TEST_CASE("map.navigation.snapshotRejectsAbsentAndMalformedSources") {
    Pathfinder map;
    REQUIRE(!map.captureNavigationGrid().ok());
    map.setSize(2, 2);
    auto previous = map.captureNavigationGrid().takeValue();
    map.setCellCost(0, 0, std::numeric_limits<float>::quiet_NaN());
    REQUIRE(!map.captureNavigationGrid().ok());
    CHECK_EQ(previous.costs()[0], 1.f);
    map.setCellCost(0, 0, 1.f);
    CHECK_EQ(map.captureNavigationGrid().value().revision(), previous.revision());
}

TEST_CASE("map.navigation.changeHintObservesLayerEditsWithoutCapturing") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto*            layer = TileLayer::createLayer(2, 2, 1.f, 1.f);
    Pathfinder       map(layer);
    map.blockGid(3);
    const auto before = map.getNavigationChangeStamp();
    REQUIRE(map.getNavigationChangeStamp() == before);
    layer->setTile(0, 0, 3);
    const auto edited = map.getNavigationChangeStamp();
    REQUIRE(edited.configuration == before.configuration);
    REQUIRE(edited.layer != before.layer);
    auto captured = map.captureNavigationGrid().takeValue();
    REQUIRE_EQ(captured.costs()[0], 0.f);
    REQUIRE(map.getNavigationChangeStamp() == edited);
    map.setCellCost(1, 1, 2.f);
    REQUIRE(map.getNavigationChangeStamp().configuration > edited.configuration);
}

}  // namespace test_map_navigation_snapshot_migrated

#include "map/Fov.h"
namespace test_rts_performance_semantics_migrated {
TEST_CASE("rts.performanceFovIndexSurvivesOrderedRemovalMoveAndClear") {
    eve::map::Fov fov(40, 20);
    const int first = fov.addRevealer(2, 2, 1), middle = fov.addRevealer(10, 2, 1), last = fov.addRevealer(20, 2, 1);
    fov.removeRevealer(middle);
    fov.setRevealerPosition(last, 30, 2);
    fov.compute();
    REQUIRE(fov.isVisible(2, 2));
    REQUIRE(fov.isVisible(30, 2));
    REQUIRE(!fov.isVisible(10, 2));
    eve::map::Fov moved(std::move(fov));
    moved.removeRevealer(first);
    moved.setRevealerRadius(last, 2);
    moved.compute();
    REQUIRE(!moved.isVisible(2, 2));
    REQUIRE(moved.isVisible(31, 2));
    moved.clearRevealers();
    moved.setRevealerPosition(last, 1, 1);
    const int fresh = moved.addRevealer(5, 5, 1);
    REQUIRE(fresh != last);
    moved.compute();
    REQUIRE(moved.isVisible(5, 5));
    REQUIRE(!moved.isVisible(30, 2));
}

TEST_CASE("rts.performanceOpenHeightmapMatchesHeightRayPathAndInvalidatesOnEdits") {
    eve::map::Fov fast(50, 50), reference(50, 50);
    for (auto* fov : {&fast, &reference}) {
        fov->setMode("heightmap");
        fov->setEyeOffset(1.f);
        fov->setCliffBlock(0.f);
        fov->addRevealer(10, 10, 8);
        fov->addRevealer(15, 14, 6);
    }
    // Outside every revealer's radius; forces the reference through ordinary height rays.
    reference.setOpaque(49, 49, true);
    for (int stage = 0; stage < 3; ++stage) {
        if (stage == 1)
            for (auto* fov : {&fast, &reference}) fov->setElevation(12, 10, 4.f);
        if (stage == 2)
            for (auto* fov : {&fast, &reference}) fov->setOpaque(10, 12, true);
        fast.compute();
        reference.compute();
        for (int y = 0; y < 50; ++y)
            for (int x = 0; x < 50; ++x) REQUIRE(fast.isVisible(x, y) == reference.isVisible(x, y));
    }
}
}  // namespace test_rts_performance_semantics_migrated
