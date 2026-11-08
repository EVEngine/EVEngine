// Native adaptation of pixelfx critters/_lib.js bubble, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::critter {
/** @brief Draw source tiny/rim bubbles through synchronous painter and four-color material borrows. */
inline void bubbleLook(Paint& p, double cx, double cy, double r, const std::array<uint32_t, 4>& colors,
                       double alpha = 1, double wobble = 0) {
    const auto [hi, lit, rim, shade] = colors;
    if (r < .9) {
        p.dot(cx, cy, r < .7 ? rim : lit, alpha);
        return;
    }
    if (r < 1.5) {
        const double x = std::floor(cx), y = std::floor(cy);
        p.dot(x, y, hi, alpha);
        p.dot(x + 1, y, lit, alpha);
        p.dot(x, y + 1, rim, alpha);
        p.dot(x + 1, y + 1, shade, alpha);
        p.glow(cx + .5, cy + .5, 3, lit, .12 * alpha);
        return;
    }
    const double rx = r * (1 + wobble), ry = r * (1 - wobble);
    for (int y = int(std::floor(cy - ry - 1)); y <= int(std::ceil(cy + ry + 1)); ++y)
        for (int x = int(std::floor(cx - rx - 1)); x <= int(std::ceil(cx + rx + 1)); ++x) {
            const double dx = (x + .5 - cx) / rx, dy = (y + .5 - cy) / ry, d = std::sqrt(dx * dx + dy * dy) * r;
            if (d > r + .5) continue;
            const double light = (-dx - dy) / std::sqrt(2.) / std::max(.01, d / r);
            if (d > r - .6) p.dot(x, y, light > .45 ? lit : light < -.55 ? shade : rim, alpha);
        }
    const double hx = jsRound(cx - rx * .45), hy = jsRound(cy - ry * .45);
    p.dot(hx, hy, hi, alpha);
    if (r >= 2.5) {
        p.dot(hx + 1, hy, lit, alpha);
        p.dot(hx, hy + 1, lit, alpha);
    }
    if (r >= 2) p.dot(jsRound(cx + rx * .5), jsRound(cy + ry * .5), lit, alpha);
    p.glow(cx, cy, r * 2.4 + 2, rim, .14 * alpha);
}
}  // namespace eve::stylize::pixelfx::critter
