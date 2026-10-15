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
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <vector>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::spatial;
using namespace eve::graphics;

[[maybe_unused]] static std::set<int> collectResults(int count, const std::function<int(int)> &getId) {
    std::set<int> out;
    for (int i = 0; i < count; ++i) out.insert(getId(i));
    return out;
}


TEST_CASE("spatial.quadtree.queryPreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
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
