// eve-hot-path
// Adapted from pixelfx projectiles/muzzle-shotgun.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Projectile.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
struct Pellet {
    double angle, speed, offset;
};
std::vector<projectile::Lobe> fan(const std::array<double, 32>& R, int f) {
    const double                  k = f == 1 ? 1 : f == 2 ? .82 : .55, w = f == 1 ? 1 : 1.25;
    std::vector<projectile::Lobe> L{{.5, 0, 11 * k, 3.6 * w, .5}};
    for (int i = 0; i < 9; ++i) {
        const double u = double(i) / 8 - .5, a = u * 1.15 + R[size_t(i) * 3] * .12,
                     len = (17 + 11 * (1 - std::abs(u) * 1.4) + R[size_t(i) * 3 + 1] * 7) * k;
        L.push_back({2, a, len, (1.7 + R[size_t(i) * 3 + 2]) * w, f == 1 ? .32 : 0});
    }
    if (f == 1)
        for (int s : {-1, 1}) L.push_back({1, s * (1.6 + R[size_t(27 + (s > 0 ? 1 : 0))] * .4), 5 + R[29] * 3, 1.2, 0});
    return L;
}
struct MuzzleShotgun final : Kernel {
    std::vector<uint32_t>  F, SM, SH;
    double                 MY, t = 0;
    Rng                    rng;
    Particles              ps;
    projectile::Smoke      smoke;
    std::array<double, 32> jitter{};
    std::array<Pellet, 10> pellets{};
    double                 shellVx, shellVy, shellSpin, shellAngle;
    explicit MuzzleShotgun(const PixelFxSettings& o)
        : F(o.palette), SM(o.projectile.smoke), SH(o.projectile.shell), MY(-o.projectile.height), rng(o.seed) {
        ps.sparks = {F[7], F[6], F[4], F[3]};
        for (auto& x : jitter) x = rng.next();
        for (auto& p : pellets) p = {rng.range(-.13, .13), rng.range(900, 1150), rng.range(0, 4)};
        for (int i = 0; i < 16; ++i) {
            const double a = rng.range(-.55, .55), sp = rng.range(180, 460), life = rng.range(.08, .3);
            ps.spawn(rng, ParticleKind::Spark, 4, MY, std::cos(a) * sp, std::sin(a) * sp - 20, life);
        }
        for (int i = 0; i < 7; ++i) {
            const double a = rng.range(-.5, .3), sp = rng.range(70, 200), life = rng.range(.4, .9);
            ps.spawn(rng, ParticleKind::Ember, 4, MY, std::cos(a) * sp, std::sin(a) * sp, life, F[5]);
        }
        for (int i = 0; i < 26; ++i) {
            const double              a = rng.range(-.45, .35), sp = rng.range(60, 230) * (i < 8 ? 1.15 : .8);
            projectile::SmokeParticle q;
            q.x     = 4 + rng.range(0, 4);
            q.y     = MY + rng.range(-1.5, 1.5);
            q.vx    = std::cos(a) * sp;
            q.vy    = std::sin(a) * sp;
            q.r0    = rng.range(2.5, 3.5);
            q.r1    = rng.range(7, 12.5);
            q.life  = rng.range(2.1, 2.9);
            q.a     = rng.range(.55, .72);
            q.b     = rng.range(.9, 1);
            q.drag  = rng.range(2.6, 3.6);
            q.rise  = rng.range(9, 15);
            q.grow  = rng.range(1.8, 2.8);
            q.warm  = 1;
            q.delay = rng.range(0, .04);
            smoke.add(rng, q);
        }
        for (int i = 0; i < 6; ++i) {
            projectile::SmokeParticle q;
            q.x     = 2;
            q.y     = MY;
            q.vx    = rng.range(15, 50);
            q.vy    = -rng.range(10, 25);
            q.r0    = 1.5;
            q.r1    = rng.range(4, 6);
            q.life  = rng.range(1.8, 2.4);
            q.a     = .5;
            q.b     = 1;
            q.drag  = 2;
            q.rise  = 10;
            q.curl  = -rng.range(1.5, 3);
            q.warm  = .6;
            q.delay = .08 + i * .05;
            smoke.add(rng, q);
        }
        shellVx        = -rng.range(30, 55);
        shellVy        = -rng.range(110, 150);
        const int side = rng.sign();
        shellSpin      = side * rng.range(16, 24);
        shellAngle     = rng.range(0, 6.28);
    }
    void update(double dt) override {
        t += dt;
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        smoke.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const double pop  = std::exp(-(t + .008) / .05);
        auto         list = smoke.puffs();
        for (auto& q : list) q.w *= .25 + pop * 1.2;
        const std::array<uint32_t, 7> warm{{SM[0], mix(F[1], SM[1], .5), mix(F[2], SM[2], .4), F[3], F[4], F[5], F[7]}};
        projectile::cloud(p, list, SM, warm, 1.4, .86, 1.4);
        if (t > .42 && t < 3.2 - .05) {
            const auto   k = projectile::casing(-9, MY, shellVx, shellVy, shellSpin, t - .42, 850, -1, .35, shellAngle);
            const double ca = std::cos(k.angle), sa = std::sin(k.angle), fade = clamp((3.2 - .05 - t) / .3);
            for (double d = -1.5; d <= 1.5; d += .5)
                p.dot(k.x + ca * d, k.y + sa * d - .5,
                      d > .8    ? (d > 1.2 ? SH[4] : SH[3])
                      : d > -.6 ? SH[2]
                                : SH[1],
                      fade);
            for (double d = -1; d <= 1; d += .5)
                p.dot(k.x + ca * d - sa, k.y + sa * d + ca - .5, d > .6 ? SH[3] : SH[0], fade);
            if (k.hit >= 0 && k.hit < .07) p.glow(k.x, k.y, 5, SH[4], .5 * (1 - k.hit / .07));
        }
        if (t < 3.0 / 30) {
            const int                     f = t < 1.0 / 30 ? 1 : t < 2.0 / 30 ? 2 : 3;
            const auto                    L = fan(jitter, f);
            const std::array<uint32_t, 4> C = f == 1   ? std::array<uint32_t, 4>{WHITE, F[7], F[6], F[3]}
                                              : f == 2 ? std::array<uint32_t, 4>{F[7], F[6], F[4], F[2]}
                                                       : std::array<uint32_t, 4>{F[5], F[4], F[3], F[1]};
            projectile::flash(p, 0, MY, 0, L, C, uint32_t(f + 9));
        }
        if (t < .2)
            for (const auto& q : pellets) {
                const double head = 6 + q.offset + q.speed * t, tail = std::max(4.0, head - 26), ca = std::cos(q.angle),
                             sa = std::sin(q.angle);
                for (double d = tail; d <= head; d += .5) {
                    const double x = ca * d;
                    if (x > 114) break;
                    const double u = (d - tail) / std::max(1.0, head - tail);
                    p.dot(x, MY + sa * d, u > .85 ? WHITE : u > .5 ? F[6] : F[4], u > .3 ? 1 : u / .3);
                }
                if (ca * head < 114) p.glow(ca * head, MY + sa * head, 4, F[6], .5);
            }
        ps.draw(p);
        p.glow(8, MY, 46, F[4], .95 * pop);
        p.glow(4, MY, 12, WHITE, .6 * pop);
        p.glow(10, 0, 30, F[4], .28 * pop);
    }
};
}  // namespace
const Descriptor& muzzleShotgunDescriptor() {
    static const std::array<Option, 3> options{{{"height", 1, 75, false, 0, &PixelFxProjectileSettings::height},
                                                {"shell", 1, 64, false, 5, &PixelFxProjectileSettings::shell},
                                                {"smoke", 1, 64, false, 7, &PixelFxProjectileSettings::smoke}}};
    static const Descriptor            descriptor{
        {PixelFxKind::MuzzleShotgun, "projectiles/muzzle-shotgun", 160, 88, 44, 82, 3.2, false},
        9,
        [](PixelFxSettings& s) {
            s.seed              = 5;
            s.projectile.height = 26;
            s.palette           = {hex(0x4a1712), hex(0x8a1f17), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                   hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
            s.projectile.smoke  = {hex(0x221e2a), hex(0x36313f), hex(0x4e4858), hex(0x6a6474),
                                   hex(0x888192), hex(0xa8a1b1), hex(0xc8c1cf)};
            s.projectile.shell  = {hex(0x4a0f14), hex(0x9a1e22), hex(0xd8423a), hex(0x8c5c1c), hex(0xe9b54a)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<MuzzleShotgun>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
