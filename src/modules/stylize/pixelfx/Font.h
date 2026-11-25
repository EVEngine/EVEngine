// Native adaptation of pixelfx core/font.js, MIT. See LICENSE.txt.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace eve::stylize::pixelfx {
enum class PixelFont { Mini, Big };
struct Glyph {
    int                    width = 0;
    std::array<uint8_t, 7> rows{};
};
#include "stylize/pixelfx/FontData.inc"
inline Glyph fontGlyph(uint32_t code, PixelFont font) {
    // JS single-character uppercase lookup can reach ASCII glyphs from these two Unicode letters.
    if (code == 0x131u)
        code = uint32_t('I');
    else if (code == 0x17fu)
        code = uint32_t('S');
    const auto& glyphs = font == PixelFont::Mini ? miniGlyphs : bigGlyphs;
    if (code >= glyphs.size()) return {};
    auto g = glyphs[code];
    if (g.width == 0 && code >= uint32_t('a') && code <= uint32_t('z')) g = glyphs[code - 32u];
    return g;
}
inline int fontHeight(PixelFont font) { return font == PixelFont::Mini ? 5 : 7; }
inline int fontSpace(PixelFont font) { return font == PixelFont::Mini ? 2 : 3; }
template <class F>
void eachCodepoint(std::string_view str, F fn) {
    size_t i = 0;
    while (i < str.size()) {
        const auto c         = uint8_t(str[i++]);
        uint32_t   code      = c;
        int        following = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            code      = c & 31u;
            following = 1;
        } else if (c >= 0xe0 && c <= 0xef) {
            code      = c & 15u;
            following = 2;
        } else if (c >= 0xf0 && c <= 0xf4) {
            code      = c & 7u;
            following = 3;
        }
        for (int n = 0; n < following; ++n) {
            if (i >= str.size() || (uint8_t(str[i]) & 0xc0u) != 0x80u) {
                code = 0xfffdu;
                break;
            }
            code = (code << 6) | (uint8_t(str[i++]) & 63u);
        }
        fn(code);
    }
}
inline int textWidth(std::string_view str, PixelFont font = PixelFont::Mini) {
    int w = 0;
    eachCodepoint(str, [&](uint32_t c) {
        const auto g = fontGlyph(c, font);
        w += (g.width ? g.width : fontSpace(font)) + 1;
    });
    return w > 0 ? w - 1 : 0;
}
template <class F>
void rasterText(std::string_view str, PixelFont font, F plot) {
    int x = 0;
    eachCodepoint(str, [&](uint32_t c) {
        const auto g = fontGlyph(c, font);
        if (g.width == 0) {
            x += fontSpace(font) + 1;
            return;
        }
        for (int r = 0; r < fontHeight(font); ++r)
            for (int col = 0; col < g.width; ++col)
                if (g.rows[size_t(r)] & (1 << col)) plot(x + col, r);
        x += g.width + 1;
    });
}
}  // namespace eve::stylize::pixelfx
