// eve-hot-path
// Adapted from pixelfx fire/brazier.js. Copyright (c) 2026 Patch and Hugo Duprez.
// MIT; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Fire.h"
#include "stylize/pixelfx/PixelFxInternal.h"

namespace eve::stylize::pixelfx {
void brazier(Paint& p, double t, const PixelFxSettings& o) {
    std::array<uint32_t, 9> H{};
    std::reverse_copy(o.palette.begin(), o.palette.end(), H.begin());
    const auto&                   I = o.fire.iron;
    const uint32_t                S = o.seed;
    const fire::Noise             N{3, t};
    const double                  fl   = 0.74 + 0.16 * N(7.3, S + 1) + 0.1 * N(19, S + 2);
    const uint32_t                glow = mix(mix(H[3], H[4], 0.5), WHITE, 0.25);
    const std::array<uint32_t, 4> sparks{H[1], H[2], H[4], H[5]}, embers{H[1], H[2], H[4], H[6]};
    fire::floorLight(p, 0, 0.5, 40, 7, glow, 0.32 * fl);
    const auto leg = [&](double x0, double y0, double x1, double y1) {
        p.line(x0, y0, x1, y1, I[0]);
        p.line(x0 + 1, y0, x1 + 1, y1, I[2]);
        p.line(x0 + 2, y0, x1 + 2, y1, I[0]);
    };
    p.ellipse(0, -9, 9, 1.8, [&](double d) { return d > 0.6 ? I[2] : 0u; });
    leg(-6, -17, -12, -1);
    leg(4, -17, 10, -1);
    p.rect(-14, -1, 5, 1, I[2]);
    p.rect(-15, 0, 6, 1, I[0]);
    p.px(-15, -1, I[3]);
    p.rect(9, -1, 5, 1, I[2]);
    p.rect(9, 0, 6, 1, I[0]);
    p.px(13, -1, I[3]);
    p.rect(-1, -16, 3, 15, I[0]);
    p.rect(0, -16, 1, 15, mix(I[3], H[4], 0.15 * fl));
    p.rect(-2, -1, 5, 1, I[2]);
    p.rect(-2, 0, 5, 1, I[0]);
    p.ellipse(0, -9, 9, 1.8,
              [&](double d, double a) { return std::sin(a) > 0 && d > 0.55 ? (d > 0.85 ? I[0] : I[3]) : 0u; });
    p.ellipse(0, -25, 15, 3, [&](double d, double a) {
        return std::sin(a) < 0 && d > 0.7 ? (d > 0.88 ? I[0] : mix(I[3], H[4], 0.3 * fl)) : 0u;
    });
    constexpr std::array<fire::Tongue, 11> tongues{{{-7, -1.5, 0.5, 2, true},
                                                    {7.2, -1.5, 0.55, 2, true},
                                                    {-3, -2.5, 0.84, 2.2, true},
                                                    {3.3, -2.5, 0.86, 2.2, true},
                                                    {-11.5, 0.5, 0.2, 1.5, false},
                                                    {11.5, 0.5, 0.22, 1.5, false},
                                                    {-8.3, 0, 0.34, 2, false},
                                                    {8.5, 0, 0.38, 2, false},
                                                    {-4.7, -0.5, 0.6, 2.8, false},
                                                    {4.9, -0.5, 0.64, 2.8, false},
                                                    {0, -1, 1, 3.6, false}}};
    fire::BurnOptions                      options;
    options.duration   = 3;
    options.seed       = S;
    options.y          = -25;
    options.height     = o.fire.height;
    options.lean       = 0.1;
    options.spread     = 7;
    options.puffs      = 8;
    options.licks      = 4;
    options.smoke      = 14;
    options.smokeSize  = 3;
    options.smokeLife  = {1.6, 2.4};
    options.embers     = 10;
    options.glowRadius = 58;
    options.glow       = 1.4;
    options.wind       = 3;
    fire::burn(p, t, tongues, H, options, [&](double) {
        p.shader(-15, -31, 15, -24, [&](double x, double y, ShadeOutput& out) {
            const double top = -26 - 4 * std::max(0.0, 1 - std::pow(x / 14, 2));
            if (y < top || y > -24.5 || std::abs(x) > 14.5) return;
            const double lump =
                rand({jsUint(std::floor((x + std::floor(y) * 1.5) / 3)), jsUint(std::floor(y / 2)), S + 20});
            const bool edge = y < top + 1;
            if (rand({jsUint(std::floor(x)), jsUint(std::floor(y)), S + 21}) < 0.16 || (edge && lump < 0.3)) {
                out.c = I[0];
                return;
            }
            const double g = (0.5 + 0.5 * std::sin(t / 3 * PI * 4 + lump * 40)) * (edge ? 0.7 : 1);
            out.k          = 0.3 * g;
            out.c          = g > 0.72 ? H[2] : g > 0.5 ? H[3] : g > 0.3 ? H[4] : g > 0.15 ? H[5] : H[6];
        });
        p.shader(-16, -26, 16, -16, [&](double x, double y, ShadeOutput& out) {
            const double v  = (y + 25) / 9,
                         hw = 15 * std::sqrt(std::max(0.0, 1 - v * v * 0.9)) - (v > 0.5 ? (v - 0.5) * 6 : 0);
            if (std::abs(x) > hw + 0.5 || y < -25) return;
            out.c = std::abs(x) > hw - 0.5 || v > 0.92 ? I[0]
                    : y < -24                          ? mix(I[3], H[4], 0.5 * fl)
                    : y < -23                          ? I[2]
                    : y < -22                          ? (int(jsRound(x)) % 4 == 0 ? mix(I[4], H[4], 0.25 * fl) : I[0])
                    : x < -6.75                        ? I[2]
                    : x < 3.75                         ? I[1]
                                                       : I[0];
        });
    });
    struct Particle {
        double x, y, vx, vy, life, a, w, m;
        bool   big;
    };
    fire::looped(
        t, 3, 60, S + 600,
        [](Rng& r, int) {
            Particle     q;
            const double side = r.range(-1, 1);
            q.x               = side * 8;
            q.y               = -25 - r.range(4, 18);
            q.vx              = side * r.range(10, 40);
            q.vy              = -r.range(70, 150);
            q.life            = r.range(0.7, 1.7);
            q.a               = r.range(0, 6.3);
            q.w               = r.range(1.8, 3.2);
            q.m               = r.range(3, 8);
            q.big             = r.isChance(0.12);
            return q;
        },
        [&](const Particle& q, double age, int) {
            const auto   m   = fire::drift(q.x, q.y, q.vx, q.vy, age, 0, -22, 1.9, 1.9);
            const double k   = 1 - age / q.life;
            const double wob = q.m * (std::sin(age * q.w + q.a) - std::sin(q.a)) * std::min(1.0, age * 1.5),
                         wv  = q.m * q.w * std::cos(age * q.w + q.a) * std::min(1.0, age * 1.5);
            if (jsHypot2(m.vx + wv, m.vy) > 40) {
                fire::spark(p, m.x + wob, m.y, m.vx + wv, m.vy, k, sparks);
            } else
                fire::ember(p, m.x + wob, m.y, k, q.big ? 2 : 1, embers, age * 40 + q.a);
        });
}
}  // namespace eve::stylize::pixelfx

namespace eve::stylize::pixelfx {

const Descriptor& brazierDescriptor() {
    static const std::array<Option, 2> options{{{"height", 1, 100, false, 0, &PixelFxFireSettings::height},
                                                {"iron", 1, 64, false, 5, &PixelFxFireSettings::iron}}};
    static const Descriptor            descriptor{{PixelFxKind::Brazier, "fire/brazier", 112, 148, 56, 136, 3, true},
                                                  9,
                                                  [](PixelFxSettings& s) {
                                           fireDefaults(s, PixelFxKind::Brazier);
                                           s.fire.height = 32;
                                                  },
                                                  &brazier,
                                                  options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
