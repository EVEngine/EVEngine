// eve-hot-path
// Adapted from patch-gg/pixelfx projectiles/laser-bolt.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"

namespace eve::stylize::pixelfx {
void laserBolt(Paint& p, double t, const PixelFxSettings& o) {
    const auto&    C = o.palette;
    const double   L = o.length, D = 0.5, ph = t / D;
    const uint32_t fr = uint32_t(std::floor(t * 30 + 1e-6));
    cycle(
        t, D, 14, o.seed, 0.18, 0.3,
        [&](double age, double life, Rng& r, int) {
            const double k = age / life, x0 = -L * 0.8 - age * r.range(150, 230), len = r.range(3, 7) * (1 - k * 0.5);
            const double y = 0.5 + std::sin(x0 * 0.25 + ph * TAU * 2) * (0.5 + k * 1.5) + r.range(-0.6, 0.6) * k;
            if (x0 - len < -106 || rand({fr, uint32_t(r.integer(0, 999))}) < k * 0.5) return;
            p.line(x0, y, x0 - len, y, k < 0.3 ? C[5] : k < 0.6 ? C[4] : C[3]);
            if (k < 0.4) p.glow(x0 - len / 2, y, 4, C[4], 0.3 * (1 - k));
        },
        true);
    p.shade(-L - 1, -5, 4, 6, [&](double x, double y, int sx, int sy) {
        const double d = -x + 3, u = d / (L + 3);
        if (u < 0 || u > 1) return;
        const double head = d < 3 ? std::sqrt(std::max(0.0, 1 - std::pow((3 - d) / 3, 2))) : 1;
        const double w    = (3.4 * std::pow(1 - u, 0.9) + 0.5) * head * (1 + 0.08 * std::sin(d * 0.9 - ph * TAU * 4));
        const double v    = std::abs(y - 0.5) / w;
        if (v > 1.2) return;
        if (v > 1 && (u > 0.5 || rand({uint32_t(int32_t(std::floor(x))), fr}) > 0.6)) return;
        const int pulse = wrap(d * 0.05 - ph * 3, 1) < 0.12 ? 1 : 0;
        const int lvl = v < 0.22 ? (u < 0.6 ? 7 : 6) : v < 0.45 ? 6 - (u > 0.5 ? 1 : 0) : v < 0.72 ? 4 : v <= 1 ? 3 : 2;
        p.put(sx, sy, C[size_t(std::min(7, lvl + (v < 0.72 ? pulse : 0)))]);
    });
    for (double s : {0.0, PI})
        for (double d = 2; d < L * 0.7; d += 1) {
            const double a = d * 0.45 - ph * TAU * 3 + s, y = 0.5 + std::sin(a) * (2.2 * (1 - d / L) + 0.6);
            if (std::cos(a) > 0.3) p.px(-d, y, std::cos(a) > 0.8 ? WHITE : C[6]);
        }
    const double g = 0.75 + 0.25 * std::sin(ph * TAU * 2);
    p.sparkle(1, 0.5, 3 + std::floor(g * 2 + 0.5), C[6], C[4], 0.5);
    p.glowLine(1, 0.5, -L, 0.5, 9, C[3], 0.4);
    p.glow(-2, 0.5, 20, C[3], 0.5 * g);
    p.glow(1, 0.5, 6, WHITE, 0.25);
}
}  // namespace eve::stylize::pixelfx

namespace eve::stylize::pixelfx {

const Descriptor& laserBoltDescriptor() {
    static const std::array<Option, 1> options{{{"length", 1, 100, false, 0, &PixelFxSettings::length}}};
    static const Descriptor descriptor{{PixelFxKind::LaserBolt, "projectiles/laser-bolt", 128, 32, 108, 16, 0.5, true},
                                       8,
                                       [](PixelFxSettings& s) {
                                           s.seed    = 2;
                                           s.palette = {hex(0x2a0610), hex(0x5c0d1c), hex(0x9a1428), hex(0xd8243a),
                                                        hex(0xff4d5e), hex(0xff8a94), hex(0xffd0d4), hex(0xffffff)};
                                       },
                                       &laserBolt,
                                       options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
