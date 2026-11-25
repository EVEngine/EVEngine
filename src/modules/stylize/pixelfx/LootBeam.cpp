// eve-hot-path
// Native adaptation of pixelfx pickups/loot-beam.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Pickup.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                D = 2.4, H = 150, OX = 4;
constexpr std::array<double, 2> RINGS{.08, .58};
const std::array<uint32_t, 6>   STEEL{hex(0x151726), hex(0x2c3149), hex(0x4f5775), hex(0x8a93b3), hex(0xd6dcef), WHITE};
const std::array<uint32_t, 6>   GOLD{hex(0x3a2008), hex(0x7a4a10), hex(0xb8801c),
                                     hex(0xe8b432), hex(0xffe27a), hex(0xfff6cc)};
const std::array<uint32_t, 3>   LEATHER{hex(0x1e0f0e), hex(0x3e2016), hex(0x633620)};
double                          since(double ph, size_t k) { return wrap(ph - RINGS[k], 1); }
double                          swordAxis(double x) { return std::floor(-4 + (x + 4) / 7 + .5); }
void                            sword(Paint& p, std::span<const uint32_t> C, double ph) {
    pickup::Layer<40, 24> L;
    L.begin(-16, -12);
    const auto set     = [&](double x, double y, uint32_t c) { L.set(x + OX, y, c); };
    const auto edgeLit = [&](double x) { return std::abs(x + OX - .5) < 6 ? C[6] : mix(STEEL[4], C[6], .45); };
    for (int x = -11; x <= 10; ++x) {
        const double a = swordAxis(x);
        if (x <= -9) {
            const int m = x + 10;
            if (m == 0) {
                set(x, a - 1, C[6]);
                set(x, a, WHITE);
                set(x, a + 1, C[4]);
            } else
                set(x, a, m < 0 ? C[5] : C[3]);
        } else if (x <= -5) {
            set(x, a, LEATHER[x % 2 ? 2 : 1]);
            set(x, a + 1, LEATHER[x % 2 ? 1 : 0]);
        } else if (x <= -3) {
            for (double y = a - 3; y <= a + 3; ++y) {
                const bool end = y == a - 3 || y == a + 3;
                set(x, y,
                    y == a    ? (x == -4 ? C[6] : C[4])
                    : x == -4 ? (end ? GOLD[3] : mix(GOLD[4], C[6], .3))
                    : end     ? GOLD[0]
                              : GOLD[1]);
            }
        } else {
            const int tip = x - 8;
            if (tip < 1) set(x, a - 1, edgeLit(x));
            if (tip < 2) set(x, a, tip == 1 ? edgeLit(x) : mix(STEEL[3], C[4], .2));
            set(x, a + 1, tip == 2 ? STEEL[2] : STEEL[0]);
        }
    }
    L.outline(hex(0x0d0b14));
    L.draw(p);
    const double g = (ph - .72) / .16;
    if (g > 0 && g < 1) {
        const double x = -1 + 9 * ease::inOutSine(g);
        pickup::glint(p, x + OX, swordAxis(jsRound(x)) - 1, 2.6 * std::sin(g * PI), WHITE, .5);
    }
}
void ring(Paint& p, std::span<const uint32_t> C, double age, bool back) {
    const double k = age / 1.1;
    if (k >= 1) return;
    const double                  r = 3 + 24 * ease::outCubic(k), th = std::max(1.0, 3.2 * (1 - k));
    const std::array<uint32_t, 4> ramp{C[2], C[4], C[5], C[6]};
    band(
        p, .5, 0, r, .2, th, std::min(1.0, 1.25 * (1 - k)), ramp,
        [back](double g) { return (std::sin(g) < 0) == back; }, k < .12);
    if (!back)
        for (int s : {-1, 1}) p.glow(.5 + s * r, -1, 8, C[5], .35 * (1 - k));
}
void column(Paint& p, std::span<const uint32_t> C, double t) {
    const double ph   = t / D;
    double       beat = 0;
    for (size_t k = 0; k < RINGS.size(); ++k) {
        const double s = since(ph, k) * D;
        beat           = std::max({beat, std::exp(-s * 7) * (s < .5 ? 1 : 0), std::exp(-((D - s) * 30))});
    }
    const double gq = (ph - .22) / .42, gh = gq > 0 && gq < 1 ? 4 + (H - 4) * ease::inQuad(gq) : -100;
    const auto   shimmer = jsUint(std::floor(ph * 24)), flick = jsUint(std::floor(ph * 12));
    for (int y = -1; y >= -int(H); --y) {
        const double h = -double(y) - .5, u = h / H, fade = 1 - smooth(.25, 1, u),
                     w       = (4.2 * (1 - .45 * u) + 3.2 * std::exp(-h / 7)) * (1 + .2 * beat);
        const auto   segment = jsUint(std::floor(h / 3 + rand({jsUint(std::floor(h / 9)), 5}) * 2));
        if (u > .66 && rand({segment, flick, 7}) > 1.75 - u * 1.65) continue;
        const bool   thread = u > .58 && rand({segment, flick, 9}) < (u - .58) * 3;
        const double climb  = wrap(h / 30 - ph * 3, 1) < .1 ? 1 : 0,
                     trail  = gh - h > 0 && gh - h < 18 ? 1 - (gh - h) / 18 : 0;
        const int    X      = thread ? 0 : int(std::ceil(w * 1.6));
        for (int x = -X; x <= X; ++x) {
            const double q = std::abs(x) / w, cw = .2 + .18 * (1 - smooth(0, .5, u));
            int          lvl = q < cw ? 0 : q < .47 ? 1 : q < .72 ? 2 : q < 1 ? 3 : 4;
            for (int s = 0; s < 3; ++s) {
                const double sx = (s - 1) * w * .55 + std::sin(h * .09 + ph * TAU * (s + 1) + s * 2) * .9;
                if (std::abs(x - sx) < .5) lvl = std::max(0, lvl - 1);
            }
            if (rand({jsUint(y), shimmer, 3}) < .05 && lvl > 0) --lvl;
            if (q < .72) lvl = std::max(0, lvl - int(climb) - (trail > .3 ? 1 : 0));
            const double alpha = lvl <= 2 ? 1 : (1.55 - q) / .75 * fade * 1.2;
            if (alpha <= 0) continue;
            p.dot(x, y,
                  thread     ? (u > .85 ? C[6] : WHITE)
                  : lvl == 0 ? (u > .8 ? C[6] : WHITE)
                             : C[size_t(7 - lvl)],
                  alpha);
        }
        const double near = smooth(8, 20, h);
        if ((y & 3) == 0) p.glow(.5, y, w * 2.6 + 6, C[5], .36 * near * (.3 + .7 * fade) * (1 + .4 * beat));
        if ((y & 15) == 0) p.glow(.5, y, 34, C[4], .2 * near * (.25 + .75 * fade));
    }
    if (gh > 0) {
        pickup::glint(p, 0, -gh, 3.5 * (1 - gq * .5), C[6], .6);
        p.glow(.5, -gh, 14, C[5], .45);
    }
    flare(p, .5, -5, 8 + 3 * beat + std::sin(ph * TAU * 4) * .8, .6 + .4 * beat, C);
    p.glow(.5, -16, 16, C[4], .12 + .25 * beat);
}
void motes(Paint& p, std::span<const uint32_t> C, double t, uint32_t seed) {
    cycle(t, D, 30, seed, 1, 2.2, [&](double age, double life, Rng& r, int) {
        const bool   inner = r.isChance(.3);
        const int    side  = r.sign();
        const double x0 = inner ? r.range(-3, 3) : side * r.range(4, 15), vy = r.range(22, 52) * (inner ? 1.5 : 1),
                     w = r.range(1.5, 3.5), phase = r.range(0, TAU), x = x0 + std::sin(age * w + phase) * 1.6,
                     y = -r.range(1, 10) - vy * age, k = std::min({1.0, age / .25, (life - age) / .5});
        if (k <= 0 || y < -H) return;
        if (r.isChance(.22)) {
            const double rate = r.range(9, 14), angle = r.range(0, TAU), tw = .5 + .5 * std::sin(age * rate + angle);
            if (tw * k > .3) pickup::glint(p, x, y, tw > .75 ? 2 : 1, C[6], .4 * k);
            return;
        }
        p.dot(x, y, inner ? WHITE : C[6], k * 1.5);
        p.dot(x, y + 1, inner ? C[6] : C[4], k);
        p.dot(x, y + 2, C[3], k * .6);
        p.glow(x + .5, y + .5, 3.5, C[5], .35 * k);
    });
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D;
    pickup::pool(p, .5, 0, 34, 5, C[4], .14);
    p.shader(-30, -.5, 31, 6, [&](double x, double y, ShadeOutput& out) {
        const double dx = (x - .5) / 30, dy = y / 6, d = dx * dx + dy * dy;
        if (d >= 1) return;
        out.l          = C[5];
        const double f = 1 - std::sqrt(d);
        out.k          = .4 * (f * f);
    });
    for (int x = -16; x <= 16; ++x) {
        const double q = std::abs(x) / 16.5, alpha = 1.35 - q * 1.25;
        p.dot(x, 0, q < .3 ? WHITE : q < .6 ? C[6] : C[5], alpha);
        if (q < .75) p.dot(x, 1, q < .3 ? C[6] : q < .55 ? C[5] : C[4], alpha * .8);
        if (q < .4) p.dot(x, 2, C[4], alpha * .5);
    }
    for (size_t k = 0; k < RINGS.size(); ++k) ring(p, C, since(ph, k) * D, true);
    column(p, C, t);
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D;
    pickup::blobShadow(p, OX - .5, 12.5, .95);
    sword(p, C, ph);
    for (size_t k = 0; k < RINGS.size(); ++k) ring(p, C, since(ph, k) * D, false);
    motes(p, C, t, o.seed);
}
}  // namespace
const Descriptor& lootBeamDescriptor() {
    static const Descriptor d{{PixelFxKind::LootBeam, "pickups/loot-beam", 96, 176, 48, 162, D, true},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed    = 4;
                                  s.palette = {hex(0x140a24), hex(0x2c1250), hex(0x4b1d86), hex(0x7330c0),
                                               hex(0xa050f0), hex(0xc890ff), hex(0xecd4ff), WHITE};
                              },
                              front,
                              {},
                              nullptr,
                              back};
    return d;
}
}  // namespace eve::stylize::pixelfx
