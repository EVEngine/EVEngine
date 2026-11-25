// Native adaptation of pixelfx water/_lib.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Painter.h"

namespace eve::stylize::pixelfx::water {
/** @brief Borrow the immutable source wet-stone ramp for process lifetime. */
inline const std::array<uint32_t, 6>& rock() {
    static const std::array<uint32_t, 6> ramp{
        {hex(0x141219), hex(0x221f2b), hex(0x34303f), hex(0x4a4558), hex(0x655f74), hex(0x8a8498)}};
    return ramp;
}
/** @brief Set the owning source water ramp during cold configuration preparation.
 * @cost Allocates eight colors per preparation; retain the prepared configuration across frames. */
inline void defaults(PixelFxSettings& s, uint32_t seed) {
    s.seed    = seed;
    s.palette = {hex(0x0a1630), hex(0x123063), hex(0x1b5596), hex(0x2a7fc4),
                 hex(0x45a9e3), hex(0x86d3f5), hex(0xcdf1ff), WHITE};
}
struct RingOptions {
    int      n     = 2;
    double   gap   = 3;
    int      half  = 0;
    bool     hot   = false;
    double   glint = 0;
    uint32_t seed  = 0;
    double   wob   = 0;
};
/** @brief Draw source crest trains; all input spans are borrowed synchronously and never retained.
 * @cost Work grows with crest circumference per draw; allocate the target once across frames. */
inline void ring(Paint& p, double cx, double cy, double r, double sy, double k, std::span<const uint32_t> C,
                 RingOptions o = {}) {
    if (k <= .02 || r < .5) return;
    for (int j = 0; j < o.n; ++j) {
        const double rr = r - j * o.gap;
        if (rr < .6) break;
        const double kj = k * (1 - j * .42), need = 1.15 - 2.3 * std::min(1., kj * 1.25);
        const int    m = std::max(6, int(std::ceil(rr * TAU * 1.3)));
        for (int i = 0; i < m; ++i) {
            const double a = (double(i) / m) * TAU, sn = std::sin(a);
            const bool   near = sn > 0;
            if ((o.half > 0 && !near) || (o.half < 0 && near)) continue;
            if (noise(a * 4.7 + o.seed * 13.1 + j * 5.3, o.seed) * .65 +
                    noise(a * 11.3 + o.seed * 7.7, o.seed + 3) * .35 <
                need)
                continue;
            const double lit = kj * (near ? 1 : .7) * (.88 - .12 * std::cos(a));
            const auto   c   = o.hot && near && j == 0 ? WHITE : C[lit > .7 ? 6 : lit > .38 ? 5 : 4];
            const double ra  = o.wob ? rr + o.wob * noise(a * 2.6 + o.seed * 1.9 + j, o.seed + 11) : rr;
            const double x = cx + std::cos(a) * ra, y = cy + sn * ra * sy;
            p.px(x, y, c);
            if (j == 0) p.light(x, y, C[4], .3 * lit);
        }
    }
    if (o.glint > 0 && o.half >= 0) {
        const double x = cx + std::cos(2.05) * r, y = cy + std::sin(2.05) * r * sy;
        p.px(x, y, WHITE);
        p.glow(x, y, 3 + o.glint * 2, C[5], .35 * o.glint);
    }
}
/** @brief Draw one source water bead and speed-dependent tail, borrowing the ramp only during this call. */
inline void bead(Paint& p, double x, double y, double vx, double vy, double s, std::span<const uint32_t> C,
                 double a = 1) {
    const double speed = jsHypot2(vx, vy), sp = speed == 0 ? 1 : speed, ux = vx / sp, uy = vy / sp,
                 tail = std::min(5., sp * .011);
    if (tail >= 1)
        p.lineFn(
            x - ux * (tail + 1), y - uy * (tail + 1), x - ux * 1.2, y - uy * 1.2,
            [&](double u) { return u < .45 ? C[3]
                                   : s > 1 ? C[5]
                                           : C[4]; }, a);
    if (s >= 3) {
        p.circle(x, y, 1.75, C[4], a);
        p.circle(x - .25, y - .25, 1.15, C[6], a);
        p.px(x - .7, y - .7, WHITE, a);
    } else if (s >= 2) {
        const double X = std::floor(x), Y = std::floor(y);
        p.px(X, Y, WHITE, a);
        p.px(X + 1, Y, C[6], a);
        p.px(X, Y + 1, C[5], a);
        p.px(X + 1, Y + 1, C[4], a);
    } else
        p.px(x, y, sp > 160 ? C[6] : C[5], a);
}
/** @brief Draw one source foam billow using synchronous target/ramp borrows; no references are retained. */
inline void billow(Paint& p, double x, double y, double r, std::span<const uint32_t> C, double a = 1) {
    if (r < .9) {
        p.dot(x, y, C[6], a);
        return;
    }
    p.circle(x + r * .12, y + r * .2, r, C[4], a);
    p.circle(x, y, r * .84, C[6], a);
    if (r > 1.6) p.circle(x - r * .3, y - r * .32, r * .46, WHITE, a);
}
}  // namespace eve::stylize::pixelfx::water
