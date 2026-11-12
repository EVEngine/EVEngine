// eve-hot-path
// Native adaptation of pixelfx critters/slime.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Critter.h"
#include "stylize/pixelfx/CritterFire.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 1.8, SQ = .5, UP = .84, LIFT = .9, LAND = 1.44, SNAP = .965, W0 = 30, H0 = 22, VC = .4, G = 640,
                 POP = .4;
constexpr std::array<Point, 5> CURVE{{{0, .36}, {.4, .02}, {.55, -.05}, {.85, .08}, {1, .16}}};
struct Pose {
    double s, by, lean, round, q, age, W, H;
};
Pose pose(double t, double hop) {
    const double age = wrap(t - LAND, D);
    double       s, by = 0, lean = 0, round = .1, q = -1;
    if (t >= LIFT && t < LAND) {
        q     = (t - LIFT) / (LAND - LIFT);
        by    = -hop * 4 * q * (1 - q);
        s     = critter::sampleCurve(q, CURVE);
        round = .1 + .8 * smooth(0, .16, q) * (1 - smooth(.86, 1, q));
    } else if (t >= UP && t < LIFT)
        s = lerp(-.3, .36, ease::outCubic(seg(t, UP, LIFT)));
    else if (t >= SQ && t < UP) {
        const double u = seg(t, SQ, UP);
        s              = -.3 * ease::inOutSine(u) + .018 * std::sin(t * TAU * 16) * smooth(.55, 1, u);
    } else {
        s    = age < .06 ? -.46 : -.46 * critter::dampedSpring(age - .06, 2.1, 4.6);
        lean = .07 * std::exp(-age * 4) * std::sin(age * TAU * 1.9);
        if (t < SQ) s += .02 * std::sin((t / SQ) * TAU);
    }
    return {s, by, lean, round, q, age, W0 / std::sqrt(1 + s), H0 * (1 + s)};
}
double halfW(double v, const Pose& P) {
    if (v < 0 || v > 1) return -1;
    if (v >= VC) {
        const double q = (v - VC) / (1 - VC);
        return (P.W / 2) * std::sqrt(1 - q * q);
    }
    const double q = (VC - v) / VC, hb = v * P.H, rc = 3;
    double       hw = (P.W / 2) * std::sqrt(std::max(0., 1 - P.round * q * q));
    if (hb < rc) hw -= rc - std::sqrt(rc * rc - std::pow(rc - hb, 2));
    return hw;
}
bool inside(const Pose& P, double x, double y) {
    const double v = (P.by - 1 - y) / P.H, hw = halfW(v, P);
    return hw >= 0 && std::abs(x - P.lean * v * P.H) <= hw;
}
void body(critter::Grid<60, 66>& g, std::span<const uint32_t> C, const Pose& P) {
    const double yb = P.by - 1, half = P.W / 2 + 3, cx = P.lean * .45 * P.H, cy = yb - P.H * .5, lx = cx - P.W * .24,
                 ly  = cy - P.H * .36;
    const auto   lit = [&](double x, double y, double k) { return inside(P, lx + (x - lx) / k, ly + (y - ly) / k); };
    g.shade(-half, yb - P.H - 1, half, yb, [&](double x, double y, auto& o) -> uint32_t {
        if (!inside(P, x, y)) return 0;
        o.tag          = 1;
        o.a            = .92;
        int          i = lit(x, y, .42) ? 5 : lit(x, y, .7) ? 4 : 3;
        const double v = (yb - y) / P.H;
        if (v < .3 && inside(P, x, y + 1.4)) i = !inside(P, x, y + 2.4) ? 5 : v < .2 ? 4 : i;
        if (!inside(P, x + 2, y + 2.4)) {
            i   = 2;
            o.a = 1;
        }
        const double ang = std::atan2(y - cy, (x - cx) * (P.H / P.W));
        if (!inside(P, x - 1.6, y - 1.6) && inside(P, x - .7, y - .7) && ang > -2.75 && ang < -1.55) {
            i   = ang > -2.45 && ang < -1.8 ? 6 : 5;
            o.a = 1;
        }
        if (i == 2 && inside(P, x + 1, y + 1.2) && !inside(P, x + 2, y + 2.4) && ang > .3 && ang < 1.5) i = 4;
        return C[size_t(i)];
    });
    const double hx = jsRound(cx - P.W * .27), hy = jsRound(cy - P.H * .14);
    if (inside(P, hx - 1, hy) && inside(P, hx + 1, hy + 1)) {
        g.set(hx, hy, C[6]);
        g.set(hx, hy + 1, C[5]);
    }
}
void face(critter::Grid<60, 66>& g, std::span<const uint32_t> C, const Pose& P, double t) {
    const double yb = P.by - 1, ev = .47, ey = jsRound(yb - P.H * ev), ex = std::max(3., jsRound(P.W * .16)),
                 xc  = jsRound(P.lean * ev * P.H);
    const bool   air = P.q >= 0, shut = P.age < .16 && !air, blink = t > .22 && t < .29;
    const double wind = t >= SQ && t < LIFT ? seg(t, SQ, UP) : 0,
                 look = air ? (P.q < .45  ? -1
                               : P.q > .7 ? 1
                                          : 0)
                            : 0,
                 eh   = clamp(jsRound(4 * (1 + P.s * .8) - wind * 2 + (air && P.q < .6 ? 1 : 0)), 1, 5);
    for (int side : {-1, 1}) {
        const double x = xc + side * ex - (side > 0 ? 1 : 0);
        if (shut) {
            const int a = side < 0 ? 0 : 1, b = 1 - a;
            g.set(x + a, ey - 1, C[0]);
            g.set(x + b, ey, C[0]);
            g.set(x + a, ey + 1, C[0]);
            continue;
        }
        if (blink) {
            g.set(x, ey + 1, C[0]);
            g.set(x + 1, ey + 1, C[0]);
            continue;
        }
        const double top = ey + 2 - eh + look;
        for (int k = 0; k < eh; ++k) {
            g.set(x, top + k, C[0]);
            g.set(x + 1, top + k, C[0]);
        }
        g.set(x + (look > 0 ? 1 : 0), top + (look > 0 ? 1 : 0), C[6]);
        if (eh >= 4) g.set(x + 1, top + eh - 1, C[2]);
    }
    const double my = jsRound(yb - P.H * .31);
    if (air && P.q < .75) {
        g.set(xc, my, C[0]);
        g.set(xc - 1, my, C[0]);
        g.set(xc, my + 1, C[1]);
        g.set(xc - 1, my + 1, C[1]);
    } else if (!shut) {
        g.set(xc, my, C[1]);
        g.set(xc - 1, my, C[1]);
    }
}
void bubbleIn(critter::Grid<60, 66>& g, std::span<const uint32_t> C, const Pose& P, double t) {
    if (t > POP) return;
    const double v = lerp(.18, .86, ease::inQuad(t / POP)), hw = halfW(v, P),
                 x = P.lean * v * P.H - hw * lerp(.35, .05, t / POP), y = P.by - 1 - v * P.H;
    g.set(x, y, C[6]);
    g.set(x + 1, y, C[5]);
    g.set(x, y + 1, C[5]);
    g.set(x + 1, y + 1, C[4]);
}
struct DropSpec {
    double x, y, vx, vy, size;
};
std::array<DropSpec, 12> prepare(uint32_t seed) {
    std::array<DropSpec, 12> out{};
    for (uint32_t i = 0; i < 12; ++i) {
        Rng          r(hash({seed, i, 7}));
        const double a = -PI / 2 + r.range(-1.2, 1.2), sp = r.range(60, 118),
                     size = r.isChance(.35)  ? 1
                            : r.isChance(.7) ? 2
                                             : 3,
                     fa   = a + PI / 2;
        out[i] = {std::sin(fa) * 21, -1 - std::cos(fa) * 12.5, std::cos(a) * sp * .75, std::sin(a) * sp, size};
    }
    return out;
}
struct Drop {
    critter::FlightSample f;
    double                size, k = 0, splat = 0;
    bool                  gone = false;
};
Drop droplet(const DropSpec& spec, double age) {
    Drop  out{critter::tossSample(spec.x, spec.y, spec.vx, spec.vy, age, G, -1), spec.size};
    auto& f = out.f;
    if (f.landed) {
        const double home = (f.landX < 0 ? -1 : 1) * (W0 / 2 - 2),
                     dur = std::min(.55, .22 + std::abs(f.landX - home) / 60), u = seg(age - f.landTime - .14, 0, dur);
        f.x       = lerp(f.landX, home, ease::inOutSine(u));
        out.k     = 1 - u;
        out.gone  = u >= 1;
        out.splat = clamp(1 - (age - f.landTime) / .14);
    }
    return out;
}
double puddleR(double t, const Pose& P) {
    if (P.q >= 0) return (W0 / std::sqrt(1.36) / 2 + 3) * (1 - .22 * P.q);
    return P.W / 2 + 3 + 9 * std::exp(-P.age * 5) * (1 - std::exp(-P.age * 40)) * (t >= LAND || t < SQ ? 1 : 0);
}
constexpr std::array<Point, 2> DRIPS{{{1.02, -4}, {1.18, 3}}};
struct Slime final : AnalyticKernel {
    const std::array<DropSpec, 12> drops;
    explicit Slime(uint32_t seed) : drops(prepare(seed)) {}
    void draw(Paint& p, double time, const PixelFxSettings& o) const override {
        const double          t  = critter::shift(time, D, o.seed);
        const auto&           C  = o.palette;
        const auto            P  = pose(t, o.critter.hop);
        const double          yb = P.by - 1, age = P.age;
        critter::Grid<60, 66> g(-30, -64);
        body(g, C, P);
        face(g, C, P, t);
        bubbleIn(g, C, P, t);
        g.outline(
            [&](double, double y, int nx, int ny) { return ny < 0 && nx <= 0 && y < yb - P.H * .5 ? C[1] : C[0]; });
        g.draw(p);
        if (t >= POP && t < POP + .16) {
            const double k = (t - POP) / .16, x = P.lean * P.H - halfW(.86, P) * .05, y = yb - P.H - .5;
            for (double a = 0; a < TAU; a += .6)
                p.dot(x + std::cos(a) * (1 + k * 3), y + std::sin(a) * (1 + k * 3) * .7, C[6], 1.2 - k);
            for (int s : {-1, 1}) p.dot(x + s * (1 + k * 4), y - 1 - k * 6 + k * k * 4, C[5], 1.3 - k);
            p.glow(x, y, 6, C[5], .4 * (1 - k));
        }
        for (const auto& spec : drops) {
            const auto  d = droplet(spec, age);
            const auto& f = d.f;
            if (f.landed || inside(P, f.x, f.y)) continue;
            const double speed = jsHypot2(f.vx, f.vy), sv = speed == 0 ? 1 : speed, ux = f.vx / sv, uy = f.vy / sv,
                         L = std::min(2.5, sv / 50);
            p.line(f.x - ux * L, f.y - uy * L, f.x, f.y, C[3]);
            if (d.size >= 2) {
                p.px(f.x - 1, f.y, C[4]);
                p.px(f.x - 1, f.y + 1, C[3]);
                p.px(f.x, f.y + 1, C[2]);
            }
            if (d.size >= 3) {
                p.px(f.x + 1, f.y, C[3]);
                p.px(f.x - 1, f.y - 1, C[4]);
                p.px(f.x, f.y - 1, C[5]);
            }
            p.px(f.x, f.y, d.size >= 2 ? C[6] : C[5]);
            p.glow(f.x, f.y, 5 + d.size, C[4], .32);
        }
        cycle(t, D, 7, o.seed * 13u + 1u, 1.2, 1.8, [&](double ag, double life, Rng& r, int i) {
            const double k = ag / life, x = r.range(-24, 24) + std::sin(ag * 2.1 + i * 1.7) * 3,
                         y   = -2 - r.range(9, 17) * ag - 5 * ag * ag,
                         a   = std::min(1., k * 4) * std::min(1., (1 - k) * 2.5),
                         tw  = .6 + .4 * std::sin(ag * 7 + i * 2.3);
            const bool   big = r.isChance(.35);
            p.dot(x, y, tw > .8 ? C[6] : C[5], a);
            if (big) {
                p.dot(x + 1, y, C[5], a);
                p.dot(x, y + 1, C[4], a);
                p.dot(x + 1, y + 1, C[4], a * .6);
            }
            p.glow(x, y, big ? 9 : 6, C[5], (big ? .45 : .32) * a * tw);
        });
        for (auto v : DRIPS) {
            const double a2 = wrap(t - v.x, D);
            if (a2 > .6) continue;
            const auto P0 = pose(v.x, o.critter.hop);
            const auto f  = critter::tossSample(v.y, P0.by - .5, 0, 12, a2, G, -1);
            if (!f.landed) {
                p.px(v.y, f.y, C[5]);
                p.px(v.y, f.y - 1, C[4]);
                p.glow(v.y, f.y, 4, C[4], .3);
            } else if (a2 - f.landTime < .14) {
                const double k = (a2 - f.landTime) / .14;
                for (int side : {-1, 1}) p.dot(v.y + side * (1 + k * 3), -2 - k * 3 + k * k * 3, C[5], 1.2 - k);
            }
        }
        if (P.q > .4 && P.q < .64) {
            const double k = std::sin(PI * seg(P.q, .4, .64));
            p.sparkle(P.lean * .8 * P.H - P.W * .21, yb - P.H * .8, 1 + 2.5 * k, C[6], C[5], .5 * k);
        }
        const double cx = P.lean * P.H * .4, cy = yb - P.H * .45, near = clamp(1 + P.by / (o.critter.hop * 1.1)),
                     R = puddleR(t, P), br = .9 + .1 * std::sin((t / D) * TAU * 2);
        p.glow(cx * .5, -14, 64, C[3], .3 * br);
        p.glow(cx, cy, 30 + P.W * .3, C[4], .2);
        p.glow(cx, cy + 3, 14, C[5], .16);
        p.glow(0, -2, R * 1.3 + 6, C[4], .12 + .08 * near);
        if (t >= LAND || t < SQ) p.glow(0, -3, 40, C[5], .42 * std::exp(-age * 9));
    }
    void drawBack(Paint& p, double time, const PixelFxSettings& o) const override {
        const double t   = critter::shift(time, D, o.seed);
        const auto&  C   = o.palette;
        const auto   P   = pose(t, o.critter.hop);
        const double age = P.age, R = puddleR(t, P), hp = std::min(2.4, .9 + R * .06);
        for (double x = std::floor(-R); x <= std::ceil(R); ++x) {
            const double d = std::abs(x + .5) / R;
            if (d > 1) continue;
            const double top = -1 - jsRound(std::sqrt(1 - d * d) * hp);
            for (double y = top; y <= 0; ++y) {
                auto c = y == top ? C[5] : y == 0 ? C[2] : C[4];
                if (d > .88) c = y == top ? C[4] : C[3];
                if (y == top && d < .88) {
                    const double n =
                        critter::loopNoise(t, D, 7, x * .18, 3) + critter::loopNoise(t, D, 4, x * .07, 4) * .6;
                    if (n > .55) c = C[6];
                } else if (y == top + 1 && d < .8 && critter::loopNoise(t, D, 3, x * .12, 5) > .3)
                    c = C[5];
                p.px(x, y, c);
            }
            if (d > .96) p.px(x, top - 1, C[1]);
        }
        cycle(t, D, 4, o.seed * 17u + 3u, .45, .75, [&](double ag, double life, Rng& r, int) {
            const double x = r.range(-.85, .85) * R, k = ag / life,
                         rb = r.range(1.4, 2.6) * (.3 + .7 * ease::outCubic(std::min(1., k * 1.25)));
            if (std::abs(x) < P.W / 2 + 1 && P.q < 0) return;
            const double sy = -1 - jsRound(std::sqrt(std::max(0., 1 - std::pow(x / R, 2))) * hp);
            if (k < .85) {
                for (double y = std::floor(sy - rb); y <= sy; ++y)
                    for (double xx = std::floor(x - rb - 1); xx <= std::ceil(x + rb + 1); ++xx) {
                        const double dx = xx + .5 - x, dy = y + .5 - sy, dd = jsHypot2(dx, dy);
                        if (dd > rb + .5) continue;
                        if (dd > rb - .7)
                            p.px(xx, y, dx < 0 && dy < -rb * .3 ? C[6] : C[4]);
                        else
                            p.blend(xx, y, C[5], .3);
                    }
                p.glow(x, sy - rb * .5, rb * 2 + 3, C[5], .2);
            } else {
                const double                   q = (k - .85) / .15;
                constexpr std::array<Point, 5> flecks{{{1, 0}, {-1, 0}, {.6, -.7}, {-.6, -.7}, {0, -1}}};
                for (auto v : flecks) p.dot(x + v.x * (2 + 3 * q), sy + v.y * (2 + 3 * q), C[6], 1.2 - q);
            }
        });
        for (const auto& spec : drops) {
            const auto  d = droplet(spec, age);
            const auto& f = d.f;
            if (!f.landed || d.gone) continue;
            const double x = jsRound(f.x);
            const int    w = int(d.size) + (d.splat > 0 ? 1 : 0);
            for (int j = 0; j < w; ++j) p.px(x - (w >> 1) + j, -1, j == 0 ? C[5] : C[4]);
            if (d.size >= 2 && d.splat <= 0) {
                p.px(x, -2, C[5]);
                p.px(x - 1, -2, C[4]);
            }
            p.px(x, 0, C[3]);
            const double delta = f.landX - f.x, dir = delta < 0 ? -1 : delta > 0 ? 1 : 0;
            for (int j = 1; j < std::min(8., std::abs(delta)); ++j) p.dot(x + dir * j, 0, C[2], .7 - j * .08);
            p.glow(x, -1, 5, C[4], .25 * d.k + .1);
        }
        if (t >= LIFT && t < SNAP) {
            const double bot = P.by - 1, u = (t - LIFT) / (SNAP - LIFT);
            for (double y = std::floor(bot); y < 0; ++y) {
                const double m = std::sin(((y - bot) / -bot) * PI);
                p.px(-1, y, C[4]);
                if (m * u < .45) p.px(0, y, C[3]);
            }
        } else if (t >= SNAP && t < SNAP + .3) {
            const double a2 = t - SNAP, ys = pose(SNAP, o.critter.hop).by - 1;
            const auto   f = critter::tossSample(-1, ys * .45, 0, 0, a2, G * 1.3, -2);
            if (!f.landed) {
                p.px(-1, f.y, C[5]);
                p.px(-1, f.y - 1, C[3]);
            }
            const double nub = 3 * (1 - a2 / .12);
            for (int j = 0; j < nub; ++j) p.px(-1, P.by + j, j == 0 ? C[3] : C[4]);
        }
    }
};
}  // namespace
const Descriptor& slimeDescriptor() {
    static const std::array<Option, 1> options{{{"hop", .01, 256, false, 0, &PixelFxCritterSettings::hop}}};
    static const Descriptor            d{{PixelFxKind::Slime, "critters/slime", 112, 88, 56, 72, D, true},
                                         7,
                                         [](PixelFxSettings& s) {
                                  s.seed        = 1;
                                  s.critter.hop = 24;
                                  s.palette     = {hex(0x061412), hex(0x0b2f2b), hex(0x12594a), hex(0x1f8f4f),
                                                   hex(0x47c752), hex(0xa6f06a), hex(0xf0ffd2)};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<Slime>(s.seed);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
