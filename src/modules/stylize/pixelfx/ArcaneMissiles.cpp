// eve-hot-path
// Native adaptation of pixelfx arcane/arcane-missiles.js, MIT.
// Sprite Fusion energy bolt, Copyright (c) 2026 Hugo Duprez; see NOTICE.txt.
#include "common/Assert.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double HX = 15, HY = -24, TX = 152, TY = -22;
constexpr size_t N = 5, TRAIL = 18;
struct Bolt {
    double                   t0, x, y, vx, vy, hit = -1;
    bool                     on = false;
    double                   tx, ty, tw, acc = 0;
    std::array<Point, TRAIL> trail{};
    size_t                   count = 0;
};
struct Pop {
    double time, x, y;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct ArcaneMissiles final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         R;
    Particles                   ps;
    Lights                      lights;
    std::array<Bolt, N>         bolts{};
    std::array<Pop, N>          pops{};
    size_t                      popCount = 0;
    double                      t        = 0;
    explicit ArcaneMissiles(const PixelFxSettings& o) : C(o.palette), R(o.seed) {
        ps.sparks = {WHITE, C[6], C[5], C[4]};
        constexpr std::array<double, N> fan{-1.05, .7, -.5, 1.1, -1.4};
        for (size_t i = 0; i < N; ++i) {
            const double a = fan[i] + R.range(-.12, .12), sp = R.range(130, 165), tx = TX + R.range(-3, 3),
                         ty = TY + R.range(-6, 6), tw = R.range(0, TAU);
            bolts[i] = {.05 + double(i) * .1, HX, HY, std::cos(a) * sp, std::sin(a) * sp, -1, false, tx, ty, tw};
        }
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void ribbon(Paint& p, const Bolt& b) const {
        const auto&  pts  = b.trail;
        const size_t n    = b.count;
        const double fade = b.hit < 0 ? 1 : 1 - clamp((t - b.hit) / .16);
        if (n < 2 || fade <= 0) return;
        for (size_t i = 0; i < n - 1; ++i) {
            const auto [x0, y0] = pts[i];
            const auto [x1, y1] = pts[i + 1];
            const double raw = jsHypot2(x1 - x0, y1 - y0), len = raw == 0 ? 1 : raw, nx = -(y1 - y0) / len,
                         ny = (x1 - x0) / len;
            for (double d = 0; d < len; d += .5) {
                const double u = (double(i) + d / len) / double(n - 1), x = x0 + ((x1 - x0) * d) / len,
                             y = y0 + ((y1 - y0) * d) / len, a = (1 - u) * 1.7 * fade,
                             w = 2.6 * std::pow(1 - u, .8) + .3;
                for (double q = -w; q <= w; q += .5) {
                    const double v = std::abs(q) / w;
                    p.dot(x + nx * q, y + ny * q,
                          v < .35  ? (u < .25 ? WHITE : C[6])
                          : v < .7 ? (u < .4 ? C[6] : C[5])
                          : u < .5 ? C[5]
                                   : C[4],
                          a * (v < .7 ? 1 : .75));
                }
                for (double s : {0., PI}) {
                    const double ph = u * 10 - t * 26 + b.tw + s, off = std::sin(ph) * (w + 1.4);
                    if (std::cos(ph) > -.2)
                        p.dot(x + nx * off, y + ny * off, std::cos(ph) > .4 ? (u < .35 ? WHITE : C[6]) : C[4], a);
                }
            }
            if (i % 3 == 0)
                p.glow(x0, y0, 9 * (1 - double(i) / double(n)) + 3, C[5], .3 * (1 - double(i) / double(n)) * fade);
        }
    }
    void head(Paint& p, const Bolt& b) const {
        const double x = b.x, y = b.y;
        for (int i = 0; i < 4; ++i) {
            const double a = t * 24 + (i * TAU) / 4 + b.tw, l = 5.5 + 1.5 * std::sin(t * 50 + i);
            p.line(x + std::cos(a) * 3.2, y + std::sin(a) * 3.2, x + std::cos(a) * l, y + std::sin(a) * l, C[6]);
        }
        p.circle(x, y, 3.8, C[4]);
        p.circle(x, y, 3, C[5]);
        p.circle(x, y, 2.1, C[6]);
        p.circle(x, y, 1.2, WHITE);
        p.glow(x, y, 24, C[5], .6);
        p.glow(x, y, 7, WHITE, .4);
    }
    void pop(Paint& p, double q, double x, double y) const {
        if (q < .16) {
            const double r = 9 * (1 - q / .16) + 1.5;
            p.circle(x, y, r + 1.5, C[5]);
            p.circle(x, y, r, WHITE);
        }
        if (q < .4) {
            const double l = 15 * (1 - q / .4) + 3;
            for (double d = -l; d <= l; ++d) {
                const double   a = std::abs(d) / l;
                const uint32_t c = a < .3 ? WHITE : a < .65 ? C[6] : C[5];
                p.dot(x + d, y, c, 1.15 - a);
                p.dot(x, y + d * .7, c, 1.15 - a);
            }
        }
        const double rr = 3 + 18 * ease::outCubic(q);
        p.ring(x, y, rr + .5, std::max(1., 3 * (1 - q)), q < .3 ? WHITE : C[6], 1.2 - q);
    }
    void lock(Paint& p) const {
        const double k = smooth(0, .22, t) * (1 - smooth(1.15, 1.45, t));
        if (k <= 0) return;
        double kick = 0;
        for (size_t i = 0; i < popCount; ++i) kick = std::max(kick, std::exp(-std::pow((t - pops[i].time) / .06, 2)));
        const double r = 9 + 10 * (1 - ease::outCubic(smooth(0, .22, t))) + 3 * kick, spin = t * 1.5;
        for (int i = 0; i < 4; ++i) {
            const double a0 = spin + (i * TAU) / 4 - .38;
            for (double a = a0; a <= a0 + .76; a += .06)
                p.dot(TX + std::cos(a) * r, TY + std::sin(a) * r, kick > .4 ? WHITE : C[5], k);
        }
        p.glow(TX, TY, 12, C[4], .2 * k);
    }
    void update(double dt) override {
        t += dt;
        for (auto& b : bolts) {
            if (t < b.t0 || b.hit >= 0) continue;
            if (!b.on) {
                b.on = true;
                lights.add(HX, HY, 28, C[5], .7, .14);
                for (int i = 0; i < 4; ++i) {
                    const double ax = std::atan2(b.vy, b.vx) + R.range(-.6, .6), sx = R.range(60, 140);
                    const double ay = std::atan2(b.vy, b.vx) + R.range(-.6, .6), sy = R.range(60, 140);
                    emit({ParticleKind::Mote, HX, HY, std::cos(ax) * sx, std::sin(ay) * sy, R.range(.12, .25), C[6],
                          .6});
                }
            }
            const double age = t - b.t0, dx = b.tx - b.x, dy = b.ty - b.y, raw = jsHypot2(dx, dy),
                         d = raw == 0 ? 1 : raw, sp = 150 + 560 * age * age,
                         steer = std::min(1., (1.4 + 15 * age) * dt);
            b.vx += ((dx / d) * sp - b.vx) * steer;
            b.vy += ((dy / d) * sp - b.vy) * steer;
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            b.count = std::min(TRAIL, b.count + 1);
            for (size_t j = b.count - 1; j > 0; --j) b.trail[j] = b.trail[j - 1];
            b.trail[0] = {b.x, b.y};
            if ((b.acc += dt) > .012) {
                b.acc = 0;
                if (R.isChance(.6))
                    emit({ParticleKind::Mote, b.x, b.y, -b.vx * .08 + R.range(-25, 25), -b.vy * .08 + R.range(-25, 25),
                          R.range(.2, .4), R.isChance(.5) ? C[6] : C[5], .55});
            }
            if (d < 3 || age > 1.3) {
                b.hit = t;
                EV_ASSERT(popCount < pops.size());
                pops[popCount++] = {t, b.x, b.y};
                lights.add(b.x, b.y, 70, C[5], .9, .3);
                lights.add(b.x, b.y, 24, WHITE, .85, .09);
                for (int i = 0; i < 16; ++i) {
                    const double a = R.range(0, TAU), v = R.range(80, 230);
                    emit({ParticleKind::Spark, b.x, b.y, std::cos(a) * v, std::sin(a) * v - 40, R.range(.15, .45)});
                }
                for (int i = 0; i < 7; ++i) {
                    const double a = R.range(-PI, .3), v = R.range(40, 140);
                    emit({i % 2 ? ParticleKind::Star : ParticleKind::Mote, b.x, b.y, std::cos(a) * v, std::sin(a) * v,
                          R.range(.25, .48), R.pick<uint32_t>(std::array<uint32_t, 3>{WHITE, C[6], C[5]}),
                          R.range(.8, 1.6)});
                }
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        for (size_t i = 0; i < popCount; ++i) {
            const auto&  q = pops[i];
            const double k = 1 - clamp((t - q.time) / .5);
            if (k > 0) p.glow(q.x, -1, 22, C[5], .25 * k * k);
        }
    }
    void draw(Paint& p) const override {
        lock(p);
        for (const auto& b : bolts)
            if (b.on) ribbon(p, b);
        for (const auto& b : bolts)
            if (b.on && b.hit < 0) head(p, b);
        for (size_t i = 0; i < popCount; ++i) {
            const auto&  q = pops[i];
            const double k = (t - q.time) / .35;
            if (k < 1) pop(p, k, q.x, q.y);
        }
        if (t < .55) p.sparkle(HX, HY, 3 + 2 * std::sin(t * 40), C[6], C[5], .45 * (1 - t / .55));
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& arcaneMissilesDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::ArcaneMissiles, "arcane/arcane-missiles", 240, 116, 26, 88, 1.75, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 3;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                              hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<ArcaneMissiles>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
