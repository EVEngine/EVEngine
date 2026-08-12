// Hex dungeon test levels — cross-module simulation for engine feature QA.
//
// Levels (each TEST_CASE is an independent scenario; pipeline combines them):
//  01 procgen + hex tilemap + A* / Flow Field pathfinding
//  02 2D dynamic FOV (shadowcast, hex topology, explored memory)
//  03 2D dynamic lighting (point light + lit sprite collection)
//  04 object pickup via SpatialHash2D collision + Inventory
//  05 particle emitters (torch / spark on pickup)
//  pipeline: full crawl through one seeded hex dungeon

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/DrawItem2D.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem.h"
#include "graphics/Texture.h"
#include "inventory/Bag.h"
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
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"
#include "particles/ParticleSystem.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "procgen/Semantic.h"
#include "spatial/Spatial.h"
#include "spatial/SpatialHash2D.h"

#include <cmath>
#include <cstdint>
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

    delete group;
    delete field;
    delete path;
    delete pf;
}

TEST_CASE("hex.level.02.dynamicFov") {
    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    HexDungeon d = buildHexDungeon(mapMod, gen, 7, 28, 20, "cave.cellular");

    Fov *fov = mapMod->newFov(d.layer);
    REQUIRE(fov != nullptr);
    fov->blockOpaqueGid(kWallGid);
    fov->setBlockEmpty(false);
    fov->setTopology("auto");
    CHECK_EQ(fov->getTopology(), std::string("hex"));
    fov->setAlgorithm("shadowcast");
    fov->setRadiusMetric("euclidean");

    const int id = fov->addRevealer(d.spawnTx, d.spawnTy, 6);
    fov->compute();
    CHECK(fov->isVisible(d.spawnTx, d.spawnTy));
    CHECK(fov->isExplored(d.spawnTx, d.spawnTy));

    // Move along A* path; memory should retain explored cells after leaving.
    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 1);

    const int mid = path->getLength() / 2;
    const int midX = path->getX(mid);
    const int midY = path->getY(mid);
    fov->setRevealerPosition(id, midX, midY);
    fov->compute();
    CHECK(fov->isVisible(midX, midY));
    CHECK(fov->isExplored(d.spawnTx, d.spawnTy));  // memory

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

    delete wallFov;
    delete path;
    delete pf;
    delete fov;
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
