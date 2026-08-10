#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "procgen/Procgen.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"
#include "procgen/JsonExport.h"
#include "procgen/texture/TextureRecipe.h"
#include "procgen/texture/NoiseField.h"
#include "procgen/texture/ColorRamp.h"
#include "map/TileLayer.h"
#include "image/ImageData.h"
#include "graphics/Graphics.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
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
