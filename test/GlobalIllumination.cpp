#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "Fixtures.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <memory>
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
#include "window/Window.h"

using namespace eve::graphics;

namespace {


Texture *makePackedAlbedoDepth(Graphics *gfx, int w, int h) {
    std::vector<uint8_t> px(size_t(w * h * 4));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t((y * w + x) * 4);
            const bool wall = (x > w / 3 && x < w / 2);
            px[i + 0] = wall ? 220 : 40;
            px[i + 1] = wall ? 40 : 40;
            px[i + 2] = wall ? 40 : 40;
            px[i + 3] = wall ? 80 : 200;
        }
    }
    return gfx->newTexture(w, h, px.data());
}

}  // namespace

TEST_CASE("gi.qualityAndApplyFromDepth") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    GlobalIllumination *raw = gfx->newGlobalIllumination();
    REQUIRE(raw != nullptr);
    std::unique_ptr<GlobalIllumination> gi(raw);
    REQUIRE(gi->getShader() != nullptr);
    CHECK(gi->getQuality() == "medium");
    CHECK(gi->getSampleCount() >= 8);

    gi->setQuality("low");
    CHECK(gi->getQuality() == "low");
    CHECK(gi->getSampleCount() == 8);
    gi->setQuality("high");
    CHECK(gi->getSampleCount() == 24);
    gi->setQuality("nope");
    CHECK(gi->getQuality() == "medium");

    gi->setQuality("low");
    gi->setIntensity(0.8f);
    gi->setRadius(1.1f);
    gi->setLightDirection(0.4f, 1.f, 0.3f);
    gi->setLightColor(1.f, 0.9f, 0.7f);
    gi->setCamera(0.f, 1.f, 4.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 60.f, 320.f / 240.f, 0.1f, 40.f);

    Texture *packed = makePackedAlbedoDepth(gfx, 64, 48);
    REQUIRE(packed != nullptr);
    Canvas *dest = gfx->newCanvas(64, 48);
    REQUIRE(dest != nullptr);
    gi->applyFromDepthTo(gfx, packed, dest);
    REQUIRE(dest->getTexture() != nullptr);

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    CHECK(rc->supports("ao"));
    CHECK(rc->supports("gi"));
    CHECK(rc->isEnabled("gi"));
    CHECK(rc->isEnabled("ao"));

    win->close();
}
