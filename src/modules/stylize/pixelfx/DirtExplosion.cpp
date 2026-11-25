// eve-hot-path
// Adapted from pixelfx explosions/dirt-explosion.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Explosion.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
using namespace explosion;
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
    ParticleFlag flags = ParticleFlag::None;
};
struct Clod : Debris {
    double pvy  = 0;
    bool   thud = false;
};
struct DirtExplosion final : Kernel {
    std::vector<uint32_t> E, F;
    Rng                   rng;
    Stone                 clod;
    uint32_t              dust, soil, dark;
    Particles             ps;
    Lights                lights;
    BasicBits<Clod>       clods;
    double                t = 0;
    explicit DirtExplosion(const PixelFxSettings& o)
        : E(o.palette),
          F(o.explosion.fire),
          rng(o.seed),
          clod(0),
          dust(mix(E[4], rgb(150, 140, 150), .2)),
          soil(mix(E[3], rgb(90, 80, 90), .2)),
          dark(mix(E[2], rgb(60, 52, 60), .3)) {
        clod.light = E[5];
        clod.body  = E[3];
        clod.shade = E[2];
        ps.sparks  = {F[6], F[5], F[3], F[2]};
        lights.add(0, -10, 56, F[4], .7, .22);
        lights.add(0, -4, 26, WHITE, .6, .06);
        for (int i = 0; i < 14; ++i) {
            const double a = -PI / 2 + rng.range(-.7, .7), sp = rng.range(160, 320);
            emit({ParticleKind::Spark, 0, -2, std::cos(a) * sp, std::sin(a) * sp, rng.range(.15, .35)});
        }
        const std::array<uint32_t, 7> grit{E[1], E[2], E[2], E[3], E[3], E[4], E[5]};
        for (int i = 0; i < 300; ++i) {
            const int    side = rng.sign();
            const double a    = -PI / 2 + side * rng.range(.05, .62),
                         sp   = rng.range(150, 470) * (1 - .3 * std::abs(a + PI / 2));
            emit({ParticleKind::Grit, rng.range(-4, 4), -1, std::cos(a) * sp, std::sin(a) * sp, rng.range(1.2, 2.2),
                  rng.pick<uint32_t>(grit), rng.isChance(.45) ? 2. : 1., rng.range(0, .06)});
        }
        for (int i = 0; i < 30; ++i) {
            const int    side = rng.sign();
            const double a = -PI / 2 + side * rng.range(.1, .6), sp = rng.range(200, 430);
            Clod         b;
            b.kind            = DebrisKind::Chunk;
            b.x               = rng.range(-5, 5);
            b.y               = -2;
            b.vx              = std::cos(a) * sp;
            b.vy              = std::sin(a) * sp;
            b.gravity         = 1;
            b.drag            = .35;
            b.bounce          = .25;
            b.life            = rng.range(2.2, 3);
            b.angle           = rng.range(0, TAU);
            b.angularVelocity = rng.range(-12, 12);
            b.size            = i < 12 ? rng.range(2.6, 3.8) : rng.range(1.4, 2.4);
            clods.add(std::move(b));
        }
        for (int i = 0; i < 22; ++i)
            emit({ParticleKind::Smoke, rng.range(-7, 7), -rng.range(2, 12), rng.range(-25, 25), -rng.range(70, 180),
                  rng.range(1.8, 2.6), rng.isChance(.6) ? soil : dust, rng.range(9, 14), rng.range(0, .14),
                  ParticleFlag::Big});
        for (int i = 0; i < 14; ++i) {
            const int    side = rng.sign();
            const double a = -PI / 2 + side * rng.range(.35, .6), sp = rng.range(110, 210);
            emit({ParticleKind::Smoke, side * rng.range(2, 8), -rng.range(2, 8), std::cos(a) * sp, std::sin(a) * sp,
                  rng.range(1.6, 2.4), rng.isChance(.5) ? soil : dust, rng.range(8, 12), rng.range(.02, .1),
                  ParticleFlag::Big});
        }
        for (int i = 0; i < 16; ++i) {
            const int side = rng.sign();
            emit({ParticleKind::Smoke, side * rng.range(4, 16), -rng.range(1, 5), side * rng.range(70, 200),
                  -rng.range(2, 12), rng.range(1.4, 2.2), dust, rng.range(6, 9), rng.range(.02, .1),
                  ParticleFlag::Big});
        }
        for (int i = 0; i < 8; ++i)
            emit({ParticleKind::Smoke, rng.range(-8, 8), -rng.range(2, 8), rng.range(-40, 40), -rng.range(40, 110),
                  rng.range(1.2, 1.8), dark, rng.range(6, 9), rng.range(0, .05)});
    }
    void emit(Emission q) { ps.spawn(rng, q.kind, q.x, q.y, q.vx, q.vy, q.life, q.color, q.size, q.delay, q.flags); }
    void update(double dt) override {
        t += dt;
        for (auto& b : clods.list) b.pvy = b.vy;
        clods.update(dt, [](double) { return 0.; });
        const std::array<uint32_t, 3> crumbs{E[2], E[3], E[4]};
        for (size_t i = clods.list.size(); i-- > 0;) {
            auto& b = clods.list[i];
            if (b.x < -123 || b.x > 123) {
                clods.list.erase(clods.list.begin() + std::ptrdiff_t(i));
                continue;
            }
            const bool landed = (b.pvy > 0 && b.vy <= 0) || (b.rest && !b.thud);
            if (!landed || b.pvy < 80) {
                if (b.rest) b.thud = true;
                continue;
            }
            b.thud = b.rest;
            for (int j = 0; j < 2 + int(jsRound(b.size)); ++j)
                emit({ParticleKind::Dust, b.x + rng.range(-2, 2), -.5, rng.signedRange(15, 55), -rng.range(4, 22),
                      rng.range(.35, .7), dust, rng.range(1.6, 1.2 + b.size)});
            for (int j = 0; j < int(jsRound(b.size * 3)); ++j)
                emit({ParticleKind::Grit, b.x, -1, rng.range(-70, 70), -rng.range(40, 140), rng.range(.8, 1.4),
                      rng.pick<uint32_t>(crumbs), 1});
            if (b.size > 2.2 && b.pvy > 200) b.size *= .6;
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        cull(ps, -127, -203, 127, 31);
        fadeEdges(ps, -127, 127, 24, 31);
        lights.update(dt);
    }
    void groundRing(Paint& p, bool back) const {
        const double k = t / .45;
        if (k >= 1) return;
        const std::array<uint32_t, 4> ramp{E[2], E[4], E[5], E[6]};
        band(p, 0, .5, 90 * ease::outCubic(k), .16, std::max(1., 3 * (1 - k)), 1.1 - k, ramp,
             [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; });
    }
    void drawBack(Paint& p) const override {
        crater(p, E, 0, 28, ease::outCubic(std::min(1., t / .12)), 1 - smooth(3.6 - .7, 3.6 - .1, t));
        groundRing(p, true);
        drawDirt(p, ps, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        groundRing(p, false);
        if (t < .09) dome(p, F, 0, 15 * (1 - t * 3), 1 - t / .09);
        for (const auto& b : clods.list) rock(p, b, clod, F);
        drawDirt(p, ps, Paint::Layer::Front);
        pool(p, 0, 1, 60, 10, F[3], .5 * std::max(0., 1 - t / .3));
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& dirtExplosionDescriptor() {
    static const std::array<Option, 1> options{{{"fire", 1, 64, false, 8, &PixelFxExplosionSettings::fire}}};
    static const Descriptor            descriptor{
        {PixelFxKind::DirtExplosion, "explosions/dirt-explosion", 260, 240, 130, 206, 3.6, false},
        7,
        [](PixelFxSettings& s) {
            s.seed           = 3;
            s.palette        = {hex(0x1c1610), hex(0x33281c), hex(0x4f3f2c), hex(0x6e5a40),
                                hex(0x927a58), hex(0xb89f78), hex(0xdcc8a2)};
            s.explosion.fire = {hex(0x4a1712), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<DirtExplosion>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
