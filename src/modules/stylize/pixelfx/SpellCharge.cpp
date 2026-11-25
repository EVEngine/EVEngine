// eve-hot-path
// Native adaptation of pixelfx arcane/spell-charge.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Arcane.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 1.6, HX = 15, HY = -22, RT = .5;
double           sinceLast(double t) {
    double since = D;
    for (int k = 0; k < 4; ++k) since = std::min(since, wrap(t - (k * D / 4 + RT), D));
    return since;
}
void orb(Paint& p, const std::vector<uint32_t>& C, double x, double y, double r, double t, double ph) {
    for (int i = 0; i < 8; ++i) {
        const double a  = (double(i) / 8) * TAU + ph * (TAU / 8) * 3,
                     l  = (1.5 + 4.5 * std::abs(loopNoise(ph, 3, uint32_t(i * 7 + 1)))) * (i % 2 ? .7 : 1),
                     x0 = x + std::cos(a) * (r + 1.2), y0 = y + std::sin(a) * (r + 1.2);
        p.lineFn(
            x0, y0, x0 + std::cos(a) * l, y0 + std::sin(a) * l,
            [&](double u) { return u < .4  ? C[6]
                                   : i % 2 ? C[4]
                                           : C[5]; },
            [](double u) { return u < .6 ? 1 : 1.6 - u; });
    }
    p.circle(x, y, r + 1.3, C[4]);
    p.circle(x, y, r + .3, C[5]);
    p.circle(x - r * .12, y - r * .12, r * .8, C[6]);
    p.circle(x, y, r * .5, WHITE);
    Rng R(hash({jsUint(std::floor(t * 15)), 77}));
    for (int i = 0; i < 3; ++i) {
        const double a = R.range(0, TAU), x0 = x + std::cos(a) * r * .4, y0 = y + std::sin(a) * r * .4,
                     x1    = x + std::cos(a + R.range(-.6, .6)) * (r + .6),
                     y1    = y + std::sin(a + R.range(-.6, .6)) * (r + .6);
        const auto   paths = arcane::arc(R, x0, y0, x1, y1, .45, 0);
        p.polyline(paths[0].points, [&](double u) { return u < .6 ? WHITE : C[6]; });
    }
    p.px(x - r * .5, y - r * .62, WHITE);
}
void ringAt(Paint& p, const std::vector<uint32_t>& C, double x, double y, double rr, double sy, double rot, double q) {
    const int    n = int(std::ceil(rr * TAU * 1.4));
    const double c = std::cos(rot), s = std::sin(rot), a = std::min(1., q * 3);
    const auto   color = q > .85 ? WHITE : q > .55 ? C[6] : C[5];
    for (int j = 0; j < n; ++j) {
        const double g = (double(j) / n) * TAU, lx = std::cos(g) * rr, ly = std::sin(g) * rr * sy,
                     px = x + lx * c - ly * s, py = y + lx * s + ly * c;
        if (py > -.5) continue;
        p.dot(px, py, color, a);
        if (j % 10 == 0) p.glow(px, py, 5, C[5], .12 * a);
    }
}
struct SpellCharge final : AnalyticKernel {
    void drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C     = o.palette;
        const double ph    = t / D;
        const auto   S     = o.seed;
        const int    f     = int(std::floor(t * 30));
        const double since = sinceLast(t),
                     swell = std::exp(-std::pow(since / .07, 2)) + .5 * std::exp(-std::pow(since / .18, 2)),
                     r = 6.1 + .5 * std::sin(ph * TAU * 4) + 1.4 * swell, x = HX + (swell > .4 ? (f % 2 ? 1 : -1) : 0),
                     y = HY;
        cycle(t, D, 110, S * 3u + 1u, .2, .34, [&](double age, double life, Rng& R, int) {
            const double a0 = R.range(2.8, 5.75), d0 = R.range(20, 52), spin = R.range(.6, 1.3);
            const auto   pos = [&](double q) {
                const double a = a0 + spin * q * q, d = d0 * (1 - q * q) + r;
                return Point{HX + std::cos(a) * d, HY + std::sin(a) * d * .92};
            };
            const double q = age / life;
            const auto   a = pos(q), b = pos(std::min(1., q + .02));
            const auto   color = R.isChance(.55) ? C[6] : C[5];
            arcane::streak(p, a.x, a.y, (b.x - a.x) / (.02 * life), (b.y - a.y) / (.02 * life), color,
                           std::min(1., q * 4) * (1 - smooth(.9, 1, q)), 7, .4 * (1 - smooth(.6, .9, q)));
        });
        cycle(t, D, 56, S * 3u + 2u, .4, .7, [&](double age, double life, Rng& R, int) {
            const double a0 = R.range(2.6, 5.2), d0 = R.range(12, 32), spin = R.range(1, 2.2), size = R.range(.6, 1);
            const auto   pos = [&](double q) {
                const double a = a0 + spin * q, d = d0 * (1 - ease::inQuad(q)) + r;
                return Point{HX + std::cos(a) * d, HY + std::sin(a) * d};
            };
            const double                  q = age / life;
            const auto                    a = pos(q), b = pos(std::min(1., q + .02));
            const std::array<uint32_t, 4> colors{C[5], C[6], C[6], WHITE};
            const auto                    color = R.pick<uint32_t>(colors);
            arcane::mote(p, a.x, a.y, (b.x - a.x) / (.02 * life), (b.y - a.y) / (.02 * life), color, size,
                         std::min(1., q * 3) * (q > .9 ? (1 - q) / .1 : 1), .3 * (1 - smooth(.55, .85, q)));
        });
        constexpr std::array<Point, 4> shapes{{{1, 0}, {.38, .55}, {1, 0}, {.42, -.7}}};
        for (int k = 0; k < 4; ++k)
            for (double off : {-D, 0.}) {
                const double q = (t - (k * D) / 4 - off) / RT;
                if (q < 0 || q >= 1) continue;
                ringAt(p, C, x, y, r + 1.5 + 22 * (1 - ease::inQuad(q)), shapes[size_t(k)].x, shapes[size_t(k)].y, q);
            }
        orb(p, C, x, y, r, t, ph);
        const int    w = int(std::floor(double(f) / 2));
        Rng          Rw(hash({S, uint32_t(w), 3}));
        const int    n     = (Rw.next() < .6 ? 1 : 0) + (swell > .3 ? 1 : 0);
        const double fresh = f % 2 == 0 ? .8 : .4;
        for (int i = 0; i < n; ++i) {
            const double a = Rw.range(0, TAU), L = Rw.range(5, 10) * (1 + swell * .4), a1 = a + Rw.range(-.5, .5),
                         x0 = x + std::cos(a) * (r + .5), y0 = y + std::sin(a) * (r + .5), x1 = x0 + std::cos(a1) * L,
                         y1    = std::min(-2., y0 + std::sin(a1) * L);
            const int    forks = Rw.next() < .5 ? 1 : 0;
            const auto   paths = arcane::arc(Rw, x0, y0, x1, y1, .35, forks);
            arcane::drawArc(p, paths, {WHITE, C[5], C[3], C[5]}, fresh, .3);
        }
        cycle(t, D, 7, S * 3u + 4u, .22, [&](double age, double life, Rng& R, int) {
            const double a = R.range(0, TAU), d = R.range(r + 5, r + 15), k = std::sin((age / life) * PI);
            p.sparkle(x + std::cos(a) * d, y + std::sin(a) * d * .9, 1 + 2.5 * k, C[6], C[4], .4 * k);
        });
        if (since < .07) p.sparkle(x, y, 4 + 6 * (1 - since / .07), C[6], C[5], .5);
        const double pulse = .85 + .15 * std::sin(ph * TAU * 4) + .2 * swell;
        p.glow(x, y, 46, C[4], .26 * pulse);
        p.glowRing(x, y, r + 3, 8, C[5], .3 * pulse);
        p.glow(x, y, r, C[6], .12);
        p.glow(HX, -1, 22, C[5], .16 * pulse);
    }
};
}  // namespace
const Descriptor& spellChargeDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{{PixelFxKind::SpellCharge, "arcane/spell-charge", 120, 86, 44, 80, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed         = 5;
                                  s.actor.source = PixelFxActorSource::Dummy;
                                  s.palette      = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                                                    hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<SpellCharge>();
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
