#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "Fixtures.h"
#include "common/Exception.h"
#include "data/ByteData.h"
#include "font/Font.h"
#include "font/FontData.h"
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

#include "PathBesideSource.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;
EVE_DEFINE_PATH_BESIDE_SOURCE()

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

    GfxFixture fx(320, 240, /*useHeadless=*/true);
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

    GfxFixture fx(320, 240, /*useHeadless=*/true);
    // FontAwesome has no printable ASCII glyphs, so most/all of the default
    // charset will be skipped — this exercises the "no glyphs rasterized"
    // fallback path (a valid 1x1 placeholder atlas) without throwing.
    std::unique_ptr<Font> gfont(fx.gfx->newFont(fontData.get()));
    REQUIRE(gfont.get() != nullptr);
    REQUIRE(gfont->getTexture() != nullptr);
}

// Shared coverage for Vulkan and WebGPU font rendering.
TEST_CASE("graphics.drawText.throwsWithNullFont") {
    GfxFixture fx(320, 240, /*useHeadless=*/true);
    fx.gfx->setFont(nullptr);
    CHECK(fx.gfx->getFont() == nullptr);
    CHECK(expectException([&] { fx.gfx->print("hello", 0.f, 0.f); }));
    CHECK(expectException([&] { fx.gfx->drawText(nullptr, "hello", 12.f, 18.f); }));
}

TEST_CASE("graphics.drawText.rendersAtRequestedPosition") {
    auto fontData = loadFontAwesome(32);
    REQUIRE(fontData.get() != nullptr);

    GfxFixture fx(320, 240, /*useHeadless=*/true);
    std::unique_ptr<Font> gfont(fx.gfx->newFont(fontData.get(), kIconUtf8));
    REQUIRE(gfont.get() != nullptr);
    fx.gfx->setFont(gfont.get());
    CHECK(fx.gfx->getFont() == gfont.get());
    fx.gfx->print("", 0.f, 0.f);
    fx.gfx->setFont(nullptr);

    Canvas *rt = fx.gfx->newCanvas(96, 96);
    REQUIRE(rt != nullptr);
    fx.gfx->setCanvas(rt);
    fx.gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    fx.gfx->drawText(gfont.get(), kIconUtf8, 48.f, 48.f);
    fx.gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> img(rt->newImageData());
    REQUIRE(img.get() != nullptr);
    const auto *px = static_cast<const unsigned char *>(img->getData());
    long long   litNearRequestedPosition = 0;
    long long   litInOppositeCorner      = 0;
    for (int y = 0; y < img->getHeight(); ++y) {
        for (int x = 0; x < img->getWidth(); ++x) {
            const unsigned char *p = px + (static_cast<size_t>(y) * img->getWidth() + x) * 4;
            if (p[0] <= 40 && p[1] <= 40 && p[2] <= 40) continue;
            if (x >= 44 && y >= 44) ++litNearRequestedPosition;
            if (x < 32 && y < 32) ++litInOppositeCorner;
        }
    }
    CHECK(litNearRequestedPosition > 0);
    CHECK(litInOppositeCorner == 0);

    CHECK(fx.gfx->getFont() == nullptr);
}

TEST_CASE("graphics.drawText.rendersGlyphPixelsOnCanvas") {
    auto fontData = loadFontAwesome(32);
    REQUIRE(fontData.get() != nullptr);

    GfxFixture fx(320, 240, /*useHeadless=*/true);
    std::unique_ptr<Font> gfont(fx.gfx->newFont(fontData.get(), kIconUtf8));
    REQUIRE(gfont.get() != nullptr);
    fx.gfx->setFont(nullptr);

    Canvas *rt = fx.gfx->newCanvas(64, 64);
    REQUIRE(rt != nullptr);

    fx.gfx->setCanvas(rt);
    fx.gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    fx.gfx->drawText(gfont.get(), kIconUtf8, 4.f, 4.f, Color(1.f, 1.f, 1.f, 1.f));
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

    // Windowed-only debug preview: hold the window open so the glyph is visible
    // while debugging (~2s). Skipped in headless mode (present() is a no-op).
    if (!fx.gfx->isHeadless()) {
        fx.gfx->setBackgroundColorRGBA(0.08f, 0.08f, 0.1f, 1.f);
        for (int frame = 0; frame < 60; ++frame) {
            fx.gfx->clearScreen();
            fx.gfx->drawTexturedRect(rt->getTexture(), 16.f, 16.f, 128.f, 128.f,
                                     Color(1.f, 1.f, 1.f, 1.f));
            fx.gfx->drawText(gfont.get(), kIconUtf8, 160.f, 40.f,
                             Color(1.f, 1.f, 1.f, 1.f), 3.f);
            fx.gfx->present();

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) break;
            }
            SDL_Delay(16);
        }
    }

    CHECK(fx.gfx->getFont() == nullptr);
}
