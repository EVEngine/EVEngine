// eve-hot-path
// Native adaptation of pixelfx nature/boulder.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/NatureGeometry.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double X0 = 6, Y0 = -26, VX = 220, VY = -170, G = 860, D = 2.3;
struct Rock : Debris {
    nature::StoneShape shape;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
    ParticleFlag flags = ParticleFlag::None;
};
struct RockEmission {
    double x, y, vx, vy, gravity, drag, bounce, life, angle, angularVelocity, size;
};
void surface(Paint& p, double x, double y, double r, double a, std::span<const uint32_t> S) {
    const double c = std::cos(a), s = std::sin(a);
    const auto   at = [&](double u, double v) { return Point{x + (u * c - v * s) * r, y + (u * s + v * c) * r}; };
    for (const auto uv : std::array<Point, 2>{{{.42, .12}, {-.3, .38}}}) {
        const auto q = at(uv.x, uv.y);
        p.px(q.x, q.y, S[3]);
    }
    const std::array<uint32_t, 3> moss{{hex(0x1f3a1e), hex(0x3f7a2c), hex(0x7cb83a)}};
    for (int k = 0; k < 9; ++k) {
        const double g = 3.6 + (double(k) / 8) * 1.3, rr = k % 3 == 1 ? .78 : .92;
        const auto   q = at(std::cos(g) * rr, std::sin(g) * rr);
        p.px(q.x, q.y, moss[k % 3 == 1 ? 0 : q.y < y ? 2 : 1]);
    }
}
struct Boulder final : Kernel {
    const std::vector<uint32_t> S;
    const double                rad;
    const uint32_t              dust, dust2;
    Rng                         R;
    Particles                   ps;
    BasicBits<Rock>             bits;
    Lights                      lights;
    const nature::StoneShape    shape;
    const double                tHit, xHit;
    double                      t   = 0;
    bool                        hit = false, thrown = false;
    explicit Boulder(const PixelFxSettings& o)
        : S(o.palette),
          rad(o.nature.size),
          dust(mix(S[6], hex(0x9a8f86), .4)),
          dust2(mix(S[5], hex(0x7a7068), .35)),
          R(o.seed),
          shape(nature::stoneShape(R, 8)),
          tHit((-VY + std::sqrt(VY * VY - 2 * G * (Y0 + rad * .8))) / G),
          xHit(X0 + VX * tHit) {
        ps.sparks = o.nature.sparks;
    }
    Point at(double tt) const { return {X0 + VX * tt, Y0 + VY * tt + .5 * G * tt * tt}; }
    void  emit(const Emission& e) {
        ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay, e.flags);
    }
    void rock(const RockEmission& e) {
        Rock b;
        b.kind            = DebrisKind::Chunk;
        b.x               = e.x;
        b.y               = e.y;
        b.vx              = e.vx;
        b.vy              = e.vy;
        b.gravity         = e.gravity;
        b.drag            = e.drag;
        b.bounce          = e.bounce;
        b.life            = e.life;
        b.angle           = e.angle;
        b.angularVelocity = e.angularVelocity;
        b.size            = e.size;
        b.shape           = nature::randomStoneShape(R);
        bits.add(std::move(b));
    }
    void update(double dt) override {
        t += dt;
        const double f = std::min(1., dt * 120);
        if (!thrown) {
            thrown = true;
            for (int i = 0; i < 6; ++i)
                emit({ParticleKind::Dust, X0 - R.range(0, 6), Y0 + R.range(-4, 4), -R.range(20, 70), R.range(-20, 20),
                      R.range(.4, .7), dust, R.range(2.5, 4.5)});
            lights.add(X0, Y0, 30, WHITE, .3, .1);
        }
        if (t < tHit) {
            const auto q = at(t);
            if (R.isChance(.5 * f))
                emit({ParticleKind::Dust, q.x - R.range(2, 6), q.y + R.range(-4, 4), -R.range(10, 40), R.range(-10, 10),
                      R.range(.3, .6), dust, R.range(2, 3.5)});
            if (R.isChance(.12 * f))
                emit({ParticleKind::Grit, q.x + R.range(-4, 4), q.y + R.range(-2, 4), VX * .3, R.range(-40, 20),
                      R.range(.6, .9), R.pick<uint32_t>(std::array<uint32_t, 2>{{S[5], S[4]}}),
                      R.isChance(.3) ? 2. : 1.});
        }
        if (!hit && t >= tHit) {
            hit            = true;
            const double x = xHit, y = -rad * .6;
            lights.add(x, y, 36, WHITE, .75, .1);
            lights.add(x, y, 60, hex(0xffb060), .45, .5);
            for (int i = 0; i < 9; ++i) {
                const double a = -PI / 2 + R.range(-1.2, 1.1), sp = R.range(100, 240);
                rock({x + R.range(-5, 5), y + R.range(-5, 5), std::cos(a) * sp + 30, std::sin(a) * sp, 1, .7, .35,
                      R.range(1.3, 1.7), R.range(0, TAU), R.range(-14, 14), R.range(2.8, 5.2)});
            }
            for (int i = 0; i < 14; ++i) {
                const double a = -PI / 2 + R.range(-1.4, 1.4), sp = R.range(120, 280);
                rock({x + R.range(-4, 4), y, std::cos(a) * sp + 25, std::sin(a) * sp, 1, .8, .3, R.range(1.1, 1.5),
                      R.range(0, TAU), R.range(-18, 18), R.range(1.4, 2.6)});
            }
            for (int i = 0; i < 60; ++i) {
                const double u = R.range(-1, 1), a = -PI / 2 + u * 1.1, sp = R.range(80, 300) * (1 - .3 * std::abs(u));
                emit({ParticleKind::Grit, x + u * 6, -1, std::cos(a) * sp + 30, std::sin(a) * sp, R.range(1, 1.8),
                      R.pick<uint32_t>(std::array<uint32_t, 5>{{S[6], S[5], S[4], S[4], S[3]}}),
                      R.isChance(.4) ? 2. : 1.});
            }
            for (int i = 0; i < 12; ++i) {
                const double a = -PI / 2 + R.range(-1.3, 1.3), sp = R.range(150, 380);
                emit({ParticleKind::Spark, x, y, std::cos(a) * sp + 40, std::sin(a) * sp, R.range(.15, .4)});
            }
            for (int i = 0; i < 14; ++i) {
                const double sd = R.sign();
                emit({ParticleKind::Smoke, x + sd * R.range(2, 10), -R.range(1, 5), sd * R.range(40, 140) + 10,
                      -R.range(4, 20), R.range(1.1, 1.8), R.isChance(.5) ? dust : dust2, R.range(4.5, 8),
                      R.range(0, .05), ParticleFlag::Big});
            }
            for (int i = 0; i < 6; ++i)
                emit({ParticleKind::Smoke, x + R.range(-6, 6), -R.range(4, 14), R.range(-15, 25), -R.range(30, 80),
                      R.range(1.2, 1.8), dust, R.range(6, 9), R.range(0, .1), ParticleFlag::Big});
            for (int i = 0; i < 3; ++i)
                emit({ParticleKind::Star, x + R.range(-8, 8), y + R.range(-10, 4), 0, -4, R.range(.2, .4), S[7],
                      R.range(1, 2)});
        }
        const auto floor = [](double) { return 0.; };
        bits.update(dt, floor);
        for (auto& b : bits.list)
            if (b.rest && !b.dusted) {
                b.dusted = true;
                for (int i = 0; i < 2; ++i) {
                    const double x = b.x + R.range(-1.5, 1.5), sd = R.sign(), vx = sd * R.range(8, 35);
                    emit({ParticleKind::Dust, x, -.5, vx, -R.range(2, 12), std::min(R.range(.3, .6), D - .05 - t),
                          dust2, R.range(1.3, 2.6)});
                }
            }
        ps.update(dt, R, floor, [](double, double) {});
        lights.update(dt);
    }
    void ring(Paint& p, bool front) const {
        const double e = t - tHit, k = e / .45;
        if (e > 0 && k < 1) {
            const std::array<uint32_t, 4> ramp{{S[4], S[6], hex(0xffd27a), WHITE}};
            band(p, xHit, .5, 46 * ease::outCubic(k), .18, std::max(1., 3 * (1 - k)), 1.15 - k, ramp,
                 [front](double g) { return front ? std::sin(g) >= 0 : std::sin(g) < 0; });
        }
    }
    void drawBack(Paint& p) const override {
        const double e = t - tHit;
        if (e > 0) {
            const double fade = 1 - smooth(D - .6, D - .15, t), w = 10 * std::min(1., e / .05);
            for (double x = std::floor(-w); x <= w; ++x) {
                const double d = std::abs(x) / w;
                p.dot(xHit + x, 0, d < .6 ? S[0] : S[1], fade);
                if (d < .5) p.dot(xHit + x, 1, S[1], fade * .8);
            }
            for (const double sd : {-1., 1.})
                for (int k = 0; k < 4; ++k)
                    p.dot(xHit + sd * (w + 2 + k * 3), (k & 1) ? 1 : 0, S[1], fade * (1 - double(k) / 4));
            ring(p, false);
        }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        if (t < tHit) {
            const auto   q  = at(t);
            const double sp = jsHypot2(VX, VY + G * t), ux = VX / sp, uy = (VY + G * t) / sp;
            for (int i = 0; i < 4; ++i) {
                const double off = (i - 1.5) * rad * .55, nx = -uy * off, ny = ux * off, L = 10 + (i % 2) * 6;
                p.line(q.x - ux * (rad + 2) + nx, q.y - uy * (rad + 2) + ny, q.x - ux * (rad + 2 + L) + nx,
                       q.y - uy * (rad + 2 + L) + ny, i % 2 ? S[5] : S[6], .8);
            }
            nature::drawRock(p, q.x, q.y, rad, t * 13, shape, S);
            surface(p, q.x, q.y, rad * .8, t * 13, S);
            p.glow(q.x, q.y, rad * 2, S[6], .12);
        }
        const double e = t - tHit;
        if (e >= 0 && e < .07) {
            nature::drawRock(p, xHit, -rad * .8, rad, tHit * 13, shape, S, 1, true);
            p.sparkle(xHit, -rad * .6, 12 * (1 - e / .07), WHITE, hex(0xffd27a), .6);
        }
        if (e >= 0 && e < .14) {
            const double k = e / .14;
            for (int i = 0; i < 9; ++i) {
                const double a = -PI + (double(i) / 8) * PI, r0 = rad + 4 + 16 * k, r1 = r0 + 8 * (1 - k);
                p.line(xHit + std::cos(a) * r0, -rad * .6 + std::sin(a) * r0, xHit + std::cos(a) * r1,
                       -rad * .6 + std::sin(a) * r1, k < .5 ? WHITE : S[6]);
            }
        }
        ring(p, true);
        for (const auto& b : bits.list)
            nature::drawRock(p, b.x, b.y, b.size, b.angle, b.shape, S, std::min(1., b.life / .4));
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& boulderDescriptor() {
    static const std::array<Option, 2> options{{{"sparks", 1, 64, false, 4, &PixelFxNatureSettings::sparks},
                                                {"size", .5, 32, false, 0, &PixelFxNatureSettings::size}}};
    static const Descriptor            d{
        {PixelFxKind::Boulder, "nature/boulder", 264, 120, 40, 104, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed        = 11;
            s.nature.size = 11;
            s.nature.sparks.assign(HOT.begin(), HOT.end());
            s.palette = {hex(0x100e12), hex(0x1e1a22), hex(0x302a36), hex(0x463e4c),
                         hex(0x5e5464), hex(0x7c7080), hex(0xa0949e), hex(0xcec4c8)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Boulder>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
