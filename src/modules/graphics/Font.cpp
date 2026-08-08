#include "graphics/Font.h"

#include "common/Exception.h"
#include "font/FontData.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

namespace eve::graphics {

uint32_t nextCodepointUtf8(const std::string &text, size_t &i) {
    if (i >= text.size()) return 0;
    const auto  *s   = reinterpret_cast<const unsigned char *>(text.data() + i);
    const size_t rem = text.size() - i;
    unsigned char c0 = s[0];
    if (c0 < 0x80) {
        i += 1;
        return c0;
    }
    if ((c0 & 0xE0) == 0xC0 && rem >= 2 && (s[1] & 0xC0) == 0x80) {
        i += 2;
        return ((c0 & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((c0 & 0xF0) == 0xE0 && rem >= 3 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        i += 3;
        return ((c0 & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    if ((c0 & 0xF8) == 0xF0 && rem >= 4 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
        (s[3] & 0xC0) == 0x80) {
        i += 4;
        return ((c0 & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    i += 1;
    return 0xFFFD;
}

namespace {

std::vector<int> decodeCodepoints(const std::string &text) {
    std::vector<int> out;
    size_t           i = 0;
    while (i < text.size()) {
        uint32_t cp = nextCodepointUtf8(text, i);
        if (cp == 0) continue;
        out.push_back(static_cast<int>(cp));
    }
    return out;
}

}  // namespace

std::string Font::defaultCharset() {
    std::string s;
    s.reserve(95);
    for (int c = 0x20; c <= 0x7E; ++c) s.push_back(static_cast<char>(c));
    return s;
}

Font::Font(Graphics *gfx, font::FontData *fontData, std::string charset) : data(fontData) {
    if (gfx == nullptr) throw eve::Exception("Font: Graphics instance is null");
    if (data == nullptr) throw eve::Exception("Font: FontData is null");

    struct Raster {
        int                                  codepoint;
        std::unique_ptr<image::ImageData> bitmap;
        int                                  bearingX, bearingY, advance;
    };

    std::vector<int>    codepoints = decodeCodepoints(charset);
    std::vector<Raster> rasters;
    rasters.reserve(codepoints.size());

    // De-dup while preserving first occurrence (charset may repeat codepoints).
    std::unordered_map<int, bool> seen;
    seen.reserve(codepoints.size());

    long long totalArea    = 0;
    int       maxGlyphSide = 1;
    const int padding      = 1;

    for (int cp : codepoints) {
        if (seen.count(cp)) continue;
        seen[cp] = true;
        if (!data->hasGlyph(cp)) continue;

        std::unique_ptr<image::ImageData> bmp(data->newGlyphImageData(cp));
        const int                           w = bmp ? bmp->getWidth() : 0;
        const int                           h = bmp ? bmp->getHeight() : 0;
        totalArea += static_cast<long long>(w + padding) * static_cast<long long>(h + padding);
        maxGlyphSide = std::max({maxGlyphSide, w, h});

        rasters.push_back({cp, std::move(bmp), data->getGlyphBearingX(cp), data->getGlyphBearingY(cp),
                            data->getGlyphAdvance(cp)});
    }

    if (rasters.empty()) {
        // No requested codepoint decoded (e.g. empty charset) — still hand back a
        // valid 1x1 texture so getTexture() is always safe to use.
        image::ImageData blank(1, 1, "RGBA8");
        atlas = gfx->newTexture(&blank);
        return;
    }

    // Shelf-pack tallest-first into a width chosen from the total glyph area,
    // then grow height as needed (no re-packing / no width growth pass).
    int atlasWidth = std::max<int>(64, static_cast<int>(std::sqrt(static_cast<double>(totalArea)) * 1.2) + 1);
    atlasWidth     = std::max(atlasWidth, maxGlyphSide + 2 * padding);

    std::vector<size_t> order(rasters.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return rasters[a].bitmap->getHeight() > rasters[b].bitmap->getHeight();
    });

    struct Rect {
        int x = 0, y = 0;
    };
    std::vector<Rect> placed(rasters.size());

    int penX = padding, penY = padding, shelfH = 0;
    int atlasHeight = padding;
    for (size_t idx : order) {
        const int w = rasters[idx].bitmap->getWidth();
        const int h = rasters[idx].bitmap->getHeight();
        if (penX + w + padding > atlasWidth) {
            penX   = padding;
            penY += shelfH + padding;
            shelfH = 0;
        }
        placed[idx] = {penX, penY};
        shelfH      = std::max(shelfH, h);
        penX += w + padding;
        atlasHeight = std::max(atlasHeight, penY + shelfH + padding);
    }

    image::ImageData atlasImage(atlasWidth, atlasHeight, "RGBA8");
    glyphs.reserve(rasters.size());
    for (size_t idx = 0; idx < rasters.size(); ++idx) {
        const Raster &r = rasters[idx];
        const int      w = r.bitmap->getWidth();
        const int      h = r.bitmap->getHeight();

        Glyph g;
        g.width    = w;
        g.height   = h;
        g.bearingX = r.bearingX;
        g.bearingY = r.bearingY;
        g.advance  = r.advance;

        if (w > 0 && h > 0) {
            atlasImage.paste(r.bitmap.get(), placed[idx].x, placed[idx].y, 0, 0, w, h);
            g.u0 = static_cast<float>(placed[idx].x) / static_cast<float>(atlasWidth);
            g.v0 = static_cast<float>(placed[idx].y) / static_cast<float>(atlasHeight);
            g.u1 = static_cast<float>(placed[idx].x + w) / static_cast<float>(atlasWidth);
            g.v1 = static_cast<float>(placed[idx].y + h) / static_cast<float>(atlasHeight);
        }

        glyphs.emplace(r.codepoint, g);
    }

    atlas = gfx->newTexture(&atlasImage);
}

Font::~Font() = default;

float Font::getHeight() const { return data->getLineHeight(); }
float Font::getAscent() const { return data->getAscent(); }
float Font::getBaseline() const { return data->getBaseline(); }

float Font::getWidth(const std::string &text) const { return data->getWidth(text); }

bool Font::hasGlyph(int codepoint) const { return glyphs.find(codepoint) != glyphs.end(); }

const Font::Glyph *Font::findGlyph(int codepoint) const {
    auto it = glyphs.find(codepoint);
    return it == glyphs.end() ? nullptr : &it->second;
}

}  // namespace eve::graphics
