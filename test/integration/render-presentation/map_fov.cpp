#include <SDL2/SDL.h>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
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
#include "map/TileOrientation.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// map_fov.cpp
using namespace eve::map;

namespace {

[[maybe_unused]] int countVisible(Fov *fov) {
    int n = 0;
    for (int y = 0; y < fov->getHeight(); ++y)
        for (int x = 0; x < fov->getWidth(); ++x)
            if (fov->isVisible(x, y)) ++n;
    return n;
}

}  // namespace


TEST_CASE("map.fov.gpuMaskTexture") {
    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 4;
    settings.height   = 4;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));

    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(4, 4);
    fov->setBlockEmpty(false);
    fov->addRevealer(1, 1, 1);
    fov->compute();
    CHECK(fov->buildMaskTexture(nullptr) == nullptr);

    auto *tex = fov->buildMaskTexture(gfx);
    REQUIRE(tex != nullptr);
    CHECK_EQ(tex->getWidth(), 4);
    CHECK_EQ(tex->getHeight(), 4);
    delete tex;
    delete fov;
    win->close();
}


// map_path.cpp
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::map;
using namespace eve::graphics;

namespace {

[[maybe_unused]] void fillOpen(TileLayer *layer, int floorGid = 2) { layer->fill(floorGid); }

[[maybe_unused]] void wallRect(TileLayer *layer, int x0, int y0, int x1, int y1, int wallGid = 1) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) layer->setTile(x, y, wallGid);
}

[[maybe_unused]] bool pathEndsAt(Path *p, int x, int y) {
    if (!p || p->getLength() <= 0) return false;
    return p->getX(p->getLength() - 1) == x && p->getY(p->getLength() - 1) == y;
}

[[maybe_unused]] bool pathStartsAt(Path *p, int x, int y) {
    if (!p || p->getLength() <= 0) return false;
    return p->getX(0) == x && p->getY(0) == y;
}

[[maybe_unused]] bool pathWalkable(Path *p, Pathfinder *pf) {
    if (!p) return false;
    for (int i = 0; i < p->getLength(); ++i) {
        if (!pf->isWalkable(p->getX(i), p->getY(i))) return false;
    }
    return true;
}

}  // namespace


TEST_CASE("map.path.astar.routePreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 640;
    s.height   = 400;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    const int  mw    = 16;
    const int  mh    = 10;
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(mw, mh, 16.f, 16.f);
    fillOpen(layer, 2);
    // Vertical wall with a gap, plus a few scattered blocks.
    for (int y = 0; y < mh; ++y) layer->setTile(6, y, 1);
    layer->setTile(6, 4, 2);
    layer->setTile(6, 5, 2);
    wallRect(layer, 10, 1, 10, 7, 1);
    layer->setTile(10, 3, 2);
    layer->setTile(3, 2, 1);
    layer->setTile(3, 7, 1);
    layer->setTile(12, 8, 1);

    Pathfinder *pf = mod->newPathfinder(layer);
    REQUIRE(pf != nullptr);
    pf->blockGid(1);
    pf->setTopology("ortho4");

    Path *path = pf->findPath(1, 4, 14, 4);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 0);
    CHECK(pathWalkable(path, pf));

    const float cell = 28.f;
    const float ox   = (float(gfx->getWidth()) - float(mw) * cell) * 0.5f;
    const float oy   = (float(gfx->getHeight()) - float(mh) * cell) * 0.5f;

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);
    int markerAt = 0;

    for (int frame = 0; frame < 100; ++frame) {
        gfx->clearScreen();
        for (int y = 0; y < mh; ++y) {
            for (int x = 0; x < mw; ++x) {
                const int gid = layer->getTile(x, y);
                Color     c   = (gid == 1) ? Color(0.25f, 0.28f, 0.35f, 1.f) : Color(0.55f, 0.58f, 0.48f, 1.f);
                gfx->drawSolidRect(ox + float(x) * cell, oy + float(y) * cell, cell - 1.f, cell - 1.f, c);
            }
        }

        // Draw full path corridor.
        for (int i = 0; i < path->getLength(); ++i) {
            const float px = ox + float(path->getX(i)) * cell + 6.f;
            const float py = oy + float(path->getY(i)) * cell + 6.f;
            gfx->drawSolidRect(px, py, cell - 12.f, cell - 12.f, Color(0.35f, 0.75f, 1.f, 1.f));
        }

        // Animate a runner along the path.
        markerAt       = (frame / 2) % path->getLength();
        const float mx = ox + float(path->getX(markerAt)) * cell + 4.f;
        const float my = oy + float(path->getY(markerAt)) * cell + 4.f;
        gfx->drawSolidRect(mx, my, cell - 8.f, cell - 8.f, Color(1.f, 0.45f, 0.25f, 1.f));

        // Start / goal markers.
        gfx->drawSolidRect(ox + 1.f * cell + 2.f, oy + 4.f * cell + 2.f, cell - 4.f, cell - 4.f,
                           Color(0.4f, 0.95f, 0.45f, 1.f));
        gfx->drawSolidRect(ox + 14.f * cell + 2.f, oy + 4.f * cell + 2.f, cell - 4.f, cell - 4.f,
                           Color(0.95f, 0.85f, 0.3f, 1.f));

        gfx->present();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_GT(path->getLength(), 8);
    delete path;
    delete pf;
    win->close();
}
