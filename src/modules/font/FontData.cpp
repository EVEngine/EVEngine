#include "font/FontData.h"

#include "common/Exception.h"
#include "image/ImageData.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include <algorithm>
#include <cstring>
#include <utility>

namespace eve {
namespace font {

namespace {

FT_Library &ftLibrary() {
    static struct Holder {
        FT_Library lib = nullptr;
        Holder() {
            if (FT_Init_FreeType(&lib) != 0)
                lib = nullptr;
        }
        // Deliberately no destructor (intentional leak): FontData instances are
        // ref-counted and the unified resource cache keeps them alive until
        // process exit. Static destruction order across TUs is unspecified, so
        // calling FT_Done_FreeType here could destroy the library before a
        // cached face's destructor runs FT_Done_Face on it. The library is
        // small and reclaimed by the OS at exit.
    } holder;
    return holder.lib;
}

// Decode one UTF-8 codepoint; advances `i`. Returns 0 on invalid / truncated.
uint32_t nextCodepoint(const std::string &text, size_t &i) {
    if (i >= text.size())
        return 0;
    const auto *s = reinterpret_cast<const unsigned char *>(text.data() + i);
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

}  // namespace

FontData::FontData(std::vector<uint8_t> fontBytes, int size, std::string uri)
    : Resource(std::move(uri)), bytes(std::move(fontBytes)), pixelSize(size) {
    if (bytes.empty())
        throw eve::Exception("Cannot decode empty font data");
    if (pixelSize <= 0)
        throw eve::Exception("Font pixel size must be positive");

    FT_Library lib = ftLibrary();
    if (!lib)
        throw eve::Exception("FreeType library init failed");

    FT_Error err = FT_New_Memory_Face(lib, bytes.data(), static_cast<FT_Long>(bytes.size()), 0, &face);
    if (err != 0 || !face)
        throw eve::Exception("Could not decode font data (FreeType error %d)", static_cast<int>(err));

    err = FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize));
    if (err != 0) {
        FT_Done_Face(face);
        face = nullptr;
        throw eve::Exception("Could not set font pixel size (FreeType error %d)", static_cast<int>(err));
    }

    glyphCache.resize(static_cast<size_t>(std::max<FT_Long>(face->num_glyphs, 1)));
}

FontData::~FontData() {
    if (face) {
        FT_Done_Face(face);
        face = nullptr;
    }
}

void FontData::adopt(eve::Resource &replacement) {
    auto &other = static_cast<FontData &>(replacement);
    std::swap(bytes, other.bytes);
    std::swap(face, other.face);
    std::swap(pixelSize, other.pixelSize);
    std::swap(glyphCache, other.glyphCache);
}

int FontData::getSize() const { return pixelSize; }

float FontData::getAscent() const {
    if (!face)
        return 0.f;
    return static_cast<float>(face->size->metrics.ascender) / 64.f;
}

float FontData::getDescent() const {
    if (!face)
        return 0.f;
    // FreeType descent is typically negative; expose as negative for layout.
    return static_cast<float>(face->size->metrics.descender) / 64.f;
}

float FontData::getLineHeight() const {
    if (!face)
        return static_cast<float>(pixelSize);
    return static_cast<float>(face->size->metrics.height) / 64.f;
}

float FontData::getBaseline() const { return getAscent(); }

std::string FontData::getFamilyName() const {
    if (!face || !face->family_name)
        return {};
    return face->family_name;
}

std::string FontData::getStyleName() const {
    if (!face || !face->style_name)
        return {};
    return face->style_name;
}

int FontData::getGlyphCount() const {
    if (!face)
        return 0;
    return static_cast<int>(face->num_glyphs);
}

unsigned int FontData::glyphIndex(int codepoint) const {
    if (!face || codepoint < 0)
        return 0;
    return FT_Get_Char_Index(face, static_cast<FT_ULong>(codepoint));
}

bool FontData::hasGlyph(int codepoint) const {
    return glyphIndex(codepoint) != 0;
}

bool FontData::hasGlyphs(std::string text) const {
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = nextCodepoint(text, i);
        if (cp == 0)
            continue;
        if (!hasGlyph(static_cast<int>(cp)))
            return false;
    }
    return true;
}

float FontData::getKerning(int leftCodepoint, int rightCodepoint) const {
    if (!face || !FT_HAS_KERNING(face))
        return 0.f;
    FT_UInt left = glyphIndex(leftCodepoint);
    FT_UInt right = glyphIndex(rightCodepoint);
    if (left == 0 || right == 0)
        return 0.f;
    FT_Vector delta;
    if (FT_Get_Kerning(face, left, right, FT_KERNING_DEFAULT, &delta) != 0)
        return 0.f;
    return static_cast<float>(delta.x) / 64.f;
}

const FontData::GlyphCache &FontData::ensureGlyph(int codepoint) const {
    static GlyphCache missing;
    if (!face)
        return missing;

    FT_UInt index = glyphIndex(codepoint);

    if (index >= glyphCache.size()) {
        glyphCache.resize(index + 1);
    }

    GlyphCache &slot = glyphCache[index];
    if (slot.loaded)
        return slot;

    FT_Error err = FT_Load_Glyph(face, index, FT_LOAD_DEFAULT);
    if (err != 0) {
        slot.loaded = true;
        slot.empty = true;
        return slot;
    }

    slot.advance = static_cast<int>((face->glyph->advance.x + 32) >> 6);
    slot.bearingX = face->glyph->bitmap_left;
    slot.bearingY = face->glyph->bitmap_top;
    slot.width = static_cast<int>((face->glyph->metrics.width + 63) >> 6);
    slot.height = static_cast<int>((face->glyph->metrics.height + 63) >> 6);
    slot.empty = (index == 0 && codepoint != 0);
    slot.loaded = true;
    return slot;
}

float FontData::getWidth(std::string text) const {
    float width = 0.f;
    int prev = -1;
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = nextCodepoint(text, i);
        if (cp == 0)
            continue;
        int code = static_cast<int>(cp);
        if (prev >= 0)
            width += getKerning(prev, code);
        width += static_cast<float>(ensureGlyph(code).advance);
        prev = code;
    }
    return width;
}

int FontData::getGlyphWidth(int codepoint) const { return ensureGlyph(codepoint).width; }
int FontData::getGlyphHeight(int codepoint) const { return ensureGlyph(codepoint).height; }
int FontData::getGlyphBearingX(int codepoint) const { return ensureGlyph(codepoint).bearingX; }
int FontData::getGlyphBearingY(int codepoint) const { return ensureGlyph(codepoint).bearingY; }
int FontData::getGlyphAdvance(int codepoint) const { return ensureGlyph(codepoint).advance; }

image::ImageData *FontData::newGlyphImageData(int codepoint) {
    if (!face)
        return new image::ImageData(0, 0, "RGBA8");

    FT_UInt index = glyphIndex(codepoint);
    FT_Error err = FT_Load_Glyph(face, index, FT_LOAD_RENDER);
    if (err != 0 || !face->glyph->bitmap.buffer || face->glyph->bitmap.width == 0 ||
        face->glyph->bitmap.rows == 0) {
        // Still refresh metric cache.
        ensureGlyph(codepoint);
        return new image::ImageData(0, 0, "RGBA8");
    }

    const FT_Bitmap &bm = face->glyph->bitmap;
    const int w = static_cast<int>(bm.width);
    const int h = static_cast<int>(bm.rows);

    // Update cache with rendered metrics.
    if (index >= glyphCache.size())
        glyphCache.resize(index + 1);
    GlyphCache &slot = glyphCache[index];
    slot.width = w;
    slot.height = h;
    slot.bearingX = face->glyph->bitmap_left;
    slot.bearingY = face->glyph->bitmap_top;
    slot.advance = static_cast<int>((face->glyph->advance.x + 32) >> 6);
    slot.empty = false;
    slot.loaded = true;

    auto *img = new image::ImageData(w, h, "RGBA8");
    auto *dst = static_cast<unsigned char *>(img->getData());

    if (bm.pixel_mode == FT_PIXEL_MODE_GRAY) {
        for (int y = 0; y < h; ++y) {
            const unsigned char *src = bm.buffer + y * bm.pitch;
            unsigned char *row = dst + y * w * 4;
            for (int x = 0; x < w; ++x) {
                unsigned char a = src[x];
                row[x * 4 + 0] = 255;
                row[x * 4 + 1] = 255;
                row[x * 4 + 2] = 255;
                row[x * 4 + 3] = a;
            }
        }
    } else if (bm.pixel_mode == FT_PIXEL_MODE_MONO) {
        for (int y = 0; y < h; ++y) {
            const unsigned char *src = bm.buffer + y * bm.pitch;
            unsigned char *row = dst + y * w * 4;
            for (int x = 0; x < w; ++x) {
                unsigned char byte = src[x >> 3];
                unsigned char a = (byte & (0x80 >> (x & 7))) ? 255 : 0;
                row[x * 4 + 0] = 255;
                row[x * 4 + 1] = 255;
                row[x * 4 + 2] = 255;
                row[x * 4 + 3] = a;
            }
        }
    } else {
        // Unsupported pixel mode — leave transparent.
        std::memset(dst, 0, static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    }

    return img;
}

}  // namespace font
}  // namespace eve
