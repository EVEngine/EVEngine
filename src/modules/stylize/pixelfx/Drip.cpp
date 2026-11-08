// eve-hot-path
// Native adaptation of pixelfx water/drip.js (MIT), including Sprite Fusion blood bead/tail recipes.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Water.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 1.6, SY = .28, G = 760, LET = .78, PRX = 20, REBOUND = .06, BACK = .2;
double           landAt(double H) { return LET + std::sqrt((2 * H) / G); }
void             dripstone(Paint& p, double H) {
    const auto&  ST  = water::rock();
    const double top = -H - 14;
    p.shader(-24, top, 22, -H + .5, [&](double x, double y, ShadeOutput& out) {
        const double under = top + 7 + std::sin(x * .45) * 1.2 + std::sin(x * 1.3 + 1) * .6 - std::abs(x + 4) * .12,
                     cone = -H - std::abs(x) * 1.25, bottom = std::max(under, std::min(cone, -H)),
                     span = x < 0 ? 24 : 22;
        if (y > bottom || std::abs(x) > span - std::max(0., top + 3 - y) * 4) return;
        const bool   rim = y > bottom - 1 || std::abs(x) > span - 1;
        const double lx = (x + 24) / 46, v = (y - top) / 14;
        out.c = rim                            ? ST[0]
                : v < .25                      ? ST[1]
                : y > under && std::abs(x) < 6 ? (x < 0 ? ST[4] : ST[2])
                : lx < .3                      ? ST[4]
                : lx < .62                     ? ST[3]
                                               : ST[2];
        if (!rim && std::abs(x - 9 - (y - top) * .4) < .5 && v > .3 && v < .7) out.c = ST[0];
    });
    for (double y = -H - 7; y < -H; ++y) p.px(-1, y, y > -H - 3 ? hex(0x9fb6d6) : hex(0x5d6a86));
    p.px(-1, -H - 2, WHITE);
}
void puddle(Paint& p, std::span<const uint32_t> C) {
    const double ry = PRX * SY;
    for (int Y = int(std::floor(-ry)); Y < ry; ++Y)
        for (int X = -int(PRX); X < PRX; ++X) {
            const double x = X + .5, y = Y + .5, d = std::pow(x / PRX, 2) + std::pow(y / ry, 2);
            if (d > 1) continue;
            p.blend(x, y, C[1], .72 * std::min(1., (1 - d) * 5));
        }
    for (int X = -int(PRX) + 3; X < PRX - 3; ++X) {
        const double x = X + .5, y = -std::sqrt(1 - std::pow(x / PRX, 2)) * ry;
        p.dot(x, y + .5, std::abs(x) < PRX * .5 ? C[4] : C[3], .95);
    }
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    const auto& C = o.palette;
    puddle(p, C);
    const double u = t - landAt(o.water.height);
    if (u >= 0)
        water::ring(p, 0, 0, 2 + 17 * ease::outCubic(u / .75), SY, 1.1 * (1 - u / .75), C,
                    {2, 2.5, -1, false, 0, 1, .4});
    const double v = t - landAt(o.water.height) - REBOUND - BACK;
    if (v >= 0)
        water::ring(p, 0, 0, 1.5 + 9 * ease::outCubic(v / .5), SY, .9 * (1 - v / .5), C, {1, 3, -1, false, 0, 2});
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C = o.palette;
    const double H = o.water.height, land = landAt(H);
    if (o.water.source) dripstone(p, H);
    if (t < LET) {
        const double k = t / LET, r = .7 + 2.3 * ease::inQuad(k), wob = .15 * std::sin(t * 26) * k,
                     neck = smooth(.82, 1, k) * 2.5, cy = -H + r + neck;
        if (neck > .2) p.line(0, -H, 0, cy - r, C[5]);
        p.ellipse(0, cy, r * (1 - wob), r * (1 + wob) + .3 * k, [&](double d, double a) {
            return d > .72                      ? (std::cos(a) < 0 && std::sin(a) < .3 ? C[5] : C[3])
                   : d < .35 && std::cos(a) > 0 ? C[6]
                                                : C[4];
        });
        if (r > 1) p.px(-r * .4, cy - r * .45, WHITE);
        p.glow(0, cy, 4 + r * 2, C[5], .14 + .14 * k);
    } else {
        const double s = (t - LET) / .15;
        if (s < 1) p.ellipse(0, -H + .8, 1.2 * (1 - s * .6), 1 + std::sin(s * PI) * .8, C[4]);
    }
    const double f = t - LET;
    if (f > 0 && t < land) {
        const double y = -H + 3 + .5 * G * f * f, vy = G * f;
        water::bead(p, 0, y, 0, vy, 3, C);
        p.glow(0, y, 6, C[5], .2);
    }
    const double u = t - land;
    if (u >= 0) {
        water::ring(p, 0, 0, 2 + 17 * ease::outCubic(u / .75), SY, 1.1 * (1 - u / .75), C,
                    {2, 2.5, 1, u < .08, u < .5 ? 1 - u * 2 : 0, 1, .4});
        if (u < .06) {
            p.px(0, 0, WHITE);
            p.px(-1, 0, C[6]);
            p.px(1, 0, C[6]);
            p.glow(0, -1, 10, C[5], .5 * (1 - u / .06));
        }
        for (const Point v : std::array<Point, 3>{{{-26, 70}, {24, 64}, {-6, 90}}}) {
            const double h = v.y * u - .5 * G * u * u;
            if (h > 0 && u < .3) p.px(v.x * u, -h, u < .1 ? WHITE : C[6]);
        }
        const double r = u - REBOUND, h = 52 * r - .5 * G * .5 * r * r * 2;
        if (r > 0 && h > 0) {
            p.line(0, -1, 0, -h + 1, C[5]);
            p.circle(0, -h, 1.1, C[6]);
            p.px(0, -h - 1, WHITE);
        }
        const double v = u - REBOUND - BACK;
        if (v >= 0)
            water::ring(p, 0, 0, 1.5 + 9 * ease::outCubic(v / .5), SY, .9 * (1 - v / .5), C, {1, 3, 1, v < .06, 0, 2});
        p.glow(0, 0, 18, C[4], .22 * (1 - clamp(u / .6)));
    }
    const double sh = std::sin(t * 20) > 0 ? 0 : 1;
    p.px(-1 + sh, 2, C[4]);
    if (f > 0 && t < land) p.px(sh, std::min(4., 2 + .5 * G * f * f * .04), C[5]);
    p.glow(0, -H + 2, 8, C[5], .16);
    p.glow(0, 0, 26, C[3], .14);
}
}  // namespace
const Descriptor& dripDescriptor() {
    static const std::array<Option, 2> options{{{"height", 1, 256, false, 0, &PixelFxWaterSettings::height},
                                                {"source", 0, 1, false, 0, &PixelFxWaterSettings::source}}};
    static const Descriptor            d{{PixelFxKind::Drip, "water/drip", 72, 88, 36, 78, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  water::defaults(s, 1);
                                  s.water = {};
                                         },
                                         front,
                                         options,
                                         nullptr,
                                         back};
    return d;
}
}  // namespace eve::stylize::pixelfx
