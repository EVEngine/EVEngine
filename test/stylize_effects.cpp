// Effect / material shaders ported from common Unity and Godot projects.
// Covers registry/schema (no GPU), GPU post passes, and GPU mesh-shader
// creation. Per-module, process-isolated like the rest of test/stylize.cpp.

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "stylize/StyleInstance.h"
#include "stylize/StylePass.h"
#include "stylize/Stylize.h"
#include "window/Window.h"

using eve::graphics::Canvas;
using eve::graphics::Graphics;
using eve::graphics::Shader;
using eve::graphics::Texture;
using eve::image::ImageData;
using eve::stylize::StyleInstance;
using eve::stylize::StylePass;
using eve::stylize::Stylize;
using Colorf = ImageData::Colorf;

namespace {

const char* kMeshEffects[] = {"rim", "dissolve", "hologram", "snow"};
const char* kPostEffects[] = {"vignette", "chromatic", "grain"};

bool near(float a, float b, float eps = 0.02f) { return std::fabs(a - b) <= eps; }

ImageData* makeGradient(int w, int h) {
    auto* img = new ImageData(w, h, "RGBA8");
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float u = float(x) / float(std::max(w - 1, 1));
            float v = float(y) / float(std::max(h - 1, 1));
            img->setPixel(x, y, Colorf{u, 0.35f + 0.4f * v, 1.f - u, 1.f});
        }
    }
    return img;
}

}  // namespace

TEST_CASE("stylize.effects.registryAndSchema") {
    auto* mod = Stylize::create();
    REQUIRE(mod != nullptr);

    for (const char* id : kMeshEffects) {
        CHECK(mod->hasStyle(id));
        CHECK(mod->hasMeshStyle(id));
        CHECK(!mod->supports(id, "post"));
        CHECK(mod->supports(id, "mesh"));
    }
    for (const char* id : kPostEffects) {
        CHECK(mod->hasStyle(id));
        CHECK(!mod->hasMeshStyle(id));
        CHECK(mod->supports(id, "post"));
        CHECK(!mod->supports(id, "mesh"));
    }
    CHECK(!mod->hasStyle("bloom"));

    // Params are clamped to [min,max] and expose defaults.
    std::unique_ptr<StyleInstance> dissolve(mod->newInstance("dissolve"));
    REQUIRE(dissolve.get() != nullptr);
    CHECK(dissolve->hasParam("amount"));
    CHECK(dissolve->hasParam("edgeColorR"));
    CHECK(!dissolve->hasParam("nope"));
    CHECK_EQ(dissolve->getParamName(0), std::string("amount"));
    CHECK_EQ(dissolve->getFloat("amount"), dissolve->getParamDefault("amount"));
    dissolve->setFloat("amount", 5.f);
    CHECK_EQ(dissolve->getFloat("amount"), dissolve->getParamMax("amount"));
    dissolve->setFloat("amount", -3.f);
    CHECK_EQ(dissolve->getFloat("amount"), dissolve->getParamMin("amount"));
    CHECK(dissolve->isOverridden("amount"));
    dissolve->resetAll();
    CHECK(!dissolve->isOverridden("amount"));

    std::unique_ptr<StyleInstance> vignette(mod->newInstance("vignette"));
    REQUIRE(vignette.get() != nullptr);
    CHECK(vignette->hasParam("strength"));
    CHECK_EQ(vignette->getStage(), std::string("afterTonemap"));
    CHECK(vignette->requiresInput("color"));
    CHECK(!vignette->requiresInput("depth"));

    // Unknown styles still throw.
    bool threw = false;
    try {
        mod->newInstance("oil");
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("stylize.effects.gpu.postPasses") {
    auto* win = eve::window::Window::create();
    auto* gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 128;
    s.height   = 96;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto* mod = Stylize::create();

    for (const char* id : kPostEffects) {
        StylePass* pass = mod->newPass(gfx, id);
        REQUIRE(pass != nullptr);
        CHECK_EQ(pass->getStyle(), std::string(id));
        REQUIRE(pass->getShader() != nullptr);
        REQUIRE(pass->getShader()->gpuHandle != nullptr);
        if (pass->hasParam("time")) pass->setTime(0.25f);

        std::unique_ptr<ImageData> img(makeGradient(64, 48));
        auto*                      tex = gfx->newTexture(img.get());
        REQUIRE(tex != nullptr);

        Canvas* rt = gfx->newCanvas(128, 96);
        REQUIRE(rt != nullptr);
        gfx->setCanvas(rt);
        gfx->clear(eve::graphics::Color(0.1f, 0.1f, 0.1f, 1.f), std::nullopt, std::nullopt);
        pass->apply(gfx, tex);
        gfx->setCanvas();

        eve::graphics::Color p = rt->getPixel(64, 48);
        CHECK_GT(p.a, 0.5f);
        delete pass;
    }

    win->close();
}

TEST_CASE("stylize.effects.gpu.meshShaders") {
    auto* win = eve::window::Window::create();
    auto* gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 128;
    s.height   = 96;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto* mod = Stylize::create();

    Shader* rim = mod->newMeshShader(gfx, "rim");
    REQUIRE(rim != nullptr);
    REQUIRE(rim->gpuHandle != nullptr);
    CHECK(rim->hasUniform("rimPower"));

    Shader* dissolve = mod->newMeshShader(gfx, "dissolve");
    REQUIRE(dissolve != nullptr);
    REQUIRE(dissolve->gpuHandle != nullptr);
    CHECK(dissolve->hasUniform("amount"));

    Shader* hologram = mod->newMeshShader(gfx, "hologram");
    REQUIRE(hologram != nullptr);
    REQUIRE(hologram->gpuHandle != nullptr);
    CHECK(hologram->hasUniform("scanDensity"));

    Shader* snow = mod->newMeshShader(gfx, "snow");
    REQUIRE(snow != nullptr);
    REQUIRE(snow->gpuHandle != nullptr);
    CHECK(snow->hasUniform("snowAmount"));

    bool threw = false;
    try {
        mod->newMeshShader(gfx, "vignette");
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);

    win->close();
}