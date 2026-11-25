// eve-hot-path
// Adapted from pixelfx fire/campfire.js, MIT. Copyright (c) 2026 Patch and Hugo Duprez.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Fire.h"
#include "stylize/pixelfx/PixelFxInternal.h"

namespace eve::stylize::pixelfx {
namespace {
constexpr std::array<std::array<double, 5>, 2>  logs{{{-19, 2, 15, -12, 3.4}, {19, 2, -15, -12, 3.4}}};
constexpr std::array<std::array<double, 5>, 11> tongues{{{-7.5, -7, .42, 1.8, 1},
                                                         {7.5, -7, .46, 1.8, 1},
                                                         {-3, -9, .78, 2, 1},
                                                         {3.2, -9, .82, 2, 1},
                                                         {-12, -3, .14, 1.3, 0},
                                                         {12.5, -3, .16, 1.3, 0},
                                                         {-9.5, -4.5, .3, 1.9, 0},
                                                         {9.5, -4.5, .34, 1.9, 0},
                                                         {-5, -6, .6, 2.6, 0},
                                                         {5.2, -6, .66, 2.6, 0},
                                                         {0, -7, 1, 3.4, 0}}};
void log(Paint& p, const std::array<double, 5>& L, std::span<const uint32_t> W, std::span<const uint32_t> H, double u0,
         double u1, double t, uint32_t S) {
    const auto [x0, y0, x1, y1, r] = L;
    const double dx = x1 - x0, dy = y1 - y0, len = jsHypot2(dx, dy), ax = dx / len, ay = dy / len;
    double       nx = -ay, ny = ax;
    if (ny < 0) {
        nx = -nx;
        ny = -ny;
    }
    const double rim = 1 - 1.05 / r;
    p.shader(std::min(x0, x1) - r - 1, std::min(y0, y1) - r - 1, std::max(x0, x1) + r + 1, std::max(y0, y1) + r + 1,
             [&](double x, double y, ShadeOutput& out) {
                 const double along = (x - x0) * ax + (y - y0) * ay, u = along / len,
                              s = ((x - x0) * nx + (y - y0) * ny) / r;
                 if (u < u0 || u > u1 || std::abs(s) > 1) return;
                 const double burn = clamp(1.2 - std::abs(x) / 8), lane = jsRound(s * r * .85),
                              run = rand({jsUint(std::floor(along / 3)), jsUint(lane), S});
                 if (std::abs(s) > rim || along > len - 1) {
                     out.c = s < 0 && burn > .4 ? H[7] : W[0];
                     return;
                 }
                 if (burn > .35 && lane < 2) {
                     if (run < .55 && lane > -2) {
                         const double g =
                             .5 + .5 * std::sin(t / 3 * PI * 6 +
                                                rand({jsUint(std::floor(along / 2)), jsUint(lane), S + 1}) * 6.3);
                         out.k = .3 * burn * g;
                         out.c = g > .72 ? H[3] : g > .4 ? H[4] : H[5];
                     } else
                         out.c = lane < -1 ? W[1] : W[0];
                 } else if (lane <= -2)
                     out.c = run < .25 ? W[2] : mix(W[3], H[4], .45 * burn);
                 else if (lane == -1)
                     out.c = run < .45 ? W[1] : W[2];
                 else if (lane == 0)
                     out.c = run < .2 ? W[1] : W[2];
                 else
                     out.c = run < .4 ? W[0] : W[1];
             });
    if (u0 <= 0)
        p.ellipse(x0, y0, r * .62, r + .2, [&](double d) {
            return d > .78 ? W[0] : d > .56 ? W[5] : d > .32 ? W[4] : W[3];
        });
}
void coals(Paint& p, std::span<const uint32_t> W, std::span<const uint32_t> H, double t, uint32_t S) {
    const std::array<uint32_t, 3> ash{hex(0x2a2530), hex(0x4a4350), hex(0x6b6270)};
    p.shader(-15, -3, 15, 2, [&](double x, double y, ShadeOutput& out) {
        const double dx = x / 14, dy = (y + .3) / 2.2, d = dx * dx + dy * dy;
        if (d > 1) return;
        const double h = rand({jsUint(std::floor(x / 2)), jsUint(std::floor(y)), S + 20});
        if (d > .55 && h < .55) {
            out.c = ash[h < .15 ? 2 : h < .32 ? 1 : 0];
            return;
        }
        if (h > .5) {
            const double g = (.5 + .5 * std::sin(t / 3 * PI * 4 + h * 40)) * (1.1 - d);
            out.k          = .35 * g;
            out.c          = g > .7 ? H[2] : g > .5 ? H[3] : g > .3 ? H[4] : H[5];
        } else
            out.c = h > .3 ? W[1] : W[0];
    });
}
void campfire(Paint& p, double t, const PixelFxSettings& o) {
    std::array<uint32_t, 9> H{};
    std::reverse_copy(o.palette.begin(), o.palette.end(), H.begin());
    const auto&       W = o.fire.wood;
    const auto        S = o.seed;
    Rng               R(S);
    const double      HC = o.fire.height;
    const fire::Noise N{3, t};
    const auto        fla  = [&](double tt) { return .74 + .16 * N.at(tt, 7.3, S + 1) + .1 * N.at(tt, 19, S + 2); };
    const double      fl   = fla(t);
    const uint32_t    GLOW = mix(mix(H[3], H[4], .5), WHITE, .25), SMOKE = mix(hex(0x5b5566), H[6], .25);
    const std::array<uint32_t, 4> SPARKS{H[1], H[2], H[4], H[5]}, EMB{H[1], H[2], H[4], H[6]};
    struct Tongue {
        double   dx, y, h, w;
        bool     back;
        double   env;
        uint32_t n;
    };
    std::array<Tongue, 11> T{};
    for (size_t i = 0; i < T.size(); ++i) {
        const auto& q = tongues[i];
        T[i]          = {q[0] + R.range(-.4, .4),   q[1],      3 + HC * q[2] * R.range(.92, 1.08),
                         q[3] * R.range(.9, 1.1),   q[4] != 0, q[2],
                         (S * 7 + uint32_t(i)) * 13};
    }
    const auto lean   = [&](double tt) { return N.at(tt, .67, S + 90) * .12; };
    const auto height = [&](int i, double tt) {
        const auto&  q    = T[size_t(i)];
        const double leap = std::max(0., N.at(tt, 2.6, q.n + 70) - .55) * 1.4;
        return (q.h + .25 * q.h * N.at(tt, 3.3, q.n) + q.h * leap) * (.9 + .1 * fla(tt));
    };
    const auto sweep = [&](int i, double tt, double h) {
        const auto& q = T[size_t(i)];
        return N.at(tt, 2.2, q.n + 40) * (1 + h * .08) + lean(tt) * h * .9 - clamp(q.dx * .3, -2.5, 2.5);
    };
    const auto tip = [&](int i, double tt) {
        const auto&  q = T[size_t(i)];
        const double h = height(i, tt);
        return std::array<double, 2>{q.dx + sweep(i, tt, h) * .85, q.y - h * .88 + 3};
    };
    const auto tongueAt = [&](int i) {
        const auto&  q      = T[size_t(i)];
        const double h      = height(i, t);
        const auto   layers = q.back      ? std::span<const fire::Layer>(fire::BACK_LAYERS)
                              : q.w > 2.2 ? std::span<const fire::Layer>(fire::BIG_LAYERS)
                                          : std::span<const fire::Layer>(fire::LAYERS);
        fire::flameTongue(p, q.dx, q.y, h, q.w, sweep(i, t, h), N, q.n, H, layers, .5, 1.1 + h * .04);
    };
    fire::looped(
        t, 3, 48, S + 300,
        [&](Rng& r, int) {
            return fire::LoopParticle{r.range(-5, 5),   -HC * .8 - r.range(0, 8), r.range(-2, 2),
                                      -r.range(10, 17), r.range(2, 2.9),          r.range(2.4, 3.8)};
        },
        [&](const fire::LoopParticle& q, double age, int) {
            const auto m = fire::drift(q.x, q.y, q.vx, q.vy, age, 4, -20, 1.6, 1);
            fire::smokePuff(p, m.x + N.at(t - age, .8, S + 60) * (2 + 3 * age), m.y, 1 - age / q.life, q.size, SMOKE);
        });
    struct Puff {
        int    i;
        double fire;
        bool   smokes;
        double sl, ox, oy, vx, vy, size, f, life;
    };
    std::vector<std::array<double, 5>> puffs;
    fire::looped(
        t, 3, int(T.size()) * 24, S + 200,
        [&](Rng& r, int j) {
            const int    i      = j % int(T.size());
            const double f      = r.range(.14, .3);
            const bool   smokes = r.isChance(.45);
            return Puff{i,
                        f,
                        smokes,
                        r.range(.8, 1.4),
                        r.range(-1, 1),
                        r.range(0, 4),
                        r.range(-4, 4),
                        -r.range(15, 30),
                        r.range(.8, 1.6) * (.8 + .6 * T[size_t(i)].env),
                        r.next(),
                        f + (smokes ? 1.4 : 0)};
        },
        [&](const Puff& q, double age, int) {
            const auto [tx, ty] = tip(q.i, t - age);
            const auto m        = fire::drift(tx + q.ox, ty + q.oy, q.vx, q.vy, std::min(age, q.fire), 0, -95, 5, 3.2);
            if (age < q.fire) {
                puffs.push_back({m.x, m.y, 1 - age / q.fire, q.size, q.f});
                return;
            }
            if (age - q.fire > q.sl) return;
            const auto s = fire::drift(m.x, m.y, m.vx * .6, m.vy * .6 - 6, age - q.fire, 2, -16, 1.6, 1.2);
            fire::smokePuff(p, s.x, s.y, 1 - (age - q.fire) / q.sl, q.size * .7, 0);
        });
    coals(p, W, H, t, S);
    for (const auto& L : logs) log(p, L, W, H, .56, 1, t, S + 11);
    for (int i = 0; i < int(T.size()); ++i)
        if (T[size_t(i)].back) tongueAt(i);
    struct WoodPuff {
        double x, y, vx, vy, life, size, f;
    };
    fire::looped(
        t, 3, 90, S + 210,
        [](Rng& r, int) {
            return WoodPuff{r.range(-10, 10), -r.range(3, 8),  r.range(-5, 5), -r.range(20, 40),
                            r.range(.16, .3), r.range(1, 1.8), r.next()};
        },
        [&](const WoodPuff& q, double age, int) {
            const auto m = fire::drift(q.x, q.y, q.vx, q.vy, age, 0, -95, 5, 3.2);
            fire::firePuff(p, m.x, m.y, 1 - age / q.life, q.size, q.f, H);
        });
    for (int i = 0; i < int(T.size()); ++i)
        if (!T[size_t(i)].back) tongueAt(i);
    struct Lick {
        int    i;
        double vx, vy, size, life;
    };
    fire::looped(
        t, 3, int(T.size()) * 12, S + 220,
        [&](Rng& r, int j) {
            const int i = j % int(T.size());
            return Lick{i, r.range(-3, 3), -r.range(12, 22), r.range(.9, 1.6) * (.8 + .5 * T[size_t(i)].env), .26};
        },
        [&](const Lick& q, double age, int) {
            const auto [tx, ty] = tip(q.i, t - age);
            fire::lick(p, tx + q.vx * age, ty - 2 + q.vy * age, 1 - age / q.life, q.size, H);
        });
    for (const auto& q : puffs) fire::firePuff(p, q[0], q[1], q[2], q[3], q[4], H);
    for (const auto& L : logs) log(p, L, W, H, 0, .56, t, S + 11);
    struct Pop {
        double x, y;
        int    n, seed;
        double life;
    };
    fire::looped(
        t, 3, 4, S + 400,
        [](Rng& r, int) { return Pop{r.range(-9, 9), -r.range(4, 9), r.integer(3, 5), r.integer(0, 1000000), .7}; },
        [&](const Pop& q, double age, int) {
            if (age < .08) {
                p.px(q.x, q.y, WHITE);
                p.glow(q.x, q.y, 12, H[1], .7 * (1 - age / .08));
            }
            Rng r(uint32_t(q.seed));
            for (int j = 0; j < q.n; ++j) {
                const double a = -PI / 2 + r.range(-.8, .8), sp = r.range(110, 220), life = r.range(.25, .6);
                if (age >= life) continue;
                const auto m = fire::sparkAt(q.x, q.y, std::cos(a) * sp, std::sin(a) * sp, age, 0);
                fire::spark(p, m.x, m.y, m.vx, m.vy, 1 - age / life, SPARKS);
            }
        });
    fire::looped(
        t, 3, 36, S + 500,
        [](Rng& r, int) {
            return fire::LoopParticle{r.range(-7, 7),   -r.range(8, 18),   r.range(-15, 15),
                                      -r.range(25, 60), r.range(1.1, 2.5), r.isChance(.15) ? 2 : r.range(1, 1.6),
                                      r.range(0, 6.3),  r.range(0, 6.3)};
        },
        [&](const fire::LoopParticle& q, double age, int) {
            const auto m = fire::drift(q.x, q.y, q.vx, q.vy, age, 0, -30, 1.6, 1.6);
            fire::ember(p, m.x + 7 * (std::sin(age * 2.3 + q.a) - std::sin(q.a)),
                        m.y + 2 * (std::sin(age * 3 + q.b) - std::sin(q.b)), 1 - age / q.life, q.size, EMB,
                        age * 40 + q.a);
        });
    const double br = .92 + .08 * N(.67, S + 3), lx = lean(t) * 12;
    p.glow(lx, -8, 74 * br * (.9 + .1 * fl), GLOW, .3 * fl);
    for (const auto& q : std::array<std::array<double, 3>, 3>{{{-6, -17, .85}, {0, -21, 1}, {6, -17, .85}}}) {
        p.glow(q[0] + lx, q[1], 46 * br, GLOW, .3 * q[2] * fl);
        p.glow(q[0] * .6, q[1] + 13, 15, H[2], .16 * q[2] * fl);
    }
    p.glow(0, -3, 9, H[0], .16 * fl);
    fire::floorLight(p, 0, .5, 48, 8, GLOW, .3 * fl);
}
}  // namespace
const Descriptor& campfireDescriptor() {
    static const std::array<Option, 2> options{{{"height", 1, 100, false, 0, &PixelFxFireSettings::height},
                                                {"wood", 1, 64, false, 6, &PixelFxFireSettings::wood}}};
    static const Descriptor            descriptor{{PixelFxKind::Campfire, "fire/campfire", 128, 128, 64, 112, 3, true},
                                                  9,
                                                  [](PixelFxSettings& s) {
                                           fireDefaults(s, PixelFxKind::Torch);
                                           s.seed        = 3;
                                           s.fire.height = 22;
                                                  },
                                                  &campfire,
                                                  options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
