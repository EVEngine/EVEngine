// eve-hot-path
// Native adaptation of pixelfx water/geyser.js (MIT), including Sprite Fusion holy-light.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/WaterSimulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double               SY = .22, G = 820, ERUPT = 1.15, FALL = 2.6, GONE = 3.3;
constexpr std::array<Point, 6> SPURTS{{{.46, 7}, {.72, 12}, {.92, 9}, {1.04, 15}, {GONE + .15, 15}, {GONE + .55, 8}}};
struct Geyser final : Kernel {
    std::vector<uint32_t> C;
    uint32_t              seed;
    Rng                   rng;
    double                HMAX, V0, t = 0;
    Particles             ps;
    water::Mist           mist;
    Lights                lights;
    water::Ripples        rings;
    water::Drops          drops;
    struct Plink {
        double x, y, s, age = 0;
    };
    struct Pop {
        double x, y, age, r;
    };
    std::vector<Plink> plinks;
    std::vector<Pop>   pops;
    uint16_t           fired = 0;
    explicit Geyser(const PixelFxSettings& o)
        : C(o.palette), seed(o.seed), rng(o.seed), HMAX(o.water.height), V0(std::sqrt(2 * G * HMAX)) {}
    enum class CueResult { NoChange, Triggered };
    CueResult once(unsigned bit, double at) {
        const auto flag = uint16_t(1u << bit);
        if ((fired & flag) || t < at) return CueResult::NoChange;
        fired |= flag;
        return CueResult::Triggered;
    }
    bool   emit(double rate, double dt) { return rng.isChance(std::min(1., rate * dt)); }
    double poolR(double tt) const {
        return (12 + 7 * smooth(0, 1, tt) + 15 * ease::outCubic(clamp((tt - ERUPT) / 1.2))) *
               (1 - .25 * smooth(GONE, 4.6, tt));
    }
    double poolK(double tt) const { return smooth(0, .2, tt) * (1 - smooth(4, 4.85, tt)); }
    double top(double tt) const {
        if (tt < ERUPT || tt > GONE) return 0;
        return HMAX * ease::outCubic(clamp((tt - ERUPT) / .3)) * (.93 + .07 * noise(tt * 2.2, seed)) *
               (1 - ease::inQuad(clamp((tt - FALL) / (GONE - FALL))));
    }
    double head(double tt) const {
        return smooth(ERUPT + .08, ERUPT + .3, tt) * (1 - smooth(FALL + .1, FALL + .45, tt));
    }
    double spurt(double tt) const {
        double h = 0;
        for (const auto& s : SPURTS) h = std::max(h, s.y * std::sin(PI * clamp((tt - s.x) / .2)));
        return h;
    }
    void update(double dt) override {
        t += dt;
        const double H = top(t), hk = head(t), k = t / ERUPT;
        if (t < ERUPT && emit(1.5 + 9 * k * k, dt)) {
            const double a = rng.range(0, TAU), r = rng.range(0, 7), x = std::cos(a) * r, z = std::sin(a) * r;
            pops.push_back({x, z * SY - rng.range(1, 3), 0, rng.range(1, 2)});
            rings.add(x, z, 1, rng.range(5, 9), .55, {1, 3, .85});
            for (int i = 0; i < rng.integer(1, 3); ++i)
                drops.add({x, z, 2, rng.range(-25, 25), rng.range(-15, 15), rng.range(50, 120), 1, 1, 0, true});
        }
        if (t < ERUPT && emit(3 + 7 * k, dt))
            mist.add({rng.range(-8, 8), -rng.range(1, 5), rng.range(-10, 10), -rng.range(6, 18), 2, rng.range(5, 9),
                      rng.range(.9, 1.5), C[6], .22});
        for (unsigned j = 0; j < SPURTS.size(); ++j) {
            const auto s = SPURTS[j];
            if (once(j, s.x + .08) == CueResult::Triggered)
                for (int i = 0; i < 3 + jsRound(s.y / 4); ++i) {
                    const double a = rng.range(0, TAU), v = rng.range(10, 40);
                    drops.add({0, 0, s.y * .8, std::cos(a) * v, std::sin(a) * v, rng.range(20, 90),
                               rng.isChance(.3) ? 2. : 1.});
                }
        }
        if (once(6, ERUPT) == CueResult::Triggered) {
            lights.add(0, -26, 64, C[5], .85, .6);
            lights.add(0, -4, 28, WHITE, .75, .1);
            rings.add(0, 0, 6, 40, 1.2, {2, 3, 1.2, .12, 1});
            for (int i = 0; i < 44; ++i) {
                const double a = rng.range(0, TAU), v = rng.range(30, 120);
                drops.add({std::cos(a) * 3, std::sin(a) * 3, rng.range(2, 10), std::cos(a) * v, std::sin(a) * v,
                           rng.range(180, 400),
                           rng.isChance(.15)   ? 3.
                           : rng.isChance(.35) ? 2.
                                               : 1.,
                           4, rng.range(0, .12)});
            }
            for (int i = 0; i < 12; ++i) {
                const double s = i % 2 ? 1 : -1;
                mist.add({s * rng.range(3, 9), -rng.range(1, 6), s * rng.range(30, 80), -rng.range(4, 18), 3,
                          rng.range(9, 14), rng.range(1.2, 1.9), rng.isChance(.5) ? C[6] : WHITE, .3,
                          rng.range(0, .15)});
            }
            for (int i = 0; i < 14; ++i) {
                const double x = rng.range(-16, 16), y = -rng.range(8, 80), vx = rng.range(-10, 10),
                             life = rng.range(.3, .7);
                const auto   c    = rng.isChance(.5) ? WHITE : C[6];
                const double size = rng.range(1, 2.6), delay = rng.range(.05, .5);
                ps.spawn(rng, ParticleKind::Star, x, y, vx, -6, life, c, size, delay);
            }
        }
        if (hk > 0) {
            if (emit(60 * hk, dt)) {
                const double a = rng.range(0, TAU), v = rng.range(10, 60);
                drops.add({rng.range(-9, 9), rng.range(-4, 4), H + rng.range(-8, 3), std::cos(a) * v, std::sin(a) * v,
                           rng.range(-40, 60),
                           rng.isChance(.08)  ? 3.
                           : rng.isChance(.3) ? 2.
                                              : 1.});
            }
            if (emit(9 * hk, dt))
                mist.add({rng.range(-8, 8), -H + rng.range(-6, 2), rng.range(-34, 34), -rng.range(14, 34), 3,
                          rng.range(8, 13), rng.range(.9, 1.4), rng.isChance(.6) ? C[6] : WHITE, .2});
            if (emit(5 * hk, dt)) {
                const double x = rng.signedRange(3, 9), y = -rng.range(1, 6), vx = rng.signedRange(20, 50),
                             vy = -rng.range(2, 10), r1 = rng.range(8, 12), life = rng.range(1, 1.6);
                mist.add({x, y, vx, vy, 3, r1, life, C[6], .22});
            }
            if (emit(40 * hk, dt)) {
                const double s = rng.sign(), hh = rng.range(8, H * .95), x = s * (3 + 6 * std::pow(hh / H, 1.8)),
                             vx = s * rng.range(10, 45), vy = rng.range(-20, 10), life = rng.range(.6, 1.2);
                const auto   c    = rng.isChance(.5) ? WHITE : C[6];
                const double size = rng.range(.6, 1.3);
                ps.spawn(rng, ParticleKind::Flake, x, -hh, vx, vy, life, c, size);
            }
            if (emit(4 * hk, dt)) {
                const double x = rng.range(-2, 2), z = rng.range(-2, 2), r = poolR(t) * rng.range(.7, 1);
                rings.add(x, z, 4, r, .9, {1, 3, .8});
            }
            if (emit(7 * hk, dt)) {
                const double x = rng.range(-14, 14), y = -rng.range(10, H + 8), vx = rng.range(-6, 6),
                             life = rng.range(.25, .55);
                const auto   c    = rng.isChance(.4) ? WHITE : C[6];
                const double size = rng.range(.8, 2.2);
                ps.spawn(rng, ParticleKind::Star, x, y, vx, -4, life, c, size, 0);
            }
        }
        if (once(7, FALL + .12) == CueResult::Triggered) {
            for (int i = 0; i < 40; ++i) {
                const double a = rng.range(0, TAU), v = rng.range(15, 80);
                drops.add({rng.range(-6, 6), rng.range(-3, 3), top(t) + rng.range(-8, 4), std::cos(a) * v,
                           std::sin(a) * v, rng.range(-40, 60),
                           rng.isChance(.12)   ? 3.
                           : rng.isChance(.35) ? 2.
                                               : 1.,
                           4, rng.range(0, .25)});
            }
            for (int i = 0; i < 6; ++i)
                mist.add({rng.range(-8, 8), -top(t) + rng.range(-6, 6), rng.range(-34, 34), -rng.range(10, 26), 4,
                          rng.range(9, 13), rng.range(.8, 1.2), C[6], .18});
        }
        for (size_t n = pops.size(); n > 0; --n)
            if ((pops[n - 1].age += dt) > .12) pops.erase(pops.begin() + std::ptrdiff_t(n - 1));
        for (size_t n = plinks.size(); n > 0; --n)
            if ((plinks[n - 1].age += dt) > .14) plinks.erase(plinks.begin() + std::ptrdiff_t(n - 1));
        drops.update(dt, [&](const water::Drop& d) {
            const bool inPool = jsHypot2(d.x, d.z) < poolR(t);
            rings.add(d.x, d.z, .6, (inPool ? 2.5 : 1.5) + d.s * 1.6, .32 + d.s * .1, {1, 3, inPool ? .95 : .7, .05});
            plinks.push_back({d.x, d.z * SY, d.s});
            if (d.s > 1 && !d.small)
                for (int i = 0; i < 2; ++i) {
                    const double a = rng.range(0, TAU), v = rng.range(8, 28);
                    drops.add({d.x, d.z, .5, std::cos(a) * v, std::sin(a) * v, rng.range(45, 95), 1, 1, 0, true});
                }
        });
        rings.update(dt);
        mist.update(dt);
        ps.update(dt, rng);
        lights.update(dt);
    }
    void column(Paint& p, double H, double thin) const {
        if (H < 1) return;
        const auto tau = [&](double h) {
            return (V0 - std::sqrt(std::max(0., V0 * V0 - 2 * G * std::min(h, HMAX * .96)))) / G;
        };
        p.shader(-22, -H - 2, 22, 0, [&](double x, double y, ShadeOutput& out) {
            // The source includes the half-pixel row below the vent: negative v produces NaN.
            // Preserve Math.max propagation so its comparisons retain source raster semantics.
            const double h = -y, v = h / H, sway = noise(t * 1.6 + h * .025, seed + 5) * 2.5 * v,
                         rag = noise2(h * .2 - t * 11, x < sway ? 3 : 7, seed + 6) * (.5 + 1.8 * v),
                         w   = (3.6 + 7.5 * std::pow(v, 1.8)) * thin + rag,
                         q = (x - sway) / (std::isnan(w) ? w : std::max(1., w)), aq = std::abs(q);
            if (aq > 1) return;
            const double X = std::floor(x), streak = noise2(X * 1.618 + .3, (tau(h) - t) * 14, seed),
                         body = noise2(std::floor((x + 40) / 2) * 2.3, (tau(h) - t) * 5, seed + 1);
            int          lvl  = aq < .42 ? 0 : aq < .7 ? 1 : aq < .9 ? 2 : 3;
            if (q > .38) ++lvl;
            if (streak > .28) --lvl;
            if (body < -.4) ++lvl;
            const std::array<uint32_t, 5> colors{{WHITE, C[6], C[5], C[4], C[3]}};
            out.c = colors[size_t(std::clamp(lvl, 0, 4))];
        });
        p.glowLine(0, 0, 0, -H, 16 + 8 * thin, C[5], .12 * thin);
    }
    void foamHead(Paint& p, double H, double k, bool back) const {
        if (k <= .01 || H < 4) return;
        if (back)
            for (uint32_t i = 0; i < 16; ++i) {
                const double side = i % 2 ? 1 : -1, x0 = side * (5 + 9 * rand({i, seed + 20})) * k,
                             len = (14 + 26 * rand({i, seed + 21})) * k, speed = 70 + 50 * rand({i, seed + 22});
                for (int j = 0; j < 3; ++j) {
                    const double f  = std::fmod(t * speed / len + double(j) / 3 + rand({i, seed + 23}), 1),
                                 y0 = -H + 2 + f * len, x = x0 + side * f * f * 6, L = 1 + 4 * f * (1 - f) * 2;
                    if (f < .85) p.line(x, y0, x + side * .6, y0 + L, f < .4 ? C[6] : f < .7 ? C[5] : C[4]);
                }
            }
        for (uint32_t i = 0; i < 12; ++i) {
            const double a = (double(i) / 12) * TAU + t * (i % 2 ? 1.3 : -1.1), z = std::sin(a);
            if ((z < 0) != back) continue;
            const double rho = (3 + 7 * rand({i, seed})) * k,
                         x   = std::cos(a) * rho * 1.25 + noise(t * 3 + i, seed + 8) * 2.5,
                         y   = -H + z * rho * .45 + (rho / 10) * 4 - 4 * (1 - rho / (10 * k)) +
                               noise(t * 3 + i + 50, seed + 9) * 2.5,
                         r   = (2 + 2.6 * rand({i, seed + 2}) + .7 * std::sin(t * 7 + i)) * k;
            if (back) {
                const std::array<uint32_t, 8> ramp{{C[0], C[1], C[2], C[3], C[3], C[4], C[5], C[5]}};
                water::billow(p, x, y, r, ramp);
            } else {
                p.circle(x + r * .15, y + r * .25, r, C[4]);
                p.circle(x, y, r * .82, rho > 6 * k ? C[5] : C[6]);
                p.circle(x - r * .25, y - r * .3, r * .5, WHITE);
            }
        }
        if (!back) p.glow(0, -H - 2, 38, C[5], .34 * k);
    }
    void drawBack(Paint& p) const override {
        const double pr = poolR(t), pk = poolK(t);
        if (pk > 0)
            p.shade(-pr, -pr * SY, pr, pr * SY, [&](double x, double y, int sx, int sy) {
                const double d = std::pow(x / pr, 2) + std::pow(y / (pr * SY), 2);
                if (d > 1) return;
                p.blendPx(sx, sy, d < .15 ? C[2] : C[1], .6 * pk * std::min(1., (1 - d) * 4));
                if (y < 0 && d > .78 && d < .95) p.blendPx(sx, sy, C[4], .7 * pk);
            });
        rings.draw(p, C, -1);
        const double u = (t - ERUPT) / .4;
        if (u > 0 && u < 1)
            water::ring(p, 0, .5, 6 + 42 * ease::outCubic(u), SY, 1.5 * (1 - u), C, {3, 1.6 + 2 * u, -1, u < .3, 0, 9});
        foamHead(p, top(t), head(t), true);
        drops.draw(p, C, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const double H = top(t), hk = head(t), k = clamp(t / ERUPT),
                     thin = smooth(ERUPT, ERUPT + .12, t) * (1 - .55 * smooth(FALL, GONE, t));
        rings.draw(p, C, 1);
        const double u = (t - ERUPT) / .4;
        if (u > 0 && u < 1)
            water::ring(p, 0, .5, 6 + 42 * ease::outCubic(u), SY, 1.5 * (1 - u), C, {3, 1.6 + 2 * u, 1, u < .3, 0, 9});
        const double fk = smooth(ERUPT, ERUPT + .3, t) * (1 - smooth(GONE - .3, 4.4, t));
        if (fk > 0) water::lace(p, 0, 0, 8 + 10 * fk, (8 + 10 * fk) * SY + .5, fk, C, t, seed);
        if (t < ERUPT + .05) {
            const double beat = std::pow(std::max(0., std::sin(t * (8 + 18 * k * k))), 3),
                         rd = 4 + 3.5 * k + 1.5 * beat, hd = 1.5 + 3 * k + 3 * beat;
            p.shader(-rd - 1, -hd - 1, rd + 1, .5, [&](double x, double y, ShadeOutput& out) {
                const double dx = x / rd, dy = y / hd, d = dx * dx + dy * dy;
                if (d > 1 || y > .5) return;
                const double lit = -dx * .7 - dy * .7;
                const bool   rim = d > .68;
                out.c            = rim         ? (lit > .3     ? WHITE
                                                  : lit > -.15 ? C[6]
                                                               : C[4])
                                   : lit > .55 ? C[6]
                                   : lit > .12 ? C[5]
                                   : lit > -.3 ? C[4]
                                               : C[3];
            });
            p.px(-rd * .35, -hd * .6, WHITE);
            p.glow(0, -5, 16 + 16 * k + 8 * beat, C[5], .06 + .12 * k + .12 * beat);
        }
        const double sp = spurt(t);
        if (sp > 1 && H < 1) {
            p.shader(-2, -sp - 1, 2, 0, [&](double x, double y, ShadeOutput& out) {
                const double q = x / (1 + .8 * (1 + y / sp));
                if (std::abs(q) > 1) return;
                out.c = q < -.3 ? C[6] : q < .35 ? WHITE : C[5];
            });
            p.circle(0, -sp, 1.3, C[6]);
            p.px(0, -sp - 1, WHITE);
            p.glowLine(0, 0, 0, -sp, 8, C[5], .3);
        }
        column(p, H, thin);
        if (hk > 0 || (H > 1 && t < GONE))
            for (uint32_t i = 0; i < 7; ++i) {
                const double a = (double(i) / 7) * TAU + t * 2.1, r = 4 + 2 * std::sin(t * 5 + i * 1.7),
                             x = std::cos(a) * r * 1.3, y = std::sin(a) * r * SY - 2;
                water::billow(p, x, y, (1.6 + 1.2 * rand({i, seed + 4})) * std::max(hk, thin * .6), C);
            }
        foamHead(p, H, hk, false);
        constexpr std::array<Point, 5> rays{{{1, 0}, {-1, 0}, {.7, -.7}, {-.7, -.7}, {0, -1}}};
        for (const auto& q : pops) {
            const double g = q.age / .12;
            for (const auto& d : rays) p.dot(q.x + d.x * (q.r + 3 * g), q.y + d.y * (q.r + 3 * g), C[6], 1 - g);
        }
        for (const auto& q : plinks) {
            const double g = q.age / .14;
            if (g < .5) p.px(q.x, q.y, WHITE);
            if (q.s > 1) p.line(q.x, q.y - 1, q.x, q.y - 1 - 3 * (1 - g), g < .4 ? WHITE : C[6]);
            p.glow(q.x, q.y, 4, C[5], .25 * (1 - g));
        }
        drops.draw(p, C, Paint::Layer::Front);
        const double f = (t - ERUPT) / .07;
        if (f >= 0 && f < 1) {
            p.circle(0, -4, 7 * (1 - f * .4), WHITE);
            p.ellipse(0, 0, 12, 2.6, [&](double d, double) { return d < .6 ? WHITE : C[6]; });
        }
        ps.draw(p);
        mist.draw(p, C[5]);
        lights.draw(p);
        if (H > 1) p.glowLine(0, 0, 0, -H, 26, C[4], .2 * thin);
        p.glow(0, -2, 46, C[4], .24 * std::max(hk, .3 * k * (t < ERUPT ? 1 : 0)));
        p.glow(0, 2, 60, C[3], .12 * poolK(t));
    }
};
}  // namespace
const Descriptor& geyserDescriptor() {
    static const std::array<Option, 1> options{{{"height", 20, 256, false, 0, &PixelFxWaterSettings::height}}};
    static const Descriptor            d{
        {PixelFxKind::Geyser, "water/geyser", 184, 168, 92, 146, 5, false},
        8,
        [](PixelFxSettings& s) {
            water::defaults(s, 4);
            s.water.height = 100;
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Geyser>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
