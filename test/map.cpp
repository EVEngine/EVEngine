#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "data/DataModule.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "map/DualGrid.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/MapObject.h"
#include "map/Pathfinder.h"
#include "map/TileCollision.h"
#include "map/TileConfig.h"
#include "map/TileLayer.h"
#include "map/TileProjection.h"
#include "map/TileSystem.h"
#include "window/Window.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace eve::map;

namespace {
class CollisionSinkMock final : public ITileCollisionSink {
public:
    void replaceTileCollision(const void *source, const TileCollisionRect *rects,
                              size_t count) override {
        layer = source;
        values.assign(rects, rects + count);
    }
    const void *layer = nullptr;
    std::vector<TileCollisionRect> values;
};
}  // namespace

namespace {

void hideAllTileLayers() {
    if (ecs::current()->getManager<TileLayer>() == nullptr) return;
    auto view = ecs::View<TileLayer, TileLayer::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw] = *it;
        draw->visible = false;
    }
}

}  // namespace

TEST_CASE("map.newLayer.ecsView") {
    auto *mod = Map::create();
    int before = mod->getLayerCount();
    TileLayer *layer = mod->newLayer(4, 3, 16.f, 16.f);
    REQUIRE(layer != nullptr);
    CHECK_EQ(layer->getMapWidth(), 4);
    CHECK_EQ(layer->getMapHeight(), 3);
    CHECK_EQ(layer->getTileWidth(), 16.f);
    CHECK_EQ(layer->getTileHeight(), 16.f);
    CHECK_EQ(mod->getLayerCount(), before + 1);
    CHECK(layer->config()->entity == layer);
}

TEST_CASE("map.navigationProfile.drivesPathCollisionAndFov") {
    auto *mod   = Map::create();
    auto *layer = mod->newLayer(3, 1, 16.f, 16.f);
    layer->setTileNavigationProfile(7, false, 1.f, 0xff, 0xff, true, 0x20);
    layer->setTile(1, 0, 7);

    Pathfinder pathfinder(layer);
    Path      *path = pathfinder.findPath(0, 0, 2, 0);
    REQUIRE(path != nullptr);
    CHECK_EQ(path->getLength(), 0);
    delete path;

    CHECK_EQ(mod->publishCollision(layer), 1);
    Fov fov(layer);
    fov.syncFromLayer();
    CHECK(fov.isOpaque(1, 0));
}

TEST_CASE("map.layer.setGetTile") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(3, 2, 32.f, 32.f);
    CHECK_EQ(layer->getTile(0, 0), 0);
    layer->setTile(1, 1, 5);
    CHECK_EQ(layer->getTile(1, 1), 5);
    layer->setTile(-1, 0, 9);  // out of range no-op
    CHECK_EQ(layer->getTile(0, 0), 0);
    layer->fill(7);
    CHECK_EQ(layer->getTile(0, 0), 7);
    CHECK_EQ(layer->getTile(2, 1), 7);
    layer->clear();
    CHECK_EQ(layer->getTile(1, 0), 0);
}

TEST_CASE("map.layer.resizeClears") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 8.f, 8.f);
    layer->setTile(0, 0, 3);
    layer->resize(4, 1);
    CHECK_EQ(layer->getMapWidth(), 4);
    CHECK_EQ(layer->getMapHeight(), 1);
    CHECK_EQ(layer->getTile(0, 0), 0);
}

TEST_CASE("map.layer.chunkIndexAndRevision") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(96, 65, 8.f, 8.f);
    CHECK_EQ(layer->getChunkSize(), 32);
    CHECK_EQ(layer->getChunkCount(), 9);
    CHECK_EQ(layer->getNonEmptyChunkCount(), 0);
    const int revision = layer->getRevision();
    layer->setTile(0, 0, 1);
    layer->setTile(64, 64, 2);
    CHECK_EQ(layer->getNonEmptyChunkCount(), 2);
    CHECK_GT(layer->getRevision(), revision);
    const int unchanged = layer->getRevision();
    layer->setTile(64, 64, 2);
    CHECK_EQ(layer->getRevision(), unchanged);
    layer->setTile(0, 0, 0);
    CHECK_EQ(layer->getNonEmptyChunkCount(), 1);
}

TEST_CASE("map.layer.fillRectClipsAndPublishesOnce") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(64, 64, 8.f, 8.f);
    const int before = layer->getRevision();
    layer->fillRect(-2, -2, 5, 5, 7);
    CHECK_EQ(layer->getTile(0, 0), 7);
    CHECK_EQ(layer->getTile(2, 2), 7);
    CHECK_EQ(layer->getTile(3, 3), 0);
    CHECK_EQ(layer->getNonEmptyChunkCount(), 1);
    CHECK_EQ(layer->getRevision(), before + 1);
    layer->fillRect(31, 31, 2, 2, 8);
    CHECK_EQ(layer->getNonEmptyChunkCount(), 4);
}

TEST_CASE("map.render.sparseChunksBoundCollectionWork") {
    hideAllTileLayers();
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(1024, 1024, 8.f, 8.f);
    layer->setTile(1, 1, 1);
    layer->setTile(900, 900, 2);
    std::vector<eve::graphics::DrawItem2D> items;
    TileRenderSystem::collect(items);
    CHECK_EQ(int(items.size()), 2);
    CHECK_EQ(TileRenderSystem::lastVisitedChunkCount(), 2);
    CHECK_EQ(TileRenderSystem::lastVisitedCellCount(), 2 * 32 * 32);
    layer->setVisible(false);
}

TEST_CASE("map.layer.applyConfigFlat") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 16.f, 16.f);
    const char *json = R"({
        "width": 2,
        "height": 2,
        "tilewidth": 16,
        "tileheight": 16,
        "data": [1, 2, 3, 4],
        "origin": [10, 20],
        "layer": 2,
        "visible": true,
        "tint": [1, 0.5, 0.25, 1]
    })";
    CHECK(layer->applyConfig(json));
    CHECK_EQ(layer->getTile(0, 0), 1);
    CHECK_EQ(layer->getTile(1, 0), 2);
    CHECK_EQ(layer->getTile(0, 1), 3);
    CHECK_EQ(layer->getTile(1, 1), 4);
    CHECK_EQ(layer->getX(), 10.f);
    CHECK_EQ(layer->getY(), 20.f);
    CHECK_EQ(layer->getLayer(), 2);
    CHECK(layer->isVisible());
}

TEST_CASE("map.layer.applyConfigMultiLayerPicksFirst") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(2, 1, 8.f, 8.f);
    const char *json = R"({
        "width": 2,
        "height": 1,
        "tilewidth": 8,
        "tileheight": 8,
        "layers": [
            { "type": "tilelayer", "width": 2, "height": 1, "data": [9, 8] },
            { "type": "objectgroup", "objects": [] },
            { "type": "tilelayer", "width": 2, "height": 1, "data": [1, 1] }
        ]
    })";
    CHECK(layer->applyConfig(json));
    CHECK_EQ(layer->getTile(0, 0), 9);
    CHECK_EQ(layer->getTile(1, 0), 8);
}

TEST_CASE("map.tileGid.masksFlipFlags") {
    CHECK_EQ(tileGid(0), 0u);
    CHECK_EQ(tileGid(5), 5u);
    CHECK_EQ(tileGid(0x80000005u), 5u);
}

TEST_CASE("map.tileset.irregularVisualMetadata") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(2, 1, 64.f, 32.f);
    layer->setTileVisual(7, 10, 20, 96, 128, 48.f, 104.f, 3.f);
    layer->setTileMetadata(7, 2, 1, false, 4.f);
    REQUIRE(layer->getTileVisualCount() == 1);
    const auto &visual = layer->tileset()->visuals[0];
    CHECK_EQ(visual.gid, 7);
    CHECK_EQ(visual.x, 10);
    CHECK_EQ(visual.y, 20);
    CHECK_EQ(visual.width, 96);
    CHECK_EQ(visual.height, 128);
    CHECK_EQ(visual.pivotX, 48.f);
    CHECK_EQ(visual.pivotY, 104.f);
    CHECK_EQ(visual.sortBias, 3.f);
    CHECK_EQ(visual.footprintW, 2);
    CHECK(!visual.walkable);
    CHECK_EQ(visual.cost, 4.f);
    layer->clearTileVisuals();
    CHECK_EQ(layer->getTileVisualCount(), 0);
    layer->setVisible(false);
}

TEST_CASE("map.tileset.manifestShapeInMapConfig") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(1, 1, 64.f, 32.f);
    const char *json = R"({
      "width":1,"height":1,"orientation":"isometric","data":[7],
      "tileset":{
        "image":"missing-atlas.png","firstGid":1,
        "tiles":[{
          "gid":7,"region":[10,20,96,128],"pivot":[48,104],
          "footprint":[2,1],"walkable":false,"cost":4,"sortBias":3
        }]
      }
    })";
    CHECK(layer->applyConfig(json));
    REQUIRE(layer->getTileVisualCount() == 1);
    const auto &visual = layer->tileset()->visuals[0];
    CHECK_EQ(visual.gid, 7);
    CHECK_EQ(visual.width, 96);
    CHECK_EQ(visual.pivotY, 104.f);
    CHECK_EQ(visual.footprintW, 2);
    CHECK(!visual.walkable);
    layer->setVisible(false);
}

TEST_CASE("map.layer.applyTiledInfiniteChunks") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(1, 1, 16.f, 16.f);
    const char *json = R"({
      "infinite":true,"width":0,"height":0,"tilewidth":16,"tileheight":16,
      "layers":[{"type":"tilelayer","chunks":[
        {"x":-2,"y":-1,"width":2,"height":2,"data":[1,2,3,4]},
        {"x":0,"y":-1,"width":2,"height":2,"data":[5,6,7,8]}
      ]}]
    })";
    CHECK(layer->applyConfig(json));
    CHECK_EQ(layer->getMapWidth(), 4);
    CHECK_EQ(layer->getMapHeight(), 2);
    CHECK_EQ(layer->getTile(0, 0), 1);
    CHECK_EQ(layer->getTile(3, 1), 8);
    CHECK_EQ(layer->getX(), -32.f);
    CHECK_EQ(layer->getY(), -16.f);
}

TEST_CASE("map.tileset.v2AnimationCustomDataAndTerrain") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(3, 3, 16.f, 16.f);
    layer->addTileAnimationFrame(10, 11, 100);
    layer->addTileAnimationFrame(10, 12, 150);
    CHECK_EQ(layer->getTileAnimationFrameCount(10), 2);
    layer->setTileDataString(10, "biome", "forest");
    layer->setTileDataNumber(10, "damage", 2.5f);
    layer->setTileDataBool(10, "wet", true);
    CHECK_EQ(layer->getTileDataType(10, "damage"), std::string("number"));
    CHECK_EQ(layer->getTileDataString(10, "biome"), std::string("forest"));
    CHECK_EQ(layer->getTileDataNumber(10, "damage"), 2.5f);
    CHECK(layer->getTileDataBool(10, "wet"));
    layer->setTerrainRule(20, 1, 0);
    layer->setTerrainRule(21, 1, 1 << 3);
    layer->paintTerrain(1, 1, 1);
    CHECK_EQ(layer->getTerrain(1, 1), 1);
    CHECK_EQ(layer->getTile(1, 1), 20);
    layer->paintTerrain(2, 1, 1);
    CHECK_EQ(layer->getTile(1, 1), 21);
    layer->clearTileAnimation(10);
    CHECK_EQ(layer->getTileAnimationFrameCount(10), 0);
}

TEST_CASE("map.collision.greedyMergeAndCapabilityPublish") {
    Map map;
    TileLayer *layer = map.newLayer(5, 4, 16.f, 8.f);
    layer->setTileMetadata(9, 1, 1, false);
    layer->fillRect(1, 1, 3, 2, 9);
    CollisionSinkMock sink;
    eve::cap::addListener<ITileCollisionSink>(&sink);

    CHECK(map.publishCollision(layer) == 1);
    CHECK(map.getCollisionRectCount() == 1);
    CHECK(map.getCollisionRectX(0) == 16.f);
    CHECK(map.getCollisionRectY(0) == 8.f);
    CHECK(map.getCollisionRectWidth(0) == 48.f);
    CHECK(map.getCollisionRectHeight(0) == 16.f);
    CHECK(sink.layer == layer);
    REQUIRE(sink.values.size() == 1);
    CHECK(sink.values[0].width == 48.f);
    eve::cap::removeListener<ITileCollisionSink>(&sink);
}

TEST_CASE("map.tileset.v2ManifestReadsTiledFields") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(1, 1, 16.f, 16.f);
    const char *json = R"({
      "width":1,"height":1,"data":[1],
      "tileset":{"firstGid":1,"columns":4,"tileWidth":16,"tileHeight":16,
        "tiles":[{"id":0,"animation":[{"tileid":1,"duration":80},{"tileid":2,"duration":120}],
          "terrain":2,"neighborMask":0,
          "properties":[{"name":"damage","type":"float","value":3.5}]}]}
    })";
    CHECK(layer->applyConfig(json));
    CHECK_EQ(layer->getTileAnimationFrameCount(1), 2);
    CHECK_EQ(layer->getTileDataType(1, "damage"), std::string("float"));
    CHECK_EQ(layer->getTileDataString(1, "damage"), std::string("3.5"));
    layer->paintTerrain(0, 0, 2);
    CHECK_EQ(layer->getTile(0, 0), 1);
}

TEST_CASE("map.autotile.wallAndWaterfallResolveExactDirtyRegion") {
    auto      *mod  = Map::create();
    TileLayer *wall = mod->newLayer(3, 5, 16.f, 16.f);
    wall->defineAutotileFamily(4, "wall", 17);
    wall->setAutotileRule(40, 4, 1 << 5, 1);               // top: connected south
    wall->setAutotileRule(41, 4, (1 << 1) | (1 << 5), 1);  // body
    wall->setAutotileRule(42, 4, 1 << 1, 1);               // foot: connected north
    const int before = wall->getRevision();
    wall->paintTerrainRect(1, 1, 1, 3, 4);
    CHECK_EQ(wall->getRevision(), before + 1);
    CHECK_EQ(wall->getTile(1, 1), 40);
    CHECK_EQ(wall->getTile(1, 2), 41);
    CHECK_EQ(wall->getTile(1, 3), 42);

    TileLayer *waterfall = mod->newLayer(1, 3, 16.f, 16.f);
    waterfall->defineAutotileFamily(8, "waterfall", 23);
    waterfall->setAutotileRule(80, 8, 1 << 5, 1);
    waterfall->setAutotileRule(81, 8, (1 << 1) | (1 << 5), 1);
    waterfall->setAutotileRule(82, 8, 1 << 1, 1);
    waterfall->paintTerrainRect(0, 0, 1, 3, 8);
    CHECK_EQ(waterfall->getTile(0, 0), 80);
    CHECK_EQ(waterfall->getTile(0, 1), 81);
    CHECK_EQ(waterfall->getTile(0, 2), 82);
    waterfall->addTileAnimationFrame(81, 81, 120);
    waterfall->addTileAnimationFrame(81, 83, 120);
    CHECK_EQ(waterfall->getTileAnimationFrameCount(81), 2);
}

TEST_CASE("map.rpgmaker.importsPlanesPassageAndSemanticsTransactionally") {
    const std::string dataDir  = std::string(EVENGINE_SOURCE_DIR) + "/test/data/rpgmaker/";
    auto              imported = importRpgMakerMap(dataDir + "Map001.json", dataDir + "Tilesets.json", "RPG Maker MZ");
    REQUIRE(imported.ok());
    auto receipt = std::move(imported).value();
    CHECK_EQ(receipt.sourceEngine, std::string("RPG Maker MZ"));
    CHECK_EQ(receipt.tilesetId, 1);
    REQUIRE(receipt.layers.size() == 4);
    TileLayer *ground = receipt.layers.front();
    CHECK_EQ(ground->getTile(0, 0), 1);
    CHECK_EQ(ground->getTile(1, 0), 2);
    CHECK_EQ(ground->resource()->sourceVersion, std::string("MV/MZ"));
    REQUIRE(receipt.navigationLayer != nullptr);
    Pathfinder pathfinder(receipt.navigationLayer);
    CHECK(!pathfinder.isWalkable(0, 0));
    CHECK(pathfinder.isWalkable(1, 0));
    REQUIRE(ground->tileset()->atlases.size() >= 6);

    auto failed = importRpgMakerMap(dataDir + "missing.json", dataDir + "Tilesets.json");
    CHECK(!failed.ok());
    REQUIRE(failed.error() != nullptr);
    CHECK_EQ(failed.error()->source(), std::string("map.rpgmaker"));
}

TEST_CASE("map.rpgmaker.decodesA1A3A4QuarterTileTablesAndAnimation") {
    const auto water = decodeRpgMakerTileVisual(2048);
    REQUIRE(water.subtileFrames.size() == 4);
    CHECK_EQ(water.subtileFrames[0].parts.size(), size_t(4));
    CHECK_NE(water.subtileFrames[0].parts[0].x, water.subtileFrames[1].parts[0].x);

    const auto waterfall = decodeRpgMakerTileVisual(2048 + 5 * 48);
    REQUIRE(waterfall.subtileFrames.size() == 3);
    CHECK_NE(waterfall.subtileFrames[0].parts[0].y, waterfall.subtileFrames[1].parts[0].y);

    const auto roof = decodeRpgMakerTileVisual(4352);
    REQUIRE(roof.subtileFrames.size() == 1);
    CHECK_EQ(roof.subtileFrames[0].parts.size(), size_t(4));
    const auto wall = decodeRpgMakerTileVisual(5888 + 8 * 48);
    REQUIRE(wall.subtileFrames.size() == 1);
    CHECK_EQ(wall.subtileFrames[0].parts.size(), size_t(4));
}

TEST_CASE("map.tileset.importsTiledWangConnectivity") {
    auto       *mod   = Map::create();
    TileLayer  *layer = mod->newLayer(2, 1, 16.f, 16.f);
    const char *json  = R"({
      "width":2,"height":1,"data":[0,0],
      "tileset":{"firstgid":10,"columns":4,"tilewidth":16,"tileheight":16,
        "wangsets":[{"name":"shore","wangtiles":[
          {"tileid":2,"wangid":[0,0,1,0,0,0,0,0]},
          {"tileid":3,"wangid":[0,0,0,0,0,0,1,0]}
        ]}]}
    })";
    CHECK(layer->applyConfig(json));
    REQUIRE(layer->tileset()->terrainRules.size() == 2);
    CHECK_EQ(layer->tileset()->terrainRules[0].gid, 12);
    CHECK_EQ(layer->tileset()->terrainRules[0].terrain, 1);
    CHECK_EQ(layer->tileset()->terrainRules[0].neighborMask, 1 << 3);
    CHECK_EQ(layer->tileset()->terrainRules[1].gid, 13);
    CHECK_EQ(layer->tileset()->terrainRules[1].neighborMask, 1 << 7);
    layer->setVisible(false);
}

TEST_CASE("map.tileset.loadsExternalTsjRelativeToMap") {
    std::string       error;
    const std::string dataDir = std::string(EVENGINE_SOURCE_DIR) + "/test/data/";
    auto              layers  = loadMapFile(dataDir + "tiled_external_map.json", &error);
    CHECK_EQ(error, std::string());
    REQUIRE(layers.size() == 1);
    TileLayer *layer = layers.front();
    CHECK_EQ(layer->getTile(0, 0), 20);
    CHECK_EQ(layer->getTile(1, 0), 30);
    CHECK_EQ(layer->getTilesetFirstGid(), 20);
    REQUIRE(layer->tileset()->atlases.size() == 2);
    CHECK_EQ(layer->tileset()->atlases[1].firstGid, 30);
    CHECK_EQ(layer->getTileDataType(20, "wet"), std::string("bool"));
    CHECK_EQ(layer->getTileDataType(30, "walkable"), std::string("bool"));
    REQUIRE(layer->tileset()->terrainRules.size() == 1);
    CHECK_EQ(layer->tileset()->terrainRules[0].gid, 20);
    CHECK_EQ(layer->tileset()->terrainRules[0].neighborMask, 1 << 3);
    CHECK_EQ(layer->resource()->texturePath, dataDir + "shore-missing.png");
    REQUIRE(layer->resource()->dependencyPaths.size() == 2);
    CHECK_EQ(layer->resource()->dependencyPaths[0], dataDir + "shore.tsj");
    CHECK_EQ(layer->resource()->dependencyPaths[1], dataDir + "walls.tsj");
    Pathfinder pathfinder(layer);
    CHECK(pathfinder.isWalkable(0, 0));
    CHECK(!pathfinder.isWalkable(1, 0));
    layer->setVisible(false);
}

TEST_CASE("map.tileset.loadsExternalTsxWithMetadataAnimationAndWang") {
    std::string       error;
    const std::string path   = std::string(EVENGINE_SOURCE_DIR) + "/test/data/tiled_external_tsx_map.json";
    auto              layers = loadMapFile(path, &error);
    CHECK_EQ(error, std::string());
    REQUIRE(layers.size() == 1);
    TileLayer *layer = layers.front();
    CHECK_EQ(layer->getTile(0, 0), 40);
    CHECK_EQ(layer->getTilesetFirstGid(), 40);
    CHECK_EQ(layer->getTileAnimationFrameCount(40), 2);
    CHECK_EQ(layer->getTileDataString(40, "terrainTag"), std::string("3"));
    REQUIRE(layer->tileset()->terrainRules.size() == 1);
    CHECK_EQ(layer->tileset()->terrainRules[0].gid, 40);
    CHECK_EQ(layer->tileset()->terrainRules[0].neighborMask, 1 << 1);
    Pathfinder pathfinder(layer);
    CHECK(!pathfinder.isWalkable(0, 0));
    Map map;
    CHECK_EQ(map.publishCollision(layer), 1);
    CHECK_EQ(map.getCollisionRectWidth(0), 8.f);
    layer->setVisible(false);
}

TEST_CASE("map.tiled.flattensNestedGroupsWithInheritedPresentation") {
    const std::string json = R"({
      "width":2,"height":1,"tilewidth":16,"tileheight":16,
      "layers":[{"type":"group","offsetx":7,"offsety":9,"opacity":0.5,
        "layers":[{"type":"tilelayer","width":2,"height":1,"data":[1,0]}]}]
    })";
    std::string       error;
    auto              layers = loadMapText(json, nullptr, &error);
    CHECK_EQ(error, std::string());
    REQUIRE(layers.size() == 1);
    CHECK_EQ(layers[0]->getX(), 7.f);
    CHECK_EQ(layers[0]->getY(), 9.f);
    CHECK_EQ(layers[0]->draw()->tint.a, 0.5f);
}

TEST_CASE("map.import.failureRollsBackObservableLayerState") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(2, 1, 16.f, 16.f);
    layer->setTile(0, 0, 7);
    const int   revision = layer->getRevision();
    std::string error;
    CHECK(!applyConfigText(layer, R"({"width":4,"height":4,"data":[1]})", &error));
    CHECK(!error.empty());
    CHECK_EQ(layer->getMapWidth(), 2);
    CHECK_EQ(layer->getMapHeight(), 1);
    CHECK_EQ(layer->getTile(0, 0), 7);
    CHECK_EQ(layer->getRevision(), revision);
}

TEST_CASE("map.tiled.preservesAndRendersUnsignedFlipFlags") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(2, 1, 16.f, 16.f);
    layer->setLayer(999);
    CHECK(layer->applyConfig(R"({"width":2,"height":1,"data":[2147483649,536870913]})"));
    CHECK_EQ(tileGid(uint32_t(layer->getTile(0, 0))), uint32_t(1));
    std::vector<eve::graphics::DrawItem2D> items;
    TileRenderSystem::collect(items);
    items.erase(std::remove_if(items.begin(), items.end(), [](const auto &item) { return item.layer != 999; }),
                items.end());
    REQUIRE(items.size() == 2);
    const auto &horizontal = items[0];
    const auto &diagonal   = items[1];
    CHECK(horizontal.flipX);
    CHECK_EQ(horizontal.rotation, 0.f);
    CHECK_EQ(diagonal.rotation, 270.f);
    CHECK(diagonal.flipX);
}

TEST_CASE("map.render.nullSafe") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 8.f, 8.f);
    layer->fill(1);
    TileRenderSystem::render(nullptr);
}

TEST_CASE("map.projection.orthogonal") {
    TileLayer::Config cfg;
    cfg.mapW = 10;
    cfg.mapH = 10;
    cfg.tileW = 32.f;
    cfg.tileH = 32.f;
    cfg.originX = 10.f;
    cfg.originY = 20.f;
    cfg.orientation = MapOrientation::Orthogonal;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 2, 3, wx, wy);
    CHECK_EQ(wx, 10.f + 2 * 32.f);
    CHECK_EQ(wy, 20.f + 3 * 32.f);
    CHECK_EQ(tileToDepthY(cfg, 2, 3), 20.f + 4 * 32.f);
    int tx = -1, ty = -1;
    worldToTile(cfg, wx + 1.f, wy + 1.f, tx, ty);
    CHECK_EQ(tx, 2);
    CHECK_EQ(ty, 3);
}

TEST_CASE("map.projection.isometric") {
    TileLayer::Config cfg;
    cfg.tileW = 64.f;
    cfg.tileH = 32.f;
    cfg.originX = 0.f;
    cfg.originY = 0.f;
    cfg.orientation = MapOrientation::Isometric;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 1, 0, wx, wy);
    CHECK_EQ(wx, 32.f);
    CHECK_EQ(wy, 16.f);
    tileToWorld(cfg, 0, 1, wx, wy);
    CHECK_EQ(wx, -32.f);
    CHECK_EQ(wy, 16.f);
    CHECK_EQ(tileToDepthY(cfg, 1, 0), 16.f + 32.f);
}

TEST_CASE("map.projection.isometric.renderSpacingRoundTrip") {
    TileLayer::Config cfg;
    cfg.mapW = 8;
    cfg.mapH = 8;
    cfg.tileW = 64.f;
    cfg.tileH = 32.f;
    cfg.cellGapX = 16.f;
    cfg.cellGapY = 8.f;
    cfg.originX = 100.f;
    cfg.originY = 20.f;
    cfg.orientation = MapOrientation::Isometric;
    float wx = 0.f, wy = 0.f;
    tileToWorld(cfg, 3, 2, wx, wy);
    CHECK_EQ(wx, 140.f);
    CHECK_EQ(wy, 120.f);
    int tx = -1, ty = -1;
    worldToTile(cfg, wx, wy, tx, ty);
    CHECK_EQ(tx, 3);
    CHECK_EQ(ty, 2);
}

TEST_CASE("map.layer.renderSpacingApi") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 80.f, 40.f);
    layer->setRenderSpacing(1.25f, 1.5f);
    CHECK_EQ(layer->getCellGapX(), 20.f);
    CHECK_EQ(layer->getCellGapY(), 20.f);
    CHECK_EQ(layer->getRenderSpacingX(), 1.25f);
    CHECK_EQ(layer->getRenderSpacingY(), 1.5f);
}

TEST_CASE("map.projection.staggeredYOdd") {
    TileLayer::Config cfg;
    cfg.tileW = 64.f;
    cfg.tileH = 32.f;
    cfg.originX = 0.f;
    cfg.originY = 0.f;
    cfg.orientation = MapOrientation::Staggered;
    cfg.staggerAxis = StaggerAxis::Y;
    cfg.staggerIndex = StaggerIndex::Odd;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 0, 0, wx, wy);
    CHECK_EQ(wx, 0.f);
    CHECK_EQ(wy, 0.f);
    tileToWorld(cfg, 0, 1, wx, wy);
    CHECK_EQ(wx, 32.f);
    CHECK_EQ(wy, 16.f);
}

TEST_CASE("map.config.orientationIsometric") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 64.f, 32.f);
    const char *json = R"({
        "width": 2, "height": 2, "tilewidth": 64, "tileheight": 32,
        "orientation": "isometric",
        "data": [1, 0, 0, 1]
    })";
    CHECK(layer->applyConfig(json));
    CHECK_EQ(static_cast<int>(layer->config()->orientation),
             static_cast<int>(MapOrientation::Isometric));
}

TEST_CASE("map.config.unknownOrientationFails") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(1, 1, 8.f, 8.f);
    CHECK(!layer->applyConfig(R"({"width":1,"height":1,"orientation":"banana","data":[1]})"));
}

TEST_CASE("map.decode.zstdFails") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(1, 1, 8.f, 8.f);
    const char *json =
        R"({"width":1,"height":1,"tilewidth":8,"tileheight":8,
        "encoding":"base64","compression":"zstd","data":"AAAA"})";
    CHECK(!layer->applyConfig(json));
}

TEST_CASE("map.objects.parseObjectGroup") {
    const char *json = R"({
      "width": 2, "height": 1, "tilewidth": 16, "tileheight": 16,
      "layers": [
        { "type": "tilelayer", "width": 2, "height": 1, "data": [1, 0] },
        { "type": "objectgroup", "objects": [
            { "name": "spawn", "type": "player", "x": 8, "y": 24, "width": 16, "height": 16 }
        ]}
      ]
    })";
    std::vector<MapObject> objs;
    std::string err;
    auto layers = loadMapText(json, &objs, &err);
    CHECK(err.empty());
    REQUIRE(layers.size() == 1);
    REQUIRE(objs.size() == 1);
    CHECK_EQ(objs[0].name, "spawn");
    CHECK_EQ(objs[0].type, "player");
    CHECK_EQ(objs[0].x, 8.f);
    CHECK_EQ(objs[0].y, 24.f);
}

TEST_CASE("map.render.tilesContributeDepthY") {
    hideAllTileLayers();
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(1, 2, 32.f, 32.f);
    layer->config()->orientation = MapOrientation::Orthogonal;
    layer->setOrigin(0, 0);
    layer->setTile(0, 0, 1);
    layer->setTile(0, 1, 1);
    std::vector<eve::graphics::DrawItem2D> items;
    TileRenderSystem::collect(items);
    REQUIRE(items.size() == 2);
    eve::graphics::sortDrawItems2D(items);
    CHECK(items[0].depthY < items[1].depthY);
    layer->clear();
    layer->setVisible(false);
}

TEST_CASE("map.drawItem.sortByDepthY") {
    using eve::graphics::DrawItem2D;
    std::vector<DrawItem2D> items(2);
    items[0].layer = 0;
    items[0].depthY = 100.f;
    items[0].canvas = nullptr;
    items[1].layer = 0;
    items[1].depthY = 50.f;
    items[1].canvas = nullptr;
    eve::graphics::sortDrawItems2D(items);
    CHECK_EQ(items[0].depthY, 50.f);
    CHECK_EQ(items[1].depthY, 100.f);
}

TEST_CASE("map.projection.isometric.worldToTile") {
    TileLayer::Config cfg;
    cfg.tileW = 64.f;
    cfg.tileH = 32.f;
    cfg.originX = 0.f;
    cfg.originY = 0.f;
    cfg.orientation = MapOrientation::Isometric;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 1, 0, wx, wy);
    int tx = -1, ty = -1;
    worldToTile(cfg, wx, wy, tx, ty);
    CHECK_EQ(tx, 1);
    CHECK_EQ(ty, 0);
    tileToWorld(cfg, 0, 1, wx, wy);
    worldToTile(cfg, wx, wy, tx, ty);
    CHECK_EQ(tx, 0);
    CHECK_EQ(ty, 1);
}

TEST_CASE("map.projection.staggeredYOdd.worldToTileAndDepth") {
    TileLayer::Config cfg;
    cfg.mapW = 4;
    cfg.mapH = 4;
    cfg.tileW = 64.f;
    cfg.tileH = 32.f;
    cfg.originX = 0.f;
    cfg.originY = 0.f;
    cfg.orientation = MapOrientation::Staggered;
    cfg.staggerAxis = StaggerAxis::Y;
    cfg.staggerIndex = StaggerIndex::Odd;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 0, 1, wx, wy);
    CHECK_EQ(wx, 32.f);
    CHECK_EQ(wy, 16.f);
    CHECK_EQ(tileToDepthY(cfg, 0, 1), 16.f + 32.f);
    // Nearest tile center
    int tx = -1, ty = -1;
    worldToTile(cfg, wx + 32.f, wy + 16.f, tx, ty);
    CHECK_EQ(tx, 0);
    CHECK_EQ(ty, 1);
}

TEST_CASE("map.projection.staggeredXEven") {
    TileLayer::Config cfg;
    cfg.mapW = 3;
    cfg.mapH = 3;
    cfg.tileW = 64.f;
    cfg.tileH = 32.f;
    cfg.orientation = MapOrientation::Staggered;
    cfg.staggerAxis = StaggerAxis::X;
    cfg.staggerIndex = StaggerIndex::Even;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 0, 0, wx, wy);
    // major=tx=0 even → staggerIndex Even → shift (since !odd)
    CHECK_EQ(wx, 0.f);
    CHECK_EQ(wy, 16.f);
    tileToWorld(cfg, 1, 0, wx, wy);
    CHECK_EQ(wx, 32.f);
    CHECK_EQ(wy, 0.f);
}

TEST_CASE("map.projection.hexagonalYOdd") {
    TileLayer::Config cfg;
    cfg.mapW = 3;
    cfg.mapH = 3;
    cfg.tileW = 64.f;
    cfg.tileH = 32.f;
    cfg.hexSideLength = 16.f;
    cfg.orientation = MapOrientation::Hexagonal;
    cfg.staggerAxis = StaggerAxis::Y;
    cfg.staggerIndex = StaggerIndex::Odd;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 0, 0, wx, wy);
    CHECK_EQ(wx, 0.f);
    CHECK_EQ(wy, 0.f);
    // pitchY = (32+16)/2 = 24; row 1 odd → +tw/2
    tileToWorld(cfg, 0, 1, wx, wy);
    CHECK_EQ(wx, 32.f);
    CHECK_EQ(wy, 24.f);
    CHECK_EQ(tileToDepthY(cfg, 0, 1), 24.f + 32.f);
    int tx = -1, ty = -1;
    worldToTile(cfg, wx + 32.f, wy + 16.f, tx, ty);
    CHECK_EQ(tx, 0);
    CHECK_EQ(ty, 1);
}

TEST_CASE("map.decode.base64ZlibRoundTrip") {
    // 2x2 little-endian GIDs: 1,2,3,4
    const unsigned char raw[16] = {1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0};
    eve::data::CompressedData *cdata =
        eve::data::compress("zlib", reinterpret_cast<const char *>(raw), 16, -1);
    REQUIRE(cdata != nullptr);
    const char *compressed = reinterpret_cast<const char *>(cdata->getData());
    const size_t compressedSize = cdata->getSize();
    size_t b64len = 0;
    char *b64raw = eve::data::encode("base64", compressed, compressedSize, b64len);
    REQUIRE(b64raw != nullptr);
    REQUIRE(b64len > 0);
    std::string b64(b64raw, b64len);
    delete[] b64raw;

    std::string json = std::string(R"({
      "width": 2, "height": 2, "tilewidth": 16, "tileheight": 16,
      "layers": [{
        "type": "tilelayer", "width": 2, "height": 2,
        "encoding": "base64", "compression": "zlib",
        "data": ")") +
                       b64 + R"("}]})";

    std::string err;
    auto layers = loadMapText(json, nullptr, &err);
    CHECK(err.empty());
    REQUIRE(layers.size() == 1);
    CHECK_EQ(layers[0]->getTile(0, 0), 1);
    CHECK_EQ(layers[0]->getTile(1, 0), 2);
    CHECK_EQ(layers[0]->getTile(0, 1), 3);
    CHECK_EQ(layers[0]->getTile(1, 1), 4);
    delete cdata;
}

TEST_CASE("map.decode.base64Uncompressed") {
    const unsigned char raw[16] = {1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0};
    size_t b64len = 0;
    char *b64raw =
        eve::data::encode("base64", reinterpret_cast<const char *>(raw), 16, b64len);
    REQUIRE(b64raw != nullptr);
    std::string b64(b64raw, b64len);
    delete[] b64raw;

    std::string json = std::string(R"({
      "width": 2, "height": 2, "tilewidth": 16, "tileheight": 16,
      "encoding": "base64", "compression": "",
      "data": ")") +
                       b64 + R"("})";

    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 16.f, 16.f);
    CHECK(layer->applyConfig(json));
    CHECK_EQ(layer->getTile(0, 0), 1);
    CHECK_EQ(layer->getTile(1, 1), 4);
}

TEST_CASE("map.load.tiledLikeIsometricWithObjects") {
    const char *json = R"({
      "width": 2, "height": 2, "tilewidth": 64, "tileheight": 32,
      "orientation": "isometric",
      "layers": [
        { "type": "tilelayer", "width": 2, "height": 2, "data": [1, 0, 0, 2] },
        { "type": "objectgroup", "objects": [
            { "name": "npc", "type": "talker", "x": 10, "y": 20, "width": 8, "height": 8, "gid": 3 }
        ]}
      ]
    })";
    std::vector<MapObject> objs;
    std::string err;
    auto layers = loadMapText(json, &objs, &err);
    CHECK(err.empty());
    REQUIRE(layers.size() == 1);
    CHECK_EQ(static_cast<int>(layers[0]->config()->orientation),
             static_cast<int>(MapOrientation::Isometric));
    CHECK_EQ(layers[0]->getTile(0, 0), 1);
    CHECK_EQ(layers[0]->getTile(1, 1), 2);
    float wx = 0, wy = 0;
    layers[0]->tileToWorld(1, 0, wx, wy);
    CHECK_EQ(wx, 32.f);
    CHECK_EQ(wy, 16.f);
    REQUIRE(objs.size() == 1);
    CHECK_EQ(objs[0].name, "npc");
    CHECK_EQ(objs[0].type, "talker");
    CHECK_EQ(objs[0].gid, 3);
}

TEST_CASE("map.dualGrid.maskAndFrameTable") {
    CHECK_EQ(dualGridMaskFromCorners(false, false, false, false), 0);
    CHECK_EQ(dualGridMaskFromCorners(true, false, false, false), 1);
    CHECK_EQ(dualGridMaskFromCorners(false, true, false, false), 2);
    CHECK_EQ(dualGridMaskFromCorners(false, false, true, false), 4);
    CHECK_EQ(dualGridMaskFromCorners(false, false, false, true), 8);
    CHECK_EQ(dualGridMaskFromCorners(true, true, true, true), 15);
    // Diagonal TL+BR (classic dual-grid case that blob edge-masks miss).
    CHECK_EQ(dualGridMaskFromCorners(true, false, false, true), 9);
    CHECK_EQ(dualGridDefaultFrame(0), -1);
    CHECK_EQ(dualGridDefaultFrame(15), 6);
    CHECK_EQ(dualGridDefaultFrame(9), 4);
    CHECK_EQ(dualGridDefaultFrameTable()[1], 15);
}

TEST_CASE("map.dualGrid.resolveHalfOffsetAndSize") {
    auto *mod = Map::create();
    TileLayer *logic = mod->newLayer(2, 2, 32.f, 32.f);
    TileLayer *display = mod->newLayer(1, 1, 8.f, 8.f);
    logic->setOrigin(10.f, 20.f);
    logic->setTile(0, 0, 1);
    logic->setTile(1, 1, 1);

    DualGridOptions opts;
    opts.useDefaultFrameTable = false;  // gid = firstGid + mask
    opts.firstDisplayGid = 10;
    opts.hideLogic = true;
    std::string err;
    CHECK(resolveDualGrid(logic, display, opts, &err));
    CHECK(err.empty());
    CHECK_EQ(display->getMapWidth(), 3);
    CHECK_EQ(display->getMapHeight(), 3);
    CHECK_EQ(display->getTileWidth(), 32.f);
    CHECK_EQ(display->getX(), 10.f - 16.f);
    CHECK_EQ(display->getY(), 20.f - 16.f);
    CHECK(!logic->isVisible());
    CHECK(display->isVisible());

    // Display (1,1) samples logic (0,0),(1,0),(0,1),(1,1) → TL+BR = mask 9
    CHECK_EQ(dualGridMaskAt(*logic, 1, 1, 0), 9);
    CHECK_EQ(display->getTile(1, 1), 10 + 9);
    // Display (0,0) samples only out-of-bounds + logic(0,0) as BR → mask 8
    CHECK_EQ(dualGridMaskAt(*logic, 0, 0, 0), 8);
    CHECK_EQ(display->getTile(0, 0), 10 + 8);
    // Empty neighborhood
    CHECK_EQ(display->getTile(2, 0), 0);

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.resolveRejectsSameLayer") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 16.f, 16.f);
    std::string err;
    CHECK(!resolveDualGrid(layer, layer, DualGridOptions{}, &err));
    CHECK(!err.empty());
    CHECK(!mod->resolveDualGrid(nullptr, layer));
    CHECK(!mod->lastDualGridError().empty());
    layer->clear();
    layer->setVisible(false);
}

TEST_CASE("map.dualGrid.filledGidFilter") {
    auto *mod = Map::create();
    TileLayer *logic = mod->newLayer(1, 1, 16.f, 16.f);
    TileLayer *display = mod->newLayer(1, 1, 16.f, 16.f);
    logic->setTile(0, 0, 5);
    DualGridOptions opts;
    opts.filledGid = 9;  // 5 should not count
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid = 1;
    opts.hideLogic = false;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    // All display cells empty when filled filter mismatches
    CHECK_EQ(display->getTile(0, 0), 0);
    CHECK_EQ(display->getTile(1, 0), 0);
    CHECK_EQ(display->getTile(0, 1), 0);
    CHECK_EQ(display->getTile(1, 1), 0);
    CHECK(logic->isVisible());

    CHECK(mod->resolveDualGridFilled(logic, display, 5));
    // logic(0,0) is BR of display(0,0) and TL of display(1,1).
    CHECK_EQ(dualGridMaskAt(*logic, 0, 0, 5), 8);
    CHECK_EQ(dualGridMaskAt(*logic, 1, 1, 5), 1);
    CHECK_EQ(display->getTile(0, 0), 1 + dualGridDefaultFrame(8));
    CHECK_EQ(display->getTile(1, 1), 1 + dualGridDefaultFrame(1));

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.halfOffset.orthogonalIsoStaggerHex") {
    TileLayer::Config ortho;
    ortho.orientation = MapOrientation::Orthogonal;
    ortho.tileW = 32.f;
    ortho.tileH = 32.f;
    float ox = 0, oy = 0;
    dualGridHalfOffset(ortho, ox, oy);
    CHECK_EQ(ox, -16.f);
    CHECK_EQ(oy, -16.f);

    TileLayer::Config iso;
    iso.orientation = MapOrientation::Isometric;
    iso.tileW = 64.f;
    iso.tileH = 32.f;
    dualGridHalfOffset(iso, ox, oy);
    CHECK_EQ(ox, 0.f);
    CHECK_EQ(oy, -16.f);

    TileLayer::Config stagY;
    stagY.orientation = MapOrientation::Staggered;
    stagY.staggerAxis = StaggerAxis::Y;
    stagY.tileW = 64.f;
    stagY.tileH = 32.f;
    dualGridHalfOffset(stagY, ox, oy);
    CHECK_EQ(ox, -32.f);
    CHECK_EQ(oy, -8.f);  // pitchY = th/2 = 16 → half = 8

    TileLayer::Config stagX;
    stagX.orientation = MapOrientation::Staggered;
    stagX.staggerAxis = StaggerAxis::X;
    stagX.tileW = 64.f;
    stagX.tileH = 32.f;
    dualGridHalfOffset(stagX, ox, oy);
    CHECK_EQ(ox, -16.f);  // pitchX = tw/2 = 32 → half = 16
    CHECK_EQ(oy, -16.f);

    TileLayer::Config hexY;
    hexY.orientation = MapOrientation::Hexagonal;
    hexY.staggerAxis = StaggerAxis::Y;
    hexY.tileW = 64.f;
    hexY.tileH = 32.f;
    hexY.hexSideLength = 16.f;
    dualGridHalfOffset(hexY, ox, oy);
    // pitchY = (32+16)/2 = 24 → half = 12
    CHECK_EQ(ox, -32.f);
    CHECK_EQ(oy, -12.f);

    TileLayer::Config hexX;
    hexX.orientation = MapOrientation::Hexagonal;
    hexX.staggerAxis = StaggerAxis::X;
    hexX.tileW = 64.f;
    hexX.tileH = 32.f;
    hexX.hexSideLength = 16.f;
    dualGridHalfOffset(hexX, ox, oy);
    // pitchX = (64+16)/2 = 40 → half = 20
    CHECK_EQ(ox, -20.f);
    CHECK_EQ(oy, -16.f);
}

TEST_CASE("map.dualGrid.resolve.isometric") {
    auto *mod = Map::create();
    TileLayer *logic = mod->newLayer(2, 2, 64.f, 32.f);
    TileLayer *display = mod->newLayer(1, 1, 8.f, 8.f);
    logic->config()->orientation = MapOrientation::Isometric;
    logic->setOrigin(100.f, 200.f);
    logic->setTile(0, 0, 1);
    logic->setTile(1, 1, 1);

    DualGridOptions opts;
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid = 1;
    opts.hideLogic = true;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(static_cast<int>(display->config()->orientation),
             static_cast<int>(MapOrientation::Isometric));
    CHECK_EQ(display->getMapWidth(), 3);
    CHECK_EQ(display->getMapHeight(), 3);
    CHECK_EQ(display->getX(), 100.f);          // iso: offX = 0
    CHECK_EQ(display->getY(), 200.f - 16.f);   // offY = -tileH/2
    CHECK_EQ(mod->dualGridOffsetX(logic), 0.f);
    CHECK_EQ(mod->dualGridOffsetY(logic), -16.f);

    // Index-space mask unchanged from orthogonal dual-grid.
    CHECK_EQ(dualGridMaskAt(*logic, 1, 1, 0), 9);
    CHECK_EQ(display->getTile(1, 1), 1 + 9);

    // display(1,0) == logic fractional (0.5, -0.5) under iso projection:
    // wx = ox + (1-0)*tw/2 = ox+32, wy = (oy-th/2) + (1+0)*th/2 = oy
    CHECK_EQ(display->tileToWorldX(1, 0), 100.f + 32.f);
    CHECK_EQ(display->tileToWorldY(1, 0), 200.f);

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.resolve.staggeredYOdd") {
    auto *mod = Map::create();
    TileLayer *logic = mod->newLayer(3, 2, 64.f, 32.f);
    TileLayer *display = mod->newLayer(1, 1, 8.f, 8.f);
    {
        auto lc = logic->config();
        lc->orientation = MapOrientation::Staggered;
        lc->staggerAxis = StaggerAxis::Y;
        lc->staggerIndex = StaggerIndex::Odd;
    }
    logic->setOrigin(0.f, 0.f);
    logic->fill(1);

    DualGridOptions opts;
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid = 20;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(static_cast<int>(display->config()->orientation),
             static_cast<int>(MapOrientation::Staggered));
    CHECK_EQ(static_cast<int>(display->config()->staggerAxis), static_cast<int>(StaggerAxis::Y));
    CHECK_EQ(static_cast<int>(display->config()->staggerIndex),
             static_cast<int>(StaggerIndex::Odd));
    CHECK_EQ(display->getMapWidth(), 4);
    CHECK_EQ(display->getMapHeight(), 3);
    CHECK_EQ(display->getX(), -32.f);
    CHECK_EQ(display->getY(), -8.f);

    // Interior display(1,1) sees four filled logic cells → mask 15
    CHECK_EQ(dualGridMaskAt(*logic, 1, 1, 0), 15);
    CHECK_EQ(display->getTile(1, 1), 20 + 15);
    // Corner display(0,0) only BR → mask 8
    CHECK_EQ(display->getTile(0, 0), 20 + 8);

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.resolve.staggeredXEven") {
    auto *mod = Map::create();
    TileLayer *logic = mod->newLayer(2, 3, 64.f, 32.f);
    TileLayer *display = mod->newLayer(1, 1, 8.f, 8.f);
    {
        auto lc = logic->config();
        lc->orientation = MapOrientation::Staggered;
        lc->staggerAxis = StaggerAxis::X;
        lc->staggerIndex = StaggerIndex::Even;
    }
    logic->setOrigin(10.f, 20.f);
    logic->setTile(0, 1, 1);
    logic->setTile(1, 1, 1);

    DualGridOptions opts;
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid = 1;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(static_cast<int>(display->config()->staggerAxis), static_cast<int>(StaggerAxis::X));
    CHECK_EQ(static_cast<int>(display->config()->staggerIndex),
             static_cast<int>(StaggerIndex::Even));
    CHECK_EQ(display->getX(), 10.f - 16.f);
    CHECK_EQ(display->getY(), 20.f - 16.f);
    // display(1,2) samples logic (0,1),(1,1),(0,2),(1,2) → TL+TR = mask 3
    CHECK_EQ(dualGridMaskAt(*logic, 1, 2, 0), 3);
    CHECK_EQ(display->getTile(1, 2), 1 + 3);

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.resolve.hexagonal") {
    auto *mod = Map::create();
    TileLayer *logic = mod->newLayer(2, 2, 64.f, 32.f);
    TileLayer *display = mod->newLayer(1, 1, 8.f, 8.f);
    {
        auto lc = logic->config();
        lc->orientation = MapOrientation::Hexagonal;
        lc->staggerAxis = StaggerAxis::Y;
        lc->staggerIndex = StaggerIndex::Odd;
        lc->hexSideLength = 16.f;
    }
    logic->setOrigin(5.f, 7.f);
    logic->setTile(0, 0, 1);

    DualGridOptions opts;
    opts.useDefaultFrameTable = true;
    opts.firstDisplayGid = 1;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(static_cast<int>(display->config()->orientation),
             static_cast<int>(MapOrientation::Hexagonal));
    CHECK_EQ(display->config()->hexSideLength, 16.f);
    CHECK_EQ(display->getX(), 5.f - 32.f);
    CHECK_EQ(display->getY(), 7.f - 12.f);  // pitchY=24 → half=12
    CHECK_EQ(display->getTile(0, 0), 1 + dualGridDefaultFrame(8));
    CHECK_EQ(display->getTile(1, 1), 1 + dualGridDefaultFrame(1));

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.flipFlagsCountAsFilled") {
    auto *mod = Map::create();
    TileLayer *logic = mod->newLayer(1, 1, 16.f, 16.f);
    TileLayer *display = mod->newLayer(1, 1, 16.f, 16.f);
    // Tiled horizontal flip flag on GID 1
    logic->setTile(0, 0, int(0x80000001u));
    DualGridOptions opts;
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid = 1;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(dualGridMaskAt(*logic, 0, 0, 0), 8);
    CHECK_EQ(display->getTile(0, 0), 1 + 8);

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.noHalfOffsetKeepsOrigin") {
    auto *mod = Map::create();
    TileLayer *logic = mod->newLayer(2, 1, 32.f, 32.f);
    TileLayer *display = mod->newLayer(1, 1, 8.f, 8.f);
    logic->config()->orientation = MapOrientation::Isometric;
    logic->setOrigin(40.f, 50.f);
    logic->setTile(0, 0, 1);
    DualGridOptions opts;
    opts.applyHalfOffset = false;
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid = 1;
    opts.hideLogic = false;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(display->getX(), 40.f);
    CHECK_EQ(display->getY(), 50.f);
    CHECK_EQ(static_cast<int>(display->config()->orientation),
             static_cast<int>(MapOrientation::Isometric));
    CHECK(logic->isVisible());

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.render.isometricDisplayDepth") {
    hideAllTileLayers();
    auto *mod = Map::create();
    TileLayer *logic = mod->newLayer(2, 2, 64.f, 32.f);
    TileLayer *display = mod->newLayer(1, 1, 64.f, 32.f);
    logic->config()->orientation = MapOrientation::Isometric;
    logic->setOrigin(0.f, 0.f);
    logic->fill(1);
    DualGridOptions opts;
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid = 1;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));

    std::vector<eve::graphics::DrawItem2D> items;
    TileRenderSystem::collect(items);
    // Interior + border dual cells that are non-empty on a full 2x2 logic map:
    // display 3x3, only mask0 empty — all 9 have some contribution from filled
    // neighbors except none are fully empty when logic is full... actually edges
    // still have non-zero masks. All 9 display cells should be non-empty.
    CHECK_EQ(items.size(), 9u);
    eve::graphics::sortDrawItems2D(items);
    for (size_t i = 1; i < items.size(); ++i) {
        CHECK(items[i - 1].depthY <= items[i].depthY);
    }

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.render.orthoAndIsoPreview") {
    hideAllTileLayers();

    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = 640;
    s.height = 400;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto *mod = Map::create();

    // Left: orthogonal concentric rings (solidForGid colors, no atlas needed).
    TileLayer *ortho = mod->newLayer(10, 8, 28.f, 28.f);
    ortho->config()->orientation = MapOrientation::Orthogonal;
    ortho->setOrigin(40.f, 40.f);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 10; ++x) {
            const int ring = std::min({x, y, 9 - x, 7 - y});
            ortho->setTile(x, y, uint32_t(1 + ring));
        }
    }
    ortho->setVisible(true);

    // Right: isometric diamond so projection is visibly different.
    TileLayer *iso = mod->newLayer(6, 6, 48.f, 24.f);
    iso->config()->orientation = MapOrientation::Isometric;
    iso->setOrigin(420.f, 80.f);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            iso->setTile(x, y, uint32_t(3 + ((x + y) % 4)));
        }
    }
    iso->setVisible(true);

    std::vector<eve::graphics::DrawItem2D> items;
    TileRenderSystem::collect(items);
    REQUIRE(items.size() == 10u * 8u + 6u * 6u);

    gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.12f, 1.f);
    for (int frame = 0; frame < 75; ++frame) {
        ortho->setOrigin(40.f + float(frame) * 0.15f, 40.f);
        iso->setOrigin(420.f, 80.f + float(std::sin(float(frame) * 0.08f)) * 6.f);

        gfx->clearScreen();
        mod->render(gfx);
        gfx->present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    ortho->setVisible(false);
    iso->setVisible(false);
    win->close();
}
