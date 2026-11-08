// eve-hot-path
// Adapted from pixelfx explosions/implosion.js (MIT).
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
struct Grit {
    double   angle, d, t0, T, spin;
    uint32_t color;
};
struct Implosion final : Kernel {
    std::vector<uint32_t> C;
    Rng                   rng;
    Particles             ps;
    Lights                lights;
    std::vector<Grit>     grit;
    double                t      = 0;
    bool                  popped = false;
    explicit Implosion(const PixelFxSettings& o) : C(o.palette), rng(o.seed) {
        ps.sparks = {C[6], C[5], C[4], C[3]};
        const std::array<uint32_t, 5> ramp{C[2], C[3], C[4], hex(0x4a4458), hex(0x6e6880)};
        for (int i = 0; i < 70; ++i) {
            const double t0 = rng.range(0, .4);
            grit.push_back({rng.range(0, TAU), rng.range(40, 86), t0, rng.range(.3, .8 - .04 - t0),
                            rng.signedRange(.6, 1.6), rng.pick<uint32_t>(ramp)});
        }
    }
    void emit(Emission q) { ps.spawn(rng, q.kind, q.x, q.y, q.vx, q.vy, q.life, q.color, q.size); }
    void update(double dt) override {
        t += dt;
        if (t < .8 - .26) {
            const double k = t / .8;
            for (int n = 0; n < 1 + int(std::floor(k * 4)); ++n) {
                if (!rng.isChance(.9)) continue;
                const double a = rng.range(0, TAU), D = rng.range(34, 82) * (1 - .3 * k), T = rng.range(.14, .24),
                             v = D * 3 / (std::exp(3 * T) - 1);
                emit({ParticleKind::Streak, std::cos(a) * D, std::sin(a) * D, -std::cos(a) * v, -std::sin(a) * v, T,
                      rng.isChance(.5) ? C[5] : C[4]});
            }
            if (rng.isChance(14 * dt)) {
                const double a = rng.range(0, TAU), D = rng.range(50, 80);
                emit({ParticleKind::Vapour, std::cos(a) * D, std::sin(a) * D, -std::cos(a) * 90, -std::sin(a) * 90,
                      rng.range(.4, .6), C[3], rng.range(3, 5)});
            }
        }
        if (!popped && t >= 1.02) {
            popped = true;
            lights.add(0, 0, 90, C[4], 1, .4);
            lights.add(0, 0, 30, WHITE, .9, .08);
            const std::array<uint32_t, 3> ramp{C[6], C[5], C[4]};
            for (int i = 0; i < 34; ++i) {
                const double a = rng.range(0, TAU), sp = rng.range(120, 330);
                emit({rng.isChance(.6) ? ParticleKind::Mote : ParticleKind::Star, 0, 0, std::cos(a) * sp,
                      std::sin(a) * sp, rng.range(.2, .45), rng.pick<uint32_t>(ramp), rng.range(.8, 1.8)});
            }
        }
        ps.update(dt, rng);
        explosion::cull(ps, -101, -89, 101, 89);
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const double   k    = clamp(t / .8);
        const uint32_t VOID = hex(0x05040a);
        if (t < 1.02) {
            const double vr = 64 * (1 - ease::inCubic(k)) + 3;
            if (k < 1) {
                p.disc(0, 0, vr, VOID, .35 + .35 * k);
                p.ring(0, 0, vr + 1, 1, k < .5 ? C[3] : C[4], .5 + .5 * k);
                p.glowRing(0, 0, vr, 5, C[4], .18 + .3 * k);
            }
            for (int i = 0; i < 4; ++i) {
                const double u = (t - i * .17) / .34;
                if (u <= 0 || u >= 1) continue;
                const double r = (70 - i * 8) * (1 - ease::inQuad(u)) + 2;
                p.ring(0, 0, r, u > .7 ? 2 : 1, u > .6 ? C[5] : C[3], .4 + .6 * u);
            }
            for (const auto& q : grit) {
                const double u = (t - q.t0) / q.T;
                if (u <= 0 || u >= 1) continue;
                const double d = q.d * (1 - ease::inCubic(u)), a = q.angle + q.spin * ease::inQuad(u) * 2,
                             x = std::cos(a) * d, y = std::sin(a) * d,
                             x2 = std::cos(a - q.spin * .1) * (d + 4 * u * u + .5),
                             y2 = std::sin(a - q.spin * .1) * (d + 4 * u * u + .5);
                p.lineFn(x2, y2, x, y, [&](double v) { return v < .5 ? mix(q.color, VOID, .4) : q.color; }, 1.);
            }
            const bool   still = t > .8;
            const double jit   = still ? (int(std::floor(t * 40)) % 2 ? .5 : -.5) : 0,
                         cr    = still ? 1.6 : 1 + 3.5 * (1 - k) * k * 4;
            p.disc(jit, 0, cr + 3.5, VOID, .85);
            p.circle(jit, 0, cr + 1.2, C[4]);
            p.circle(jit, 0, cr, WHITE);
            p.glow(0, 0, 10 + 26 * k, C[4], .3 + .5 * k);
            if (still) p.glow(0, 0, 8, WHITE, .4 + .2 * std::sin(t * 60));
        } else {
            const double q = (t - 1.02) / .5, e = ease::outCubic(clamp(q));
            if (q < .08) p.circle(0, 0, 6 * (1 - q / .08) + 1, WHITE);
            if (q < .35) {
                const double L = (1 - q / .35) * 34 + 3;
                for (int i = 0; i < 4; ++i) {
                    const double a = i * TAU / 4 + PI / 4;
                    for (double d = 0; d <= L; d += .5)
                        p.dot(std::cos(a) * d, std::sin(a) * d, d < L * .4 ? WHITE : C[5],
                              d < L * .7 ? 1 : 1 - (d / L - .7) / .3);
                }
                for (int i = 0; i < 4; ++i) {
                    const double a = i * TAU / 4 + PI / 4;
                    for (double d = 0; d <= L * .45; d += .5)
                        p.dot(std::cos(a + PI / 4) * d, std::sin(a + PI / 4) * d, C[5], 1 - d / (L * .45));
                }
            }
            if (q < 1) {
                p.ring(0, 0, 4 + 74 * e, std::max(1., 2.5 * (1 - q)), q < .2 ? WHITE : C[5], 1.15 - q);
                p.ring(0, 0, 2 + 40 * ease::outCubic(clamp(q * 1.4)), 1, C[4], 1 - q);
            }
            if (q < .6) p.sparkle(0, 0, 5 * (1 - q / .6), WHITE, C[5], .6);
        }
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& implosionDescriptor() {
    static const Descriptor descriptor{
        {PixelFxKind::Implosion, "explosions/implosion", 208, 184, 104, 92, 1.65, false},
        7,
        [](PixelFxSettings& s) {
            s.seed    = 2;
            s.palette = {hex(0x0a0614), hex(0x1c1236), hex(0x3a2470), hex(0x6a44c0),
                         hex(0xa080f0), hex(0xd8c8ff), WHITE};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Implosion>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
