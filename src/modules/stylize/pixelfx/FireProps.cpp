// eve-hot-path
// Adapted from pixelfx fire/{torch,candle}.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Fire.h"
#include "stylize/pixelfx/PixelFxInternal.h"

namespace eve::stylize::pixelfx {
void fireDefaults(PixelFxSettings& s, PixelFxKind kind) {
    s.palette = {hex(0x4a1712), hex(0x8a1f17), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                 hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
    if (kind == PixelFxKind::FireBreath) {
        s.seed       = 6;
        s.fire.speed = 320;
    } else if (kind == PixelFxKind::Flamethrower) {
        s.seed       = 3;
        s.fire.speed = 280;
        s.fire.pilot = {hex(0x1b2a8a), hex(0x3a62e0), hex(0xa8d4ff)};
    } else if (kind == PixelFxKind::FireTornado) {
        s.seed = 4;
    } else if (kind == PixelFxKind::Fireball) {
        s.seed = 5;
    } else if (kind == PixelFxKind::SpiritFlame) {
        s.seed    = 7;
        s.palette = {hex(0x040a16), hex(0x08203a), hex(0x0b3a52), hex(0x0f5e6e), hex(0x16908e),
                     hex(0x2cc4b4), hex(0x7ef0dc), hex(0xc8fff2), WHITE};
    } else if (kind == PixelFxKind::Torch) {
        s.seed      = 4;
        s.fire.wood = {hex(0x0e080a), hex(0x24130f), hex(0x3b2216), hex(0x5a3520), hex(0x8a5a34), hex(0xb9884f)};
        s.fire.iron = {hex(0x0d0b12), hex(0x1e1b26), hex(0x363242), hex(0x5b5566), hex(0x8a8396)};
    } else if (kind == PixelFxKind::Brazier) {
        s.seed      = 8;
        s.fire.iron = {hex(0x0b0910), hex(0x1c1924), hex(0x302c3a), hex(0x4c4658), hex(0x78708a)};
    } else if (kind == PixelFxKind::Candle) {
        s.seed       = 2;
        s.fire.root  = {hex(0x1b2a8a), hex(0x3a62e0), hex(0x7fb4ff)};
        s.fire.wax   = {hex(0x2e201e), hex(0x5e4436), hex(0x8e6e58), hex(0xb8957a), hex(0xd8b896), hex(0xf0dcc0)};
        s.fire.brass = {hex(0x2a1a10), hex(0x6a4a1e), hex(0xb4862e), hex(0xe8c060), hex(0xfff0b0)};
    }
}
void torch(Paint& p, double t, const PixelFxSettings& o) {
    std::array<uint32_t, 9> H{};
    std::reverse_copy(o.palette.begin(), o.palette.end(), H.begin());
    const auto&       W = o.fire.wood;
    const auto&       I = o.fire.iron;
    const uint32_t    S = o.seed;
    const fire::Noise N{2, t};
    const double      fl = 0.74 + 0.16 * N(7.3, S + 1) + 0.1 * N(19, S + 2);
    p.shader(-5.5, -5.5, 5.5, 11.5, [&](double x, double y, ShadeOutput& out) {
        if (x < -5 || x > 5 || y < -5 || y > 11) {
            out.c = I[0];
            return;
        }
        const bool rivet = std::abs(x) < 0.6 && (std::abs(y + 3.5) < 0.6 || std::abs(y - 9.5) < 0.6);
        out.c            = rivet              ? mix(I[4], H[3], 0.35 * fl)
                           : y < -4 || x < -4 ? mix(I[3], H[4], 0.3 * fl)
                           : y > 10 || x > 4  ? I[1]
                           : y < 2            ? I[2]
                                              : I[1];
    });
    p.shader(-3, -12, 3, 15, [&](double x, double y, ShadeOutput& out) {
        const double half = 1.3 + clamp((14 - y) / 26) * 0.9;
        if (std::abs(x) > half + 0.5 || y > 14.5) return;
        if (std::abs(x) > half - 0.5 || y > 13.5) {
            out.c = W[0];
            return;
        }
        const double g = rand({jsUint(std::floor(y / 3)), jsUint(jsRound(x)), 9});
        out.c          = x < -0.3 ? (g < 0.3 ? W[2] : W[4]) : g < 0.4 ? W[1] : W[2];
    });
    p.rect(-3, -2, 7, 4, I[0]);
    p.rect(-2, -1, 5, 1, mix(I[4], H[3], 0.3 * fl));
    p.rect(-2, 0, 5, 1, I[2]);
    constexpr std::array<fire::Tongue, 5> tongues{{{-1.9, -1, 0.74, 2, true},
                                                   {2.1, -1, 0.7, 2, true},
                                                   {-3.4, 0.5, 0.42, 1.6, false},
                                                   {3.6, 0.5, 0.46, 1.6, false},
                                                   {0, 0, 1, 2.9, false}}};
    fire::BurnOptions                     options;
    options.duration   = 2;
    options.seed       = S;
    options.y          = -16;
    options.height     = o.fire.height;
    options.lean       = 0.12;
    options.spread     = 4;
    options.puffs      = 10;
    options.licks      = 5;
    options.smoke      = 7;
    options.smokeSize  = 2.3;
    options.smokeLife  = {1.3, 2.1};
    options.embers     = 7;
    options.glowRadius = 48;
    options.glow       = 1.25;
    options.wind       = 2;
    fire::burn(p, t, tongues, H, options, [&](double) {
        p.shader(-6, -18, 6, -9, [&](double x, double y, ShadeOutput& out) {
            const double v = (y + 17.5) / 7.5, half = 4.6 - v * 1.9;
            if (std::abs(x) > half + 0.5 || y < -17.5 || y > -9.5) return;
            if (std::abs(x) > half - 0.5 || y > -10.5) {
                out.c = W[0];
                return;
            }
            const bool   cord = int(std::floor(y - x * 0.6 + 40)) % 3 == 0;
            const double h    = rand({jsUint(std::floor(x)), jsUint(std::floor(y)), 31});
            if (v < 0.45) {
                if (h < 0.42) {
                    const double g = 0.5 + 0.5 * std::sin(t / 2 * PI * 4 + h * 40);
                    out.k          = 0.3 * g;
                    out.c          = g > 0.66 ? H[2] : g > 0.33 ? H[4] : H[5];
                    return;
                }
                out.c = h < 0.75 ? W[0] : W[1];
                return;
            }
            out.c = cord ? I[2] : x < -1 ? W[3] : x < 1.5 ? W[2] : W[1];
        });
    });
}
void candle(Paint& p, double t, const PixelFxSettings& o) {
    std::array<uint32_t, 9> H{};
    std::reverse_copy(o.palette.begin(), o.palette.end(), H.begin());
    const auto&       X  = o.fire.wax;
    const auto&       B  = o.fire.brass;
    const auto&       RT = o.fire.root;
    const uint32_t    S  = o.seed;
    const fire::Noise NL{4, t};
    const auto        N  = [&](double rate, uint32_t seed, double offset) { return NL(rate * 0.35, seed, offset); };
    const double      fl = 0.86 + 0.08 * NL(2, S + 1) + 0.06 * NL(7, S + 2),
                      stretch = std::max(0.0, NL(0.75, S + 3) - 0.3) * 0.6;
    const double      h       = o.fire.height * (1 + stretch) * (0.94 + 0.06 * fl),
                      sway    = NL(0.5, S + 4) * 1.6 + NL(1.5, S + 5) * 0.5;
    const uint32_t    glow    = mix(mix(H[3], H[4], 0.5), WHITE, 0.3);
    const double      cy      = -19 - h * 0.4;
    fire::floorLight(p, 0, 1, 26, 5, glow, 0.3 * fl);
    p.glow(sway * 0.3, cy, 38 * fl, glow, 0.42 * fl);
    p.glowRing(sway * 0.3, cy, 10, 4, H[2], 0.12 * fl);
    p.ellipse(0, 1, 10, 2.6, [&](double d, double a) {
        return d > 0.82             ? B[0]
               : std::sin(a) < -0.2 ? (d > 0.6 ? mix(B[3], H[3], 0.3 * fl) : B[2])
               : d > 0.55           ? B[1]
                                    : B[2];
    });
    p.rect(-8, 2, 17, 1, B[1]);
    p.rect(-7, 3, 15, 1, B[0]);
    const std::array<uint32_t, 9> cols{X[1], X[4], X[5], X[4], X[4], X[3], X[3], X[2], X[0]};
    for (int i = 0; i < 9; ++i) {
        p.rect(i - 4, -15, 1, 14, cols[size_t(i)]);
        if (i == 2) p.rect(i - 4, -8, 1, 7, X[4]);
    }
    p.rect(-3, -1, 7, 1, X[2]);
    p.px(-4, -1, X[0]);
    p.px(4, -1, X[0]);
    p.rect(-3, -16, 7, 1, mix(X[5], H[2], 0.25 * fl));
    p.px(-4, -16, X[1]);
    p.px(4, -16, X[1]);
    p.rect(-2, -15, 5, 1, mix(X[5], H[1], 0.5 * fl));
    p.rect(-1, -15, 3, 1, mix(X[4], H[2], 0.45 * fl));
    p.rect(3, -15, 1, 8, X[4]);
    p.rect(4, -14, 1, 7, X[3]);
    p.px(5, -13, X[0]);
    p.rect(5, -12, 1, 5, X[0]);
    p.rect(3, -7, 2, 2, X[4]);
    p.px(5, -7, X[3]);
    p.px(5, -6, X[2]);
    p.px(6, -7, X[0]);
    p.px(6, -6, X[0]);
    p.rect(3, -5, 3, 1, X[0]);
    p.px(3, -14, X[5]);
    p.rect(-4, -15, 1, 4, X[4]);
    p.px(-5, -13, X[1]);
    p.px(-5, -12, X[1]);
    p.px(-4, -11, X[3]);
    const double run = std::fmod(t, 4) / 4, swell = smooth(0, 0.3, run), slide = smooth(0.3, 0.85, run),
                 set = smooth(0.85, 1, run);
    if (set < 1 && swell > 0.15) {
        const double y = jsRound(-14 + 7 * slide);
        p.px(4, y, mix(X[5], H[1], 0.4 * fl));
        if (swell > 0.6 && set < 0.5) {
            p.px(5, y, X[4]);
            p.px(5, y + 1, X[2]);
        }
    }
    p.line(0, -16, 0, -18, X[0]);
    p.px(0, -18, H[4]);
    constexpr std::array<fire::Layer, 5> calm{
        {{1, 1, 5}, {0.8, 0.88, 3}, {0.6, 0.72, 2}, {0.38, 0.52, 1}, {0.18, 0.3, 0}}};
    fire::flameTongue(p, 0, -18, h, 2.6, sway, N, S * 31, H, calm, 0.25, 0.6);
    for (int dx = -2; dx <= 2; ++dx) {
        p.dot(dx, -18, std::abs(dx) == 2 ? RT[0] : RT[1], 1 - std::abs(dx) * 0.2);
        if (std::abs(dx) <= 1) p.dot(dx, -17, RT[0], 0.7);
    }
    p.px(0, -19, RT[2]);
    fire::looped(
        t, 4, 6, S + 300,
        [](Rng& r, int) {
            fire::LoopParticle q;
            q.vx   = r.range(-1.5, 1.5);
            q.vy   = -r.range(8, 12);
            q.life = r.range(1.4, 2.2);
            q.size = r.range(0.8, 1.2);
            return q;
        },
        [&](const fire::LoopParticle& q, double age, int) {
            const auto m = fire::drift(sway, -19 - h, q.vx, q.vy, age, 1, -6, 1, 0.8);
            fire::smokePuff(p, m.x + NL.at(t - age, 0.6, S + 60) * (1 + age * 2), m.y, 1 - age / q.life, q.size, 0,
                            false, 0.6);
        });
    fire::looped(
        t, 4, 10, S + 400,
        [](Rng& r, int) {
            fire::LoopParticle q;
            q.x    = r.range(-20, 20);
            q.y    = r.range(-44, -8);
            q.vx   = r.range(-3, 3);
            q.vy   = r.range(-4, 2);
            q.life = r.range(2.5, 4);
            q.a    = r.range(0, 6.3);
            return q;
        },
        [&](const fire::LoopParticle& q, double age, int) {
            const double x = q.x + q.vx * age + std::sin(age * 1.3 + q.a) * 2, y = q.y + q.vy * age,
                         d = jsHypot2(x, y - cy);
            const double k = std::min({1.0, age * 2, (q.life - age) * 2}) * clamp(1.2 - d / 26);
            if (k > 0.15) {
                p.dot(x, y, k > 0.6 ? H[1] : H[3], k);
                p.glow(x, y, 3, H[2], 0.2 * k);
            }
        });
    p.glow(sway * 0.5, -19 - h * 0.3, 10, H[1], 0.35 * fl);
}
}  // namespace eve::stylize::pixelfx

namespace eve::stylize::pixelfx {

const Descriptor& torchDescriptor() {
    static const std::array<Option, 3> options{{{"height", 1, 100, false, 0, &PixelFxFireSettings::height},
                                                {"wood", 1, 64, false, 6, &PixelFxFireSettings::wood},
                                                {"iron", 1, 64, false, 5, &PixelFxFireSettings::iron}}};
    static const Descriptor            descriptor{{PixelFxKind::Torch, "fire/torch", 72, 120, 36, 94, 2, true},
                                                  9,
                                                  [](PixelFxSettings& s) {
                                           fireDefaults(s, PixelFxKind::Torch);
                                           s.fire.height = 20;
                                                  },
                                                  &torch,
                                                  options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx

namespace eve::stylize::pixelfx {

const Descriptor& candleDescriptor() {
    static const std::array<Option, 4> options{{{"height", 1, 100, false, 0, &PixelFxFireSettings::height},
                                                {"root", 1, 64, false, 3, &PixelFxFireSettings::root},
                                                {"wax", 1, 64, false, 6, &PixelFxFireSettings::wax},
                                                {"brass", 1, 64, false, 5, &PixelFxFireSettings::brass}}};
    static const Descriptor            descriptor{{PixelFxKind::Candle, "fire/candle", 56, 76, 28, 64, 4, true},
                                                  9,
                                                  [](PixelFxSettings& s) {
                                           fireDefaults(s, PixelFxKind::Candle);
                                           s.fire.height = 12;
                                                  },
                                                  &candle,
                                                  options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
