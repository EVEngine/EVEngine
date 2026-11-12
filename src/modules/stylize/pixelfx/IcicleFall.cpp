// eve-hot-path
// Native adaptation of pixelfx ice/icicle-fall.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/IcePanes.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double ROOT = -80, LEN = 36, W = 12, DRIP = .16, TREMBLE = .3, SNAP = .6, FREE = SNAP + .04, D = 2.6;
const double     IMPACT = FREE + std::sqrt((2 * (-ROOT - LEN)) / GRAVITY);
void             icicle(Paint& p, std::span<const uint32_t> C, double x, double top, bool white, double glint) {
    p.shader(x - W / 2 - 4, top - 1, x + W / 2 + 4, top + LEN + 1, [&](double px, double py, ShadeOutput& o) {
        const double u = (py - top) / LEN, cx = x + .7 * std::sin(u * 6 + 1), s = px - cx;
        if (u < 0 || u > 1) return;
        const double h = (W / 2) * std::pow(1 - u, .75) * (1 + .14 * std::sin(u * 13 + .5)) * (u < .08 ? 1.15 : 1) + .3;
        if (std::abs(s) > h + 1) return;
        if (std::abs(s) > h) {
            o.c = C[1];
            return;
        }
        if (white) {
            o.c = WHITE;
            return;
        }
        const double q    = s / h;
        const bool   ring = std::sin(u * 13 + .5) > .7;
        uint32_t     c    = u > .88                                       ? WHITE
                            : q < -.72                                    ? C[6]
                            : q < -.2                                     ? (ring ? C[7] : C[5])
                            : q < .18                                     ? (u < .72 ? C[7] : C[6])
                            : q > .8                                      ? C[2]
                            : std::abs(q - .45) < .1 && u > .15 && u < .6 ? C[5]
                            : q < .6                                      ? C[4]
                                                                          : C[3];
        if (glint >= 0 && q < .25 && std::abs(u - glint) < .06) c = WHITE;
        o.c = c;
    });
}
struct Rime {
    int x, h, stub;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color;
    double       size = 1, delay = 0;
};
struct Chip {
    DebrisKind            kind;
    double                x, y, vx, vy, g, drag, bounce, life, a, va, s;
    uint32_t              c;
    std::vector<uint32_t> ramp{};
};
struct IcicleFall final : Kernel {
    std::vector<uint32_t> C;
    Rng                   R;
    Particles             ps;
    Bits                  bits;
    Lights                lights;
    ice::Panes            panes;
    std::array<Rime, 37>  rime{};
    double                t       = 0;
    bool                  snapped = false, landed = false, dripped = false;
    explicit IcicleFall(const PixelFxSettings& o) : C(o.palette), R(o.seed) {
        ps.sparks = {C[7], C[6], C[5], C[4]};
        ps.haze   = C[5];
        for (int x = -18; x <= 18; ++x) {
            const double h = rand({jsUint(x), o.seed, 2}), d = std::abs(x) / 18.;
            rime[size_t(x + 18)] = {
                x, 1 + int(jsRound((.6 + h * 1.8) * (1 - d * .7))),
                h < .3 && std::abs(x) > 7 ? 2 + int(jsRound(rand({jsUint(x), 5}) * 5 * (1 - d * .5))) : 0};
        }
    }
    double top() const {
        const double q = t - FREE;
        return ROOT + (t < FREE ? 0 : .5 * GRAVITY * (q * q));
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    void chip(Chip v) {
        Debris q;
        q.kind            = v.kind;
        q.x               = v.x;
        q.y               = v.y;
        q.vx              = v.vx;
        q.vy              = v.vy;
        q.gravity         = v.g;
        q.drag            = v.drag;
        q.bounce          = v.bounce;
        q.life            = v.life;
        q.angle           = v.a;
        q.angularVelocity = v.va;
        q.size            = v.s;
        q.color           = v.c;
        q.ramp            = std::move(v.ramp);
        bits.add(std::move(q));
    }
    void splinter(double x, double y, double vx, int n) {
        for (int i = 0; i < n; ++i)
            chip({DebrisKind::Shard, x + R.range(-2, 2), y - 1, vx * .5 + R.range(-45, 45), -R.range(30, 100), .8, .6,
                  .35, R.range(.4, .75), R.range(0, TAU), R.range(-18, 18), R.range(1.2, 2.4),
                  C[size_t(R.integer(4, 7))]});
    }
    void impact() {
        landed              = true;
        constexpr double tp = -LEN;
        lights.add(0, -16, 46, WHITE, .85, .12);
        lights.add(0, -16, 58, C[4], .5, .5);
        std::vector<std::vector<Point>> parts{{{-W / 2, tp}, {W / 2, tp}, {.5, 0}}};
        for (double v : {.3, .58, .8}) {
            std::vector<std::vector<Point>> next;
            for (const auto& poly : parts) {
                const double y = tp + LEN * v + R.range(-2, 2), a = R.range(-.4, .4);
                auto         cut = ice::splitPoly(poly, 0, y, a);
                for (auto& q : cut) next.push_back(std::move(q));
            }
            parts = std::move(next);
        }
        std::vector<std::vector<Point>> next;
        for (auto& poly : parts) {
            const auto m = ice::centroid(poly);
            if (m.area > 22) {
                const double a   = PI / 2 + R.range(-.5, .5);
                auto         cut = ice::splitPoly(poly, m.x, m.y, a);
                for (auto& q : cut) next.push_back(std::move(q));
            } else
                next.push_back(std::move(poly));
        }
        parts = std::move(next);
        for (const auto& part : parts) {
            const auto   m    = ice::centroid(part);
            const int    side = m.x < .5 ? -1 : 1;
            const double low  = clamp(-m.y / LEN);
            ice::Pane    q;
            for (const auto& p : part) q.pts.push_back({p.x - m.x, p.y - m.y});
            q.x                = m.x;
            q.y                = m.y;
            const double speed = R.range(30, 110), bias = R.range(-15, 15);
            q.vx      = side * speed + bias;
            q.vy      = -R.range(70, 200) * (.4 + low);
            q.a       = 0;
            q.va      = R.range(-12, 12);
            q.f       = R.range(-.7, .7);
            q.vf      = R.range(-8, 8);
            q.lit     = .75;
            q.g       = .9;
            q.drag    = .6;
            q.bounce  = .35;
            q.solid   = true;
            q.ridge   = PI / 2;
            q.brittle = 2;
            q.crumble = R.range(.15, .45);
            q.life    = R.range(1, 1.4);
            panes.add(std::move(q));
        }
        for (int i = 0; i < 52; ++i) {
            const double a = -PI / 2 + R.range(-1.25, 1.25), sp = R.range(60, 230);
            chip({R.isChance(.15) ? DebrisKind::Chunk : DebrisKind::Shard,
                  R.range(-3, 3),
                  -R.range(1, 10),
                  std::cos(a) * sp,
                  std::sin(a) * sp,
                  .85,
                  .6,
                  .32,
                  R.range(.6, 1.2),
                  R.range(0, TAU),
                  R.range(-22, 22),
                  R.range(1.2, 3),
                  C[size_t(R.integer(4, 7))],
                  {C[1], C[3], C[4], C[6], C[7]}});
        }
        for (int i = 0; i < 10; ++i) {
            const int s = R.sign();
            emit({ParticleKind::Vapour, s * R.range(0, 8), -R.range(1, 6), s * R.range(25, 90), -R.range(2, 14),
                  R.range(.6, 1.1), C[6], R.range(2.2, 3.8)});
        }
        for (int i = 0; i < 12; ++i)
            emit({ParticleKind::Flake, R.range(-6, 6), -R.range(2, 16), R.range(-70, 70), -R.range(30, 110),
                  R.range(1, 1.5), R.isChance(.6) ? WHITE : C[6], R.range(.5, 1.5)});
        for (int i = 0; i < 6; ++i)
            emit({ParticleKind::Star, R.range(-24, 24), -R.range(4, 34), R.range(-10, 10), -R.range(4, 14),
                  R.range(.4, .8), R.isChance(.5) ? WHITE : C[6], R.range(1, 2.2)});
    }
    void update(double dt) override {
        t += dt;
        if (!dripped && t >= DRIP) {
            dripped = true;
            Debris q;
            q.kind    = DebrisKind::Drip;
            q.x       = .5;
            q.y       = ROOT + LEN + 1;
            q.vy      = 20;
            q.gravity = .9;
            q.life    = 1.2;
            q.color   = C[6];
            bits.add(std::move(q));
        }
        if (t > TREMBLE && t < SNAP && R.isChance(.25))
            emit({ParticleKind::Flake, R.range(-6, 6), ROOT + R.range(0, 3), R.range(-8, 8), R.range(5, 20),
                  R.range(.6, 1.1), R.isChance(.5) ? WHITE : C[6], R.range(.5, 1.1)});
        if (!snapped && t >= SNAP) {
            snapped = true;
            lights.add(0, ROOT + 4, 24, WHITE, .6, .1);
            lights.add(0, ROOT + 4, 32, C[5], .4, .3);
            for (int i = 0; i < 7; ++i) {
                const double a = R.range(0, TAU), sp = R.range(30, 110);
                chip({DebrisKind::Shard, R.range(-4, 4), ROOT + 2, std::cos(a) * sp, std::sin(a) * sp - 20, .8, .8, .3,
                      R.range(.5, .9), R.range(0, TAU), R.range(-20, 20), R.range(1, 2), C[size_t(R.integer(5, 7))]});
            }
            for (int i = 0; i < 3; ++i)
                emit({ParticleKind::Vapour, R.range(-5, 5), ROOT + 2, R.range(-25, 25), R.range(-5, 5), R.range(.4, .7),
                      C[6], R.range(1.6, 2.6)});
            for (int i = 0; i < 5; ++i)
                emit({ParticleKind::Flake, R.range(-6, 6), ROOT + 2, R.range(-30, 30), R.range(-20, 20),
                      R.range(.6, 1.2), R.isChance(.5) ? WHITE : C[6], R.range(.5, 1.3)});
        }
        if (t > FREE && t < IMPACT && R.isChance(.5))
            emit({ParticleKind::Mote, R.range(-3, 3), top() + R.range(0, LEN * .6), R.range(-10, 10), R.range(-40, -10),
                  R.range(.2, .35), R.isChance(.5) ? C[6] : C[5], .6});
        if (!landed && t >= IMPACT) impact();
        if (landed && t < IMPACT + .9 && R.isChance(.08) && !bits.list.empty()) {
            const auto& b = bits.list[size_t(R.integer(0, int(bits.list.size()) - 1))];
            emit({ParticleKind::Star, b.x, b.y - 1, 0, -3, R.range(.2, .4), R.isChance(.5) ? WHITE : C[6],
                  R.range(.6, 1.4)});
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        bits.update(dt, [](double) { return 0.; });
        panes.update(
            dt, [](double) { return 0.; },
            [&](const ice::Pane& q, double x, double y, double v) {
                if (v < 100) return;
                emit({ParticleKind::Vapour, x, y - 1, R.range(-15, 15), -R.range(2, 8), R.range(.4, .6), C[6],
                      R.range(1.4, 2.2)});
                splinter(x, y, q.vx, 2);
            },
            [&](const ice::Pane& q) { splinter(q.x, 0, q.vx, 3); }, [](const ice::Pane&, double, double) {});
        lights.update(dt);
        const double left = std::max(.001, D - .05 - t);
        for (auto& q : ps.list)
            if (q.life > left) q.life = left;
        for (auto& q : bits.list)
            if (q.life > left) q.life = left;
        for (auto& q : panes.list)
            if (q.life > left) q.life = left;
    }
    void drawBand(Paint& p, bool front) const {
        const double k = seg(t, IMPACT, IMPACT + .35);
        if (k >= 1) return;
        const std::array<uint32_t, 4> ramp{{C[3], C[4], C[5], C[6]}};
        band(
            p, 0, .5, 6 + 34 * ease::outCubic(k), .18, std::max(1., 4 * (1 - k)), 1.1 - k, ramp,
            [&](double a) { return front ? std::sin(a) >= 0 : std::sin(a) < 0; }, front && k < .15);
    }
    void drawBack(Paint& p) const override {
        if (t > TREMBLE && t < IMPACT + .05) {
            const double k = t < FREE ? seg(t, TREMBLE, FREE) * .35 : .35 + .65 * seg(t, FREE, IMPACT), rx = 3 + 7 * k;
            for (int x = int(std::floor(-rx)); x <= rx; ++x) {
                const double f = 1 - (x / rx) * (x / rx);
                p.blend(x + .5, 0, C[0], .7 * k * f);
                if (std::abs(x) < rx * .7) p.blend(x + .5, 1, C[0], .45 * k * f);
            }
        }
        if (t >= IMPACT) {
            const double g   = ease::outCubic(seg(t, IMPACT, IMPACT + .25)),
                         out = 1 - smooth(IMPACT + .8, IMPACT + 1.5, t);
            for (int x = -26; x <= 26; ++x) {
                const double d = std::abs(x + .5) / 26, h = rand({jsUint(x), 23}),
                             a = out * clamp((g * 1.15 - d) / .15);
                if (a <= 0) continue;
                p.dot(x, 0, h < .35 || d > g - .12 ? WHITE : C[6], a);
                if (h < .4)
                    for (int j = 1; j <= jsRound((1 + rand({jsUint(x), 3}) * 2.5) * (1 - d)); ++j)
                        p.dot(x + (x > 0 ? 1 : x < 0 ? -1 : 0) * (j > 1 ? 1 : 0), -j, j > 1 ? WHITE : C[5], a);
                for (int j = 1; j <= jsRound((2 + rand({jsUint(x), 7}) * 3) * (1 - d * .6)); ++j)
                    p.dot(x, j, j < 2 ? C[5] : C[4], a * (1.1 - double(j) / 6));
            }
            drawBand(p, false);
        }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const double ra = 1 - smooth(D - .7, D - .2, t);
        for (const auto& r : rime) {
            for (int j = 0; j < r.h + 1; ++j)
                p.dot(r.x, ROOT - 1 - j, j == r.h ? WHITE : j == r.h - 1 ? C[6] : j == 0 ? C[3] : C[5], ra);
            for (int j = 0; j < r.stub; ++j) p.dot(r.x, ROOT + j, j == r.stub - 1 ? WHITE : j == 0 ? C[5] : C[4], ra);
            if (r.stub > 4) p.dot(r.x + 1, ROOT, C[3], ra);
        }
        if (snapped)
            for (int x = int(-W / 2 + 1); x <= W / 2 - 1; ++x) {
                const int L = 1 + int(jsRound(rand({jsUint(x), 77}) * 2.5));
                for (int j = 0; j < L; ++j)
                    p.dot(x, ROOT + j, j == L - 1 ? (t < SNAP + .15 ? WHITE : C[6]) : x < 0 ? C[5] : C[3], ra);
            }
        p.glow(0, ROOT, 18, C[4], .3 * ra);
        if (t >= IMPACT) {
            drawBand(p, true);
            if (t < IMPACT + .22)
                p.sparkle(.5, -3, 10 * ease::outBack(std::min(1., (t - IMPACT) / .05)) * (1 - (t - IMPACT) / .22), C[6],
                          C[4], .7);
        }
        if (t < IMPACT) {
            const double tp = top(), jx = t > TREMBLE && t < SNAP ? (int(std::floor(t * 36)) % 2 ? 1 : 0) *
                                                                        jsRound(seg(t, TREMBLE, SNAP) * 1.4 + .4)
                                                                  : 0;
            const bool   falling = t >= FREE;
            const double sp      = falling ? GRAVITY * (t - FREE) : 0;
            if (falling)
                for (const auto& q : std::array<Point, 3>{{{-3, .7}, {.5, 1}, {4, .6}}}) {
                    const double L = std::min(30., sp * .07) * q.y;
                    if (L > 2)
                        p.lineFn(
                            q.x, tp - L, q.x, tp + 3, [&](double u) { return u > .75  ? C[6]
                                                                             : u > .4 ? C[4]
                                                                                      : C[3]; },
                            1.);
                }
            const double gl = (t - .04) / .3;
            icicle(p, C, jx + .5, tp, t >= IMPACT - .025 || (t >= SNAP && t < SNAP + .03),
                   gl >= 0 && gl <= 1 ? gl : -1);
            if (snapped && t < SNAP + .12) p.line(-W / 2 - 1, ROOT + 1, W / 2 + 1, ROOT + 1, WHITE);
            p.glow(.5, tp + LEN * .5, 20, C[4], .28);
            if (t < DRIP) {
                const double s = seg(t, 0, DRIP);
                p.dot(.5, ROOT + LEN + 1, C[6], s);
                if (s > .6) p.px(.5, ROOT + LEN + 2, WHITE);
            }
        }
        panes.draw(p, C);
        bits.draw(p);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& icicleFallDescriptor() {
    static const Descriptor d{
        {PixelFxKind::IcicleFall, "ice/icicle-fall", 152, 120, 76, 100, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 4;
            s.palette = {hex(0x151a3d), hex(0x212f6b), hex(0x2c58aa), hex(0x3a8bd8),
                         hex(0x62c8f4), hex(0xa6ecff), hex(0xdcfbff), WHITE};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<IcicleFall>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
