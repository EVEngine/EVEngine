// eve-hot-path
// Native adaptation of pixelfx impacts/heavy-landing.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/GroundImpact.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct HeavyLanding final : Kernel {
    const std::vector<uint32_t> C;
    const impact::Stone         stone;
    Rng                         rng;
    Particles                   back, front;
    BasicBits<impact::Rock>     bits;
    std::vector<impact::Crack>  cracks;
    uint32_t                    dust, lit;
    double                      phase = 0, t = 0;
    explicit HeavyLanding(const PixelFxSettings& o) : C(o.palette), stone(o.impact.stone), rng(o.seed) {
        impact::CrackRecipe recipe;
        recipe.minLength = 8;
        recipe.maxLength = 20;
        recipe.dive      = .7;
        recipe.maxY      = 12;
        recipe.forks     = .4;
        recipe.minX      = 2;
        recipe.maxX      = 6;
        cracks           = impact::makeCracks(rng, 6, recipe);
        dust             = mix(stone.dust, C[5], .4);
        lit              = mix(dust, WHITE, .3);
        phase            = rng.range(0, TAU);
        const auto ring  = [&](int n, double minSpeed, double maxSpeed, double minSize, double maxSize, double delay) {
            for (int i = 0; i < n; ++i) {
                const double g = (double(i) / n) * TAU + rng.range(-.1, .1), v = rng.range(minSpeed, maxSpeed),
                             vy = std::sin(g) * v * .16 - rng.range(2, 10), life = rng.range(.45, .8);
                const auto   color = rng.isChance(.45) ? lit : dust;
                const double size = rng.range(minSize, maxSize), start = delay + rng.range(0, .03);
                (std::sin(g) < 0 ? back : front)
                    .spawn(rng, ParticleKind::Dust, std::cos(g) * 4, std::sin(g) * .7 - 1, std::cos(g) * v, vy, life,
                           color, size, start);
            }
        };
        ring(20, 140, 190, 2.2, 3.4, .01);
        ring(18, 70, 110, 3.4, 5.4, .12);
        for (int i = 0; i < 12; ++i) {
            const int    side = rng.sign();
            const bool   up   = i < 6;
            const double x = side * rng.range(2, 10), y = -rng.range(1, 4),
                         vx = side * rng.range(10, 35) * (up ? .6 : 1),
                         vy = -(up ? rng.range(25, 60) : rng.range(4, 12)), life = rng.range(.55, .9);
            const auto   color = rng.isChance(.5) ? lit : dust;
            const double size = rng.range(3, 5), delay = rng.range(.02, .1);
            (i % 2 ? front : back).spawn(rng, ParticleKind::Dust, x, y, vx, vy, life, color, size, delay);
        }
        for (int i = 0; i < 12; ++i) {
            const int    side = rng.sign();
            const double v    = rng.range(50, 130);
            impact::Rock b;
            b.kind            = DebrisKind::Chunk;
            b.x               = side * rng.range(3, 8);
            b.y               = -1;
            b.vx              = side * v;
            b.vy              = -rng.range(140, 230);
            b.gravity         = 1;
            b.drag            = .5;
            b.bounce          = .55;
            b.life            = rng.range(1, 1.45);
            b.angle           = rng.range(0, TAU);
            b.angularVelocity = side * rng.range(8, 16);
            b.size            = rng.isChance(.3) ? 2 : 1;
            b.points          = impact::rockShape(rng);
            bits.add(std::move(b));
        }
        const std::array<uint32_t, 3> grit{stone.light, stone.body, stone.lip};
        for (int i = 0; i < 24; ++i) {
            const int    side = rng.sign();
            const double a = -PI / 2 + side * rng.range(.3, 1.2), v = rng.range(60, 170), x = side * rng.range(2, 7),
                         life  = rng.range(.6, 1.1);
            const auto   color = rng.pick<uint32_t>(grit);
            const double size  = rng.isChance(.3) ? 2 : 1;
            front.spawn(rng, ParticleKind::Grit, x, -1, std::cos(a) * v, std::sin(a) * v, life, color, size);
        }
    }
    void rings(Paint& p, bool far) const {
        for (const auto& ring :
             std::array<std::array<double, 6>, 2>{{{.01, .45, 58, 4.5, .75, 1}, {.12, .85, 42, 6, .8, 0}}}) {
            const double g = (t - ring[0]) / ring[1];
            if (g <= 0 || g >= 1) continue;
            const double r = 6 + ring[2] * ease::outCubic(g);
            impact::dustRing(p, 0, 0, r, ring[3] * (1 + g), ring[4] * std::pow(1 - g, 1.2) * std::min(1., g / .06),
                             dust, .2, phase + ring[0] * 10, g * .8, .35, INFINITY, far ? -1 : 1);
            if (ring[5] != 0 && g < .75) {
                const std::array<uint32_t, 4> ramp{stone.body, stone.light, lit, WHITE};
                band(p, 0, .5, r + 2, .2, std::max(1., 2.5 * (1 - g)), 1.1 - g / .75, ramp,
                     [&](double a) { return (std::sin(a) < 0) == far; });
            }
        }
    }
    void update(double dt) override {
        t += dt;
        for (auto& b : bits.list) b.previousVy = b.vy;
        bits.update(dt, [](double) { return 0.; });
        for (auto& b : bits.list)
            if (b.previousVy > 80 && b.vy <= 0) {
                const int    side = rng.sign();
                const double vx = side * rng.range(6, 20), vy = -rng.range(2, 10), life = rng.range(.25, .45),
                             size = rng.range(1, 1.8);
                front.spawn(rng, ParticleKind::Dust, b.x, -.5, vx, vy, life, dust, size);
                b.previousVy = 0;
            }
        back.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        front.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
    }
    void drawBack(Paint& p) const override {
        const double fade = 1 - smooth(1.6 - .5, 1.6 - .1, t);
        for (int x = -10; x <= 9; ++x) {
            const double q = std::abs(x + .5) / 10, depth = jsRound(1.8 * (1 - q * q));
            if (q < .82) {
                for (int y = 0; y < depth; ++y) p.dot(x, y, impact::SOOT, fade);
                p.dot(x, depth, stone.lip, fade);
            } else
                p.dot(x, -1, q > .92 ? stone.light : stone.lip, fade);
        }
        impact::drawCracks(
            p, cracks, [&](const impact::Crack& c) { return c.length * ease::outCubic(clamp((t - c.delay) / .08)); },
            [](double) { return 0.; }, {WHITE, WHITE, WHITE, WHITE}, impact::SOOT, stone.light, fade);
        rings(p, true);
        back.draw(p);
    }
    void draw(Paint& p) const override {
        rings(p, false);
        front.draw(p);
        for (const auto& b : bits.list) impact::rock(p, b, stone);
        if (t < .035)
            p.ellipse(0, 0, 15, 2.5, [&](double d) { return d < .55 ? WHITE : C[6]; });
        else if (t < .07)
            for (int x = -22; x <= 22; ++x) p.dot(x, 0, std::abs(x) < 10 ? WHITE : C[5], 1 - std::abs(x) / 24.);
        if (t < .12) p.glowLine(-20, 0, 20, 0, 8, C[6], .45 * (1 - t / .12));
    }
};
}  // namespace
const Descriptor& heavyLandingDescriptor() {
    static const std::array<Option, 2> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
         {"stone", 4278190080., 4294967295., false, 0, &PixelFxImpactSettings::stone, true}}};
    static const Descriptor d{
        {PixelFxKind::HeavyLanding, "impacts/heavy-landing", 176, 80, 88, 60, 1.6, false},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 14;
            s.actor.source = PixelFxActorSource::Dummy;
            s.impact.stone = hex(0x4a3f69);
            s.palette      = {hex(0x1c1a24), hex(0x2f2a3a), hex(0x4a4258), hex(0x6b6280),
                              hex(0x9088a4), hex(0xbdb6cc), hex(0xe6e1ee)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<HeavyLanding>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
