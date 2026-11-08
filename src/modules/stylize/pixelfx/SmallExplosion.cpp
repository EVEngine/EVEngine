// eve-hot-path
// Adapted from pixelfx explosions/small-explosion.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
    ParticleFlag flags = ParticleFlag::None;
};
struct Flash {
    double radius, life, max;
};
struct Ring {
    double   r0, r1, life, max, thickness;
    uint32_t color;
};
struct SmallExplosion final : Kernel {
    std::vector<uint32_t> F;
    double                size, radius, power, y, t = 0;
    uint32_t              stone, light, shade, dust;
    Rng                   rng;
    Particles             ps;
    Blazes                blazes;
    Bits                  rocks;
    Lights                lights;
    std::vector<Flash>    flashes;
    std::vector<Ring>     rings;
    explicit SmallExplosion(const PixelFxSettings& o)
        : F(o.palette),
          size(o.explosion.size),
          radius(18 * size),
          power(.9 * std::pow(size, .8)),
          y(-radius * .3),
          stone(o.explosion.stone),
          light(mix(scale(stone, 1.35), WHITE, .08)),
          shade(scale(stone, .64)),
          dust(mix(scale(stone, 1.2), rgb(150, 140, 150), .3)),
          rng(o.seed) {
        ps.sparks = {F[6], F[5], F[3], F[2]};
        flashes.push_back({radius * 1.05, .06, .06});
        rings.push_back({radius * .45, radius * 2.7, .34, .34, 3, F[5]});
        rings.push_back({radius * .3, radius * 1.6, .22, .22, 1.8, F[3]});
        lights.add(0, y, radius * 3.4, rgb(255, 176, 96), .6 * power, .7);
        lights.add(0, y, radius * 1.6, WHITE, .6, .08);
        for (int i = 0; i < 5; ++i)
            emit({ParticleKind::Fire, rng.range(-2.5, 2.5), y + rng.range(-2.5, 2.5), rng.range(-20, 20),
                  rng.range(-40, -10), rng.range(.18, .3), 0, rng.range(radius * .4, radius * .55)});
        for (int i = 0; i < int(jsRound(28 * power)); ++i) {
            const double a = rng.range(0, TAU), d = rng.range(0, radius * .55), sp = rng.range(40, 210) * power;
            emit({ParticleKind::Fire, std::cos(a) * d, y + std::sin(a) * d, std::cos(a) * sp,
                  std::sin(a) * sp * .8 - 40, rng.range(.55, 1.15), 0, rng.range(radius * .22, radius * .46), 0,
                  rng.isChance(.45) ? ParticleFlag::Smokes | ParticleFlag::Big : ParticleFlag::None});
        }
        for (int i = 0; i < 5; ++i) {
            const double a = rng.range(0, TAU), d = rng.range(radius * .1, radius * .5);
            emit({ParticleKind::Smoke, std::cos(a) * d, y + std::sin(a) * d, std::cos(a) * 30, std::sin(a) * 20 - 30,
                  rng.range(.9, 1.5), rgb(34, 29, 38), rng.range(radius * .15, radius * .26), rng.range(.06, .2),
                  ParticleFlag::Big});
        }
        for (int i = 0; i < 5; ++i)
            emit({ParticleKind::Fire, rng.range(-radius * .25, radius * .25), y, rng.range(-12, 12),
                  rng.range(-180, -100), rng.range(.8, 1.3), 0, rng.range(radius * .24, radius * .36), rng.range(0, .1),
                  rng.isChance(.6) ? ParticleFlag::Smokes | ParticleFlag::Big : ParticleFlag::None});
        for (int i = 0; i < int(jsRound(10 * power)); ++i) {
            const double a = rng.range(0, TAU), d = rng.range(0, radius * .7), sp = rng.range(15, 100);
            emit({ParticleKind::Smoke, std::cos(a) * d, y + std::sin(a) * d, std::cos(a) * sp,
                  std::sin(a) * sp * .6 - 35, rng.range(1.8, 2.8), 0, rng.range(radius * .26, radius * .44),
                  rng.range(.12, .45), ParticleFlag::Big});
        }
        for (int i = 0; i < int(jsRound(80 * power)); ++i) {
            const double a = rng.range(0, TAU), sp = rng.range(140, 380) * power;
            emit({ParticleKind::Spark, 0, y, std::cos(a) * sp, std::sin(a) * sp - 70, rng.range(.18, .6)});
        }
        for (int i = 0; i < int(jsRound(40 * power)); ++i) {
            const double a = rng.range(0, TAU), sp = rng.range(30, 150);
            emit({ParticleKind::Ember, std::cos(a) * radius * .4, y + std::sin(a) * radius * .4, std::cos(a) * sp,
                  std::sin(a) * sp - 75, rng.range(1.2, 2.6), F[3]});
        }
        // The source samples the upper loop bound each iteration; preserve RNG consumption.
        for (int i = 0; i < int(jsRound(rng.range(9, 14) * std::max(0.0, power - .55))); ++i) {
            const double a = -PI / 2 + rng.range(-1.35, 1.35), sp = rng.range(170, 330);
            emit({ParticleKind::Brand, 0, y, std::cos(a) * sp, std::sin(a) * sp, rng.range(.7, 1.3)});
        }
        for (int i = 0; i < int(jsRound(radius * .5)); ++i) {
            const int side = rng.sign();
            emit({ParticleKind::Smoke, side * rng.range(0, radius * .6), -rng.range(1, 5 + radius * .04),
                  side * rng.range(3, 9.5) * radius, -rng.range(2, 15), rng.range(1.1, 2), dust,
                  rng.range(4, 7) + radius / 6, rng.range(0, .06), ParticleFlag::Big});
        }
        for (int i = 0; i < 26 * std::min(1.5, size); ++i) {
            Debris b;
            b.kind            = DebrisKind::Chunk;
            b.x               = rng.range(-.4, .4) * radius;
            b.y               = -3;
            b.vx              = rng.range(-9, 9) * radius * .25;
            b.vy              = -rng.range(7, 16) * radius * .9;
            b.gravity         = 1;
            b.drag            = .4;
            b.bounce          = .3;
            b.life            = rng.range(1.8, 2.6);
            b.angle           = rng.range(0, TAU);
            b.angularVelocity = rng.range(-16, 16);
            b.size            = rng.integer(1, 3);
            rocks.add(std::move(b));
        }
        const std::array<uint32_t, 4> grit{{light, stone, stone, shade}};
        for (int i = 0; i < int(jsRound(1.6 * radius * 4)); ++i) {
            const double u = rng.range(-1, 1), a = -PI / 2 + u * .5,
                         sp = rng.range(5, 15) * radius * (1 - .45 * std::abs(u));
            emit({ParticleKind::Grit, u * .25 * radius, -1, std::cos(a) * sp, std::sin(a) * sp, rng.range(1.2, 2.2),
                  rng.pick<uint32_t>(grit), rng.isChance(.45) ? 2. : 1.});
        }
        for (int i = 0; i < int(jsRound(1.6 * 12)); ++i)
            emit({ParticleKind::Smoke, rng.range(-.3, .3) * radius, -rng.range(2, radius * .6), rng.range(-15, 15),
                  -rng.range(60, 190) * 1.6 * .6, rng.range(1.6, 2.6), dust,
                  rng.range(5, 9) * std::sqrt(size) + radius / 7, rng.range(0, .15), ParticleFlag::Big});
    }
    void emit(Emission e) { ps.spawn(rng, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay, e.flags); }
    void update(double dt) override {
        t += dt;
        for (size_t n = flashes.size(); n > 0; --n)
            if ((flashes[n - 1].life -= dt) <= 0) flashes.erase(flashes.begin() + std::ptrdiff_t(n - 1));
        for (size_t n = rings.size(); n > 0; --n)
            if ((rings[n - 1].life -= dt) <= 0) rings.erase(rings.begin() + std::ptrdiff_t(n - 1));
        rocks.update(dt, [](double) { return 0.; });
        for (auto& b : rocks.list)
            if (b.rest && !b.dusted) {
                b.dusted = true;
                for (int i = 0; i < 3; ++i) {
                    const double x    = b.x + rng.range(-1.5, 1.5);
                    const int    side = rng.sign();
                    const double vx = side * rng.range(10, 55) * .6, vy = -rng.range(2, 22), life = rng.range(.3, .65),
                                 s = rng.range(1.3, 2.8);
                    emit({ParticleKind::Dust, x, -.5, vx, vy, life, dust, s});
                }
            }
        blazes.update(dt, ps, rng);
        ps.update(
            dt, rng, [](double) { return 0.; },
            [&](double x, double py) {
                const double life = rng.range(.9, 1.5), p = rng.range(.55, .85);
                blazes.add(rng, x, py - .25, life, p);
            });
        lights.update(dt);
    }
    void scorch(Paint& p) const {
        const double fade = 1 - smooth(5.4 - 1.2, 5.4 - .2, t);
        if (fade <= 0) return;
        const double rx = 22 * size * std::min(1.0, t / .08), cool = 1.2 + radius / 14;
        for (int py = 0; py <= 2; ++py)
            for (int x = int(std::floor(-rx)); x <= int(std::ceil(rx)); ++x) {
                const double dx = (x + .5) / rx, dy = (py + .5) / 4, d = std::sqrt(dx * dx + dy * dy);
                if (d > 1) continue;
                const double k    = rand({jsUint(x * 31 + py), 3}),
                             heat = std::pow(clamp(1 - t / cool), 1.4) * (1.25 - d) - k * .35;
                if (heat > .06) {
                    const size_t index =
                        std::min(F.size() - 1, size_t(std::floor((1 - std::min(1.0, heat)) * (F.size() - 1))));
                    const uint32_t c = F[F.size() - 1 - index];
                    p.dot(x, py, c, fade);
                    if ((x & 3) == 0) p.glow(x + .5, py, 6, c, .18 * heat * fade);
                } else
                    p.dot(x, py, d < .5 ? hex(0x0d0a12) : hex(0x17121d), (d < .95 - k * .3 ? 1 : 1 - d) * fade);
            }
    }
    void groundRing(Paint& p, bool back) const {
        const double k = t / .5;
        if (k >= 1) return;
        const std::array<uint32_t, 4> ramp{{stone, light, F[3], F[6]}};
        band(p, 0, .5, 80 * size * ease::outCubic(k), .16, std::max(1.0, (2 + radius / 9) * (1 - k)), 1.15 - k, ramp,
             [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; });
    }
    void drawBack(Paint& p) const override {
        scorch(p);
        ps.draw(p, Paint::Layer::Back);
        blazes.drawBack(p);
        groundRing(p, true);
    }
    void draw(Paint& p) const override {
        groundRing(p, false);
        for (const auto& b : rocks.list) {
            const double r = .6 + b.size * .75, fade = std::min(1.0, b.life / .5), lx = std::cos(b.angle) * r * .4,
                         ly = std::sin(b.angle) * r * .4;
            p.shader(b.x - r - 1, b.y - r - 1, b.x + r + 1, b.y + r + 1, [&](double x, double py, ShadeOutput& out) {
                const double dx = x - b.x, dy = py - b.y, d = jsHypot2(dx * 1.15, dy);
                if (d > r + .9) return;
                const double lit = dx * lx + dy * ly;
                out.c            = d > r ? hex(0x1a1422) : lit < -.4 ? light : lit > .6 ? shade : stone;
                out.a            = fade;
            });
        }
        ps.draw(p, Paint::Layer::Front);
        for (const auto& f : flashes) {
            const double k = f.life / f.max, r = f.radius * (.8 + .2 * k);
            p.circle(0, y, r + 1.2, F[2]);
            p.circle(0, y, r, k > .45 ? F[6] : F[5]);
            p.circle(0, y, r * .6, k > .45 ? WHITE : F[6]);
        }
        for (const auto& r : rings) {
            const double k = r.life / r.max, e = 1 - std::pow(k, 3);
            p.ring(0, y, r.r0 + (r.r1 - r.r0) * e, std::max(1.0, r.thickness * k + .4), r.color,
                   std::min(1.0, k * 1.6));
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& smallExplosionDescriptor() {
    static const std::array<Option, 2> options{
        {{"size", .25, 2, false, 0, &PixelFxExplosionSettings::size},
         {"stone", 4278190080., 4294967295., false, 0, &PixelFxExplosionSettings::stone, true}}};
    static const Descriptor descriptor{
        {PixelFxKind::SmallExplosion, "explosions/small-explosion", 300, 240, 150, 196, 5.4, false},
        8,
        [](PixelFxSettings& s) {
            s.seed            = 7;
            s.explosion.size  = 1;
            s.explosion.stone = hex(0x4a3f69);
            s.palette         = {hex(0x4a1712), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                 hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<SmallExplosion>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
