// eve-hot-path
// Adapted from pixelfx explosions/barrel-explosion.js (MIT).
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
struct Plate : Debris {
    double heat = 0;
};
struct Lid {
    double x = 0, y = -22, vx = 0, vy = 0, angle = 0, va = 0;
    bool   on = false, rest = false;
};
struct Fuel {
    double x, w;
};
struct BarrelExplosion final : Kernel {
    std::vector<uint32_t>    F, C;
    std::array<uint32_t, 9>  FR;
    std::array<uint32_t, 10> HR;
    Rng                      rng;
    Particles                ps;
    Blazes                   blazes;
    Lights                   lights;
    BasicBits<Plate>         staves;
    Lid                      lid;
    std::vector<Fuel>        pools;
    double                   t     = 0;
    bool                     blown = false;
    explicit BarrelExplosion(const PixelFxSettings& o)
        : F(o.palette), C(o.explosion.barrel), FR(fireRamp(F)), HR(heatRamp(FR)), rng(o.seed) {
        ps.sparks = {F[6], F[5], F[3], F[2]};
    }
    void emit(Emission q) { ps.spawn(rng, q.kind, q.x, q.y, q.vx, q.vy, q.life, q.color, q.size, q.delay, q.flags); }
    void blow() {
        blown          = true;
        const double y = -11;
        lights.add(0, y - 20, 90, mix(F[3], F[6], .45), .85, .9);
        lights.add(0, y, 44, WHITE, .7, .09);
        for (int i = 0; i < 5; ++i)
            emit({ParticleKind::Fire, rng.range(-3, 3), y + rng.range(-3, 3), rng.range(-20, 20), rng.range(-40, -10),
                  rng.range(.18, .28), 0, rng.range(9, 12)});
        for (int i = 0; i < 30; ++i) {
            const double a = rng.range(-PI - .15, .15), d = rng.range(0, 8), sp = rng.range(50, 190);
            emit({ParticleKind::Fire, std::cos(a) * d, y + std::sin(a) * d, std::cos(a) * sp,
                  std::sin(a) * sp * .8 - 40, rng.range(.55, 1.05), 0, rng.range(5, 10), 0,
                  rng.isChance(.5) ? ParticleFlag::Smokes | ParticleFlag::Big : ParticleFlag::None});
        }
        for (int i = 0; i < 12; ++i)
            emit({ParticleKind::Fire, rng.range(-6, 6), y - rng.range(0, 10), rng.range(-14, 14), -rng.range(170, 300),
                  rng.range(.7, 1.15), 0, rng.range(6, 10), rng.range(0, .12),
                  rng.isChance(.7) ? ParticleFlag::Smokes | ParticleFlag::Big : ParticleFlag::None});
        for (int i = 0; i < 12; ++i) {
            const double a = rng.range(-PI, 0), d = rng.range(4, 16);
            emit({ParticleKind::Smoke, std::cos(a) * d, y + std::sin(a) * d, std::cos(a) * 30, -rng.range(30, 90),
                  rng.range(1.6, 2.6), hex(0x1f1a24), rng.range(8, 13), rng.range(.1, .4), ParticleFlag::Big});
        }
        for (int i = 0; i < 90; ++i) {
            const double a = rng.range(0, TAU), sp = rng.range(160, 400);
            emit({ParticleKind::Spark, 0, y, std::cos(a) * sp, std::sin(a) * sp - 70, rng.range(.2, .6)});
        }
        for (int i = 0; i < 40; ++i) {
            const double a = rng.range(0, TAU), sp = rng.range(30, 140);
            emit({ParticleKind::Ember, std::cos(a) * 8, y + std::sin(a) * 8, std::cos(a) * sp, std::sin(a) * sp - 70,
                  rng.range(1.2, 2.6), F[3]});
        }
        for (int i = 0; i < 14; ++i) {
            const double a = -PI / 2 + rng.range(-1.2, 1.2), sp = rng.range(120, 300);
            emit({ParticleKind::Brand, rng.range(-4, 4), y, std::cos(a) * sp, std::sin(a) * sp, rng.range(.8, 1.4)});
        }
        for (int i = 0; i < 9; ++i) {
            const int    side = i % 2 ? 1 : -1;
            const double a = -PI / 2 + side * rng.range(.25, 1.15), sp = rng.range(170, 330);
            Plate        b;
            b.kind            = DebrisKind::Chunk;
            b.x               = side * rng.range(2, 6);
            b.y               = -rng.range(4, 18);
            b.vx              = std::cos(a) * sp;
            b.vy              = std::sin(a) * sp;
            b.gravity         = 1;
            b.drag            = .5;
            b.bounce          = .35;
            b.life            = rng.range(3, 3.8);
            b.angle           = rng.range(0, TAU);
            b.angularVelocity = rng.range(-16, 16);
            b.size            = rng.range(9, 13);
            b.heat            = rng.range(.4, .9);
            staves.add(std::move(b));
        }
        lid.on    = true;
        lid.x     = 0;
        lid.y     = -22;
        lid.vx    = rng.range(-40, 40);
        lid.vy    = -rng.range(430, 480);
        lid.angle = 0;
        lid.va    = rng.signedRange(10, 14);
        for (int i = 0; i < 7; ++i) {
            const double x = rng.range(-34, 34);
            pools.push_back({x, rng.range(5, 10)});
            const double life = rng.range(2.2, 3), power = rng.range(.8, 1.3);
            blazes.add(rng, x, -.25, life, power);
        }
        for (int i = 0; i < 12; ++i) {
            const int side = rng.sign();
            emit({ParticleKind::Smoke, side * rng.range(0, 10), -rng.range(1, 4), side * rng.range(80, 220),
                  -rng.range(2, 12), rng.range(1.1, 1.8), hex(0x7d7280), rng.range(6, 9), rng.range(0, .05),
                  ParticleFlag::Big});
        }
        const std::array<uint32_t, 3> stones{hex(0x5a4f6b), hex(0x3e3650), hex(0x776c88)};
        for (int i = 0; i < 60; ++i) {
            const double u = rng.range(-1, 1), a = -PI / 2 + u * .7, sp = rng.range(120, 330) * (1 - .4 * std::abs(u));
            emit({ParticleKind::Grit, u * 6, -1, std::cos(a) * sp, std::sin(a) * sp, rng.range(1.1, 1.9),
                  rng.pick<uint32_t>(stones), rng.isChance(.4) ? 2. : 1.});
        }
    }
    void update(double dt) override {
        t += dt;
        if (!blown) {
            const double k = t / .8;
            if (rng.isChance((6 + 30 * k * k) * dt)) {
                const int side = rng.sign();
                emit({ParticleKind::Fire, side * rng.range(4, 7), -21, side * rng.range(20, 60), -rng.range(20, 60),
                      rng.range(.15, .3), 0, rng.range(1.5, 3)});
                emit({ParticleKind::Spark, side * 6., -21, side * rng.range(40, 140), -rng.range(20, 120),
                      rng.range(.1, .25)});
            }
            if (rng.isChance(10 * dt))
                emit({ParticleKind::Smoke, rng.range(-5, 5), -22, rng.range(-8, 8), -rng.range(15, 35),
                      rng.range(.8, 1.3), 0, rng.range(2.5, 4)});
            if (t >= .8) blow();
        }
        if (lid.on && !lid.rest) {
            lid.vy += 950 * dt;
            lid.vx -= lid.vx * .4 * dt;
            lid.x += lid.vx * dt;
            lid.y += lid.vy * dt;
            lid.angle += lid.va * dt;
            if (lid.y >= -1 && lid.vy > 0) {
                for (int i = 0; i < 6; ++i)
                    emit({ParticleKind::Spark, lid.x, -1, rng.range(-90, 90), -rng.range(40, 140), rng.range(.1, .25)});
                for (int i = 0; i < 5; ++i)
                    emit({ParticleKind::Dust, lid.x + rng.range(-4, 4), -.5, rng.signedRange(15, 60), -rng.range(4, 18),
                          rng.range(.35, .7), hex(0x7d7280), rng.range(1.8, 3)});
                lid.y = -1;
                if (lid.vy > 160) {
                    lid.vy *= -.3;
                    lid.vx *= .5;
                    lid.va *= .5;
                } else {
                    lid.rest  = true;
                    lid.angle = PI / 2;
                }
            }
        }
        staves.update(dt, [](double) { return 0.; });
        for (size_t i = staves.list.size(); i-- > 0;) {
            auto& b = staves.list[i];
            if (b.x < -139 || b.x > 139)
                staves.list.erase(staves.list.begin() + std::ptrdiff_t(i));
            else if (b.heat > 0)
                b.heat = std::max(0., b.heat - dt * .35);
        }
        blazes.update(dt, ps, rng);
        ps.update(
            dt, rng, [](double) { return 0.; },
            [&](double x, double y) {
                const double life = rng.range(1.2, 2.2), power = rng.range(.6, 1);
                blazes.add(rng, x, y - .25, life, power);
            });
        cull(ps, -147, -201, 147, 33);
        fadeEdges(ps, -147, 147, 24, 33);
        lights.update(dt);
    }
    void groundRing(Paint& p, bool back) const {
        const double k = (t - .8) / .45;
        if (k >= 1) return;
        const std::array<uint32_t, 4> ramp{hex(0x4a3f69), hex(0x6e6390), F[3], F[6]};
        band(p, 0, .5, 100 * ease::outCubic(k), .16, std::max(1., 4.5 * (1 - k)), 1.15 - k, ramp,
             [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; });
    }
    void drawBack(Paint& p) const override {
        if (blown) {
            const double u = t - .8, out = 1 - smooth(5.2 - .8 - 1, 5.2 - .8 - .2, u);
            scorch(p, HR, u, 30, 2.2, out, 3);
            const double left = 1 - smooth(1.6, 3.4, u);
            if (left > 0)
                for (const auto& q : pools)
                    for (int x = int(std::floor(-q.w * left)); x <= int(std::ceil(q.w * left)); ++x) {
                        const double e = std::abs(x) / (q.w * left + .01);
                        p.dot(q.x + x, 1,
                              rand({jsUint(std::floor(q.x + x)), jsUint(std::floor(t * 12))}) < .25 ? F[4]
                                                                                                    : hex(0x221a22),
                              out * (e < .8 ? 1 : .6));
                        if (e < .6) p.dot(q.x + x, 2, hex(0x221a22), out * .8);
                    }
            groundRing(p, true);
        }
        drawPs(p, ps, Paint::Layer::Back, FR);
        blazes.drawBack(p);
    }
    void drawBarrel(Paint& p) const {
        const double k = t / .8, x = std::sin(t * 70) * k * k * 1.4, y = -std::abs(std::sin(t * 53)) * k * k * .8,
                     bulge = jsRound(k * k * 2);
        p.shader(x - 9, y - 25, x + 9, y + .5, [&](double px, double py, ShadeOutput& out) {
            const double dx = (px - x) / 8, v = (y - py) / 22;
            if (std::abs(dx) > 1.1 || v < 0 || v > 1.05) return;
            if (std::abs(dx) > .92 || v > .97 || v < .04) {
                out.c = hex(0x1a1422);
                return;
            }
            const bool   rib    = std::abs(v - .33) < .035 || std::abs(v - .67) < .035,
                         ribLit = std::abs(v - .31) < .03 || std::abs(v - .65) < .03;
            const double lit    = -dx;
            int          i      = lit > .55 ? 5 : lit > .15 ? 4 : lit > -.35 ? 3 : lit > -.75 ? 2 : 1;
            if (v > .44 && v < .56) {
                out.c = lit > .3 ? hex(0xe8d8b0) : lit > -.4 ? hex(0xb8a888) : hex(0x6e6050);
                return;
            }
            if (rib)
                i = std::max(1, i - 2);
            else if (ribLit)
                i = std::min(5, i + 1);
            out.c = C[i];
        });
        p.ellipse(x, y - 22 - bulge * .5, 7.5, 2.2 + bulge * .4,
                  [&](double d, double a) { return d > .7 ? (std::sin(a) < 0 ? C[4] : C[2]) : C[1]; });
    }
    void stave(Paint& p, const Plate& b) const {
        const double fade = std::min(1., b.life / .5), ca = std::cos(b.angle), sa = std::sin(b.angle), L = b.size;
        const bool   face = std::cos(b.angle * 1.7) > 0;
        for (double d = -L / 2; d <= L / 2; d += .5) {
            const double bow = (1 - std::pow(2 * d / L, 2)) * 1.2, x = b.x + ca * d - sa * bow,
                         y   = b.y + sa * d + ca * bow;
            const bool   hot = b.heat > .3 && std::abs(d) > L / 2 - 1;
            p.dot(x + sa, y - ca, hex(0x1a1422), fade);
            p.dot(x, y, hot ? FR[b.heat > .6 ? 3 : 5] : face ? C[4] : C[1], fade);
            p.dot(x - sa, y + ca, face ? C[2] : hex(0x1a1422), fade);
        }
        if (b.heat > .3) p.glow(b.x, b.y, 6, FR[4], .25 * b.heat * fade);
    }
    void drawLid(Paint& p) const {
        const double w = std::max(.8, std::abs(std::cos(lid.angle)) * 7.5),
                     h = std::max(1.2, std::abs(std::sin(lid.angle)) * 7.5), rx = lid.rest ? 7.5 : w,
                     ry = lid.rest ? 1.6 : h, alpha = std::min(1., (5.2 - t) / .4), y = lid.y - (lid.rest ? .5 : 0);
        p.ellipse(lid.x, y, rx + .8, ry + .8, hex(0x1a1422), alpha);
        p.ellipse(
            lid.x, y, rx, ry, [&](double d, double an) { return d > .65 ? (std::sin(an) < 0 ? C[4] : C[2]) : C[3]; },
            alpha);
    }
    void draw(Paint& p) const override {
        if (!blown) {
            drawBarrel(p);
            const double k = t / .8;
            p.glow(0, -22, 10 + 8 * k, F[4], .25 + .35 * k * k * (.6 + .4 * std::sin(t * 40)));
        } else
            groundRing(p, false);
        drawPs(p, ps, Paint::Layer::Front, FR);
        for (const auto& b : staves.list) stave(p, b);
        if (lid.on) drawLid(p);
        if (blown) {
            const double u = t - .8;
            if (u < .08) {
                const double k = 1 - u / .08, r = 18 * (.85 + .15 * k);
                p.circle(0, -11, r + 1.4, F[2]);
                p.circle(0, -11, r, k > .45 ? F[6] : F[5]);
                p.circle(0, -11, r * .6, k > .45 ? WHITE : F[6]);
            }
            if (u < .22) flare(p, 0, -11, 54 * std::pow(1 - u / .22, 1.4), 1 - u / .22, F);
            pool(p, 0, 1, 90, 14, F[3], .5 * std::max(0., 1 - u / .7) + .22 * (1 - smooth(1.6, 3.4, u)));
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& barrelExplosionDescriptor() {
    static const std::array<Option, 1> options{{{"barrel", 1, 64, false, 6, &PixelFxExplosionSettings::barrel}}};
    static const Descriptor            descriptor{
        {PixelFxKind::BarrelExplosion, "explosions/barrel-explosion", 300, 240, 150, 204, 5.2, false},
        8,
        [](PixelFxSettings& s) {
            s.seed             = 8;
            s.palette          = {hex(0x4a1712), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                  hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
            s.explosion.barrel = {hex(0x1c0a0e), hex(0x4a1218), hex(0x7c1a1e),
                                  hex(0xb02a26), hex(0xd8483a), hex(0xf08a70)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<BarrelExplosion>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
