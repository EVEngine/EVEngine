#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "data/ByteData.h"
#include "font/Font.h"
#include "font/FontData.h"
#include "graphics/Font.h"
#include "graphics/Graphics.h"
#include "image/ImageData.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace eve::graphics;

namespace {

// Same fixture used by test/font.cpp: FontAwesome, which only has icon
// glyphs (no printable ASCII) — its one well-known bitmap lives at U+F000.
constexpr int         kIconCodepoint = 0xF000;
constexpr const char *kIconUtf8      = "\xEF\x80\x80";  // UTF-8 for U+F000

std::string pathBesideThisSource(const char *filename) {
    std::string here  = __FILE__;
    auto        slash = here.find_last_of("/\\");
    std::string dir   = (slash == std::string::npos) ? std::string(".") : here.substr(0, slash);
    return dir + "/" + filename;
}

std::vector<char> readBinaryFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::unique_ptr<eve::font::FontData> loadFontAwesome(int size = 32) {
    auto raw = readBinaryFile(pathBesideThisSource("fonts/FontAwesome.ttf"));
    if (raw.empty()) return nullptr;
    eve::data::ByteData data(raw.data(), raw.size());
    return std::unique_ptr<eve::font::FontData>(eve::font::Font::create()->newFontData(&data, size));
}

// Prepares a small headless window + Graphics pair, matching the pattern
// used by test/RenderSystem.cpp's GraphicsSmoke.* cases.
struct GraphicsFixture {
    eve::window::Window *win = nullptr;
    Graphics             *gfx = nullptr;

    GraphicsFixture(int w = 320, int h = 240) {
        win = eve::window::Window::create();
        gfx = Graphics::create();
        win->setGraphics(gfx);
        eve::window::WindowSettings s;
        s.width    = static_cast<uint16_t>(w);
        s.height   = static_cast<uint16_t>(h);
        s.centered = true;
        win->setWindowSettings(s);
    }
    ~GraphicsFixture() { win->close(); }
};

bool expectException(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const eve::Exception &) {
        return true;
    }
    return false;
}

}  // namespace

TEST_CASE("graphics.font.newFontBuildsAtlasForCharset") {
    auto fontData = loadFontAwesome(32);
    REQUIRE(fontData.get() != nullptr);

    GraphicsFixture fx;
    REQUIRE(fx.gfx != nullptr);

    std::unique_ptr<Font> gfont(fx.gfx->newFont(fontData.get(), kIconUtf8));
    REQUIRE(gfont.get() != nullptr);
    REQUIRE(gfont->getTexture() != nullptr);
    CHECK(gfont->getTexture()->getWidth() > 0);
    CHECK(gfont->getTexture()->getHeight() > 0);

    CHECK(gfont->getHeight() > 0.f);
    CHECK(gfont->getAscent() > 0.f);
    CHECK(gfont->getBaseline() > 0.f);
    CHECK(gfont->getWidth(kIconUtf8) > 0.f);

    // In the atlas (built only for kIconUtf8's codepoint), the icon is present
    // but an arbitrary ASCII letter is not — even though FontData itself might
    // resolve it differently; atlas coverage is what Font::hasGlyph reports.
    CHECK(gfont->hasGlyph(kIconCodepoint));
    CHECK(!gfont->hasGlyph(static_cast<int>('A')));

    const Font::Glyph *g = gfont->findGlyph(kIconCodepoint);
    REQUIRE(g != nullptr);
    CHECK(g->width > 0);
    CHECK(g->height > 0);
    CHECK(g->advance > 0);
    CHECK(gfont->findGlyph(static_cast<int>('A')) == nullptr);
}

TEST_CASE("graphics.font.newFontDefaultCharsetDoesNotCrash") {
    auto fontData = loadFontAwesome(24);
    REQUIRE(fontData.get() != nullptr);

    GraphicsFixture fx;
    // FontAwesome has no printable ASCII glyphs, so most/all of the default
    // charset will be skipped — this exercises the "no glyphs rasterized"
    // fallback path (a valid 1x1 placeholder atlas) without throwing.
    std::unique_ptr<Font> gfont(fx.gfx->newFont(fontData.get()));
    REQUIRE(gfont.get() != nullptr);
    REQUIRE(gfont->getTexture() != nullptr);
}

TEST_CASE("graphics.print.throwsWithoutFont") {
    GraphicsFixture fx;
    fx.gfx->setFont(nullptr);
    CHECK(fx.gfx->getFont() == nullptr);
    CHECK(expectException([&] { fx.gfx->print("hello", 0, 0); }));
}

TEST_CASE("graphics.print.rendersGlyphPixelsOnCanvas") {
    auto fontData = loadFontAwesome(32);
    REQUIRE(fontData.get() != nullptr);

    GraphicsFixture fx;
    std::unique_ptr<Font> gfont(fx.gfx->newFont(fontData.get(), kIconUtf8));
    REQUIRE(gfont.get() != nullptr);
    fx.gfx->setFont(gfont.get());
    CHECK(fx.gfx->getFont() == gfont.get());

    Canvas *rt = fx.gfx->newCanvas(64, 64);
    REQUIRE(rt != nullptr);

    fx.gfx->setCanvas(rt);
    fx.gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    fx.gfx->print(kIconUtf8, 4.f, 4.f, Color(1.f, 1.f, 1.f, 1.f));
    fx.gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> img(rt->newImageData());
    REQUIRE(img.get() != nullptr);
    const auto *px = static_cast<const unsigned char *>(img->getData());
    const int    w = img->getWidth();
    const int    h = img->getHeight();

    // The glyph should have lit up at least some pixels away from the
    // all-black clear color somewhere in the canvas.
    long long litPixels = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char *p = px + (static_cast<size_t>(y) * w + x) * 4;
            if (p[0] > 40 || p[1] > 40 || p[2] > 40) ++litPixels;
        }
    }
    CHECK(litPixels > 0);

    // Hold the window open so the glyph is visible while debugging (~2s).
    // Left: magnified canvas from the assert above. Right: live print at 3x.
    fx.gfx->setBackgroundColorRGBA(0.08f, 0.08f, 0.1f, 1.f);
    for (int frame = 0; frame < 60; ++frame) {
        fx.gfx->clearScreen();
        fx.gfx->drawTexturedRect(rt->getTexture(), 16.f, 16.f, 128.f, 128.f,
                                 Color(1.f, 1.f, 1.f, 1.f));
        fx.gfx->print(kIconUtf8, 160.f, 40.f, Color(1.f, 1.f, 1.f, 1.f), 3.f);
        fx.gfx->present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    // Reset so other tests don't inherit this font.
    fx.gfx->setFont(nullptr);
}
