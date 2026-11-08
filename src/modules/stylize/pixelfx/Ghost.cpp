// eve-hot-path
// Native adaptation of pixelfx critters/ghost.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Critter.h"
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4, R = 10.5, TOP = 2 * R + 9, HW = R + 2.2, LW = (2 * HW) / 4;
Point            at(double t, double hover) {
    const double a = (t / D) * TAU;
    return {4 * std::sin(a), -hover - 3 * std::sin(2 * a + .8)};
}
double halfWidth(double h) {
    if (h > TOP || h < -1) return -1;
    if (h > TOP - R) return std::sqrt(std::max(0., R * R - std::pow(h - (TOP - R), 2)));
    return lerp(R + 2.2, R, std::pow(h / (TOP - R), .7));
}
void draw(Paint& p, double time, const PixelFxSettings& o) {
    const double t   = critter::shift(time, D, o.seed);
    const auto&  C   = o.palette;
    const auto   pos = at(t, o.critter.hover);
    const double lag = at(t - .3, o.critter.hover).x - pos.x, phase = (t / D) * TAU * 2;
    const auto   off = [&](double h) { return lag * std::pow(1 - clamp(h / TOP), 1.4); };
    struct Lobe {
        double j, u, depth;
    };
    const auto lobe = [&](double x) {
        const double j = clamp(std::floor((x + HW) / LW), 0, 3), c = -HW + (j + .5) * LW, u = (x - c) / (LW / 2);
        return Lobe{j, u, (2.2 + 1.1 * std::sin(phase - j * 1.5)) * std::sqrt(std::max(0., 1 - u * u))};
    };
    const auto inside = [&](double x, double y) {
        const double h = -y, X = x - off(h);
        if (h < 0 && -h > lobe(X).depth) return false;
        const double half = halfWidth(std::max(0., h));
        return half >= 0 && std::abs(X) <= half;
    };
    const auto arm = [&](double x, double y) {
        for (int side : {-1, 1}) {
            const double ax = side * (R + 1.2) + off(13), ay = -13 + 1.6 * std::sin(phase + side * 1.3);
            if (std::pow((x - ax) / 3, 2) + std::pow((y - ay) / 2.2, 2) <= 1) return true;
        }
        return false;
    };
    const auto any = [&](double x, double y) { return inside(x, y) || arm(x, y); };
    const auto lit = [&](double x, double y, double k) { return any(-9 + (x + 9) / k, -TOP - 1 + (y + TOP + 1) / k); };
    critter::Grid<40, 46> g(-20, -40);
    g.shade(-19, -TOP - 2, 19, 5, [&](double x, double y, auto& out) -> uint32_t {
        if (!any(x, y)) return 0;
        const double h = -y, X = x - off(h);
        int          i = lit(x, y, .62) ? 5 : 4;
        if (!any(x + 2.4, y + 1.8)) i = 3;
        if (!any(x + 1.2, y + 1) && X > 2) i = 2;
        if (h > TOP - R - 2 && X < 1 && !any(x - 1.5, y - 1.5) && any(x - .6, y - .6)) i = 6;
        if (h < 1) {
            const auto l = lobe(X);
            if (l.u > .1 || -h > l.depth - 1.2) i = std::min(i, 3);
        }
        if (h < 7 && h >= -.5)
            for (int j = 1; j < 4; ++j) {
                const double cx = -HW + j * LW + .6 * std::sin(phase - j), w = (7 - h) * .09;
                if (std::abs(X - cx) <= w + .3)
                    i = std::min(i, 3);
                else if (cx - X > w + .3 && cx - X < w + 1.3 && i == 4)
                    i = 5;
            }
        out.a = h < 2 ? .86 : .95;
        return C[size_t(i)];
    });
    const double wrapped = wrap(t, D),
                 look    = wrapped < .7    ? 0
                           : wrapped < 1.3 ? 1
                           : wrapped < 1.9 ? -1
                                           : 0,
                 ey = -TOP + R - 1, mo = .5 + .5 * std::sin((t / D) * TAU * 2);
    const bool   blink = wrapped > 1.95 && wrapped < 2.03;
    for (int side : {-1, 1}) {
        const double ex = side * 4 - (side > 0 ? 1 : 0) + look;
        if (blink) {
            g.set(ex, ey + 2, C[1]);
            g.set(ex + 1, ey + 2, C[1]);
        } else {
            for (int k = 0; k < 4; ++k) {
                g.set(ex, ey + k, C[0]);
                g.set(ex + 1, ey + k, C[0]);
            }
            g.set(ex + (look < 0 ? 0 : 1), ey, C[2]);
        }
    }
    const double my = ey + 6;
    g.set(look - 1, my, C[1]);
    g.set(look, my, C[1]);
    if (mo > .4) {
        g.set(look - 1, my + 1, C[0]);
        g.set(look, my + 1, C[0]);
    }
    g.outline([&](double, double, int, int ny) { return ny > 0 ? C[2] : C[1]; }, .95);
    g.draw(p, 1, pos.x, pos.y);
    cycle(t, D, 12, o.seed * 3u + 1u, 1, 1.7, [&](double age, double life, Rng& r, int i) {
        const auto   born = at(wrap(t - age, D), o.critter.hover);
        const double k = age / life, x0 = born.x + r.range(-10, 10), y0 = born.y + 1.5,
                     x = x0 + std::sin(age * 2.2 + i) * 2.4, y = y0 + r.range(5, 11) * age - 3 * age * age,
                     a = std::min(1., k * 6) * std::min(1., (1 - k) * 2.2), tw = .6 + .4 * std::sin(age * 8 + i * 1.7);
        p.dot(x, y, tw > .8 ? C[6] : C[5], a);
        if (r.isChance(.3)) p.dot(x, y - 1, C[4], a * .7);
        p.glow(x, y, 7, C[4], .42 * a * tw);
    });
    p.glow(pos.x, pos.y - TOP * .55, 54, C[3], .3);
    p.glow(pos.x - 3, pos.y - TOP * .72, 14, C[5], .12);
}
void back(Paint& p, double time, const PixelFxSettings& o) {
    const double t   = critter::shift(time, D, o.seed);
    const auto   pos = at(t, o.critter.hover);
    const auto&  C   = o.palette;
    const double h   = clamp((-pos.y - 8) / 16);
    critter::floorShadow(p, pos.x, 0, 11 - 3 * h, 1.6, C[0], .55 - .2 * h);
    for (int x = -24; x <= 24; x += 6)
        p.glow(pos.x + x, -1, 16, C[4], .14 * (1 - std::abs(double(x)) / 34) * (1 - .4 * h));
}
}  // namespace
const Descriptor& ghostDescriptor() {
    static const std::array<Option, 1> options{{{"hover", 0, 256, false, 0, &PixelFxCritterSettings::hover}}};
    static const Descriptor            d{{PixelFxKind::Ghost, "critters/ghost", 96, 100, 48, 90, D, true},
                                         7,
                                         [](PixelFxSettings& s) {
                                  s.seed          = 1;
                                  s.critter.hover = 14;
                                  s.palette       = {hex(0x10142a), hex(0x232c52), hex(0x3e4f86), hex(0x6b84bb),
                                                     hex(0xa3b8e2), hex(0xd8e4ff), WHITE};
                                         },
                                         draw,
                                         options,
                                         nullptr,
                                         back};
    return d;
}
}  // namespace eve::stylize::pixelfx
