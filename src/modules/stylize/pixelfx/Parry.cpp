// eve-hot-path
// Native adaptation of pixelfx impacts/parry.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Impact.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct Parry final : Kernel {
    const std::vector<uint32_t>        C;
    const double                       Y;
    Rng                                rng;
    Particles                          ps;
    Lights                             lights;
    std::array<impact::Spike, 8>       spikes;
    std::array<impact::RingSegment, 6> segments;
    std::array<double, 2>              edges;
    double                             t = 0;
    explicit Parry(const PixelFxSettings& o) : C(o.palette), Y(-o.impact.height), rng(o.seed) {
        ps.sparks = {C[4], C[4], C[3], C[2]};
        for (int i = 0; i < 8; ++i) {
            const double a = (i * PI) / 4 + rng.range(-.04, .04), len = i % 2        ? rng.range(6, 8)
                                                                        : i % 4 == 0 ? rng.range(19, 22)
                                                                                     : rng.range(14, 17);
            spikes[size_t(i)] = {a, len, i % 2 ? 1.2 : 2.2};
        }
        for (int i = 0; i < 6; ++i) {
            const double a = (double(i) / 6) * TAU + rng.range(-.1, .1), gap = rng.range(.55, .8),
                         drift  = rng.range(.1, .35);
            segments[size_t(i)] = {a, gap, drift, 1};
        }
        edges = {-.62 + rng.range(-.08, .08), PI + .62 + rng.range(-.08, .08)};
        lights.add(0, Y, 60, C[4], .65, .4);
        lights.add(0, Y, 22, WHITE, .8, .08);
        for (int i = 0; i < 24; ++i) {
            const double side = i % 2 ? 1 : -1, a = -PI / 2 + side * rng.range(.3, 1.3), v = rng.range(130, 290),
                         x = side * rng.range(1, 3), life = rng.range(.45, .85),
                         delay = i > 17 ? rng.range(.02, .07) : 0;
            ps.spawn(rng, ParticleKind::Spark, x, Y, std::cos(a) * v, std::sin(a) * v, life, WHITE, 1, delay);
        }
        const std::array<uint32_t, 2> moteColors{C[5], C[6]};
        for (int i = 0; i < 8; ++i) {
            const double a = rng.range(0, TAU), v = rng.range(40, 110), life = rng.range(.3, .55);
            const auto   color = rng.pick<uint32_t>(moteColors);
            const double delay = rng.range(0, .04);
            ps.spawn(rng, ParticleKind::Mote, 0, Y, std::cos(a) * v, std::sin(a) * v - 20, life, color, .6, delay);
        }
        for (int i = 0; i < 3; ++i) {
            const double a = rng.range(0, TAU), d = rng.range(12, 26), life = rng.range(.22, .34),
                         size = rng.range(1.6, 2.6), delay = rng.range(.1, .3);
            ps.spawn(rng, ParticleKind::Star, std::cos(a) * d, Y + std::sin(a) * d, 0, -3, life, WHITE, size, delay);
        }
    }
    void update(double dt) override {
        t += dt;
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const double u = (t - .03) / .36;
        if (u >= 0 && u < 1) {
            const double r = 8 + 24 * ease::outCubic(u), brk = smooth(.15, .6, u);
            const size_t i = u < .15 ? 6 : u < .35 ? 5 : u < .6 ? 4 : 3;
            impact::brokenRing(p, 0, Y, r, std::max(1., 2.6 * std::pow(1 - u, 1.2)), segments, brk, C[i],
                               C[std::max(size_t(2), i - 2)], u < .6 ? 1 : (1 - u) / .4, 1, 0);
            if (u < .5) p.glowRing(0, Y, r, 5, C[4], .3 * (1 - u / .5));
        }
        ps.draw(p);
        if (t < .058)
            for (double a : edges) {
                const double ca = std::cos(a), sa = std::sin(a), L = t < .025 ? 26 : 32, k = t < .025 ? 1 : .6;
                for (double d = -L; d <= L; d += .5) {
                    const double q = std::abs(d) / L;
                    p.dot(ca * d, Y + sa * d, q < .4 ? WHITE : q < .75 ? C[5] : C[4], q < .6 ? 1 : k * (1.3 - q));
                }
                p.glowLine(-ca * L, Y - sa * L, ca * L, Y + sa * L, 4, C[5], .35 * k);
            }
        if (t < .025)
            impact::star(p, 0, Y, spikes, 1, 1, {C[4], WHITE, WHITE, WHITE}, 1, 2.6);
        else if (t < .058)
            impact::star(p, 0, Y, spikes, 1.25, .65, {C[2], C[5], C[6], WHITE}, 1, 2.6);
        else if (t < .092)
            impact::star(p, 0, Y, spikes, 1.4, .3, {C[2], C[4], C[5], C[6]}, 1, 2);
        const double pop = std::exp(-t / .05);
        if (pop > .02) {
            p.glow(0, Y, 30, C[4], .5 * pop);
            p.glow(0, Y, 10, WHITE, .4 * pop);
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& parryDescriptor() {
    static const std::array<Option, 1> options{{{"height", 0, 256, false, 0, &PixelFxImpactSettings::height}}};
    static const Descriptor            d{
        {PixelFxKind::Parry, "impacts/parry", 200, 112, 100, 96, 1, false},
        7,
        [](PixelFxSettings& s) {
            s.seed          = 11;
            s.impact.height = 30;
            s.palette       = {hex(0x2a1a08), hex(0x6a4410), hex(0xb07a1a), hex(0xe8b030),
                               hex(0xffe066), hex(0xfff6c4), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Parry>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
