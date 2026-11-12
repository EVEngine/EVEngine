// eve-hot-path
// Adapted from pixelfx explosions/plasma-explosion.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Explosion.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct PlasmaExplosion final : Kernel {
    std::vector<uint32_t> C, V;
    uint32_t              seed;
    Rng                   rng;
    Particles             ps;
    Lights                lights;
    double                t = 0;
    explicit PlasmaExplosion(const PixelFxSettings& o)
        : C(o.palette), V(o.explosion.accent), seed(o.seed), rng(o.seed) {
        ps.sparks = {C[6], C[5], C[4], V[3]};
        lights.add(0, 0, 96, C[4], 1, .6);
        lights.add(0, 0, 40, WHITE, .9, .1);
        lights.add(0, 0, 70, V[2], .5, 1.1);
        const std::array<uint32_t, 4> motes{C[6], C[5], C[4], V[3]};
        for (int i = 0; i < 46; ++i) {
            const double a = rng.range(0, TAU), sp = rng.range(140, 360);
            emit({ParticleKind::Mote, std::cos(a) * 4, std::sin(a) * 4, std::cos(a) * sp, std::sin(a) * sp,
                  rng.range(.2, .42), rng.pick<uint32_t>(motes), rng.range(.7, 1.1)});
        }
        const std::array<uint32_t, 3> flakes{C[5], V[3], V[4]};
        for (int i = 0; i < 26; ++i) {
            const double a = rng.range(0, TAU), sp = rng.range(40, 140);
            emit({ParticleKind::Flake, std::cos(a) * 10, std::sin(a) * 10, std::cos(a) * sp, std::sin(a) * sp,
                  rng.range(.8, 1.5), rng.pick<uint32_t>(flakes), rng.range(.6, 1.6)});
        }
    }
    void          emit(Emission q) { ps.spawn(rng, q.kind, q.x, q.y, q.vx, q.vy, q.life, q.color, q.size); }
    static double shell(double time) { return 5 + 30 * ease::outCubic(clamp(time / .55)); }
    void          update(double dt) override {
        t += dt;
        if (t > .12 && t < .9 && rng.isChance(30 * dt)) {
            const double a = rng.range(0, TAU), r = shell(t);
            emit({ParticleKind::Star, std::cos(a) * r, std::sin(a) * r, std::cos(a) * rng.range(10, 40),
                  std::sin(a) * rng.range(10, 40), rng.range(.3, .6), rng.isChance(.5) ? C[5] : V[4],
                  rng.range(.5, 1.8)});
        }
        ps.update(dt, rng);
        explosion::cull(ps, -109, -97, 109, 97);
        lights.update(dt);
    }
    void tilted(Paint& p, bool back) const {
        const double u = (t - .03) / .5;
        if (u <= 0 || u >= 1) return;
        const std::array<uint32_t, 4> ramp{V[1], V[2], C[4], C[5]};
        band(
            p, 0, 4, 12 + 86 * ease::outCubic(u), .28, std::max(1., 4 * (1 - u)), 1.15 - u, ramp,
            [&](double g) { return (std::sin(g) < 0) == back; }, u < .15);
    }
    void arc(Paint& p, std::span<const Point> path, double k) const {
        if (k <= .05) return;
        for (size_t i = 1; i < path.size(); ++i) {
            const auto &a = path[i - 1], &b = path[i];
            p.glowLine(a.x, a.y, b.x, b.y, 5, V[2], .18 * k);
            p.line(a.x + 1, a.y, b.x + 1, b.y, C[4], k * .8);
            p.line(a.x, a.y, b.x, b.y, i < path.size() * .6 ? WHITE : C[5], std::min(1., k * 1.4));
        }
    }
    void drawBack(Paint& p) const override { tilted(p, true); }
    void draw(Paint& p) const override {
        if (t < .55) {
            const double u = t / .55, r = shell(t), hollow = smooth(.35, 1, u), boil = std::floor(t * 20);
            p.shader(-r - 3, -r - 3, r + 3, r + 3, [&](double x, double y, ShadeOutput& out) {
                const double d = jsHypot2(x, y), a = std::atan2(y, x),
                             edge = r * (1 + .1 * noise(a * 3 + boil * .7, 3));
                if (d > edge) return;
                const double v = d / edge;
                if (v < hollow * .92) return;
                const double n = noise(a * 4 + d * .25 + boil * .5, 5), w = v + n * .14;
                const bool   fil = std::abs(std::sin(a * 5 + d * .32 - t * 14 + n * 2.5)) < .1;
                out.k            = .25 * (1 - u);
                out.c            = v > .9              ? V[2]
                                   : fil && v > .3     ? (u < .5 ? WHITE : C[5])
                                   : w < .32 - u * .25 ? WHITE
                                   : w < .52           ? C[5]
                                   : w < .72           ? C[4]
                                   : w < .86           ? (u < .5 ? C[3] : V[3])
                                                       : V[2];
            });
            p.glow(0, 0, r * 1.6 + 6, C[4], .6 * (1 - u));
        }
        if (t < .07) p.circle(0, 0, 9 - t * 60, WHITE);
        if (t < .25) flare(p, 0, 0, 52 * std::pow(1 - t / .25, 1.3), 1 - t / .25, C);
        const double u1 = t / .42;
        if (u1 < 1) {
            const double r = 8 + 84 * ease::outCubic(u1);
            p.ring(0, 0, r, std::max(1., 3 * (1 - u1)), u1 < .25 ? WHITE : C[5], 1.15 - u1);
            p.glowRing(0, 0, r, 5, C[4], .35 * (1 - u1));
        }
        const double u2 = (t - .06) / .6;
        if (u2 > 0 && u2 < 1) p.ring(0, 0, 6 + 60 * ease::outCubic(u2), std::max(1., 2 * (1 - u2)), V[3], 1.1 - u2);
        const double   left   = 1 - smooth(.15, .85, t);
        const uint32_t strike = jsUint(std::floor(t * 18));
        if (left > 0) {
            Rng       r(hash({seed, strike, 17}));
            const int n = 2 + int(jsRound(4 * left));
            for (int i = 0; i < n; ++i) {
                const double a = r.range(0, TAU), L = r.range(26, 58) * (.45 + .55 * left);
                const auto   path = bolt(r, 0, 0, std::cos(a) * L, std::sin(a) * L, .28, 4);
                arc(p, path, .8 * left);
                if (r.isChance(.5))
                    for (const auto& f : forks(r, path, 1, 14, .35)) arc(p, f, .5 * left);
            }
            const double rs = shell(t);
            for (int i = 0; i < 3; ++i) {
                const double a = r.range(0, TAU), b = a + r.range(.3, .6);
                const auto   path =
                    bolt(r, std::cos(a) * rs, std::sin(a) * rs, std::cos(b) * rs, std::sin(b) * rs, .4, 3);
                arc(p, path, .6 * left);
            }
        }
        tilted(p, false);
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& plasmaExplosionDescriptor() {
    static const std::array<Option, 1> options{{{"accent", 1, 64, false, 5, &PixelFxExplosionSettings::accent}}};
    static const Descriptor            descriptor{
        {PixelFxKind::PlasmaExplosion, "explosions/plasma-explosion", 224, 200, 112, 100, 1.7, false},
        7,
        [](PixelFxSettings& s) {
            s.seed             = 7;
            s.palette          = {hex(0x0a0828), hex(0x1a1c6e), hex(0x2c3cc0), hex(0x4a7cf0),
                                  hex(0x7cc0ff), hex(0xc8ecff), WHITE};
            s.explosion.accent = {hex(0x1c0a30), hex(0x4a1a8c), hex(0x8a3ce0), hex(0xc48cff), hex(0xf0d8ff)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<PlasmaExplosion>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
