// eve-hot-path
// Native adaptation of pixelfx critters/bat.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Critter.h"
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4;
struct Frame {
    double                arm, length;
    std::array<double, 3> fingers, lengths;
    double                lift;
};
constexpr std::array<Frame, 4> FRAMES{{{-58, 8, {-112, -76, -38}, {9, 11, 9}, 1},
                                       {-18, 9, {-40, -12, 16}, {10, 11, 9}, 0},
                                       {28, 8, {50, 72, 94}, {9, 10, 8}, -2},
                                       {-42, 6, {-66, -38, -10}, {7, 8, 6}, -1}}};
double                         radians(double d) { return (d * PI) / 180; }
Point                          at(double t) {
    const double a = (t / D) * TAU;
    return {22 * std::sin(a), 7 * std::sin(2 * a)};
}
struct Wing {
    Point                shoulder, wrist;
    std::array<Point, 3> tips;
    std::array<Point, 9> polygon;
};
Wing wingPoints(const Frame& f, int side) {
    Wing w;
    w.shoulder     = {side * 2.5, -2};
    const double a = radians(f.arm);
    w.wrist        = {w.shoulder.x + side * std::cos(a) * f.length, w.shoulder.y + std::sin(a) * f.length};
    for (size_t i = 0; i < 3; ++i)
        w.tips[i] = {w.wrist.x + side * std::cos(radians(f.fingers[i])) * f.lengths[i],
                     w.wrist.y + std::sin(radians(f.fingers[i])) * f.lengths[i]};
    const auto pull = [&](Point a0, Point b, double k) {
        return Point{lerp((a0.x + b.x) / 2, w.wrist.x, k), lerp((a0.y + b.y) / 2, w.wrist.y, k)};
    };
    const Point hip{double(side * 2), 3.5};
    w.polygon = {w.shoulder, w.wrist,
                 w.tips[0],  pull(w.tips[0], w.tips[1], .22),
                 w.tips[1],  pull(w.tips[1], w.tips[2], .22),
                 w.tips[2],  pull(w.tips[2], hip, .38),
                 hip};
    return w;
}
void body(critter::Grid<48, 40>& g, std::span<const uint32_t> C, std::span<const uint32_t> E, const Frame& f) {
    g.clear();
    for (int side : {-1, 1}) {
        const auto w = wingPoints(f, side);
        g.poly(w.polygon, [&](double x, double y) {
            const auto   a = w.wrist, b = w.tips[0];
            const double dx = b.x - a.x, dy = b.y - a.y, length = jsHypot2(dx, dy),
                         lead = std::abs(((x - a.x) * dy - (y - a.y) * dx) / (length == 0 ? 1 : length)),
                         near = jsHypot2(x - w.shoulder.x, y - w.shoulder.y);
            if (lead < 1.2 && f.arm < 20) return C[3];
            return near < 4.5 ? C[1] : C[2];
        });
        g.line(w.shoulder.x, w.shoulder.y, w.wrist.x, w.wrist.y, C[4]);
        for (auto tip : w.tips)
            g.line(w.wrist.x, w.wrist.y, tip.x, tip.y, [&](double u) { return u < .6 ? C[4] : C[3]; });
        g.set(w.wrist.x, w.wrist.y - 1, C[5]);
    }
    g.ellipse(0, .5, 3.8, 4.8, [&](double nx, double ny) {
        return nx * -.6 + ny * -.8 > .45      ? C[4]
               : nx * .6 + ny * .8 > .55      ? C[2]
               : ny > .1 && std::abs(nx) < .5 ? C[4]
                                              : C[3];
    });
    for (int side : {-1, 1}) {
        const std::array<Point, 3> ear{{{side * .6, -3.5}, {side * 3.6, -9.5}, {side * 3.8, -3}}};
        g.poly(ear, side < 0 ? C[4] : C[3]);
        g.line(side * 2.6, -4, side * 3.2, -7.5, C[5]);
    }
    g.set(-2, -1, E[1]);
    g.set(1, -1, E[1]);
    g.set(-1, 0, C[1]);
    g.set(0, 0, C[1]);
    g.set(-1, 1, C[6]);
    g.set(0, 1, C[6]);
    g.set(-2, 5, C[3]);
    g.set(1, 5, C[3]);
    g.outline(C[0]);
}
void draw(Paint& p, double time, const PixelFxSettings& o) {
    const double t     = critter::shift(time, D, o.seed);
    const auto&  C     = o.palette;
    const auto&  E     = o.critter.eyes;
    const auto   pos   = at(t);
    const size_t frame = size_t(std::floor(wrap((t / D) * 6, 1) * 4)) % 4;
    const auto&  f     = FRAMES[frame];
    const double X = jsRound(pos.x), Y = jsRound(pos.y + f.lift);
    for (int k = 2; k <= 16; ++k) {
        const auto   a = at(t - (k - 1) * .022), b = at(t - k * .022);
        const double u = 1 - double(k) / 17;
        for (double ex : {-1.5, 1.5}) p.glowLine(a.x + ex, a.y - .5, b.x + ex, b.y - .5, 2.6, E[1], .16 * u);
    }
    cycle(t, D, 9, o.seed * 11u + 1u, D, [&](double age, double life, Rng& r, int) {
        const double rx = r.range(-50, 50), phase = r.next(), x = rx + std::sin(age * 1.1 + phase * 6) * 3,
                     ry = r.range(-34, 34), velocity = r.range(-2, 3), y = ry + age * velocity,
                     tw = std::sin(age * 2.6 + r.next() * 6),
                     a  = std::min({1., age * 2, (life - age) * 2}) * (.4 + .5 * tw);
        p.dot(x, y, tw > .7 ? C[6] : C[5], a);
        if (tw > .5) p.glow(x, y, 3, C[5], .2 * a);
    });
    critter::Grid<48, 40> g(-24, -22);
    if (frame == 2) {
        body(g, C, E, FRAMES[1]);
        g.reworkCells([&](double, double, uint32_t c) { return mix(c, C[4], .35); });
        g.alpha.fill(.35f);
        g.draw(p, 1, X, Y - FRAMES[1].lift + f.lift);
    }
    body(g, C, E, f);
    g.draw(p, 1, X, Y);
    p.glow(X - 3, Y - 6, 10, C[5], .12);
    for (double ex : {-1.5, 1.5}) {
        p.glow(X + ex, Y - .5, 7, E[1], .6);
        p.glow(X + ex, Y - .5, 2.5, E[2], .5);
    }
    p.glow(X, Y, 22, E[0], .25);
    for (double origin : {.3, 1.5}) {
        const double k = wrap(t - origin, D) / .24;
        if (k < 1) p.sparkle(X - 2, Y - 1, 1 + 2.5 * std::sin(PI * k), E[3], E[2], .4 * std::sin(PI * k));
    }
}
}  // namespace
const Descriptor& batDescriptor() {
    static const std::array<Option, 1> options{{{"eyes", 1, 64, false, 4, &PixelFxCritterSettings::eyes}}};
    static const Descriptor            d{{PixelFxKind::Bat, "critters/bat", 112, 80, 56, 40, D, true},
                                         7,
                                         [](PixelFxSettings& s) {
                                  s.seed         = 1;
                                  s.palette      = {hex(0x0c0912), hex(0x1f1629), hex(0x382645), hex(0x5b3f66),
                                                    hex(0x866089), hex(0xb88fb0), hex(0xe8cfe0)};
                                  s.critter.eyes = {hex(0x8a1020), hex(0xff3b30), hex(0xffb04a), hex(0xfff0c0)};
                                         },
                                         draw,
                                         options};
    return d;
}
}  // namespace eve::stylize::pixelfx
