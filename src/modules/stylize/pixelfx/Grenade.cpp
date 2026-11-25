// eve-hot-path
// Adapted from pixelfx explosions/grenade.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Explosion.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
    ParticleFlag flags = ParticleFlag::None;
};
struct Body {
    double x = -140, y = -70, vx = 168, vy = -150, angle = 0, va = -9;
    bool   rest = false;
};
struct Grenade final : Kernel {
    std::vector<uint32_t>    F, G;
    std::array<uint32_t, 9>  FR;
    std::array<uint32_t, 10> HR;
    Rng                      rng;
    Particles                ps;
    Lights                   lights;
    Bits                     bits;
    Body                     g;
    double                   t = 0, bx = 0;
    bool                     blown = false;
    explicit Grenade(const PixelFxSettings& o)
        : F(o.palette), G(o.explosion.body), FR(explosion::fireRamp(F)), HR(explosion::heatRamp(FR)), rng(o.seed) {
        ps.sparks = {F[6], F[5], F[3], F[2]};
    }
    void  emit(Emission q) { ps.spawn(rng, q.kind, q.x, q.y, q.vx, q.vy, q.life, q.color, q.size, q.delay, q.flags); }
    Point fuse(double x) const { return {x + std::sin(g.angle) * 10.4, g.y - std::cos(g.angle) * 10.4}; }
    void  blast() {
        blown          = true;
        bx             = g.x;
        const double y = -5;
        lights.add(bx, y - 10, 70, mix(F[3], F[6], .45), .85, .5);
        lights.add(bx, y, 30, WHITE, .7, .07);
        const std::array<uint32_t, 3> hot{F[7], F[6], F[5]};
        for (int i = 0; i < 38; ++i) {
            const double a = rng.range(-PI + .04, -.04), sp = rng.range(380, 640);
            emit({ParticleKind::Mote, bx, y, std::cos(a) * sp, std::sin(a) * sp, rng.range(.12, .26),
                  rng.pick<uint32_t>(hot), rng.range(.6, .9)});
        }
        for (int i = 0; i < 60; ++i) {
            const double a = rng.range(0, TAU), sp = rng.range(140, 360);
            emit({ParticleKind::Spark, bx, y, std::cos(a) * sp, std::sin(a) * sp - 60, rng.range(.2, .55)});
        }
        for (int i = 0; i < 4; ++i)
            emit({ParticleKind::Fire, bx + rng.range(-2, 2), y + rng.range(-2, 2), rng.range(-15, 15),
                  rng.range(-30, -8), rng.range(.16, .26), 0, rng.range(6, 8)});
        for (int i = 0; i < 16; ++i) {
            const double a = rng.range(-PI - .2, .2), d = rng.range(0, 6), sp = rng.range(40, 150);
            emit({ParticleKind::Fire, bx + std::cos(a) * d, y + std::sin(a) * d, std::cos(a) * sp,
                  std::sin(a) * sp * .8 - 30, rng.range(.45, .9), 0, rng.range(3.5, 7), 0,
                  rng.isChance(.5) ? ParticleFlag::Smokes : ParticleFlag::None});
        }
        for (int i = 0; i < 9; ++i) {
            const double a = rng.range(-PI, 0), d = rng.range(2, 10);
            emit({ParticleKind::Smoke, bx + std::cos(a) * d, y + std::sin(a) * d, std::cos(a) * 25, -rng.range(20, 60),
                  rng.range(1.4, 2.1), 0, rng.range(6, 10), rng.range(.05, .3), ParticleFlag::Big});
        }
        for (int i = 0; i < 30; ++i) {
            const double a = rng.range(0, TAU), sp = rng.range(30, 120);
            emit({ParticleKind::Ember, bx, y, std::cos(a) * sp, std::sin(a) * sp - 50, rng.range(.8, 1.8), F[3]});
        }
        const std::array<uint32_t, 3> metal{hex(0x3a3c46), hex(0x6a6e7c), hex(0xa8acb8)};
        for (int i = 0; i < 12; ++i) {
            const double a = rng.range(-PI + .2, -.2), sp = rng.range(150, 330);
            Debris       b;
            b.kind            = DebrisKind::Shard;
            b.x               = bx;
            b.y               = y;
            b.vx              = std::cos(a) * sp;
            b.vy              = std::sin(a) * sp;
            b.gravity         = 1;
            b.drag            = .5;
            b.bounce          = .3;
            b.life            = rng.range(1.4, 2.1);
            b.angle           = rng.range(0, TAU);
            b.angularVelocity = rng.range(-24, 24);
            b.size            = rng.range(2, 3.5);
            b.color           = rng.pick<uint32_t>(metal);
            bits.add(std::move(b));
        }
        const std::array<uint32_t, 3> grit{hex(0x5a4f6b), hex(0x3e3650), hex(0x776c88)};
        for (int i = 0; i < 70; ++i) {
            const double u = rng.range(-1, 1), a = -PI / 2 + u * .8, sp = rng.range(100, 300) * (1 - .4 * std::abs(u));
            emit({ParticleKind::Grit, bx + u * 4, -1, std::cos(a) * sp, std::sin(a) * sp, rng.range(1, 1.8),
                  rng.pick<uint32_t>(grit), rng.isChance(.35) ? 2. : 1.});
        }
        for (int i = 0; i < 10; ++i) {
            const int side = rng.sign();
            emit({ParticleKind::Smoke, bx + side * rng.range(0, 6), -rng.range(1, 4), side * rng.range(60, 180),
                  -rng.range(2, 10), rng.range(1, 1.7), hex(0x7d7280), rng.range(5, 8), rng.range(0, .05),
                  ParticleFlag::Big});
        }
    }
    void update(double dt) override {
        t += dt;
        if (!blown) {
            if (!g.rest) {
                g.vy += 900 * dt;
                g.x += g.vx * dt;
                g.y += g.vy * dt;
                if (g.y >= -6.2 && g.vy > 0) {
                    g.y = -6.2;
                    if (g.vy > 90) {
                        for (int i = 0; i < 4; ++i)
                            emit({ParticleKind::Dust, g.x + rng.range(-2, 2), -.5, rng.signedRange(10, 40),
                                  -rng.range(4, 16), rng.range(.3, .6), hex(0x7d7280), rng.range(1.5, 2.6)});
                        for (int i = 0; i < 3; ++i)
                            emit({ParticleKind::Spark, g.x, -1, rng.range(-60, 60), -rng.range(40, 120),
                                  rng.range(.08, .16)});
                        g.vy = -g.vy * .36;
                        g.vx *= .8;
                    } else
                        g.vy = 0;
                }
                if (g.y >= -6.2 - .01 && g.vy == 0) {
                    g.vx -= g.vx * 2.4 * dt;
                    g.va = -g.vx / 6.2;
                    if (std::abs(g.vx) < 3) {
                        g.rest = true;
                        g.vx   = 0;
                    }
                }
                g.angle += g.va * dt;
            }
            const auto   tip   = fuse(g.x);
            const double hurry = smooth(1.62 - .5, 1.62, t);
            if (rng.isChance((24 + 50 * hurry) * dt))
                emit({ParticleKind::Spark, tip.x, tip.y, rng.range(-50, 50), -rng.range(30, 110), rng.range(.08, .2)});
            if (rng.isChance(9 * dt))
                emit({ParticleKind::Vapour, tip.x, tip.y - 1, rng.range(-4, 4), -rng.range(10, 20), rng.range(.5, .9),
                      hex(0x8a8494), rng.range(1.2, 2)});
            if (t >= 1.62) blast();
        }
        bits.update(dt, [](double) { return 0.; });
        std::erase_if(bits.list, [](const auto& b) { return b.x < -154 || b.x > 134 || b.y < -164; });
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        explosion::cull(ps, -157, -167, 137, 27);
        explosion::fadeEdges(ps, -157, 137, 24, 27);
        lights.update(dt);
    }
    void groundRing(Paint& p, bool back) const {
        const double k = (t - 1.62) / .4;
        if (k >= 1) return;
        const std::array<uint32_t, 4> ramp{hex(0x4a3f69), hex(0x6e6390), F[3], F[6]};
        band(p, bx, .5, 70 * ease::outCubic(k), .16, std::max(1., 3.5 * (1 - k)), 1.15 - k, ramp,
             [&](double a) { return back ? std::sin(a) < 0 : std::sin(a) >= 0; });
    }
    void drawBack(Paint& p) const override {
        if (blown) {
            const double u = t - 1.62;
            explosion::scorch(p, HR, u, 22, 1.6, 1 - smooth(4.2 - 1.62 - .8, 4.2 - 1.62 - .15, u), 2);
            groundRing(p, true);
        }
        explosion::drawPs(p, ps, Paint::Layer::Back, FR);
    }
    void drawBody(Paint& p, double x) const {
        const double                  ca = std::cos(g.angle), sa = std::sin(g.angle);
        const std::array<uint32_t, 4> M{hex(0x2a2c36), hex(0x5e6270), hex(0x9a9eac), hex(0xd0d4de)};
        p.shader(x - 13, g.y - 13, x + 13, g.y + 13, [&](double px, double py, ShadeOutput& out) {
            const double dx = px - x, dy = py - g.y, u = dx * ca + dy * sa, v = -dx * sa + dy * ca;
            if (v < -5 && v > -10.2 && std::abs(u) < 2.4) {
                out.c = v < -9.4 || std::abs(u) > 1.7 ? M[0] : u < -.6 ? M[3] : M[2];
                return;
            }
            const double pin = jsHypot2(u + 3.4, v + 8);
            if (pin < 1.9 && pin > .9) {
                out.c = M[2];
                return;
            }
            if (u > 2.2 && u < 3.6 && v > -8.6 && v < 2) {
                out.c = u > 3 ? M[0] : M[1];
                return;
            }
            const double e = std::pow(u / 5.1, 2) + std::pow((v - .6) / 6.4, 2);
            if (e > 1.2) return;
            if (e > 1) {
                out.c = hex(0x1a1422);
                return;
            }
            const double lit = -(dx * .6 + dy * .8) / 6;
            const bool   gv  = std::abs(std::fmod(v + 30, 3.1) - 1.55) < .45,
                         gu  = std::abs(std::fmod(u + 30, 3.4) - 1.7) < .45;
            const int i = int(clamp(jsRound(2 + lit * 1.7 - (gv || gu ? 1 : 0) + (e < .3 && lit > .3 ? 1 : 0)), 0, 4));
            out.c       = G[i];
        });
    }
    void draw(Paint& p) const override {
        if (!blown) {
            const double shake = smooth(1.62 - .45, 1.62, t), jx = shake * std::sin(t * 90) * .8;
            drawBody(p, g.x + jx);
            const auto tip = fuse(g.x + jx);
            p.px(tip.x, tip.y, std::sin(t * 50) > 0 ? F[6] : F[5]);
            p.glow(tip.x, tip.y, 6 + 3 * shake, F[4], .5 + .3 * std::sin(t * 37));
        } else
            groundRing(p, false);
        bits.draw(p);
        explosion::drawPs(p, ps, Paint::Layer::Front, FR);
        if (blown) {
            const double u = t - 1.62;
            if (u < .07) {
                const double k = 1 - u / .07, r = 11 * (.8 + .2 * k);
                p.circle(bx, -6, r + 1.2, F[2]);
                p.circle(bx, -6, r, k > .45 ? F[6] : F[5]);
                p.circle(bx, -6, r * .6, k > .45 ? WHITE : F[6]);
            }
            if (u < .2) flare(p, bx, -6, 40 * std::pow(1 - u / .2, 1.4), 1 - u / .2, F);
            if (u < .3) {
                const double k = u / .3, e = 1 - std::pow(1 - k, 3), r = 8 + 40 * e;
                p.ring(bx, -6, r, std::max(1., 2.5 * (1 - k) + .4), F[5],
                       [&](double, double an) { return std::sin(an) * r > 9 ? 0 : std::min(1., (1 - k) * 1.6); });
            }
            explosion::pool(p, bx, 1, 70, 12, F[3], .55 * std::max(0., 1 - u / .6));
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& grenadeDescriptor() {
    static const std::array<Option, 1> options{{{"body", 1, 64, false, 5, &PixelFxExplosionSettings::body}}};
    static const Descriptor            descriptor{
        {PixelFxKind::Grenade, "explosions/grenade", 300, 200, 160, 170, 4.2, false},
        8,
        [](PixelFxSettings& s) {
            s.seed           = 6;
            s.palette        = {hex(0x4a1712), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
            s.explosion.body = {hex(0x141a10), hex(0x2c3a1e), hex(0x4a5c30), hex(0x6e8446), hex(0x9ab06a)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Grenade>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
