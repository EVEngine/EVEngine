// eve-hot-path
// Native adaptation of pixelfx transitions/_lib.js clipTo/rect/glint, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#pragma once
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::screen {
/** @brief Authored top-left-origin screen dimensions in logical pixels. */
inline constexpr int W = 320, H = 180;
/** @brief Copy a synchronous painter borrow with source pixel-center screen clipping.
 * @note No target storage or caller references escape the draw. */
[[nodiscard]] inline Paint clip(const Paint& p) {
    auto q     = p;
    q.clipRect = Paint::ClipRect{0, 0, W, H, Paint::ClipRect::Rule::PixelCenters};
    return q;
}
/** @brief Raster an authored opaque rectangle using source whole-row coordinate mapping.
 * @cost Linear in authored logical rectangle area per draw; no allocation. */
inline void rect(Paint& p, double x, double y, double w, double h, uint32_t color) {
    if (p.sn != 0 || p.cs != 1) {
        p.rect(x, y, w, h, color);
        return;
    }
    const int x0 = int(jsRound(x)), x1 = int(jsRound(x + w)), y0 = int(jsRound(y)), y1 = int(jsRound(y + h)),
              b = int(std::floor(p.tx + (p.fx > 0 ? .5 : -.5)));
    for (int yy = y0; yy < y1; ++yy) {
        const int sy = int(std::floor(p.ty + yy + .5));
        for (int xx = x0; xx < x1; ++xx) p.put(b + int(p.fx) * xx, sy, color, 1);
    }
}
/** @brief Draw the source spinning eight-ray glint, dithered arms and additive heart.
 * @note Synchronous owner-thread draw; no retained input or RNG.
 * @cost Linear in authored ray length plus quadratic clipped glow area per draw; no allocation. */
inline void glint(Paint& p, double x, double y, double length, double angle, uint32_t c1, uint32_t c2,
                  double alpha = 1) {
    if (length < .8 || alpha <= 0) return;
    for (int i = 0; i < 8; ++i) {
        const bool   diag = i % 2 == 1;
        const double a = angle + (i * TAU) / 8, len = diag ? length * .42 : length, ca = std::cos(a), sa = std::sin(a);
        for (double d = 0; d <= len; d += .5) {
            const double u = d / len;
            const auto   c = diag ? (u < .5 ? c1 : c2) : u < .3 ? WHITE : u < .65 ? c1 : c2;
            p.dot(x + ca * d, y + sa * d, c, alpha * (u < .7 ? 1 : 1 - (u - .7) / .35));
            if (!diag && u < .22 && length > 4) {
                p.dot(x + ca * d - sa, y + sa * d + ca, c1, alpha);
                p.dot(x + ca * d + sa, y + sa * d - ca, c1, alpha);
            }
        }
    }
    p.dot(x, y, WHITE, alpha);
    p.glow(x, y, 4 + length * 1.4, c1, .5 * alpha);
    p.glow(x, y, 2 + length * .5, WHITE, .4 * alpha);
}
}  // namespace eve::stylize::pixelfx::screen
