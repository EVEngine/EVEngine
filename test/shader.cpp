#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#include "common/Exception.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/Shader.h"
#include "graphics/shaders/custom2d_frag_spv.inc"
#include "window/Window.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

TEST_CASE("graphics.Shader.declareAndSendUniforms") {
    Shader sh;
    CHECK_EQ(sh.declareFloat("time"), 0);
    CHECK_EQ(sh.declareVec4("tint"), 1);
    CHECK_EQ(sh.usedFloats(), 5);
    CHECK(sh.hasUniform("time"));
    CHECK(sh.hasUniform("tint"));

    sh.sendFloat("time", 1.5f);
    sh.sendVec4("tint", 0.2f, 0.4f, 0.6f, 1.f);

    float t = 0.f;
    REQUIRE_EQ(sh.getFromVar("time", &t, sizeof(t)), int(sizeof(t)));
    CHECK(std::abs(t - 1.5f) < 1e-5f);

    float tint[4] = {};
    REQUIRE_EQ(sh.getFromVar("tint", tint, sizeof(tint)), int(sizeof(tint)));
    CHECK(std::abs(tint[0] - 0.2f) < 1e-5f);
    CHECK(std::abs(tint[3] - 1.f) < 1e-5f);

    bool threw = false;
    try {
        sh.sendFloat("missing", 1.f);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        sh.declareFloat("time");  // same size OK
        sh.declareVec2("time");   // different size → error
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("graphics.Shader.customSpvOnSprite") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    std::vector<uint32_t> frag(custom2d_frag_spv, custom2d_frag_spv + custom2d_frag_spv_count);
    Shader *shader = gfx->newShaderFromSpv({}, frag);
    REQUIRE(shader != nullptr);
    REQUIRE(shader->gpuHandle != nullptr);
    CHECK_EQ(shader->declareFloat("factor"), 0);
    shader->sendFloat("factor", 0.5f);

    // White 2x2 texture
    const uint8_t px[16] = {255, 255, 255, 255, 255, 255, 255, 255,
                            255, 255, 255, 255, 255, 255, 255, 255};
    Texture *tex = gfx->newTexture(2, 2, px);
    REQUIRE(tex != nullptr);

    auto *ent = Renderable2D::create();
    ent->transform()->x = 40;
    ent->transform()->y = 40;
    ent->sprite()->width = 80;
    ent->sprite()->height = 80;
    ent->sprite()->texture = tex;
    ent->sprite()->shader = shader;
    ent->sprite()->visible = true;

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.f, 0.f, 0.f, 1.f));

    // Warm up a couple frames (swapchain acquire can soft-fail once).
    for (int i = 0; i < 3; ++i) RenderSystem::render(*gfx);

    Color c = gfx->getPixel(80, 80);
    // factor 0.5 on white → ~0.5 RGB (allow tolerance for sRGB/swapchain format)
    CHECK_GT(c.r, 0.15f);
    CHECK_LT(c.r, 0.85f);
    CHECK(std::abs(c.r - c.g) < 0.15f);
}

#if !defined(_WIN32)
TEST_CASE("graphics.Shader.newShaderGlslc") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 160;
    s.height = 120;
    s.centered = true;
    if (!win->setWindowSettings(s)) return;

    const char *frag = R"(#version 450
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;
void main() {
  outColor = texture(MainTex, fragUV) * fragColor * u.data[0];
}
)";
    try {
        Shader *sh = gfx->newShader(frag);
        REQUIRE(sh != nullptr);
        sh->declareFloat("factor");
        sh->sendFloat("factor", 1.f);
        gfx->setShader(sh);
        CHECK(gfx->getShader() == sh);
        gfx->setShader();
        CHECK(gfx->getShader() == nullptr);
    } catch (const eve::Exception &e) {
        // glslc missing on CI hosts is acceptable — mark as soft skip
        const char *msg = e.what();
        bool missing = msg && (std::strstr(msg, "glslc") != nullptr);
        CHECK(missing);
    }
}
#endif
