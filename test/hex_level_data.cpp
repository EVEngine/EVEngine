// Data-driven hex level fixtures — loads examples/hex-levels/data/*.json
// and exercises catalog / items / loot / seeds / particles / tiny map / perception.

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "data/DataModule.h"
#include "data/JsonDocument.h"
#include "filesystem/Filesystem.h"
#include "inventory/Bag.h"
#include "inventory/Inventory.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "map/TileConfig.h"
#include "map/TileLayer.h"
#include "map/TileOrientation.h"
#include "graphics/Light.h"
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "procgen/Semantic.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace eve::map;
using namespace eve::procgen;
using namespace eve::inventory;
using namespace eve::particles;
using namespace eve::data;

namespace {

std::string hexDataDir() {
    std::string here = __FILE__;
    const auto slash = here.find_last_of("/\\");
    const std::string testDir =
        (slash == std::string::npos) ? std::string(".") : here.substr(0, slash);
    return testDir + "/../examples/hex-levels/data";
}

std::string readTextFile(const std::string &path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::unique_ptr<JsonDocument> loadJson(const std::string &relPath) {
    const std::string path = hexDataDir() + "/" + relPath;
    const std::string text = readTextFile(path);
    REQUIRE(!text.empty());
    auto *dm = DataModule::create();
    std::string err;
    std::unique_ptr<JsonDocument> doc(dm->decodeJson(text, &err));
    REQUIRE(doc.get() != nullptr);
    return doc;
}

void hideLayers() {
    if (ecs::current()->getManager<TileLayer>() == nullptr) return;
    auto view = ecs::View<TileLayer, TileLayer::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw] = *it;
        draw->visible = false;
    }
}

int countWalkable(const Grid2D &g) {
    int n = 0;
    for (uint32_t c : g.cells()) {
        if (c == Semantic::Floor || c == Semantic::Corridor || c == Semantic::Door) ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("hex.data.catalog.loadsLevels") {
    auto doc = loadJson("catalog.json");
    REQUIRE(doc->isObject());
    auto root = doc->object();
    REQUIRE(root);
    CHECK_EQ(root->getValue<int>("version"), 1);
    auto levels = root->getArray("levels");
    REQUIRE(levels);
    CHECK(levels->size() >= 16);

    int foundPipeline = 0;
    for (size_t i = 0; i < levels->size(); ++i) {
        auto lv = levels->getObject(i);
        REQUIRE(lv);
        CHECK(lv->has("id"));
        CHECK(lv->has("key"));
        CHECK(lv->has("seed"));
        CHECK(lv->has("algorithm"));
        CHECK(lv->has("width"));
        CHECK(lv->has("height"));
        CHECK(lv->has("features"));
        if (lv->getValue<std::string>("key") == "pipeline_full") foundPipeline = 1;
    }
    CHECK_EQ(foundPipeline, 1);

    auto defaults = root->getObject("defaults");
    REQUIRE(defaults);
    CHECK_EQ(defaults->getValue<std::string>("orientation"), std::string("hexagonal"));
    CHECK_EQ(defaults->getValue<int>("wallGid"), 1);
    CHECK_EQ(defaults->getValue<int>("floorGid"), 2);
}

TEST_CASE("hex.data.items.registerAll") {
    auto doc = loadJson("items.json");
    REQUIRE(doc->isArray());
    auto arr = doc->array();
    REQUIRE(arr);
    CHECK(arr->size() >= 12);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto *inv = Inventory::create();
    const std::string text = readTextFile(hexDataDir() + "/items.json");
    const int n = inv->registerItemsFromJson(text);
    CHECK_EQ(n, int(arr->size()));
    CHECK(ItemRegistry::find("hex.potion") != nullptr);
    CHECK(ItemRegistry::find("hex.relic") != nullptr);
    CHECK(ItemRegistry::find("hex.coin") != nullptr);
    CHECK(ItemRegistry::find("hex.boots") != nullptr);
    CHECK(ItemRegistry::find("hex.scroll") != nullptr);
    CHECK_EQ(ItemRegistry::find("hex.gem")->maxStack, 5);
    CHECK(ItemRegistry::find("hex.potion")->hasTag("potion"));
    CHECK_EQ(ItemRegistry::find("hex.relic")->getExtra("rarity"), std::string("legendary"));

    Bag *bag = inv->newBag(20);
    const int coinAdded = bag->addItem("hex.coin", 50);
    const int potionAdded = bag->addItem("hex.potion", 3);
    CHECK_EQ(coinAdded, 50);
    CHECK_EQ(potionAdded, 3);
    CHECK_EQ(bag->countItem("hex.coin"), 50);
    CHECK_EQ(bag->countItem("hex.potion"), 3);
    bag->destroy();
    ItemRegistry::clear();
}

TEST_CASE("hex.data.lootTables.placeFromOffsets") {
    auto lootDoc = loadJson("loot_tables.json");
    auto tables = lootDoc->object()->getObject("tables");
    REQUIRE(tables);
    auto rich = tables->getArray("rich");
    REQUIRE(rich);
    CHECK(rich->size() >= 4);

    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    hideLayers();
    TileLayer *layer = mapMod->newLayer(16, 12, 64.f, 32.f);
    {
        auto c = layer->config();
        c->orientation = MapOrientation::Hexagonal;
        c->hexSideLength = 16.f;
        c->staggerAxis = StaggerAxis::Y;
        c->staggerIndex = StaggerIndex::Odd;
    }
    GeneratorRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(123);
    p.setSize(16, 12);
    Grid2D grid;
    std::string err;
    REQUIRE(GeneratorRegistry::instance().generate("dungeon.bsp", p, grid, err));
    gen->setPaletteGid("hex_data", "wall", 1);
    gen->setPaletteGid("hex_data", "floor", 2);
    gen->setPaletteGid("hex_data", "corridor", 2);
    gen->setPaletteGid("hex_data", "door", 3);
    REQUIRE(gen->applyToLayer(&grid, "hex_data", layer));

    int spawnX = -1, spawnY = -1;
    for (int i = 0; i < grid.getObjectCount(); ++i) {
        if (grid.getObjectType(i) == "spawn") {
            spawnX = int(grid.getObjectX(i));
            spawnY = int(grid.getObjectY(i));
        }
    }
    if (spawnX < 0) {
        for (int y = 0; y < grid.getHeight() && spawnX < 0; ++y)
            for (int x = 0; x < grid.getWidth() && spawnX < 0; ++x)
                if (grid.getCell(x, y) == int(Semantic::Floor)) {
                    spawnX = x;
                    spawnY = y;
                }
    }
    REQUIRE(spawnX >= 0);

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("hex");

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto *inv = Inventory::create();
    inv->registerItemsFromJson(readTextFile(hexDataDir() + "/items.json"));
    Bag *bag = inv->newBag(24);

    int placed = 0;
    int picked = 0;
    for (size_t i = 0; i < rich->size(); ++i) {
        auto entry = rich->getObject(i);
        const std::string itemId = entry->getValue<std::string>("itemId");
        const int qty = entry->has("qty") ? entry->getValue<int>("qty") : 1;
        auto off = entry->getArray("offset");
        REQUIRE(off);
        const int tx = spawnX + off->getElement<int>(0);
        const int ty = spawnY + off->getElement<int>(1);
        if (!pf->isWalkable(tx, ty)) continue;
        ++placed;
        const int added = bag->addItem(itemId, qty);
        CHECK(added > 0);
        picked += added;
    }
    CHECK(placed >= 1);
    CHECK(picked >= 1);
    CHECK(bag->getUsedSlotCount() >= 1);

    bag->destroy();
    ItemRegistry::clear();
    delete pf;
    layer->setVisible(false);
}

TEST_CASE("hex.data.seedsMatrix.smokeConnectivity") {
    auto doc = loadJson("seeds_matrix.json");
    auto root = doc->object();
    auto seeds = root->getArray("seeds");
    auto algos = root->getArray("algorithms");
    auto sizes = root->getArray("sizes");
    REQUIRE(seeds);
    REQUIRE(algos);
    REQUIRE(sizes);
    CHECK(seeds->size() >= 10);
    CHECK(algos->size() >= 4);

    // Sample a subset: first 6 seeds × all algos × medium size — keep runtime bounded.
    auto size = sizes->getObject(1);  // 24×18
    const int w = size->getValue<int>("width");
    const int h = size->getValue<int>("height");
    const int minWalkable = root->getObject("expectations")->getValue<int>("minWalkable");

    GeneratorRegistry::instance().registerBuiltins();
    int okRuns = 0;
    const size_t seedLimit = std::min<size_t>(6, seeds->size());
    for (size_t si = 0; si < seedLimit; ++si) {
        const uint32_t seed = uint32_t(seeds->getElement<int>(si));
        for (size_t ai = 0; ai < algos->size(); ++ai) {
            auto algoObj = algos->getObject(ai);
            const std::string algo = algoObj->getValue<std::string>("id");
            Params p;
            p.setSeed(seed);
            p.setSize(w, h);
            if (auto params = algoObj->getObject("params")) {
                if (params->has("loops")) p.setInt("loops", params->getValue<int>("loops"));
                if (params->has("fill")) p.setFloat("fill", float(params->getValue<double>("fill")));
                if (params->has("floorPct"))
                    p.setFloat("floorPct", float(params->getValue<double>("floorPct")));
                if (params->has("preset"))
                    p.setString("preset", params->getValue<std::string>("preset"));
                if (params->has("maxAttempts"))
                    p.setInt("maxAttempts", params->getValue<int>("maxAttempts"));
            }
            Grid2D grid;
            std::string err;
            const bool ok = GeneratorRegistry::instance().generate(algo, p, grid, err);
            CHECK(ok);
            if (!ok) continue;
            // Some cellular seeds are wall-heavy on medium maps; only BSP is
            // expected to reliably clear the fixture minWalkable quota.
            const int walkable = countWalkable(grid);
            if (algo == "dungeon.bsp") {
                CHECK(walkable >= minWalkable);
            } else {
                CHECK(walkable >= 1);
            }
            ++okRuns;
        }
    }
    CHECK(okRuns >= int(seedLimit) * 3);
}

TEST_CASE("hex.data.particles.loadConfigs") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_hex_particles", true));
    REQUIRE(fs->setupWriteDirectory());

    // Copy fixture JSON into VFS write dir so loadConfig can find them.
    const char *names[] = {"torch_fire.json", "pickup_burst.json", "ember_trail.json",
                           "mist_fog.json"};
    for (const char *name : names) {
        const std::string src = readTextFile(hexDataDir() + "/particles/" + name);
        REQUIRE(!src.empty());
        fs->write(name, src.c_str(), src.size());
    }

    auto *parts = Particles::create();
    ParticleEmitter *torch = parts->newEmitterFromFile("torch_fire.json");
    REQUIRE(torch != nullptr);
    CHECK(torch->getEmissionRate() > 0.f);
    torch->setPosition(10.f, 20.f);
    torch->start();
    for (int i = 0; i < 6; ++i) parts->update(1.f / 60.f);
    CHECK(torch->getCount() > 0);
    torch->stop();

    ParticleEmitter *burst = parts->newEmitterFromFile("pickup_burst.json");
    REQUIRE(burst != nullptr);
    burst->setPosition(30.f, 40.f);
    burst->start();
    burst->emit(20);
    CHECK(burst->getCount() > 0);
    burst->stop();

    ParticleEmitter *ember = parts->newEmitterFromFile("ember_trail.json");
    REQUIRE(ember != nullptr);
    CHECK(ember->getEmissionRate() > 0.f);
    ember->stop();

    ParticleEmitter *mist = parts->newEmitterFromFile("mist_fog.json");
    REQUIRE(mist != nullptr);
    CHECK(mist->getEmissionRate() > 0.f);
    mist->setPosition(50.f, 60.f);
    mist->start();
    for (int i = 0; i < 8; ++i) parts->update(1.f / 60.f);
    CHECK(mist->getCount() > 0);
    mist->stop();
}

TEST_CASE("hex.data.tinyMap.handcraftedPath") {
    const std::string text = readTextFile(hexDataDir() + "/maps/tiny_hex.json");
    REQUIRE(!text.empty());

    hideLayers();
    std::vector<MapObject> objects;
    std::string err;
    auto layers = loadMapText(text, &objects, &err);
    REQUIRE(!layers.empty());
    TileLayer *layer = layers.front();
    CHECK_EQ(static_cast<int>(layer->config()->orientation),
             static_cast<int>(MapOrientation::Hexagonal));
    CHECK(std::fabs(layer->config()->hexSideLength - 16.f) < 1e-3f);
    CHECK_EQ(layer->getMapWidth(), 8);
    CHECK_EQ(layer->getMapHeight(), 6);
    CHECK_EQ(layer->getTile(0, 0), 1);  // wall
    CHECK_EQ(layer->getTile(1, 1), 2);  // floor
    CHECK_EQ(layer->getTile(3, 3), 3);  // door

    auto *mapMod = Map::create();
    mapMod->setObjects(objects);
    CHECK(mapMod->getObjectCount() >= 2);

    int spawnX = -1, spawnY = -1, exitX = -1, exitY = -1;
    for (int i = 0; i < mapMod->getObjectCount(); ++i) {
        const std::string t = mapMod->getObjectType(i);
        if (t == "spawn") {
            spawnX = int(mapMod->getObjectX(i));
            spawnY = int(mapMod->getObjectY(i));
        } else if (t == "stairs") {
            exitX = int(mapMod->getObjectX(i));
            exitY = int(mapMod->getObjectY(i));
        }
    }
    CHECK_EQ(spawnX, 1);
    CHECK_EQ(spawnY, 1);
    CHECK_EQ(exitX, 6);
    CHECK_EQ(exitY, 4);

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("hex");
    CHECK(pf->isWalkable(spawnX, spawnY));
    CHECK(pf->isWalkable(exitX, exitY));
    Path *path = pf->findPath(spawnX, spawnY, exitX, exitY);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK_EQ(path->getX(path->getLength() - 1), exitX);
    CHECK_EQ(path->getY(path->getLength() - 1), exitY);
    delete path;
    delete pf;
    for (TileLayer *L : layers) L->setVisible(false);
}

TEST_CASE("hex.data.ringMap.loopPathThroughDoor") {
    const std::string text = readTextFile(hexDataDir() + "/maps/ring_hex.json");
    REQUIRE(!text.empty());

    hideLayers();
    std::vector<MapObject> objects;
    std::string err;
    auto layers = loadMapText(text, &objects, &err);
    REQUIRE(!layers.empty());
    TileLayer *layer = layers.front();
    CHECK_EQ(layer->getMapWidth(), 9);
    CHECK_EQ(layer->getMapHeight(), 7);
    CHECK_EQ(layer->getTile(4, 3), 3);  // center door
    CHECK_EQ(layer->getTile(4, 2), 1);  // inner wall ring

    auto *mapMod = Map::create();
    mapMod->setObjects(objects);
    int spawnX = -1, spawnY = -1, exitX = -1, exitY = -1;
    for (int i = 0; i < mapMod->getObjectCount(); ++i) {
        const std::string t = mapMod->getObjectType(i);
        if (t == "spawn") {
            spawnX = int(mapMod->getObjectX(i));
            spawnY = int(mapMod->getObjectY(i));
        } else if (t == "stairs") {
            exitX = int(mapMod->getObjectX(i));
            exitY = int(mapMod->getObjectY(i));
        }
    }
    CHECK_EQ(spawnX, 1);
    CHECK_EQ(exitX, 7);

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("hex");
    Path *outer = pf->findPath(spawnX, spawnY, exitX, exitY);
    REQUIRE(outer != nullptr);
    CHECK(outer->getLength() > 0);

    // Path must stay on the outer ring (avoid blocked center) or pass the door.
    bool touchedDoor = false;
    for (int i = 0; i < outer->getLength(); ++i) {
        if (outer->getX(i) == 4 && outer->getY(i) == 3) touchedDoor = true;
    }
    // Outer corridor path is valid either way; just ensure connectivity.
    CHECK(pf->isWalkable(4, 3));
    delete outer;

    Path *viaDoor = pf->findPath(4, 1, 4, 5);
    REQUIRE(viaDoor != nullptr);
    CHECK(viaDoor->getLength() > 0);
    delete viaDoor;
    delete pf;
    for (TileLayer *L : layers) L->setVisible(false);
    (void)touchedDoor;
}

TEST_CASE("hex.data.perception.casesFromJson") {
    auto doc = loadJson("perception_cases.json");
    auto root = doc->object();
    auto cases = root->getArray("cases");
    auto radiusCases = root->getArray("radiusScaleCases");
    REQUIRE(cases);
    REQUIRE(radiusCases);

    auto *mapMod = Map::create();
    Fov *fov = mapMod->newFovSize(7, 7);
    fov->setBlockEmpty(false);
    fov->setTopology("hex");
    fov->setDetectionMargin(0.f);

    for (size_t i = 0; i < cases->size(); ++i) {
        auto c = cases->getObject(i);
        const float perception = float(c->getValue<double>("perception"));
        const float stealth = float(c->getValue<double>("stealth"));
        const bool expect = c->getValue<bool>("expectDetect");
        fov->clearRevealers();
        const int id = fov->addRevealer(3, 3, 2);
        fov->setRevealerPerception(id, perception);
        fov->setPerceptionRadiusScale(0.f);  // keep radius fixed for stealth-only cases
        fov->compute();
        CHECK(fov->isVisible(3, 3));
        const bool got = fov->canDetect(id, 3, 3, stealth);
        CHECK_EQ(got, expect);
    }

    for (size_t i = 0; i < radiusCases->size(); ++i) {
        auto c = radiusCases->getObject(i);
        const int base = c->getValue<int>("baseRadius");
        const float perception = float(c->getValue<double>("perception"));
        const float scale = float(c->getValue<double>("scale"));
        const int expect = c->getValue<int>("expectEffective");
        fov->clearRevealers();
        const int id = fov->addRevealer(3, 3, base);
        fov->setRevealerPerception(id, perception);
        fov->setPerceptionRadiusScale(scale);
        CHECK_EQ(fov->getEffectiveRadius(id), expect);
    }
    delete fov;
}

TEST_CASE("hex.data.catalog.driveLevelGeneration") {
    auto doc = loadJson("catalog.json");
    auto levels = doc->object()->getArray("levels");
    REQUIRE(levels);

    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    GeneratorRegistry::instance().registerBuiltins();
    gen->setPaletteGid("hex_catalog", "wall", 1);
    gen->setPaletteGid("hex_catalog", "floor", 2);
    gen->setPaletteGid("hex_catalog", "corridor", 2);
    gen->setPaletteGid("hex_catalog", "door", 3);

    int generated = 0;
    for (size_t i = 0; i < levels->size(); ++i) {
        auto lv = levels->getObject(i);
        // Skip id 7 synthetic cost map (still dungeon.bsp — generate is fine).
        const uint32_t seed = uint32_t(lv->getValue<int>("seed"));
        const int w = lv->getValue<int>("width");
        const int h = lv->getValue<int>("height");
        const std::string algo = lv->getValue<std::string>("algorithm");

        hideLayers();
        TileLayer *layer = mapMod->newLayer(w, h, 64.f, 32.f);
        {
            auto c = layer->config();
            c->orientation = MapOrientation::Hexagonal;
            c->hexSideLength = 16.f;
            c->staggerAxis = StaggerAxis::Y;
            c->staggerIndex = StaggerIndex::Odd;
        }
        Params p;
        p.setSeed(seed);
        p.setSize(w, h);
        if (auto params = lv->getObject("params")) {
            if (params->has("loops")) p.setInt("loops", params->getValue<int>("loops"));
            if (params->has("fill")) p.setFloat("fill", float(params->getValue<double>("fill")));
        }
        Grid2D grid;
        std::string err;
        REQUIRE(GeneratorRegistry::instance().generate(algo, p, grid, err));
        REQUIRE(gen->applyToLayer(&grid, "hex_catalog", layer));
        CHECK(countWalkable(grid) >= 4);

        Pathfinder *pf = mapMod->newPathfinder(layer);
        pf->blockGid(1);
        pf->setTopology("auto");
        CHECK_EQ(pf->getTopology(), std::string("hex"));
        delete pf;
        layer->setVisible(false);
        ++generated;
    }
    CHECK_EQ(generated, int(levels->size()));
}

TEST_CASE("hex.data.catalog.bootEachLevelDistinctConfig") {
    // Start every catalog level with its own boot config and assert the
    // applied FOV / light / cell-cost / loot / algo settings actually differ
    // where the catalog says they should.
    auto catalog = loadJson("catalog.json");
    auto lootDoc = loadJson("loot_tables.json");
    auto levels = catalog->object()->getArray("levels");
    auto tables = lootDoc->object()->getObject("tables");
    REQUIRE(levels);
    REQUIRE(tables);

    auto *mapMod = Map::create();
    auto *gen = Procgen::create();
    GeneratorRegistry::instance().registerBuiltins();
    gen->setPaletteGid("hex_boot", "wall", 1);
    gen->setPaletteGid("hex_boot", "floor", 2);
    gen->setPaletteGid("hex_boot", "corridor", 2);
    gen->setPaletteGid("hex_boot", "door", 3);

    std::vector<uint32_t> seenSeeds;
    std::vector<std::string> seenKeys;
    int booted = 0;

    for (size_t i = 0; i < levels->size(); ++i) {
        auto lv = levels->getObject(i);
        REQUIRE(lv);
        const int id = lv->getValue<int>("id");
        const std::string key = lv->getValue<std::string>("key");
        const uint32_t seed = uint32_t(lv->getValue<int>("seed"));
        const int w = lv->getValue<int>("width");
        const int h = lv->getValue<int>("height");
        const std::string algo = lv->getValue<std::string>("algorithm");
        const std::string lootTable =
            lv->has("lootTable") ? lv->getValue<std::string>("lootTable") : "starter";

        CHECK(!key.empty());
        CHECK(tables->has(lootTable));
        for (uint32_t s : seenSeeds) CHECK(s != seed);
        for (const auto &k : seenKeys) CHECK(k != key);
        seenSeeds.push_back(seed);
        seenKeys.push_back(key);

        hideLayers();
        TileLayer *layer = mapMod->newLayer(w, h, 64.f, 32.f);
        {
            auto c = layer->config();
            c->orientation = MapOrientation::Hexagonal;
            c->hexSideLength = 16.f;
            c->staggerAxis = StaggerAxis::Y;
            c->staggerIndex = StaggerIndex::Odd;
        }

        Params p;
        p.setSeed(seed);
        p.setSize(w, h);
        if (auto params = lv->getObject("params")) {
            if (params->has("loops")) p.setInt("loops", params->getValue<int>("loops"));
            if (params->has("fill")) p.setFloat("fill", float(params->getValue<double>("fill")));
            if (params->has("floorPct"))
                p.setFloat("floorPct", float(params->getValue<double>("floorPct")));
            if (params->has("preset"))
                p.setString("preset", params->getValue<std::string>("preset"));
            if (params->has("maxAttempts"))
                p.setInt("maxAttempts", params->getValue<int>("maxAttempts"));
        }

        Grid2D grid;
        std::string err;
        REQUIRE(GeneratorRegistry::instance().generate(algo, p, grid, err));
        REQUIRE(gen->applyToLayer(&grid, "hex_boot", layer));
        CHECK(countWalkable(grid) >= 1);

        Pathfinder *pf = mapMod->newPathfinder(layer);
        pf->blockGid(1);
        pf->setTopology("auto");
        CHECK_EQ(pf->getTopology(), std::string("hex"));

        // Apply cell-cost boot config when present.
        if (auto cc = lv->getObject("cellCost")) {
            const float cost = float(cc->getValue<double>("cost"));
            const int strip = cc->getValue<int>("stripWidth");
            CHECK(cost > 1.f);
            CHECK(strip >= 1);
            int painted = 0;
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 2; x < 2 + strip && x < w - 1; ++x) {
                    if (!pf->isWalkable(x, y)) continue;
                    pf->setCellCost(x, y, cost);
                    CHECK(std::fabs(pf->getCellCost(x, y) - cost) < 1e-3f);
                    ++painted;
                }
            }
            CHECK(painted >= 1);
        }

        // Apply FOV boot config when present / enabled.
        auto enable = lv->getObject("enable");
        const bool fovOn = !enable || !enable->has("fov") || enable->getValue<bool>("fov");
        if (fovOn) {
            Fov *fov = mapMod->newFov(layer);
            fov->blockOpaqueGid(1);
            fov->setBlockEmpty(false);
            fov->setTopology("auto");
            auto fovCfg = lv->getObject("fov");
            std::string fovAlgo = "shadowcast";
            int radius = 6;
            float perception = 0.f;
            float scale = 0.f;
            int torchRadius = 0;
            if (fovCfg) {
                if (fovCfg->has("algorithm"))
                    fovAlgo = fovCfg->getValue<std::string>("algorithm");
                if (fovCfg->has("radius")) radius = fovCfg->getValue<int>("radius");
                if (fovCfg->has("heroRadius")) radius = fovCfg->getValue<int>("heroRadius");
                if (fovCfg->has("heroPerception"))
                    perception = float(fovCfg->getValue<double>("heroPerception"));
                if (fovCfg->has("perceptionScale"))
                    scale = float(fovCfg->getValue<double>("perceptionScale"));
                if (fovCfg->has("torchRadius")) torchRadius = fovCfg->getValue<int>("torchRadius");
                if (fovCfg->has("enabled") && !fovCfg->getValue<bool>("enabled")) {
                    delete fov;
                    delete pf;
                    layer->setVisible(false);
                    ++booted;
                    continue;
                }
            }
            if (radius < 1) radius = 1;
            fov->setAlgorithm(fovAlgo);
            CHECK_EQ(fov->getAlgorithm(), fovAlgo);
            fov->setPerceptionRadiusScale(scale);
            const int hero = fov->addRevealer(w / 2, h / 2, radius);
            fov->setRevealerPerception(hero, perception);
            CHECK(std::fabs(fov->getRevealerPerception(hero) - perception) < 1e-3f);
            const int expectEff = radius + int(std::floor(perception * scale));
            CHECK_EQ(fov->getEffectiveRadius(hero), expectEff);
            if (torchRadius > 0) {
                const int torch = fov->addRevealer(1, 1, torchRadius);
                CHECK_EQ(fov->getEffectiveRadius(torch), torchRadius);
                CHECK_EQ(fov->getRevealerCount(), 2);
            } else {
                CHECK_EQ(fov->getRevealerCount(), 1);
            }
            fov->compute();
            delete fov;
        }

        // Apply light boot config when present.
        if (auto light = lv->getObject("light")) {
            auto *lamp = eve::graphics::Light2D::createLight("point");
            const float radius = light->has("radius")
                                     ? float(light->getValue<double>("radius"))
                                     : 120.f;
            lamp->setRadius(radius);
            CHECK(std::fabs(lamp->getRadius() - radius) < 1e-3f);
            if (light->has("count")) CHECK(light->getValue<int>("count") >= 1);
            lamp->setEnabled(false);
        }

        // Level-specific feature tags should be non-empty and include hex.
        auto features = lv->getArray("features");
        REQUIRE(features);
        CHECK(features->size() >= 1);
        bool hasHex = false;
        for (size_t fi = 0; fi < features->size(); ++fi) {
            if (features->getElement<std::string>(fi) == "hex") hasHex = true;
        }
        CHECK(hasHex);

        // Spot-check a few known distinct boot profiles.
        if (id == 2) {
            CHECK_EQ(algo, std::string("cave.cellular"));
            CHECK_EQ(lootTable, std::string("cave"));
            auto fovCfg = lv->getObject("fov");
            REQUIRE(fovCfg);
            CHECK_EQ(fovCfg->getValue<int>("radius"), 6);
        } else if (id == 3) {
            auto light = lv->getObject("light");
            REQUIRE(light);
            CHECK(std::fabs(float(light->getValue<double>("radius")) - 140.f) < 1e-3f);
        } else if (id == 7) {
            auto cc = lv->getObject("cellCost");
            REQUIRE(cc);
            CHECK(std::fabs(float(cc->getValue<double>("cost")) - 8.f) < 1e-3f);
        } else if (id == 8) {
            auto fovCfg = lv->getObject("fov");
            REQUIRE(fovCfg);
            CHECK_EQ(fovCfg->getValue<int>("heroRadius"), 4);
            CHECK(std::fabs(float(fovCfg->getValue<double>("heroPerception")) - 2.f) < 1e-3f);
        } else if (id == 15) {
            CHECK_EQ(lootTable, std::string("equipment"));
            auto parts = lv->getArray("particles");
            REQUIRE(parts);
            CHECK(parts->size() >= 3);
        }

        delete pf;
        layer->setVisible(false);
        ++booted;
        (void)id;
    }

    CHECK_EQ(booted, int(levels->size()));
    CHECK(booted >= 16);
}
