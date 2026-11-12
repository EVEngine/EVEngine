// eve-hot-path
// Adapted from patch-gg/pixelfx holy/{halo,sunburst}.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"

namespace eve::stylize::pixelfx {
void halo(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C = o.palette;
    const double T = 2, ph = t / T * TAU, RX = 9.5, RY = 2.6;
    const double y0 = -46 + (std::sin(ph) > 0.55 ? -1 : 0), gl = PI / 2 + ph;
    const auto&  A = o.actor;
    for (int i = 0; i < A.width; ++i)
        for (int j = 0; j < std::min(A.height, 9); ++j) {
            const auto c = A.pixels[size_t(j) * size_t(A.width) + size_t(i)];
            if (!c || luma(c) < .15) continue;
            const double x = A.x + i, y = A.y + j;
            p.px(x, y, mix(c, C[6], .45));
            const auto c2 = j + 1 < A.height ? A.pixels[size_t(j + 1) * size_t(A.width) + size_t(i)] : 0;
            if (c2 && luma(c2) >= .15) p.px(x, y + 1, mix(c2, C[5], .18));
            for (int d = 0; d <= 4; ++d) p.light(x, y + d, C[4], .3 * (1 - double(d) / 5));
            break;
        }
    for (int j = 0; j < 92; ++j) {
        const double g = double(j) / 92 * TAU, x = std::cos(g) * RX, y = y0 + std::sin(g) * RY;
        const bool   near = std::sin(g) >= 0, shimmer = std::sin(g * 3 - ph * 2) > 0.7;
        const bool   hot = std::abs(wrap(g - gl + PI, TAU) - PI) < 0.3;
        if (near) {
            p.px(x, y, hot || shimmer ? WHITE : C[6]);
            p.px(x, y + 1, hot ? C[6] : C[4]);
        } else
            p.px(x, y, hot ? WHITE : shimmer ? C[6] : C[5], 0.9);
    }
    p.glow(0, y0, 20, C[4], 0.4);
    p.glow(0, y0 + 1, 9, WHITE, 0.16);
    const double gx = std::cos(gl) * RX, gy = y0 + std::sin(gl) * RY;
    if (std::sin(gl) > -0.2)
        p.sparkle(gx, gy + 0.5, 1.5 + 2.5 * std::max(0.0, std::sin(gl)), WHITE, C[6], 0.45);
    else
        p.glow(gx, gy, 5, C[6], 0.3);
    p.glowLine(0, y0 + 2, 0, y0 + 9, 9, C[4], 0.16);
    cycle(t, T, 6, o.seed + 3, 0.3, 0.5, [&](double age, double life, Rng& R, int) {
        const double g = R.range(0, TAU), d = R.range(1.1, 1.5), k = std::sin(age / life * PI);
        p.sparkle(std::cos(g) * RX * d, y0 + std::sin(g) * RY * d - R.range(1, 4), 1 + 2 * k, WHITE, C[6], 0.4 * k);
    });
    cycle(t, T, 16, o.seed, 0.8, 1.5, [&](double age, double life, Rng& R, int) {
        const double g = R.range(0, TAU), u = age / life;
        const double x  = std::cos(g) * RX * R.range(0.8, 1) + std::sin(u * 5 + g) * 1.2;
        const double y  = y0 + std::sin(g) * RY + 1 + u * R.range(7, 14);
        const double tw = 0.6 + 0.4 * std::sin(age * 18 + g * 7), k = std::sin(u * PI) * tw;
        p.px(x, y, tw > 0.85 ? WHITE : C[6], std::min(1.0, k * 1.5));
        p.light(x, y, C[5], 0.5 * k);
    });
}
void sunburst(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C = o.palette;
    const int    N = o.rays * 2;
    const double T = 4, rot = t / T * (2 * TAU / N), R0 = 9;
    p.glow(0, 0, 62, C[4], 0.3 + 0.06 * std::sin(t / T * TAU * 2));
    for (int i = 0; i < N; ++i) {
        const double a = rot + (i + 0.5) * TAU / N, L = 14 + 6 * std::sin(a * 3 + t / T * TAU);
        p.lineFn(
            std::cos(a) * (R0 + 1), std::sin(a) * (R0 + 1), std::cos(a) * (R0 + L), std::sin(a) * (R0 + L),
            [&](double u) { return u < 0.3 ? C[6] : C[5]; }, [](double u) { return 0.7 - u * 0.8; });
    }
    for (int i = 0; i < N; ++i) {
        const double a       = rot + i * TAU / N;
        const bool   lng     = i % 2 == 0;
        const double breathe = 0.86 + 0.14 * std::sin(a * 3 - t / T * TAU * 2), L = (lng ? 36 : 21) * breathe,
                     w0 = lng ? 4 : 2.6;
        const double ca = std::cos(a), sa = std::sin(a), x1 = ca * (R0 + L), y1 = sa * (R0 + L), pad = w0 + 1;
        p.shade(std::min(0.0, x1) - pad, std::min(0.0, y1) - pad, std::max(0.0, x1) + pad, std::max(0.0, y1) + pad,
                [&](double x, double y, int sx, int sy) {
                    const double u = (x * ca + y * sa - R0) / L;
                    if (u < 0 || u > 1) return;
                    const double across = std::abs(-x * sa + y * ca), w = w0 / 2 * (1 - u * 0.85) + 0.25;
                    if (across > w) return;
                    const bool spine                               = across<0.5, rim = across> w - 0.6 && w > 0.9;
                    const uint32_t                               c = spine             ? (u < 0.4    ? WHITE
                                                                                          : u < 0.75 ? C[6]
                                                                                                     : C[5])
                                                                     : rim && u > 0.15 ? C[3]
                                                                     : u < 0.3         ? C[6]
                                                                     : u < 0.62        ? C[5]
                                                                                       : C[4];
                    p.put(sx, sy, c, u < 0.72 ? 1 : (1 - (u - 0.72) / 0.28) * 1.2);
                });
        p.glowLine(ca * R0, sa * R0, ca * (R0 + L * 0.8), sa * (R0 + L * 0.8), 5, C[5], 0.16);
        if (lng) {
            const double g = wrap(t / T * 2 + a * 3 / TAU, 1), r = R0 + g * L;
            if (g < 0.9) {
                p.px(ca * r, sa * r, WHITE);
                p.glow(ca * r, sa * r, 5, C[6], 0.45 * (1 - g));
            } else
                p.sparkle(ca * (R0 + L), sa * (R0 + L), 1 + 3 * std::sin((g - 0.9) / 0.1 * PI), WHITE, C[6], 0.5);
        }
    }
    p.shade(-9.5, -9.5, 9.5, 9.5, [&](double x, double y, int sx, int sy) {
        const double d = jsHypot2(x, y);
        if (d > 7.5) return;
        const double flick = 0.5 + 0.5 * std::sin(std::atan2(y, x) * 5 + t / T * TAU * 3);
        p.put(sx, sy, d < 4.5 ? WHITE : d < 6 ? (flick > 0.6 ? WHITE : C[6]) : flick > 0.5 ? C[5] : C[4]);
    });
    p.glow(0, 0, 16, WHITE, 0.28);
    cycle(t, T, 10, o.seed, 0.35, 0.7, [&](double age, double life, Rng& R, int) {
        const double a = R.range(0, TAU), d = R.range(16, 44), k = std::sin(age / life * PI);
        p.sparkle(std::cos(a) * d, std::sin(a) * d, 1 + 2.4 * k * R.range(0.5, 1), WHITE, C[5], 0.4 * k);
    });
}
}  // namespace eve::stylize::pixelfx

namespace eve::stylize::pixelfx {

const Descriptor& haloDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            descriptor{{PixelFxKind::Halo, "holy/halo", 56, 80, 28, 70, 2, true},
                                                  8,
                                                  [](PixelFxSettings& s) {
                                           s.actor.source = PixelFxActorSource::Dummy;
                                           s.seed         = 5;
                                           s.palette = {hex(0x3b1d2a), hex(0x7a3a2c), hex(0xc4652e), hex(0xf0a040),
                                                        hex(0xffd25e), hex(0xfff0a0), hex(0xfffbe0), hex(0xffffff)};
                                                  },
                                                  &halo,
                                                  options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx

namespace eve::stylize::pixelfx {

const Descriptor& sunburstDescriptor() {
    static const std::array<Option, 1> options{{{"rays", 1, 32, false, 0, &PixelFxSettings::rays}}};
    static const Descriptor            descriptor{{PixelFxKind::Sunburst, "holy/sunburst", 128, 128, 64, 64, 4, true},
                                                  8,
                                                  [](PixelFxSettings& s) {
                                           s.seed    = 3;
                                           s.palette = {hex(0x3b1d2a), hex(0x7a3a2c), hex(0xc4652e), hex(0xf0a040),
                                                        hex(0xffd25e), hex(0xfff0a0), hex(0xfffbe0), hex(0xffffff)};
                                                  },
                                                  &sunburst,
                                                  options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
