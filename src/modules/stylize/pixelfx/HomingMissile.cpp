// eve-hot-path
// Adapted from pixelfx projectiles/homing-missile.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Projectile.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                          FLIGHT = 1.15;
constexpr std::array<Point, 4>            PATH{{{-130, -22}, {-114, -110}, {-30, -120}, {0, 0}}};
constexpr std::array<std::string_view, 5> MISSILE{
    {"ff..........", ".fhhhhhhhre.", "nmmmmmmmmrrw", ".fsssssssrd.", "ff.........."}};
Point at(double u) {
    const double v = 1 - u, a = v * v * v, b = 3 * v * v * u, c = 3 * v * u * u, d = u * u * u;
    return {a * PATH[0].x + b * PATH[1].x + c * PATH[2].x + d * PATH[3].x,
            a * PATH[0].y + b * PATH[1].y + c * PATH[2].y + d * PATH[3].y};
}
double heading(double u) {
    const double v  = 1 - u;
    const double dx = 3 * v * v * (PATH[1].x - PATH[0].x) + 6 * v * u * (PATH[2].x - PATH[1].x) +
                      3 * u * u * (PATH[3].x - PATH[2].x),
                 dy = 3 * v * v * (PATH[1].y - PATH[0].y) + 6 * v * u * (PATH[2].y - PATH[1].y) +
                      3 * u * u * (PATH[3].y - PATH[2].y);
    return std::atan2(dy, dx);
}
double progress(double t) { return std::pow(clamp(t / FLIGHT), 1.45); }
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
struct HomingMissile final : Kernel {
    std::vector<uint32_t>     F, M, SM;
    std::array<uint32_t, 256> palette{};
    uint32_t                  stone, stoneLight, stoneShade, dust;
    Rng                       rng;
    Particles                 ps;
    projectile::Smoke         smoke;
    Bits                      rocks;
    Lights                    lights;
    double                    t = 0, emitCount = 0;
    bool                      hit = false;
    explicit HomingMissile(const PixelFxSettings& o)
        : F(o.palette),
          M(o.projectile.metal),
          SM(o.projectile.smoke),
          stone(o.projectile.stone),
          stoneLight(mix(scale(stone, 1.35), WHITE, .08)),
          stoneShade(scale(stone, .64)),
          dust(mix(scale(stone, 1.2), hex(0x968c96), .3)),
          rng(o.seed) {
        ps.sparks    = {F[7], F[6], F[4], F[3]};
        palette['f'] = M[1];
        palette['h'] = M[4];
        palette['m'] = M[3];
        palette['s'] = M[1];
        palette['n'] = M[0];
        palette['r'] = F[2];
        palette['e'] = F[3];
        palette['d'] = F[1];
        palette['w'] = WHITE;
        lights.add(PATH[0].x, PATH[0].y, 30, F[4], .7, .25);
        for (int i = 0; i < 6; ++i) {
            projectile::SmokeParticle q;
            q.x    = PATH[0].x + 2;
            q.y    = PATH[0].y + 2;
            q.vx   = rng.range(-30, 20);
            q.vy   = rng.range(-10, 15);
            q.r0   = 2;
            q.r1   = rng.range(5, 7.5);
            q.life = rng.range(1.2, 1.8);
            q.a    = .6;
            q.warm = 1;
            q.rise = 8;
            smoke.add(rng, q);
        }
    }
    void emit(Emission e) { ps.spawn(rng, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    void blast() {
        const double Y = -6;
        lights.add(0, Y - 2, 52, hex(0xffb060), .8, .8);
        lights.add(0, Y - 2, 30, WHITE, .75, .1);
        for (int i = 0; i < 26; ++i) {
            const double a = rng.range(0, TAU), d = rng.range(0, 6), sp = rng.range(30, 170);
            emit({ParticleKind::Fire, std::cos(a) * d, Y + std::sin(a) * d, std::cos(a) * sp,
                  std::sin(a) * sp * .75 - 40, rng.range(.45, .95), 0, rng.range(3.2, 6.5)});
        }
        for (int i = 0; i < 5; ++i)
            emit({ParticleKind::Fire, rng.range(-3, 3), Y, rng.range(-10, 10), -rng.range(90, 150), rng.range(.7, 1.1),
                  0, rng.range(4, 5.5), rng.range(0, .08)});
        for (int i = 0; i < 70; ++i) {
            const double a = rng.range(PI + .12, TAU - .12), sp = rng.range(110, 280);
            emit({ParticleKind::Spark, 0, Y, std::cos(a) * sp, std::sin(a) * sp - 40, rng.range(.2, .65)});
        }
        for (int i = 0; i < 22; ++i) {
            const double a = rng.range(PI, TAU), sp = rng.range(30, 110);
            emit({ParticleKind::Ember, std::cos(a) * 4, Y + std::sin(a) * 4, std::cos(a) * sp, std::sin(a) * sp - 50,
                  rng.range(.9, 1.6), F[4]});
        }
        for (int i = 0; i < 16; ++i) {
            Debris b;
            b.kind            = DebrisKind::Chunk;
            b.x               = rng.range(-5, 5);
            b.y               = -2;
            b.vx              = rng.range(-110, 110);
            b.vy              = -rng.range(140, 290);
            b.gravity         = 1;
            b.drag            = .4;
            b.bounce          = .3;
            b.life            = rng.range(1.5, 2.2);
            b.angle           = rng.range(0, TAU);
            b.angularVelocity = rng.range(-16, 16);
            b.size            = rng.integer(1, 2);
            rocks.add(std::move(b));
        }
        const std::array<uint32_t, 4> grit{{stoneLight, stone, stone, stoneShade}};
        for (int i = 0; i < 60; ++i) {
            const double u = rng.range(-1, 1), a = -PI / 2 + u * .55, sp = rng.range(80, 220) * (1 - .4 * std::abs(u));
            emit({ParticleKind::Grit, u * 4, -1, std::cos(a) * sp, std::sin(a) * sp, rng.range(1, 1.8),
                  rng.pick<uint32_t>(grit), rng.isChance(.4) ? 2. : 1.});
        }
        for (int i = 0; i < 18; ++i) {
            const double              a = rng.range(PI * 1.15, PI * 1.85), sp = rng.range(20, 90);
            projectile::SmokeParticle q;
            q.x     = std::cos(a) * 4;
            q.y     = Y + std::sin(a) * 4;
            q.vx    = std::cos(a) * sp * .7;
            q.vy    = std::sin(a) * sp - 45 * (double(i) / 18);
            q.r0    = 3;
            q.r1    = rng.range(8, 14);
            q.life  = rng.range(1.7, 2.2);
            q.a     = .62;
            q.b     = .88;
            q.warm  = 1;
            q.rise  = 30 + i * 2;
            q.drag  = 1.8;
            q.delay = rng.range(.05, .35);
            smoke.add(rng, q);
        }
        for (int i = 0; i < 12; ++i) {
            const int                 side = rng.sign();
            projectile::SmokeParticle q;
            q.x    = side * rng.range(0, 6);
            q.y    = -3;
            q.vx   = side * rng.range(60, 150);
            q.vy   = -rng.range(3, 12);
            q.r0   = 2.5;
            q.r1   = rng.range(6, 9);
            q.life = rng.range(1.1, 1.7);
            q.a    = .5;
            q.b    = .75;
            q.warm = .3;
            q.rise = 6;
            q.drag = 3;
            smoke.add(rng, q);
        }
    }
    void update(double dt) override {
        t += dt;
        if (t < FLIGHT) {
            const double u = progress(t);
            const auto   p = at(u), prev = at(progress(std::max(0.0, t - dt)));
            const double h = heading(u), cx = std::cos(h), cy = std::sin(h),
                         distance = jsHypot2(p.x - prev.x, p.y - prev.y) / 1.5;
            for (emitCount += distance; emitCount > 1; --emitCount) {
                const double              f = (emitCount - 1) / std::max(1e-6, distance), ex = p.x + (prev.x - p.x) * f,
                                          ey = p.y + (prev.y - p.y) * f;
                projectile::SmokeParticle q;
                q.x                 = ex - cx * 7 + rng.range(-.6, .6);
                q.y                 = ey - cy * 7 + rng.range(-.6, .6);
                const double speedX = rng.range(10, 30), jitterX = rng.range(-6, 6);
                q.vx                = -cx * speedX + jitterX;
                const double speedY = rng.range(10, 30), jitterY = rng.range(-6, 6);
                q.vy   = -cy * speedY + jitterY;
                q.r0   = 1.4;
                q.r1   = rng.range(3.5, 6);
                q.life = rng.range(1.5, 2.3);
                q.a    = .5;
                q.b    = .9;
                q.warm = .8;
                q.rise = 4;
                q.drag = 3;
                q.grow = 1.2;
                smoke.add(rng, q);
            }
            if (rng.isChance(.35)) {
                const double a = h + PI + rng.range(-.5, .5), sp = rng.range(40, 140);
                emit({ParticleKind::Spark, p.x - cx * 7, p.y - cy * 7, std::cos(a) * sp, std::sin(a) * sp,
                      rng.range(.15, .35)});
            }
        } else if (!hit) {
            hit = true;
            blast();
        }
        for (auto& b : rocks.list)
            if (b.rest && !b.dusted) {
                b.dusted = true;
                for (int i = 0; i < 2; ++i) {
                    const double x    = b.x + rng.range(-1, 1);
                    const int    side = rng.sign();
                    const double vx = side * rng.range(8, 30), vy = -rng.range(2, 14), life = rng.range(.3, .6),
                                 size = rng.range(1.2, 2.2);
                    emit({ParticleKind::Dust, x, -.5, vx, vy, life, dust, size});
                }
            }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        smoke.update(dt);
        rocks.update(dt, [](double) { return 0.; });
        lights.update(dt);
    }
    void ringPart(Paint& p, bool back) const {
        const double q = (t - FLIGHT) / .45;
        if (q >= 1) return;
        const std::array<uint32_t, 4> ramp{{stone, stoneLight, F[4], F[7]}};
        band(p, 0, .5, 60 * (1 - std::pow(1 - q, 3)), .16, std::max(1.0, 4 * (1 - q)), 1.15 - q, ramp,
             [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; });
    }
    void drawBack(Paint& p) const override {
        if (!hit) return;
        const double k = t - FLIGHT, fade = 1 - smooth(2.8, 3.6, t), rx = 16 * std::min(1.0, k / .06);
        for (int y = 0; y <= 2; ++y)
            for (int x = int(std::floor(-rx)); x <= int(std::ceil(rx)); ++x) {
                const double d = jsHypot2((x + .5) / rx, (y + .5) / 4);
                if (d > 1) continue;
                const double T = std::pow(clamp(1 - k / 1.4), 1.4) * (1.25 - d) - rand({jsUint(x * 31 + y), 3}) * .35;
                p.dot(x, y,
                      T > .06  ? F[size_t(std::max(1, std::min(8, int(jsRound(T * 8)))))]
                      : d < .5 ? hex(0x0d0a12)
                               : hex(0x17121d),
                      fade);
            }
        ringPart(p, true);
    }
    void draw(Paint& p) const override {
        const auto                    list = smoke.puffs();
        const std::array<uint32_t, 7> warm{{SM[0], mix(F[1], SM[1], .5), mix(F[2], SM[2], .4), F[3], F[4], F[6], F[7]}};
        projectile::cloud(p, list, SM, warm, 1.4, .82, 1.4);
        if (hit) ringPart(p, false);
        for (const auto& b : rocks.list) {
            const double r = .6 + b.size * .75, fade = std::min(1.0, b.life / .5), lx = std::cos(b.angle) * r * .4,
                         ly = std::sin(b.angle) * r * .4;
            p.shader(b.x - r - 1, b.y - r - 1, b.x + r + 1, b.y + r + 1, [&](double x, double y, ShadeOutput& out) {
                const double dx = x - b.x, dy = y - b.y, d = jsHypot2(dx * 1.15, dy);
                if (d > r + .9) return;
                const double lit = dx * lx + dy * ly;
                out.c            = d > r ? hex(0x1a1422) : lit < -.4 ? stoneLight : lit > .6 ? stoneShade : stone;
                out.a            = fade;
            });
        }
        ps.draw(p);
        if (t < FLIGHT) {
            const double   u   = progress(t);
            const auto     pos = at(u);
            const double   h = heading(u), cx = std::cos(h), cy = std::sin(h);
            const uint32_t fr = jsUint(std::floor(t * 30));
            const double   J  = 5 + 3 * rand({fr, 7});
            for (double d = 0; d <= J; d += .5) {
                const double   v = d / J;
                const uint32_t c = v < .3 ? WHITE : v < .6 ? F[6] : F[4];
                p.dot(pos.x - cx * (6.5 + d), pos.y - cy * (6.5 + d), c);
                if (v < .5) p.dot(pos.x - cx * (6.5 + d) - cy * .8, pos.y - cy * (6.5 + d) + cx * .8, F[5]);
            }
            const double radius = jsHypot2(6., 2.5) + 1;
            p.shade(pos.x - radius, pos.y - radius, pos.x + radius, pos.y + radius,
                    [&](double x, double y, int sx, int sy) {
                        const double dx = x - pos.x, dy = y - pos.y;
                        const int    i = int(std::floor(dx * cx + dy * cy + 6)),
                                     j = int(std::floor(-dx * cy + dy * cx + 2.5));
                        if (i < 0 || j < 0 || i >= 12 || j >= 5) return;
                        const uint32_t c = palette[uint8_t(MISSILE[size_t(j)][size_t(i)])];
                        if (c) p.put(sx, sy, c);
                    });
            p.glow(pos.x - cx * 8, pos.y - cy * 8, 18, F[4], .5);
            p.glow(pos.x - cx * 7, pos.y - cy * 7, 6, F[7], .5);
        }
        const double k = t - FLIGHT;
        if (k >= 0 && k < .07) {
            const double r = 14 * (.8 + .2 * (1 - k / .07));
            p.circle(0, -6, r + 1.2, F[3]);
            p.circle(0, -6, r, k < .035 ? F[7] : F[6]);
            p.circle(0, -6, r * .6, k < .035 ? WHITE : F[7]);
        }
        if (k >= 0 && k < .3) {
            const double rr = 8 + 30 * (1 - std::pow(1 - k / .3, 3));
            p.ring(0, -6, rr, std::max(1.0, 2.5 * (1 - k / .3) + .4), F[5], [&](double, double angle) {
                return -6 + std::sin(angle) * rr > 0 ? 0 : std::min(1.0, (1 - k / .3) * 1.6);
            });
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& homingMissileDescriptor() {
    static const std::array<Option, 3> options{
        {{"metal", 1, 64, false, 5, &PixelFxProjectileSettings::metal},
         {"smoke", 1, 64, false, 7, &PixelFxProjectileSettings::smoke},
         {"stone", 4278190080., 4294967295., false, 0, &PixelFxProjectileSettings::stone, true}}};
    static const Descriptor descriptor{
        {PixelFxKind::HomingMissile, "projectiles/homing-missile", 248, 140, 160, 120, 3.8, false},
        9,
        [](PixelFxSettings& s) {
            s.seed             = 8;
            s.palette          = {hex(0x4a1712), hex(0x8a1f17), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                  hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
            s.projectile.metal = {hex(0x25243a), hex(0x3e4058), hex(0x61667f), hex(0x8e97b0), hex(0xc4cce0)};
            s.projectile.smoke = {hex(0x26222e), hex(0x3b3644), hex(0x544e5e), hex(0x6f6979),
                                  hex(0x8c8596), hex(0xaca5b5), hex(0xcbc4d2)};
            s.projectile.stone = hex(0x4a3f69);
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<HomingMissile>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
