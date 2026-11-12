// eve-hot-path
// Native adaptation of pixelfx impacts/hit-spark.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
struct Ray {
    double angle, length, width;
};
void ray(Paint& p, double y, double angle, double length, double width, std::span<const uint32_t> C, double from = 0) {
    const double c = std::cos(angle), s = std::sin(angle);
    for (double d = from * length; d <= length; d += .5) {
        const double u = d / length, hw = width * std::pow(1 - u, 1.6);
        p.dot(c * d, y + s * d, u < .3 ? WHITE : u < .62 ? C[5] : u < .88 ? C[4] : C[3]);
        if (hw > .45)
            for (double o = .5; o <= hw; o += .5)
                for (double e : {-o, o}) p.dot(c * d - s * e, y + s * d + c * e, u < .3 && o < 1 ? C[5] : C[4]);
    }
}
void diamond(Paint& p, double y, int radius, std::span<const uint32_t> C) {
    for (int dy = -radius - 1; dy <= radius + 1; ++dy)
        for (int dx = -radius - 1; dx <= radius + 1; ++dx) {
            const int m = std::abs(dx) + std::abs(dy);
            if (m <= radius)
                p.dot(dx, y + dy, WHITE);
            else if (m == radius + 1)
                p.dot(dx, y + dy, C[3]);
        }
}
struct HitSpark final : Kernel {
    const std::vector<uint32_t> C;
    const double                Y, aim;
    Rng                         rng;
    Particles                   ps;
    Lights                      lights;
    std::array<Ray, 5>          rays;
    double                      t = 0;
    explicit HitSpark(const PixelFxSettings& o) : C(o.palette), Y(-o.impact.height), aim(o.impact.aim), rng(o.seed) {
        ps.sparks         = {C[6], C[5], C[4], C[2]};
        const double tilt = rng.range(-.08, .08);
        rays              = {{{aim + tilt, rng.range(33, 37), 2.2},
                              {aim + PI + tilt, rng.range(8, 10), 2.2},
                              {aim - PI / 2 + tilt, rng.range(15, 18), 2.6},
                              {aim + PI / 2 + tilt, rng.range(10, 12), 2.3},
                              {aim - PI / 4 + tilt, rng.range(5, 7), 1}}};
        lights.add(0, Y, 38, C[4], .35, .18);
        lights.add(std::cos(aim) * 10, Y + std::sin(aim) * 10, 22, C[5], .45, .1);
        lights.add(0, -2, 26, C[3], .22, .22);
        for (int i = 0; i < 9; ++i) {
            const double a = aim + rng.range(-.6, .6) - .15, v = rng.range(170, 360), life = rng.range(.25, .45),
                         delay = i > 6 ? rng.range(.01, .03) : 0;
            ps.spawn(rng, ParticleKind::Spark, 1, Y, std::cos(a) * v, std::sin(a) * v - 40, life, WHITE, 1, delay);
        }
        const std::array<uint32_t, 4> chips{C[5], C[4], C[4], C[3]};
        for (int i = 0; i < 6; ++i) {
            const double a = aim + rng.range(-1.1, .5) - .3, v = rng.range(70, 170),
                         vy = std::sin(a) * v - rng.range(30, 80), life = rng.range(.35, .45);
            const auto   color = rng.pick<uint32_t>(chips);
            ps.spawn(rng, ParticleKind::Grit, 1, Y, std::cos(a) * v, vy, life, color, 1);
        }
    }
    void update(double dt) override {
        t += dt;
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        ps.draw(p);
        if (t < .025) {
            for (const auto& r : rays) ray(p, Y, r.angle, r.length, r.width, C);
            diamond(p, Y, 4, C);
        } else if (t < .058) {
            for (const auto& r : rays) ray(p, Y, r.angle, r.length * 1.2, r.width * .55, C, .12);
            diamond(p, Y, 3, C);
        } else if (t < .092) {
            const std::array<uint32_t, 6> cooling{C[0], C[1], C[2], C[3], C[3], C[4]};
            ray(p, Y, rays[0].angle, rays[0].length * 1.3, .6, cooling, .4);
        }
        const double pop = std::exp(-t / .045);
        if (pop > .02) {
            p.glow(0, Y, 22, C[4], .3 * pop);
            p.glow(0, Y, 7, WHITE, .15 * pop);
            p.glowLine(0, Y, std::cos(aim) * 26, Y + std::sin(aim) * 26, 4, C[5], .25 * pop);
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& hitSparkDescriptor() {
    static const std::array<Option, 2> options{{{"height", 0, 256, false, 0, &PixelFxImpactSettings::height},
                                                {"aim", -TAU, TAU, false, 0, &PixelFxImpactSettings::aim}}};
    static const Descriptor            d{
        {PixelFxKind::HitSpark, "impacts/hit-spark", 136, 80, 44, 64, .5, false},
        7,
        [](PixelFxSettings& s) {
            s.seed          = 4;
            s.impact.height = 20;
            s.impact.aim    = .1;
            s.palette       = {hex(0x4a1712), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                               hex(0xffe26b), hex(0xfff3a6), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<HitSpark>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
