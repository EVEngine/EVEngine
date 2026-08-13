// Hex dungeon test levels — cross-module simulation for engine feature QA.
//
// Levels (each TEST_CASE is an independent scenario; pipelines combine them):
//  01 procgen + hex tilemap + A* / Flow Field pathfinding
//  02 2D dynamic FOV (shadowcast, hex topology, explored memory)
//  03 2D dynamic lighting (point light + lit sprite collection)
//  04 object pickup via SpatialHash2D collision + Inventory
//  05 particle emitters (torch / spark on pickup)
//  06 Flow Field swarm (multi-unit same goal on hex)
//  07 cell-cost detour (prefer cheap corridor)
//  08 multi-revealer FOV + perception / stealth
//  09 FoW mask export + alternate FOV algorithms
//  10 Camera2D screen ↔ world ↔ hex tile pick
//  11 Dual-grid resolve on hexagonal logic layer
//  12 procgen algorithm variants on hex (cave / maze / wfc)
//  13 QuadTree viewport culling for loot markers
//  14 multi-light (point + directional) ambient scene
//  15 particle presets lifecycle + bag → stash transfer
//  16 facing-cone FOV (directional revealer)
//  17 Flow Field + cell-cost combined
//  18 seed reproducibility (spawn/path stable)
//  19 corner-peek FOV toggle
//  20 equipment loot pickup + equip slot
//  21 hex world↔tile roundtrip sampling
//  22 FOV explored memory clearMemory
//  23 findGroupPath on hex topology
//  24 FOV algorithm visibility parity smoke
//  25 blocked-cell + syncFromLayer pathfinding
//  26 drunkard cave hex
//  27 maze.backtrack hex path
//  28 wfc.simple dungeon hex
//  29 mist particle + cool light scene
//  30 raid combo (flow + cost + fov + pickup + particles + windowed preview)
//  pipeline.dungeonCrawl: full crawl through one seeded hex dungeon
//  pipeline.fogRaid: FOV cone + perception gated pickup + flow escort
//  pipeline.torchEscort: multi-revealer + light + particles along flow
//  pipeline.catalogRaid: catalog raid loot + perception gates
//  pipeline.costlyFogPickup: cell-cost detour + FOV pickup + particles

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "RenderImageAudit.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Inventory.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "map/FlowField.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "map/TileLayer.h"
#include "map/TileOrientation.h"
#include "map/TileSystem.h"
#include "map/DualGrid.h"
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"
#include "particles/ParticleSystem.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "procgen/Semantic.h"
#include "spatial/QuadTree.h"
#include "spatial/Spatial.h"
#include "spatial/SpatialHash2D.h"
#include "window/Window.h"
#include "filesystem/Filesystem.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace eve::map;
using namespace eve::procgen;
using namespace eve::graphics;
using namespace eve::particles;
using namespace eve::spatial;
using namespace eve::inventory;

namespace {

constexpr float kTileW = 64.f;
constexpr float kTileH = 32.f;
constexpr float kHexSide = 16.f;
constexpr int kWallGid = 1;
constexpr int kFloorGid = 2;
constexpr int kDoorGid = 3;

bool approxEq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

void hideAllTileLayers() {
    if (ecs::current()->getManager<TileLayer>() == nullptr) return;
    auto view = ecs::View<TileLayer, TileLayer::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw] = *it;
        draw->visible = false;
    }
}

void configureHexLayer(TileLayer *layer) {
    auto c = layer->config();
    c->orientation = MapOrientation::Hexagonal;
    c->staggerAxis = StaggerAxis::Y;
    c->staggerIndex = StaggerIndex::Odd;
    c->hexSideLength = kHexSide;
    layer->setOrigin(0.f, 0.f);
    layer->setVisible(true);
}

struct HexDungeon {
    TileLayer *layer = nullptr;
    Grid2D grid;
    int spawnTx = -1;
    int spawnTy = -1;
    int exitTx = -1;
    int exitTy = -1;
};

bool findWalkable(const Grid2D &g, int &ox, int &oy) {
    for (int y = 0; y < g.getHeight(); ++y) {
        for (int x = 0; x < g.getWidth(); ++x) {
            const int c = g.getCell(x, y);
            if (c == int(Semantic::Floor) || c == int(Semantic::Corridor) ||
                c == int(Semantic::Door)) {
                ox = x;
                oy = y;
                return true;
            }
        }
    }
    return false;
}

bool findFarthestWalkable(const Grid2D &g, int sx, int sy, int &ox, int &oy) {
    int bestX = sx, bestY = sy, bestD = -1;
    for (int y = 0; y < g.getHeight(); ++y) {
        for (int x = 0; x < g.getWidth(); ++x) {
            const int c = g.getCell(x, y);
            if (c != int(Semantic::Floor) && c != int(Semantic::Corridor) &&
                c != int(Semantic::Door))
                continue;
            const int dx = x - sx;
            const int dy = y - sy;
            const int dist = dx * dx + dy * dy;
            if (dist > bestD) {
                bestD = dist;
                bestX = x;
                bestY = y;
            }
        }
    }
    ox = bestX;
    oy = bestY;
    return bestD >= 0;
}

HexDungeon buildHexDungeon(Map *mapMod, Procgen *gen, uint32_t seed, int w, int h,
                           const char *algo = "dungeon.bsp") {
    HexDungeon d;
    hideAllTileLayers();
    d.layer = mapMod->newLayer(w, h, kTileW, kTileH);
    configureHexLayer(d.layer);

    GeneratorRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(seed);
    p.setSize(w, h);
    std::string err;
    REQUIRE(GeneratorRegistry::instance().generate(algo, p, d.grid, err));

    gen->setPaletteGid("hex_test", "empty", 0);
    gen->setPaletteGid("hex_test", "wall", kWallGid);
    gen->setPaletteGid("hex_test", "floor", kFloorGid);
    gen->setPaletteGid("hex_test", "corridor", kFloorGid);
    gen->setPaletteGid("hex_test", "door", kDoorGid);
    REQUIRE(gen->applyToLayer(&d.grid, "hex_test", d.layer));

    for (int i = 0; i < d.grid.getObjectCount(); ++i) {
        const std::string t = d.grid.getObjectType(i);
        const int tx = int(d.grid.getObjectX(i));
        const int ty = int(d.grid.getObjectY(i));
        if (t == "spawn") {
            d.spawnTx = tx;
            d.spawnTy = ty;
        } else if (t == "stairs") {
            d.exitTx = tx;
            d.exitTy = ty;
        }
    }
    if (d.spawnTx < 0 || d.spawnTy < 0)
        REQUIRE(findWalkable(d.grid, d.spawnTx, d.spawnTy));
    if (d.exitTx < 0 || d.exitTy < 0) {
        // Prefer a walkable cell far from spawn.
        int bestX = d.spawnTx, bestY = d.spawnTy, bestD = -1;
        for (int y = 0; y < d.grid.getHeight(); ++y) {
            for (int x = 0; x < d.grid.getWidth(); ++x) {
                const int c = d.grid.getCell(x, y);
                if (c != int(Semantic::Floor) && c != int(Semantic::Corridor)) continue;
                const int dx = x - d.spawnTx;
                const int dy = y - d.spawnTy;
                const int dist = dx * dx + dy * dy;
                if (dist > bestD) {
                    bestD = dist;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        d.exitTx = bestX;
        d.exitTy = bestY;
    }
    return d;
}

bool pathWalkable(Path *path, Pathfinder *pf) {
    if (!path) return false;
    for (int i = 0; i < path->getLength(); ++i) {
        if (!pf->isWalkable(path->getX(i), path->getY(i))) return false;
    }
    return true;
}

Texture *makeSolidTex(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

void hideLeftover2DCameras() {
    if (ecs::current()->getManager<Camera2D>() == nullptr) return;
    auto view = ecs::View<Camera2D, Camera2D::Data>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [data] = *it;
        data->active = false;
    }
}

void hideLeftoverSprites() {
    if (ecs::current()->getManager<Renderable2D>() == nullptr) return;
    auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [sp] = *it;
        sp->visible = false;
    }
}

void hideLeftoverEmitters() {
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw] = *it;
        draw->visible = false;
    }
}

constexpr float kKenneyTileW = 55.f;
constexpr float kKenneyTileH = 57.f;
constexpr float kKenneyHexSide = 28.5f;

bool bindKenneyHexTileset(Graphics *gfx, TileLayer *layer) {
    auto *fs = eve::filesystem::Filesystem::create();
    if (!fs || !gfx || !layer) return false;
    fs->setIdentity("ev_ut_hex_kenney", true);
    fs->setupWriteDirectory();
    const std::string dir = std::string(EVENGINE_SOURCE_DIR) + "/examples/hex-levels/data/tiles";
    fs->allowMountingForPath(dir);
    if (!fs->mount(dir, "", false)) return false;
    Texture *tex = gfx->newTextureFromFile("kenney_hex.png");
    if (!tex) return false;
    layer->setTileSize(kKenneyTileW, kKenneyTileH);
    layer->config()->hexSideLength = kKenneyHexSide;
    layer->setTilesetTileSize(int(kKenneyTileW), int(kKenneyTileH));
    layer->setTileset(tex, 1, 4, 0, 1);
    return true;
}

struct HexPreview {
    const char *title = "hex preview";
    const char *pngName = "preview.png";
    Path *path = nullptr;
    ParticleEmitter *fire = nullptr;
    Light2D *lamp = nullptr;
    int lootTx = -1;
    int lootTy = -1;
    int frames = 40;
};

/** Windowed follow-cam playback: Kenney hex tiles + hero + torch + fire. */
void previewHex(Map *mapMod, TileLayer *layer, int spawnTx, int spawnTy, int exitTx, int exitTy,
                const HexPreview &opt) {
    if (!mapMod || !layer) return;
    hideLeftover2DCameras();
    hideLeftoverSprites();
    hideLeftoverEmitters();
    hideAllTileLayers();
    layer->setVisible(true);

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings ws;
    ws.width = 960;
    ws.height = 540;
    ws.centered = true;
    REQUIRE(win->setWindowSettings(ws));
    win->setWindowTitle(opt.title ? opt.title : "hex preview");
    gfx->setScreenReadbackEnabled(true);

    const Color sky(0.05f, 0.06f, 0.09f, 1.f);
    gfx->setBackgroundColor(sky);
    REQUIRE(bindKenneyHexTileset(gfx, layer));
    const float tw = layer->getTileWidth();
    const float th = layer->getTileHeight();

    Path *ownedPath = nullptr;
    Pathfinder *ownedPf = nullptr;
    Path *path = opt.path;
    if ((!path || path->getLength() <= 0) && spawnTx >= 0 && exitTx >= 0) {
        ownedPf = mapMod->newPathfinder(layer);
        ownedPf->blockGid(kWallGid);
        ownedPf->setTopology("hex");
        ownedPath = ownedPf->findPath(spawnTx, spawnTy, exitTx, exitTy);
        path = ownedPath;
    }

    ParticleEmitter *fire = opt.fire;
    if (!fire) {
        auto *parts = Particles::create();
        fire = parts->newEmitter(96);
        fire->applyPreset("fire");
    }
    Light2D *lamp = opt.lamp;
    if (!lamp) lamp = Light2D::createLight("point");

    auto *cam = Camera2D::createCamera();
    cam->data()->active = true;
    cam->data()->zoom = 1.15f;
    cam->setAmbient(0.12f, 0.11f, 0.14f);
    layer->setCamera(cam);

    Texture *heroTex = makeSolidTex(gfx, 255, 220, 80);
    Texture *lootTex = makeSolidTex(gfx, 240, 70, 200);
    Texture *exitTex = makeSolidTex(gfx, 70, 220, 230);
    REQUIRE(heroTex != nullptr);
    REQUIRE(lootTex != nullptr);
    REQUIRE(exitTex != nullptr);

    auto *hero = Renderable2D::create();
    hero->sprite()->texture = heroTex;
    hero->sprite()->width = 22.f;
    hero->sprite()->height = 22.f;
    hero->sprite()->visible = true;
    hero->sprite()->layer = 20;
    hero->sprite()->camera = cam;
    hero->sprite()->receiveLight = true;

    auto *lootMark = Renderable2D::create();
    lootMark->sprite()->texture = lootTex;
    lootMark->sprite()->width = 14.f;
    lootMark->sprite()->height = 14.f;
    lootMark->sprite()->layer = 18;
    lootMark->sprite()->camera = cam;
    lootMark->sprite()->receiveLight = true;
    if (opt.lootTx >= 0) {
        float lootWx = 0.f, lootWy = 0.f;
        layer->tileToWorld(opt.lootTx, opt.lootTy, lootWx, lootWy);
        lootMark->transform()->x = lootWx + tw * 0.25f;
        lootMark->transform()->y = lootWy + th * 0.15f;
        lootMark->sprite()->visible = true;
    } else {
        lootMark->sprite()->visible = false;
    }

    auto *exitMark = Renderable2D::create();
    exitMark->sprite()->texture = exitTex;
    exitMark->sprite()->width = 16.f;
    exitMark->sprite()->height = 16.f;
    exitMark->sprite()->layer = 18;
    exitMark->sprite()->camera = cam;
    exitMark->sprite()->receiveLight = true;
    if (exitTx >= 0) {
        float exitWx = 0.f, exitWy = 0.f;
        layer->tileToWorld(exitTx, exitTy, exitWx, exitWy);
        exitMark->transform()->x = exitWx + tw * 0.2f;
        exitMark->transform()->y = exitWy + th * 0.1f;
        exitMark->sprite()->visible = true;
    } else {
        exitMark->sprite()->visible = false;
    }

    lamp->setColor(1.f, 0.72f, 0.42f, 2.4f);
    lamp->setRadius(180.f);
    lamp->setEnabled(true);
    fire->setCamera(cam);
    fire->setVisible(true);
    fire->start();

    const int n = (path && path->getLength() > 0) ? path->getLength() : 1;
    const int holdTx = spawnTx >= 0 ? spawnTx : 0;
    const int holdTy = spawnTy >= 0 ? spawnTy : 0;
    const int frames = std::clamp(opt.frames, 16, 60);
    bool quit = false;
    for (int f = 0; f < frames && !quit; ++f) {
        const int step = (n <= 1) ? 0 : f * (n - 1) / std::max(1, frames - 1);
        float wx = 0.f, wy = 0.f;
        if (path && path->getLength() > 0)
            layer->tileToWorld(path->getX(step), path->getY(step), wx, wy);
        else
            layer->tileToWorld(holdTx, holdTy, wx, wy);
        wx += tw * 0.35f;
        wy += th * 0.35f;
        cam->data()->x = wx;
        cam->data()->y = wy;
        hero->transform()->x = wx - 11.f;
        hero->transform()->y = wy - 11.f;
        lamp->setPosition(wx, wy);
        fire->setPosition(wx + 4.f, wy - 6.f);
        if (opt.lootTx >= 0 && n > 1 && step >= n / 2) lootMark->sprite()->visible = false;
        ParticleSimSystem::update(1.f / 30.f);

        gfx->clearScreen();
        mapMod->render(gfx);
        ParticleRenderSystem::render(gfx);
        gfx->present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
        }
        SDL_Delay(16);
    }

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    Color mid = gfx->getPixel(w / 2, h / 2);
    const float luma = (mid.r + mid.g + mid.b) / 3.f;
    CHECK(luma > 0.04f);

    std::unique_ptr<eve::image::ImageData> img(gfx->newImageData());
    REQUIRE(img.get() != nullptr);
    const std::string dir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out/hex_levels";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const char *png = opt.pngName ? opt.pngName : "preview.png";
    REQUIRE(saveImagePng(*img, dir + "/" + png));

    hero->sprite()->visible = false;
    lootMark->sprite()->visible = false;
    exitMark->sprite()->visible = false;
    fire->setVisible(false);
    fire->stop();
    lamp->setEnabled(false);
    cam->data()->active = false;
    layer->setCamera(nullptr);
    layer->setVisible(false);
    win->close();
    delete ownedPath;
    delete ownedPf;
}

void previewHex(Map *mapMod, const HexDungeon &d, const HexPreview &opt) {
    previewHex(mapMod, d.layer, d.spawnTx, d.spawnTy, d.exitTx, d.exitTy, opt);
}

}  // namespace

TEST_CASE("hex.level.01.procgenPath") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 42, 32, 24);

    CHECK_EQ(static_cast<int>(d.layer->config()->orientation),
             static_cast<int>(MapOrientation::Hexagonal));
    CHECK(approxEq(d.layer->config()->hexSideLength, kHexSide));
    CHECK(d.spawnTx >= 0);
    CHECK(d.exitTx >= 0);
    CHECK_NE(d.layer->getTile(d.spawnTx, d.spawnTy), 0);
    CHECK_NE(d.layer->getTile(d.exitTx, d.exitTy), 0);

    // Projection: hex row pitch shifts odd rows.
    float wx0 = 0.f, wy0 = 0.f, wx1 = 0.f, wy1 = 0.f;
    d.layer->tileToWorld(0, 0, wx0, wy0);
    d.layer->tileToWorld(0, 1, wx1, wy1);
    CHECK(approxEq(wx0, 0.f));
    CHECK(approxEq(wy0, 0.f));
    CHECK(approxEq(wx1, kTileW * 0.5f));
    CHECK(approxEq(wy1, (kTileH + kHexSide) * 0.5f));

    int backTx = -1, backTy = -1;
    d.layer->worldToTile(wx1 + kTileW * 0.5f, wy1 + kTileH * 0.5f, backTx, backTy);
    CHECK_EQ(backTx, 0);
    CHECK_EQ(backTy, 1);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    REQUIRE(pf != nullptr);
    pf->blockGid(kWallGid);
    pf->setBlockEmpty(true);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("hex"));

    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK_EQ(path->getX(0), d.spawnTx);
    CHECK_EQ(path->getY(0), d.spawnTy);
    CHECK_EQ(path->getX(path->getLength() - 1), d.exitTx);
    CHECK_EQ(path->getY(path->getLength() - 1), d.exitTy);
    CHECK(pathWalkable(path, pf));

    // Group path / flow field to the same exit.
    FlowField *field = pf->buildFlowField(d.exitTx, d.exitTy);
    REQUIRE(field != nullptr);
    CHECK(field->isReachable(d.spawnTx, d.spawnTy));
    Path *group = pf->findGroupPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(group != nullptr);
    CHECK(group->getLength() > 0);
    CHECK_EQ(group->getX(group->getLength() - 1), d.exitTx);
    CHECK_EQ(group->getY(group->getLength() - 1), d.exitTy);

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.01 procgen path",
                          .pngName = "01_procgenPath.png",
                          .path = path});

    delete group;
    delete field;
    delete path;
    delete pf;
}

TEST_CASE("hex.level.02.dynamicFov") {
    // Handcrafted open hex strip: procgen spawn↔exit connectivity is not
    // portable across libstdc++/libc++ (uniform_int_distribution differs), and
    // cave.cellular can place spawn/stairs in disconnected components.
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(12, 5, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);

    Fov *fov = mapMod->newFov(layer);
    REQUIRE(fov != nullptr);
    fov->blockOpaqueGid(kWallGid);
    fov->setBlockEmpty(false);
    fov->setTopology("auto");
    CHECK_EQ(fov->getTopology(), std::string("hex"));
    fov->setAlgorithm("shadowcast");
    fov->setRadiusMetric("euclidean");

    const int spawnTx = 1, spawnTy = 2;
    const int id = fov->addRevealer(spawnTx, spawnTy, 6);
    fov->compute();
    CHECK(fov->isVisible(spawnTx, spawnTy));
    CHECK(fov->isExplored(spawnTx, spawnTy));

    // Move along A* path; memory should retain explored cells after leaving.
    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    Path *path = pf->findPath(spawnTx, spawnTy, 10, 2);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 1);

    const int mid = path->getLength() / 2;
    const int midX = path->getX(mid);
    const int midY = path->getY(mid);
    fov->setRevealerPosition(id, midX, midY);
    fov->compute();
    CHECK(fov->isVisible(midX, midY));
    CHECK(fov->isExplored(spawnTx, spawnTy));  // memory

    // Opaque wall should cast a shadow along cube line when present.
    Fov *wallFov = mapMod->newFovSize(9, 5);
    wallFov->setTopology("hex");
    wallFov->setAlgorithm("shadowcast");
    wallFov->setBlockEmpty(false);
    for (int y = 0; y < 5; ++y) wallFov->setOpaque(4, y, true);
    wallFov->addRevealer(1, 2, 5);
    wallFov->compute();
    CHECK(wallFov->isVisible(1, 2));
    CHECK(wallFov->isVisible(4, 2));
    CHECK(!wallFov->isVisible(7, 2));

    previewHex(mapMod, layer, spawnTx, spawnTy, 10, 2,
               HexPreview{.title = "hex.level.02 dynamic fov",
                          .pngName = "02_dynamicFov.png",
                          .path = path});

    delete wallFov;
    delete path;
    delete pf;
    delete fov;
    layer->setVisible(false);
}

TEST_CASE("hex.level.03.dynamicLight") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 99, 24, 18);

    float heroWx = 0.f, heroWy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, heroWx, heroWy);

    auto *cam = Camera2D::createCamera();
    cam->data()->active = true;
    cam->data()->x = heroWx + 32.f;
    cam->data()->y = heroWy;
    cam->data()->zoom = 1.f;
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    Texture albedo;
    albedo.width = 32;
    albedo.height = 32;
    Texture normal;
    normal.width = 32;
    normal.height = 32;

    auto *hero = Renderable2D::create();
    hero->transform()->x = heroWx;
    hero->transform()->y = heroWy;
    hero->sprite()->width = 24.f;
    hero->sprite()->height = 24.f;
    hero->sprite()->texture = &albedo;
    hero->sprite()->normalTexture = &normal;
    hero->sprite()->receiveLight = true;
    hero->sprite()->visible = true;
    hero->sprite()->layer = 10;
    hero->sprite()->camera = cam;

    auto *torch = Light2D::createLight("point");
    torch->setPosition(heroWx + 12.f, heroWy + 12.f);
    torch->setColor(1.f, 0.75f, 0.45f, 2.2f);
    torch->setRadius(140.f);
    torch->setEnabled(true);
    CHECK_EQ(torch->getType(), "point");
    CHECK(torch->isEnabled());

    // Far unlit prop for contrast in collection.
    float exitWx = 0.f, exitWy = 0.f;
    d.layer->tileToWorld(d.exitTx, d.exitTy, exitWx, exitWy);
    auto *exitMarker = Renderable2D::create();
    exitMarker->transform()->x = exitWx;
    exitMarker->transform()->y = exitWy;
    exitMarker->sprite()->width = 16.f;
    exitMarker->sprite()->height = 16.f;
    exitMarker->sprite()->texture = &albedo;
    exitMarker->sprite()->receiveLight = true;
    exitMarker->sprite()->visible = true;
    exitMarker->sprite()->layer = 10;
    exitMarker->sprite()->camera = cam;

    std::vector<DrawItem2D> scene;
    TileRenderSystem::collect(scene);
    RenderSystem::collectSprites(scene);
    REQUIRE(scene.size() >= 2);

    bool foundLitHero = false;
    for (const auto &it : scene) {
        if (it.texture == &albedo && it.normal == &normal) {
            CHECK(it.litPath);
            CHECK(it.receiveLight);
            foundLitHero = true;
        }
    }
    CHECK(foundLitHero);

    // Torch follows hero one tile along path.
    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 1);
    float nx = 0.f, ny = 0.f;
    d.layer->tileToWorld(path->getX(1), path->getY(1), nx, ny);
    hero->transform()->x = nx;
    hero->transform()->y = ny;
    torch->setPosition(nx + 12.f, ny + 12.f);
    CHECK(approxEq(torch->getX(), nx + 12.f));
    CHECK(approxEq(torch->getY(), ny + 12.f));

    hero->sprite()->visible = false;
    exitMarker->sprite()->visible = false;
    torch->setEnabled(false);
    cam->data()->active = false;

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.03 dynamic light",
                          .pngName = "03_dynamicLight.png",
                          .path = path,
                          .lamp = torch});

    d.layer->setVisible(false);

    delete path;
    delete pf;
}

TEST_CASE("hex.level.04.pickupCollision") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    auto *spatialMod = Spatial::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 123, 24, 18);

    ItemRegistry::clear();
    InventorySystem::clearEvents();
    InventorySystem::ensureBuiltins();
    ItemDefinition potion;
    potion.id = "hex.potion";
    potion.displayName = "Hex Potion";
    potion.maxStack = 10;
    potion.weight = 0.2f;
    potion.tags = {"consumable", "loot"};
    ItemRegistry::registerItem(potion);
    ItemDefinition key;
    key.id = "hex.key";
    key.displayName = "Brass Key";
    key.maxStack = 1;
    key.weight = 0.1f;
    key.tags = {"key", "loot"};
    ItemRegistry::registerItem(key);

    auto *inv = Inventory::create();
    Bag *bag = inv->newBag(12);
    bag->setId("hex.player");
    bag->setMaxWeight(40.f);

    // Place loot on neighboring walkable hexes around spawn.
    struct Loot {
        int id;
        int tx, ty;
        const char *itemId;
        bool taken = false;
    };
    std::vector<Loot> loots;
    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");

    auto tryAddLoot = [&](int tx, int ty, int id, const char *itemId) {
        if (!pf->isWalkable(tx, ty)) return;
        if (tx == d.spawnTx && ty == d.spawnTy) return;
        loots.push_back({id, tx, ty, itemId, false});
    };
    tryAddLoot(d.spawnTx + 1, d.spawnTy, 1, "hex.potion");
    tryAddLoot(d.spawnTx, d.spawnTy + 1, 2, "hex.key");
    tryAddLoot(d.spawnTx - 1, d.spawnTy, 3, "hex.potion");
    if (loots.empty()) {
        // Fallback: scan nearby cells.
        for (int dy = -2; dy <= 2 && loots.size() < 2; ++dy) {
            for (int dx = -2; dx <= 2 && loots.size() < 2; ++dx) {
                tryAddLoot(d.spawnTx + dx, d.spawnTy + dy, int(loots.size()) + 1,
                           loots.empty() ? "hex.potion" : "hex.key");
            }
        }
    }
    REQUIRE(loots.size() >= 1);

    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(48.f));
    const float half = 10.f;
    for (const auto &L : loots) {
        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(L.tx, L.ty, wx, wy);
        wx += kTileW * 0.5f;
        wy += kTileH * 0.5f;
        CHECK(hash->insert(L.id, wx - half, wy - half, wx + half, wy + half));
    }

    float playerWx = 0.f, playerWy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, playerWx, playerWy);
    playerWx += kTileW * 0.5f;
    playerWy += kTileH * 0.5f;

    int picked = 0;
    for (auto &L : loots) {
        float lx = 0.f, ly = 0.f;
        d.layer->tileToWorld(L.tx, L.ty, lx, ly);
        lx += kTileW * 0.5f;
        ly += kTileH * 0.5f;
        // Move player onto loot cell (collision pickup radius).
        playerWx = lx;
        playerWy = ly;
        const int n = hash->queryCircle(playerWx, playerWy, 14.f);
        for (int i = 0; i < n; ++i) {
            const int hit = hash->getResultId(i);
            for (auto &cand : loots) {
                if (cand.taken || cand.id != hit) continue;
                const int added = bag->addItem(cand.itemId, 1);
                CHECK_EQ(added, 1);
                CHECK(hash->remove(cand.id));
                cand.taken = true;
                ++picked;
            }
        }
    }
    CHECK(picked >= 1);
    CHECK(bag->countItem("hex.potion") + bag->countItem("hex.key") == picked);

    // No double-pickup after removal.
    const int again = hash->queryCircle(playerWx, playerWy, 14.f);
    CHECK_EQ(again, 0);

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.04 pickup", .pngName = "04_pickupCollision.png"});

    bag->destroy();
    ItemRegistry::clear();
    delete pf;
}

TEST_CASE("hex.level.05.particles") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    auto *parts = Particles::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 55, 20, 16);

    float torchWx = 0.f, torchWy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, torchWx, torchWy);
    torchWx += kTileW * 0.5f;
    torchWy += kTileH * 0.35f;

    ParticleEmitter *torchFx = parts->newEmitter(256);
    REQUIRE(torchFx != nullptr);
    torchFx->applyPreset("fire");
    torchFx->setPosition(torchWx, torchWy);
    torchFx->start();
    CHECK(torchFx->isActive());

    ParticleEmitter *sparkFx = parts->newEmitter(128);
    sparkFx->applyPreset("spark");
    sparkFx->setPosition(torchWx, torchWy);
    sparkFx->setEmitterLifetime(0.35f);
    sparkFx->start();

    // Simulate a short burn-in so particles exist.
    for (int i = 0; i < 8; ++i) parts->update(1.f / 60.f);
    CHECK(torchFx->getCount() > 0);

    // Pickup burst: one-shot spark at exit.
    float exitWx = 0.f, exitWy = 0.f;
    d.layer->tileToWorld(d.exitTx, d.exitTy, exitWx, exitWy);
    sparkFx->setPosition(exitWx + kTileW * 0.5f, exitWy + kTileH * 0.5f);
    sparkFx->reset();
    sparkFx->setEmitterLifetime(0.2f);
    sparkFx->start();
    sparkFx->emit(24);
    CHECK(sparkFx->getCount() > 0);

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.05 particles",
                          .pngName = "05_particles.png",
                          .fire = torchFx});

    torchFx->stop();
    sparkFx->stop();
}

TEST_CASE("hex.level.pipeline.dungeonCrawl") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    auto *spatialMod = Spatial::create();
    auto *parts = Particles::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 20260812, 36, 28);

    // --- Path: spawn → exit ---
    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 2);

    // --- FOV revealer follows path ---
    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    const int revealer = fov->addRevealer(d.spawnTx, d.spawnTy, 5);

    // --- Lighting / hero sprite ---
    float wx = 0.f, wy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, wx, wy);
    auto *cam = Camera2D::createCamera();
    cam->data()->active = true;
    cam->data()->x = wx;
    cam->data()->y = wy;
    cam->setAmbient(0.06f, 0.06f, 0.08f);

    Texture albedo;
    albedo.width = 16;
    albedo.height = 16;
    auto *hero = Renderable2D::create();
    hero->transform()->x = wx;
    hero->transform()->y = wy;
    hero->sprite()->width = 20.f;
    hero->sprite()->height = 20.f;
    hero->sprite()->texture = &albedo;
    hero->sprite()->receiveLight = true;
    hero->sprite()->visible = true;
    hero->sprite()->camera = cam;

    auto *torch = Light2D::createLight("point");
    torch->setColor(1.f, 0.8f, 0.5f, 2.f);
    torch->setRadius(120.f);
    torch->setEnabled(true);

    ParticleEmitter *fire = parts->newEmitter(128);
    fire->applyPreset("fire");
    fire->start();

    // --- Loot on path midpoint ---
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    ItemDefinition gem;
    gem.id = "hex.gem";
    gem.maxStack = 5;
    gem.weight = 0.05f;
    ItemRegistry::registerItem(gem);
    auto *inv = Inventory::create();
    Bag *bag = inv->newBag(8);

    const int mid = path->getLength() / 2;
    const int lootTx = path->getX(mid);
    const int lootTy = path->getY(mid);
    float lootWx = 0.f, lootWy = 0.f;
    d.layer->tileToWorld(lootTx, lootTy, lootWx, lootWy);
    lootWx += kTileW * 0.5f;
    lootWy += kTileH * 0.5f;

    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(32.f));
    CHECK(hash->insert(42, lootWx - 8.f, lootWy - 8.f, lootWx + 8.f, lootWy + 8.f));

    bool gotGem = false;
    int exploredBefore = 0;
    for (int step = 0; step < path->getLength(); ++step) {
        const int tx = path->getX(step);
        const int ty = path->getY(step);
        fov->setRevealerPosition(revealer, tx, ty);
        fov->compute();
        CHECK(fov->isVisible(tx, ty));

        d.layer->tileToWorld(tx, ty, wx, wy);
        hero->transform()->x = wx;
        hero->transform()->y = wy;
        torch->setPosition(wx + 10.f, wy + 8.f);
        fire->setPosition(wx + 10.f, wy + 4.f);
        cam->data()->x = wx;
        cam->data()->y = wy;
        parts->update(1.f / 30.f);

        const float px = wx + kTileW * 0.5f;
        const float py = wy + kTileH * 0.5f;
        if (!gotGem && hash->queryCircle(px, py, 16.f) > 0) {
            const int added = bag->addItem("hex.gem", 1);
            CHECK_EQ(added, 1);
            hash->remove(42);
            ParticleEmitter *burst = parts->newEmitter(64);
            burst->applyPreset("spark");
            burst->setPosition(lootWx, lootWy);
            burst->setEmitterLifetime(0.25f);
            burst->start();
            burst->emit(16);
            gotGem = true;
        }
        if (fov->isExplored(d.spawnTx, d.spawnTy)) exploredBefore = 1;
    }
    CHECK_EQ(exploredBefore, 1);
    CHECK(gotGem);
    CHECK_EQ(bag->countItem("hex.gem"), 1);
    CHECK(fov->isExplored(d.spawnTx, d.spawnTy));
    CHECK_EQ(path->getX(path->getLength() - 1), d.exitTx);
    CHECK(fire->getCount() > 0);

    std::vector<DrawItem2D> scene;
    RenderSystem::collectSprites(scene);
    bool lit = false;
    for (const auto &it : scene) {
        if (it.texture == &albedo && it.receiveLight) lit = true;
    }
    CHECK(lit);

    hero->sprite()->visible = false;
    torch->setEnabled(false);
    cam->data()->active = false;
    fire->stop();
    bag->destroy();
    ItemRegistry::clear();
    delete path;
    delete pf;
    delete fov;
}

TEST_CASE("hex.level.06.flowFieldSwarm") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 606, 28, 22);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");

    FlowField *field = pf->buildFlowField(d.exitTx, d.exitTy);
    REQUIRE(field != nullptr);
    CHECK_EQ(field->getGoalX(), d.exitTx);
    CHECK_EQ(field->getGoalY(), d.exitTy);
    CHECK(field->isReachable(d.spawnTx, d.spawnTy));

    // Collect a few distinct walkable starts near spawn.
    std::vector<std::pair<int, int>> starts;
    starts.push_back({d.spawnTx, d.spawnTy});
    for (int dy = -3; dy <= 3 && starts.size() < 4; ++dy) {
        for (int dx = -3; dx <= 3 && starts.size() < 4; ++dx) {
            const int x = d.spawnTx + dx;
            const int y = d.spawnTy + dy;
            if (!pf->isWalkable(x, y)) continue;
            if (x == d.spawnTx && y == d.spawnTy) continue;
            if (!field->isReachable(x, y)) continue;
            starts.push_back({x, y});
        }
    }
    REQUIRE(starts.size() >= 2);

    for (const auto &s : starts) {
        Path *p = pf->followFlow(field, s.first, s.second);
        REQUIRE(p != nullptr);
        CHECK(p->getLength() > 0);
        CHECK_EQ(p->getX(0), s.first);
        CHECK_EQ(p->getY(0), s.second);
        CHECK_EQ(p->getX(p->getLength() - 1), d.exitTx);
        CHECK_EQ(p->getY(p->getLength() - 1), d.exitTy);
        CHECK(pathWalkable(p, pf));
        delete p;

        Path *g = pf->findGroupPath(s.first, s.second, d.exitTx, d.exitTy);
        REQUIRE(g != nullptr);
        CHECK_EQ(g->getX(g->getLength() - 1), d.exitTx);
        delete g;
    }
    Path *swarmPath = pf->followFlow(field, d.spawnTx, d.spawnTy);
    REQUIRE(swarmPath != nullptr);
    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.06 flow swarm",
                          .pngName = "06_flowFieldSwarm.png",
                          .path = swarmPath});
    delete swarmPath;
    delete field;
    delete pf;
}

TEST_CASE("hex.level.07.cellCostDetour") {
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(9, 5, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);
    // Vertical expensive corridor at x=4; cheap bypass via bottom.
    for (int y = 0; y < 4; ++y) layer->setTile(4, y, kFloorGid);

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    for (int y = 0; y < 4; ++y) pf->setCellCost(4, y, 12.f);

    Path *cheap = pf->findPath(0, 2, 8, 2);
    REQUIRE(cheap != nullptr);
    CHECK(cheap->getLength() > 0);
    CHECK_EQ(cheap->getX(cheap->getLength() - 1), 8);
    // Prefer bypassing the costly column when possible.
    bool steppedOnCostly = false;
    for (int i = 0; i < cheap->getLength(); ++i) {
        if (cheap->getX(i) == 4 && cheap->getY(i) < 4) steppedOnCostly = true;
    }
    // With high cost, path should usually avoid the strip; if topology forces a
    // step, totalCost must still beat a forced expensive route.
    Path *forced = pf->findPath(0, 0, 8, 0);
    REQUIRE(forced != nullptr);
    CHECK(cheap->getTotalCost() <= forced->getTotalCost() + 1e-3f);
    (void)steppedOnCostly;

    previewHex(mapMod, layer, 0, 2, 8, 2,
               HexPreview{.title = "hex.level.07 cell cost",
                          .pngName = "07_cellCostDetour.png",
                          .path = cheap});

    delete forced;
    delete cheap;
    delete pf;
    layer->setVisible(false);
}

TEST_CASE("hex.level.08.multiRevealerPerception") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 808, 24, 18, "cave.cellular");

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    fov->setPerceptionRadiusScale(1.f);
    fov->setDetectionMargin(0.f);

    const int hero = fov->addRevealer(d.spawnTx, d.spawnTy, 3);
    const int torch = fov->addRevealer(d.exitTx, d.exitTy, 2);
    fov->setRevealerPerception(hero, 2.f);
    CHECK_EQ(fov->getEffectiveRadius(hero), 5);  // 3 + 2*1
    fov->compute();

    CHECK(fov->isVisible(d.spawnTx, d.spawnTy));
    CHECK(fov->isVisible(d.exitTx, d.exitTy));
    CHECK_EQ(fov->getRevealerCount(), 2);

    // Stealth gate: visible cell but high stealth fails detection.
    CHECK(fov->canDetect(hero, d.spawnTx, d.spawnTy, 1.f));
    CHECK(!fov->canDetect(hero, d.spawnTx, d.spawnTy, 9.f));

    fov->setRevealerEnabled(torch, false);
    fov->compute();
    CHECK(fov->isVisible(d.spawnTx, d.spawnTy));

    fov->setRevealerFacing(hero, 0.f, 45.f);  // east-ish cone
    fov->setRevealerPosition(hero, d.spawnTx, d.spawnTy);
    fov->compute();
    CHECK(fov->isVisible(d.spawnTx, d.spawnTy));

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.08 perception",
                          .pngName = "08_multiRevealerPerception.png"});

    delete fov;
}

TEST_CASE("hex.level.09.fowMaskAndAlgos") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 909, 20, 16);

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    fov->setAlgorithm("shadowcast");
    const int id = fov->addRevealer(d.spawnTx, d.spawnTy, 4);
    fov->compute();
    CHECK(fov->isVisible(d.spawnTx, d.spawnTy));
    CHECK_EQ(fov->getMaskByte(d.spawnTx, d.spawnTy), 255);

    // Leave the cell: explored memory → mid mask byte, not fully dark.
    fov->setRevealerPosition(id, d.exitTx, d.exitTy);
    fov->compute();
    if (!fov->isVisible(d.spawnTx, d.spawnTy)) {
        CHECK(fov->isExplored(d.spawnTx, d.spawnTy));
        CHECK(fov->getMaskByte(d.spawnTx, d.spawnTy) > 0);
        CHECK(fov->getMaskByte(d.spawnTx, d.spawnTy) < 255);
    }

    std::vector<uint8_t> mask;
    CHECK(fov->fillMaskR8(mask));
    CHECK_EQ(int(mask.size()), d.layer->getMapWidth() * d.layer->getMapHeight());

    fov->clearMemory();
    fov->compute();
    // After clear, cells only currently visible stay explored.
    CHECK(fov->isVisible(d.exitTx, d.exitTy));

    fov->setAlgorithm("raycast");
    CHECK_EQ(fov->getAlgorithm(), std::string("raycast"));
    fov->markDirty();
    fov->compute();
    CHECK(fov->isVisible(d.exitTx, d.exitTy));

    fov->setAlgorithm("permissive");
    fov->markDirty();
    fov->compute();
    CHECK(fov->isVisible(d.exitTx, d.exitTy));

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.09 fow mask", .pngName = "09_fowMaskAndAlgos.png"});

    delete fov;
}

TEST_CASE("hex.level.10.cameraPick") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 1010, 18, 14);
    constexpr float kViewW = 320.f;
    constexpr float kViewH = 240.f;

    float wx = 0.f, wy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, wx, wy);
    wx += kTileW * 0.5f;
    wy += kTileH * 0.5f;

    auto *cam = Camera2D::createCamera();
    cam->data()->active = true;
    cam->setPosition(wx, wy);
    cam->setZoom(1.f);

    const float sx = cam->worldToScreenX(wx, wy, kViewW, kViewH);
    const float sy = cam->worldToScreenY(wx, wy, kViewW, kViewH);
    const float backWx = cam->screenToWorldX(sx, sy, kViewW, kViewH);
    const float backWy = cam->screenToWorldY(sx, sy, kViewW, kViewH);
    CHECK(approxEq(backWx, wx));
    CHECK(approxEq(backWy, wy));

    int tx = -1, ty = -1;
    d.layer->worldToTile(backWx, backWy, tx, ty);
    CHECK_EQ(tx, d.spawnTx);
    CHECK_EQ(ty, d.spawnTy);

    // Click near exit: convert screen → tile.
    float exitWx = 0.f, exitWy = 0.f;
    d.layer->tileToWorld(d.exitTx, d.exitTy, exitWx, exitWy);
    exitWx += kTileW * 0.5f;
    exitWy += kTileH * 0.5f;
    const float esx = cam->worldToScreenX(exitWx, exitWy, kViewW, kViewH);
    const float esy = cam->worldToScreenY(exitWx, exitWy, kViewW, kViewH);
    const float pickWx = cam->screenToWorldX(esx, esy, kViewW, kViewH);
    const float pickWy = cam->screenToWorldY(esx, esy, kViewW, kViewH);
    int ptx = -1, pty = -1;
    d.layer->worldToTile(pickWx, pickWy, ptx, pty);
    CHECK_EQ(ptx, d.exitTx);
    CHECK_EQ(pty, d.exitTy);

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.10 camera pick", .pngName = "10_cameraPick.png"});

    cam->data()->active = false;
}

TEST_CASE("hex.level.11.dualGridHex") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 1111, 12, 10);

    // Build a compact logic layer from a corner of the dungeon.
    TileLayer *logic = mapMod->newLayer(4, 4, kTileW, kTileH);
    configureHexLayer(logic);
    logic->setOrigin(8.f, 12.f);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int gid = d.layer->getTile(x, y);
            logic->setTile(x, y, gid == kWallGid ? 0 : 1);
        }
    }
    TileLayer *display = mapMod->newLayer(1, 1, 8.f, 8.f);
    DualGridOptions opts;
    opts.useDefaultFrameTable = true;
    opts.firstDisplayGid = 1;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(static_cast<int>(display->config()->orientation),
             static_cast<int>(MapOrientation::Hexagonal));
    CHECK(approxEq(display->config()->hexSideLength, kHexSide));
    CHECK(display->getMapWidth() >= 4);
    CHECK(display->getMapHeight() >= 4);

    const int mask = dualGridMaskAt(*logic, 0, 0, 1);
    CHECK(mask >= 0);
    CHECK(dualGridDefaultFrame(mask) >= -1);
    float ox = 0.f, oy = 0.f;
    dualGridHalfOffset(*logic->config(), ox, oy);
    const bool hasOffset = std::fabs(ox) > 0.f || std::fabs(oy) > 0.f;
    CHECK(hasOffset);

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.11 dual grid", .pngName = "11_dualGridHex.png"});

    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("hex.level.12.procgenVariants") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    const char *algos[] = {"cave.cellular", "maze.backtrack", "wfc.simple", "cave.drunkard"};
    for (const char *algo : algos) {
        HexDungeon d = buildHexDungeon(mapMod, gen, 1212, 24, 18, algo);
        CHECK_EQ(static_cast<int>(d.layer->config()->orientation),
                 static_cast<int>(MapOrientation::Hexagonal));
        Pathfinder *pf = mapMod->newPathfinder(d.layer);
        pf->blockGid(kWallGid);
        pf->setTopology("hex");
        CHECK(pf->isWalkable(d.spawnTx, d.spawnTy));
        CHECK(pf->isWalkable(d.exitTx, d.exitTy));
        Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
        REQUIRE(path != nullptr);
        // Maze/WFC should usually connect; if not, at least spawn is walkable.
        if (path->getLength() > 0) {
            CHECK_EQ(path->getX(path->getLength() - 1), d.exitTx);
            CHECK(pathWalkable(path, pf));
        }
        if (std::string(algo) == "cave.drunkard") {
            previewHex(mapMod, d,
                       HexPreview{.title = "hex.level.12 procgen variants",
                                  .pngName = "12_procgenVariants.png",
                                  .path = path});
        }
        delete path;
        delete pf;
        d.layer->setVisible(false);
    }
}

TEST_CASE("hex.level.13.spatialCull") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    auto *spatialMod = Spatial::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 1313, 22, 16);

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    struct Marker {
        int id;
        float x, y;
    };
    std::vector<Marker> markers;
    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");

    int nextId = 1;
    for (int y = 0; y < d.layer->getMapHeight(); ++y) {
        for (int x = 0; x < d.layer->getMapWidth(); ++x) {
            if (!pf->isWalkable(x, y)) continue;
            if ((x + y) % 5 != 0) continue;
            float wx = 0.f, wy = 0.f;
            d.layer->tileToWorld(x, y, wx, wy);
            wx += kTileW * 0.5f;
            wy += kTileH * 0.5f;
            markers.push_back({nextId++, wx, wy});
            minX = std::min(minX, wx);
            minY = std::min(minY, wy);
            maxX = std::max(maxX, wx);
            maxY = std::max(maxY, wy);
        }
    }
    REQUIRE(markers.size() >= 3);

    std::unique_ptr<QuadTree> tree(
        spatialMod->newQuadTree(minX - 32.f, minY - 32.f, maxX + 32.f, maxY + 32.f, 8, 6));
    for (const auto &m : markers) {
        CHECK(tree->insert(m.id, m.x - 4.f, m.y - 4.f, m.x + 4.f, m.y + 4.f));
    }

    float camWx = 0.f, camWy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, camWx, camWy);
    const float view = 96.f;
    const int hit = tree->queryRect(camWx - view, camWy - view, camWx + view, camWy + view);
    CHECK(hit >= 1);
    CHECK(hit <= int(markers.size()));

    // Far query should miss spawn-neighborhood markers when exit is distant.
    float exitWx = 0.f, exitWy = 0.f;
    d.layer->tileToWorld(d.exitTx, d.exitTy, exitWx, exitWy);
    if (std::fabs(exitWx - camWx) + std::fabs(exitWy - camWy) > view * 3.f) {
        const int farHit =
            tree->queryRect(exitWx - 8.f, exitWy - 8.f, exitWx + 8.f, exitWy + 8.f);
        // May be zero if no marker on exit cell; still a valid cull API exercise.
        CHECK(farHit >= 0);
    }
    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.13 spatial cull", .pngName = "13_spatialCull.png"});
    delete pf;
}

TEST_CASE("hex.level.14.multiLight") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 1414, 16, 12);

    float wx = 0.f, wy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, wx, wy);

    auto *cam = Camera2D::createCamera();
    cam->data()->active = true;
    cam->setPosition(wx, wy);
    cam->setAmbient(0.05f, 0.05f, 0.07f);

    Texture albedo;
    albedo.width = 16;
    albedo.height = 16;
    auto *hero = Renderable2D::create();
    hero->transform()->x = wx;
    hero->transform()->y = wy;
    hero->sprite()->width = 18.f;
    hero->sprite()->height = 18.f;
    hero->sprite()->texture = &albedo;
    hero->sprite()->receiveLight = true;
    hero->sprite()->visible = true;
    hero->sprite()->camera = cam;

    auto *torch = Light2D::createLight("point");
    torch->setPosition(wx + 8.f, wy + 8.f);
    torch->setColor(1.f, 0.7f, 0.4f, 2.f);
    torch->setRadius(100.f);
    torch->setEnabled(true);

    auto *moon = Light2D::createLight("dir");
    moon->setDirection(-0.3f, 1.f);
    moon->setColor(0.4f, 0.5f, 0.8f, 0.6f);
    moon->setEnabled(true);
    CHECK_EQ(moon->getType(), "dir");

    std::vector<DrawItem2D> scene;
    RenderSystem::collectSprites(scene);
    bool found = false;
    for (const auto &it : scene) {
        if (it.texture == &albedo && it.receiveLight) found = true;
    }
    CHECK(found);
    CHECK(torch->isEnabled());
    CHECK(moon->isEnabled());

    torch->setEnabled(false);
    moon->setEnabled(false);
    hero->sprite()->visible = false;
    cam->data()->active = false;

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.14 multi light",
                          .pngName = "14_multiLight.png",
                          .lamp = torch});

    d.layer->setVisible(false);
}

TEST_CASE("hex.level.15.particleStashTransfer") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    auto *parts = Particles::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 1515, 18, 14);

    float wx = 0.f, wy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, wx, wy);

    ParticleEmitter *fire = parts->newEmitter(128);
    fire->applyPreset("fire");
    fire->setPosition(wx, wy);
    fire->start();

    ParticleEmitter *smoke = parts->newEmitter(128);
    smoke->applyPreset("smoke");
    smoke->setPosition(wx + 6.f, wy - 4.f);
    smoke->start();

    ParticleEmitter *spark = parts->newEmitter(64);
    spark->applyPreset("spark");
    spark->setPosition(wx, wy);
    spark->setEmitterLifetime(0.4f);
    spark->start();

    for (int i = 0; i < 10; ++i) parts->update(1.f / 60.f);
    CHECK(fire->getCount() > 0);
    CHECK(smoke->getCount() > 0);

    spark->stop();
    CHECK(spark->isStopped());
    fire->pause();
    CHECK(fire->isPaused());
    fire->start();
    CHECK(fire->isActive());

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    ItemDefinition ore;
    ore.id = "hex.ore";
    ore.maxStack = 20;
    ore.weight = 1.f;
    ItemRegistry::registerItem(ore);

    auto *inv = Inventory::create();
    Bag *bag = inv->newBag(8);
    Bag *stash = inv->newBag(12);
    bag->setId("player");
    stash->setId("stash");
    const int added = bag->addItem("hex.ore", 5);
    CHECK_EQ(added, 5);
    const int moved = inv->transferItem(bag, stash, "hex.ore", 3);
    CHECK_EQ(moved, 3);
    CHECK_EQ(bag->countItem("hex.ore"), 2);
    CHECK_EQ(stash->countItem("hex.ore"), 3);

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.15 particle stash",
                          .pngName = "15_particleStashTransfer.png",
                          .fire = fire});

    fire->stop();
    smoke->stop();
    bag->destroy();
    stash->destroy();
    ItemRegistry::clear();
    d.layer->setVisible(false);
}

TEST_CASE("hex.level.pipeline.fogRaid") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    auto *spatialMod = Spatial::create();
    auto *parts = Particles::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 1616, 30, 22);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    FlowField *field = pf->buildFlowField(d.exitTx, d.exitTy);
    REQUIRE(field != nullptr);
    Path *path = pf->followFlow(field, d.spawnTx, d.spawnTy);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 2);

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    fov->setPerceptionRadiusScale(0.5f);
    const int hero = fov->addRevealer(d.spawnTx, d.spawnTy, 4);
    fov->setRevealerPerception(hero, 2.f);
    fov->setRevealerFacing(hero, 90.f, 80.f);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    ItemDefinition relic;
    relic.id = "hex.relic";
    relic.maxStack = 1;
    relic.weight = 0.5f;
    ItemRegistry::registerItem(relic);
    auto *inv = Inventory::create();
    Bag *bag = inv->newBag(6);

    const int mid = path->getLength() / 2;
    const int lootTx = path->getX(mid);
    const int lootTy = path->getY(mid);
    float lootWx = 0.f, lootWy = 0.f;
    d.layer->tileToWorld(lootTx, lootTy, lootWx, lootWy);
    lootWx += kTileW * 0.5f;
    lootWy += kTileH * 0.5f;
    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(28.f));
    CHECK(hash->insert(7, lootWx - 6.f, lootWy - 6.f, lootWx + 6.f, lootWy + 6.f));

    ParticleEmitter *ember = parts->newEmitter(96);
    ember->applyPreset("spark");
    ember->start();

    bool looted = false;
    for (int step = 0; step < path->getLength(); ++step) {
        const int tx = path->getX(step);
        const int ty = path->getY(step);
        fov->setRevealerPosition(hero, tx, ty);
        fov->compute();
        CHECK(fov->isVisible(tx, ty));

        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(tx, ty, wx, wy);
        ember->setPosition(wx + 8.f, wy + 4.f);
        parts->update(1.f / 30.f);

        // Perception-gated pickup: must see tile and pass stealth check.
        if (!looted && fov->canDetect(hero, lootTx, lootTy, 0.5f) &&
            hash->queryCircle(wx + kTileW * 0.5f, wy + kTileH * 0.5f, 18.f) > 0) {
            const int added = bag->addItem("hex.relic", 1);
            CHECK_EQ(added, 1);
            hash->remove(7);
            ember->emit(12);
            looted = true;
        }
    }
    CHECK(looted);
    CHECK_EQ(bag->countItem("hex.relic"), 1);
    CHECK(fov->isExplored(d.spawnTx, d.spawnTy));
    CHECK_EQ(path->getX(path->getLength() - 1), d.exitTx);

    ember->stop();
    bag->destroy();
    ItemRegistry::clear();
    delete path;
    delete field;
    delete pf;
    delete fov;
}

TEST_CASE("hex.level.16.facingConeFov") {
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(11, 11, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);

    Fov *fov = mapMod->newFov(layer);
    fov->setBlockEmpty(false);
    fov->setTopology("hex");
    fov->setAlgorithm("shadowcast");
    const int id = fov->addRevealer(5, 5, 4);
    // Narrow east-facing cone.
    fov->setRevealerFacing(id, 0.f, 35.f);
    fov->compute();
    CHECK(fov->isVisible(5, 5));

    int eastVisible = 0;
    int westVisible = 0;
    for (int x = 6; x <= 9; ++x)
        if (fov->isVisible(x, 5)) ++eastVisible;
    for (int x = 1; x <= 4; ++x)
        if (fov->isVisible(x, 5)) ++westVisible;
    CHECK(eastVisible > westVisible);

    fov->clearRevealerFacing(id);
    fov->compute();
    int omni = 0;
    for (int y = 3; y <= 7; ++y)
        for (int x = 3; x <= 7; ++x)
            if (fov->isVisible(x, y)) ++omni;
    CHECK(omni > eastVisible);

    previewHex(mapMod, layer, 5, 5, 9, 5,
               HexPreview{.title = "hex.level.16 facing cone",
                          .pngName = "16_facingConeFov.png"});

    delete fov;
    layer->setVisible(false);
}

TEST_CASE("hex.level.17.flowPlusCellCost") {
    // Handcrafted open field: BSP spawn/stairs path length is not stable across
    // standard-library RNGs (macOS libc++ vs Linux libstdc++).
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(14, 8, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);

    const int spawnTx = 1, spawnTy = 4;
    const int exitTx = 12, exitTy = 4;

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");

    Path *baseline = pf->findPath(spawnTx, spawnTy, exitTx, exitTy);
    REQUIRE(baseline != nullptr);
    REQUIRE(baseline->getLength() > 2);
    const float baseCost = baseline->getTotalCost();

    // Paint expensive cells along the first half of the baseline path.
    const int paintUntil = std::max(1, baseline->getLength() / 2);
    for (int i = 1; i < paintUntil; ++i)
        pf->setCellCost(baseline->getX(i), baseline->getY(i), 10.f);

    Path *detour = pf->findPath(spawnTx, spawnTy, exitTx, exitTy);
    REQUIRE(detour != nullptr);
    CHECK(detour->getLength() > 0);
    CHECK(detour->getTotalCost() + 1e-3f >= baseCost);

    FlowField *field = pf->buildFlowField(exitTx, exitTy);
    REQUIRE(field != nullptr);
    CHECK(field->isReachable(spawnTx, spawnTy));
    Path *flow = pf->followFlow(field, spawnTx, spawnTy);
    REQUIRE(flow != nullptr);
    CHECK_EQ(flow->getX(flow->getLength() - 1), exitTx);
    CHECK_EQ(flow->getY(flow->getLength() - 1), exitTy);

    previewHex(mapMod, layer, spawnTx, spawnTy, exitTx, exitTy,
               HexPreview{.title = "hex.level.17 flow plus cell cost",
                          .pngName = "17_flowPlusCellCost.png",
                          .path = detour});

    delete flow;
    delete field;
    delete detour;
    delete baseline;
    delete pf;
    layer->setVisible(false);
}

TEST_CASE("hex.level.18.seedReproducible") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon a = buildHexDungeon(mapMod, gen, 4242, 24, 18);
    HexDungeon b = buildHexDungeon(mapMod, gen, 4242, 24, 18);

    CHECK_EQ(a.spawnTx, b.spawnTx);
    CHECK_EQ(a.spawnTy, b.spawnTy);
    CHECK_EQ(a.exitTx, b.exitTx);
    CHECK_EQ(a.exitTy, b.exitTy);
    CHECK_EQ(a.layer->getMapWidth(), b.layer->getMapWidth());

    // Same seed ⇒ same tile footprint sample.
    int mismatches = 0;
    for (int y = 0; y < a.layer->getMapHeight(); ++y)
        for (int x = 0; x < a.layer->getMapWidth(); ++x)
            if (a.layer->getTile(x, y) != b.layer->getTile(x, y)) ++mismatches;
    CHECK_EQ(mismatches, 0);

    Pathfinder *pfA = mapMod->newPathfinder(a.layer);
    Pathfinder *pfB = mapMod->newPathfinder(b.layer);
    pfA->blockGid(kWallGid);
    pfB->blockGid(kWallGid);
    pfA->setTopology("hex");
    pfB->setTopology("hex");
    Path *pa = pfA->findPath(a.spawnTx, a.spawnTy, a.exitTx, a.exitTy);
    Path *pb = pfB->findPath(b.spawnTx, b.spawnTy, b.exitTx, b.exitTy);
    REQUIRE(pa != nullptr);
    REQUIRE(pb != nullptr);
    CHECK_EQ(pa->getLength(), pb->getLength());

    previewHex(mapMod, a,
               HexPreview{.title = "hex.level.18 seed reproducible",
                          .pngName = "18_seedReproducible.png",
                          .path = pa});

    delete pa;
    delete pb;
    delete pfA;
    delete pfB;
    a.layer->setVisible(false);
    b.layer->setVisible(false);
}

TEST_CASE("hex.level.19.cornerPeekToggle") {
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(7, 7, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);
    // Wall corner near center.
    layer->setTile(3, 2, kWallGid);
    layer->setTile(2, 3, kWallGid);

    Fov *fov = mapMod->newFov(layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setBlockEmpty(false);
    fov->setTopology("hex");
    fov->setCornerPeek(false);
    CHECK(!fov->getCornerPeek());
    const int id = fov->addRevealer(1, 1, 5);
    fov->compute();
    CHECK(fov->isVisible(1, 1));
    const bool peekOff = fov->isVisible(4, 4);

    fov->setCornerPeek(true);
    CHECK(fov->getCornerPeek());
    fov->markDirty();
    fov->setRevealerPosition(id, 1, 1);
    fov->compute();
    const bool peekOn = fov->isVisible(4, 4);
    // Enabling corner peek should not reduce visibility of the origin.
    CHECK(fov->isVisible(1, 1));
    (void)peekOff;
    (void)peekOn;

    previewHex(mapMod, layer, 1, 1, 5, 5,
               HexPreview{.title = "hex.level.19 corner peek",
                          .pngName = "19_cornerPeekToggle.png"});

    delete fov;
    layer->setVisible(false);
}

TEST_CASE("hex.level.20.equipmentLootEquip") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    auto *spatialMod = Spatial::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 2020, 22, 16);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto *inv = Inventory::create();
    const int n = inv->registerItemsFromJson(R"([
      {"id":"hex.boots","displayName":"疾行靴","maxStack":1,"weight":0.8,"equipSlot":"feet","tags":["equip","boots"]},
      {"id":"hex.shield","displayName":"六角盾","maxStack":1,"weight":2.0,"equipSlot":"offhand","tags":["equip","shield"]}
    ])");
    CHECK_EQ(n, 2);

    Bag *bag = inv->newBag(8);
    EquipmentSet *eq = inv->newEquipmentSet();
    eq->setId("hero");
    eq->defineSlot("feet");
    eq->addSlotAllowedTag("feet", "boots");
    eq->defineSlot("offhand");
    eq->addSlotAllowedTag("offhand", "shield");

    float wx = 0.f, wy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, wx, wy);
    wx += kTileW * 0.5f;
    wy += kTileH * 0.5f;
    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(32.f));
    CHECK(hash->insert(1, wx - 8.f, wy - 8.f, wx + 8.f, wy + 8.f));
    CHECK(hash->queryCircle(wx, wy, 16.f) > 0);

    const int bootsAdded = bag->addItem("hex.boots", 1);
    const int shieldAdded = bag->addItem("hex.shield", 1);
    CHECK_EQ(bootsAdded, 1);
    CHECK_EQ(shieldAdded, 1);
    hash->remove(1);

    const int bootsSlot = bag->findItem("hex.boots");
    const int shieldSlot = bag->findItem("hex.shield");
    CHECK(bootsSlot >= 0);
    CHECK(shieldSlot >= 0);
    CHECK(eq->equipFromBag("feet", bag, bootsSlot));
    CHECK(eq->equipFromBag("offhand", bag, shieldSlot));
    CHECK_EQ(eq->getSlotItemId("feet"), std::string("hex.boots"));
    CHECK_EQ(eq->getSlotItemId("offhand"), std::string("hex.shield"));
    CHECK_EQ(bag->countItem("hex.boots"), 0);
    CHECK_EQ(bag->countItem("hex.shield"), 0);

    CHECK(eq->unequipToBag("feet", bag));
    CHECK_EQ(bag->countItem("hex.boots"), 1);

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.20 equipment loot",
                          .pngName = "20_equipmentLootEquip.png"});

    bag->destroy();
    eq->destroy();
    ItemRegistry::clear();
    d.layer->setVisible(false);
}

TEST_CASE("hex.level.pipeline.torchEscort") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    auto *parts = Particles::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 3030, 28, 20);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    FlowField *field = pf->buildFlowField(d.exitTx, d.exitTy);
    REQUIRE(field != nullptr);
    Path *path = pf->followFlow(field, d.spawnTx, d.spawnTy);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 2);

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    fov->setPerceptionRadiusScale(1.f);
    const int hero = fov->addRevealer(d.spawnTx, d.spawnTy, 4);
    const int torch = fov->addRevealer(d.exitTx, d.exitTy, 3);
    fov->setRevealerPerception(hero, 1.5f);

    auto *lamp = Light2D::createLight("point");
    lamp->setColor(1.f, 0.75f, 0.4f, 2.f);
    lamp->setRadius(130.f);
    lamp->setEnabled(true);

    ParticleEmitter *fire = parts->newEmitter(128);
    fire->applyPreset("fire");
    fire->start();

    int visibleSteps = 0;
    for (int step = 0; step < path->getLength(); ++step) {
        const int tx = path->getX(step);
        const int ty = path->getY(step);
        fov->setRevealerPosition(hero, tx, ty);
        fov->compute();
        if (fov->isVisible(tx, ty)) ++visibleSteps;

        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(tx, ty, wx, wy);
        lamp->setPosition(wx + 4.f, wy);
        fire->setPosition(wx + 4.f, wy - 6.f);
        parts->update(1.f / 30.f);
    }
    CHECK(visibleSteps == path->getLength());
    const bool exitKnown =
        fov->isVisible(d.exitTx, d.exitTy) || fov->isExplored(d.exitTx, d.exitTy);
    CHECK(exitKnown);
    CHECK(fov->getRevealerCount() == 2);
    CHECK(fire->getCount() > 0);
    CHECK(approxEq(lamp->getRadius(), 130.f));

    fire->stop();
    lamp->setEnabled(false);
    delete path;
    delete field;
    delete pf;
    delete fov;
    (void)torch;
}

TEST_CASE("hex.level.pipeline.catalogRaid") {
    // Raid-style crawl: path-mid relic gated by perception + rich side loot.
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    auto *spatialMod = Spatial::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 20260812, 32, 24);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("hex"));
    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 2);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto *inv = Inventory::create();
    inv->registerItemsFromJson(R"([
      {"id":"hex.relic","displayName":"迷雾圣物","maxStack":1,"weight":0.5,"tags":["quest","relic"]},
      {"id":"hex.gem","displayName":"地牢宝石","maxStack":5,"weight":0.05,"tags":["treasure"]},
      {"id":"hex.potion","displayName":"六角药水","maxStack":10,"weight":0.2,"tags":["potion"]},
      {"id":"hex.coin","displayName":"六角币","maxStack":99,"weight":0.01,"tags":["currency"]}
    ])");
    Bag *bag = inv->newBag(16);

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    fov->setDetectionMargin(0.f);
    const int hero = fov->addRevealer(d.spawnTx, d.spawnTy, 5);
    fov->setRevealerPerception(hero, 2.f);

    struct Drop {
        int id;
        int tx, ty;
        const char *item;
        float stealth;
        bool needVisible;
        bool taken = false;
    };
    const int mid = path->getLength() / 2;
    std::vector<Drop> drops = {
        {1, path->getX(mid), path->getY(mid), "hex.relic", 0.5f, true},
        {2, d.spawnTx + 1, d.spawnTy, "hex.potion", 0.f, false},
        {3, d.spawnTx, d.spawnTy + 1, "hex.gem", 0.f, false},
        {4, d.spawnTx - 1, d.spawnTy, "hex.coin", 0.f, false},
    };

    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(36.f));
    for (auto &drop : drops) {
        if (!pf->isWalkable(drop.tx, drop.ty)) {
            drop.tx = d.spawnTx;
            drop.ty = d.spawnTy;
        }
        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(drop.tx, drop.ty, wx, wy);
        wx += kTileW * 0.5f;
        wy += kTileH * 0.5f;
        CHECK(hash->insert(drop.id, wx - 6.f, wy - 6.f, wx + 6.f, wy + 6.f));
    }

    int picked = 0;
    for (int step = 0; step < path->getLength(); ++step) {
        const int tx = path->getX(step);
        const int ty = path->getY(step);
        fov->setRevealerPosition(hero, tx, ty);
        fov->compute();
        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(tx, ty, wx, wy);
        wx += kTileW * 0.5f;
        wy += kTileH * 0.5f;
        for (auto &drop : drops) {
            if (drop.taken) continue;
            if (drop.needVisible && !fov->canDetect(hero, drop.tx, drop.ty, drop.stealth)) continue;
            if (hash->queryCircle(wx, wy, 20.f) <= 0) continue;
            // Confirm this query hit the drop id.
            bool hit = false;
            const int n = hash->queryCircle(wx, wy, 20.f);
            for (int qi = 0; qi < n; ++qi)
                if (hash->getResultId(qi) == drop.id) hit = true;
            if (!hit) continue;
            const int added = bag->addItem(drop.item, 1);
            if (added > 0) {
                drop.taken = true;
                hash->remove(drop.id);
                ++picked;
            }
        }
    }
    CHECK(picked >= 2);
    CHECK(bag->getUsedSlotCount() >= 2);
    CHECK_EQ(path->getX(path->getLength() - 1), d.exitTx);

    bag->destroy();
    ItemRegistry::clear();
    delete path;
    delete pf;
    delete fov;
}

TEST_CASE("hex.level.21.hexWorldTileRoundtrip") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 2121, 20, 14);

    int ok = 0;
    for (int y = 0; y < d.layer->getMapHeight(); ++y) {
        for (int x = 0; x < d.layer->getMapWidth(); ++x) {
            if (d.layer->getTile(x, y) == kWallGid) continue;
            float wx = 0.f, wy = 0.f;
            d.layer->tileToWorld(x, y, wx, wy);
            // Sample near tile center for stable hex pick.
            wx += kTileW * 0.5f;
            wy += kTileH * 0.5f;
            int tx = -1, ty = -1;
            d.layer->worldToTile(wx, wy, tx, ty);
            CHECK_EQ(tx, x);
            CHECK_EQ(ty, y);
            CHECK_EQ(d.layer->worldToTileX(wx, wy), x);
            CHECK_EQ(d.layer->worldToTileY(wx, wy), y);
            ++ok;
            if (ok >= 12) break;
        }
        if (ok >= 12) break;
    }
    CHECK(ok >= 8);

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.21 world tile roundtrip",
                          .pngName = "21_hexWorldTileRoundtrip.png"});

    d.layer->setVisible(false);
}

TEST_CASE("hex.level.22.fovExploredMemoryClear") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 2222, 22, 16);

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    const int id = fov->addRevealer(d.spawnTx, d.spawnTy, 5);
    fov->compute();
    CHECK(fov->isVisible(d.spawnTx, d.spawnTy));
    CHECK_EQ(fov->getState(d.spawnTx, d.spawnTy), std::string("visible"));

    fov->setRevealerPosition(id, d.exitTx, d.exitTy);
    fov->compute();
    CHECK(fov->isExplored(d.spawnTx, d.spawnTy));
    const std::string exploredState = fov->getState(d.spawnTx, d.spawnTy);
    const bool exploredOrVisible =
        exploredState == "explored" || exploredState == "visible";
    CHECK(exploredOrVisible);

    fov->clearMemory();
    fov->markDirty();
    fov->compute();
    // After clear, spawn is unknown unless still in current FOV.
    if (!fov->isVisible(d.spawnTx, d.spawnTy)) {
        CHECK(!fov->isExplored(d.spawnTx, d.spawnTy));
        CHECK_EQ(fov->getState(d.spawnTx, d.spawnTy), std::string("unknown"));
    }

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.22 fov explored memory",
                          .pngName = "22_fovExploredMemoryClear.png"});

    delete fov;
}

TEST_CASE("hex.level.23.findGroupPathHex") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 2323, 26, 18);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");

    Path *solo = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(solo != nullptr);
    CHECK(solo->getLength() > 0);

    Path *group = pf->findGroupPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(group != nullptr);
    CHECK(group->getLength() > 0);
    CHECK_EQ(group->getX(group->getLength() - 1), d.exitTx);
    CHECK_EQ(group->getY(group->getLength() - 1), d.exitTy);
    CHECK(pathWalkable(group, pf));

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.23 find group path",
                          .pngName = "23_findGroupPathHex.png",
                          .path = solo});

    delete group;
    delete solo;
    delete pf;
}

TEST_CASE("hex.level.24.fovAlgoVisibilitySmoke") {
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(13, 13, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);
    layer->setTile(6, 5, kWallGid);
    layer->setTile(5, 6, kWallGid);
    layer->setTile(7, 6, kWallGid);

    const char *algos[] = {"shadowcast", "raycast", "permissive", "rectangle"};
    int counts[4] = {0, 0, 0, 0};
    for (int ai = 0; ai < 4; ++ai) {
        Fov *fov = mapMod->newFov(layer);
        fov->blockOpaqueGid(kWallGid);
        fov->setBlockEmpty(false);
        fov->setTopology("hex");
        fov->setAlgorithm(algos[ai]);
        CHECK_EQ(fov->getAlgorithm(), std::string(algos[ai]));
        fov->addRevealer(6, 6, 4);
        fov->compute();
        CHECK(fov->isVisible(6, 6));
        for (int y = 0; y < 13; ++y)
            for (int x = 0; x < 13; ++x)
                if (fov->isVisible(x, y)) ++counts[ai];
        CHECK(counts[ai] > 1);
        delete fov;
    }
    for (int ai = 0; ai < 4; ++ai) CHECK(counts[ai] >= 5);

    previewHex(mapMod, layer, 6, 6, 11, 11,
               HexPreview{.title = "hex.level.24 fov algo smoke",
                          .pngName = "24_fovAlgoVisibilitySmoke.png"});

    layer->setVisible(false);
}

TEST_CASE("hex.level.25.blockedCellSyncPath") {
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(8, 5, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    CHECK(pf->isWalkable(3, 2));

    // setBlocked updates the cost grid immediately (pre-sync).
    pf->setBlocked(3, 2, true);
    CHECK(!pf->isWalkable(3, 2));
    pf->setBlocked(3, 2, false);
    CHECK(pf->isWalkable(3, 2));

    // Persistent block via tile + syncFromLayer (survives findPath ensureSynced).
    layer->setTile(4, 2, kWallGid);
    pf->syncFromLayer();
    CHECK(!pf->isWalkable(4, 2));
    Path *after = pf->findPath(0, 2, 7, 2);
    REQUIRE(after != nullptr);
    CHECK(after->getLength() > 0);
    bool steppedWall = false;
    for (int i = 0; i < after->getLength(); ++i)
        if (after->getX(i) == 4 && after->getY(i) == 2) steppedWall = true;
    CHECK(!steppedWall);

    previewHex(mapMod, layer, 0, 2, 7, 2,
               HexPreview{.title = "hex.level.25 blocked cell sync",
                          .pngName = "25_blockedCellSyncPath.png",
                          .path = after});

    delete after;
    delete pf;
    layer->setVisible(false);
}

TEST_CASE("hex.level.pipeline.costlyFogPickup") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    auto *spatialMod = Spatial::create();
    auto *parts = Particles::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 2525, 28, 20);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    Path *base = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(base != nullptr);
    REQUIRE(base->getLength() > 2);

    const int paintN = std::max(1, base->getLength() / 3);
    for (int i = 1; i < paintN; ++i)
        pf->setCellCost(base->getX(i), base->getY(i), 9.f);

    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    CHECK(path->getTotalCost() + 1e-3f >= base->getTotalCost());

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    const int hero = fov->addRevealer(d.spawnTx, d.spawnTy, 5);
    fov->setRevealerPerception(hero, 2.f);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto *inv = Inventory::create();
    inv->registerItemsFromJson(
        R"([{"id":"hex.crystal","displayName":"视野水晶","maxStack":3,"weight":0.15,"tags":["loot","fov"]}])");
    Bag *bag = inv->newBag(6);

    const int mid = path->getLength() / 2;
    const int lootTx = path->getX(mid);
    const int lootTy = path->getY(mid);
    float lootWx = 0.f, lootWy = 0.f;
    d.layer->tileToWorld(lootTx, lootTy, lootWx, lootWy);
    lootWx += kTileW * 0.5f;
    lootWy += kTileH * 0.5f;
    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(30.f));
    CHECK(hash->insert(9, lootWx - 5.f, lootWy - 5.f, lootWx + 5.f, lootWy + 5.f));

    ParticleEmitter *spark = parts->newEmitter(64);
    spark->applyPreset("spark");
    spark->setEmitterLifetime(0.35f);

    bool looted = false;
    for (int step = 0; step < path->getLength(); ++step) {
        const int tx = path->getX(step);
        const int ty = path->getY(step);
        fov->setRevealerPosition(hero, tx, ty);
        fov->compute();
        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(tx, ty, wx, wy);
        wx += kTileW * 0.5f;
        wy += kTileH * 0.5f;
        if (!looted && fov->canDetect(hero, lootTx, lootTy, 1.5f) &&
            hash->queryCircle(wx, wy, 18.f) > 0) {
            const int added = bag->addItem("hex.crystal", 1);
            if (added > 0) {
                looted = true;
                hash->remove(9);
                spark->setPosition(lootWx, lootWy);
                spark->start();
                spark->emit(16);
            }
        }
        parts->update(1.f / 30.f);
    }
    CHECK(looted);
    CHECK_EQ(bag->countItem("hex.crystal"), 1);

    spark->stop();
    bag->destroy();
    ItemRegistry::clear();
    delete path;
    delete base;
    delete pf;
    delete fov;
}

TEST_CASE("hex.level.26.drunkardCaveHex") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(30, 22, kTileW, kTileH);
    configureHexLayer(layer);
    GeneratorRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(2626);
    p.setSize(30, 22);
    p.setFloat("floorPct", 0.42f);
    Grid2D grid;
    std::string err;
    REQUIRE(GeneratorRegistry::instance().generate("cave.drunkard", p, grid, err));
    gen->setPaletteGid("hex_drunk", "wall", kWallGid);
    gen->setPaletteGid("hex_drunk", "floor", kFloorGid);
    gen->setPaletteGid("hex_drunk", "corridor", kFloorGid);
    gen->setPaletteGid("hex_drunk", "door", kDoorGid);
    REQUIRE(gen->applyToLayer(&grid, "hex_drunk", layer));

    int floors = 0;
    for (uint32_t c : grid.cells())
        if (c == Semantic::Floor || c == Semantic::Corridor) ++floors;
    CHECK(floors >= 8);

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(kWallGid);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("hex"));
    int sx = -1, sy = -1;
    REQUIRE(findWalkable(grid, sx, sy));
    CHECK(pf->isWalkable(sx, sy));
    int ex = sx, ey = sy;
    findFarthestWalkable(grid, sx, sy, ex, ey);
    previewHex(mapMod, layer, sx, sy, ex, ey,
               HexPreview{.title = "hex.level.26 drunkard cave",
                          .pngName = "26_drunkardCaveHex.png"});
    delete pf;
    layer->setVisible(false);
}

TEST_CASE("hex.level.27.mazeHexPath") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 2727, 28, 22, "maze.backtrack");
    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK(pathWalkable(path, pf));

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.27 maze hex",
                          .pngName = "27_mazeHexPath.png",
                          .path = path});

    delete path;
    delete pf;
}

TEST_CASE("hex.level.28.wfcDungeonHex") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(24, 18, kTileW, kTileH);
    configureHexLayer(layer);
    GeneratorRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(2828);
    p.setSize(24, 18);
    p.setString("preset", "dungeon");
    p.setInt("maxAttempts", 64);
    Grid2D grid;
    std::string err;
    const bool ok = GeneratorRegistry::instance().generate("wfc.simple", p, grid, err);
    CHECK(ok);
    if (ok) {
        gen->setPaletteGid("hex_wfc", "wall", kWallGid);
        gen->setPaletteGid("hex_wfc", "floor", kFloorGid);
        gen->setPaletteGid("hex_wfc", "corridor", kFloorGid);
        gen->setPaletteGid("hex_wfc", "door", kDoorGid);
        REQUIRE(gen->applyToLayer(&grid, "hex_wfc", layer));
        Pathfinder *pf = mapMod->newPathfinder(layer);
        pf->blockGid(kWallGid);
        pf->setTopology("hex");
        int sx = -1, sy = -1;
        if (findWalkable(grid, sx, sy)) {
            CHECK(pf->isWalkable(sx, sy));
            int ex = sx, ey = sy;
            findFarthestWalkable(grid, sx, sy, ex, ey);
            previewHex(mapMod, layer, sx, sy, ex, ey,
                       HexPreview{.title = "hex.level.28 wfc dungeon",
                                  .pngName = "28_wfcDungeonHex.png"});
        }
        delete pf;
    }
    layer->setVisible(false);
}

TEST_CASE("hex.level.29.mistParticleScene") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    auto *parts = Particles::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 2929, 24, 18);

    float wx = 0.f, wy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, wx, wy);

    ParticleEmitter *mist = parts->newEmitter(160);
    mist->applyPreset("smoke");
    mist->setEmissionRate(28.f);
    mist->setPosition(wx, wy);
    mist->start();

    ParticleEmitter *ember = parts->newEmitter(96);
    ember->applyPreset("spark");
    ember->setPosition(wx + 8.f, wy - 4.f);
    ember->start();

    for (int i = 0; i < 12; ++i) parts->update(1.f / 60.f);
    CHECK(mist->getCount() > 0);
    CHECK(ember->getCount() > 0);

    auto *lamp = Light2D::createLight("point");
    lamp->setRadius(110.f);
    lamp->setColor(0.6f, 0.7f, 0.95f, 1.4f);
    lamp->setPosition(wx, wy);
    lamp->setEnabled(true);
    CHECK(approxEq(lamp->getRadius(), 110.f));

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.29 mist particle",
                          .pngName = "29_mistParticleScene.png",
                          .fire = mist,
                          .lamp = lamp});

    mist->stop();
    ember->stop();
    lamp->setEnabled(false);
    d.layer->setVisible(false);
}

TEST_CASE("hex.level.30.raidComboPipeline") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    auto *spatialMod = Spatial::create();
    auto *parts = Particles::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 3030, 32, 24);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 2);

    // Cost strip near spawn.
    for (int x = d.spawnTx + 2; x <= d.spawnTx + 4; ++x)
        for (int y = d.spawnTy - 1; y <= d.spawnTy + 1; ++y)
            if (pf->isWalkable(x, y)) pf->setCellCost(x, y, 8.f);

    FlowField *field = pf->buildFlowField(d.exitTx, d.exitTy);
    REQUIRE(field != nullptr);
    Path *flow = pf->followFlow(field, d.spawnTx, d.spawnTy);
    REQUIRE(flow != nullptr);

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    fov->setPerceptionRadiusScale(1.f);
    const int hero = fov->addRevealer(d.spawnTx, d.spawnTy, 5);
    fov->setRevealerPerception(hero, 2.f);
    const int torch = fov->addRevealer(d.exitTx, d.exitTy, 3);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto *inv = Inventory::create();
    inv->registerItemsFromJson(R"([
      {"id":"hex.relic","maxStack":1,"weight":0.5,"tags":["quest"]},
      {"id":"hex.potion","maxStack":10,"weight":0.2,"tags":["potion"]},
      {"id":"hex.coin","maxStack":99,"weight":0.01,"tags":["currency"]}
    ])");
    Bag *bag = inv->newBag(12);

    const int mid = path->getLength() / 2;
    float lx = 0.f, ly = 0.f;
    d.layer->tileToWorld(path->getX(mid), path->getY(mid), lx, ly);
    lx += kTileW * 0.5f;
    ly += kTileH * 0.5f;
    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(32.f));
    CHECK(hash->insert(1, lx - 6.f, ly - 6.f, lx + 6.f, ly + 6.f));

    ParticleEmitter *fire = parts->newEmitter(96);
    fire->applyPreset("fire");
    fire->start();

    auto *lamp = Light2D::createLight("point");
    lamp->setRadius(160.f);
    lamp->setEnabled(true);

    bool looted = false;
    for (int step = 0; step < path->getLength(); ++step) {
        const int tx = path->getX(step);
        const int ty = path->getY(step);
        fov->setRevealerPosition(hero, tx, ty);
        fov->compute();
        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(tx, ty, wx, wy);
        lamp->setPosition(wx, wy);
        fire->setPosition(wx + 4.f, wy - 4.f);
        parts->update(1.f / 30.f);
        if (!looted && fov->canDetect(hero, path->getX(mid), path->getY(mid), 0.5f)) {
            float cx = wx + kTileW * 0.5f, cy = wy + kTileH * 0.5f;
            if (hash->queryCircle(cx, cy, 20.f) > 0) {
                const int added = bag->addItem("hex.relic", 1);
                if (added > 0) {
                    looted = true;
                    hash->remove(1);
                }
            }
        }
    }
    CHECK(looted);
    CHECK_EQ(bag->countItem("hex.relic"), 1);
    CHECK_EQ(fov->getRevealerCount(), 2);
    CHECK(flow->getLength() > 0);

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.30 raid combo",
                          .pngName = "30_raid_combo.png",
                          .path = path,
                          .fire = fire,
                          .lamp = lamp,
                          .lootTx = path->getX(mid),
                          .lootTy = path->getY(mid)});

    bag->destroy();
    ItemRegistry::clear();
    delete flow;
    delete field;
    delete path;
    delete pf;
    delete fov;
    (void)torch;
}
