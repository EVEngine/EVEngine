// eve-hot-path
// Adapted from pixelfx projectiles/muzzle-pistol.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Projectile.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
struct MuzzlePistol final : Kernel {
    std::vector<uint32_t> F, B, SM;
    double                H, MY, tilt, t = 0;
    uint32_t              seed, shapeSeed;
    Rng                   rng;
    Particles             ps;
    projectile::Smoke     smoke;
    double                shellVx, shellVy, shellSpin, shellAngle;
    explicit MuzzlePistol(const PixelFxSettings& o)
        : F(o.palette),
          B(o.projectile.brass),
          SM(o.projectile.smoke),
          H(o.projectile.height),
          MY(-H),
          seed(o.seed),
          rng(seed) {
        ps.sparks = {F[7], F[6], F[4], F[3]};
        shapeSeed = uint32_t(rng.integer(0, 1000000));
        tilt      = rng.range(-.05, .05);
        for (int i = 0; i < 6; ++i) {
            const double a = rng.range(-.3, .3), sp = rng.range(180, 400), life = rng.range(.06, .16);
            ps.spawn(rng, ParticleKind::Spark, 3, MY, std::cos(a) * sp, std::sin(a) * sp, life);
        }
        for (int i = 0; i < 5; ++i) {
            const double a = rng.range(-.35, .25), sp = rng.range(60, 160), life = rng.range(.25, .5);
            ps.spawn(rng, ParticleKind::Ember, 3, MY, std::cos(a) * sp, std::sin(a) * sp, life, F[5]);
        }
        for (int i = 0; i < 3; ++i) {
            projectile::SmokeParticle q;
            q.x     = 3;
            q.y     = MY;
            q.vx    = rng.range(70, 120);
            q.vy    = rng.range(-12, 6);
            q.r0    = 1.6;
            q.r1    = rng.range(4.5, 6);
            q.life  = rng.range(.8, 1.05);
            q.a     = .55;
            q.drag  = 3.5;
            q.rise  = 10;
            q.warm  = .9;
            q.delay = i * .012;
            smoke.add(rng, q);
        }
        for (int i = 0; i < 14; ++i) {
            const double              k = double(i) / 13;
            projectile::SmokeParticle q;
            q.x     = 2;
            q.y     = MY;
            q.vx    = 115 - 75 * k + rng.range(-12, 12);
            q.vy    = -6 - 10 * k + rng.range(-5, 5);
            q.r0    = 1.4;
            q.r1    = (3.6 + k * 2.6) * rng.range(.75, 1.25);
            q.life  = 1.15 + k * .3;
            q.a     = .62;
            q.b     = rng.range(.85, 1);
            q.drag  = 2.2;
            q.rise  = 6;
            q.curl  = -4.4 + k * 1.8 + rng.range(-.5, .5);
            q.grow  = 1.6;
            q.warm  = .7;
            q.delay = .02 + i * .022;
            smoke.add(rng, q);
        }
        projectile::SmokeParticle q;
        q.x     = -7;
        q.y     = MY - 2;
        q.vx    = -6;
        q.vy    = -18;
        q.r0    = 1.2;
        q.r1    = 4;
        q.life  = 1.05;
        q.a     = .4;
        q.rise  = 8;
        q.delay = .03;
        smoke.add(rng, q);
        shellVx        = -rng.range(25, 45);
        shellVy        = -rng.range(95, 130);
        const int side = rng.sign();
        shellSpin      = side * rng.range(22, 32);
        shellAngle     = rng.range(0, 6.28);
    }
    void update(double dt) override {
        t += dt;
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        smoke.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const double pop  = std::exp(-(t + .008) / .035);
        auto         list = smoke.puffs();
        for (auto& q : list) q.w *= .4 + pop;
        const std::array<uint32_t, 7> warm{{SM[0], mix(F[1], SM[1], .5), mix(F[2], SM[2], .4), F[3], F[4], F[5], F[7]}};
        projectile::cloud(p, list, SM, warm, 1.5, .8, 1.4);
        if (t < 1.8)
            projectile::drawCasing(
                p, projectile::casing(-7, MY - 1, shellVx, shellVy, shellSpin, t, 850, -.5, .42, shellAngle), B,
                std::min(1.0, (1.8 - .05 - t) / .3));
        if (t < 2.0 / 30) {
            const int                f = t < 1.0 / 30 ? 1 : 2;
            Rng                      g(hash({shapeSeed}));
            projectile::FlashOptions options;
            options.size                         = 1.15;
            options.spike                        = {15, 22};
            options.diagonal                     = {7, 12};
            options.cross                        = {4.5, 8};
            options.back                         = {3, 5.5};
            const auto                    lobes  = projectile::flashShape(g, f, options);
            const std::array<uint32_t, 4> colors = f == 1 ? std::array<uint32_t, 4>{WHITE, F[7], F[6], F[3]}
                                                          : std::array<uint32_t, 4>{F[7], F[6], F[4], F[2]};
            projectile::flash(p, 0, MY, tilt, lobes, colors, uint32_t(f) + seed);
        }
        ps.draw(p);
        p.glow(4, MY, 30, F[4], .85 * pop);
        p.glow(2, MY, 8, WHITE, .5 * pop);
        p.glow(4, 0, 16, F[4], .2 * pop);
    }
};
}  // namespace
const Descriptor& muzzlePistolDescriptor() {
    static const std::array<Option, 3> options{{{"height", 1, 60, false, 0, &PixelFxProjectileSettings::height},
                                                {"brass", 1, 64, false, 4, &PixelFxProjectileSettings::brass},
                                                {"smoke", 1, 64, false, 7, &PixelFxProjectileSettings::smoke}}};
    static const Descriptor            descriptor{
        {PixelFxKind::MuzzlePistol, "projectiles/muzzle-pistol", 96, 72, 44, 66, 1.8, false},
        9,
        [](PixelFxSettings& s) {
            s.seed              = 3;
            s.projectile.height = 26;
            s.palette           = {hex(0x4a1712), hex(0x8a1f17), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                   hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
            s.projectile.brass  = {hex(0x4a2e10), hex(0x8c5c1c), hex(0xd9a441), hex(0xffe39a)};
            s.projectile.smoke  = {hex(0x26222e), hex(0x3b3644), hex(0x544e5e), hex(0x6f6979),
                                   hex(0x8c8596), hex(0xaca5b5), hex(0xcbc4d2)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<MuzzlePistol>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
