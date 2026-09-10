#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include "ManualPreview.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "image/ImageData.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::graphics;

static Color colorForSemantic(int sem) {
    switch (sem) {
        case int(Semantic::Wall): return Color(0.22f, 0.24f, 0.30f, 1.f);
        case int(Semantic::Floor): return Color(0.72f, 0.68f, 0.55f, 1.f);
        case int(Semantic::Corridor): return Color(0.55f, 0.52f, 0.42f, 1.f);
        case int(Semantic::Water): return Color(0.25f, 0.45f, 0.85f, 1.f);
        case int(Semantic::Sand): return Color(0.85f, 0.78f, 0.45f, 1.f);
        case int(Semantic::Grass): return Color(0.35f, 0.65f, 0.30f, 1.f);
        case int(Semantic::Dirt): return Color(0.55f, 0.40f, 0.25f, 1.f);
        case int(Semantic::Stone): return Color(0.55f, 0.58f, 0.62f, 1.f);
        case int(Semantic::Snow): return Color(0.90f, 0.93f, 0.97f, 1.f);
        case int(Semantic::Door): return Color(0.75f, 0.45f, 0.20f, 1.f);
        default: return Color(0.05f, 0.05f, 0.07f, 1.f);
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
    eve::window::WindowSettings s;
    s.width    = 720;
    s.height   = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    struct Algo {
        const char *id;
        int         w, h;
    };
    const Algo algos[] = {
        {"dungeon.bsp", 36, 28},
        {"cave.cellular", 36, 28},
        {"maze.backtrack", 31, 23},
        {"noise.terrain", 36, 28},
    };

    gfx->setBackgroundColorRGBA(0.06f, 0.07f, 0.09f, 1.f);
    gfx->setScreenReadbackEnabled(true);

    for (int ai = 0; ai < 4; ++ai) {
        const Algo &algo = algos[ai];
        Params      p;
        p.setSeed(42);
        p.setSize(algo.w, algo.h);
        if (std::string(algo.id) == "cave.cellular") {
            p.setInt("loops", 4);
            p.setFloat("fill", 0.45f);
        } else if (std::string(algo.id) == "noise.terrain") {
            p.setFloat("frequency", 5.f);
            p.setInt("octaves", 3);
        }

        Grid2D      grid;
        std::string err;
        REQUIRE(GeneratorRegistry::instance().generate(algo.id, p, grid, err));

        const float cell = std::min(
            16.f, std::min(float(gfx->getWidth() - 40) / float(algo.w), float(gfx->getHeight() - 40) / float(algo.h)));
        const float originX = (float(gfx->getWidth()) - float(algo.w) * cell) * 0.5f;
        const float originY = (float(gfx->getHeight()) - float(algo.h) * cell) * 0.5f;

        for (int frame = 0; frame < (eve::test::manualPreviewEnabled() ? 45 : 1); ++frame) {
            gfx->clearScreen();
            drawGrid(gfx, grid, originX, originY, cell);
            // Title bar strip so algorithm changes are readable without fonts.
            const float barW = float(gfx->getWidth()) * (0.15f + 0.2f * float(ai));
            gfx->drawSolidRect(12.f, 10.f, barW, 8.f, Color(0.9f, 0.85f, 0.4f, 1.f));
            gfx->present();


            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) break;
            }
            if (eve::test::manualPreviewEnabled()) SDL_Delay(16);
        }
        std::unique_ptr<eve::image::ImageData> image(gfx->newImageData());
        REQUIRE(image != nullptr);
        std::set<int> semantics;
        int           matchingCells = 0;
        for (int y = 0; y < grid.getHeight(); ++y) {
            for (int x = 0; x < grid.getWidth(); ++x) {
                const int semantic = grid.getCell(x, y);
                semantics.insert(semantic);
                const auto expected = colorForSemantic(semantic);
                const auto actual =
                    image->getPixel(int(originX + (float(x) + 0.5f) * cell), int(originY + (float(y) + 0.5f) * cell));
                if (std::fabs(actual.r - expected.r) < 0.08f && std::fabs(actual.g - expected.g) < 0.08f &&
                    std::fabs(actual.b - expected.b) < 0.08f)
                    ++matchingCells;
            }
        }
        REQUIRE(semantics.size() >= 2);
        // Object markers may cover a few cells; the grid must still be recognizable.
        REQUIRE(matchingCells > grid.getWidth() * grid.getHeight() * 9 / 10);
    }

    win->close();
}
