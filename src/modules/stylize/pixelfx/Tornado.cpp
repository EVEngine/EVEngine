// eve-hot-path
// Native adaptation of pixelfx nature/tornado.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/NatureGeometry.h"
#include "stylize/pixelfx/NatureLeafGeometry.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2, SY = .28;
struct Geo {
    double FH, ph;
    double ax(double u) const {
        return 8.5 * std::sin(ph - 2.6 * u) * (.2 + .8 * u) + 3 * std::sin(2 * ph + 1.3 - 4.5 * u) * u;
    }
    double rad(double u) const { return 3 + 20 * std::pow(u, 1.5) + 17 * std::pow(smooth(.78, 1, u), 2); }
};
template <class Painter>
void body(Painter& p, std::span<const uint32_t> C, std::span<const uint32_t> Dst, const Geo& g, double t,
          uint32_t seed) {
    for (int y = -int(std::ceil(g.FH)) + 2; y <= 0; ++y) {
        const double u = -double(y) / g.FH, cx = g.ax(u), r = g.rad(u);
        const int    K   = 2 + (rand({jsUint(y), seed}) < .35 ? 1 : 0);
        const double rot = (TAU * K * t) / D, fade = std::min(1., u * 8 + .25) * (1 - smooth(.9, 1.02, u));
        for (int x = int(std::floor(cx - r)); x <= std::ceil(cx + r); ++x) {
            const double c = (x + .5 - cx) / r;
            if (std::abs(c) > 1) continue;
            const double n   = tileNoise(((std::acos(c) + rot) * 6) / TAU, y * .34, 6, 0, seed),
                         v   = -c * .55 + .15 + n * .55;
            uint32_t     col = v > .6 ? C[6] : v > .28 ? C[5] : v > -.05 ? C[4] : v > -.4 ? C[3] : C[2];
            if (u < .3) col = mix(col, Dst[v > .3 ? 3 : v > -.2 ? 2 : 1], .7 * (1 - u / .3));
            p.blend(x, y, col, (.14 + .36 * std::sqrt(1 - c * c)) * fade);
        }
    }
}
template <class Painter>
void bands(Painter& p, std::span<const uint32_t> C, const Geo& g, double t, uint32_t seed, bool back) {
    for (uint32_t j = 0; j < 58; ++j) {
        Rng          R(hash({seed, j}));
        const double u    = (j + R.range(.1, .9)) / 58;
        const int    K    = R.integer(2, 4);
        const double span = R.range(.7, 2.1), lvl = R.next(), cx = g.ax(u), r = g.rad(u) * R.range(1., 1.1),
                     y0 = -u * g.FH, head = R.range(0, TAU) + (TAU * K * t) / D;
        if (u > .97) continue;
        for (double s = 0; s <= span; s += .5 / std::max(2., r)) {
            const double th = head - s, sn = std::sin(th);
            if ((sn > 0) == back) continue;
            const double k = s / span, x = cx + r * std::cos(th), y = y0 + r * SY * sn;
            const bool   lit = std::cos(th) < .2;
            int          i   = lvl < .18 ? 2 : k < .15 && lvl > .55 ? 7 : k < .45 ? (lit ? 6 : 5) : lit ? 5 : 4;
            if (back) i = std::max(1, i - 2);
            if (k < .5)
                p.dot(x, y, C[size_t(i)]);
            else
                p.blend(x, y, C[size_t(i)], 2 * (1 - k));
        }
        if (!back && lvl > .6 && std::sin(head) > 0) {
            const double hx = cx + r * std::cos(head), hy = y0 + r * SY * std::sin(head);
            p.dot(hx, hy, WHITE);
            p.glow(hx, hy, 5, C[6], .3);
        }
    }
}
template <class Painter>
void blob(Painter& p, double cx, double cy, double rx, double ry, uint32_t c, double a) {
    for (int y = int(std::floor(cy - ry)); y <= std::ceil(cy + ry); ++y)
        for (int x = int(std::floor(cx - rx)); x <= std::ceil(cx + rx); ++x) {
            const double dx = (x + .5 - cx) / rx, dy = (y + .5 - cy) / ry;
            if (dx * dx + dy * dy <= 1) p.blend(x, y, c, a);
        }
}
template <class Painter>
void crown(Painter& p, std::span<const uint32_t> C, const Geo& g, double t, uint32_t seed, bool back, double flash) {
    const double cx = g.ax(1), r = g.rad(1), y0 = -g.FH - 3;
    const auto   L = [&](uint32_t c) { return mix(c, C[7], flash * .6); };
    if (back) {
        blob(p, cx, y0 + 1, r * .85, 6, C[1], .6);
        blob(p, cx - 4, y0 - 3, r * .6, 3.5, L(C[3]), .45);
    }
    for (uint32_t j = 0; j < 16; ++j) {
        Rng          R(hash({seed, j}));
        const double rr   = r * R.range(.4, 1.15);
        const int    K    = R.integer(1, 2);
        const double span = R.range(1.4, 2.8), head = R.range(0, TAU) + (TAU * K * t) / D, th0 = R.range(3.2, 6),
                     lift = R.range(-4, 3) - (1 - rr / (r * 1.15)) * 5;
        for (double s2 = 0; s2 <= span; s2 += .6 / rr) {
            const double th = head - s2, sn = std::sin(th);
            if ((sn > 0) == back) continue;
            const double k = s2 / span, th2 = th0 * std::sin(PI * std::pow(1 - k, .7)), x = cx + rr * std::cos(th),
                         y = y0 + lift + rr * SY * sn;
            const int    T = std::max(1, int(jsRound(th2))), d = back ? 1 : 0;
            const bool   lit = std::cos(th) < .3;
            for (int i = 0; i < T; ++i) {
                const uint32_t c = i == 0                ? L(C[size_t((lit ? 6 : 5) - d)])
                                   : i == T - 1 && T > 2 ? C[1]
                                                         : L(C[size_t((i == 1 ? 4 : 3) - d)]);
                p.blend(x, y - T / 2. + i, c, (.55 + .35 * (1 - k)) * (back ? .75 : 1.));
            }
        }
    }
}
void debris(Paint& p, std::span<const uint32_t> L, const Geo& g, double t, uint32_t seed, bool back,
            std::span<const uint32_t> S) {
    cycle(t, D, 24, seed, 1.3, 1.95, [&](double age, double life, Rng& R, int) {
        const double k = age / life, sk = R.range(.15, .3), q = clamp((k - sk) / (1 - sk)),
                     u = lerp(.03, .98, std::pow(q, .85)), phase = R.range(0, TAU), turns = R.range(2, 3.2),
                     th = phase + turns * TAU * k, sn = std::sin(th);
        if ((sn > 0) == back) return;
        const double fling = smooth(.78, 1, q), inward = 1 - smooth(0, sk, k), far = R.range(26, 44);
        const double rr = lerp(std::min(68., g.rad(u) * (1.12 + fling * 1.1) + R.range(2, 7)), far, inward),
                     x = g.ax(u) + rr * std::cos(th), y = std::min(-1., -u * g.FH + rr * SY * sn - fling * 8);
        const double a = std::min(1., k * 8) * (1 - smooth(.85, 1, k)), kind = R.next(), spin = R.range(-14, 14) * age;
        const int    dk = back ? 1 : 0;
        if (kind < .62) {
            const int                     o = R.next() < .5 ? 0 : 4;
            const std::array<uint32_t, 4> pal{
                {L[size_t(o)], L[size_t(o + 1)], L[size_t(o + 2 - dk)], L[size_t(o + 3 - dk)]}};
            const double len = R.range(6, 9), wid = R.range(1.8, 2.6), flip = age * R.range(5, 10);
            nature::drawLeaf(p, x, y, len, wid, spin, flip, pal, a);
        } else if (kind < .8) {
            const double ca = std::cos(spin) * 2.5, sa = std::sin(spin) * 2.5;
            p.line(x - ca, y - sa, x + ca, y + sa, back ? L[0] : L[1], a);
            p.dot(x + ca, y + sa, L[2], a);
        } else {
            const double size  = R.range(1.6, 2.6);
            const auto   shape = nature::stoneShape(R, 5);
            nature::drawRock(p, x, y, size, spin, shape, S, a);
        }
    });
}
template <class Painter>
void skirt(Painter& p, std::span<const uint32_t> Dust, const Geo& g, double t, uint32_t seed, bool back) {
    const double cx0 = g.ax(0);
    cycle(t, D, 40, seed, .7, 1.3, [&](double age, double life, Rng& R, int) {
        const double k = age / life, phase = R.range(0, TAU), speed = R.range(5, 9), th = phase + speed * age,
                     sn = std::sin(th);
        if ((sn > 0) == back) return;
        const double rr = 4 + k * R.range(14, 28), x = cx0 + rr * std::cos(th),
                     y = -1.5 - k * R.range(4, 16) + rr * .3 * sn, s = R.range(3, 6) * (.6 + .8 * k),
                     a = std::min(1., k * 6) * (1 - k) * .6;
        p.disc(x, y + s * .2, s, Dust[back ? 1 : 2], a);
        p.disc(x - s * .2, y - s * .3, s * .55, Dust[back ? 2 : 3], a * .8);
    });
    if (back) return;
    cycle(t, D, 16, seed + 7u, .35, .6, [&](double age, double life, Rng& R, int) {
        const double th = R.range(0, TAU), v = R.range(25, 55), x = cx0 + std::cos(th) * (4 + v * age),
                     y = std::sin(th) * 2 - R.range(40, 80) * age + 260 * age * age;
        if (y < .5) p.dot(x, std::min(0., y), R.next() < .5 ? Dust[3] : Dust[4], 1 - age / life);
    });
    for (uint32_t j = 0; j < 7; ++j) {
        Rng          R(hash({seed + 3u, j}));
        const double rr = R.range(12, 28), th0 = R.range(0, TAU) + (TAU * 3 * t) / D, span = R.range(.5, 1.1);
        for (double s = 0; s < span; s += .6 / rr) {
            const double th = th0 - s;
            p.dot(cx0 + rr * std::cos(th), .5 + rr * .18 * std::sin(th), Dust[s < span * .4 ? 3 : 2], 1 - s / span);
        }
    }
}
double flashAt(double t) {
    const double q = t / D;
    return std::max(0., 1 - std::abs(q - .62) / .025) + .6 * std::max(0., 1 - std::abs(q - .69) / .015);
}
struct Tornado final : AnalyticKernel {
    template <class Painter>
    void paintBack(Painter& p, double t, const PixelFxSettings& o) const {
        const Geo    g{o.nature.height, (TAU * t) / D};
        const double f = flashAt(t);
        skirt(p, o.nature.dust, g, t, o.seed * 5u + 1u, true);
        crown(p, o.palette, g, t, o.seed * 7u + 2u, true, f);
        bands(p, o.palette, g, t, o.seed * 3u + 5u, true);
        debris(p, o.nature.leaves, g, t, o.seed * 11u + 3u, true, o.palette);
        body(p, o.palette, o.nature.dust, g, t, o.seed);
    }
    template <class Painter>
    void paintFront(Painter& p, double t, const PixelFxSettings& o) const {
        const auto&  C    = o.palette;
        const auto&  Dust = o.nature.dust;
        const Geo    g{o.nature.height, (TAU * t) / D};
        const double f = flashAt(t), cx = g.ax(1);
        bands(p, C, g, t, o.seed * 3u + 5u, false);
        crown(p, C, g, t, o.seed * 7u + 2u, false, f);
        skirt(p, Dust, g, t, o.seed * 5u + 1u, false);
        debris(p, o.nature.leaves, g, t, o.seed * 11u + 3u, false, C);
        p.glowLine(g.ax(.1), -g.FH * .1, cx, -g.FH * .9, 26, C[5], .08);
        p.glow(g.ax(0), -2, 30, Dust[3], .12);
        if (f > .05) {
            Rng        R(hash({o.seed, 99u, jsUint(std::floor(t / D * 7))}));
            const auto path = bolt(R, cx - 18, -g.FH - 6, cx + 16, -g.FH + 2, .3, 3);
            p.polyline(path, f > .6 ? WHITE : C[7], std::min(1., f * 1.5));
            p.glow(cx, -g.FH - 2, 70, C[7], .45 * f);
        }
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override { paintBack(p, t, o); }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override { paintFront(p, t, o); }
};
}  // namespace
const Descriptor& tornadoDescriptor() {
    static const std::array<Option, 3> options{{{"dust", 1, 64, false, 5, &PixelFxNatureSettings::dust},
                                                {"leaves", 1, 64, false, 8, &PixelFxNatureSettings::leaves},
                                                {"height", 1, 256, false, 0, &PixelFxNatureSettings::height}}};
    static const Descriptor            d{
        {PixelFxKind::Tornado, "nature/tornado", 196, 168, 98, 152, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed          = 4;
            s.nature.height = 112;
            s.palette       = {hex(0x141221), hex(0x231f33), hex(0x36314a), hex(0x4d4762),
                               hex(0x68617d), hex(0x8a839e), hex(0xb3adc4), hex(0xe2def0)};
            s.nature.dust   = {hex(0x2a2018), hex(0x4e3e2e), hex(0x76624a), hex(0xa08a6a), hex(0xcbb894)};
            s.nature.leaves = {hex(0x4a200c), hex(0xa8541c), hex(0xe8962e), hex(0xffd27a),
                               hex(0x123818), hex(0x2f7a2a), hex(0x6cb83a), hex(0xc8f070)};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Tornado>(); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
