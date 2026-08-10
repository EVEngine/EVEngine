#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "map/Map.h"
#include "map/TileLayer.h"
#include "map/TileSystem.h"
#include "map/TileConfig.h"
#include "map/TileProjection.h"
#include "map/DualGrid.h"
#include "map/MapObject.h"
#include "graphics/DrawItem2D.h"
#include "data/DataModule.h"

#include <string>
#include <vector>

using namespace eve::map;

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
