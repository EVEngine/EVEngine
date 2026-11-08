// eve-hot-path
// Native adaptation of pixelfx arcane/time-stop.js, MIT.
// Sprite Fusion ice shatter, Copyright (c) 2026 Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                CY = -42, RC = 26, OPEN = .3, FREEZE = 1.75, CRACK = 2.2, SHATTER = 2.42;
constexpr std::array<double, 8> TICKS{.36, .46, .57, .7, .85, 1.03, 1.24, 1.49};
double                          hand(double time) {
    double a = 0;
    for (double tick : TICKS)
        if (time > tick) a += ease::outBack(clamp((time - tick) / .07), 3);
    return -PI / 2 + a * (TAU / 12);
}
double flow(double time) { return time < OPEN ? 1 : std::pow(std::max(0., 1 - (time - OPEN) / (FREEZE - OPEN)), 1.6); }
double clockTime(double time) {
    double s = 0;
    for (double u = 0; u < std::min(time, FREEZE); u += 1. / 120) s += flow(u) / 120;
    return s;
}
struct Mote {
    double   angle, radius, speed, tilt;
    uint32_t color;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct Chip {
    DebrisKind            kind;
    double                x, y, vx, vy, gravity, drag, bounce, life, angle, va, size;
    uint32_t              color = WHITE;
    std::vector<uint32_t> ramp;
};
struct TimeStop final : Kernel {
    const std::vector<uint32_t>     C;
    std::vector<uint32_t>           G;
    Rng                             R;
    Particles                       ps;
    Bits                            bits;
    Lights                          lights;
    std::array<Mote, 14>            motes{};
    double                          ix = 0, iy = 0, t = 0, tc = 0;
    size_t                          ticked    = 0;
    bool                            shattered = false, frozen = false;
    std::vector<std::vector<Point>> cracks;
    explicit TimeStop(const PixelFxSettings& o) : C(o.palette), R(o.seed) {
        for (auto c : C) {
            const double l = luma(c) * 255;
            G.push_back(rgb(l * .92, l * .96, std::min(255., l * 1.06)));
        }
        ps.sparks = {WHITE, C[6], C[5], C[4]};
        for (size_t i = 0; i < motes.size(); ++i) {
            const double a = R.range(0, TAU), r = RC + R.range(5, 14), w = R.range(1.4, 2.6) * (i % 2 ? 1 : -1),
                         tilt = R.range(-.5, .5);
            const auto   c    = R.pick<uint32_t>(std::array<uint32_t, 3>{WHITE, C[6], C[5]});
            motes[i]          = {a, r, w, tilt, c};
        }
        ix = R.range(-6, 6);
        iy = R.range(-6, 6);
        for (int i = 0; i < 7; ++i) walk(ix, iy, (double(i) / 7) * TAU + R.range(-.3, .3), 16, 0);
    }
    void walk(double x, double y, double a, int len, int depth) {
        std::vector<Point> pts{{x, y}};
        for (int s = 0; s < len; ++s) {
            a += R.range(-.55, .55);
            x += std::cos(a) * R.range(1.6, 2.6);
            y += std::sin(a) * R.range(1.6, 2.6);
            if (jsHypot2(x, y) > RC - .5) break;
            pts.push_back({x, y});
            if (depth < 1 && pts.size() > 3 && R.isChance(.14)) {
                const int    side   = R.sign();
                const double offset = R.range(.6, 1.1);
                walk(x, y, a + side * offset, len / 2, depth + 1);
            }
        }
        cracks.push_back(std::move(pts));
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void chip(Chip v) {
        Debris q;
        q.kind            = v.kind;
        q.x               = v.x;
        q.y               = v.y;
        q.vx              = v.vx;
        q.vy              = v.vy;
        q.gravity         = v.gravity;
        q.drag            = v.drag;
        q.bounce          = v.bounce;
        q.life            = v.life;
        q.angle           = v.angle;
        q.angularVelocity = v.va;
        q.size            = v.size;
        q.color           = v.color;
        q.ramp            = std::move(v.ramp);
        bits.add(std::move(q));
    }
    void face(Paint& p, std::span<const uint32_t> K, double scale, double cracked) const {
        const double r = RC * scale;
        if (r < 1) return;
        p.disc(0, CY, r, K[1], [](double d) { return .3 + .35 * d * d; });
        p.ring(0, CY, r + .5, 1, K[6]);
        p.ring(0, CY, r - 1.5, 1, K[4]);
        p.ring(0, CY, r + 2, 1, K[3], .6);
        for (int i = 0; i < 60; ++i) {
            const double a = (double(i) / 60) * TAU;
            if (i % 5)
                p.dot(std::cos(a) * (r - 3.5), CY + std::sin(a) * (r - 3.5), K[4], .8);
            else {
                const bool   quarter = i % 15 == 0;
                const double r0      = r - (quarter ? 8 : 6.5);
                p.line(std::cos(a) * r0, CY + std::sin(a) * r0, std::cos(a) * (r - 3), CY + std::sin(a) * (r - 3),
                       quarter ? K[7] : K[6]);
            }
        }
        p.ring(0, CY, r * .3, 1, K[4], .8);
        const double ha = -PI / 2 + (hand(t) + PI / 2) / 12 + .9, sa = hand(t) + (frozen ? std::sin(t * 70) * .012 : 0);
        p.beam(0, CY, std::cos(ha) * r * .48, CY + std::sin(ha) * r * .48, 2.6,
               [&](double, double v) { return v < .5 ? K[6] : K[4]; });
        p.lineFn(
            -std::cos(sa) * 5, CY - std::sin(sa) * 5, std::cos(sa) * (r - 4), CY + std::sin(sa) * (r - 4),
            [&](double u) { return u > .75 ? K[7] : K[6]; }, 1.);
        p.line(-std::cos(sa) * 5 + .5, CY - std::sin(sa) * 5, -std::cos(sa) * 2, CY - std::sin(sa) * 2, K[5]);
        p.circle(0, CY, 2.2, K[5]);
        p.circle(0, CY, 1.2, K[7]);
        if (cracked > 0)
            for (const auto& pts : cracks) {
                const size_t n = size_t(std::floor(double(pts.size()) * cracked));
                for (size_t i = 0; i < n; ++i) {
                    const auto [x, y] = pts[i];
                    const bool fresh  = double(i) > double(n) - 3 && cracked < 1;
                    p.px(x, CY + y, fresh ? WHITE : K[7]);
                    if (!fresh) p.px(x, CY + y - 1, K[2]);
                }
            }
    }
    void update(double dt) override {
        t += dt;
        tc = clockTime(t);
        while (ticked < TICKS.size() && t >= TICKS[ticked]) {
            ++ticked;
            const double a = hand(t + .1);
            lights.add(std::cos(a) * (RC - 4), CY + std::sin(a) * (RC - 4), 22, C[6], .6, .12);
            for (int i = 0; i < 3; ++i)
                emit({ParticleKind::Star, std::cos(a) * (RC + 1), CY + std::sin(a) * (RC + 1),
                      std::cos(a) * 30 + R.range(-15, 15), std::sin(a) * 30 + R.range(-15, 15), R.range(.2, .4),
                      R.pick<uint32_t>(std::array<uint32_t, 2>{WHITE, C[6]}), R.range(.7, 1.3)});
        }
        if (!frozen && t >= FREEZE) {
            frozen = true;
            lights.add(0, CY, 70, WHITE, .7, .2);
        }
        if (!shattered && t >= SHATTER) {
            shattered = true;
            lights.add(0, CY, 80, WHITE, .8, .16);
            lights.add(0, CY, 100, C[5], .6, .6);
            for (int i = 0; i < 64; ++i) {
                const double a = R.range(0, TAU), d = R.range(0, RC), x = std::cos(a) * d, y = CY + std::sin(a) * d,
                             sp = R.range(40, 140), dx = x - ix, dy = y - CY - iy, raw = jsHypot2(dx, dy),
                             len = raw == 0 ? 1 : raw;
                chip({DebrisKind::Shard,
                      x,
                      y,
                      (dx / len) * sp + R.range(-25, 25),
                      (dy / len) * sp * .7 - R.range(40, 120),
                      .9,
                      .5,
                      .25,
                      R.range(.8, 1.15),
                      R.range(0, TAU),
                      R.range(-22, 22),
                      R.range(1.5, 3.6),
                      R.pick<uint32_t>(std::array<uint32_t, 4>{G[7], G[6], G[6], G[5]}),
                      {}});
            }
            for (int i = 0; i < 10; ++i) {
                const double a = R.range(0, TAU), sp = R.range(40, 110);
                chip({DebrisKind::Chunk,
                      std::cos(a) * RC * .6,
                      CY + std::sin(a) * RC * .6,
                      std::cos(a) * sp,
                      std::sin(a) * sp * .6 - R.range(60, 130),
                      1,
                      .4,
                      .3,
                      R.range(.9, 1.15),
                      R.range(0, TAU),
                      R.range(-14, 14),
                      R.range(1.6, 3),
                      WHITE,
                      {G[1], G[3], G[4], G[6], G[7]}});
            }
            for (int i = 0; i < 26; ++i) {
                const double a = R.range(0, TAU), sp = R.range(30, 110);
                emit({i % 2 ? ParticleKind::Star : ParticleKind::Mote, 0, CY, std::cos(a) * sp, std::sin(a) * sp,
                      R.range(.4, .8), R.pick<uint32_t>(std::array<uint32_t, 3>{WHITE, C[6], C[5]}), R.range(.8, 1.8)});
            }
            for (int i = 0; i < 18; ++i) {
                const double a = R.range(0, TAU), sp = R.range(90, 200);
                emit({ParticleKind::Spark, 0, CY, std::cos(a) * sp, std::sin(a) * sp, R.range(.2, .45)});
            }
            for (int i = 0; i < 12; ++i) {
                const double a = R.range(0, TAU), d = R.range(0, RC * .8), vx = std::cos(a) * R.range(10, 40),
                             vertical = R.range(10, 30), lift = R.range(5, 20), vy = std::sin(a) * vertical - lift;
                emit({ParticleKind::Vapour, std::cos(a) * d, CY + std::sin(a) * d, vx, vy, R.range(.6, 1),
                      R.isChance(.5) ? G[5] : C[4], R.range(3, 6)});
            }
        }
        bits.update(dt, [](double) { return 0.; });
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawMote(Paint& p, const Mote& m, bool back) const {
        const double a = m.angle + m.speed * tc, z = std::sin(a);
        if ((z < 0) != back) return;
        const double x = std::cos(a) * m.radius, y = CY + std::cos(a) * m.radius * m.tilt + z * m.radius * .3;
        const bool   cold = t >= FREEZE;
        const double v    = flow(t) * m.speed;
        p.dot(x, y, cold ? G[6] : m.color, back ? .6 : 1);
        if (std::abs(v) > .2)
            for (int k = 1; k <= 3; ++k) {
                const double b = a - (m.speed > 0 ? 1 : -1) * k * .05 * std::min(1., std::abs(v));
                p.dot(std::cos(b) * m.radius, CY + std::cos(b) * m.radius * m.tilt + std::sin(b) * m.radius * .3,
                      k < 2 ? C[5] : C[4], (back ? .5 : .9) * (1 - double(k) / 4));
            }
        if (!back) p.glow(x, y, 5, cold ? G[6] : C[6], .3);
    }
    void drawBack(Paint& p) const override {
        const double k = smooth(0, OPEN, t) * (1 - smooth(SHATTER, SHATTER + .5, t));
        if (k > 0) p.glow(0, -1, 40, t >= FREEZE ? G[5] : C[4], .25 * k);
        if (t < SHATTER)
            for (const auto& m : motes) drawMote(p, m, true);
    }
    void draw(Paint& p) const override {
        const bool   cold  = t >= FREEZE;
        const double scale = t < OPEN ? ease::outBack(t / OPEN, 2.6) : 1;
        if (t < SHATTER) {
            const bool              white = (t >= FREEZE && t < FREEZE + .06) || t >= SHATTER - .05;
            std::array<uint32_t, 8> whites{};
            whites.fill(WHITE);
            face(p,
                 white  ? std::span<const uint32_t>(whites)
                 : cold ? std::span<const uint32_t>(G)
                        : std::span<const uint32_t>(C),
                 scale, clamp((t - CRACK) / .18));
            p.glow(0, CY, RC * 1.9, cold ? G[5] : C[4], .32 * scale);
            p.glowRing(0, CY, RC, 7, cold ? G[6] : C[5], .28 * scale);
            for (const auto& m : motes) drawMote(p, m, false);
        }
        if (t < .35) {
            const double q = t / .35;
            p.ring(0, CY, RC * (.4 + 1.2 * ease::outCubic(q)), std::max(1., 3 * (1 - q)), q < .3 ? WHITE : C[6],
                   1.2 - q);
            if (q < .5) flare(p, 0, CY, 26 * (1 - q * 2) + 2, 1 - q * 2, C);
        }
        if (t < SHATTER)
            for (double tick : TICKS) {
                if (t < tick) continue;
                const double q = (tc - clockTime(tick)) / .32;
                if (q >= 1) continue;
                p.ring(0, CY, RC + 3 + 24 * ease::outCubic(q), 1, cold ? G[5] : C[5],
                       (cold ? .6 : .8) * std::pow(1 - q, 1.6));
            }
        if (t >= SHATTER && t < SHATTER + .2) {
            const double q = (t - SHATTER) / .2;
            flare(p, 0, CY, 34 * (1 - q) + 2, 1 - q, C);
            p.ring(0, CY, RC + 30 * ease::outCubic(q), std::max(1., 3 * (1 - q)), WHITE, 1.2 - q);
        }
        bits.draw(p);
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& timeStopDescriptor() {
    static const Descriptor d{
        {PixelFxKind::TimeStop, "arcane/time-stop", 196, 136, 98, 118, 3.6, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 14;
            s.palette = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                         hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<TimeStop>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
