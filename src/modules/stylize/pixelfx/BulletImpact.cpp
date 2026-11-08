// eve-hot-path
// Native adaptation of pixelfx impacts/bullet-impact.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Impact.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct Rim {
    double x, y;
    int    kind;
};
struct BulletImpact final : Kernel {
    const std::vector<uint32_t>          C;
    const impact::Stone                  stone;
    const double                         Y;
    Rng                                  rng;
    Particles                            ps;
    Lights                               lights;
    std::vector<impact::Spike>           spikes;
    std::vector<Rim>                     rim;
    std::array<std::array<double, 4>, 3> cracks;
    double                               t = 0;
    explicit BulletImpact(const PixelFxSettings& o)
        : C(o.palette), stone(o.impact.stone), Y(-o.impact.height), rng(o.seed) {
        ps.sparks             = {C[6], C[5], C[4], C[2]};
        const double        N = PI, B = PI - .08;
        impact::SpikeRecipe recipe;
        recipe.maxLength = 10.5;
        recipe.minWidth  = 1.6;
        recipe.maxWidth  = 2.1;
        recipe.back      = .4;
        spikes           = impact::spikes(rng, 9, B, recipe);
        for (auto& s : spikes)
            if (std::cos(s.angle - N) < -.2) s.length *= .3;
        for (int y = -2; y <= 2; ++y)
            for (int x = 0; x <= 3; ++x) {
                const double d = jsHypot2((x - 1) / 1.6, double(y) / 2.1) + rand({jsUint(x), jsUint(y), o.seed}) * .35;
                if (d < .75)
                    rim.push_back({double(x), double(y), 0});
                else if (d < 1.25)
                    rim.push_back({double(x), double(y), 1});
            }
        for (auto& c : cracks) {
            const double a = rng.range(-1.2, 1.2), L = rng.range(2, 4.5);
            c = {std::cos(a) * 2.2 + 1, std::sin(a) * 2.2, std::cos(a) * (2.2 + L) + 1, std::sin(a) * (2.2 + L)};
        }
        lights.add(-4, Y, 40, C[4], .6, .22);
        lights.add(-2, Y, 14, WHITE, .6, .06);
        for (int i = 0; i < 18; ++i) {
            const double a = B + rng.range(-1, 1) * .8, v = rng.range(170, 450), life = rng.range(.12, .4),
                         delay = i > 13 ? rng.range(.01, .04) : 0;
            ps.spawn(rng, ParticleKind::Spark, -1, Y, std::cos(a) * v, std::sin(a) * v - 30, life, WHITE, 1, delay);
        }
        const std::array<uint32_t, 4> chips{stone.light, stone.body, stone.body, stone.lip};
        for (int i = 0; i < 8; ++i) {
            const double a = N + rng.range(-.8, .6), v = rng.range(60, 190), y = Y + rng.range(-1, 1),
                         vy = std::sin(a) * v - rng.range(20, 70), life = rng.range(.9, 1.3);
            const auto   color = rng.pick<uint32_t>(chips);
            ps.spawn(rng, ParticleKind::Grit, -1, y, std::cos(a) * v, vy, life, color, i < 3 ? 2 : 1);
        }
        for (int i = 0; i < 9; ++i) {
            const double a = N + rng.range(-1.1, 1.1), v = rng.range(15, 60), y = Y + rng.range(-1.5, 1.5),
                         life  = rng.range(.3, .55);
            const auto   color = rng.isChance(.5) ? stone.dust : mix(stone.dust, WHITE, .25);
            const double size  = rng.range(1.2, 2.3);
            ps.spawn(rng, ParticleKind::Dust, -2, y, std::cos(a) * v, std::sin(a) * v, life, color, size);
        }
        const double vx = rng.range(-10, -4), vy = rng.range(-16, -8), life = rng.range(.8, 1.2),
                     size = rng.range(1.8, 2.4);
        ps.spawn(rng, ParticleKind::Smoke, -4, Y, vx, vy, life, stone.dust, size, .05);
    }
    void update(double dt) override {
        t += dt;
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override { ps.draw(p, Paint::Layer::Back); }
    void draw(Paint& p) const override {
        const double heat = std::pow(clamp(1 - t / .9), 1.3), fade = 1 - smooth(1.4 - .45, 1.4 - .1, t);
        if (fade > 0) {
            for (auto c : cracks)
                p.line(c[0], Y + c[1], c[2], Y + c[3], heat > .5 ? C[3] : stone.shade, fade * (heat > .5 ? 1 : .85));
            for (auto q : rim) {
                const double h = heat * (q.kind ? .85 : 1.15);
                p.dot(q.x, Y + q.y,
                      q.kind == 0 && h < .75 ? hex(0x0d0a12)
                      : h > .85              ? WHITE
                      : h > .6               ? C[5]
                      : h > .4               ? C[4]
                      : h > .22              ? C[3]
                      : h > .1               ? C[2]
                                             : stone.shade,
                      fade);
            }
            if (heat > 0) p.glow(1, Y, 6 + 6 * heat, C[3], .5 * heat);
        }
        const double u = (t - .03) / .18;
        if (u >= 0 && u < 1) {
            const double r = 5 + 12 * ease::outCubic(u), n = std::ceil(r * TAU * 1.3);
            const auto   c = u < .3 ? C[5] : u < .6 ? C[4] : C[3];
            for (int j = 0; j < n; ++j) {
                const double g = (double(j) / n) * TAU;
                if (std::cos(g - PI) > -.25) p.dot(std::cos(g) * r, Y + std::sin(g) * r, c, 1.15 - u);
            }
        }
        ps.draw(p, Paint::Layer::Front);
        if (t < .03)
            impact::star(p, 0, Y, spikes, 1, 1, {C[2], C[5], C[4], WHITE}, 1, 2.2);
        else if (t < .06)
            impact::star(p, 0, Y, spikes, 1.25, .55, {C[1], C[4], C[3], C[5]}, 1, 2.2);
        const double pop = std::exp(-t / .045);
        if (pop > .02) {
            p.glow(-2, Y, 22, C[3], .6 * pop);
            p.glow(-1, Y, 8, WHITE, .5 * pop);
            p.glowLine(1, Y - 16, 1, Y + 16, 4, C[4], .4 * pop);
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& bulletImpactDescriptor() {
    static const std::array<Option, 2> options{
        {{"stone", 4278190080., 4294967295., false, 0, &PixelFxImpactSettings::stone, true},
         {"height", 0, 256, false, 0, &PixelFxImpactSettings::height}}};
    static const Descriptor d{
        {PixelFxKind::BulletImpact, "impacts/bullet-impact", 152, 100, 116, 84, 1.4, false},
        7,
        [](PixelFxSettings& s) {
            s.seed          = 6;
            s.impact.stone  = hex(0x5b5079);
            s.impact.height = 24;
            s.palette       = {hex(0x4a1712), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                               hex(0xffe26b), hex(0xfff3a6), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<BulletImpact>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
