// eve-hot-path
// Native adaptation of pixelfx holy/smite.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#include "stylize/pixelfx/HolyActor.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double TOP = -148, SY = .38, MARK = .42, HIT = .54, LEN = 44;
struct Crack {
    std::vector<Point> points, fork;
    double             length, cool;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
    ParticleFlag flags = ParticleFlag::None;
};
struct Smite final : Kernel {
    const std::vector<uint32_t>   C;
    const std::array<uint32_t, 4> stone;
    const uint32_t                dust;
    Rng                           rng;
    Particles                     ps;
    Bits                          chips;
    Lights                        lights;
    std::array<Crack, 7>          cracks;
    double                        t   = 0;
    bool                          hit = false;
    explicit Smite(const PixelFxSettings& o)
        : C(o.palette),
          stone{scale(o.holy.stone, .5), scale(o.holy.stone, .75), o.holy.stone,
                mix(scale(o.holy.stone, 1.4), WHITE, .1)},
          dust(mix(o.holy.stone, hex(0x8a8094), .4)),
          rng(o.seed) {
        ps.sparks = {WHITE, C[6], C[5], C[3]};
        for (size_t i = 0; i < cracks.size(); ++i) {
            const double a   = .12 + (double(i) / 6) * (PI - .24) + rng.range(-.12, .12),
                         L   = rng.range(30, 54) * (std::abs(std::sin(a)) > .7 ? .6 : 1);
            auto         pts = bolt(rng, 0, 0, std::cos(a) * L, std::sin(a) * L, .28, 4);
            for (auto& point : pts) point.y *= SY;
            std::vector<Point> fork;
            if (pts.size() > 6) {
                const auto   q = pts[pts.size() / 2];
                const double x = q.x + std::cos(a + rng.sign() * .7) * L * .35,
                             y = q.y + std::sin(a + rng.sign() * .7) * L * .35 * SY;
                fork           = bolt(rng, q.x, q.y, x, y, .3, 3);
            }
            const double cool = rng.range(1.6, 2.1);
            cracks[i]         = {std::move(pts), std::move(fork), L, cool};
        }
    }
    void emit(const Emission& e) {
        ps.spawn(rng, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay, e.flags);
    }
    double tipY() const { return lerp(TOP - 6, 0, ease::inCubic(seg(t, MARK, HIT))); }
    void   chip(Paint& p, const Debris& b) const {
        const double r = .6 + b.size * .7, fade = std::min(1., b.life / .5), lx = std::cos(b.angle) * r * .4,
                     ly = std::sin(b.angle) * r * .4;
        p.shade(b.x - r - 1, b.y - r - 1, b.x + r + 1, b.y + r + 1, [&](double x, double y, int sx, int sy) {
            const double dx = x - b.x, dy = y - b.y, d = jsHypot2(dx * 1.2, dy);
            if (d > r + .9) return;
            const double lit = dx * lx + dy * ly;
            p.put(sx, sy, d > r ? hex(0x1a1422) : lit < -.3 ? stone[3] : lit > .5 ? stone[1] : stone[2], fade);
        });
    }
    void hammer(Paint& p) const {
        if (t < MARK || t >= HIT + .06) return;
        const double y1    = t >= HIT ? 0 : tipY(),
                     y0    = std::max(TOP + 5, y1 - LEN * (t >= HIT ? 1 - (t - HIT) / .06 : 1));
        const bool   white = t >= HIT;
        if (y1 < TOP + 12) return;
        for (const auto q : std::array<Point, 4>{{{-5, .6}, {-2, .9}, {2, .8}, {5, .5}}})
            if (y0 > TOP + 2)
                p.lineFn(
                    q.x, std::max(TOP, y0 - 70), q.x, y0, [&](double u) { return u > .7 ? C[6] : C[4]; },
                    [&](double u) { return u * q.y; });
        if (y1 - 3 > y0)
            p.beam(0, y0, 0, y1 - 3, 9, [&](double, double v) {
                return white || v < .35 ? WHITE : v < .7 ? C[6] : C[5];
            });
        p.beam(0, y1 - 6, 0, y1 - 1, 13, [&](double, double v) { return v < .6 || white ? WHITE : C[6]; });
        p.glowLine(0, y0 + 10, 0, y1, 16, C[5], .6);
        p.glow(0, y1 - 4, std::min(22., y1 - TOP - 6), WHITE, .5);
    }
    void crack(Paint& p) const {
        if (t < HIT) return;
        const double grow = ease::outCubic(seg(t, HIT, HIT + .1));
        for (const auto& c : cracks)
            for (int branch = 0; branch < (c.fork.empty() ? 1 : 2); ++branch) {
                const auto&  path = branch ? c.fork : c.points;
                const size_t n    = size_t(std::max(1., jsRound(double(path.size() - 1) * grow)));
                for (size_t i = 0; i < n; ++i) {
                    const auto   q = path[i], z = path[i + 1];
                    const double d = jsHypot2(q.x, q.y / SY) / c.length, heat = clamp(1 - (t - HIT) / c.cool - d * .5);
                    if (heat <= 0) continue;
                    const auto col = heat > .85   ? WHITE
                                     : heat > .65 ? C[6]
                                     : heat > .45 ? C[5]
                                     : heat > .3  ? C[4]
                                     : heat > .18 ? C[3]
                                                  : C[2];
                    p.line(q.x, q.y, z.x, z.y, col, heat < .12 ? heat / .12 : 1);
                    if (d < .45 && branch == 0)
                        p.line(q.x, q.y + 1, z.x, z.y + 1, heat > .5 ? C[4] : C[2], heat < .12 ? heat / .12 : 1);
                    if (i % 2 == 0) p.glow(q.x, q.y, 10, C[4], .45 * heat);
                }
            }
        const double h = clamp(1 - (t - HIT) / 1.6);
        if (h > 0) {
            holy::oval(p, 0, 0, 5, 1.6,
                       h > .6   ? WHITE
                       : h > .3 ? C[5]
                                : C[3],
                       std::min(1., h * 3), [](double) { return true; });
            p.glow(0, 0, 18, C[4], .45 * h);
        }
    }
    void update(double dt) override {
        t += dt;
        if (t < MARK && rng.isChance(.25))
            emit({ParticleKind::Star, rng.range(-10, 10), -rng.range(0, 18), 0, -rng.range(4, 12), rng.range(.2, .4),
                  rng.isChance(.5) ? WHITE : C[6], rng.range(.5, 1.2)});
        if (t > MARK && t < HIT && tipY() > TOP + 50 && rng.isChance(.6)) {
            const int side = rng.sign();
            emit({ParticleKind::Mote, side * rng.range(4, 7), tipY() - rng.range(4, 30), rng.range(-20, 20),
                  -rng.range(10, 40), rng.range(.15, .3), C[6], .7});
        }
        if (!hit && t >= HIT) {
            hit = true;
            lights.add(0, -24, 92, C[5], 1, .6);
            lights.add(0, -8, 40, WHITE, .9, .12);
            for (int i = 0; i < 60; ++i) {
                const double a = -PI / 2 + rng.range(-1.25, 1.25), sp = rng.range(110, 280);
                emit({ParticleKind::Spark, rng.range(-3, 3), -2, std::cos(a) * sp, std::sin(a) * sp,
                      rng.range(.25, .75)});
            }
            for (int i = 0; i < 16; ++i) {
                const double a = -PI / 2 + rng.range(-1, 1), sp = rng.range(60, 160);
                emit({ParticleKind::Star, rng.range(-4, 4), -4, std::cos(a) * sp, std::sin(a) * sp, rng.range(.4, .9),
                      rng.isChance(.5) ? WHITE : C[6], rng.range(.8, 2.4)});
            }
            for (int i = 0; i < 14; ++i) {
                const double a = -PI / 2 + rng.range(-1.2, 1.2), sp = rng.range(90, 230);
                Debris       b;
                b.kind            = DebrisKind::Chunk;
                b.x               = rng.range(-5, 5);
                b.y               = -2;
                b.vx              = std::cos(a) * sp;
                b.vy              = std::sin(a) * sp;
                b.gravity         = 1;
                b.drag            = .4;
                b.bounce          = .35;
                b.life            = rng.range(1.2, 1.8);
                b.angle           = rng.range(0, TAU);
                b.angularVelocity = rng.range(-14, 14);
                b.size            = rng.integer(1, 3);
                chips.add(std::move(b));
            }
            for (int i = 0; i < 34; ++i) {
                const double a = -PI / 2 + rng.range(-1, 1), sp = rng.range(60, 220);
                emit({ParticleKind::Grit, rng.range(-4, 4), -1, std::cos(a) * sp, std::sin(a) * sp, rng.range(.9, 1.5),
                      rng.pick<uint32_t>(std::span(stone).subspan(1)), rng.isChance(.4) ? 2. : 1.});
            }
            for (int i = 0; i < 9; ++i) {
                const int s = rng.sign();
                emit({ParticleKind::Smoke, s * rng.range(2, 10), -rng.range(1, 4), s * rng.range(50, 130),
                      -rng.range(2, 12), rng.range(.7, 1.2), mix(dust, C[4], .18), rng.range(3.5, 6), rng.range(0, .06),
                      ParticleFlag::Big});
            }
            for (int i = 0; i < 4; ++i)
                emit({ParticleKind::Smoke, rng.range(-4, 4), -rng.range(2, 8), rng.range(-8, 8), -rng.range(20, 45),
                      rng.range(1.2, 1.8), mix(dust, C[5], .25), rng.range(4, 6), rng.range(.05, .3)});
        }
        if (t > HIT && t < HIT + 1.3 && rng.isChance(.5)) {
            const auto& c = cracks[size_t(std::floor(rng.next() * double(cracks.size())))];
            const auto  q = rng.pick<Point>(c.points);
            if (rng.isChance(.75))
                emit(
                    {ParticleKind::Ember, q.x, q.y - 1, rng.range(-8, 8), -rng.range(15, 35), rng.range(.5, .9), C[5]});
            else
                emit({ParticleKind::Star, q.x, q.y - 2, 0, -rng.range(4, 12), rng.range(.3, .6),
                      rng.isChance(.5) ? WHITE : C[6], rng.range(.8, 1.8)});
        }
        chips.update(dt, [](double) { return 0.; });
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void wave(Paint& p, bool back) const {
        const double k = seg(t, HIT, HIT + .42);
        if (t >= HIT && k < 1)
            band(
                p, 0, 0, 6 + 84 * ease::outCubic(k), SY * .8, std::max(1., 5 * (1 - k)), 1.15 - k,
                std::span(C).subspan(3), [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; }, k < .12);
    }
    void drawBack(Paint& p) const override {
        crack(p);
        ps.draw(p, Paint::Layer::Back);
        wave(p, true);
    }
    void draw(Paint& p) const override {
        if (t < HIT) {
            const double u = seg(t, 0, MARK), k = smooth(0, .08, t), spin = t * 3,
                         rr = lerp(26, 8, ease::inOutCubic(u));
            holy::oval(
                p, 0, 0, rr, rr * SY, [&](double g) { return std::sin(g * 4 + spin) > 0 ? WHITE : C[5]; },
                k * (.65 + .35 * std::sin(t * 40)), [](double) { return true; });
            holy::oval(p, 0, 1, rr, rr * SY, C[3], k * .6, [](double g) { return std::sin(g) > 0; });
            const double L = (3 + 8 * ease::outBack(clamp(t / .2), 2)) * (t > MARK ? 1 + .6 * seg(t, MARK, HIT) : 1);
            for (int i = 0; i < 4; ++i) {
                const double a = spin * .3 + (i * PI) / 2, len = i % 2 ? L * .6 : L;
                p.lineFn(
                    0, -2, std::cos(a) * len, -2 + std::sin(a) * len, [&](double v) { return v < .4 ? WHITE : C[5]; },
                    [](double v) { return 1.2 - v; });
            }
            p.px(0, -2, WHITE);
            p.glow(0, -2, 14 + 10 * u, C[5], .5 * k);
            p.glowLine(0, TOP + 6, 0, -2, 5, C[4], .12 * u);
        }
        hammer(p);
        const double ai = seg(t, HIT + .04, HIT + .55);
        if (t > HIT + .04 && ai < 1) {
            const double w = 1 + 4 * std::pow(1 - ai, 2);
            p.beam(
                0, TOP + 4, 0, -4, w, [&](double, double v) { return v < .5 ? WHITE : C[5]; },
                [&](double u) { return (.3 + .7 * u) * (1 - ai * .5); });
            p.glowLine(0, TOP + 16, 0, -6, 10, C[5], .4 * (1 - ai));
        }
        wave(p, false);
        const double d = seg(t, HIT, HIT + .28);
        if (t >= HIT && d < 1) {
            const double r = 8 + 40 * ease::outCubic(d);
            p.arc(
                0, 0, r, std::max(1., 3 * (1 - d)), PI, TAU,
                [&](double, double v) { return d < .2 || v > .6 ? WHITE : C[5]; }, 1.1 - d);
            for (double g = PI * 1.1; g < TAU - .2; g += .5)
                p.glow(std::cos(g) * r, std::sin(g) * r, 8, C[5], .3 * (1 - d));
        }
        if (t >= HIT && t < HIT + .07) {
            p.circle(0, -6, 16, WHITE);
            p.circle(0, -6, 19, C[6], .6);
        }
        for (const auto& b : chips.list) chip(p, b);
        ps.draw(p, Paint::Layer::Front);
        if (t >= HIT && t < HIT + .25) p.sparkle(0, -8, 16 * (1 - (t - HIT) / .25), WHITE, C[5], .7);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& smiteDescriptor() {
    static const std::array<Option, 1> options{
        {{"stone", 4278190080., 4294967295., false, 0, &PixelFxHolySettings::stone, true}}};
    static const Descriptor d{
        {PixelFxKind::Smite, "holy/smite", 224, 176, 112, 148, 2.8, false},
        8,
        [](PixelFxSettings& s) {
            s.seed       = 9;
            s.holy.stone = hex(0x5a5066);
            s.palette    = {hex(0x3b1d2a), hex(0x7a3a2c), hex(0xc4652e), hex(0xf0a040),
                            hex(0xffd25e), hex(0xfff0a0), hex(0xfffbe0), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Smite>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
