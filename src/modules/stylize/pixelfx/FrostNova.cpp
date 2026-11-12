// eve-hot-path
// Native adaptation of pixelfx ice/frost-nova.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double CHARGE = .42, SPREAD = .62, HOLD = 1.95, MELT = .7, CORE_Y = -24, D = 3.4;
struct Part {
    double dx, h, w, lean;
};
struct Site {
    double            x, d, h;
    bool              back;
    double            t0;
    bool              up;
    std::vector<Part> parts;
    double            phase;
};
struct Ring {
    double q0, q1, h0, h1, w0, w1;
    int    n, kids;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color;
    double       size = 1;
};
struct Chip {
    DebrisKind kind;
    double     x, y, vx, vy, g, drag, bounce, life, a, va, s;
    uint32_t   c;
};
void frost(Paint& p, std::span<const uint32_t> C, double x0, double x1, double reach, double a, double edge) {
    for (int x = int(std::floor(x0)); x <= int(std::ceil(x1)); ++x) {
        const double d = std::abs(x + .5), q = d / reach, k = clamp((a * 1.15 - q) / .15);
        if (k <= 0) continue;
        const double h     = rand({jsUint(x), 17});
        const bool   fresh = edge > 0 && d > edge - 3;
        const double tuft  = h < .45 ? jsRound((1 + rand({jsUint(x), 5}) * 3) * (1 - q * .6)) : 0,
                     depth = (4 + rand({jsUint(x), 11}) * 4) * (1 - q * .5);
        p.dot(x, 0, fresh || h < .25 ? WHITE : C[6], k);
        for (int j = 1; j <= tuft; ++j) p.dot(x, -j, j == tuft ? WHITE : C[5], k);
        if (tuft > 1 && rand({jsUint(x), 9}) < .5) p.dot(x + (rand({jsUint(x), 3}) < .5 ? -1 : 1), -tuft + 1, C[6], k);
        for (int j = 1; j <= depth; ++j) {
            const double v = j / depth;
            p.dot(x, j,
                  rand({jsUint(x * 31 + j), 7}) < .06 ? WHITE
                  : v < .25                           ? C[6]
                  : v < .55                           ? C[5]
                                                      : C[4],
                  k * (1.1 - v) * (fresh ? 1.3 : 1));
        }
    }
}
void snowflake(Paint& p, std::span<const uint32_t> C, double x, double y, double L, double spin, double k) {
    if (L < .8) return;
    for (int i = 0; i < 6; ++i) {
        const double a = spin + i * TAU / 6, ca = std::cos(a), sa = std::sin(a);
        for (double d = 0; d <= L; d += .5) p.dot(x + ca * d, y + sa * d, d < L * .4 ? WHITE : C[6], k + .2);
        if (L > 3)
            for (int s : {-1, 1}) {
                const double bx = x + ca * L * .6, by = y + sa * L * .6, ba = a + s * .9;
                for (double d = 0; d <= L * .35; d += .5) p.dot(bx + std::cos(ba) * d, by + std::sin(ba) * d, C[5], k);
            }
    }
    p.px(x, y, WHITE);
    p.glow(x, y, 10 + L * 3, C[5], .35 + .5 * k);
}
struct FrostNova final : Kernel {
    std::vector<uint32_t> C, DIM;
    Rng                   R;
    double                reach, t = 0;
    Particles             ps;
    Bits                  bits;
    Lights                lights;
    std::vector<Site>     sites;
    bool                  released = false;
    explicit FrostNova(const PixelFxSettings& o)
        : C(o.palette), DIM{C[0], C[0], C[1], C[2], C[3], C[4], C[5], C[6]}, R(o.seed), reach(o.ice.reach) {
        ps.sparks = {C[7], C[6], C[5], C[4]};
        ps.haze   = C[5];
        constexpr std::array<Ring, 3> rings{
            {{.15, .24, 24, 32, 10, 13, 2, 2}, {.36, .52, 13, 19, 7, 9, 3, 1}, {.62, .9, 5, 10, 4, 6, 4, 0}}};
        for (int side : {-1, 1})
            for (size_t ri = 0; ri < rings.size(); ++ri) {
                const auto& ring = rings[ri];
                for (int i = 0; i < ring.n; ++i) {
                    const double      q = ring.q0 + (ring.q1 - ring.q0) * (i + R.range(.2, .8)) / ring.n,
                                      h = R.range(ring.h0, ring.h1), w = R.range(ring.w0, ring.w1);
                    std::vector<Part> parts{{0, h, w, side * h * R.range(.1, .28)}};
                    for (int k = 0; k < ring.kids; ++k) {
                        const int    s  = k % 2 ? -side : side;
                        const double kh = h * R.range(.4, .62);
                        Part         part{s * R.range(.3, .5) * w, kh, w * .62, s * kh * R.range(.35, .6)};
                        parts.insert(parts.begin(), part);
                    }
                    const double t0 = reachedAt(q * reach) + R.range(0, .03), phase = R.next();
                    sites.push_back({side * q * reach, q * reach, h, (i + int(ri) + (side > 0 ? 1 : 0)) % 2 == 1, t0,
                                     false, std::move(parts), phase});
                }
            }
    }
    double front() const { return t < CHARGE ? 0 : reach * (1 - std::pow(1 - clamp((t - CHARGE) / SPREAD), 3)); }
    double reachedAt(double d) const { return CHARGE + SPREAD * (1 - std::cbrt(1 - std::min(1.0, d / reach))); }
    double sink() const { return smooth(HOLD, HOLD + MELT, t); }
    void   emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void   chip(const Chip& v) {
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
        bits.add(std::move(q));
    }
    uint32_t shardColor() { return C[std::array<size_t, 3>{7, 6, 5}[size_t(R.integer(0, 2))]]; }
    void     update(double dt) override {
        t += dt;
        if (t < CHARGE - .06) {
            if (R.isChance(.9)) {
                const double a = R.range(0, TAU), distance = R.range(26, 58), T = R.range(.2, .3),
                             v = distance * 3 / (std::exp(3 * T) - 1), x = std::cos(a) * distance,
                             y = std::min(CORE_Y + std::sin(a) * distance * .8, -2.0), len = jsHypot2(x, y - CORE_Y),
                             l = len == 0 ? 1 : len;
                emit({ParticleKind::Streak, x, y, -x / l * v, (CORE_Y - y) / l * v, T, R.isChance(.5) ? C[6] : C[5]});
            }
            if (R.isChance(.14)) {
                const double s = R.sign();
                emit({ParticleKind::Vapour, s * R.range(28, 60), -R.range(0, 2), -s * R.range(35, 70), -R.range(0, 3),
                      R.range(.35, .5), C[6], R.range(2, 3)});
            }
        }
        if (t >= CHARGE && !released) {
            released = true;
            lights.add(0, CORE_Y, 84, C[4], .8, .55);
            lights.add(0, CORE_Y, 45, WHITE, .7, .12);
            for (int i = 0; i < 22; ++i) {
                const double a = R.range(-PI, .25), sp = R.range(110, 280);
                chip({DebrisKind::Shard, R.range(-3, 3), CORE_Y, std::cos(a) * sp, std::sin(a) * sp, .7, .6, .3,
                      R.range(.9, 1.5), R.range(0, TAU), R.range(-20, 20), R.range(1.5, 3.5), shardColor()});
            }
            for (int i = 0; i < 14; ++i) {
                const double a = R.range(-PI, 0), sp = R.range(40, 160);
                emit({ParticleKind::Flake, 0, CORE_Y, std::cos(a) * sp, std::sin(a) * sp, R.range(1.2, 1.8),
                      R.isChance(.6) ? WHITE : C[6], R.range(.5, 1.6)});
            }
        }
        const double fr = front();
        if (t > CHARGE && t < CHARGE + SPREAD)
            for (int s : {-1, 1}) {
                emit({ParticleKind::Vapour, s * fr + R.range(-2, 2), -R.range(0, 5), s * R.range(15, 75),
                      -R.range(2, 15), R.range(.6, 1), R.isChance(.7) ? C[6] : C[5],
                      R.range(3, 5.5) * (1 - .4 * (fr / reach))});
                if (R.isChance(.45))
                    emit({ParticleKind::Flake, s * fr, -2, s * R.range(10, 45), -R.range(30, 85), R.range(1.1, 1.9),
                          R.isChance(.5) ? WHITE : C[6], R.range(.5, 1.2)});
            }
        for (auto& s : sites)
            if (!s.up && t >= s.t0) {
                s.up = true;
                for (int i = 0; i < 2 + jsRound(s.h / 8); ++i) {
                    const double a = -PI / 2 + R.range(-1.3, 1.3), sp = R.range(60, 200) * (.5 + s.h / 40);
                    chip({DebrisKind::Shard, s.x + R.range(-3, 3), 0, std::cos(a) * sp, std::sin(a) * sp, .8, .6, .3,
                          R.range(.9, 1.6), R.range(0, TAU), R.range(-18, 18), R.range(1.5, 3), shardColor()});
                }
                emit({ParticleKind::Vapour, s.x, -1, R.range(-30, 30), -R.range(2, 8), R.range(.5, .9), C[6],
                      R.range(2.5, 4)});
                if (s.h > 10)
                    emit({ParticleKind::Star, s.x + R.range(-3, 3), -R.range(4, s.h * .8), 0, -3, R.range(.3, .6), C[6],
                          R.range(1, 2.4)});
            }
        if (t > CHARGE + .2 && t < HOLD + .2) {
            if (R.isChance(.5)) {
                const auto& s = sites[size_t(R.integer(0, int(sites.size()) - 1))];
                if (s.up)
                    emit({ParticleKind::Star, s.x + R.range(-3, 3), -R.range(2, s.h * .9), 0, -3, R.range(.3, .6),
                          R.isChance(.6) ? C[6] : WHITE, R.range(.6, 2.2)});
            }
            if (t < HOLD - .3 && R.isChance(.16))
                emit({ParticleKind::Flake, R.range(-1, 1) * fr, -R.range(40, 90), R.range(-5, 5), R.range(3, 10),
                      R.range(1.1, 1.5), R.isChance(.5) ? WHITE : C[6], R.range(.5, 1.4)});
        }
        if (t > HOLD && t < HOLD + MELT) {
            if (R.isChance(.5)) {
                const auto& s = sites[size_t(R.integer(0, int(sites.size()) - 1))];
                Debris      q;
                q.kind    = DebrisKind::Drip;
                q.x       = s.x + R.range(-3, 3);
                q.y       = -R.range(3, s.h * .6 * (1 - sink()));
                q.vy      = 10;
                q.gravity = .6;
                q.life    = 1;
                q.color   = C[5];
                bits.add(std::move(q));
            }
            if (R.isChance(.25))
                emit({ParticleKind::Vapour, R.range(-1, 1) * reach * .8, -R.range(0, 3), R.range(-10, 10),
                      -R.range(6, 16), R.range(.6, 1), C[5], R.range(3, 5)});
        }
        const auto floor = [](double) { return 0.0; };
        ps.update(dt, R, floor, [](double, double) {});
        bits.update(dt, floor);
        lights.update(dt);
    }
    void drawCrystals(Paint& p, bool back) const {
        const double m = sink();
        for (const auto& s : sites) {
            if (!s.up || s.back != back || m >= 1) continue;
            const double g     = ease::outBack(clamp((t - s.t0) / .1), 2.6),
                         sw    = std::fmod((t - s.t0) * .7 + s.phase * 3, 1.6) / .5;
            const bool   white = t < s.t0 + .03;
            for (const auto& q : s.parts)
                crystal(p, s.x + q.dx, back ? -1 : 0, q.h * g * (1 - m), q.w * (1 - m * .3), q.lean * g * (1 - m),
                        back ? DIM : C, {1, white, sw <= 1 ? sw : -1});
        }
    }
    void ring(Paint& p, bool back) const {
        if (t < CHARGE) return;
        const double u = clamp((t - CHARGE) / (SPREAD + .15));
        if (u >= 1) return;
        band(
            p, 0, 0, front() + 2, .16, std::max(2.0, 10 * std::pow(1 - u, .7)), u < .75 ? 1 : (1 - u) / .25, C,
            [back](double g) { return (std::sin(g) < 0) == back; }, t - CHARGE < .1);
        if (!back)
            for (int s : {-1, 1}) p.glow(s * (front() + 2), -2, 22, C[5], .55 * (1 - u));
    }
    void drawBack(Paint& p) const override {
        const double out = 1 - smooth(HOLD + .2, HOLD + MELT + .6, t), fr = front();
        if (out > 0 && t > CHARGE) frost(p, C, -std::max(12.0, fr + 1), std::max(12.0, fr + 1), reach, out, fr);
        ring(p, true);
        drawCrystals(p, true);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        ring(p, false);
        drawCrystals(p, false);
        if (t < CHARGE) {
            const double k = t / CHARGE, v = t - CHARGE * .55, w = t - CHARGE * .85,
                         beat = 1 + .25 * std::exp(-(v * v) / .002) + .3 * std::exp(-(w * w) / .002);
            snowflake(p, C, 0, CORE_Y, (2 + 5.5 * k * k) * beat, t * 4, k);
        } else if (t < CHARGE + .22) {
            const double a = (t - CHARGE) / .22;
            flare(p, 0, CORE_Y, 36 * std::pow(1 - a, 1.5), 1 - a, C);
            p.glow(0, CORE_Y, 46, WHITE, .6 * (1 - a));
        }
        if (t > CHARGE) {
            const double k = (1 - smooth(HOLD, HOLD + MELT + .3, t)) * (.85 + .15 * std::sin(t * 2.4)), fr = front();
            for (int i = -3; i <= 3; ++i) p.glow(i / 3.5 * fr, -4, 26, C[4], .16 * k);
        }
        bits.draw(p);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& frostNovaDescriptor() {
    static const std::array<Option, 1> options{{{"reach", 16, 512, false, 0, &PixelFxIceSettings::reach}}};
    static const Descriptor            d{
        {PixelFxKind::FrostNova, "ice/frost-nova", 288, 192, 144, 150, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed      = 4;
            s.ice.reach = 104;
            s.palette   = {hex(0x151a3d), hex(0x212f6b), hex(0x2c58aa), hex(0x3a8bd8),
                           hex(0x62c8f4), hex(0xa6ecff), hex(0xdcfbff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<FrostNova>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
