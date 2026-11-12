// eve-hot-path
// Native adaptation of pixelfx ui/_lib.js spell/drawWord/shake, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#pragma once
#include "common/Assert.h"
#include "stylize/pixelfx/Font.h"
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::ui {
/** @brief Owned source letter and its left edge in font pixels; zero-width glyph means source space. */
struct Letter {
    Glyph glyph;
    int   x, width, height;
};
/** @brief Prepared owned glyph layout; no retained caller string or font pointer. */
struct Word {
    std::vector<Letter> letters;
    int                 width = 0, height = 7;
};
/** @brief Lay out source glyphs once, preserving unknown-glyph spacing and case lookup.
 * @cost Linear in bounded codepoint count per preparation; amortized over all draws. */
[[nodiscard]] inline Word spell(std::string_view text, PixelFont font = PixelFont::Big) {
    Word out;
    out.height = fontHeight(font);
    int x      = 0;
    eachCodepoint(text, [&](uint32_t code) {
        const auto g = fontGlyph(code, font);
        const int  w = g.width ? g.width : fontSpace(font);
        out.letters.push_back({g, x, w, out.height});
        x += w + 1;
    });
    out.width = std::max(0, x - 1);
    return out;
}
/** @brief Owned per-draw source letter transform and finish; copied into bounded stack storage. */
struct WordItem {
    Glyph  glyph;
    int    height;
    double x, y, sx, sy, alpha = 1, flash = 0, slant = 0;
};
/** @brief Bounded stack letter list; callers add only prepared nonempty glyphs. */
struct WordItems {
    std::array<WordItem, 256> data{};
    size_t                    count = 0;
    /** @brief Append a compiled recipe's item synchronously; no allocation or retained borrow. */
    void add(WordItem item) {
        EV_ASSERT(count < data.size());
        data[count++] = item;
    }
    /** @brief Borrow current items only for the synchronous word draw. */
    [[nodiscard]] std::span<const WordItem> view() const { return {data.data(), count}; }
};
/** @brief Borrowed compiled word finish, valid only for one synchronous draw. */
struct WordStyle {
    std::span<const uint32_t> fill;
    std::optional<uint32_t>   edge, under;
    uint32_t                  outline = 0, shadow = 0, glint = WHITE;
    int                       outlineWidth = 1;
    std::optional<double>     shine, shineY;
};
/** @brief Instance-owned raster scratch; drawing touches scratch only, never simulation or RNG.
 * @note The owning PixelFx instance's render-thread affinity applies; no data or callbacks escape draws. */
struct WordScratch {
    std::vector<int16_t> ids;
    std::vector<uint8_t> edges;
    /** @brief Allocate the compiled recipe's conservative local word bounds once.
     * @cost Linear in maximum word width times maximum height per preparation, amortized over draws. */
    WordScratch(size_t width, size_t height) : ids(width * height), edges(width * height) {}
    /** @brief Raster source letters, bevel, outline, shadow and optional shine synchronously.
     * @cost Linear in local word area times outline-neighborhood area per draw; no allocation. */
    void draw(Paint& p, std::span<const WordItem> items, const WordStyle& style) {
        if (items.empty()) return;
        double ax = INFINITY, ay = INFINITY, bx = -INFINITY, by = -INFINITY;
        for (const auto& it : items) {
            const double h = it.height * it.sy, w = it.glyph.width * it.sx, sl = std::abs(it.slant) * it.height;
            ax = std::min(ax, it.x - sl);
            bx = std::max(bx, it.x + w + sl);
            ay = std::min(ay, it.y);
            by = std::max(by, it.y + h);
        }
        const int    ow = style.outlineWidth, x0 = int(std::floor(ax)) - 1 - ow, y0 = int(std::floor(ay)) - 1 - ow,
                     x1 = int(std::ceil(bx)) + 2 + ow, y1 = int(std::ceil(by)) + 3 + ow, W = x1 - x0, H = y1 - y0;
        const size_t cells = size_t(W) * size_t(H);
        EV_ASSERT(cells <= ids.size());
        EV_ASSERT(!style.fill.empty());
        std::fill_n(ids.begin(), cells, int16_t(0));
        for (size_t k = 0; k < items.size(); ++k) {
            const auto&  it = items[k];
            const int    gh = it.height;
            const double h = gh * it.sy, bot = it.y + h;
            const auto   has = [&](int gx, int gy) {
                return gx >= 0 && gx < it.glyph.width && gy >= 0 && gy < gh &&
                       (it.glyph.rows[size_t(gy)] & (1u << gx)) != 0;
            };
            for (int py = int(std::floor(it.y)); py < int(std::ceil(bot)); ++py) {
                const int gy = int(std::floor((py + .5 - it.y) / it.sy));
                if (gy < 0 || gy >= gh) continue;
                const double shift = jsRound(((bot - py - .5) / it.sy) * it.slant), ry = py + .5 - it.y - gy * it.sy;
                for (int px = int(std::floor(it.x + shift)) - 1;
                     px <= int(std::ceil(it.x + shift + it.glyph.width * it.sx)); ++px) {
                    const double fx = px + .5 - it.x - shift;
                    const int    gx = int(std::floor(fx / it.sx));
                    if (!has(gx, gy)) continue;
                    const int i = (py - y0) * W + (px - x0);
                    if (i < 0 || size_t(i) >= cells) continue;
                    ids[size_t(i)]  = int16_t(k + 1);
                    const double rx = fx - gx * it.sx;
                    const auto open = [&](int row) { return !has(gx, row) && !has(gx - 1, row) && !has(gx + 1, row); };
                    edges[size_t(i)] =
                        uint8_t((ry < 1 && open(gy - 1) ? 1 : 0) | (ry >= it.sy - 1 && open(gy + 1) ? 2 : 0) |
                                (rx < 1 && !has(gx - 1, gy) ? 4 : 0));
                }
            }
        }
        const auto at = [&](int x, int y) -> int {
            return x < x0 || y < y0 || x >= x1 || y >= y1 ? 0 : ids[size_t((y - y0) * W + x - x0)];
        };
        const auto   F   = style.fill;
        const int    nF  = int(F.size());
        const double sy0 = style.shineY.value_or(double(y0));
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const int id = at(x, y);
                if (id) {
                    const auto&  it = items[size_t(id - 1)];
                    const double v  = (y + .5 - it.y) / (it.height * it.sy);
                    const int    i  = std::min(nF - 1, std::max(0, int(std::floor((1 - v) * nF))));
                    auto         c  = F[size_t(i)];
                    const auto   e  = edges[size_t((y - y0) * W + x - x0)];
                    if (e & 1)
                        c = style.edge.value_or(F.back());
                    else if (e & 2)
                        c = style.under.value_or(F.front());
                    else if ((e & 4) && i < nF - 1)
                        c = F[size_t(i + 1)];
                    if (style.shine) {
                        const double d = std::abs(x + .5 - (y + .5 - sy0) * .6 - *style.shine);
                        if (d < 1.1)
                            c = WHITE;
                        else if (d < 2.4)
                            c = style.glint;
                    }
                    if (it.flash > .6 || (it.flash > .25 && v < .5)) c = WHITE;
                    p.dot(x, y, c, it.alpha);
                    continue;
                }
                int n = 0;
                for (int dy = -ow; dy <= ow && !n; ++dy)
                    for (int dx = -ow; dx <= ow && !n; ++dx)
                        if (ow < 2 || std::abs(dx) + std::abs(dy) < 4) n = at(x + dx, y + dy);
                if (n) {
                    p.dot(x, y, style.outline, items[size_t(n - 1)].alpha);
                    continue;
                }
                int shadow = at(x - ow, y - 1 - ow);
                if (!shadow) shadow = at(x + 1 - ow, y - 1 - ow);
                if (!shadow) shadow = at(x - ow, y - ow);
                if (shadow && style.shadow) p.dot(x, y, style.shadow, items[size_t(shadow - 1)].alpha * .9);
            }
    }
};
/** @brief Sample the source whole-pixel shake from injected absolute time and seed. */
[[nodiscard]] inline Point shake(double t, double amp, double t0 = 0, double length = .3, uint32_t seed = 1,
                                 double hz = 32) {
    if (t < t0 || t > t0 + length) return {0, 0};
    const double k = 1 - (t - t0) / length;
    return {jsRound(noise(t * hz, seed) * amp * k * 1.7), jsRound(noise(t * hz, seed + 7) * amp * k * 1.3)};
}
}  // namespace eve::stylize::pixelfx::ui
