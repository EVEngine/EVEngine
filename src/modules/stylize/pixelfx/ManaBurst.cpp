// eve-hot-path
// Native adaptation of pixelfx arcane/mana-burst.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "common/Assert.h"
#include "stylize/pixelfx/Arcane.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double CY = -28, GATHER = .22;
constexpr int    N = 9;
struct Rune {
    int    glyph;
    double x, y, vx, vy, pop;
    bool   popped;
    double phase;
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
struct ManaBurst final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         R;
    Particles                   ps;
    Lights                      lights;
    std::array<Rune, N>         runes{};
    std::array<Pop, N>          pops{};
    size_t                      popCount = 0;
    double                      t        = 0;
    bool                        released = false;
    explicit ManaBurst(const PixelFxSettings& o) : C(o.palette), R(o.seed) {
        ps.sparks = {WHITE, C[6], C[5], C[4]};
        for (int i = 0; i < N; ++i) {
            const double a = (double(i) / N) * TAU + R.range(-.25, .25) - PI / 2, sp = R.range(150, 210),
                         pop = GATHER + .42 + i * .065 + R.range(0, .05), phase = R.range(0, TAU);
            runes[size_t(i)] = {i,   0,     CY,   std::cos(a) * sp, std::sin(a) * sp * (std::sin(a) > 0 ? .42 : .85),
                                pop, false, phase};
        }
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void update(double dt) override {
        t += dt;
        if (t < GATHER - .03)
            for (int i = 0; i < 2; ++i) {
                const double a = R.range(0, TAU), D = R.range(24, 52), T = R.range(.12, .18),
                             v = (D * 3) / (std::exp(3 * T) - 1), x = std::cos(a) * D,
                             y = std::min(-2., CY + std::sin(a) * D * .85), raw = jsHypot2(x, y - CY),
                             l = raw == 0 ? 1 : raw;
                emit({ParticleKind::Streak, x, y, (-x / l) * v, ((CY - y) / l) * v, T, R.isChance(.5) ? C[6] : C[5]});
            }
        if (t >= GATHER && !released) {
            released = true;
            lights.add(0, CY - 12, 96, C[5], 1, .5);
            lights.add(0, CY, 40, WHITE, .9, .1);
            const std::array<uint32_t, 3> colors{WHITE, C[6], C[5]};
            for (int i = 0; i < 96; ++i) {
                const double a = R.range(0, TAU), sp = R.range(100, 250), dn = std::sin(a) > 0 ? .45 : 1;
                const auto   kind  = R.isChance(.65) ? ParticleKind::Mote : ParticleKind::Star;
                const double life  = R.range(.25, .55);
                const auto   color = R.pick<uint32_t>(colors);
                const double size  = R.range(.8, 1.8);
                emit({kind, 0, CY, std::cos(a) * sp, std::sin(a) * sp * dn, life, color, size});
            }
            for (int i = 0; i < 20; ++i) {
                const double a = R.range(0, TAU), sp = R.range(130, 280);
                emit({ParticleKind::Spark, 0, CY, std::cos(a) * sp, std::sin(a) * sp, R.range(.2, .45)});
            }
        }
        if (released)
            for (auto& r : runes) {
                if (r.popped) continue;
                r.vx -= r.vx * 4.2 * dt;
                r.vy -= r.vy * 4.2 * dt + 8 * dt;
                r.x += r.vx * dt;
                r.y += r.vy * dt;
                if (R.isChance(.35))
                    emit({ParticleKind::Mote, r.x + R.range(-1.5, 1.5), r.y, r.vx * .2 + R.range(-10, 10),
                          r.vy * .2 - R.range(5, 25), R.range(.2, .4), C[5], .5});
                if (t >= r.pop) {
                    r.popped = true;
                    EV_ASSERT(popCount < pops.size(), "Each rune may pop only once");
                    pops[popCount++] = {t, r.x, r.y};
                    lights.add(r.x, r.y, 34, C[5], .9, .22);
                    lights.add(r.x, r.y, 12, WHITE, .8, .06);
                    for (int i = 0; i < 9; ++i) {
                        const double a = R.range(0, TAU), sp = R.range(70, 200);
                        emit(
                            {ParticleKind::Spark, r.x, r.y, std::cos(a) * sp, std::sin(a) * sp - 40, R.range(.15, .4)});
                    }
                    const std::array<uint32_t, 2> colors{WHITE, C[6]};
                    for (int i = 0; i < 5; ++i) {
                        const double a = R.range(0, TAU), sp = R.range(20, 80), life = R.range(.3, .6);
                        const auto   color = R.pick<uint32_t>(colors);
                        const double size  = R.range(.8, 1.6);
                        emit({i % 2 ? ParticleKind::Star : ParticleKind::Mote, r.x, r.y, std::cos(a) * sp,
                              std::sin(a) * sp - 20, life, color, size});
                    }
                }
            }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double k = t < GATHER ? .4 * (t / GATHER) : std::max(0., 1 - (t - GATHER) / 1.1);
        if (k > 0) p.glow(0, -1, 50, C[4], .3 * k);
    }
    void draw(Paint& p) const override {
        if (t < GATHER) {
            const double k = t / GATHER, r = 1 + 3.5 * k * k;
            p.circle(0, CY, r + 1.2, C[4]);
            p.circle(0, CY, r, C[6]);
            p.circle(0, CY, r * .5, WHITE);
            p.glow(0, CY, 10 + 20 * k, C[5], .4 + .4 * k);
            const double q = clamp((t - GATHER * .35) / (GATHER * .65));
            if (q > 0 && q < 1)
                p.ring(0, CY, r + 2 + 16 * (1 - ease::outCubic(q)), 1, q > .6 ? WHITE : C[6], 1 - q * .5);
        } else {
            const double q = (t - GATHER) / .6;
            if (q < 1) {
                const double e = ease::outCubic(q);
                if (q < .12) {
                    p.circle(0, CY, 12 * (1 - q / .12) + 2, C[5]);
                    p.circle(0, CY, 12 * (1 - q / .12), WHITE);
                }
                if (q < .32) flare(p, 0, CY, 30 * (1 - q / .32) + 4, 1 - q / .32, C);
                p.ring(0, CY, 4 + 62 * e, std::max(1., 5 * (1 - q)), q < .2 ? WHITE : C[6],
                       [&](double, double ang) { return CY + std::sin(ang) * (4 + 62 * e) > 0 ? 0 : 1.2 - q; });
                p.ring(0, CY, 2 + 38 * ease::outCubic(std::min(1., q * 1.4)), std::max(1., 2 * (1 - q)), C[5],
                       [&](double, double ang) { return CY + std::sin(ang) * 40 > 0 ? 0 : 1 - q; });
                p.glow(0, CY, 60 * (.4 + .6 * e), C[5], .9 * (1 - q));
            }
        }
        for (const auto& r : runes) {
            if (!released || r.popped) continue;
            const double left = r.pop - t, bob = std::sin(t * 9 + r.phase) * .6;
            const bool   hot = t - GATHER < .08 || left < .06;
            arcane::sigil(p, r.glyph, r.x, r.y + bob, hot ? WHITE : C[6], 1, hot ? C[4] : C[3]);
            p.glow(r.x, r.y + bob, 10, C[5], hot ? .7 : .35);
            if (left < .06) p.sparkle(r.x, r.y + bob, 3 + 4 * (1 - left / .06), C[6], C[5], .5);
        }
        for (size_t i = 0; i < popCount; ++i) {
            const auto&  q = pops[i];
            const double k = (t - q.time) / .32;
            if (k >= 1) continue;
            if (k < .35) flare(p, q.x, q.y, 11 * (1 - k / .35) + 2, 1 - k / .35, C);
            p.ring(q.x, q.y, 2 + 11 * ease::outCubic(k), std::max(1., 2 * (1 - k)), k < .3 ? WHITE : C[6], 1.15 - k);
        }
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& manaBurstDescriptor() {
    static const Descriptor d{
        {PixelFxKind::ManaBurst, "arcane/mana-burst", 208, 150, 104, 124, 1.8, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 9;
            s.palette = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                         hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<ManaBurst>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
