// eve-hot-path
// Native adaptation of pixelfx arcane/arcane-beam.js, MIT.
// Beam recipe: Sprite Fusion energy, Copyright (c) 2026 Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Arcane.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 1.2, HX = 15, HY = -24, L = 168, W = 6, EX = HX + L;
struct ArcaneBeam final : AnalyticKernel {
    void drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&                   C  = o.palette;
        const double                  ph = t / D;
        const uint32_t                S  = o.seed;
        const std::array<uint32_t, 6> LV{WHITE, C[6], C[5], C[4], C[3], C[2]};
        for (double d = 0; d <= L; d += .5) {
            const double x = HX + d, w = W * (1 + .12 * std::sin(d * .35 - ph * TAU * 8)) * std::min(1., (d + 3) / 7);
            const int    pulse = fract(d * .025 - ph * 4) < .07 ? 1 : 0;
            for (double q0 = -w * 1.3; q0 <= w * 1.3; q0 += .5) {
                const double q = std::abs(q0) / w, y = HY + q0;
                int          lv = q < .2 ? 0 : q < .4 ? 1 : q < .62 ? 2 : q < .85 ? 3 : q < 1 ? 4 : 5;
                lv              = std::max(0, lv - pulse);
                if (lv == 5 && arcane::bayer(x, y) > (1.3 - q) / .3) continue;
                p.px(x, y, LV[size_t(lv)]);
            }
            for (int s = 0; s < 3; ++s) {
                const double a = d * .17 - ph * TAU * 6 + (s * TAU) / 3, off = std::sin(a) * w * 1.05;
                if (std::cos(a) > -.15) p.dot(x, HY + off, std::cos(a) > .55 ? WHITE : C[6]);
            }
            if ((int(jsRound(d * 2)) & 11) == 0) p.glow(x, HY, w * 2.2 + 4, C[5], .2);
        }
        const double fr = 4.2 + std::sin(ph * TAU * 10) * .6;
        for (int i = 0; i < 4; ++i) {
            const double a = ph * TAU * 2.5 + (i * TAU) / 4, l = 7 + 3 * std::sin(ph * TAU * 6 + i);
            p.lineFn(
                HX, HY, HX + std::cos(a) * l, HY + std::sin(a) * l, [&](double u) { return u < .5 ? WHITE : C[6]; },
                [](double u) { return u < .6 ? 1 : 1.6 - u; });
        }
        p.circle(HX, HY, fr + 1.5, C[4]);
        p.circle(HX, HY, fr, C[6]);
        p.circle(HX, HY, fr * .6, WHITE);
        p.glow(HX, HY, 32, C[5], .8);
        cycle(t, D, 10, S * 3u + 1u, .18, [&](double age, double life, Rng& r, int) {
            const double a = r.range(0, TAU), dd = 16 * (1 - age / life) + 3, x = HX + std::cos(a) * dd,
                         y = HY + std::sin(a) * dd;
            p.dot(x, y, age / life > .6 ? WHITE : C[6]);
        });
        const double ir = 5 + std::sin(ph * TAU * 12) * .8;
        for (int i = 0; i < 7; ++i) {
            const double a = PI + (i - 3) * .36, l = 8 + 8 * std::abs(loopNoise(ph, 4, S * 7u + uint32_t(i)));
            p.lineFn(
                EX, HY, EX + std::cos(a) * l, HY + std::sin(a) * l,
                [&](double u) { return u < .4  ? WHITE
                                       : i % 2 ? C[5]
                                               : C[6]; },
                [](double u) { return u < .6 ? 1 : 1.6 - u; });
        }
        for (int k = 0; k < 3; ++k) {
            const double q = fract(ph * 3 + double(k) / 3);
            p.ring(EX, HY, 3 + 15 * ease::outCubic(q), std::max(1., 2 * (1 - q)), q < .25 ? WHITE : C[5], 1.1 - q);
        }
        p.circle(EX, HY, ir + 1.5, C[4]);
        p.circle(EX, HY, ir, C[6]);
        p.circle(EX, HY, ir * .55, WHITE);
        p.glow(EX, HY, 40, C[5], .9);
        p.glow(EX, HY, 12, WHITE, .6);
        cycle(t, D, 220, S * 3u + 2u, .18, .5, [&](double age, double life, Rng& r, int) {
            const double a = PI * 1.5 + r.range(-1.7, .45), sp = r.range(70, 260), vx = std::cos(a) * sp,
                         vy     = std::sin(a) * sp - 30;
            const auto [x, y]   = fly(EX, HY, vx, vy, age, 480, 2.2);
            const auto [ux, uy] = flyV(vx, vy, age, 480, 2.2);
            const double k      = 1 - age / life;
            if (y > -1) return;
            const uint32_t head = k > .6 ? WHITE : k > .3 ? C[6] : C[5];
            const double   raw = jsHypot2(ux, uy), sp2 = raw == 0 ? 1 : raw, len = std::min(8., 1 + sp2 * .022);
            p.lineFn(
                x - (ux / sp2) * len, y - (uy / sp2) * len, x, y, [&](double u) { return u > .5 ? head : C[4]; }, 1.);
            p.glow(x, y, 3, head, .45 * k);
        });
        cycle(t, D, 6, S * 3u + 3u, .3, [&](double age, double life, Rng& r, int) {
            const double k = std::sin((age / life) * PI), x = EX + r.range(-12, 4), y = HY + r.range(-12, 12);
            p.sparkle(x, y, 1 + 2.5 * k, C[6], C[5], .4 * k);
        });
        cycle(t, D, 7, S * 3u + 5u, .8, 1.2, [&](double age, double life, Rng& r, int) {
            const double q = age / life, x = EX + r.range(-8, 4) - q * 6, y = HY - 4 - q * r.range(14, 24),
                         rad = r.range(3, 5) * (.7 + .8 * q);
            p.disc(x, y, rad, C[4], [&](double dd) { return .28 * std::sin(q * PI) * (1 - dd * dd); });
        });
        cycle(t, D, 16, S * 3u + 4u, .3, .6, [&](double age, double life, Rng& r, int) {
            const double q = age / life, x = HX + r.range(10, L - 10) + q * 20,
                         y = HY + r.sign() * (W + 1 + q * r.range(4, 10));
            arcane::mote(p, x, y, 40, (y - HY) * 1.5, r.isChance(.5) ? C[6] : C[5], .6, 1 - q, .3);
        });
        p.glow(EX, -1, 30, C[5], .25);
        p.glow(HX + L / 2, -1, 60, C[4], .12);
    }
};
}  // namespace
const Descriptor& arcaneBeamDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::ArcaneBeam, "arcane/arcane-beam", 264, 100, 26, 84, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 10;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                              hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<ArcaneBeam>(); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
