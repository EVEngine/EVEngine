#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace eve::font {
class FontData;
}

namespace eve::graphics {

class Graphics;
class Texture;

/**
 * @brief Decodes one UTF-8 codepoint from `text` starting at byte offset `i`,
 * advancing `i` past it. Returns 0 at end of string; invalid/truncated
 * sequences decode as U+FFFD and advance by one byte.
 */
uint32_t nextCodepointUtf8(const std::string &text, size_t &i);

/**
 * @brief GPU-side font: wraps a decoded `font::FontData` and rasterizes a fixed
 * set of codepoints into a single RGBA8 glyph atlas texture up front, so
 * `Graphics::print()` can draw text with plain textured-quad draws.
 *
 * Codepoints outside the pre-rasterized charset still advance the pen using
 * `FontData` metrics (via FreeType) but are not drawn (no atlas entry).
 * Does not take ownership of the `FontData*` passed to the constructor.
 */
class Font {
public:
    /** @brief Printable ASCII (0x20..0x7E), used when no explicit charset is given. */
    static std::string defaultCharset();

    Font(Graphics *gfx, font::FontData *data, std::string charset = defaultCharset());
    ~Font();

    Font(const Font &)            = delete;
    Font &operator=(const Font &) = delete;

    font::FontData *getData() const { return data; }
    Texture        *getTexture() const { return atlas; }

    /** @brief Line height in pixels at the FontData's decoded pixel size. */
    float getHeight() const;
    float getAscent() const;
    /** @brief Distance from the top of a line to the baseline (== getAscent()). */
    float getBaseline() const;

    /** @brief Pixel width of `text` (UTF-8), including kerning; delegates to FontData. */
    float getWidth(const std::string &text) const;

    /** @brief Whether `codepoint` was rasterized into this Font's atlas. */
    bool hasGlyph(int codepoint) const;

    struct Glyph {
        float u0 = 0.f, v0 = 0.f, u1 = 0.f, v1 = 0.f;  // atlas UV rect (empty if width/height == 0)
        int   width = 0, height = 0;                    // glyph bitmap size, px
        int   bearingX = 0, bearingY = 0;
        int   advance = 0;
    };

    /** @brief Returns nullptr if `codepoint` isn't in this Font's atlas. */
    const Glyph *findGlyph(int codepoint) const;

private:
    void buildAtlas(const std::string &charset);

    font::FontData                   *data  = nullptr;
    Texture                          *atlas = nullptr;
    std::unordered_map<int, Glyph> glyphs;
};

}  // namespace eve::graphics
