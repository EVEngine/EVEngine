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
#include "map/Map.h"
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "map/TileLayer.h"
#include "map/TileOrientation.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <string>
#include <vector>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::map;
using namespace eve::graphics;

namespace {

void fillOpen(TileLayer *layer, int floorGid = 2) { layer->fill(floorGid); }

void wallRect(TileLayer *layer, int x0, int y0, int x1, int y1, int wallGid = 1) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) layer->setTile(x, y, wallGid);
}

bool pathEndsAt(Path *p, int x, int y) {
    if (!p || p->getLength() <= 0) return false;
    return p->getX(p->getLength() - 1) == x && p->getY(p->getLength() - 1) == y;
}

bool pathStartsAt(Path *p, int x, int y) {
    if (!p || p->getLength() <= 0) return false;
    return p->getX(0) == x && p->getY(0) == y;
}

bool pathWalkable(Path *p, Pathfinder *pf) {
    if (!p) return false;
    for (int i = 0; i < p->getLength(); ++i) {
        if (!pf->isWalkable(p->getX(i), p->getY(i))) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("map.path.topology.parse") {
    auto *mod = Map::create();
    Pathfinder *pf = mod->newPathfinderSize(2, 2);
    pf->setTopology("ortho4");
    CHECK_EQ(pf->getTopology(), std::string("ortho4"));
    pf->setTopology("ortho8");
    CHECK_EQ(pf->getTopology(), std::string("ortho8"));
    pf->setTopology("hex");
    CHECK_EQ(pf->getTopology(), std::string("hex"));
    pf->setTopology("nope");  // fallback keeps last valid (hex)
    CHECK_EQ(pf->getTopology(), std::string("hex"));
    delete pf;
}

TEST_CASE("map.path.astar.orthoBypassWall") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(8, 5, 16.f, 16.f);
    fillOpen(layer);
    for (int y = 0; y < 5; ++y) layer->setTile(3, y, 1);
    layer->setTile(3, 2, 2);

    Pathfinder *pf = mod->newPathfinder(layer);
    REQUIRE(pf != nullptr);
    pf->blockGid(1);
    pf->setTopology("ortho4");

    Path *path = pf->findPath(0, 2, 7, 2);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK(pathStartsAt(path, 0, 2));
    CHECK(pathEndsAt(path, 7, 2));
    CHECK(pathWalkable(path, pf));
    bool viaGap = false;
    for (int i = 0; i < path->getLength(); ++i) {
        if (path->getX(i) == 3 && path->getY(i) == 2) viaGap = true;
    }
    CHECK(viaGap);
    delete path;
    delete pf;
}

TEST_CASE("map.path.astar.unreachable") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(5, 3, 8.f, 8.f);
    fillOpen(layer);
    for (int y = 0; y < 3; ++y) layer->setTile(2, y, 1);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    Path *path = pf->findPath(0, 1, 4, 1);
    REQUIRE(path != nullptr);
    CHECK_EQ(path->getLength(), 0);
    delete path;
    delete pf;
}

TEST_CASE("map.path.astar.ortho8NoCornerCut") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(3, 3, 8.f, 8.f);
    fillOpen(layer);
    layer->setTile(1, 1, 1);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("ortho8");
    Path *path = pf->findPath(0, 1, 2, 1);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK(pathEndsAt(path, 2, 1));
    CHECK(pathWalkable(path, pf));
    CHECK(path->getLength() >= 5);
    delete path;
    delete pf;
}

TEST_CASE("map.path.customGrid") {
    auto *mod = Map::create();
    Pathfinder *pf = mod->newPathfinderSize(4, 4);
    REQUIRE(pf != nullptr);
    pf->setTopology("ortho4");
    pf->setBlocked(1, 0, true);
    pf->setBlocked(1, 1, true);
    pf->setBlocked(1, 2, true);
    Path *blocked = pf->findPath(0, 1, 3, 1);
    CHECK_EQ(blocked->getLength(), 0);
    delete blocked;

    pf->setBlocked(1, 1, false);
    Path *path = pf->findPath(0, 1, 3, 1);
    CHECK(pathStartsAt(path, 0, 1));
    CHECK(pathEndsAt(path, 3, 1));
    delete path;
    delete pf;
}

TEST_CASE("map.path.flowField.groupSameGoal") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(10, 8, 16.f, 16.f);
    fillOpen(layer);
    wallRect(layer, 4, 0, 4, 5);
    layer->setTile(4, 6, 2);

    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("ortho4");

    const int gx = 9, gy = 1;
    FlowField *field = pf->buildFlowField(gx, gy);
    REQUIRE(field != nullptr);
    CHECK_EQ(field->getGoalX(), gx);
    CHECK_EQ(field->getGoalY(), gy);
    CHECK(field->isReachable(0, 0));
    CHECK(field->isReachable(1, 7));

    int starts[][2] = {{0, 0}, {0, 7}, {2, 3}};
    for (auto &s : starts) {
        Path *p = pf->followFlow(field, s[0], s[1]);
        CHECK(pathStartsAt(p, s[0], s[1]));
        CHECK(pathEndsAt(p, gx, gy));
        CHECK(pathWalkable(p, pf));
        delete p;
    }

    Path *g0 = pf->findGroupPath(0, 0, gx, gy);
    Path *g1 = pf->findGroupPath(2, 3, gx, gy);
    CHECK(pathEndsAt(g0, gx, gy));
    CHECK(pathEndsAt(g1, gx, gy));
    delete g0;
    delete g1;
    delete field;
    delete pf;
}

TEST_CASE("map.path.flowField.matchesAstarReachability") {
    auto *mod = Map::create();
    Pathfinder *pf = mod->newPathfinderSize(6, 6);
    pf->setTopology("ortho4");
    for (int y = 0; y < 5; ++y) pf->setBlocked(3, y, true);

    Path *a = pf->findPath(0, 0, 5, 0);
    Path *b = pf->findGroupPath(0, 0, 5, 0);
    CHECK_EQ(a->getLength(), 0);
    CHECK_EQ(b->getLength(), 0);
    delete a;
    delete b;

    pf->setBlocked(3, 5, false);
    Path *a2 = pf->findPath(0, 0, 5, 0);
    Path *b2 = pf->findGroupPath(0, 0, 5, 0);
    CHECK(a2->getLength() > 0);
    CHECK(b2->getLength() > 0);
    CHECK(pathEndsAt(a2, 5, 0));
    CHECK(pathEndsAt(b2, 5, 0));
    delete a2;
    delete b2;
    delete pf;
}

TEST_CASE("map.path.hex.staggeredNeighborsReach") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(5, 5, 64.f, 32.f);
    layer->config()->orientation = MapOrientation::Staggered;
    layer->config()->staggerAxis = StaggerAxis::Y;
    layer->config()->staggerIndex = StaggerIndex::Odd;
    fillOpen(layer);

    Pathfinder *pf = mod->newPathfinder(layer);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("hex"));

    Path *path = pf->findPath(0, 0, 4, 4);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK(pathEndsAt(path, 4, 4));
    // Consecutive steps must be adjacent (chebyshev distance 1 in offset space is not
    // enough for hex; verify each step changes coords and stays walkable).
    for (int i = 1; i < path->getLength(); ++i) {
        const int x0 = path->getX(i - 1), y0 = path->getY(i - 1);
        const int x1 = path->getX(i), y1 = path->getY(i);
        const int adx = x1 > x0 ? x1 - x0 : x0 - x1;
        const int ady = y1 > y0 ? y1 - y0 : y0 - y1;
        CHECK(adx + ady > 0);
        CHECK(adx <= 1);
        CHECK(ady <= 1);
        CHECK(pf->isWalkable(x1, y1));
    }
    delete path;
    delete pf;
}

TEST_CASE("map.path.isometric.autoUsesOrtho") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(4, 4, 64.f, 32.f);
    layer->config()->orientation = MapOrientation::Isometric;
    fillOpen(layer);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("ortho4"));
    Path *path = pf->findPath(0, 0, 3, 3);
    CHECK(pathEndsAt(path, 3, 3));
    delete path;

    pf->setDiagonal(true);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("ortho8"));
    delete pf;
}

TEST_CASE("map.path.blockGid.syncFromLayer") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(3, 1, 8.f, 8.f);
    layer->setTile(0, 0, 2);
    layer->setTile(1, 0, 2);
    layer->setTile(2, 0, 2);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    Path *ok = pf->findPath(0, 0, 2, 0);
    CHECK(ok->getLength() > 0);
    delete ok;

    layer->setTile(1, 0, 1);
    pf->syncFromLayer();
    Path *blocked = pf->findPath(0, 0, 2, 0);
    CHECK_EQ(blocked->getLength(), 0);
    delete blocked;
    delete pf;
}

TEST_CASE("map.path.cellCost.prefersCheaper") {
    auto *mod = Map::create();
    Pathfinder *pf = mod->newPathfinderSize(3, 3);
    pf->setTopology("ortho4");
    pf->setCellCost(1, 1, 10.f);
    Path *path = pf->findPath(0, 1, 2, 1);
    REQUIRE(path->getLength() > 0);
    bool throughCenter = false;
    for (int i = 0; i < path->getLength(); ++i)
        if (path->getX(i) == 1 && path->getY(i) == 1) throughCenter = true;
    CHECK(!throughCenter);
    delete path;
    delete pf;
}

TEST_CASE("map.path.nullLayerReturnsNull") {
    auto *mod = Map::create();
    CHECK(mod->newPathfinder(nullptr) == nullptr);
    CHECK(mod->newPathfinderSize(0, 5) == nullptr);
}

TEST_CASE("map.path.astar.routePreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = 640;
    s.height = 400;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    const int mw = 16;
    const int mh = 10;
    auto *mod = Map::create();
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
    const float ox = (float(gfx->getWidth()) - float(mw) * cell) * 0.5f;
    const float oy = (float(gfx->getHeight()) - float(mh) * cell) * 0.5f;

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);
    int markerAt = 0;

    for (int frame = 0; frame < 100; ++frame) {
        gfx->clearScreen();
        for (int y = 0; y < mh; ++y) {
            for (int x = 0; x < mw; ++x) {
                const int gid = layer->getTile(x, y);
                Color c = (gid == 1) ? Color(0.25f, 0.28f, 0.35f, 1.f)
                                     : Color(0.55f, 0.58f, 0.48f, 1.f);
                gfx->drawSolidRect(ox + float(x) * cell, oy + float(y) * cell, cell - 1.f,
                                   cell - 1.f, c);
            }
        }

        // Draw full path corridor.
        for (int i = 0; i < path->getLength(); ++i) {
            const float px = ox + float(path->getX(i)) * cell + 6.f;
            const float py = oy + float(path->getY(i)) * cell + 6.f;
            gfx->drawSolidRect(px, py, cell - 12.f, cell - 12.f, Color(0.35f, 0.75f, 1.f, 1.f));
        }

        // Animate a runner along the path.
        markerAt = (frame / 2) % path->getLength();
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

TEST_CASE("map.path.accessorRoundTrips") {
    auto *mod = Map::create();
    Pathfinder *pf = mod->newPathfinderSize(4, 4);
    REQUIRE(pf != nullptr);

    pf->setDiagonal(true);
    CHECK(pf->getDiagonal());
    pf->setDiagonal(false);
    CHECK(!pf->getDiagonal());

    pf->setBlockEmpty(true);
    CHECK(pf->getBlockEmpty());
    pf->setBlockEmpty(false);
    CHECK(!pf->getBlockEmpty());

    pf->setCellCost(1, 1, 2.5f);
    CHECK_EQ(pf->getCellCost(1, 1), 2.5f);
    CHECK_EQ(pf->getCellCost(9, 9), 0.f);  // out of bounds

    pf->blockGid(7);
    pf->setBlocked(0, 0, true);
    pf->unblockGid(7);
    pf->clearBlockedGids();
    pf->invalidateCache();
    // setBlocked cells stay blocked until explicitly cleared; gid blocking is gone.
    CHECK(!pf->isWalkable(0, 0));
    pf->setBlocked(0, 0, false);
    CHECK(pf->isWalkable(0, 0));

    delete pf;
}

TEST_CASE("map.path.boundLayerAutoSyncsRevision") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(5, 1, 8.f, 8.f);
    layer->fill(2);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    Path *initial = pf->findPath(0, 0, 4, 0);
    CHECK_GT(initial->getLength(), 0);
    delete initial;
    layer->setTile(2, 0, 1);
    Path *blocked = pf->findPath(0, 0, 4, 0);
    CHECK_EQ(blocked->getLength(), 0);
    delete blocked;
    layer->setTile(2, 0, 2);
    Path *open = pf->findPath(0, 0, 4, 0);
    CHECK_GT(open->getLength(), 0);
    delete open;
    delete pf;
}
