// eve-hot-path
// Native adaptation of pixelfx ice/ice-shatter.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/IceFractures.h"
#include "stylize/pixelfx/IcePanes.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double               HIT = .26, CRACK2 = .4, SHIVER = .54, FLASH = .62, BURST = .68, D = 2.8;
constexpr std::array<Point, 4> FRONT{{{-16, 0}, {-16, -28}, {12, -28}, {12, 0}}},
    TOP{{{-16, -28}, {-8, -34}, {20, -34}, {12, -28}}}, SIDE{{{12, 0}, {12, -28}, {20, -34}, {20, -6}}},
    PLANE{{{-16, -21}, {-5, -28}, {3, -13}, {-16, -9}}};
constexpr std::array<Point, 6> SIL{{{-16, 0}, {-16, -28}, {-8, -34}, {20, -34}, {20, -6}, {12, 0}}};
constexpr std::array<Point, 3> SHADE{{{5, -28}, {12, -28}, {12, -10}}};
constexpr Point                HIT_AT{-16, -15};
struct Bubble {
    double x, y;
    bool   big;
};
struct Scuff {
    double x, t = 0;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
struct Chip {
    DebrisKind            kind;
    double                x, y, vx, vy, g, drag, bounce, life, a, va, s;
    uint32_t              c;
    std::vector<uint32_t> ramp{};
};
struct IceShatter final : Kernel {
    std::vector<uint32_t>   C;
    Rng                     R;
    Particles               ps;
    Bits                    bits;
    Lights                  lights;
    ice::Panes              panes;
    std::vector<Scuff>      scuffs;
    std::vector<Point>      veins;
    std::array<Bubble, 5>   bubbles{};
    std::vector<ice::Crack> cracks, cracks2;
    double                  t      = 0;
    bool                    struck = false, burst = false, wave2 = false, white = false;
    explicit IceShatter(const PixelFxSettings& o) : C(o.palette), R(o.seed) {
        ps.sparks = {C[7], C[6], C[5], C[4]};
        ps.haze   = C[5];
        for (int k = 0; k < 3; ++k) {
            double x = R.range(-12, 8), y = R.range(-24, -6), a = R.range(0, TAU);
            for (int i = 0; i < 9; ++i) {
                a += R.range(-.6, .6);
                x += std::cos(a) * 1.5;
                y += std::sin(a) * 1.2;
                if (x < -14 || x > 10 || y < -26 || y > -2) break;
                veins.push_back({std::floor(x), std::floor(y)});
            }
        }
        for (auto& q : bubbles) q = {double(R.integer(-12, 8)), double(R.integer(-23, -4)), R.isChance(.4)};
        const auto           inside = [](double x, double y) { return ice::isInsidePoly(SIL, x, y); };
        ice::FractureOptions f;
        f.length = 8;
        f.angle  = -.9;
        f.spread = 1.9;
        cracks   = ice::fractures(R, inside, HIT_AT.x + 1, HIT_AT.y, 4, f);
        f.length = 18;
        f.angle  = -1.3;
        f.spread = 2.7;
        f.fork   = .16;
        cracks2  = ice::fractures(R, inside, HIT_AT.x + 1, HIT_AT.y, 6, f);
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
            chip({DebrisKind::Shard, x + R.range(-2, 2), y - 1, vx * .6 + R.range(-35, 45), -R.range(30, 100), .8, .6,
                  .35, R.range(.4, .8), R.range(0, TAU), R.range(-18, 18), R.range(1.2, 2.4),
                  C[size_t(R.integer(4, 7))]});
    }
    /** @brief Build independent cut pieces from trusted corners using the instance RNG.
     * @cost At most five binary cutting levels per face, once at the authored burst event. */
    std::vector<std::vector<Point>> chop(std::span<const Point> pts, double max, int depth) {
        const auto c = ice::centroid(pts);
        if (c.area < max || depth > 4) return {std::vector<Point>(pts.begin(), pts.end())};
        const double                    x = c.x + R.range(-1, 1), y = c.y + R.range(-1, 1), a = R.range(0, PI);
        auto                            cut = ice::splitPoly(pts, x, y, a);
        std::vector<std::vector<Point>> out;
        for (const auto& q : cut) {
            auto parts = chop(q, max, depth + 1);
            for (auto& p : parts) out.push_back(std::move(p));
        }
        return out;
    }
    void explode() {
        struct Face {
            std::span<const Point> pts;
            double                 tone, max;
        };
        const std::array<Face, 3> faces{{{FRONT, .6, 46}, {TOP, 1, 40}, {SIDE, .3, 46}}};
        for (const auto& face : faces)
            for (const auto& part : chop(face.pts, face.max, 0)) {
                const auto   m  = ice::centroid(part);
                const double dx = m.x - HIT_AT.x, dy = m.y - HIT_AT.y, h = jsHypot2(dx, dy), d = h == 0 ? 1 : h,
                             sp = R.range(50, 150) * (.7 + .3 * (1 - d / 40));
                ice::Pane    q;
                for (const auto& p : part) q.pts.push_back({p.x - m.x, p.y - m.y});
                q.x       = m.x;
                q.y       = m.y;
                q.vx      = (dx / d) * sp * .9 + R.range(40, 110);
                q.vy      = (dy / d) * sp * .4 - R.range(60, 200);
                q.a       = 0;
                q.va      = R.range(-10, 10);
                q.f       = R.range(-.9, .9);
                q.vf      = R.range(-7, 7);
                q.tone    = face.tone;
                q.lit     = face.tone;
                q.g       = .9;
                q.drag    = .5;
                q.bounce  = .38;
                q.solid   = true;
                q.brittle = 3;
                q.crumble = R.range(.25, .6);
                q.life    = R.range(1.5, 1.9);
                panes.add(std::move(q));
            }
        for (int i = 0; i < 46; ++i) {
            const double x = R.range(-15, 19), y = -R.range(1, 33), dx = x - HIT_AT.x, dy = y - HIT_AT.y,
                         h = jsHypot2(dx, dy), d = h == 0 ? 1 : h, sp = R.range(60, 200);
            if (!ice::isInsidePoly(SIL, x, y)) continue;
            chip({R.isChance(.15) ? DebrisKind::Chunk : DebrisKind::Shard,
                  x,
                  y,
                  (dx / d) * sp * .8 + R.range(20, 90),
                  (dy / d) * sp * .4 - R.range(60, 240),
                  .85,
                  .6,
                  .32,
                  R.range(.8, 1.4),
                  R.range(0, TAU),
                  R.range(-22, 22),
                  R.range(1.2, 3.4),
                  C[size_t(R.integer(4, 7))],
                  {C[1], C[3], C[4], C[6], C[7]}});
        }
        for (int i = 0; i < 9; ++i)
            emit({ParticleKind::Vapour, R.range(-6, 18), -R.range(1, 6), R.range(10, 130), -R.range(2, 12),
                  R.range(.6, 1.1), C[6], R.range(2.2, 3.8)});
        for (int i = 0; i < 5; ++i)
            emit({ParticleKind::Dust, R.range(-6, 16), -R.range(1, 4), R.range(30, 110), -R.range(2, 8),
                  R.range(.5, .9), C[5], R.range(1.6, 2.8)});
        for (int i = 0; i < 4; ++i)
            emit({ParticleKind::Vapour, R.range(-8, 18), -R.range(8, 28), R.range(-10, 50), -R.range(8, 24),
                  R.range(.6, 1), C[6], R.range(2.6, 4)});
        for (int i = 0; i < 16; ++i) {
            const double a = R.range(-PI * .9, -.1), sp = R.range(40, 140);
            emit({ParticleKind::Flake, R.range(-8, 16), -R.range(6, 28), std::cos(a) * sp + 20, std::sin(a) * sp,
                  R.range(1, 1.6), R.isChance(.6) ? WHITE : C[6], R.range(.5, 1.6)});
        }
        for (int i = 0; i < 7; ++i)
            emit({ParticleKind::Star, R.range(-14, 40), -R.range(4, 40), R.range(-8, 16), -R.range(4, 16),
                  R.range(.4, .8), R.isChance(.5) ? WHITE : C[6], R.range(1, 2.4)});
        lights.add(2, -17, 64, WHITE, .85, .14);
        lights.add(16, -18, 86, C[4], .5, .55);
    }
    void update(double dt) override {
        t += dt;
        if (!struck && t >= HIT) {
            struck = true;
            lights.add(HIT_AT.x, HIT_AT.y, 24, WHITE, .6, .08);
            lights.add(HIT_AT.x + 6, HIT_AT.y, 40, C[5], .4, .3);
            for (int i = 0; i < 12; ++i) {
                const double a = PI + R.range(-.9, .9), sp = R.range(50, 120);
                emit({ParticleKind::Spark, HIT_AT.x - 1, HIT_AT.y, std::cos(a) * sp, std::sin(a) * sp - 40,
                      R.range(.15, .4)});
            }
            for (int i = 0; i < 6; ++i) {
                const double a = PI + R.range(-1, .8), sp = R.range(40, 100);
                chip({DebrisKind::Shard, HIT_AT.x - 1, HIT_AT.y + R.range(-2, 2), std::cos(a) * sp,
                      std::sin(a) * sp - 60, .85, .6, .3, R.range(.5, .9), R.range(0, TAU), R.range(-20, 20),
                      R.range(1.2, 2.4), C[size_t(R.integer(5, 7))]});
            }
        }
        if (!wave2 && t >= CRACK2) {
            wave2 = true;
            lights.add(0, -16, 36, C[6], .35, .15);
            for (int i = 0; i < 3; ++i)
                chip({DebrisKind::Shard, R.range(-14, 18), -R.range(4, 30), R.range(-30, 30), -R.range(10, 50), .85, .6,
                      .3, R.range(.4, .7), R.range(0, TAU), R.range(-20, 20), R.range(1, 2),
                      C[size_t(R.integer(5, 7))]});
        }
        if (!white && t >= FLASH) {
            white = true;
            lights.add(2, -17, 44, WHITE, .7, .08);
        }
        if (!burst && t >= BURST) {
            burst = true;
            explode();
        }
        if (burst && t < BURST + 1 && R.isChance(.08) && !bits.list.empty()) {
            const auto& b = bits.list[size_t(R.integer(0, int(bits.list.size()) - 1))];
            emit({ParticleKind::Star, b.x, b.y - 1, 0, -3, R.range(.2, .4), R.isChance(.5) ? WHITE : C[6],
                  R.range(.6, 1.5)});
        }
        for (size_t left = scuffs.size(); left > 0; --left)
            if ((scuffs[left - 1].t += dt) > .9) scuffs.erase(scuffs.begin() + std::ptrdiff_t(left - 1));
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        bits.update(dt, [](double) { return 0.; });
        panes.update(
            dt, [](double) { return 0.; },
            [&](const ice::Pane& q, double x, double y, double v) {
                if (v < 100) return;
                emit({ParticleKind::Vapour, x, y - 1, q.vx * .3 + R.range(-15, 15), -R.range(2, 10), R.range(.4, .7),
                      C[6], R.range(1.6, 2.8)});
                splinter(x, y, q.vx, 2);
                if (R.isChance(.3))
                    emit({ParticleKind::Star, x, y - 3, 0, -5, R.range(.2, .4), WHITE, R.range(.8, 1.6)});
            },
            [&](const ice::Pane& q) {
                splinter(q.x, 0, q.vx, 3);
                emit({ParticleKind::Vapour, q.x, -1, R.range(-10, 14), -R.range(2, 6), R.range(.4, .7), C[6],
                      R.range(1.2, 2)});
            },
            [&](const ice::Pane&, double x, double) {
                if (R.isChance(.5)) scuffs.push_back({jsRound(x + R.range(-1, 1)), 0});
            });
        lights.update(dt);
        const double left = std::max(.001, D - .05 - t);
        for (auto& q : ps.list)
            if (q.life > left) q.life = left;
        for (auto& q : bits.list)
            if (q.life > left) q.life = left;
        for (auto& q : panes.list)
            if (q.life > left) q.life = left;
    }
    double jolt() const {
        return t >= HIT && t < HIT + .05         ? 2
               : t >= HIT + .05 && t < HIT + .12 ? 1
               : t >= SHIVER && t < FLASH        ? (int(std::floor(t * 40)) % 2 ? 1 : 0)
                                                 : 0;
    }
    /** @brief Return independent shifted authored corners for this draw.
     * @cost Copies at most six points per face; no references escape the draw. */
    static std::vector<Point> shift(std::span<const Point> pts, double x) {
        std::vector<Point> out;
        out.reserve(pts.size());
        for (const auto& q : pts) out.push_back({q.x + x, q.y});
        return out;
    }
    void drawBlock(Paint& p) const {
        if (t >= BURST) return;
        const double                   jx = jolt(), sweep = -40 + 70 * seg(t, .04, .3);
        constexpr std::array<Point, 6> rim{{{-17, 1}, {-17, -28.5}, {-8.3, -35.2}, {21, -35.2}, {21, -5.6}, {12.5, 1}}};
        p.poly(shift(rim, jx), C[0]);
        if (t >= FLASH) {
            p.poly(shift(SIL, jx), WHITE);
            return;
        }
        p.poly(shift(FRONT, jx), [&](double x, double y) {
            const double lx = x - jx, d = lx + y * .8;
            if (std::abs(d - sweep) < 1.3 && y < -6) return WHITE;
            if (y < -9 && (std::abs(d + 21) < 1.1 || std::abs(d + 16.5) < .6)) return C[6];
            if (((std::abs(lx + 7.5) < .5 && y < -6) || (std::abs(y + 5.5) < .5 && lx > -8)) &&
                std::fmod(std::floor(lx) + std::floor(y), 2) == 0)
                return C[5];
            if (ice::isInsidePoly(PLANE, lx, y)) return y < -25.5 ? C[6] : C[5];
            if (ice::isInsidePoly(SHADE, lx, y)) return C[3];
            return y < -25.5 ? C[5] : y < -8 ? C[4] : y < -2.5 ? C[3] : C[2];
        });
        p.poly(shift(TOP, jx), [&](double x, double y) {
            return y > -29.5                                                       ? C[7]
                   : y < -32.5                                                     ? C[5]
                   : rand({jsUint(std::floor(x)), jsUint(std::floor(y)), 3}) < .08 ? C[7]
                                                                                   : C[6];
        });
        p.poly(shift(SIDE, jx), [&](double x, double y) {
            const double lx = x - jx;
            return lx < 13.5 ? C[3] : std::abs(lx - 16.5) < .6 && y<-8 ? C[3] : y>(lx - 12) * -.75 - 3 ? C[1] : C[2];
        });
        for (const auto& q : veins) p.px(q.x + jx, q.y, C[5]);
        for (const auto& q : bubbles) {
            p.px(q.x + jx, q.y, C[6]);
            if (q.big) {
                p.px(q.x + jx + 1, q.y, C[5]);
                p.px(q.x + jx, q.y + 1, C[5]);
            }
        }
        const auto edge = [&](Point a, Point b, uint32_t c) { p.line(a.x + jx, a.y, b.x + jx, b.y, c); };
        edge({-16, -28}, {12, -28}, WHITE);
        edge({-16, -28}, {-8, -34}, C[6]);
        edge({-8, -34}, {20, -34}, C[5]);
        edge({12, -28}, {20, -34}, C[7]);
        edge({-16, -1}, {-16, -28}, C[5]);
        edge({12, -1}, {12, -28}, C[6]);
        edge({20, -6}, {20, -34}, C[3]);
        edge({13, -1}, {20, -6}, C[1]);
        ice::FracturePaint o;
        o.x = jx;
        if (t >= HIT) ice::drawFractures(p, cracks, (t - HIT) * 170, C, o);
        if (t >= CRACK2) ice::drawFractures(p, cracks2, (t - CRACK2) * 200, C, o);
    }
    void drawFloor(Paint& p) const {
        const double out = 1 - smooth(BURST + .6, BURST + 1.5, t);
        if (out > 0)
            for (int x = -22; x <= 26; ++x) {
                const double d = std::abs(x - 2) / 24., h = rand({jsUint(x), 41}), a = out * clamp((1.1 - d) / .25);
                if (a <= 0) continue;
                p.dot(x, 0, h < .35 ? WHITE : C[6], a);
                if (h < .4 && (x < -16 || x > 20)) p.dot(x, -1, C[5], a);
                for (int j = 1; j <= 1 + jsRound(rand({jsUint(x), 7}) * 3 * (1 - d)); ++j)
                    p.dot(x, j, j < 2 ? C[5] : C[4], a * (1 - double(j) / 5));
            }
        for (const auto& s : scuffs) {
            const double a = 1 - s.t / .9;
            if (a > 0) {
                p.dot(s.x, 0, s.t < .15 ? WHITE : C[6], a);
                p.dot(s.x, 1, C[4], a * .6);
            }
        }
    }
    void drawBack(Paint& p) const override {
        drawFloor(p);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        drawBlock(p);
        const double h = t - HIT;
        if (h >= 0 && h < .2) {
            p.sparkle(HIT_AT.x - 1, HIT_AT.y, 9 * ease::outBack(std::min(1., h / .04)) * (1 - h / .2), C[6], C[4], .7);
            if (h < .08) flare(p, HIT_AT.x - 1, HIT_AT.y, 14 * (1 - h / .08), 1 - h / .08, C);
        }
        const double k = 1 - smooth(BURST, BURST + .4, t), crk = smooth(HIT, FLASH, t);
        if (k > 0) {
            p.glow(2, -17, 34, C[4], (.16 + .22 * crk) * k);
            p.glow(2, -2, 30, C[4], .14 * k);
        }
        if (t >= FLASH && t < BURST + .12)
            flare(p, 2, -17, 26 * (1 - clamp((t - FLASH) / .18)), 1 - clamp((t - FLASH) / .18), C);
        panes.draw(p, C);
        bits.draw(p);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& iceShatterDescriptor() {
    static const Descriptor d{
        {PixelFxKind::IceShatter, "ice/ice-shatter", 224, 112, 68, 90, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 6;
            s.palette = {hex(0x151a3d), hex(0x212f6b), hex(0x2c58aa), hex(0x3a8bd8),
                         hex(0x62c8f4), hex(0xa6ecff), hex(0xdcfbff), WHITE};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<IceShatter>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
