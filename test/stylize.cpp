#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>
#include <optional>
#include <string>

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "image/ImageData.h"
#include "stylize/Stylize.h"
#include "window/Window.h"

using eve::graphics::Canvas;
using eve::graphics::Graphics;
using eve::graphics::Shader;
using eve::image::ImageData;
using eve::stylize::StylePass;
using eve::stylize::Stylize;
using Colorf = ImageData::Colorf;

namespace {

bool near(float a, float b, float eps = 0.08f) { return std::fabs(a - b) <= eps; }

ImageData *makeGradient(int w, int h) {
    auto *img = new ImageData(w, h, "RGBA8");
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float u = float(x) / float(std::max(w - 1, 1));
            float v = float(y) / float(std::max(h - 1, 1));
            Colorf c{u, 0.35f + 0.4f * v, 1.f - u, 1.f};
            // Hard edge for Sobel / outline styles.
            if (x > w / 2 && y > h / 3 && y < 2 * h / 3) c = Colorf{0.95f, 0.85f, 0.2f, 1.f};
            img->setPixel(x, y, c);
        }
    }
    return img;
}

}  // namespace

TEST_CASE("stylize.styles.registry") {
    auto *mod = Stylize::create();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("Stylize"));
    CHECK_EQ(mod->getStyleCount(), 4);
    CHECK(mod->hasStyle("cartoon"));
    CHECK(mod->hasStyle("watercolor"));
    CHECK(mod->hasStyle("ink"));
    CHECK(mod->hasStyle("pixel"));
    CHECK(!mod->hasStyle("oil"));
    CHECK(mod->hasMeshStyle("cartoon"));
    CHECK(mod->hasMeshStyle("ink"));
    CHECK(!mod->hasMeshStyle("watercolor"));
    CHECK(!mod->hasMeshStyle("pixel"));

    bool sawCartoon = false;
    for (int i = 0; i < mod->getStyleCount(); ++i) {
        if (mod->getStyleId(i) == "cartoon") sawCartoon = true;
    }
    CHECK(sawCartoon);
}

TEST_CASE("stylize.processImage.cpuAllStyles") {
    auto *mod = Stylize::create();
    std::unique_ptr<ImageData> src(makeGradient(32, 24));

    for (const char *style : {"cartoon", "watercolor", "ink", "pixel"}) {
        std::unique_ptr<ImageData> out(mod->processImage(src.get(), style));
        REQUIRE(out.get() != nullptr);
        CHECK_EQ(out->getWidth(), 32);
        CHECK_EQ(out->getHeight(), 24);
        CHECK_EQ(out->getFormat(), std::string("RGBA8"));
        Colorf p = out->getPixel(8, 8);
        CHECK(p.a > 0.9f);
        // Must differ from a flat identity copy somewhere in the image.
        bool changed = false;
        for (int y = 0; y < 24 && !changed; ++y) {
            for (int x = 0; x < 32 && !changed; ++x) {
                Colorf a = src->getPixel(x, y);
                Colorf b = out->getPixel(x, y);
                if (!near(a.r, b.r, 0.02f) || !near(a.g, b.g, 0.02f) || !near(a.b, b.b, 0.02f))
                    changed = true;
            }
        }
        CHECK(changed);
    }

    bool threw = false;
    try {
        mod->processImage(src.get(), "unknown");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("stylize.processImage.inkIsDesaturated") {
    auto *mod = Stylize::create();
    std::unique_ptr<ImageData> src(makeGradient(48, 32));
    std::unique_ptr<ImageData> ink(mod->processImage(src.get(), "ink"));
    REQUIRE(ink.get() != nullptr);
    // Ink wash should collapse toward gray/sepia paper — chroma reduced.
    int nearGray = 0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 48; ++x) {
            Colorf c = ink->getPixel(x, y);
            float mx = std::max(c.r, std::max(c.g, c.b));
            float mn = std::min(c.r, std::min(c.g, c.b));
            if (mx - mn < 0.25f) ++nearGray;
        }
    }
    CHECK_GT(nearGray, 48 * 32 / 2);
}

TEST_CASE("stylize.processImage.pixelQuantizes") {
    auto *mod = Stylize::create();
    std::unique_ptr<ImageData> src(makeGradient(64, 32));
    std::unique_ptr<ImageData> pix(mod->processImage(src.get(), "pixel"));
    REQUIRE(pix.get() != nullptr);
    // Neighboring pixels inside a 4x4 block should match (UV snap).
    Colorf a = pix->getPixel(0, 0);
    Colorf b = pix->getPixel(1, 1);
    CHECK(near(a.r, b.r, 0.001f));
    CHECK(near(a.g, b.g, 0.001f));
    CHECK(near(a.b, b.b, 0.001f));
}

TEST_CASE("stylize.gpu.postPassAndMeshShader") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 256;
    s.height = 192;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto *mod = Stylize::create();

    for (const char *style : {"cartoon", "watercolor", "ink", "pixel"}) {
        StylePass *pass = mod->newPass(gfx, style);
        REQUIRE(pass != nullptr);
        CHECK_EQ(pass->getStyle(), std::string(style));
        REQUIRE(pass->getShader() != nullptr);
        REQUIRE(pass->getShader()->gpuHandle != nullptr);
        CHECK(pass->hasParam("texelW"));
        if (pass->hasParam("time")) pass->setTime(0.25f);

        std::unique_ptr<ImageData> img(makeGradient(64, 48));
        auto *tex = gfx->newTexture(img.get());
        REQUIRE(tex != nullptr);

        Canvas *rt = gfx->newCanvas(128, 96);
        REQUIRE(rt != nullptr);
        gfx->setCanvas(rt);
        gfx->clear(::Color(0.1f, 0.1f, 0.1f, 1.f), std::nullopt, std::nullopt);
        pass->apply(gfx, tex);
        gfx->setCanvas();

        ::Color p = rt->getPixel(64, 48);
        CHECK_GT(p.a, 0.5f);
        delete pass;
    }

    Shader *toonMesh = mod->newMeshShader(gfx, "cartoon");
    REQUIRE(toonMesh != nullptr);
    REQUIRE(toonMesh->gpuHandle != nullptr);
    CHECK(toonMesh->hasUniform("bands"));

    Shader *inkMesh = mod->newMeshShader(gfx, "ink");
    REQUIRE(inkMesh != nullptr);
    REQUIRE(inkMesh->gpuHandle != nullptr);
    CHECK(inkMesh->hasUniform("washLevels"));

    bool threw = false;
    try {
        mod->newMeshShader(gfx, "watercolor");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}
