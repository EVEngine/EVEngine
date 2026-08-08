#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "procgen/Procgen.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"
#include "procgen/JsonExport.h"
#include "map/TileLayer.h"

#include <set>
#include <string>
#include <vector>

using namespace eve::procgen;

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
