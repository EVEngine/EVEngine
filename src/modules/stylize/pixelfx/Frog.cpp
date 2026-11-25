// eve-hot-path
// Native adaptation of pixelfx critters/frog.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Critter.h"
#include "stylize/pixelfx/CritterMotion.h"
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.6, CROUCH = .95, UP = 1.2, LAND = 1.62, SNAP = 1.4, BACK = 1.95;
const double     HOP = (1000 * std::pow(LAND - UP, 2)) / 8;
struct Pose {
    double y = 0, rotation = 0, squash = 0, leg = 0, throat = 0;
    bool   shut = false, air = false;
};
Pose pose(double t) {
    Pose                           p;
    const double                   age = wrap(t - LAND, D);
    constexpr std::array<Point, 3> THROAT{{{.05, .45}, {.5, .85}, {1.75, 1.95}}};
    for (auto q : THROAT)
        if (t > q.x && t < q.y) p.throat = std::pow(std::sin(PI * ((t - q.x) / (q.y - q.x))), .7) * (q.x > 1 ? .6 : 1);
    p.shut = t > .88 && t < .95;
    if (t >= CROUCH && t < UP) {
        const double u = ease::inOutSine(seg(t, CROUCH, UP));
        p.squash       = -.18 * u;
        p.rotation     = -.12 * u;
    } else if (t >= UP && t < LAND) {
        const double q = (t - UP) / (LAND - UP);
        p.y            = -HOP * 4 * q * (1 - q);
        p.rotation     = lerp(.42, -.25, q);
        p.squash       = lerp(.18, .02, std::min(1., q * 2));
        p.leg          = q < .75 ? 1 : 1 - (q - .75) / .25;
    } else {
        p.squash   = -.24 * critter::dampedSpring(age, 2.4, 6.5);
        p.rotation = -.08 * std::exp(-age * 6);
    }
    p.air = t >= UP && t < LAND;
    return p;
}
std::optional<Point> bugAt(double t) {
    if (t > SNAP && t < BACK) return std::nullopt;
    const double a = (t / D) * TAU * 3;
    const Point  home{16 + 6 * std::sin(a), -40 + 3 * std::sin(2 * a)};
    const double age = wrap(t - BACK, D);
    if (age < .45) {
        const double u = ease::outCubic(age / .45);
        return Point{lerp(-52, home.x, u), lerp(-46, home.y, u) + std::sin(age * 30) * 1.5};
    }
    return home;
}
bool inBody(double x, double y) {
    return std::pow((x + 1) / 10.5, 2) + std::pow((y + 7) / 6, 2) <= 1 ||
           std::pow((x - 6) / 6.2, 2) + std::pow((y + 8.6) / 4.8, 2) <= 1;
}
bool inEye(double x, double y) { return std::pow(x - 5.5, 2) + std::pow(y + 13.2, 2) <= std::pow(3.2, 2); }
void body(critter::Grid<44, 44>& g, std::span<const uint32_t> C, std::span<const uint32_t> Bl, const Pose& p) {
    const double cs = std::cos(p.rotation), sn = std::sin(p.rotation), sx = 1 + p.squash * -.6, sy = 1 + p.squash;
    g.clear();
    const auto local = [&](double x, double y) {
        return Point{(x * cs + (y + 7) * sn) / sx, (-x * sn + (y + 7) * cs) / sy - 7};
    };
    const auto lit = [](double x, double y, double k) { return inBody(-4 + (x + 4) / k, -13 + (y + 13) / k); };
    g.shade(-15, -22, 15, 1, [&](double x, double y) -> uint32_t {
        const auto   q = local(x, y);
        const double u = q.x, v = q.y;
        if (!inBody(u, v) && !inEye(u, v)) return 0;
        if (inEye(u, v)) {
            const double d = jsHypot2(u - 5.5, v - 13.2 * -1);
            if (p.shut) return v < -13.2 ? C[4] : C[3];
            if (std::abs(v + 13) < .7 && std::abs(u - 6) < 1.8) return C[0];
            if (u < 4.7 && v < -14) return hex(0xffd860);
            return d < 2.4 ? hex(0xc89420) : hex(0x3a2a08);
        }
        if (v > -5 && u > -6) return v > -3 ? Bl[1] : Bl[2];
        constexpr std::array<std::array<double, 3>, 4> SPOTS{
            {{{-5, -10, 1.6}}, {{0, -11.5, 1.3}}, {{-8, -7, 1.2}}, {{3, -12.5, .9}}}};
        for (auto spot : SPOTS)
            if (jsHypot2(u - spot[0], v - spot[1]) < spot[2]) return C[2];
        if (!inBody(u + 2, v + 2.4)) return C[2];
        return lit(u, v, .5) ? C[5] : lit(u, v, .8) ? C[4] : C[3];
    });
    std::array<Point, 4> legs;
    if (p.leg < .5) {
        g.ellipse(-7.5, -4.5 + (1 - sy) * 3, 5, 3.6, [&](double nx, double ny) {
            return nx * -.6 + ny * -.8 > .35 ? C[4] : ny > .4 ? C[2] : C[3];
        });
        legs = {Point{-4, -1.5}, {-9, -1}, {-3, -.5}, {4, -.5}};
    } else {
        const double ex = -8 - 10 * p.leg, ey = 4 + 6 * p.leg;
        legs = {Point{-6, -5}, {-10, 0}, {ex, ey}, {ex - 5, ey + 2}};
    }
    g.tube(
        legs, [](double u) { return lerp(2, .7, u); },
        [&](double u, double v) { return v > .55   ? C[2]
                                         : u > .66 ? C[3]
                                                   : C[4]; });
    const auto   f = legs[3], previous = legs[2];
    const double dx = f.x - previous.x, dy = f.y - previous.y, length = jsHypot2(dx, dy),
                 l = length == 0 ? 1 : length;
    for (int k : {-1, 0, 1})
        g.line(f.x, f.y, f.x + (dx / l) * 2 - (dy / l) * k * 1.6, f.y + (dy / l) * 2 + (dx / l) * k * 1.6, C[3]);
    if (!p.shut) {
        const double gx = 4.6, gy = -14.4, X = gx * cs - (gy + 7) * sn * 1, Y = gx * sn + (gy + 7) * cs - 7;
        g.set(X, Y, C[6]);
    }
    for (int u = 3; u <= 11; ++u) {
        const double v = -7.8 + (u - 3) * .05, X = (u * sx) * cs - ((v + 7) * sy) * sn,
                     Y = (u * sx) * sn + ((v + 7) * sy) * cs - 7;
        g.set(X, Y, C[1]);
    }
    const Point front = p.air ? Point{9, -1} : Point{8, -.5};
    g.line(7, -4, front.x, front.y, C[3]);
    g.set(front.x + 1, front.y, C[3]);
    g.set(front.x - 1, front.y, C[3]);
    if (p.throat > .05) {
        const double r = .8 + 2.8 * p.throat, cx = 7, cy = -5.2 + r * .25;
        g.ellipse(
            cx, cy, r * 1.1, r,
            [&](double nx, double ny) {
                return nx * -.6 + ny * -.8 > .55 ? Bl[2] : ny > .5 ? Bl[1] : mix(Bl[2], C[5], .4);
            },
            .9);
        g.set(cx - r * .4, cy - r * .45, C[6]);
    }
    g.outline(C[0]);
}
void draw(Paint& p, double time, const PixelFxSettings& o) {
    const double          t = critter::shift(time, D, o.seed);
    const auto&           C = o.palette;
    const auto            z = pose(t);
    critter::Grid<44, 44> g(-22, -40);
    body(g, C, o.critter.belly, z);
    g.draw(p, 1, 0, jsRound(z.y));
    const double tongue = (t - SNAP + .06) / .14;
    if (tongue > 0 && tongue < 1) {
        const auto   bug = bugAt(SNAP - .001);
        const double e = std::sin(PI * tongue), mx = 11, my = z.y - 8, x = lerp(mx, bug->x, e), y = lerp(my, bug->y, e);
        p.line(mx, my, x, y, hex(0xe8607a));
        p.line(mx, my + 1, x, y + 1, mix(hex(0xe8607a), C[0], .4));
        p.circle(x, y, 1.4, hex(0xe8607a));
    }
    if (const auto bug = bugAt(t)) {
        const bool flap = std::sin(t * 90) > 0;
        p.px(bug->x, bug->y, hex(0x141018));
        p.px(bug->x + 1, bug->y, hex(0x141018));
        p.dot(bug->x, bug->y - (flap ? 1 : 0), hex(0xc8d0e0), .8);
        p.dot(bug->x + 1, bug->y - (flap ? 0 : 1), hex(0xc8d0e0), .8);
        p.glow(bug->x, bug->y, 3, hex(0xc8d0e0), .15);
    }
    Particles ps;
    ps.time = t;
    constexpr std::array<std::array<double, 3>, 2> BURSTS{{{{UP, 7, .6}}, {{LAND, 18, 1}}}};
    for (auto burst : BURSTS) {
        const double age = wrap(t - burst[0], D), power = burst[2];
        for (int i = 0; i < burst[1]; ++i) {
            Rng          r(hash({o.seed, uint32_t(i), jsUint(burst[0] * 100)}));
            const double life = r.range(.6, 1.1) * power;
            if (age > life) continue;
            const int    side = r.sign();
            const double v = r.range(24, 70) * power, x = side * (7 + (v / 3.5) * (1 - std::exp(-age * 3.5))),
                         y = -1.5 - r.range(1, 5) * age, size = r.range(2.2, 4.2) * (.65 + .35 * power);
            if (age < life)
                ps.list.push_back(
                    {ParticleKind::Smoke, x, y, 0, 0, life - age, life, size, 0, 0, hex(0x8a7f92), ParticleFlag::Big});
        }
        if (power == 1)
            for (uint32_t i = 0; i < 8; ++i) {
                Rng          r(hash({o.seed, i, 909}));
                const double x = r.range(-8, 8), vx = r.range(-40, 40), vy = -r.range(30, 70);
                const auto   b = critter::tossSample(x, -1, vx, vy, age, 700, -.5);
                if (age < .7) p.dot(b.x, b.y, i % 2 ? hex(0x6a6074) : hex(0x9a90a4), b.landed ? 1.4 - age * 2 : 1);
            }
    }
    ps.draw(p);
}
void back(Paint& p, double time, const PixelFxSettings& o) {
    const auto   z = pose(critter::shift(time, D, o.seed));
    const double h = clamp(-z.y / HOP);
    critter::floorShadow(p, 0, -.2, 13 * (1 - .35 * h), 1.6, o.palette[0], .6 - .3 * h);
}
}  // namespace
const Descriptor& frogDescriptor() {
    static const std::array<Option, 1> options{{{"belly", 1, 64, false, 3, &PixelFxCritterSettings::belly}}};
    static const Descriptor            d{{PixelFxKind::Frog, "critters/frog", 112, 88, 56, 80, D, true},
                                         7,
                                         [](PixelFxSettings& s) {
                                  s.seed          = 1;
                                  s.palette       = {hex(0x0a160c), hex(0x173a18), hex(0x256024), hex(0x3a8a2e),
                                                     hex(0x62b440), hex(0xa8e070), hex(0xe8ffc0)};
                                  s.critter.belly = {hex(0x5e5a24), hex(0xbdb45a), hex(0xefe9a8)};
                                         },
                                         draw,
                                         options,
                                         nullptr,
                                         back};
    return d;
}
}  // namespace eve::stylize::pixelfx
