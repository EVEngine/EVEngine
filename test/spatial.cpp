#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "spatial/BSPTree2D.h"
#include "spatial/BSPTree3D.h"
#include "spatial/Octree.h"
#include "spatial/QuadTree.h"
#include "spatial/Spatial.h"
#include "spatial/SpatialHash2D.h"
#include "spatial/SpatialHash3D.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <vector>

using namespace eve::spatial;
using namespace eve::graphics;

static std::set<int> collectResults(int count, const std::function<int(int)> &getId) {
    std::set<int> out;
    for (int i = 0; i < count; ++i) out.insert(getId(i));
    return out;
}

TEST_CASE("spatial.module.name") {
    auto *mod = Spatial::create();
    CHECK_EQ(mod->getName(), std::string("Spatial"));
    CHECK_EQ(Spatial::create(), mod);
}

TEST_CASE("spatial.quadtree.queryRect") {
    std::unique_ptr<QuadTree> tree(new QuadTree(0, 0, 100, 100, 6, 4));
    CHECK(tree->insert(1, 10, 10, 20, 20));
    CHECK(tree->insert(2, 50, 50, 60, 60));
    CHECK(tree->insert(3, 80, 10, 90, 20));
    CHECK_EQ(tree->getCount(), 3);

    int n = tree->queryRect(0, 0, 30, 30);
    CHECK_EQ(n, 1);
    CHECK_EQ(tree->getResultId(0), 1);

    n = tree->queryPoint(55, 55);
    CHECK_EQ(n, 1);
    CHECK_EQ(tree->getResultId(0), 2);

    n = tree->queryCircle(85, 15, 5);
    CHECK_EQ(n, 1);
    CHECK_EQ(tree->getResultId(0), 3);

    CHECK(tree->remove(2));
    CHECK(!tree->contains(2));
    CHECK_EQ(tree->getCount(), 2);
    CHECK_EQ(tree->queryPoint(55, 55), 0);
}

TEST_CASE("spatial.octree.queryAABB") {
    std::unique_ptr<Octree> tree(new Octree(0, 0, 0, 100, 100, 100, 6, 4));
    CHECK(tree->insert(1, 10, 10, 10, 20, 20, 20));
    CHECK(tree->insert(2, 70, 70, 70, 80, 80, 80));
    CHECK_EQ(tree->queryAABB(0, 0, 0, 30, 30, 30), 1);
    CHECK_EQ(tree->getResultId(0), 1);
    CHECK_EQ(tree->querySphere(75, 75, 75, 8), 1);
    CHECK_EQ(tree->getResultId(0), 2);
    CHECK(tree->update(1, 71, 71, 71, 72, 72, 72));
    CHECK_EQ(tree->queryAABB(0, 0, 0, 30, 30, 30), 0);
    CHECK_EQ(tree->querySphere(75, 75, 75, 10), 2);
}

TEST_CASE("spatial.hash2d.query") {
    std::unique_ptr<SpatialHash2D> hash(new SpatialHash2D(16.f));
    CHECK(hash->insert(10, 0, 0, 8, 8));
    CHECK(hash->insert(11, 40, 40, 48, 48));
    CHECK(hash->insert(12, 14, 14, 18, 18));  // spans cells
    CHECK_EQ(hash->queryRect(0, 0, 10, 10), 1);
    CHECK_EQ(hash->getResultId(0), 10);
    CHECK_EQ(hash->queryPoint(16, 16), 1);
    CHECK_EQ(hash->getResultId(0), 12);
    CHECK(hash->remove(11));
    CHECK_EQ(hash->queryCircle(44, 44, 5), 0);
}

TEST_CASE("spatial.hash3d.query") {
    std::unique_ptr<SpatialHash3D> hash(new SpatialHash3D(16.f));
    CHECK(hash->insert(1, 0, 0, 0, 5, 5, 5));
    CHECK(hash->insert(2, 32, 32, 32, 40, 40, 40));
    CHECK_EQ(hash->queryPoint(2, 2, 2), 1);
    CHECK_EQ(hash->getResultId(0), 1);
    CHECK_EQ(hash->queryAABB(30, 30, 30, 50, 50, 50), 1);
    CHECK_EQ(hash->getResultId(0), 2);
}

TEST_CASE("spatial.bsp2d.query") {
    std::unique_ptr<BSPTree2D> tree(new BSPTree2D(0, 0, 200, 200, 10, 2));
    for (int i = 0; i < 20; ++i) {
        float x = float(i) * 8.f;
        CHECK(tree->insert(100 + i, x, x, x + 4.f, x + 4.f));
    }
    CHECK_EQ(tree->getCount(), 20);
    int n = tree->queryRect(0, 0, 20, 20);
    CHECK(n >= 2);
    auto ids = collectResults(n, [&](int i) { return tree->getResultId(i); });
    CHECK(ids.count(100) == 1);
    CHECK(ids.count(101) == 1);
    CHECK_EQ(tree->queryCircle(1000, 1000, 1), 0);
}

TEST_CASE("spatial.bsp3d.query") {
    std::unique_ptr<BSPTree3D> tree(new BSPTree3D(0, 0, 0, 100, 100, 100, 10, 2));
    CHECK(tree->insert(1, 5, 5, 5, 10, 10, 10));
    CHECK(tree->insert(2, 50, 50, 50, 55, 55, 55));
    CHECK_EQ(tree->queryAABB(0, 0, 0, 15, 15, 15), 1);
    CHECK_EQ(tree->getResultId(0), 1);
    CHECK_EQ(tree->querySphere(52, 52, 52, 5), 1);
    CHECK_EQ(tree->getResultId(0), 2);
}

TEST_CASE("spatial.factories") {
    auto *mod = Spatial::create();
    std::unique_ptr<QuadTree> qt(mod->newQuadTree(0, 0, 10, 10));
    std::unique_ptr<Octree> ot(mod->newOctree(0, 0, 0, 10, 10, 10));
    std::unique_ptr<SpatialHash2D> h2(mod->newSpatialHash2D(8.f));
    std::unique_ptr<SpatialHash3D> h3(mod->newSpatialHash3D(8.f));
    std::unique_ptr<BSPTree2D> b2(mod->newBSPTree2D(0, 0, 10, 10));
    std::unique_ptr<BSPTree3D> b3(mod->newBSPTree3D(0, 0, 0, 10, 10, 10));
    // zeroerr CHECK() pretty-prints the expression value; unique_ptr is not copyable.
    CHECK(qt.get() != nullptr);
    CHECK(ot.get() != nullptr);
    CHECK(h2.get() != nullptr);
    CHECK(h3.get() != nullptr);
    CHECK(b2.get() != nullptr);
    CHECK(b3.get() != nullptr);
    CHECK(qt->insert(1, 1, 1, 2, 2));
    CHECK_EQ(qt->queryPoint(1.5f, 1.5f), 1);
}

TEST_CASE("spatial.invalidBounds") {
    bool threw = false;
    try {
        QuadTree t(0, 0, 0, 10);
        (void)t;
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        SpatialHash2D h(0.f);
        (void)h;
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("spatial.quadtree.queryPreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 520;
    s.height = 520;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    const float world = 400.f;
    const float pad = 60.f;
    std::unique_ptr<QuadTree> tree(new QuadTree(0, 0, world, world, 8, 4));

    struct Item {
        int id;
        float minX, minY, maxX, maxY;
        Color color;
    };
    std::vector<Item> items = {
        {1, 40, 40, 90, 90, Color(0.9f, 0.4f, 0.35f, 1.f)},
        {2, 160, 50, 210, 110, Color(0.35f, 0.75f, 0.95f, 1.f)},
        {3, 280, 30, 360, 80, Color(0.45f, 0.9f, 0.5f, 1.f)},
        {4, 60, 180, 120, 240, Color(0.95f, 0.85f, 0.35f, 1.f)},
        {5, 200, 200, 260, 270, Color(0.8f, 0.5f, 0.95f, 1.f)},
        {6, 310, 170, 370, 230, Color(0.95f, 0.55f, 0.75f, 1.f)},
        {7, 30, 300, 100, 370, Color(0.55f, 0.85f, 0.8f, 1.f)},
        {8, 180, 320, 250, 380, Color(0.7f, 0.7f, 0.4f, 1.f)},
        {9, 300, 300, 380, 380, Color(0.6f, 0.6f, 0.95f, 1.f)},
    };
    for (const Item &it : items)
        REQUIRE(tree->insert(it.id, it.minX, it.minY, it.maxX, it.maxY));

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.1f, 1.f);
    int hitFrames = 0;

    for (int frame = 0; frame < 100; ++frame) {
        const float t = float(frame) * 0.05f;
        const float qw = 90.f;
        const float qh = 90.f;
        const float qx = 40.f + (world - qw - 40.f) * (0.5f + 0.5f * std::sin(t));
        const float qy = 40.f + (world - qh - 40.f) * (0.5f + 0.5f * std::cos(t * 0.8f));

        const int n = tree->queryRect(qx, qy, qx + qw, qy + qh);
        std::set<int> hits = collectResults(n, [&](int i) { return tree->getResultId(i); });
        if (!hits.empty()) ++hitFrames;

        gfx->clearScreen();
        // World frame
        gfx->drawSolidRect(pad - 2.f, pad - 2.f, world + 4.f, world + 4.f,
                           Color(0.2f, 0.22f, 0.28f, 1.f));
        gfx->drawSolidRect(pad, pad, world, world, Color(0.1f, 0.11f, 0.14f, 1.f));

        for (const Item &it : items) {
            const bool hit = hits.count(it.id) > 0;
            Color c = it.color;
            if (hit) {
                c.r = std::min(1.f, c.r + 0.25f);
                c.g = std::min(1.f, c.g + 0.25f);
                c.b = std::min(1.f, c.b + 0.25f);
            }
            gfx->drawSolidRect(pad + it.minX, pad + it.minY, it.maxX - it.minX, it.maxY - it.minY,
                               c);
            if (hit) {
                gfx->drawSolidRect(pad + it.minX - 2.f, pad + it.minY - 2.f, it.maxX - it.minX + 4.f,
                                   3.f, Color(1.f, 1.f, 1.f, 1.f));
            }
        }

        // Query window outline
        gfx->drawSolidRect(pad + qx, pad + qy, qw, 3.f, Color(1.f, 0.95f, 0.4f, 1.f));
        gfx->drawSolidRect(pad + qx, pad + qy + qh - 3.f, qw, 3.f, Color(1.f, 0.95f, 0.4f, 1.f));
        gfx->drawSolidRect(pad + qx, pad + qy, 3.f, qh, Color(1.f, 0.95f, 0.4f, 1.f));
        gfx->drawSolidRect(pad + qx + qw - 3.f, pad + qy, 3.f, qh, Color(1.f, 0.95f, 0.4f, 1.f));

        gfx->present();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_GT(hitFrames, 20);
    win->close();
}
