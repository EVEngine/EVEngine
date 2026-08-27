// Procgen cross-module scenarios — the same surfaces a game would drive:
//   1) WFC dungeon → TileLayer palette → A* / FlowField spawn→stairs delve
//   2) Marching Cubes terrain island + crystal props → mesh budgets (+ optional GPU)
//   3) WFC cave → A* expedition + FOV shadowcast
//   4) WFC terrain overworld → weighted biome travel + seed replay
//   5) Marching Cubes torus/sphere loot prop bake + cache bust

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
#include "map/FlowField.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "map/TileLayer.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/JsonExport.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace eve::procgen;
using namespace eve::map;
using namespace eve::graphics;

namespace {

struct ParamsLease {
    ProcgenParamsHandleRef handle{};
    ParamsLease() = default;
    explicit ParamsLease(ProcgenParamsHandleRef value) : handle(value) {}
    ParamsLease(const ParamsLease&) = delete;
    ParamsLease& operator=(const ParamsLease&) = delete;
    ParamsLease(ParamsLease&& other) noexcept : handle(other.handle) { other.handle = {}; }
    ParamsLease& operator=(ParamsLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        handle = other.handle;
        other.handle = {};
        return *this;
    }
    ~ParamsLease() { reset(); }
    void reset() noexcept {
        if (!handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("simulation params cleanup");
        handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<Params> view() const noexcept {
        return Procgen::resolve(handle);
    }
};

struct GridLease {
    ProcgenGridHandleRef handle{};
    GridLease() = default;
    explicit GridLease(ProcgenGridHandleRef value) : handle(value) {}
    GridLease(const GridLease&) = delete;
    GridLease& operator=(const GridLease&) = delete;
    GridLease(GridLease&& other) noexcept : handle(other.handle) { other.handle = {}; }
    GridLease& operator=(GridLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        handle = other.handle;
        other.handle = {};
        return *this;
    }
    ~GridLease() { reset(); }
    void reset() noexcept {
        if (!handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("simulation grid cleanup");
        handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<Grid2D> view() const noexcept {
        return Procgen::resolve(handle);
    }
};

struct OutputLease {
    Procgen* owner = nullptr;
    ProcgenOutputHandleRef handle{};
    OutputLease() = default;
    OutputLease(Procgen& proc, ProcgenOutputHandleRef value) : owner(&proc), handle(value) {}
    OutputLease(const OutputLease&) = delete;
    OutputLease& operator=(const OutputLease&) = delete;
    OutputLease(OutputLease&& other) noexcept : owner(other.owner), handle(other.handle) {
        other.owner = nullptr;
        other.handle = {};
    }
    OutputLease& operator=(OutputLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner = other.owner;
        handle = other.handle;
        other.owner = nullptr;
        other.handle = {};
        return *this;
    }
    ~OutputLease() { reset(); }
    void reset() noexcept {
        if (!owner || !handle.isValid()) return;
        auto result = owner->releaseOutput(handle);
        result.ignore("simulation output cleanup");
        owner = nullptr;
        handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<OutputSpec> view() const noexcept {
        return owner ? owner->resolveOutput(handle) : eve::script::Borrowed<OutputSpec>();
    }
};

OutputLease requireOutput(Procgen& proc) {
    auto result = proc.newOutputHandle();
    REQUIRE(result.ok());
    return OutputLease(proc, std::move(result).takeValue());
}

struct MeshBuildLease {
    Procgen* owner = nullptr;
    ProcgenMeshBuildHandleRef handle{};
    MeshBuildLease() = default;
    MeshBuildLease(Procgen& proc, ProcgenMeshBuildHandleRef value) : owner(&proc), handle(value) {}
    MeshBuildLease(const MeshBuildLease&) = delete;
    MeshBuildLease& operator=(const MeshBuildLease&) = delete;
    MeshBuildLease(MeshBuildLease&& other) noexcept : owner(other.owner), handle(other.handle) {
        other.owner = nullptr;
        other.handle = {};
    }
    MeshBuildLease& operator=(MeshBuildLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner = other.owner;
        handle = other.handle;
        other.owner = nullptr;
        other.handle = {};
        return *this;
    }
    ~MeshBuildLease() { reset(); }
    void reset() noexcept {
        if (!owner || !handle.isValid()) return;
        auto result = owner->releaseMeshBuild(handle);
        result.ignore("simulation mesh build cleanup");
        owner = nullptr;
        handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<MeshBuild> view() const noexcept {
        return owner ? owner->resolveMeshBuild(handle) : eve::script::Borrowed<MeshBuild>();
    }
};

ParamsLease requireParams(const Params& source) {
    auto result = Procgen::newParamsHandle();
    REQUIRE(result.ok());
    auto lease = ParamsLease(std::move(result).takeValue());
    auto view = lease.view();
    REQUIRE(view.isBound());
    *view = source;
    return lease;
}

GridLease requireGrid(Procgen& proc, const std::string& algorithm,
                      ProcgenParamsHandleRef params) {
    auto result = proc.generateHandle(algorithm, params);
    REQUIRE(result.ok());
    return GridLease(std::move(result).takeValue());
}

MeshBuildLease requireMeshBuild(Procgen& proc, const std::string& recipe,
                                ProcgenParamsHandleRef params) {
    auto result = proc.buildMeshHandle(recipe, params);
    REQUIRE(result.ok());
    return MeshBuildLease(proc, std::move(result).takeValue());
}

constexpr int kWallGid     = 1;
constexpr int kFloorGid    = 2;
constexpr int kCorridorGid = 3;
constexpr int kDoorGid     = 4;

bool approxEq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

void bindDungeonPalette(Procgen *mod, const std::string &name) {
    mod->setPaletteGid(name, "wall", kWallGid);
    mod->setPaletteGid(name, "floor", kFloorGid);
    mod->setPaletteGid(name, "corridor", kCorridorGid);
    mod->setPaletteGid(name, "door", kDoorGid);
}

bool borderIsWallLike(const Grid2D &g) {
    const int w = g.getWidth();
    const int h = g.getHeight();
    for (int x = 0; x < w; ++x) {
        if (g.getCell(x, 0) != int(Semantic::Wall)) return false;
        if (g.getCell(x, h - 1) != int(Semantic::Wall)) return false;
    }
    for (int y = 0; y < h; ++y) {
        if (g.getCell(0, y) != int(Semantic::Wall)) return false;
        if (g.getCell(w - 1, y) != int(Semantic::Wall)) return false;
    }
    return true;
}

bool terrainAdjacencyOk(const Grid2D &g) {
    static const int band[] = {int(Semantic::Water), int(Semantic::Sand),  int(Semantic::Grass),
                               int(Semantic::Dirt),  int(Semantic::Stone), int(Semantic::Snow)};
    std::set<std::pair<int, int>> allowed;
    for (int i = 0; i < 6; ++i) {
        allowed.insert({band[i], band[i]});
        if (i + 1 < 6) {
            int lo = band[i], hi = band[i + 1];
            if (lo > hi) std::swap(lo, hi);
            allowed.insert({lo, hi});
        }
    }
    auto ok = [&](int a, int b) {
        if (a > b) std::swap(a, b);
        return allowed.count({a, b}) > 0;
    };
    const int w = g.getWidth();
    const int h = g.getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int c = g.getCell(x, y);
            if (x + 1 < w && !ok(c, g.getCell(x + 1, y))) return false;
            if (y + 1 < h && !ok(c, g.getCell(x, y + 1))) return false;
        }
    }
    return true;
}

bool findObject(const Grid2D &g, const std::string &type, int &outX, int &outY) {
    for (int i = 0; i < g.getObjectCount(); ++i) {
        if (g.getObjectType(i) == type) {
            outX = int(g.getObjectX(i));
            outY = int(g.getObjectY(i));
            return true;
        }
    }
    return false;
}

bool pathOnLayer(Path *path, TileLayer *layer) {
    if (!path || !layer || path->getLength() <= 0) return false;
    for (int i = 0; i < path->getLength(); ++i) {
        const int gid = layer->getTile(path->getX(i), path->getY(i));
        if (gid == kWallGid || gid == 0) return false;
    }
    return true;
}

void meshBounds(const MeshBuild &m, float &minX, float &minY, float &minZ, float &maxX, float &maxY,
                float &maxZ) {
    minX = minY = minZ = 1e9f;
    maxX = maxY = maxZ = -1e9f;
    for (int i = 0; i < m.getVertexCount(); ++i) {
        minX = std::min(minX, m.getPositionX(i));
        minY = std::min(minY, m.getPositionY(i));
        minZ = std::min(minZ, m.getPositionZ(i));
        maxX = std::max(maxX, m.getPositionX(i));
        maxY = std::max(maxY, m.getPositionY(i));
        maxZ = std::max(maxZ, m.getPositionZ(i));
    }
}

float meshRadius(const MeshBuild &m) {
    float minX, minY, minZ, maxX, maxY, maxZ;
    meshBounds(m, minX, minY, minZ, maxX, maxY, maxZ);
    const float cx = 0.5f * (minX + maxX);
    const float cy = 0.5f * (minY + maxY);
    const float cz = 0.5f * (minZ + maxZ);
    float r2 = 0.f;
    for (int i = 0; i < m.getVertexCount(); ++i) {
        const float dx = m.getPositionX(i) - cx;
        const float dy = m.getPositionY(i) - cy;
        const float dz = m.getPositionZ(i) - cz;
        r2 = std::max(r2, dx * dx + dy * dy + dz * dz);
    }
    return std::sqrt(r2);
}

}  // namespace

/**
 * Scenario 1 — Dungeon delve (pixel RPG loop).
 *
 * Timeline (deterministic, headless):
 *  1) WFC dungeon.simple → semantic Grid2D (border walls, spawn + stairs)
 *  2) Palette → TileLayer GIDs (wall/floor/corridor/door)
 *  3) Pathfinder blocks walls; A* from spawn → stairs must succeed on walkable tiles
 *  4) FlowField from stairs; followFlow from spawn reaches the goal
 *  5) Same seed regenerate is byte-identical; JSON export contains algorithm meta
 */
TEST_CASE("procgen.simulation.dungeonDelve") {
    constexpr int kMapW = 36;
    constexpr int kMapH = 24;
    constexpr float kTile = 16.f;

    Procgen *proc = Procgen::create();
    Map *mapMod   = Map::create();
    REQUIRE(proc != nullptr);
    REQUIRE(mapMod != nullptr);

    bindDungeonPalette(proc, "sim_dungeon");

    Params p;
    p.setSeed(20260812);
    p.setSize(kMapW, kMapH);
    p.setString("preset", "dungeon");
    p.setInt("maxAttempts", 96);
    auto params = requireParams(p);

    // ------------------------------------------------------------------
    // 1) Generate semantic dungeon
    // ------------------------------------------------------------------
    auto gridLease = requireGrid(*proc, "wfc.simple", params.handle);
    auto grid = gridLease.view();
    REQUIRE(grid.isBound());
    CHECK_EQ(grid->getWidth(), kMapW);
    CHECK_EQ(grid->getHeight(), kMapH);
    CHECK_EQ(grid->getMeta("algorithm", ""), std::string("wfc.simple"));
    CHECK_EQ(grid->getMeta("preset", ""), std::string("dungeon"));

    // Enclosed border (WFC dungeon contract).
    for (int x = 0; x < kMapW; ++x) {
        CHECK_EQ(grid->getCell(x, 0), int(Semantic::Wall));
        CHECK_EQ(grid->getCell(x, kMapH - 1), int(Semantic::Wall));
    }
    for (int y = 0; y < kMapH; ++y) {
        CHECK_EQ(grid->getCell(0, y), int(Semantic::Wall));
        CHECK_EQ(grid->getCell(kMapW - 1, y), int(Semantic::Wall));
    }

    int spawnX = -1, spawnY = -1, stairsX = -1, stairsY = -1;
    REQUIRE(findObject(*grid, "spawn", spawnX, spawnY));
    REQUIRE(findObject(*grid, "stairs", stairsX, stairsY));
    CHECK(spawnX > 0);
    CHECK(spawnX < kMapW - 1);
    CHECK(spawnY > 0);
    CHECK(spawnY < kMapH - 1);
    CHECK(stairsX > 0);
    CHECK(stairsX < kMapW - 1);
    CHECK(stairsY > 0);
    CHECK(stairsY < kMapH - 1);
    // Spawn/stairs sit on walkable semantics.
    auto walkableSem = [](int s) {
        return s == int(Semantic::Floor) || s == int(Semantic::Corridor) || s == int(Semantic::Door);
    };
    CHECK(walkableSem(grid->getCell(spawnX, spawnY)));
    CHECK(walkableSem(grid->getCell(stairsX, stairsY)));

    // ------------------------------------------------------------------
    // 2) Apply palette → TileLayer (runtime map swap)
    // ------------------------------------------------------------------
    TileLayer *layer = mapMod->newLayer(kMapW, kMapH, kTile, kTile);
    REQUIRE(layer != nullptr);
    layer->setOrigin(0.f, 0.f);
    layer->setVisible(true);
    auto applyResult = proc->applyToLayer(gridLease.handle, "sim_dungeon", *layer);
    CHECK(applyResult.ok());
    CHECK_EQ(layer->getTile(0, 0), kWallGid);
    {
        const int spawnGid = layer->getTile(spawnX, spawnY);
        const bool spawnWalkable =
            spawnGid == kFloorGid || spawnGid == kCorridorGid || spawnGid == kDoorGid;
        CHECK(spawnWalkable);
    }

    // generateTo must produce the same GIDs for the same seed.
    TileLayer *layer2 = mapMod->newLayer(kMapW, kMapH, kTile, kTile);
    auto out = requireOutput(*proc);
    auto outView = out.view();
    REQUIRE(outView.isBound());
    outView->setTarget("tilelayer");
    outView->setLayer(layer2);
    outView->setPalette("sim_dungeon");
    auto generateToResult = proc->generateTo("wfc.simple", params.handle, out.handle);
    CHECK(generateToResult.ok());
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            CHECK_EQ(layer->getTile(x, y), layer2->getTile(x, y));
        }
    }

    // ------------------------------------------------------------------
    // 3) A* delve: spawn → stairs on walkable tiles
    // ------------------------------------------------------------------
    Pathfinder *pf = mapMod->newPathfinder(layer);
    REQUIRE(pf != nullptr);
    pf->blockGid(kWallGid);
    pf->setBlockEmpty(true);
    pf->setTopology("ortho4");
    CHECK(pf->isWalkable(spawnX, spawnY));
    CHECK(pf->isWalkable(stairsX, stairsY));

    Path *path = pf->findPath(spawnX, spawnY, stairsX, stairsY);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() >= 2);
    CHECK_EQ(path->getX(0), spawnX);
    CHECK_EQ(path->getY(0), spawnY);
    CHECK_EQ(path->getX(path->getLength() - 1), stairsX);
    CHECK_EQ(path->getY(path->getLength() - 1), stairsY);
    CHECK(pathOnLayer(path, layer));
    // Path should not jump over walls (4-neigh continuity).
    for (int i = 1; i < path->getLength(); ++i) {
        const int dx = std::abs(path->getX(i) - path->getX(i - 1));
        const int dy = std::abs(path->getY(i) - path->getY(i - 1));
        CHECK_EQ(dx + dy, 1);
    }

    // ------------------------------------------------------------------
    // 4) Party follow: FlowField from stairs, follow from spawn
    // ------------------------------------------------------------------
    FlowField *flow = pf->buildFlowField(stairsX, stairsY);
    REQUIRE(flow != nullptr);
    Path *flowPath = pf->followFlow(flow, spawnX, spawnY);
    REQUIRE(flowPath != nullptr);
    CHECK(flowPath->getLength() >= 2);
    CHECK_EQ(flowPath->getX(flowPath->getLength() - 1), stairsX);
    CHECK_EQ(flowPath->getY(flowPath->getLength() - 1), stairsY);
    CHECK(pathOnLayer(flowPath, layer));

    // ------------------------------------------------------------------
    // 5) Reproducibility + JSON export a designer/debug tool would save
    // ------------------------------------------------------------------
    auto gridBLease = requireGrid(*proc, "wfc.simple", params.handle);
    auto gridB = gridBLease.view();
    REQUIRE(gridB.isBound());
    CHECK(grid->cells() == gridB->cells());
    auto jsonResult = proc->gridToJson(gridLease.handle);
    REQUIRE(jsonResult.ok());
    const std::string json = std::move(jsonResult).takeValue();
    CHECK(json.find("wfc.simple") != std::string::npos);
    CHECK(json.find("spawn") != std::string::npos);
    CHECK(json.find("stairs") != std::string::npos);

    delete path;
    delete flowPath;
    delete flow;
    delete pf;
    layer->setVisible(false);
    layer2->setVisible(false);
}

/**
 * Scenario 2 — Crystal cave prop kit (3D mesh authoring loop).
 *
 * Timeline (deterministic, CPU-first):
 *  1) Marching Cubes terrain island = cave floor / rocky base
 *  2) Marching Cubes spheres = crystal clusters (different seeds/radii)
 *  3) Assert game-ready budgets: triangle counts, finite normals, AABB in unit cube
 *  4) Combined "scene" vertex budget stays under a soft real-time cap
 *  5) Optional GPU: upload island mesh via newMeshFromArrays (soft-skip without Vulkan window)
 */
TEST_CASE("procgen.simulation.crystalCave") {
    Procgen *proc = Procgen::create();
    REQUIRE(proc != nullptr);
    CHECK(proc->hasMeshRecipe("mesh.marchingcubes"));

    // ------------------------------------------------------------------
    // 1) Rocky island / cave floor
    // ------------------------------------------------------------------
    Params islandP;
    islandP.setSeed(77);
    islandP.setInt("resolution", 28);
    islandP.setString("field", "terrain");
    islandP.setFloat("scale", 1.8f);
    islandP.setInt("octaves", 3);
    islandP.setFloat("isolevel", 0.f);

    auto islandParams = requireParams(islandP);
    auto islandLease = requireMeshBuild(*proc, "mesh.marchingcubes", islandParams.handle);
    auto island = islandLease.view();
    REQUIRE(island.isBound());
    CHECK(island->getVertexCount() > 200);
    CHECK_EQ(island->getIndexCount() % 3, 0);
    CHECK_EQ(island->getMeta("field", ""), std::string("terrain"));

    float iminX, iminY, iminZ, imaxX, imayY, imaxZ;
    meshBounds(*island, iminX, iminY, iminZ, imaxX, imayY, imaxZ);
    CHECK(iminX >= -0.55f);
    CHECK(imaxX <= 0.55f);
    CHECK(iminY >= -0.55f);
    CHECK(imayY <= 0.55f);
    CHECK(iminZ >= -0.55f);
    CHECK(imaxZ <= 0.55f);
    // Terrain island should have some vertical extent.
    CHECK(imayY - iminY > 0.05f);

    for (int i = 0; i < island->getVertexCount(); ++i) {
        CHECK(std::isfinite(island->getPositionX(i)));
        CHECK(std::isfinite(island->getNormalX(i)));
        const float nx = island->getNormalX(i);
        const float ny = island->getNormalY(i);
        const float nz = island->getNormalZ(i);
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        CHECK(std::fabs(len - 1.f) < 0.2f);
        // Indices in range (spot-check via triangle walk below).
    }
    for (int t = 0; t < island->getIndexCount(); ++t) {
        CHECK(island->getIndex(t) >= 0);
        CHECK(island->getIndex(t) < island->getVertexCount());
    }

    // ------------------------------------------------------------------
    // 2) Crystal clusters (spheres) scattered as prop meshes
    // ------------------------------------------------------------------
    struct CrystalSpec {
        uint32_t seed;
        float    radius;
        int      resolution;
    };
    // Radii must span multiple voxels at the chosen resolution (unit field in [-1,1]).
    const CrystalSpec crystals[] = {
        {101u, 0.45f, 18},
        {202u, 0.35f, 16},
        {303u, 0.55f, 20},
    };

    std::vector<MeshBuild *> crystalMeshes;
    crystalMeshes.reserve(3);
    int crystalTris = 0;
    for (const CrystalSpec &spec : crystals) {
        Params cp;
        cp.setSeed(spec.seed);
        cp.setInt("resolution", spec.resolution);
        cp.setString("field", "sphere");
        cp.setFloat("radius", spec.radius);
        std::string err;
        MeshBuild mesh;
        const bool ok =
            MeshRecipeRegistry::instance().generate("mesh.marchingcubes", cp, mesh, err);
        CHECK(ok);
        if (!ok) {
            CHECK_EQ(err, std::string());  // surface the recipe error in the log
            continue;
        }
        MeshBuild *c = new MeshBuild(std::move(mesh));
        CHECK(c->getVertexCount() > 40);
        CHECK_EQ(c->getIndexCount() % 3, 0);
        // Crystals stay inside the unit cube prop volume.
        CHECK(meshRadius(*c) < 0.55f);
        crystalTris += c->getIndexCount() / 3;
        crystalMeshes.push_back(c);
    }
    REQUIRE(crystalMeshes.size() == 3);
    // Different seeds/radii should not be identical meshes.
    CHECK(crystalMeshes[0]->positions() != crystalMeshes[1]->positions());

    // ------------------------------------------------------------------
    // 3–4) Combined scene budget (island + crystals) for a small prop kit
    // ------------------------------------------------------------------
    const int islandTris = island->getIndexCount() / 3;
    const int totalTris  = islandTris + crystalTris;
    CHECK(islandTris > 50);
    CHECK(crystalTris > 50);
    // Soft real-time prop kit budget (well under a typical mobile draw call).
    CHECK(totalTris < 50000);
    CHECK(totalTris > 200);

    // Same island params ⇒ identical bake (cache key for a content pipeline).
    auto islandBLease = requireMeshBuild(*proc, "mesh.marchingcubes", islandParams.handle);
    auto islandB = islandBLease.view();
    REQUIRE(islandB.isBound());
    CHECK(island->positions() == islandB->positions());
    CHECK(island->indices() == islandB->indices());

    // Noise cave variant also usable as an alternate room shell.
    Params caveP;
    caveP.setSeed(55);
    caveP.setInt("resolution", 20);
    caveP.setString("field", "noise");
    caveP.setFloat("scale", 2.2f);
    caveP.setFloat("threshold", 0.0f);
    auto caveParams = requireParams(caveP);
    auto caveShellLease = requireMeshBuild(*proc, "mesh.marchingcubes", caveParams.handle);
    auto caveShell = caveShellLease.view();
    REQUIRE(caveShell.isBound());
    CHECK(caveShell->getVertexCount() > 100);
    CHECK(caveShell->positions() != island->positions());

    // ------------------------------------------------------------------
    // 5) Optional GPU upload (soft-skip without window / Vulkan surface)
    // ------------------------------------------------------------------
    try {
        auto *win = eve::window::Window::create();
        auto *gfx = Graphics::create();
        if (win && gfx) {
            eve::window::WindowSettings ws;
            ws.width    = 320;
            ws.height   = 240;
            ws.centered = true;
            if (win->setWindowSettings(ws)) {
                Mesh *gpuIsland =
                    gfx->newMeshFromArrays(island->positions().data(), island->normals().data(),
                                           island->uvs().data(), island->getVertexCount(),
                                           island->indices().data(), island->getIndexCount());
                REQUIRE(gpuIsland != nullptr);
                CHECK(gpuIsland->indexCount == island->getIndexCount());

                // Also exercise Procgen::generateMesh convenience path.
                auto gpuCrystal = proc->generateMeshBorrowed(
                    "mesh.marchingcubes", islandParams.handle, gfx);
                REQUIRE(gpuCrystal.isBound());
                CHECK(gpuCrystal->indexCount == island->getIndexCount());
                win->close();
            }
        }
    } catch (const std::exception &ex) {
        // Headless / no VK_KHR_surface — CPU path above is the contract.
        (void)ex;
    }

    for (MeshBuild *c : crystalMeshes) delete c;
}

/**
 * Scenario 3 — Cave expedition (WFC cave + pathfinding + FOV).
 *
 * Timeline:
 *  1) WFC cave → TileLayer (wall/floor)
 *  2) Find two distant floor cells in the same connected component
 *  3) A* path between them; path length ≥ manhattan distance
 *  4) FOV from the start cell: walls block sight, nearby floor is visible
 */
TEST_CASE("procgen.simulation.caveExpedition") {
    constexpr int kMapW = 32;
    constexpr int kMapH = 24;
    constexpr float kTile = 16.f;

    Procgen *proc = Procgen::create();
    Map *mapMod   = Map::create();
    bindDungeonPalette(proc, "sim_cave");

    Params p;
    p.setSeed(4242);
    p.setSize(kMapW, kMapH);
    p.setString("preset", "cave");
    p.setInt("maxAttempts", 64);

    auto params = requireParams(p);
    auto gridLease = requireGrid(*proc, "wfc.simple", params.handle);
    auto grid = gridLease.view();
    REQUIRE(grid.isBound());
    CHECK(borderIsWallLike(*grid));

    TileLayer *layer = mapMod->newLayer(kMapW, kMapH, kTile, kTile);
    auto applyResult = proc->applyToLayer(gridLease.handle, "sim_cave", *layer);
    CHECK(applyResult.ok());

    // Collect floor cells.
    std::vector<std::pair<int, int>> floors;
    for (int y = 1; y < kMapH - 1; ++y) {
        for (int x = 1; x < kMapW - 1; ++x) {
            if (layer->getTile(x, y) == kFloorGid) floors.emplace_back(x, y);
        }
    }
    REQUIRE(floors.size() >= 8);

    // BFS components; pick two cells from the largest component that are far apart.
    auto idx = [&](int x, int y) { return y * kMapW + x; };
    std::vector<int> comp(size_t(kMapW * kMapH), -1);
    int bestComp = -1, bestSize = 0;
    std::vector<std::pair<int, int>> bestCells;
    int nextId = 0;
    const int dx4[4] = {1, -1, 0, 0};
    const int dy4[4] = {0, 0, 1, -1};
    for (auto [sx, sy] : floors) {
        if (comp[size_t(idx(sx, sy))] >= 0) continue;
        std::vector<std::pair<int, int>> cells;
        std::vector<std::pair<int, int>> q;
        q.emplace_back(sx, sy);
        comp[size_t(idx(sx, sy))] = nextId;
        size_t qi = 0;
        while (qi < q.size()) {
            auto [cx, cy] = q[qi++];
            cells.emplace_back(cx, cy);
            for (int d = 0; d < 4; ++d) {
                const int nx = cx + dx4[d], ny = cy + dy4[d];
                if (nx < 0 || ny < 0 || nx >= kMapW || ny >= kMapH) continue;
                if (layer->getTile(nx, ny) != kFloorGid) continue;
                if (comp[size_t(idx(nx, ny))] >= 0) continue;
                comp[size_t(idx(nx, ny))] = nextId;
                q.emplace_back(nx, ny);
            }
        }
        if (int(cells.size()) > bestSize) {
            bestSize = int(cells.size());
            bestComp = nextId;
            bestCells = std::move(cells);
        }
        ++nextId;
    }
    REQUIRE(bestSize >= 4);
    CHECK(bestComp >= 0);

    // Farthest pair (approx) for a meaningful expedition.
    int a = 0, b = 0, bestDist = -1;
    for (int i = 0; i < int(bestCells.size()); ++i) {
        for (int j = i + 1; j < int(bestCells.size()); ++j) {
            const int dist = std::abs(bestCells[size_t(i)].first - bestCells[size_t(j)].first) +
                             std::abs(bestCells[size_t(i)].second - bestCells[size_t(j)].second);
            if (dist > bestDist) {
                bestDist = dist;
                a = i;
                b = j;
            }
        }
    }
    CHECK(bestDist >= 2);
    const int ax = bestCells[size_t(a)].first, ay = bestCells[size_t(a)].second;
    const int bx = bestCells[size_t(b)].first, by = bestCells[size_t(b)].second;

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(kWallGid);
    pf->setBlockEmpty(true);
    pf->setTopology("ortho4");
    Path *path = pf->findPath(ax, ay, bx, by);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() >= bestDist + 1);  // inclusive endpoints
    CHECK(pathOnLayer(path, layer));
    CHECK_EQ(path->getX(0), ax);
    CHECK_EQ(path->getY(path->getLength() - 1), by);

    // FOV: walls opaque; floor around start is visible; far goal may be hidden.
    Fov *fov = mapMod->newFov(layer);
    REQUIRE(fov != nullptr);
    fov->blockOpaqueGid(kWallGid);
    fov->setAlgorithm("shadowcast");
    const int rev = fov->addRevealer(ax, ay, 6);
    CHECK(rev >= 0);
    fov->compute();
    CHECK(fov->isVisible(ax, ay));
    CHECK(fov->isExplored(ax, ay));
    // Immediate floor neighbors (if any) should be visible.
    for (int d = 0; d < 4; ++d) {
        const int nx = ax + dx4[d], ny = ay + dy4[d];
        if (nx < 0 || ny < 0 || nx >= kMapW || ny >= kMapH) continue;
        if (layer->getTile(nx, ny) == kFloorGid) CHECK(fov->isVisible(nx, ny));
    }

    delete path;
    delete pf;
    delete fov;
    layer->setVisible(false);
}

/**
 * Scenario 4 — Overworld biome travel (WFC terrain + weighted path).
 *
 * Timeline:
 *  1) WFC terrain → biome Grid2D with adjacency invariant
 *  2) Palette GIDs per biome; TileLayer filled
 *  3) Pathfinder with higher cost on water/snow; path prefers mid biomes
 *  4) Same seed regenerate matches for a save-game overworld
 */
TEST_CASE("procgen.simulation.overworldBiomeTravel") {
    constexpr int kMapW = 28;
    constexpr int kMapH = 22;
    constexpr float kTile = 16.f;

    Procgen *proc = Procgen::create();
    Map *mapMod   = Map::create();

    // Biome GIDs
    constexpr int kWater = 10, kSand = 11, kGrass = 12, kDirt = 13, kStone = 14, kSnow = 15;
    proc->setPaletteGid("sim_biome", "water", kWater);
    proc->setPaletteGid("sim_biome", "sand", kSand);
    proc->setPaletteGid("sim_biome", "grass", kGrass);
    proc->setPaletteGid("sim_biome", "dirt", kDirt);
    proc->setPaletteGid("sim_biome", "stone", kStone);
    proc->setPaletteGid("sim_biome", "snow", kSnow);

    Params p;
    p.setSeed(9001);
    p.setSize(kMapW, kMapH);
    p.setString("preset", "terrain");
    p.setInt("maxAttempts", 64);

    auto params = requireParams(p);
    auto gridLease = requireGrid(*proc, "wfc.simple", params.handle);
    auto grid = gridLease.view();
    REQUIRE(grid.isBound());
    CHECK(terrainAdjacencyOk(*grid));

    TileLayer *layer = mapMod->newLayer(kMapW, kMapH, kTile, kTile);
    auto applyResult = proc->applyToLayer(gridLease.handle, "sim_biome", *layer);
    CHECK(applyResult.ok());

    // Pick a grass cell and a dirt/stone cell if possible.
    int sx = -1, sy = -1, gx = -1, gy = -1;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const int gid = layer->getTile(x, y);
            if (sx < 0 && gid == kGrass) {
                sx = x;
                sy = y;
            }
            if (gid == kDirt || gid == kStone) {
                gx = x;
                gy = y;
            }
        }
    }
    if (sx < 0) {
        sx = 1;
        sy = 1;
    }
    if (gx < 0) {
        gx = kMapW - 2;
        gy = kMapH - 2;
    }

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->setTopology("ortho4");
    pf->setBlockEmpty(false);
    // Prefer dry land: water/snow expensive.
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const int gid = layer->getTile(x, y);
            float cost = 1.f;
            if (gid == kWater) cost = 8.f;
            else if (gid == kSnow) cost = 4.f;
            else if (gid == kSand) cost = 1.5f;
            pf->setCellCost(x, y, cost);
        }
    }

    Path *path = pf->findPath(sx, sy, gx, gy);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() >= 2);
    CHECK_EQ(path->getX(0), sx);
    CHECK_EQ(path->getY(path->getLength() - 1), gy);
    // Cost should be finite and positive.
    CHECK(path->getTotalCost() > 0.f);

    // Reproducible overworld for save/load.
    auto grid2Lease = requireGrid(*proc, "wfc.simple", params.handle);
    auto grid2 = grid2Lease.view();
    REQUIRE(grid2.isBound());
    CHECK(grid->cells() == grid2->cells());

    delete path;
    delete pf;
    layer->setVisible(false);
}

/**
 * Scenario 5 — Loot prop bake (torus ring + gem spheres for a content pipeline).
 */
TEST_CASE("procgen.simulation.lootPropBake") {
    Procgen *proc = Procgen::create();

    Params ringP;
    ringP.setSeed(12);
    ringP.setInt("resolution", 22);
    ringP.setString("field", "torus");
    ringP.setFloat("majorRadius", 0.5f);
    ringP.setFloat("minorRadius", 0.18f);

    auto ringParams = requireParams(ringP);
    auto ringLease = requireMeshBuild(*proc, "mesh.marchingcubes", ringParams.handle);
    auto ring = ringLease.view();
    REQUIRE(ring.isBound());
    CHECK(ring->getVertexCount() > 150);
    CHECK_EQ(ring->getIndexCount() % 3, 0);

    Params gemP;
    gemP.setSeed(44);
    gemP.setInt("resolution", 16);
    gemP.setString("field", "sphere");
    gemP.setFloat("radius", 0.4f);
    auto gemParams = requireParams(gemP);
    auto gemLease = requireMeshBuild(*proc, "mesh.marchingcubes", gemParams.handle);
    auto gem = gemLease.view();
    REQUIRE(gem.isBound());

    const int totalTris = ring->getIndexCount() / 3 + gem->getIndexCount() / 3;
    CHECK(totalTris > 100);
    CHECK(totalTris < 30000);

    // Content-pipeline cache key: identical params → identical bytes.
    auto ring2Lease = requireMeshBuild(*proc, "mesh.marchingcubes", ringParams.handle);
    auto ring2 = ring2Lease.view();
    REQUIRE(ring2.isBound());
    CHECK(ring->positions() == ring2->positions());
    CHECK(ring->indices() == ring2->indices());

    // Slight param change busts the cache.
    Params ringP2 = ringP;
    ringP2.setFloat("minorRadius", 0.22f);
    auto ring2Params = requireParams(ringP2);
    auto ring3Lease = requireMeshBuild(*proc, "mesh.marchingcubes", ring2Params.handle);
    auto ring3 = ring3Lease.view();
    REQUIRE(ring3.isBound());
    CHECK(ring->positions() != ring3->positions());

}
