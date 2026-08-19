#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstring>
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
#include "graphics/RenderSystem.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/ImageData.h"
#include "window/Window.h"

using namespace eve::graphics;

TEST_CASE("Quad.getUVMapsPixelRect") {
    Quad q(16, 8, 32, 16);
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    q.getUV(64, 64, u0, v0, u1, v1);
    CHECK(std::abs(u0 - 0.25f) < 1e-5f);
    CHECK(std::abs(v0 - 0.125f) < 1e-5f);
    CHECK(std::abs(u1 - 0.75f) < 1e-5f);
    CHECK(std::abs(v1 - 0.375f) < 1e-5f);

    q.setViewport(0, 0, 64, 64);
    q.getUV(64, 64, u0, v0, u1, v1);
    CHECK(std::abs(u0) < 1e-5f);
    CHECK(std::abs(v0) < 1e-5f);
    CHECK(std::abs(u1 - 1.f) < 1e-5f);
    CHECK(std::abs(v1 - 1.f) < 1e-5f);
}

TEST_CASE("Quad.spriteAtlasSamplesSubRect") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    // Atlas: left half red, right half green.
    constexpr int TW = 32, TH = 16;
    std::vector<uint8_t> px(size_t(TW) * size_t(TH) * 4);
    for (int y = 0; y < TH; ++y) {
        for (int x = 0; x < TW; ++x) {
            size_t i = (size_t(y) * size_t(TW) + size_t(x)) * 4;
            bool left = x < TW / 2;
            px[i + 0] = left ? 255 : 0;
            px[i + 1] = left ? 0 : 255;
            px[i + 2] = 0;
            px[i + 3] = 255;
        }
    }
    eve::image::ImageData imageData(TW, TH, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    Texture *atlas = gfx->newTexture(&imageData);
    REQUIRE(atlas != nullptr);

    Quad *right = gfx->newQuad(TW / 2, 0, TW / 2, TH);
    REQUIRE(right != nullptr);

    Canvas *rt = gfx->newCanvas(64, 64);
    REQUIRE(rt != nullptr);

    auto *cam = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x = 32.f;
    cam->data()->y = 32.f;
    cam->data()->zoom = 1.f;
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    auto *sp = Renderable2D::create();
    sp->transform()->x = 16.f;
    sp->transform()->y = 16.f;
    sp->sprite()->width = 32.f;
    sp->sprite()->height = 32.f;
    sp->sprite()->texture = atlas;
    sp->sprite()->quad = right;
    sp->sprite()->receiveLight = false;
    sp->sprite()->canvas = rt;
    sp->sprite()->visible = true;

    RenderSystem::render(*gfx);

    Color mid = rt->getPixel(32, 32);
    CHECK(mid.g > 0.7f);
    CHECK(mid.r < 0.2f);

    sp->sprite()->visible = false;
    cam->data()->active = false;
    win->close();
}
