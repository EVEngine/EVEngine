#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "procgen/Procgen.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"
#include "procgen/JsonExport.h"
#include "procgen/MeshBuild.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/texture/TextureRecipe.h"
#include "procgen/texture/NoiseField.h"
#include "procgen/texture/ColorRamp.h"
#include "map/TileLayer.h"
#include "image/ImageData.h"
#include "graphics/Graphics.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

using namespace eve::procgen;
using namespace eve::graphics;

namespace {

bool gridsEqual(const Grid2D &a, const Grid2D &b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    return a.cells() == b.cells();
}

int countSemantic(const Grid2D &g, int semantic) {
    int n = 0;
    for (uint32_t c : g.cells())
        if (int(c) == semantic) ++n;
    return n;
}

bool neighborsOk(int a, int b, const std::set<std::pair<int, int>> &allowed) {
    if (a > b) std::swap(a, b);
    return allowed.count({a, b}) > 0;
}

bool terrainAdjacencyOk(const Grid2D &g) {
    // Ordered elevation band: only self + ±1 neighbors.
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
    const int w = g.getWidth();
    const int h = g.getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int c = g.getCell(x, y);
            if (x + 1 < w && !neighborsOk(c, g.getCell(x + 1, y), allowed)) return false;
            if (y + 1 < h && !neighborsOk(c, g.getCell(x, y + 1), allowed)) return false;
        }
    }
    return true;
}

bool caveAdjacencyOk(const Grid2D &g) {
    std::set<std::pair<int, int>> allowed = {
        {int(Semantic::Wall), int(Semantic::Wall)},
        {int(Semantic::Wall), int(Semantic::Floor)},
        {int(Semantic::Floor), int(Semantic::Floor)},
    };
    // Normalize pair order for lookup.
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

bool borderIsWall(const Grid2D &g) {
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

bool meshIndicesInRange(const MeshBuild &m) {
    const int vc = m.getVertexCount();
    for (int i = 0; i < m.getIndexCount(); ++i) {
        const int id = m.getIndex(i);
        if (id < 0 || id >= vc) return false;
    }
    return true;
}

bool meshNormalsFiniteUnit(const MeshBuild &m, float tol = 0.15f) {
    for (int i = 0; i < m.getVertexCount(); ++i) {
        const float nx = m.getNormalX(i);
        const float ny = m.getNormalY(i);
        const float nz = m.getNormalZ(i);
        if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz)) return false;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (std::fabs(len - 1.f) > tol) return false;
    }
    return true;
}

bool meshPositionsFinite(const MeshBuild &m) {
    for (int i = 0; i < m.getVertexCount(); ++i) {
        if (!std::isfinite(m.getPositionX(i)) || !std::isfinite(m.getPositionY(i)) ||
            !std::isfinite(m.getPositionZ(i)))
            return false;
    }
    return true;
}

float meshApproxSignedVolume(const MeshBuild &m) {
    // Sum of signed tet volumes with origin (valid for closed mesh).
    float vol = 0.f;
    for (int t = 0; t + 2 < m.getIndexCount(); t += 3) {
        const int i0 = m.getIndex(t);
        const int i1 = m.getIndex(t + 1);
        const int i2 = m.getIndex(t + 2);
        const float ax = m.getPositionX(i0), ay = m.getPositionY(i0), az = m.getPositionZ(i0);
        const float bx = m.getPositionX(i1), by = m.getPositionY(i1), bz = m.getPositionZ(i1);
        const float cx = m.getPositionX(i2), cy = m.getPositionY(i2), cz = m.getPositionZ(i2);
        vol += ax * (by * cz - bz * cy) - ay * (bx * cz - bz * cx) + az * (bx * cy - by * cx);
    }
    return vol / 6.f;
}

bool approxEq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

bool hasWalkablePath(const Grid2D &g) {
    // BFS from first floor/corridor cell; ensure >1 walkable and connected component covers all.
    const int w = g.getWidth();
    const int h = g.getHeight();
    auto walkable = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) return false;
        const int c = g.getCell(x, y);
        return c == int(Semantic::Floor) || c == int(Semantic::Corridor);
    };
    int start = -1;
    int total = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (walkable(x, y)) {
                ++total;
                if (start < 0) start = y * w + x;
            }
        }
    }
    if (total < 2 || start < 0) return false;
    std::vector<char> seen(size_t(w * h), 0);
    std::vector<int>  q;
    q.push_back(start);
    seen[size_t(start)] = 1;
    size_t qi = 0;
    int    reached = 0;
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    while (qi < q.size()) {
        const int i = q[qi++];
        const int x = i % w;
        const int y = i / w;
        ++reached;
        for (int d = 0; d < 4; ++d) {
            const int nx = x + dx[d];
            const int ny = y + dy[d];
            if (!walkable(nx, ny)) continue;
            const int ni = ny * w + nx;
            if (seen[size_t(ni)]) continue;
            seen[size_t(ni)] = 1;
            q.push_back(ni);
        }
    }
    return reached == total;
}

}  // namespace

TEST_CASE("procgen.semantic.names") {
    CHECK_EQ(std::string(semanticName(Semantic::Wall)), "wall");
    CHECK_EQ(semanticId("floor"), Semantic::Floor);
    CHECK_EQ(semanticId("nope"), Semantic::Empty);
}

TEST_CASE("procgen.registry.builtins") {
    GeneratorRegistry::instance().registerBuiltins();
    CHECK(GeneratorRegistry::instance().has("dungeon.bsp"));
    CHECK(GeneratorRegistry::instance().has("cave.cellular"));
    CHECK(GeneratorRegistry::instance().has("cave.drunkard"));
    CHECK(GeneratorRegistry::instance().has("maze.backtrack"));
    CHECK(GeneratorRegistry::instance().has("noise.terrain"));
    CHECK(GeneratorRegistry::instance().has("wfc.simple"));
}

TEST_CASE("procgen.mesh.rock.reproducibleAndControllable") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(1847);
    p.setInt("subdivisions", 3);
    p.setFloat("radius", 0.7f);
    p.setFloat("flattening", 0.3f);
    p.setFloat("angularity", 0.45f);
    p.setFloat("erosion", 0.16f);
    p.setFloat("scale", 2.4f);
    MeshBuild a, b, flatter;
    std::string err;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.rock", p, a, err));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.rock", p, b, err));
    CHECK_EQ(a.getVertexCount(), 642);
    CHECK_EQ(a.getIndexCount() / 3, 1280);
    CHECK(a.positions() == b.positions());
    CHECK(a.indices() == b.indices());
    CHECK(meshIndicesInRange(a));
    CHECK(meshNormalsFiniteUnit(a));

    p.setFloat("flattening", 0.58f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.rock", p, flatter, err));
    CHECK(a.positions() != flatter.positions());

    p.setInt("subdivisions", 2);
    MeshBuild lod1;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.rock", p, lod1, err));
    CHECK_EQ(lod1.getVertexCount(), 162);
    CHECK_EQ(lod1.getIndexCount() / 3, 320);

    std::vector<std::vector<float>> shapePositions;
    for (const char *shape : {"boulder", "slab", "block", "shard"}) {
        p.setString("baseShape", shape);
        p.setInt("subdivisions", 3);
        MeshBuild variant;
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.rock", p, variant, err));
        CHECK_EQ(variant.getMeta("baseShape", ""), shape);
        CHECK_EQ(variant.getVertexCount(), 642);
        CHECK(meshIndicesInRange(variant));
        CHECK(meshNormalsFiniteUnit(variant));
        shapePositions.push_back(variant.positions());
    }
    CHECK(shapePositions[0] != shapePositions[1]);
    CHECK(shapePositions[1] != shapePositions[2]);
    CHECK(shapePositions[2] != shapePositions[3]);

    p.setString("baseShape", "invalid");
    MeshBuild invalid;
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.rock", p, invalid, err));
}

TEST_CASE("procgen.dungeon.bsp.reproducible") {
    Params p;
    p.setSeed(42);
    p.setSize(48, 36);
    Grid2D a, b;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("dungeon.bsp", p, a, err));
    CHECK(GeneratorRegistry::instance().generate("dungeon.bsp", p, b, err));
    CHECK(gridsEqual(a, b));
    CHECK(countSemantic(a, int(Semantic::Floor)) > 0);
    CHECK(countSemantic(a, int(Semantic::Wall)) > 0);
    CHECK(a.getObjectCount() >= 2);
    CHECK(hasWalkablePath(a));
}

TEST_CASE("procgen.cave.cellular.reproducible") {
    Params p;
    p.setSeed(7);
    p.setSize(32, 24);
    p.setInt("loops", 4);
    p.setFloat("fill", 0.45f);
    Grid2D a, b;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("cave.cellular", p, a, err));
    CHECK(GeneratorRegistry::instance().generate("cave.cellular", p, b, err));
    CHECK(gridsEqual(a, b));
    CHECK(countSemantic(a, int(Semantic::Floor)) > 0);
}

TEST_CASE("procgen.cave.drunkard.reproducible") {
    Params p;
    p.setSeed(99);
    p.setSize(40, 30);
    p.setFloat("floorPct", 0.4f);
    Grid2D a, b;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("cave.drunkard", p, a, err));
    CHECK(GeneratorRegistry::instance().generate("cave.drunkard", p, b, err));
    CHECK(gridsEqual(a, b));
    CHECK(countSemantic(a, int(Semantic::Floor)) > 10);
}

TEST_CASE("procgen.maze.backtrack.reproducible") {
    Params p;
    p.setSeed(123);
    p.setSize(21, 15);
    Grid2D a, b;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("maze.backtrack", p, a, err));
    CHECK(GeneratorRegistry::instance().generate("maze.backtrack", p, b, err));
    CHECK(gridsEqual(a, b));
    CHECK(hasWalkablePath(a));
}

TEST_CASE("procgen.noise.terrain.reproducible") {
    Params p;
    p.setSeed(55);
    p.setSize(32, 32);
    p.setFloat("frequency", 5.f);
    p.setInt("octaves", 3);
    Grid2D a, b;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("noise.terrain", p, a, err));
    CHECK(GeneratorRegistry::instance().generate("noise.terrain", p, b, err));
    CHECK(gridsEqual(a, b));
    // Multiple biome bands expected.
    std::set<int> kinds;
    for (uint32_t c : a.cells()) kinds.insert(int(c));
    CHECK(kinds.size() >= 2);
}

TEST_CASE("procgen.wfc.simple.reproducible") {
    GeneratorRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(42);
    p.setSize(24, 18);
    p.setString("preset", "dungeon");
    p.setInt("maxAttempts", 64);
    Grid2D a, b;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, a, err));
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, b, err));
    CHECK(gridsEqual(a, b));
    CHECK(countSemantic(a, int(Semantic::Wall)) > 0);
    CHECK(countSemantic(a, int(Semantic::Floor)) + countSemantic(a, int(Semantic::Corridor)) > 0);
    CHECK(borderIsWall(a));
}

TEST_CASE("procgen.wfc.simple.cavePreset") {
    Params p;
    p.setSeed(11);
    p.setSize(20, 16);
    p.setString("preset", "cave");
    p.setInt("maxAttempts", 64);
    Grid2D g;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, g, err));
    CHECK(borderIsWall(g));
    CHECK(caveAdjacencyOk(g));
    CHECK(countSemantic(g, int(Semantic::Floor)) > 0);
    CHECK(countSemantic(g, int(Semantic::Wall)) > 0);
    CHECK_EQ(g.getMeta("algorithm", ""), std::string("wfc.simple"));
    CHECK_EQ(g.getMeta("preset", ""), std::string("cave"));
}

TEST_CASE("procgen.wfc.simple.terrainPreset") {
    Params p;
    p.setSeed(7);
    p.setSize(24, 20);
    p.setString("preset", "terrain");
    p.setInt("maxAttempts", 64);
    Grid2D g, g2;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, g, err));
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, g2, err));
    CHECK(gridsEqual(g, g2));
    CHECK(terrainAdjacencyOk(g));
    std::set<int> kinds;
    for (uint32_t c : g.cells()) kinds.insert(int(c));
    CHECK(kinds.size() >= 1);
    // Terrain does not force wall border.
    CHECK(countSemantic(g, int(Semantic::Wall)) == 0);
}

TEST_CASE("procgen.wfc.simple.dungeonObjectsAndMeta") {
    Params p;
    p.setSeed(99);
    p.setSize(28, 20);
    p.setString("preset", "dungeon");
    p.setInt("maxAttempts", 64);
    Grid2D g;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, g, err));
    CHECK_EQ(g.getMeta("algorithm", ""), std::string("wfc.simple"));
    CHECK_EQ(g.getMeta("preset", ""), std::string("dungeon"));
    // Walkable cells should produce spawn + stairs.
    const int walkable =
        countSemantic(g, int(Semantic::Floor)) + countSemantic(g, int(Semantic::Corridor));
    if (walkable > 0) {
        CHECK(g.getObjectCount() >= 2);
        std::unordered_set<std::string> types;
        for (int i = 0; i < g.getObjectCount(); ++i) types.insert(g.getObjectType(i));
        CHECK(types.count("spawn") == 1);
        CHECK(types.count("stairs") == 1);
    }
}

TEST_CASE("procgen.wfc.simple.seedVaries") {
    Params a, b;
    a.setSeed(1);
    b.setSeed(2);
    a.setSize(18, 14);
    b.setSize(18, 14);
    a.setString("preset", "cave");
    b.setString("preset", "cave");
    a.setInt("maxAttempts", 64);
    b.setInt("maxAttempts", 64);
    Grid2D ga, gb;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", a, ga, err));
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", b, gb, err));
    // Different seeds should almost always differ; allow rare collision by checking not
    // identical OR both valid borders.
    CHECK(borderIsWall(ga));
    CHECK(borderIsWall(gb));
    CHECK(!gridsEqual(ga, gb));
}

TEST_CASE("procgen.wfc.simple.errors") {
    std::string err;
    Grid2D g;
    Params tooSmall;
    tooSmall.setSeed(1);
    tooSmall.setSize(2, 2);
    tooSmall.setString("preset", "cave");
    CHECK(!GeneratorRegistry::instance().generate("wfc.simple", tooSmall, g, err));
    CHECK(err.find("4x4") != std::string::npos);

    Params badPreset;
    badPreset.setSeed(1);
    badPreset.setSize(12, 12);
    badPreset.setString("preset", "nope");
    CHECK(!GeneratorRegistry::instance().generate("wfc.simple", badPreset, g, err));
    CHECK(err.find("preset") != std::string::npos);

    Params tooBig;
    tooBig.setSeed(1);
    tooBig.setSize(300, 16);
    tooBig.setString("preset", "cave");
    CHECK(!GeneratorRegistry::instance().generate("wfc.simple", tooBig, g, err));
    CHECK(err.find("256") != std::string::npos);
}

TEST_CASE("procgen.wfc.simple.viaModule") {
    Procgen *mod = Procgen::create();
    CHECK(mod->hasAlgorithm("wfc.simple"));
    Params p;
    p.setSeed(5);
    p.setSize(16, 12);
    p.setString("preset", "dungeon");
    p.setInt("maxAttempts", 64);
    Grid2D *grid = mod->generate("wfc.simple", &p);
    REQUIRE(grid != nullptr);
    CHECK(borderIsWall(*grid));
    CHECK_EQ(grid->getWidth(), 16);
    CHECK_EQ(grid->getHeight(), 12);
    delete grid;

    Params bad;
    bad.setSeed(1);
    bad.setSize(8, 8);
    bad.setString("preset", "invalid");
    CHECK(mod->generate("wfc.simple", &bad) == nullptr);
    CHECK(mod->lastError().find("preset") != std::string::npos);
}

TEST_CASE("procgen.meshBuild.accessors") {
    MeshBuild m;
    CHECK(m.empty());
    CHECK_EQ(m.getVertexCount(), 0);
    CHECK_EQ(m.getIndexCount(), 0);
    m.addVertex(1.f, 2.f, 3.f, 0.f, 1.f, 0.f, 0.25f, 0.75f);
    m.addVertex(4.f, 5.f, 6.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    m.addVertex(7.f, 8.f, 9.f, 0.f, 1.f, 0.f, 1.f, 1.f);
    m.addTriangle(0, 1, 2);
    CHECK(!m.empty());
    CHECK_EQ(m.getVertexCount(), 3);
    CHECK_EQ(m.getIndexCount(), 3);
    CHECK_EQ(m.getPositionX(0), 1.f);
    CHECK_EQ(m.getPositionY(1), 5.f);
    CHECK_EQ(m.getPositionZ(2), 9.f);
    CHECK_EQ(m.getNormalY(0), 1.f);
    CHECK_EQ(m.getUvU(0), 0.25f);
    CHECK_EQ(m.getUvV(0), 0.75f);
    CHECK_EQ(m.getIndex(2), 2);
    // Out of range accessors are safe zeros.
    CHECK_EQ(m.getPositionX(99), 0.f);
    CHECK_EQ(m.getIndex(99), 0);
    m.setMeta("algo", "test");
    CHECK_EQ(m.getMeta("algo", ""), std::string("test"));
    CHECK_EQ(m.getMeta("missing", "d"), std::string("d"));
    m.clear();
    CHECK(m.empty());
}

TEST_CASE("procgen.mesh.marchingcubes.sphere") {
    MeshRecipeRegistry::instance().registerBuiltins();
    CHECK(MeshRecipeRegistry::instance().has("mesh.marchingcubes"));
    Params p;
    p.setSeed(1);
    p.setInt("resolution", 20);
    p.setString("field", "sphere");
    p.setFloat("radius", 0.7f);
    p.setFloat("isolevel", 0.f);
    MeshBuild mesh;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, mesh, err));
    CHECK(mesh.getVertexCount() > 100);
    CHECK(mesh.getIndexCount() > 100);
    CHECK_EQ(mesh.getIndexCount() % 3, 0);
    CHECK(meshIndicesInRange(mesh));
    CHECK(meshPositionsFinite(mesh));
    CHECK(meshNormalsFiniteUnit(mesh));
    // Closed sphere should have non-trivial volume.
    CHECK(std::fabs(meshApproxSignedVolume(mesh)) > 0.05f);
    // Same seed ⇒ same mesh.
    MeshBuild mesh2;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, mesh2, err));
    CHECK_EQ(mesh.getVertexCount(), mesh2.getVertexCount());
    CHECK(mesh.positions() == mesh2.positions());
    CHECK(mesh.indices() == mesh2.indices());
}

TEST_CASE("procgen.mesh.marchingcubes.allFields") {
    MeshRecipeRegistry::instance().registerBuiltins();
    const char *fields[] = {"sphere", "torus", "noise", "terrain"};
    for (const char *field : fields) {
        Params p;
        p.setSeed(42);
        p.setInt("resolution", 18);
        p.setString("field", field);
        p.setFloat("scale", 1.5f);
        p.setInt("octaves", 2);
        MeshBuild mesh;
        std::string err;
        CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, mesh, err));
        CHECK(mesh.getVertexCount() > 20);
        CHECK_EQ(mesh.getIndexCount() % 3, 0);
        CHECK(meshIndicesInRange(mesh));
        CHECK(meshPositionsFinite(mesh));
        CHECK(meshNormalsFiniteUnit(mesh));
        CHECK_EQ(mesh.getMeta("algorithm", ""), std::string("mesh.marchingcubes"));
        CHECK_EQ(mesh.getMeta("field", ""), std::string(field));
    }
}

TEST_CASE("procgen.mesh.marchingcubes.noiseReproducibleAndVaries") {
    Params p;
    p.setSeed(9);
    p.setInt("resolution", 16);
    p.setString("field", "noise");
    p.setFloat("scale", 2.f);
    MeshBuild a, b, c;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, a, err));
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, b, err));
    CHECK(a.positions() == b.positions());
    CHECK(a.indices() == b.indices());

    Params p2 = p;
    p2.setSeed(10);
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p2, c, err));
    CHECK(a.positions() != c.positions());
}

TEST_CASE("procgen.mesh.marchingcubes.rawDensityPlane") {
    // Density = y - 0.5 on a 4³ grid → horizontal plane at mid height.
    const int n = 4;
    std::vector<float> density(size_t(n * n * n));
    for (int z = 0; z < n; ++z) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                density[size_t(x + y * n + z * n * n)] = float(y) / float(n - 1) - 0.5f;
            }
        }
    }
    MeshBuild mesh;
    std::string err;
    CHECK(marchingCubes(density.data(), n, n, n, 0.f, mesh, &err));
    CHECK(mesh.getVertexCount() > 0);
    CHECK_EQ(mesh.getIndexCount() % 3, 0);
    CHECK(meshIndicesInRange(mesh));
    // All vertices near mid-Y in unit cube mapping [-0.5,0.5].
    for (int i = 0; i < mesh.getVertexCount(); ++i) {
        CHECK(std::fabs(mesh.getPositionY(i)) < 0.35f);
    }
}

TEST_CASE("procgen.mesh.marchingcubes.rawDensityEmpty") {
    const int n = 3;
    std::vector<float> density(size_t(n * n * n), 1.f);  // all solid, no surface
    MeshBuild mesh;
    std::string err;
    CHECK(marchingCubes(density.data(), n, n, n, 0.f, mesh, &err));
    CHECK(mesh.empty());

    density.assign(size_t(n * n * n), -1.f);  // all empty
    CHECK(marchingCubes(density.data(), n, n, n, 0.f, mesh, &err));
    CHECK(mesh.empty());
}

TEST_CASE("procgen.mesh.marchingcubes.rawApiErrors") {
    MeshBuild mesh;
    std::string err;
    CHECK(!marchingCubes(nullptr, 4, 4, 4, 0.f, mesh, &err));
    CHECK(err.find("null") != std::string::npos);

    float tiny[1] = {0.f};
    CHECK(!marchingCubes(tiny, 1, 1, 1, 0.f, mesh, &err));
    CHECK(err.find("2x2x2") != std::string::npos);
}

TEST_CASE("procgen.mesh.marchingcubes.fillDensityAndErrors") {
    std::vector<float> density;
    int nx = 0, ny = 0, nz = 0;
    std::string err;

    Params ok;
    ok.setInt("resolution", 8);
    ok.setString("field", "sphere");
    CHECK(fillDensityField(ok, density, nx, ny, nz, err));
    CHECK_EQ(nx, 8);
    CHECK_EQ(ny, 8);
    CHECK_EQ(nz, 8);
    CHECK_EQ(int(density.size()), 8 * 8 * 8);

    Params axis;
    axis.setInt("nx", 6);
    axis.setInt("ny", 7);
    axis.setInt("nz", 5);
    axis.setString("field", "torus");
    CHECK(fillDensityField(axis, density, nx, ny, nz, err));
    CHECK_EQ(nx, 6);
    CHECK_EQ(ny, 7);
    CHECK_EQ(nz, 5);

    Params badField;
    badField.setInt("resolution", 8);
    badField.setString("field", "banana");
    CHECK(!fillDensityField(badField, density, nx, ny, nz, err));
    CHECK(err.find("field") != std::string::npos);

    Params tooSmall;
    tooSmall.setInt("resolution", 1);
    tooSmall.setString("field", "sphere");
    CHECK(!fillDensityField(tooSmall, density, nx, ny, nz, err));

    Params tooBig;
    tooBig.setInt("resolution", 200);
    tooBig.setString("field", "sphere");
    CHECK(!fillDensityField(tooBig, density, nx, ny, nz, err));
    CHECK(err.find("128") != std::string::npos);
}

TEST_CASE("procgen.mesh.marchingcubes.recipeErrors") {
    MeshRecipeRegistry::instance().registerBuiltins();
    MeshBuild mesh;
    std::string err;
    Params badField;
    badField.setInt("resolution", 12);
    badField.setString("field", "nope");
    CHECK(!MeshRecipeRegistry::instance().generate("mesh.marchingcubes", badField, mesh, err));
    CHECK(err.find("field") != std::string::npos);

    CHECK(!MeshRecipeRegistry::instance().generate("mesh.missing", badField, mesh, err));
    CHECK(err.find("unknown") != std::string::npos);

    auto ids = MeshRecipeRegistry::instance().list();
    CHECK(!ids.empty());
    CHECK(std::find(ids.begin(), ids.end(), "mesh.marchingcubes") != ids.end());
}

TEST_CASE("procgen.mesh.marchingcubes.isolevelAffectsMesh") {
    Params lo;
    lo.setSeed(1);
    lo.setInt("resolution", 16);
    lo.setString("field", "sphere");
    lo.setFloat("radius", 0.7f);
    lo.setFloat("isolevel", -0.2f);
    Params hi = lo;
    hi.setFloat("isolevel", 0.2f);
    MeshBuild a, b;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", lo, a, err));
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", hi, b, err));
    // Higher isolevel shrinks solid region → fewer / different triangles.
    const bool differs =
        a.getVertexCount() != b.getVertexCount() || a.positions() != b.positions();
    CHECK(differs);
}

TEST_CASE("procgen.mesh.marchingcubes.viaModule") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(3);
    p.setInt("resolution", 16);
    p.setString("field", "torus");
    MeshBuild *m = mod->buildMesh("mesh.marchingcubes", &p);
    REQUIRE(m != nullptr);
    CHECK(m->getVertexCount() > 50);
    CHECK(meshIndicesInRange(*m));
    CHECK(mod->hasMeshRecipe("mesh.marchingcubes"));
    CHECK(mod->getMeshRecipeCount() >= 1);
    CHECK_EQ(mod->getMeshRecipeId(0), std::string("mesh.marchingcubes"));
    delete m;

    Params bad;
    bad.setInt("resolution", 8);
    bad.setString("field", "nope");
    CHECK(mod->buildMesh("mesh.marchingcubes", &bad) == nullptr);
    CHECK(mod->lastError().find("field") != std::string::npos);
    CHECK(mod->buildMesh("mesh.missing", &p) == nullptr);
    CHECK(mod->generateMesh("mesh.marchingcubes", nullptr, nullptr) == nullptr);
}

TEST_CASE("procgen.wfc.simple.dungeonAdjacencyAndListing") {
    GeneratorRegistry::instance().registerBuiltins();
    Procgen *mod = Procgen::create();
    CHECK(mod->hasAlgorithm("wfc.simple"));
    bool listed = false;
    for (int i = 0; i < mod->getAlgorithmCount(); ++i) {
        if (mod->getAlgorithmId(i) == "wfc.simple") listed = true;
    }
    CHECK(listed);

    Params p;
    p.setSeed(17);
    p.setSize(22, 16);
    p.setString("preset", "dungeon");
    p.setInt("maxAttempts", 64);
    Grid2D g;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("wfc.simple", p, g, err));
    CHECK(borderIsWall(g));
    // Allowed dungeon pairs (undirected).
    auto okPair = [](int a, int b) {
        if (a > b) std::swap(a, b);
        const int W = int(Semantic::Wall), F = int(Semantic::Floor), C = int(Semantic::Corridor),
                  D = int(Semantic::Door);
        if (a == W && (b == W || b == F || b == C || b == D)) return true;
        if (a == F && (b == F || b == C || b == D)) return true;
        if (a == C && b == C) return true;
        return false;
    };
    for (int y = 0; y < g.getHeight(); ++y) {
        for (int x = 0; x < g.getWidth(); ++x) {
            const int c = g.getCell(x, y);
            if (x + 1 < g.getWidth()) CHECK(okPair(c, g.getCell(x + 1, y)));
            if (y + 1 < g.getHeight()) CHECK(okPair(c, g.getCell(x, y + 1)));
        }
    }
}

TEST_CASE("procgen.wfc.simple.multiSeedBatch") {
    // Stress: several seeds/presets must all collapse successfully.
    const char *presets[] = {"dungeon", "cave", "terrain"};
    int okCount = 0;
    for (const char *preset : presets) {
        for (uint32_t seed = 1; seed <= 5; ++seed) {
            Params p;
            p.setSeed(seed * 13u + 7u);
            p.setSize(18, 14);
            p.setString("preset", preset);
            p.setInt("maxAttempts", 64);
            Grid2D g;
            std::string err;
            if (GeneratorRegistry::instance().generate("wfc.simple", p, g, err)) {
                ++okCount;
                CHECK_EQ(g.getWidth(), 18);
                CHECK_EQ(g.getHeight(), 14);
                if (std::string(preset) != "terrain") CHECK(borderIsWall(g));
            }
        }
    }
    CHECK_EQ(okCount, 15);
}

TEST_CASE("procgen.mesh.marchingcubes.resolutionScales") {
    Params lo;
    lo.setSeed(1);
    lo.setInt("resolution", 12);
    lo.setString("field", "sphere");
    lo.setFloat("radius", 0.7f);
    Params hi = lo;
    hi.setInt("resolution", 24);
    MeshBuild a, b;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", lo, a, err));
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", hi, b, err));
    CHECK(b.getIndexCount() > a.getIndexCount());
    CHECK(b.getVertexCount() > a.getVertexCount());
}

TEST_CASE("procgen.mesh.marchingcubes.torusAndAxisResolution") {
    Params torus;
    torus.setSeed(2);
    torus.setInt("resolution", 20);
    torus.setString("field", "torus");
    MeshBuild ring;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", torus, ring, err));
    CHECK(ring.getVertexCount() > 100);
    CHECK(meshIndicesInRange(ring));
    CHECK(meshNormalsFiniteUnit(ring));
    // Torus should have non-zero volume magnitude.
    CHECK(std::fabs(meshApproxSignedVolume(ring)) > 0.01f);

    Params axis;
    axis.setSeed(3);
    axis.setInt("nx", 10);
    axis.setInt("ny", 14);
    axis.setInt("nz", 12);
    axis.setString("field", "sphere");
    axis.setFloat("radius", 0.65f);
    MeshBuild m;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", axis, m, err));
    CHECK(m.getVertexCount() > 50);
}

TEST_CASE("procgen.mesh.marchingcubes.sphereVolumeSignStable") {
    Params p;
    p.setSeed(1);
    p.setInt("resolution", 18);
    p.setString("field", "sphere");
    p.setFloat("radius", 0.7f);
    MeshBuild a, b;
    std::string err;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, a, err));
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p, b, err));
    const float va = meshApproxSignedVolume(a);
    const float vb = meshApproxSignedVolume(b);
    CHECK(std::fabs(va) > 0.05f);
    CHECK(approxEq(va, vb, 1e-5f));
    // Winding must be consistent across seeds of the same field recipe family.
    Params p2 = p;
    p2.setSeed(9);
    MeshBuild c;
    CHECK(MeshRecipeRegistry::instance().generate("mesh.marchingcubes", p2, c, err));
    const float vc = meshApproxSignedVolume(c);
    CHECK(va * vc > 0.f);  // same sign
}

TEST_CASE("procgen.mesh.marchingcubes.moduleNullAndList") {
    Procgen *mod = Procgen::create();
    CHECK(mod->buildMesh("mesh.marchingcubes", nullptr) == nullptr);
    CHECK(mod->lastError().find("null") != std::string::npos);
    CHECK(mod->generateMesh("mesh.marchingcubes", nullptr, nullptr) == nullptr);

    CHECK(mod->getMeshRecipeCount() >= 1);
    bool found = false;
    for (int i = 0; i < mod->getMeshRecipeCount(); ++i) {
        if (mod->getMeshRecipeId(i) == "mesh.marchingcubes") found = true;
    }
    CHECK(found);
}

TEST_CASE("procgen.palette.applyToLayer") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(1);
    p.setSize(16, 12);
    Grid2D *grid = mod->generate("dungeon.bsp", &p);
    CHECK(grid != nullptr);

    mod->setPaletteGid("test", "wall", 10);
    mod->setPaletteGid("test", "floor", 20);
    mod->setPaletteGid("test", "corridor", 21);

    eve::map::TileLayer *layer = eve::map::TileLayer::createLayer(16, 12, 16.f, 16.f);
    CHECK(mod->applyToLayer(grid, "test", layer));
    bool sawMapped = false;
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int gid = layer->getTile(x, y);
            const int sem = grid->getCell(x, y);
            if (sem == int(Semantic::Wall)) {
                CHECK_EQ(gid, 10);
                sawMapped = true;
            } else if (sem == int(Semantic::Floor)) {
                CHECK_EQ(gid, 20);
                sawMapped = true;
            } else if (sem == int(Semantic::Corridor)) {
                CHECK_EQ(gid, 21);
                sawMapped = true;
            }
        }
    }
    CHECK(sawMapped);
    delete grid;
    layer->release();
}

TEST_CASE("procgen.json.export") {
    Params p;
    p.setSeed(3);
    p.setSize(12, 10);
    Grid2D g;
    std::string err;
    CHECK(GeneratorRegistry::instance().generate("dungeon.bsp", p, g, err));
    const std::string json = gridToJson(g);
    CHECK(json.find("\"width\":12") != std::string::npos);
    CHECK(json.find("\"cells\":") != std::string::npos);
    CHECK(json.find("spawn") != std::string::npos);
}

TEST_CASE("procgen.texture.noiseField.seamlessReproducible") {
    NoiseField a{42, 8, 8};
    NoiseField b{42, 8, 8};
    CHECK_EQ(a.fbm(1.25f, 3.5f, 4), b.fbm(1.25f, 3.5f, 4));
    // Period wrap: x and x+period should match on lattice contribution.
    CHECK_EQ(a.hash01(0, 0), a.hash01(8, 0));
    CHECK_EQ(a.hash01(0, 0), a.hash01(0, 8));
}

TEST_CASE("procgen.texture.colorRamp.banded") {
    ColorRamp ramp;
    ramp.add(0.f, 0, 0, 0);
    ramp.add(1.f, 255, 255, 255);
    const Rgba8 c0 = ramp.sampleBanded(0.1f, 3);
    const Rgba8 c1 = ramp.sampleBanded(0.9f, 3);
    CHECK(c0.r < c1.r);
}

TEST_CASE("procgen.texture.recipes.reproducible") {
    TextureRecipeRegistry::instance().registerBuiltins();
    const char *ids[] = {"tex.soil", "tex.stone", "tex.marble", "tex.water", "tex.sky_cloud"};
    for (const char *id : ids) {
        Params p;
        p.setSeed(11);
        p.setSize(32, 32);
        p.setInt("colors", 5);
        p.setInt("pixelSize", 2);
        p.setInt("seamless", 1);
        std::string err;
        // Fully qualify: `using namespace eve::procgen` would make bare `image::`
        // look outside `eve::image`.
        eve::image::ImageData *a = TextureRecipeRegistry::instance().generate(id, p, err);
        eve::image::ImageData *b = TextureRecipeRegistry::instance().generate(id, p, err);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK_EQ(a->getWidth(), 32);
        CHECK_EQ(a->getHeight(), 32);
        CHECK_EQ(a->getFormat(), std::string("RGBA8"));
        CHECK_EQ(a->getSize(), b->getSize());
        CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) == 0);
        delete a;
        delete b;
    }
}

TEST_CASE("procgen.texture.generateImage.andNormal") {
    Procgen *mod = Procgen::create();
    Params p;
    p.setSeed(3);
    p.setSize(48, 48);
    p.setInt("colors", 6);
    eve::image::ImageData *img = mod->generateImage("tex.marble", &p);
    REQUIRE(img != nullptr);
    eve::image::ImageData *nrm = mod->generateNormalImage("tex.marble", &p);
    REQUIRE(nrm != nullptr);
    CHECK_EQ(nrm->getWidth(), 48);
    CHECK_EQ(nrm->getHeight(), 48);
    delete img;
    delete nrm;
    CHECK(mod->hasTextureRecipe("tex.soil"));
    CHECK(mod->getTextureRecipeCount() >= 5);
}

static Color colorForSemantic(int sem) {
    switch (sem) {
    case int(Semantic::Wall):
        return Color(0.22f, 0.24f, 0.30f, 1.f);
    case int(Semantic::Floor):
        return Color(0.72f, 0.68f, 0.55f, 1.f);
    case int(Semantic::Corridor):
        return Color(0.55f, 0.52f, 0.42f, 1.f);
    case int(Semantic::Water):
        return Color(0.25f, 0.45f, 0.85f, 1.f);
    case int(Semantic::Sand):
        return Color(0.85f, 0.78f, 0.45f, 1.f);
    case int(Semantic::Grass):
        return Color(0.35f, 0.65f, 0.30f, 1.f);
    case int(Semantic::Dirt):
        return Color(0.55f, 0.40f, 0.25f, 1.f);
    case int(Semantic::Stone):
        return Color(0.55f, 0.58f, 0.62f, 1.f);
    case int(Semantic::Snow):
        return Color(0.90f, 0.93f, 0.97f, 1.f);
    case int(Semantic::Door):
        return Color(0.75f, 0.45f, 0.20f, 1.f);
    default:
        return Color(0.05f, 0.05f, 0.07f, 1.f);
    }
}

static void drawGrid(Graphics *gfx, const Grid2D &grid, float originX, float originY, float cell) {
    const int w = grid.getWidth();
    const int h = grid.getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Color c = colorForSemantic(grid.getCell(x, y));
            gfx->drawSolidRect(originX + float(x) * cell, originY + float(y) * cell, cell, cell, c);
        }
    }
    // Markers for spawn / objects.
    for (int i = 0; i < grid.getObjectCount(); ++i) {
        const float ox = originX + grid.getObjectX(i) * cell;
        const float oy = originY + grid.getObjectY(i) * cell;
        gfx->drawSolidRect(ox - 2.f, oy - 2.f, 5.f, 5.f, Color(1.f, 0.3f, 0.3f, 1.f));
    }
}

TEST_CASE("procgen.render.dungeonCaveMazePreview") {
    GeneratorRegistry::instance().registerBuiltins();

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 720;
    s.height = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    struct Algo {
        const char *id;
        int w, h;
    };
    const Algo algos[] = {
        {"dungeon.bsp", 36, 28},
        {"cave.cellular", 36, 28},
        {"maze.backtrack", 31, 23},
        {"noise.terrain", 36, 28},
    };

    gfx->setBackgroundColorRGBA(0.06f, 0.07f, 0.09f, 1.f);
    int drawnCells = 0;

    for (int ai = 0; ai < 4; ++ai) {
        const Algo &algo = algos[ai];
        Params p;
        p.setSeed(42);
        p.setSize(algo.w, algo.h);
        if (std::string(algo.id) == "cave.cellular") {
            p.setInt("loops", 4);
            p.setFloat("fill", 0.45f);
        } else if (std::string(algo.id) == "noise.terrain") {
            p.setFloat("frequency", 5.f);
            p.setInt("octaves", 3);
        }

        Grid2D grid;
        std::string err;
        REQUIRE(GeneratorRegistry::instance().generate(algo.id, p, grid, err));

        const float cell = std::min(16.f, std::min(float(gfx->getWidth() - 40) / float(algo.w),
                                                   float(gfx->getHeight() - 40) / float(algo.h)));
        const float originX = (float(gfx->getWidth()) - float(algo.w) * cell) * 0.5f;
        const float originY = (float(gfx->getHeight()) - float(algo.h) * cell) * 0.5f;

        for (int frame = 0; frame < 45; ++frame) {
            gfx->clearScreen();
            drawGrid(gfx, grid, originX, originY, cell);
            // Title bar strip so algorithm changes are readable without fonts.
            const float barW = float(gfx->getWidth()) * (0.15f + 0.2f * float(ai));
            gfx->drawSolidRect(12.f, 10.f, barW, 8.f, Color(0.9f, 0.85f, 0.4f, 1.f));
            gfx->present();

            drawnCells = algo.w * algo.h;
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) break;
            }
            SDL_Delay(16);
        }
    }

    CHECK_GT(drawnCells, 100);
    win->close();
}
