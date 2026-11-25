// eve-hot-path
// Adapted from pixelfx projectiles/cannonball.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Projectile.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double ANG = -.32;
struct Cannonball final : Kernel {
    std::vector<uint32_t>  F, SM, IR;
    double                 height, my, t = 0, wisp = 0, x = 4, y, vx, vy;
    bool                   out = false;
    Rng                    rng;
    Particles              ps;
    projectile::Smoke      smoke;
    Lights                 lights;
    std::array<double, 16> jitter{};
    explicit Cannonball(const PixelFxSettings& o)
        : F(o.palette),
          SM(o.projectile.smoke),
          IR(o.projectile.iron),
          height(o.projectile.height),
          my(-height),
          y(my),
          vx(std::cos(ANG) * 330),
          vy(std::sin(ANG) * 330),
          rng(o.seed) {
        ps.sparks = {F[7], F[6], F[4], F[3]};
        for (auto& q : jitter) q = rng.next();
        lights.add(12, my - 2, 44, hex(0xffb060), 1, .35);
        lights.add(6, my, 18, WHITE, .9, .08);
        for (int i = 0; i < 22; ++i) {
            const double a = ANG + rng.range(-.45, .45), sp = rng.range(140, 380), life = rng.range(.1, .4);
            ps.spawn(rng, ParticleKind::Spark, 6, my, std::cos(a) * sp, std::sin(a) * sp, life);
        }
        for (int i = 0; i < 10; ++i) {
            const double a = ANG + rng.range(-.3, .3), sp = rng.range(70, 170), life = rng.range(.5, .9);
            ps.spawn(rng, ParticleKind::Spark, 6, my, std::cos(a) * sp, std::sin(a) * sp, life);
        }
        for (int i = 0; i < 8; ++i) {
            const double a = ANG + rng.range(-.5, .3), sp = rng.range(40, 110), life = rng.range(.7, 1.4);
            ps.spawn(rng, ParticleKind::Ember, 8, my, std::cos(a) * sp, std::sin(a) * sp, life, F[5], 2);
        }
        for (int i = 0; i < 34; ++i) {
            const double              a = ANG + rng.range(-.4, .35), sp = rng.range(40, 260) * (i < 12 ? 1.1 : .75);
            projectile::SmokeParticle q;
            q.x     = 6 + rng.range(0, 6);
            q.y     = my + rng.range(-2, 2);
            q.vx    = std::cos(a) * sp;
            q.vy    = std::sin(a) * sp;
            q.r0    = rng.range(3, 4.5);
            q.r1    = rng.range(9, 16);
            q.life  = rng.range(2.7, 3.5);
            q.a     = rng.range(.6, .75);
            q.b     = rng.range(.9, 1);
            q.drag  = rng.range(2.2, 3.2);
            q.rise  = rng.range(7, 13);
            q.grow  = rng.range(1.5, 2.4);
            q.curl  = rng.range(-1.2, .4);
            q.warm  = 1;
            q.delay = rng.range(0, .05);
            smoke.add(rng, q);
        }
    }
    void update(double dt) override {
        t += dt;
        if (!out) {
            const double x0 = x, y0 = y;
            vy += 260 * dt;
            x += vx * dt;
            y += vy * dt;
            for (wisp += jsHypot2(x - x0, y - y0) / 2; wisp > 1; --wisp) {
                const double f = rng.next(), px = x0 + (x - x0) * f, py = y0 + (y - y0) * f;
                if (px < 149 && px > 24 && t < .6) {
                    projectile::SmokeParticle q;
                    q.x    = px - 3;
                    q.y    = py;
                    q.vx   = -vx * .04;
                    q.vy   = -vy * .04;
                    q.r0   = 1;
                    q.r1   = rng.range(2.5, 3.6) * (1 - t);
                    q.life = rng.range(.7, 1.1);
                    q.a    = .5 * (1 - t);
                    q.b    = .95;
                    q.drag = 2;
                    q.rise = 5;
                    q.grow = 2;
                    smoke.add(rng, q);
                }
            }
            if (x > 157 || y < -99) out = true;
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        smoke.update(dt);
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const double pop  = std::exp(-(t + .008) / .06);
        auto         list = smoke.puffs();
        for (auto& q : list) q.w *= .2 + pop * 1.3;
        const std::array<uint32_t, 7> warm{
            {SM[0], mix(F[1], SM[1], .5), mix(F[2], SM[2], .45), F[3], F[4], F[6], F[7]}};
        projectile::cloud(p, list, SM, warm, 1.35, .86, 1.3);
        ps.draw(p);
        if (!out) {
            p.circle(x, y, 3.2, IR[0]);
            p.circle(x, y, 2.6, IR[1]);
            p.circle(x - .7, y - .7, 1.8, IR[2]);
            p.px(x - 1, y - 1.5, IR[3]);
            p.px(x - 2, y - 1, IR[3]);
            p.px(x + 1.5, y + 1, t < .3 ? F[5] : IR[2]);
            p.glow(x, y, 6, F[4], .45 * clamp(1 - t / .5));
        }
        if (t < 3.0 / 30) {
            const int                     f = t < 1.0 / 30 ? 1 : t < 2.0 / 30 ? 2 : 3;
            const double                  k = (f == 1 ? 1 : f == 2 ? 1.15 : .8) * 1.3;
            std::vector<projectile::Lobe> L{{0, 0, 12 * k, 5 * k, .55}, {2, 0, (24 + jitter[0] * 8) * k, 2.2 * k, .4}};
            for (int i = 0; i < 6; ++i) {
                const double a    = (i < 3 ? -1 : 1) * (.4 + (i % 3) * .45 + jitter[size_t(i) + 1] * .2),
                             down = std::sin(ANG + a);
                double       len  = (8 + jitter[size_t(i) + 7] * 7) * k * (f == 3 ? .7 : 1);
                if (down > .1) len = std::min(len, (height - 3) / down);
                L.push_back({2, a, len, 1.6 * k, .25});
            }
            const std::array<uint32_t, 4> C = f == 1   ? std::array<uint32_t, 4>{WHITE, F[7], F[6], F[3]}
                                              : f == 2 ? std::array<uint32_t, 4>{F[7], F[6], F[4], F[2]}
                                                       : std::array<uint32_t, 4>{F[5], F[4], F[3], F[1]};
            projectile::flash(p, 4, my, ANG, L, C, uint32_t(f));
            if (f == 1) {
                p.circle(7, my - 1, 6.5, WHITE);
                p.ring(7, my - 1, 7.5, 1, F[7]);
            }
        }
        lights.draw(p);
        p.glow(14, 0, 22, F[4], .25 * pop);
    }
};
}  // namespace
const Descriptor& cannonballDescriptor() {
    static const std::array<Option, 3> options{{{"height", 4, 90, false, 0, &PixelFxProjectileSettings::height},
                                                {"smoke", 1, 64, false, 7, &PixelFxProjectileSettings::smoke},
                                                {"iron", 1, 64, false, 4, &PixelFxProjectileSettings::iron}}};
    static const Descriptor            descriptor{
        {PixelFxKind::Cannonball, "projectiles/cannonball", 208, 116, 48, 102, 3.8, false},
        9,
        [](PixelFxSettings& s) {
            s.seed              = 4;
            s.projectile.height = 12;
            s.palette           = {hex(0x4a1712), hex(0x8a1f17), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                   hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
            s.projectile.smoke  = {hex(0x3a3542), hex(0x55505e), hex(0x736d7c), hex(0x938d9b),
                                   hex(0xb3adba), hex(0xd2cdd6), hex(0xece8ee)};
            s.projectile.iron   = {hex(0x141218), hex(0x2c2a34), hex(0x4c4a58), hex(0x8a8898)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Cannonball>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
