// eve-hot-path
// Adapted from pixelfx projectiles/flare.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Projectile.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double RISE = .85, BURN = 3.1;
struct SignalFlare final : Kernel {
    std::vector<uint32_t>   C, SM;
    std::array<uint32_t, 7> warm;
    double                  height, t = 0, emitCount = 0, drip = 0;
    bool                    burst = false;
    Rng                     rng;
    Particles               ps;
    projectile::Smoke       smoke;
    Lights                  lights;
    explicit SignalFlare(const PixelFxSettings& o)
        : C(o.palette),
          SM(o.projectile.smoke),
          warm{{C[0], C[1], mix(C[2], SM[2], .45), mix(C[3], SM[3], .35), C[4], C[5], C[6]}},
          height(o.projectile.height),
          rng(o.seed) {
        ps.sparks = {WHITE, C[6], C[4], C[2]};
        lights.add(0, -3, 26, C[4], .8, .25);
        for (int i = 0; i < 5; ++i) {
            projectile::SmokeParticle q;
            q.x    = rng.range(-3, 3);
            q.y    = -2;
            q.vx   = rng.range(-25, 25);
            q.vy   = -rng.range(5, 20);
            q.r0   = 2;
            q.r1   = rng.range(4, 6);
            q.life = rng.range(1, 1.6);
            q.a    = .5;
            q.warm = .8;
            q.rise = 6;
            smoke.add(rng, q);
        }
    }
    Point rise(double time) const {
        return {std::sin(time * 3) * 2 * (time / RISE), -height * ease::outCubic(clamp(time / RISE))};
    }
    Point star(double time) const {
        const double u = time - RISE;
        return {std::sin(u * 1.6) * 3 + rise(RISE).x, -height + u * 9 + std::sin(u * 2.3) * 1.5};
    }
    void spark(double x, double y, double vx, double vy, double life) {
        ps.spawn(rng, ParticleKind::Spark, x, y, vx, vy, life);
    }
    void update(double dt) override {
        t += dt;
        if (t < RISE) {
            const auto p = rise(t), prev = rise(std::max(0.0, t - dt));
            for (emitCount += jsHypot2(p.x - prev.x, p.y - prev.y) / 1.5; emitCount > 1; --emitCount) {
                projectile::SmokeParticle q;
                q.x    = p.x + rng.range(-.6, .6);
                q.y    = p.y + 4;
                q.vx   = rng.range(-6, 6);
                q.vy   = rng.range(0, 8);
                q.r0   = 1.3;
                q.r1   = rng.range(3, 4.8);
                q.life = rng.range(1.4, 2.1);
                q.a    = .5;
                q.b    = .95;
                q.warm = .9;
                q.rise = 3;
                q.drag = 2;
                q.grow = 1.4;
                smoke.add(rng, q);
            }
            if (rng.isChance(.5)) {
                const double vx = rng.range(-40, 40), vy = rng.range(20, 90), life = rng.range(.2, .45);
                spark(p.x, p.y + 3, vx, vy, life);
            }
        } else if (!burst) {
            burst        = true;
            const auto p = star(t);
            lights.add(p.x, p.y, 64, C[4], .9, .5);
            lights.add(p.x, p.y, 24, WHITE, .8, .12);
            for (int i = 0; i < 30; ++i) {
                const double a = rng.range(0, TAU), sp = rng.range(70, 190), life = rng.range(.25, .7);
                spark(p.x, p.y, std::cos(a) * sp, std::sin(a) * sp - 30, life);
            }
            for (int i = 0; i < 10; ++i) {
                projectile::SmokeParticle q;
                q.x    = p.x + rng.range(-3, 3);
                q.y    = p.y + rng.range(-3, 3);
                q.vx   = rng.range(-40, 40);
                q.vy   = rng.range(-40, 10);
                q.r0   = 2.5;
                q.r1   = rng.range(6, 9);
                q.life = rng.range(1.6, 2.4);
                q.a    = .55;
                q.warm = 1;
                q.rise = 10;
                q.drag = 2.5;
                smoke.add(rng, q);
            }
        } else if (t < RISE + BURN) {
            const auto   p = star(t);
            const double k = (t - RISE) / BURN, power = 1 - smooth(.75, 1, k);
            for (drip += dt * 26 * power; drip > 1; --drip) {
                const double x = p.x + rng.range(-1.5, 1.5), vx = rng.range(-30, 30), vy = rng.range(10, 60),
                             life = rng.range(.35, .8);
                spark(x, p.y + 2, vx, vy, life);
            }
            if (rng.isChance(dt * 18 * power)) {
                projectile::SmokeParticle q;
                q.x    = p.x + rng.range(-1.5, 1.5);
                q.y    = p.y - 2;
                q.vx   = rng.range(-8, 8);
                q.vy   = -rng.range(15, 30);
                q.r0   = 2;
                q.r1   = rng.range(6, 10);
                q.life = rng.range(1.6, 2.4) * (1 - .4 * k);
                q.a    = .5;
                q.b    = .85;
                q.warm = 1;
                q.rise = 14;
                q.drag = 1.6;
                q.grow = 1.2;
                smoke.add(rng, q);
            }
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        smoke.update(dt);
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        auto         list = smoke.puffs();
        const double lit  = t > RISE && t < RISE + BURN ? 1 - smooth(.75, 1, (t - RISE) / BURN) : 0;
        if (lit > 0) {
            const auto pos = star(t);
            for (auto& q : list) q.w = std::max(q.w, lit * std::exp(-jsHypot2(q.x - pos.x, q.y - pos.y) / 26));
        }
        projectile::cloud(p, list, SM, warm, 1.4, .8, 1.3);
        ps.draw(p);
        if (t < RISE) {
            const auto pos = rise(t);
            p.lineFn(
                pos.x, pos.y + 1, pos.x, pos.y + 5, [&](double u) { return u < .3   ? C[6]
                                                                           : u < .7 ? C[4]
                                                                                    : C[3]; },
                1.0);
            p.px(pos.x, pos.y, WHITE);
            p.px(pos.x, pos.y - 1, C[6]);
            p.glow(pos.x, pos.y, 14, C[4], .6);
            p.glow(pos.x, pos.y, 4, WHITE, .4);
        } else if (t < RISE + BURN) {
            const auto     pos = star(t);
            const double   u = t - RISE, k = u / BURN;
            const uint32_t fr = jsUint(std::floor(t * 30));
            const double   fl = .8 + .2 * rand({fr, 3}) + .08 * std::sin(u * 19), die = 1 - smooth(.78, 1, k);
            if (u < .25) {
                const double e = u / .25;
                flare(p, pos.x, pos.y, 22 * (1 - e) + 3, 1 - e, C);
                if (e < .25) p.circle(pos.x, pos.y, 5 * (1 - e * 3) + 1, WHITE);
            }
            const double r = (2 + .6 * fl) * (.4 + .6 * die);
            p.circle(pos.x, pos.y, r + 1.2, C[3]);
            p.circle(pos.x, pos.y, r, die > .4 ? C[5] : C[4]);
            if (die > .25) p.circle(pos.x - .3, pos.y - .3, r * .55, die > .6 ? WHITE : C[6]);
            if (die > .15 && rand({fr, 9}) > .15)
                p.sparkle(pos.x, pos.y, jsRound((5 + 3 * fl) * die), C[6], C[4], .5 * die);
            p.glow(pos.x, pos.y, 70 * (.6 + .4 * die), C[3], .55 * fl * die);
            p.glow(pos.x, pos.y, 22, C[4], .5 * fl * die);
            p.glow(pos.x, pos.y, 6, WHITE, .35 * die);
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& signalFlareDescriptor() {
    static const std::array<Option, 2> options{{{"height", 20, 195, false, 0, &PixelFxProjectileSettings::height},
                                                {"smoke", 1, 64, false, 7, &PixelFxProjectileSettings::smoke}}};
    static const Descriptor            descriptor{
        {PixelFxKind::SignalFlare, "projectiles/flare", 152, 232, 76, 226, 5, false},
        8,
        [](PixelFxSettings& s) {
            s.seed              = 2;
            s.projectile.height = 150;
            s.palette           = {hex(0x2a0610), hex(0x5c0d1c), hex(0x9a1428), hex(0xd8243a),
                                   hex(0xff4d5e), hex(0xff8a94), hex(0xffd0d4), WHITE};
            s.projectile.smoke  = {hex(0x1e1a26), hex(0x2e2938), hex(0x433d4d), hex(0x5b5466),
                                   hex(0x77707f), hex(0x958e9c), hex(0xb3acb9)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<SignalFlare>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
