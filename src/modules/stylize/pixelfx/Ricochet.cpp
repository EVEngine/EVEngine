// eve-hot-path
// Native adaptation of pixelfx impacts/ricochet.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Impact.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double HIT = .05, SPEED = 760, IN = .32, OUT = -.21, D = 1.1;
const uint32_t   SOOT = hex(0x0d0a12);
void tracer(Paint& p, double x, double y, double angle, double length, std::span<const uint32_t> C, double k = 1) {
    const double ca = std::cos(angle), sa = std::sin(angle);
    for (double d = 0; d <= length; d += .5) {
        const double u = d / length;
        p.dot(x - ca * d, y - sa * d,
              u < .12   ? WHITE
              : u < .45 ? C[5]
              : u < .75 ? C[4]
                        : C[2],
              u < .45 ? 1 : k * (1.25 - u));
    }
    p.glow(x, y, 9, C[4], .6 * k);
}
struct Ricochet final : Kernel {
    const std::vector<uint32_t> C;
    const impact::Stone         stone;
    Rng                         rng;
    Particles                   ps;
    Lights                      lights;
    std::vector<impact::Spike>  spikes;
    double                      t   = 0;
    bool                        hit = false;
    explicit Ricochet(const PixelFxSettings& o) : C(o.palette), stone(o.impact.stone), rng(o.seed) {
        ps.sparks = {C[6], C[5], C[4], C[2]};
        impact::SpikeRecipe recipe;
        recipe.minLength = 6;
        recipe.maxLength = 9;
        recipe.minWidth  = 1.4;
        recipe.maxWidth  = 1.9;
        recipe.forward   = 2.6;
        spikes           = impact::spikes(rng, 9, OUT, recipe);
        for (auto& s : spikes) {
            if (std::sin(s.angle) > .2) s.length *= .3;
            if (std::abs(std::sin(s.angle - OUT)) > .6) s.length *= .65;
        }
    }
    void update(double dt) override {
        t += dt;
        if (!hit && t >= HIT) {
            hit = true;
            lights.add(0, -2, 44, C[4], .65, .25);
            lights.add(0, -1, 14, WHITE, .6, .06);
            for (int i = 0; i < 18; ++i) {
                const bool   low = i < 12;
                const double a   = OUT + (low ? rng.range(-.22, .12) : rng.range(-.7, .25)),
                             v = rng.range(260, 520) * (low ? 1 : .7), x = rng.range(-1, 2),
                             life = rng.range(.15, .42) * (low ? 1.3 : 1), delay = low ? rng.range(0, .05) : 0;
                ps.spawn(rng, ParticleKind::Spark, x, -1, std::cos(a) * v, std::sin(a) * v, life, WHITE, 1, delay);
            }
            const std::array<uint32_t, 3> grit{stone.light, stone.body, stone.lip};
            for (int i = 0; i < 7; ++i) {
                const double a = -PI / 2 + rng.range(-.2, .9), v = rng.range(60, 170), x = rng.range(0, 4),
                             life  = rng.range(.6, .95);
                const auto   color = rng.pick<uint32_t>(grit);
                ps.spawn(rng, ParticleKind::Grit, x, -1, std::cos(a) * v, std::sin(a) * v, life, color, i < 2 ? 2 : 1);
            }
            for (int i = 0; i < 4; ++i) {
                const double x = rng.range(0, 8), vx = rng.range(20, 70), vy = -rng.range(4, 16),
                             life = rng.range(.25, .45), mixK = rng.range(.1, .3), size = rng.range(1.2, 2);
                ps.spawn(rng, ParticleKind::Dust, x, -1, vx, vy, life, mix(stone.dust, WHITE, mixK), size);
            }
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double u = t - HIT;
        if (u >= 0) {
            const double heat = std::pow(clamp(1 - u / .85), 1.2), fade = 1 - smooth(D - .4, D - .05, t),
                         L = 12 * std::min(1., u / .03);
            for (int x = -3; x <= L; ++x) {
                const double q = (x + 3) / 15., h = heat * (1.15 - q * .6);
                p.dot(x, 0,
                      h > .8    ? WHITE
                      : h > .55 ? C[5]
                      : h > .35 ? C[4]
                      : h > .18 ? C[3]
                      : h > .08 ? C[2]
                                : SOOT,
                      fade);
                if (x > -2 && x < L - 2 && (x & 1) == 0) p.dot(x, 1, h > .4 ? C[3] : SOOT, fade * .7);
            }
            if (heat > 0) p.glowLine(-3, 0, L, 0, 5, C[3], .45 * heat);
        }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        ps.draw(p, Paint::Layer::Front);
        if (t < HIT) {
            const double d = (HIT - t) * SPEED;
            tracer(p, -std::cos(IN) * d, -std::sin(IN) * d, IN, 22, C);
        } else {
            const double u = t - HIT;
            if (u < .03)
                impact::star(p, 0, -1, spikes, 1, 1, {C[2], C[5], C[4], WHITE}, 1, 2);
            else if (u < .06)
                impact::star(p, 0, -1, spikes, 1.25, .55, {C[1], C[4], C[3], C[5]}, 1, 2);
            if (u < .2) {
                const double head = 4 + u * SPEED, k = 1 - u / .2;
                tracer(p, std::cos(OUT) * head, -1 + std::sin(OUT) * head, OUT, std::min(head - 2, 18 + u * 200), C, k);
            }
            const double g = (u - .02) / .2;
            if (g >= 0 && g < 1) {
                const double r = 5 + 12 * (1 - std::pow(1 - g, 3)), n = std::ceil(r * PI * 1.3);
                const auto   color = g < .3 ? C[5] : g < .6 ? C[4] : C[3];
                for (int j = 0; j <= n; ++j) {
                    const double a = -PI + (j / n) * PI;
                    p.dot(std::cos(a) * r, -1 + std::sin(a) * r, color, 1.15 - g);
                }
            }
            const double pop = std::exp(-u / .045);
            if (pop > .02) {
                p.glow(0, -2, 22, C[3], .75 * pop);
                p.glow(0, -1, 8, WHITE, .5 * pop);
            }
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& ricochetDescriptor() {
    static const std::array<Option, 1> options{
        {{"stone", 4278190080., 4294967295., false, 0, &PixelFxImpactSettings::stone, true}}};
    static const Descriptor d{
        {PixelFxKind::Ricochet, "impacts/ricochet", 232, 84, 64, 60, D, false},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 8;
            s.impact.stone = hex(0x5b5079);
            s.palette      = {hex(0x4a1712), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                              hex(0xffe26b), hex(0xfff3a6), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Ricochet>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
