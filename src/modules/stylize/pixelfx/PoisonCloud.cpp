// eve-hot-path
// Native adaptation of pixelfx nature/poison-cloud.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 4;
struct Sheet {
    double height, extent, alpha;
    int    dim, flow, count;
    bool   back;
};
constexpr std::array<Sheet, 3> SHEETS{
    {{36, .84, .46, 2, 4, 2, true}, {26, .96, .5, 1, 8, 2, true}, {16, 1.04, .56, 0, 8, 3, false}}};
struct Tongue {
    int    side;
    double phase, x, height;
};
struct Tongues {
    std::array<Tongue, 6> values;
    size_t                count = 0;
};
Tongues tongues(const Sheet& sh, size_t si, double E, double ph) {
    Tongues out;
    for (int side : {-1, 1})
        for (int i = 0; i < sh.count; ++i) {
            const double q =
                std::fmod(ph + double(i) / sh.count + double(si) * .17 + (side > 0 ? .5 / sh.count : 0), 1);
            out.values[out.count++] = {side, q, side * E * (.3 + .82 * q), sh.height * (.75 - .45 * q)};
        }
    return out;
}
double height(const Sheet& sh, size_t si, double E, double x, double ph, const Tongues& T, uint32_t seed) {
    const double ax = std::abs(x), env = 1 - smooth(.5, 1, ax / (E * sh.extent)),
                 r = tileNoise(ax * .07 - ph * sh.flow, double(si) * 3.7, sh.flow, 0, seed + uint32_t(si));
    double       h = sh.height * env * (.55 + .45 * r);
    for (size_t i = 0; i < T.count; ++i) {
        const auto&  g      = T.values[i];
        const double behind = g.side * (g.x - x);
        if (behind < -1.5 || behind > 30) continue;
        h = std::max(h, g.height * std::sqrt(std::max(0., 1 - behind / 30)) * smooth(-1.5, 1.5, behind) *
                            (1 - smooth(.75, 1, g.phase)));
    }
    return h;
}
template <class Painter>
void sheet(Painter& p, std::span<const uint32_t> C, const Sheet& sh, size_t si, double E, double t, uint32_t seed) {
    const double ph = t / D;
    const auto   T  = tongues(sh, si, E, ph);
    const int    d = sh.dim, X = int(std::ceil(E * 1.25));
    for (int x = -X; x <= X; ++x) {
        const double H = height(sh, si, E, x + .5, ph, T, seed);
        if (H < .6) continue;
        const int top = -int(jsRound(H));
        for (int y = top; y <= 1; ++y) {
            const double   v      = clamp(-y / H),
                           streak = .7 + .3 * tileNoise(x * .05 - ph * sh.flow * (x < 0 ? -1 : 1), y * .55, sh.flow, 0,
                                                        seed + 9u + uint32_t(si)),
                           lv     = v - (streak - .7) * .6;
            const uint32_t c      = y == top   ? C[size_t(std::max(1, 4 - d))]
                                    : lv < .16 ? C[size_t(6 - d)]
                                    : lv < .42 ? C[size_t(5 - d)]
                                    : lv < .68 ? C[size_t(4 - d)]
                                               : C[size_t(std::max(1, 3 - d))];
            p.blend(x, y, c, sh.alpha * (y == top ? .65 : (.6 + .4 * (1 - v)) * streak));
        }
    }
    for (size_t i = 0; i < T.count; ++i) {
        const auto&  g = T.values[i];
        const double k = std::sin(PI * g.phase);
        if (k < .15 || g.height < 4) continue;
        const double r = 1.8 + g.height * .22, cx = g.x - g.side * r * .6, cy = -g.height * .72,
                     spin = -g.side * g.phase * TAU * 2.2;
        for (double a = 0; a < 4.6; a += .1) {
            const double rr = r * (1 - a * .12), px = cx + g.side * std::cos(spin + a) * rr,
                         py = cy + std::sin(spin + a) * rr, w = 1.6 * (1 - a / 6);
            for (double o = -w; o <= w; o += .8) {
                const double qx = px + g.side * std::cos(spin + a) * o, qy = py + std::sin(spin + a) * o;
                p.blend(qx, qy, o > w * .4 ? C[size_t(5 - d)] : C[size_t(4 - d)], sh.alpha * .75 * k * (1 - a / 5.5));
            }
        }
    }
}
template <class Painter>
void plumes(Painter& p, std::span<const uint32_t> C, double E, double t, uint32_t seed) {
    cycle(t, D, 5, seed, 2.4, 3.2, [&](double age, double L, Rng& R, int) {
        const double u = age / L, x0 = R.range(-.55, .55) * E, rise = R.range(13, 19), len = R.range(34, 48),
                     ph = R.range(0, TAU), amp = R.range(3, 5), head = 14 + rise * age,
                     tail = std::max(10., head - len), fade = smooth(0, .15, u) * (1 - smooth(.5, 1, u));
        for (double h = tail; h <= head; h += .7) {
            const double s = (h - tail) / std::max(1., head - tail), tw = std::cos(h * .22 + age * 2.4 + ph),
                         w = .6 + 2.2 * std::abs(tw) * (1 - s * .5),
                         x = x0 + amp * std::sin(h * .12 + age + ph) * (.4 + s),
                         a = fade * std::sin(PI * std::min(1., s * 1.2)) * .32;
            for (double o = -w; o <= w; o += 1)
                p.blend(x + o, -h,
                        tw > 0                 ? (std::abs(o) < w * .5 ? C[6] : C[5])
                        : std::abs(o) < w * .5 ? C[3]
                                               : C[2],
                        a);
        }
    });
}
template <class Painter>
void specks(Painter& p, std::span<const uint32_t> C, double E, double t, uint32_t seed) {
    cycle(t, D, 30, seed, 1.5, 2.6, [&](double age, double L, Rng& R, int) {
        const double u     = age / L;
        const int    side  = R.sign();
        const double start = R.range(.05, .7), speed = R.range(6, 14), x = side * (start * E + age * speed),
                     y0 = -R.range(2, 20), frequency = R.range(1.5, 3), phase = R.next(),
                     y = y0 - std::sin(age * frequency + phase * TAU) * 2, twinkle = R.range(6, 10), offset = R.next(),
                     a = std::min(1., u * 5) * std::min(1., (1 - u) * 3) *
                         (.6 + .4 * std::sin(age * twinkle + offset * TAU));
        if (a < .1) return;
        p.dot(x, y, a > .7 ? C[7] : C[6], a * 1.3);
        p.glow(x, y, 4, C[6], .3 * a);
    });
}
struct PoisonCloud final : AnalyticKernel {
    template <class Painter>
    void paintBack(Painter& p, double t, const PixelFxSettings& o) const {
        const auto&  C = o.palette;
        const double E = 96 * o.nature.spread, pulse = .85 + .15 * std::sin((TAU * t * 2) / D);
        for (double x = -E; x <= E; x += 22) p.glow(x, -3, 30, C[5], .14 * pulse * (1 - std::abs(x) / (E * 1.3)));
        for (uint32_t i = 0; i < 6; ++i) {
            Rng          R(hash({o.seed, i, 7u}));
            const double phase = R.range(0, TAU), scale = R.range(.2, .7), offset = R.range(-.3, .3),
                         x = std::sin((TAU * t) / D + phase) * E * scale + offset * E,
                         k = .6 + .4 * std::sin((TAU * t * 3) / D + i), y = -R.range(4, 10), radius = R.range(14, 22);
            p.glow(x, y, radius, C[6], .2 * k);
        }
        for (size_t i = 0; i < SHEETS.size(); ++i)
            if (SHEETS[i].back) sheet(p, C, SHEETS[i], i, E, t, o.seed);
    }
    template <class Painter>
    void paintFront(Painter& p, double t, const PixelFxSettings& o) const {
        const double E = 96 * o.nature.spread;
        plumes(p, o.palette, E, t, o.seed * 7u + 2u);
        for (size_t i = 0; i < SHEETS.size(); ++i)
            if (!SHEETS[i].back) sheet(p, o.palette, SHEETS[i], i, E, t, o.seed);
        specks(p, o.palette, E, t, o.seed * 11u + 5u);
        p.glow(0, -6, E * .9, o.palette[5], .1);
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override { paintBack(p, t, o); }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override { paintFront(p, t, o); }
};
}  // namespace
const Descriptor& poisonCloudDescriptor() {
    static const std::array<Option, 1> options{{{"spread", 0, 4, false, 0, &PixelFxNatureSettings::spread}}};
    static const Descriptor            d{{PixelFxKind::PoisonCloud, "nature/poison-cloud", 256, 100, 128, 86, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed          = 3;
                                  s.nature.spread = 1;
                                  s.palette       = {hex(0x0e0816), hex(0x24123a), hex(0x3e2056), hex(0x5a3a62),
                                                     hex(0x6a6a3a), hex(0x96aa30), hex(0xcce040), hex(0xf4ffa0)};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<PoisonCloud>();
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
