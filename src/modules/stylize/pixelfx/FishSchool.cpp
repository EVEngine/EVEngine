// eve-hot-path
// Native adaptation of pixelfx critters/fish-school.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Critter.h"
#include "stylize/pixelfx/CritterBubble.h"
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 4.8;
struct Fish {
    double x, y, size;
    bool   back;
    double phase;
};
constexpr std::array<Fish, 7> SCHOOL{{{-34, -16, .72, true, .1},
                                      {2, -20, .68, true, .55},
                                      {30, -10, .7, true, .3},
                                      {-22, 4, 1, false, .8},
                                      {12, -4, 1, false, .2},
                                      {-40, -2, .95, false, .45},
                                      {26, 14, .96, false, .65}}};
struct Position {
    double x, y, dart;
};
Position place(const Fish& f, size_t i, double t) {
    const double a = (t / D) * TAU, dt = wrap(t - std::fmod(double(i) * .71 * D, D), D),
                 dart = i == 4 ? (dt < .25   ? ease::outCubic(dt / .25)
                                  : dt < 1.6 ? 1 - ease::inOutSine((dt - .25) / 1.35)
                                             : 0)
                               : 0;
    return {f.x + 3 * std::sin(a + f.phase * TAU) + 8 * dart, f.y + 1.8 * std::sin(2 * a + f.phase * 5),
            i == 4 && dt < .6 ? 1 - dt / .6 : 0};
}
void fish(critter::Grid<34, 24>& g, std::span<const uint32_t> C, std::span<const uint32_t> stripe, double size,
          double t, double phase, double dart) {
    g.clear();
    const double length = 15 * size, height = 8 * size, beats = 12 + (dart > 0 ? 6 : 0),
                 w = (t / D) * TAU * beats + phase * TAU, amp = (1.3 + 1.2 * dart) * size;
    const auto   bend = [&](double u) { return amp * std::pow(u, 1.8) * std::sin(w - 4 * u); };
    const auto   half = [&](double u) {
        return (height / 2) * (u < .3 ? std::sqrt(u / .3) : 1 - std::pow((u - .3) / .7, 1.5) * .72);
    };
    g.shade(-length, -height, 0, height, [&](double x, double y) -> uint32_t {
        const double u = -x / length;
        if (u < 0 || u > 1) return 0;
        const double dy = y - bend(u), h = half(u);
        if (std::abs(dy) > h) return 0;
        const double v = dy / h;
        if (v > -.12 && v < .12 && u > .22 && u < .92) return u < .3 ? stripe[1] : stripe[v < 0 ? 2 : 1];
        if (v < -.6) return u < .5 && v < -.8 ? C[3] : C[2];
        if (v < -.12) return C[3];
        if (v < .5) return C[4];
        return v > .86 ? C[4] : C[5];
    });
    const double tb = bend(1), ta = std::atan(amp * 1.8 * .9 * std::cos(w - 4)) * .8, tail = 6 * size, hb = half(1);
    const auto   rot = [&](double dx, double dy) {
        return Point{-(length + dx * std::cos(ta) - dy * std::sin(ta)), tb + dx * std::sin(ta) + dy * std::cos(ta)};
    };
    const std::array<Point, 5> tailPts{
        {rot(-.5, -hb), rot(tail, -hb - 3.4 * size), rot(tail * .62, 0), rot(tail, hb + 3.4 * size), rot(-.5, hb)}};
    g.poly(tailPts, [&](double, double y) { return y < tb - 1 ? C[4] : C[3]; }, .88);
    constexpr double           d0 = .34, d1 = .66;
    const std::array<Point, 3> dorsal{{{-d0 * length, bend(d0) - half(d0) + .5},
                                       {-(d0 + .08) * length, bend(d0) - half(d0) - 3.6 * size},
                                       {-d1 * length, bend(d1) - half(d1) + .5}}};
    g.poly(dorsal, [&](double x) { return std::fmod(jsRound(x), 2) != 0 ? C[3] : C[4]; }, .85);
    const double pf = std::sin(w * .5) * 1.2;
    g.set(-.34 * length, bend(.34) + 1.5 * size + pf * .3, C[4], .85);
    g.set(-.34 * length - 1, bend(.34) + 2 * size + pf * .6, C[4], .85);
    for (int y = -2; y <= 1; ++y) g.set(-.26 * length, bend(.26) + y * size, C[2]);
    const double ex = -.13 * length - 1, ey = bend(.13) - .25 * height;
    g.set(ex, ey, C[0]);
    g.set(ex + 1, ey, C[0]);
    g.set(ex, ey + 1, C[1]);
    g.set(ex + 1, ey + 1, C[0]);
    g.set(ex + 1, ey, C[6]);
    g.outline(C[0]);
}
void caustics(Paint& p, std::span<const uint32_t> C, double t) {
    const double a = (t / D) * TAU;
    for (int y = -38; y < 38; ++y)
        for (int x = -66; x < 66; ++x) {
            const double fall = clamp(1 - (std::pow(double(x) / 64, 2) + std::pow(double(y) / 40, 2))) *
                                (.6 + .4 * clamp(-double(y) / 40 + .6));
            if (fall <= 0) continue;
            const double u = x * .16 + 1.9 * std::sin(y * .12 + a), v = y * .19 + 1.9 * std::sin(x * .1 - a),
                         n = std::sin(u) + std::sin(v), shade = .75 + .25 * std::sin(x * .05 - y * .04 + 2 * a),
                         f = std::max(0., 1 - std::abs(n) * 5) * fall * shade;
            if (f > .55)
                p.light(x, y, C[4], .3);
            else if (f > .25)
                p.light(x, y, C[3], .14);
        }
}
void draw(Paint& p, double time, const PixelFxSettings& o) {
    const double                                   t      = critter::shift(time, D, o.seed);
    const auto&                                    C      = o.palette;
    const auto&                                    stripe = o.critter.stripe;
    constexpr std::array<std::array<double, 3>, 3> SHAFTS{{{{-30, 10, 0}}, {{14, 7, 2.1}}, {{44, 12, 4}}}};
    for (auto q : SHAFTS) {
        const double sw = std::sin((t / D) * TAU + q[2]) * 4;
        p.glowLine(q[0] + sw - 12, -40, q[0] + sw + 8, 40, q[1], C[4], .05);
    }
    critter::Grid<34, 24> g(-28, -12);
    for (bool back : {true, false})
        for (size_t i = 0; i < SCHOOL.size(); ++i) {
            const auto& f = SCHOOL[i];
            if (f.back != back) continue;
            const auto pos = place(f, i, t);
            fish(g, C, stripe, f.size, t, f.phase, pos.dart);
            if (back) g.reworkCells([&](double, double, uint32_t c) { return mix(c, C[1], .42); });
            g.draw(p, 1, pos.x, pos.y);
        }
    cycle(t, D, 9, o.seed * 13u + 1u, 1.6, 2.6, [&](double age, double life, Rng& r, int i) {
        const size_t index  = size_t(i) % SCHOOL.size();
        const auto&  f      = SCHOOL[index];
        const auto   pos    = place(f, index, wrap(t - age, D));
        const double radius = r.range(.6, 1.6) * f.size, phase = r.next() * TAU,
                     x = pos.x + 1 + std::sin(age * (4 + 2 / radius) + phase) * (.5 + radius * .7),
                     y = pos.y - 1 - (16 + 12 * radius) * age;
        if (y < -38) return;
        critter::bubbleLook(p, x, y, radius, {C[6], C[5], C[4], C[3]},
                            std::min({1., age * 4, (life - age) * 3, (y + 38) / 4}));
    });
    caustics(p, C, t);
    p.glow(0, -8, 74, C[2], .2);
    p.glow(-6, -18, 40, C[3], .1);
}
}  // namespace
const Descriptor& fishSchoolDescriptor() {
    static const std::array<Option, 1> options{{{"stripe", 1, 64, false, 3, &PixelFxCritterSettings::stripe}}};
    static const Descriptor            d{{PixelFxKind::FishSchool, "critters/fish-school", 136, 80, 68, 40, D, true},
                                         7,
                                         [](PixelFxSettings& s) {
                                  s.seed           = 1;
                                  s.palette        = {hex(0x0a1a2a), hex(0x123552), hex(0x1d5a7e), hex(0x2f8aa8),
                                                      hex(0x62bccc), hex(0xbfe8e6), hex(0xf4fff8)};
                                  s.critter.stripe = {hex(0x5a1e14), hex(0xd8582a), hex(0xffa04a)};
                                         },
                                         draw,
                                         options};
    return d;
}
}  // namespace eve::stylize::pixelfx
