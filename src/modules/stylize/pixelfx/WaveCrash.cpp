// eve-hot-path
// Native adaptation of pixelfx water/wave-crash.js (MIT), including Sprite Fusion slash lighting.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "common/Assert.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/WaterSimulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double CRASH = 1.2, LIP0 = .62, EDGE = 110, D = 3.2;
double           crestX(double t) {
    return t < CRASH ? -80 + 72 * ease::inOutSine(t / CRASH) : -8 + 30 * ease::outCubic(clamp((t - CRASH) / 1.4));
}
double crestH(double t) {
    return t < CRASH ? 5 + 33 * ease::inOutSine(clamp(t / (CRASH * .92)))
                     : 38 * (1 - ease::outCubic(clamp((t - CRASH) / .45))) + 5 * (1 - smooth(CRASH, CRASH + 1.6, t));
}
double faceW(double t) { return t < CRASH ? 24 - 16 * smooth(.3, CRASH, t) : 8 + 14 * smooth(CRASH, CRASH + .6, t); }
double lipK(double t) { return clamp((t - LIP0) / (CRASH - LIP0)); }
std::array<double, 3> lip(double s, double t) {
    const double k = lipK(t), A = crestH(t);
    return {crestX(t) + 1 + s * 30 * std::pow(k, .8),
            -A - 4 * s * (1 - s) * (1 + 4 * k) + s * s * A * ease::inQuad(k) * .98, (11 - 8.5 * s) * (.5 + .5 * k)};
}
struct WaveCrash final : Kernel {
    std::vector<uint32_t> C;
    uint32_t              seed;
    Rng                   rng;
    double                t       = 0;
    bool                  crashed = false;
    Particles             ps;
    water::Mist           mist;
    Lights                lights;
    water::Ripples        rings;
    water::Drops          drops;
    struct Puff {
        double x, y, vx, vy, r0, r1, life, max, delay;
    };
    struct Plink {
        double x, y, age = 0;
    };
    std::vector<Puff>  puffs;
    std::vector<Plink> plinks;
    explicit WaveCrash(const PixelFxSettings& o) : C(o.palette), seed(o.seed), rng(o.seed) {}
    void puff(double x, double y, double vx, double vy, double r0, double r1, double life, double delay = 0) {
        puffs.push_back({x, y, vx, vy, r0, r1, life, life, delay});
    }
    void update(double dt) override {
        t += dt;
        const double k = lipK(t);
        if (t < CRASH && crestH(t) > 14 && rng.isChance(.5)) {
            const double x = crestX(t) + rng.range(-4, 3), y = -crestH(t) - rng.range(0, 2), vx = -rng.range(20, 60),
                         vy = -rng.range(10, 40), life = rng.range(.5, .9);
            const auto   c    = rng.isChance(.5) ? WHITE : C[6];
            const double size = rng.range(.6, 1.3);
            ps.spawn(rng, ParticleKind::Flake, x, y, vx, vy, life, c, size);
        }
        if (k > .3 && t < CRASH && rng.isChance(.35)) {
            const auto q = lip(1, t);
            drops.add({q[0], rng.range(-3, 3), -q[1], rng.range(30, 90), rng.range(-10, 10), rng.range(-20, 40),
                       rng.isChance(.3) ? 2. : 1.});
        }
        if (!crashed && t >= CRASH) {
            crashed         = true;
            const double X0 = lip(1, CRASH - 1e-3)[0];
            lights.add(X0, -20, 56, C[5], .8, .55);
            lights.add(X0, -4, 22, WHITE, .6, .1);
            for (int i = 0; i < 46; ++i) {
                const double a = rng.range(-.9, .6) * PI, v = rng.range(30, 120);
                drops.add({X0 + rng.range(-6, 6), rng.range(-4, 4), rng.range(2, 8), std::cos(a) * v * .85 + 18,
                           std::sin(a) * v * .5, rng.range(120, 320),
                           rng.isChance(.15)   ? 3.
                           : rng.isChance(.35) ? 2.
                                               : 1.,
                           4, rng.range(0, .1)});
            }
            for (int i = 0; i < 14; ++i)
                drops.add({X0 + rng.range(-3, 3), rng.range(-2, 2), rng.range(2, 6), rng.range(-15, 25),
                           rng.range(-8, 8), rng.range(220, 300), rng.isChance(.3) ? 2. : 1., 4, rng.range(.04, .16)});
            for (int i = 0; i < 34; ++i) {
                const double x = X0 + rng.range(-12, 10), y = -rng.range(2, 12), vx = rng.range(-40, 130),
                             vy = -rng.range(30, 150), r0 = rng.range(1, 2), r1 = rng.range(1.8, 4.2),
                             life = rng.range(.4, .95), delay = rng.range(0, .14);
                puff(x, y, vx, vy, r0, r1, life, delay);
            }
            for (int i = 0; i < 34; ++i) {
                const double a = rng.range(-PI * .95, -PI * .05), v = rng.range(40, 160), x = X0 + rng.range(-8, 8),
                             y = -rng.range(4, 14), vx = std::cos(a) * v + 25, vy = std::sin(a) * v,
                             life = rng.range(.6, 1.3);
                const auto   c    = rng.isChance(.5) ? WHITE : C[6];
                const double size = rng.range(.6, 1.4), delay = rng.range(0, .1);
                ps.spawn(rng, ParticleKind::Flake, x, y, vx, vy, life, c, size, delay);
            }
            for (int i = 0; i < 12; ++i)
                mist.add({X0 + rng.range(-10, 14), -rng.range(3, 16), rng.range(-20, 70), -rng.range(6, 24), 3,
                          rng.range(9, 15), rng.range(1.1, 1.7), rng.isChance(.5) ? C[6] : WHITE, .26,
                          rng.range(0, .2)});
            for (int i = 0; i < 10; ++i) {
                const double x = X0 + rng.range(-20, 24), y = -rng.range(6, 40), life = rng.range(.3, .6),
                             size = rng.range(1, 2.4), delay = rng.range(.05, .4);
                ps.spawn(rng, ParticleKind::Star, x, y, 0, -5, life, C[6], size, delay);
            }
            rings.add(X0, 0, 6, 40, 1.2, {2, 3, 1.1, .12});
        }
        if (crashed && t < CRASH + 1.1 && rng.isChance(.45 * (1 - (t - CRASH) / 1.1))) {
            const double x = crestX(t) + rng.range(-6, 14), y = -rng.range(1, 4), vx = rng.range(10, 40),
                         vy = -rng.range(10, 40), r0 = rng.range(.9, 1.8), r1 = rng.range(1.8, 3.6),
                         life = rng.range(.4, .8);
            puff(x, y, vx, vy, r0, r1, life);
        }
        if (crashed && t < CRASH + .8 && rng.isChance(.12))
            mist.add({crestX(t) + rng.range(-8, 8), -rng.range(2, 6), rng.range(10, 40), -rng.range(3, 10), 3,
                      rng.range(7, 11), rng.range(.8, 1.1), C[6], .2});
        for (size_t n = puffs.size(); n > 0; --n) {
            auto& f = puffs[n - 1];
            if (f.delay > 0) {
                f.delay -= dt;
                continue;
            }
            if ((f.life -= dt) <= 0) {
                puffs.erase(puffs.begin() + std::ptrdiff_t(n - 1));
                continue;
            }
            f.vx -= f.vx * 2.5 * dt;
            f.vy += (160 - f.vy * 2.5) * dt;
            f.x += f.vx * dt;
            f.y = std::min(f.y + f.vy * dt, -1.);
        }
        for (size_t n = plinks.size(); n > 0; --n)
            if ((plinks[n - 1].age += dt) > .12) plinks.erase(plinks.begin() + std::ptrdiff_t(n - 1));
        drops.update(dt, [&](const water::Drop& d) {
            rings.add(d.x, d.z, .6, 2 + d.s * 1.8, .3 + d.s * .1, {1, 3, .9, .05});
            plinks.push_back({d.x, d.z * .22});
        });
        rings.update(dt);
        mist.update(dt);
        ps.update(dt, rng);
        lights.update(dt);
    }
    void body(Paint& p, double fade) const {
        const double xc = crestX(t), A = crestH(t);
        for (int X = -int(EDGE); X < EDGE; ++X) {
            const double x = X + .5, dx = x - xc, h = A * std::exp(-std::pow(dx / (dx < 0 ? 30 : faceW(t)), 2)),
                         edge = 1 - smooth(EDGE - 24, EDGE, std::abs(x));
            if (h < .6 || edge <= 0) continue;
            const bool   face = x > xc;
            const double near = std::exp(-std::pow((x - xc) / 9, 2)), a = fade * edge * 1.4;
            for (int Y = int(std::floor(-h)); Y < 0; ++Y) {
                const double y = Y + .5, u = (y + h) / h, depth = y + h;
                uint32_t     c;
                if (depth < 1)
                    c = near > .5 && A > 14 && noise(x * .6 + t * 9, seed) > -.35 ? WHITE : face ? C[5] : C[6];
                else if (depth < 2.4 && near > .35 && A > 14)
                    c = noise2(x * .5, y * .5 + t * 6, seed + 1) > -.1 ? WHITE : C[6];
                else {
                    const double win  = near * (1 - u) * 1.6,
                                 up   = noise2((x * .9 - y * .35) * .8, y * .12 + t * 5, seed + 2),
                                 wind = noise2(x * .18 - t * 2, y * .9, seed + 3);
                    const int    fi   = up > .55 && u < .85    ? 6
                                        : win > .8             ? 5
                                        : win > .45 || up > .3 ? 4
                                        : win > .2 || up > .05 ? 3
                                        : u < .6               ? 2
                                                               : 1;
                    const int    bi   = u < .14 ? 4 : wind > .55 ? 4 : wind > .25 || u < .32 ? 3 : u < .7 ? 2 : 1;
                    c                 = C[size_t(jsRound(bi + (fi - bi) * smooth(xc - 5, xc + 5, x)))];
                }
                p.dot(X, Y, c, a);
            }
        }
    }
    void curl(Paint& p) const {
        const double k = lipK(t);
        if (k <= 0 || t >= CRASH) return;
        struct Cell {
            double   x, y, q;
            uint32_t c;
        };
        // Source Map insertion order is retained when a brighter sample replaces a cell.
        // The bounded lip spans <45 by 55 local pixels; no map or string lookup is used.
        std::vector<Cell> cells;
        cells.reserve(1024);
        std::array<int16_t, 256 * 80> slots;
        slots.fill(-1);
        for (double s = 0; s <= 1; s += .012) {
            const auto   a = lip(s, t), b = lip(std::min(1., s + .012), t);
            const double tx = b[0] - a[0], ty = b[1] - a[1], len = jsHypot2(tx, ty), l = len != 0 ? len : 1,
                         nx = ty / l, ny = -tx / l, w = a[2];
            for (double q = 0; q <= 1; q += .5 / std::max(1., w)) {
                const double x = a[0] + nx * (q - .5) * w, y = a[1] + ny * (q - .5) * w;
                const int    ix = int(std::floor(x)) + 128, iy = int(std::floor(y)) + 64;
                const bool   inside = ix >= 0 && ix < 256 && iy >= 0 && iy < 80;
                EV_ASSERT(inside, "The bounded source lip must fit its numeric cell table");
                if (!inside) continue;
                auto&      index = slots[size_t(iy) * 256 + size_t(ix)];
                const auto c     = q < .18   ? WHITE
                                   : q < .36 ? (s > .8 ? WHITE : C[6])
                                   : q < .56 ? C[5]
                                   : q < .76 ? C[4]
                                             : C[3];
                if (index < 0) {
                    index = int16_t(cells.size());
                    cells.push_back({x, y, q, c});
                } else if (q < cells[size_t(index)].q)
                    cells[size_t(index)] = {x, y, q, c};
            }
        }
        for (const auto& c : cells) p.px(c.x, c.y, c.c);
        for (double s = 0; s <= 1; s += .125) {
            const auto a = lip(s, t);
            p.glow(a[0], a[1], 6 + a[2], C[5], .24 * k);
        }
        const auto   tip = lip(1, t);
        const double g   = std::sin(PI * clamp((k - .55) / .45));
        if (g > .05) p.sparkle(tip[0], tip[1], 6 * g, C[6], C[5], .5 * g);
        const auto b = lip(.6, t);
        p.glow(b[0] - 3, b[1] + 8, 16, C[4], .25 * k);
    }
    void drawBack(Paint& p) const override {
        const double fade = smooth(0, .25, t) * (1 - smooth(D - .7, D - .1, t));
        for (int X = -int(EDGE); X < EDGE; ++X) {
            const double x = X + .5, edge = 1 - smooth(EDGE - 30, EDGE, std::abs(x)), a = fade * edge;
            if (a <= .02) continue;
            const double g = noise(x * .3 - t * 3, seed + 4);
            p.dot(X, 0, g > .55 ? C[6] : C[4], a * 1.2);
            p.blend(x, 1.5, C[2], .6 * a);
            p.blend(x, 2.5, C[1], .45 * a);
        }
        rings.draw(p, C, -1);
        drops.draw(p, C, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const double fade = smooth(0, .25, t) * (1 - smooth(D - .7, D - .1, t));
        body(p, fade);
        curl(p);
        rings.draw(p, C, 1);
        if (crashed) {
            const double k = 1 - smooth(CRASH + .4, D - .15, t), x1 = crestX(t) + 10;
            for (int X = -20; X < x1; ++X) {
                const double x = X + .5, n = 1 - std::abs(noise2(x * .35, t * .8, seed + 7)) * 4;
                if (n > 1.15 - k * 1.1) p.px(X, 0, n > 1.4 - k ? WHITE : C[6]);
                if (n > 1.4 - k * .9) p.px(X, 1, C[5]);
            }
        }
        for (const auto& f : puffs) {
            if (f.delay > 0) continue;
            const double g = 1 - f.life / f.max,
                         r = (f.r0 + (f.r1 - f.r0) * ease::outCubic(g)) * (1 - smooth(.7, 1, g));
            if (r > .6) water::billow(p, f.x, f.y, r, C);
        }
        for (const auto& q : plinks) {
            if (q.age < .06) p.px(q.x, q.y, WHITE);
            p.glow(q.x, q.y, 4, C[5], .25 * (1 - q.age / .12));
        }
        drops.draw(p, C, Paint::Layer::Front);
        const double f = (t - CRASH) / .08;
        if (f >= 0 && f < 1) {
            const double x = lip(1, CRASH - 1e-3)[0];
            p.circle(x, -3, 8 * (1 - f * .4), WHITE);
            p.ellipse(x, 0, 14, 2.6, [&](double d, double) { return d < .6 ? WHITE : C[6]; });
        }
        ps.draw(p);
        mist.draw(p, C[5]);
        lights.draw(p);
        if (t < CRASH) {
            p.glow(crestX(t) + 4, -crestH(t) + 2, 26, C[4], .22 * smooth(.2, CRASH, t));
            p.glowLine(crestX(t) - 26, -crestH(t) * .45, crestX(t), -crestH(t), 10, C[5], .14 * smooth(.2, CRASH, t));
        } else
            p.glow(crestX(t), -10, 34, C[4], .26 * (1 - smooth(CRASH, D - .3, t)));
    }
};
}  // namespace
const Descriptor& waveCrashDescriptor() {
    static const Descriptor d{
        {PixelFxKind::WaveCrash, "water/wave-crash", 224, 104, 112, 86, D, false},
        8,
        [](PixelFxSettings& s) { water::defaults(s, 6); },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<WaveCrash>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
