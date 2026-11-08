// eve-hot-path
// Native adaptation of pixelfx critters/jellyfish.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Critter.h"
#include "stylize/pixelfx/CritterBubble.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 3.2, BEAT = D / 2;
struct Bell {
    double x, y, c, W, H;
};
double squeeze(double u) {
    if (u < .16) return ease::outCubic(u / .16);
    if (u < .22) return 1;
    if (u < .72) return lerp(1, -.14, ease::inOutSine((u - .22) / .5));
    return lerp(-.14, 0, ease::inOutSine((u - .72) / .28));
}
double lift(double u) {
    return u < .42 ? -6 * ease::outCubic(seg(u, .04, .42)) : -6 + 6 * ease::inOutSine((u - .42) / .58);
}
Bell bell(double t) {
    t              = wrap(t, D);
    const double u = std::fmod(t, BEAT) / BEAT, c = squeeze(u);
    return {2 * std::sin((t / D) * TAU), lift(u), c, 32 * (1 - .3 * c), 18 * (1 + .24 * c)};
}
double halfW(double h, const Bell& b) {
    if (h > b.H) return -1;
    double hw = (b.W / 2) * std::sqrt(std::max(0., 1 - std::pow(std::max(0., h) / b.H, 2)));
    if (h < b.H * .3) hw -= b.c * 2.4 * (1 - h / (b.H * .3));
    return hw;
}
double scallop(double dx, const Bell& b) { return 1.1 * std::max(0., std::sin((dx / (b.W / 2)) * PI * 4 + PI / 2)); }
void   drawBell(Paint& p, std::span<const uint32_t> C, std::span<const uint32_t> G, const Bell& B, double t) {
    critter::Grid<44, 36> g(-22, -28);
    const double          bx = jsRound(B.x), by = jsRound(B.y);
    const auto            inside = [&](double x, double y) {
        const double dx = x - B.x, h = B.y - y;
        if (h < -scallop(dx, B)) return false;
        const double hw = halfW(std::max(0., h), B);
        return hw >= 0 && std::abs(dx) <= hw;
    };
    g.shade(-20, -26, 20, 3, [&](double x, double y, auto& out) -> uint32_t {
        if (!inside(x + bx, y + by)) return 0;
        out.tag = 1;
        return C[3];
    });
    g.computeDepths(4);
    const double iw = B.W * .36, ih = B.H * .66, lx = B.x - B.W * .22, ly = B.y - B.H * .78;
    g.reworkCells([&](double x, double y, uint32_t c, double, uint8_t tag) {
        if (tag != 1) return c;
        const double X = x + bx + .5, Y = y + by + .5, d = g.cellDepth(x, y), dx = X - B.x, h = B.y - Y, v = h / B.H;
        const int    i   = g.index(x, y);
        auto         col = C[3];
        double       al  = .26 + .2 * clamp(v);
        const double ie  = std::pow(dx / iw, 2) + std::pow(std::max(0., h) / ih, 2);
        if (ie > .8 && ie < 1.12 && h > .5) {
            col = C[4];
            al  = .55;
        }
        if (h < 1) {
            col = C[5];
            al  = .85;
        }
        if (d == 2) {
            col = v > .5 && dx < 0 ? C[5] : C[4];
            al  = .75;
        }
        if (d == 1) {
            col = dx < 0 && v > .25 ? C[6] : C[5];
            al  = .92;
        }
        const double gx = X - lx, gy = Y - ly;
        if (d == 2 && dx < -1 && v > .45 && gx * gx + gy * gy < 26) {
            col = C[7];
            al  = 1;
        }
        g.alpha[size_t(i)] = float(al);
        return col;
    });
    for (double s : {-.62, .62})
        for (double h = 2; h < B.H - 2; ++h) {
            const double hw = halfW(h, B), x = B.x + s * hw * .9, y = B.y - h;
            if (g.cellDepth(x - bx, y - by) >= 3) g.set(x - bx, y - by, C[4], .5);
        }
    const double pulse = .5 + .5 * B.c, cy = B.y - B.H * .42, rx = B.W * .2, ry = B.H * .2;
    for (double y = std::floor(cy - ry); y <= cy + ry; ++y)
        for (double x = std::floor(B.x - rx); x <= B.x + rx; ++x) {
            const double d = std::pow((x + .5 - B.x) / rx, 2) + std::pow((y + .5 - cy) / ry, 2);
            if (d > 1 || g.cellDepth(x - bx, y - by) < 2) continue;
            g.set(x - bx, y - by, d < .3 ? C[6] : d < .65 ? C[5] : C[4], d < .3 ? .9 : .6);
        }
    g.outline([&](double, double, int, int ny) { return ny > 0 ? 0u : C[1]; }, .9);
    g.draw(p, 1, bx, by);
    p.glow(B.x, B.y - B.H * .45, 30 + 6 * B.c, C[4], .22 + .12 * pulse);
    p.glow(B.x, B.y - B.H * .45, 11, C[6], .14 + .16 * pulse);
    for (int s : {-1, 1})
        for (double q : {.14, .42, .78}) {
            const double x = B.x + s * halfW(.5, B) * q, y = B.y + 1,
                         tw = .6 + .4 * std::sin((t / D) * TAU * 3 + q * 9 + s);
            p.px(x, y, tw > .85 ? G[3] : G[2]);
            p.glow(x, y, 4, G[2], .4 * tw);
        }
}
struct Spec {
    double q, length, phase;
    bool   back;
};
std::array<Spec, 13> prepare(uint32_t seed) {
    std::array<Spec, 13> specs{};
    for (uint32_t i = 0; i < 9; ++i) {
        Rng          r(hash({seed, i, 4}));
        const double q = -.88 + (1.76 * i) / 8 + r.range(-.05, .05), len = r.range(36, 50), phase = r.range(0, TAU);
        specs[i] = {q, len, phase, i % 2 == 1};
    }
    constexpr std::array<double, 4> q{-.32, -.1, .12, .3};
    for (uint32_t i = 0; i < 4; ++i) {
        Rng          r(hash({seed, i, 9}));
        const double len = r.range(20, 27), phase = r.range(0, TAU) + i;
        specs[9 + i] = {q[i], len, phase, i == 1 || i == 2};
    }
    return specs;
}
struct Strand {
    std::array<Point, 40> points{};
    size_t                count = 0;
};
Strand strand(double t, const Spec& spec, bool arm) {
    Strand       pts;
    const auto   B1  = bell(t);
    const double o1x = arm ? spec.q * B1.W * .5 : spec.q * halfW(.5, B1), o1y = arm ? -1 : .5, step = arm ? 1 : 1.5,
                 lag = arm ? .03 : .022, sway = arm ? .06 : .065, pull = arm ? .4 : 1;
    for (double s = 0; s <= spec.length; s += step) {
        const auto   B     = bell(t - s * lag);
        const double relax = 1 - clamp(B.c), off = arm ? spec.q * B.W * .5 : spec.q * halfW(.5, B),
                     ox = lerp(o1x, off, .4),
                     w  = (.3 + s * sway) * (.55 + .65 * relax) * std::sin((t / D) * TAU * 2 - s * .11 + spec.phase);
        pts.points[pts.count++] = {B.x + ox + w + ox * s * (.009 * relax - .005 * pull * B.c),
                                   B.y + o1y + s * (.88 + .05 * B.c)};
    }
    return pts;
}
void drawTentacle(Paint& p, std::span<const uint32_t> C, std::span<const uint32_t> G, const Strand& pts, bool back,
                  double t, int i) {
    for (size_t k = 1; k < pts.count; ++k) {
        const double u = double(k) / double(pts.count - 1);
        const auto   a = pts.points[k - 1], b = pts.points[k];
        const auto   c = back      ? (u < .3   ? C[3]
                                      : u < .7 ? C[2]
                                               : C[1])
                         : u < .2  ? C[5]
                         : u < .45 ? C[4]
                         : u < .78 ? C[3]
                                   : C[2];
        p.line(a.x, a.y, b.x, b.y, c);
    }
    if (back) return;
    for (size_t k = 3; k < pts.count; k += 4) {
        const double tw = std::sin((t / D) * TAU * 2 - double(k) * .5 + i * 1.7);
        if (tw < .3) continue;
        const auto a = pts.points[k];
        p.px(a.x, a.y, tw > .8 ? G[3] : G[2]);
        p.glow(a.x, a.y, 3.5, G[2], .35 * tw);
    }
}
void drawArms(Paint& p, std::span<const uint32_t> C, const std::array<Strand, 13>& list,
              const std::array<Spec, 13>& specs, bool back) {
    critter::Grid<80, 100> g(-40, -40);
    const double           alpha = back ? .6 : .88;
    for (size_t a = 9; a < 13; ++a) {
        if (specs[a].back != back) continue;
        const auto& pts     = list[a];
        double      lastRow = -1e9;
        for (size_t k = 1; k < pts.count; ++k) {
            const auto   a0 = pts.points[k - 1], a1 = pts.points[k];
            const double m = std::max(1., std::ceil(std::abs(a1.y - a0.y) * 2));
            for (double j = 0; j < m; ++j) {
                const double q = j / m, x = lerp(a0.x, a1.x, q), y = lerp(a0.y, a1.y, q),
                             u = (double(k) - 1 + q) / double(pts.count - 1), wd = lerp(2.2, .6, u);
                for (double i = -std::ceil(wd); i <= std::ceil(wd); ++i) {
                    if (std::abs(i) > wd + .2) continue;
                    g.set(x + i, y, i < 0 ? C[back ? 4 : 6] : i > 0 ? C[back ? 2 : 4] : C[back ? 3 : 5], alpha);
                }
                const double row = std::floor(y);
                if (row != lastRow && std::fmod(row, 3) == 0 && u < .85)
                    g.set(x + (std::fmod(row / 3, 2) != 0 ? -wd - 1.2 : wd + 1.2), y, C[back ? 4 : 6], alpha);
                lastRow = row;
            }
        }
    }
    g.draw(p);
}
struct Jellyfish final : AnalyticKernel {
    const std::array<Spec, 13> specs;
    explicit Jellyfish(uint32_t seed) : specs(prepare(seed)) {}
    void drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void draw(Paint& p, double time, const PixelFxSettings& o) const override {
        const double                  t = critter::shift(time, D, o.seed);
        const auto&                   C = o.palette;
        const auto&                   G = o.critter.glow;
        const auto                    B = bell(t);
        const std::array<uint32_t, 4> bubbles{G[3], G[3], G[2], G[1]};
        cycle(t, D, 16, o.seed * 5u + 1u, D, [&](double age, double life, Rng& r, int) {
            const double x = r.range(-42, 42) + std::sin(age * .8 + r.next() * 6) * 3,
                         y = r.range(-34, 66) + age * r.range(1, 4), tw = std::sin(age * 2.2 + r.next() * 6),
                         a = std::min({1., age * 2, (life - age) * 2}) * (.35 + .35 * tw);
            p.dot(x, y, tw > .8 ? G[2] : C[3], a);
        });
        std::array<Strand, 13> paths;
        for (size_t i = 0; i < 13; ++i) paths[i] = strand(t, specs[i], i >= 9);
        for (uint32_t b = 0; b < 2; ++b) {
            const double age = wrap(t - b * BEAT - .05, D);
            if (age > .9) continue;
            const auto   B0 = bell(b * BEAT + .05);
            const double e = ease::outCubic(age / .9), k = 1 - age / .9, cx = B0.x, cy = B0.y + 3 + 14 * e,
                         rx = B0.W * .32 + 9 * e, ry = 1.5 + 2.5 * e;
            for (int j = 0; j < 8; ++j) {
                const double a = (double(j) / 8) * TAU + e * .8;
                p.glow(cx + std::cos(a) * rx, cy + std::sin(a) * ry, 5 + 3 * e, C[5], .14 * k);
            }
            for (int j = 0; j < 7; ++j) {
                const double a = (double(j) / 7) * TAU + e * 2.4 + b, x = cx + std::cos(a) * rx * 1.05,
                             y = cy + std::sin(a) * ry * 1.4 - std::cos(a * 2) * .6;
                if (k < .15 && j % 2) continue;
                p.dot(x, y, std::sin(a) > 0 ? G[3] : G[2], std::min(1., k * 2.5));
                p.glow(x, y, 3, G[2], .3 * k);
            }
            for (uint32_t i = 0; i < 4; ++i) {
                Rng          r(hash({o.seed, b, i, 13}));
                const double life = r.range(1.4, 2.2), ag = wrap(t - b * BEAT - .08 - i * .04, D);
                if (ag > life) continue;
                const double rad = r.range(.6, 1.5), sx = r.sign(),
                             x = B0.x + sx * (B0.W * .4 + r.range(0, 4) + ag * r.range(4, 10)),
                             y = B0.y + 2 - (14 + 10 * rad) * ag + 6 * std::exp(-ag * 4);
                if (y < -33) continue;
                critter::bubbleLook(p, x + std::sin(ag * 5 + i) * .8, y, rad, bubbles,
                                    std::min({1., ag * 6, (life - ag) * 3, (y + 33) / 6}));
            }
        }
        for (size_t i = 0; i < 9; ++i)
            if (specs[i].back) drawTentacle(p, C, G, paths[i], true, t, int(i));
        drawArms(p, C, paths, specs, true);
        drawBell(p, C, G, B, t);
        drawArms(p, C, paths, specs, false);
        for (size_t i = 0; i < 9; ++i)
            if (!specs[i].back) drawTentacle(p, C, G, paths[i], false, t, int(i));
        cycle(t, D, 7, o.seed * 5u + 2u, 1.8, 3, [&](double age, double life, Rng& r, int) {
            const bool   small = r.isChance(.6);
            const double rad = small ? r.range(.6, 1.4) : r.range(1.6, 2.6), x0 = r.range(-30, 30),
                         y0 = r.range(10, 70), v = 20 + 13 * rad, end = std::min(life, (y0 + 30) / v);
            if (age > end) return;
            const double ph = r.next() * TAU, x = x0 + std::sin(age * (4 + 2 / rad) + ph) * (.6 + rad * .8),
                         y = y0 - v * age;
            critter::bubbleLook(p, x, y, rad, bubbles, std::min({1., age * 3, (end - age) * 3}),
                                .1 * std::sin(age * 9 + ph));
        });
        p.glow(B.x, B.y + 6, 58, C[3], .14);
    }
};
}  // namespace
const Descriptor& jellyfishDescriptor() {
    static const std::array<Option, 1> options{{{"glow", 1, 64, false, 4, &PixelFxCritterSettings::glow}}};
    static const Descriptor            d{{PixelFxKind::Jellyfish, "critters/jellyfish", 96, 120, 48, 38, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed         = 1;
                                  s.palette      = {hex(0x140b2b), hex(0x2a1650), hex(0x4b2386), hex(0x7a3cb8),
                                                    hex(0xb066e0), hex(0xe09cf5), hex(0xffd6ff), WHITE};
                                  s.critter.glow = {hex(0x0b3550), hex(0x1f7fa8), hex(0x4fd2e8), hex(0xb8fbff)};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<Jellyfish>(s.seed);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
