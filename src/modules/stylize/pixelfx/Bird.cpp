// eve-hot-path
// Native adaptation of pixelfx critters/bird.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Critter.h"
#include "stylize/pixelfx/Descriptor.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4, GLIDE = 1.5;
using Pose = std::array<Point, 8>;
constexpr Pose UP{{{0, 0}, {-1, -8}, {-5, -15}, {-8, -13.5}, {-10.5, -10.5}, {-11, -7}, {-9, -3}, {-6, 1}}};
constexpr Pose MID{{{0, 0}, {-3, -2.5}, {-9, -2.5}, {-10.5, -.5}, {-10, 1.2}, {-8.5, 2}, {-7, 2.2}, {-5, 2}}};
constexpr Pose DOWN{{{0, 0}, {-1, 6}, {-3.5, 12}, {-6, 11}, {-7.5, 8.5}, {-8, 6}, {-7, 3}, {-5, 2}}};
constexpr Pose FOLD{{{0, 0}, {-3, -4}, {-10, -5.5}, {-11, -4}, {-10, -2.5}, {-8.5, -1}, {-7, .5}, {-5, 1.5}}};
constexpr Pose GLIDING{{{0, 0}, {-2, -5}, {-7, -10}, {-10, -9}, {-12, -6.5}, {-11.5, -3.5}, {-9, -1}, {-6, 1}}};
Pose           mixPose(const Pose& a, const Pose& b, double u) {
    Pose result;
    for (size_t i = 0; i < a.size(); ++i) result[i] = {lerp(a[i].x, b[i].x, u), lerp(a[i].y, b[i].y, u)};
    return result;
}
struct Wing {
    Pose   pts;
    bool   down;
    double u;
};
Wing wing(double t) {
    t = wrap(t, D);
    if (t < GLIDE) {
        const double u = std::fmod((t / GLIDE) * 4, 1);
        if (u < .4) {
            const double k = ease::inOutSine(u / .4);
            return {k < .5 ? mixPose(UP, MID, k * 2) : mixPose(MID, DOWN, k * 2 - 1), true, u};
        }
        const double k = ease::inOutSine((u - .4) / .6);
        return {k < .5 ? mixPose(DOWN, FOLD, k * 2) : mixPose(FOLD, UP, k * 2 - 1), false, u};
    }
    const double g = (t - GLIDE) / (D - GLIDE), into = smooth(0, .15, g), out = smooth(.85, 1, g);
    return {mixPose(mixPose(UP, GLIDING, into), UP, out), false, 0};
}
double lift(double t) {
    t        = wrap(t, D);
    double y = 0;
    for (int k = 0; k < 6; ++k)
        if (wing(t - k * .02).down) y -= .45;
    if (t >= GLIDE) y += 2.2 * std::sin(PI * ((t - GLIDE) / (D - GLIDE)));
    return y + 1.2;
}
Pose wingPoints(const Wing& w, bool far) {
    Pose pts;
    for (size_t i = 0; i < pts.size(); ++i)
        pts[i] = {1 + w.pts[i].x - (far ? 1 : 0), -2 + w.pts[i].y * (far ? .85 : 1) - (far ? 1 : 0)};
    return pts;
}
bool inBody(double x, double y) {
    return std::pow((x + 1) / 8.5, 2) + std::pow(y / 5.2, 2) <= 1 ||
           std::pow(x - 6.5, 2) + std::pow(y + 3, 2) <= std::pow(4.6, 2);
}
void body(critter::Grid<52, 46>& g, std::span<const uint32_t> C, std::span<const uint32_t> breast, double t,
          const Wing& w) {
    g.clear();
    const auto far = wingPoints(w, true);
    g.poly(far, C[1]);
    const double tf = .25 + .35 * std::sin((t / D) * TAU * 2);
    for (int i = 0; i < 3; ++i) {
        const double               a = PI + (i - 1) * tf * .5 + .1, length = 9 - std::abs(i - 1);
        const std::array<Point, 4> pts{{{-7, -2.4},
                                        {-7 + std::cos(a) * length, -1 + std::sin(a) * length - 1.2},
                                        {-7 + std::cos(a) * length, -1 + std::sin(a) * length + 1.2},
                                        {-7, .6}}};
        g.poly(pts, i == 0 ? C[3] : C[2]);
    }
    g.shade(-10, -9, 12, 6, [&](double x, double y) -> uint32_t {
        if (!inBody(x, y)) return 0;
        const bool top = !inBody(x, y - 2.2), under = !inBody(x + .5, y + 1.6);
        if (y > -.5 && x > -2.5) return under ? breast[1] : x > 3 && y < 2 ? breast[3] : breast[2];
        if (y > 1.5) return under ? C[4] : C[5];
        return top ? (x > 2 ? C[5] : C[4]) : y < -3.5 && x > 3 ? C[4] : C[3];
    });
    const std::array<Point, 3> beak{{{10.5, -4.5}, {14.5, -3}, {10.5, -2}}};
    g.poly(beak, [](double, double y) { return y < -3 ? hex(0xf0b030) : hex(0xa86a18); });
    g.set(8, -4, C[0]);
    g.set(8, -5, C[6]);
    g.set(9, -4, C[0]);
    g.line(5, -7, 9, -6.5, C[2]);
    const auto near = wingPoints(w, false);
    const auto edge = [&](double x, double y) {
        double distance = INFINITY;
        for (size_t i = 1; i < 3; ++i) {
            const auto   a = near[i - 1], b = near[i];
            const double dx = b.x - a.x, dy = b.y - a.y, L2 = dx * dx + dy * dy == 0 ? 1 : dx * dx + dy * dy,
                         u = clamp(((x - a.x) * dx + (y - a.y) * dy) / L2);
            distance       = std::min(distance, jsHypot2(a.x + dx * u - x, a.y + dy * u - y));
        }
        return distance;
    };
    g.poly(near, [&](double x, double y) {
        const double d = edge(x, y);
        return d < 1.2 ? C[5] : d < 3 ? C[4] : C[3];
    });
    for (size_t i = 3; i < 7; ++i)
        g.line(near[1].x, near[1].y, lerp(near[1].x, near[i].x, .92), lerp(near[1].y, near[i].y, .92), C[2]);
    g.outline(C[0]);
}
void draw(Paint& p, double time, const PixelFxSettings& o) {
    const double          t = critter::shift(time, D, o.seed), y = lift(t);
    const auto&           C = o.palette;
    const auto            w = wing(t);
    critter::Grid<52, 46> g(-26, -26);
    if (w.down && w.u > .1) {
        const double s   = t - .05;
        const auto   ws  = wing(s);
        const auto   pts = wingPoints(ws, false);
        g.clear();
        g.poly(pts, C[6]);
        g.alpha.fill(.42f);
        g.draw(p, 1, 0, jsRound(lift(s)));
    }
    body(g, C, o.critter.breast, t, w);
    g.draw(p, 1, 0, jsRound(y));
    const double age = wrap(t - .45, D);
    if (age < 1.8) {
        const double x = -6 - 14 * age - 3 * std::sin(age * 5), fy = 4 + 9 * age + 1.5 * std::cos(age * 5) * age,
                     rock = std::sin(age * 5) * .8, a = std::min(1., (1.8 - age) * 2);
        for (int i = -2; i <= 2; ++i)
            p.dot(x + i * std::cos(rock), fy + i * std::sin(rock) * .8, i < 0 ? C[5] : C[4], a);
        p.dot(x + 2.6 * std::cos(rock), fy + 2.6 * std::sin(rock) * .8, C[3], a);
    }
    p.glow(2, y - 6, 16, C[5], .12);
}
}  // namespace
const Descriptor& birdDescriptor() {
    static const std::array<Option, 1> options{{{"breast", 1, 64, false, 4, &PixelFxCritterSettings::breast}}};
    static const Descriptor            d{{PixelFxKind::Bird, "critters/bird", 112, 80, 56, 40, D, true},
                                         7,
                                         [](PixelFxSettings& s) {
                                  s.seed           = 1;
                                  s.palette        = {hex(0x0a0e1e), hex(0x18244a), hex(0x24407a), hex(0x3466aa),
                                                      hex(0x5a94d4), hex(0x9cc8f0), hex(0xeaf6ff)};
                                  s.critter.breast = {hex(0x4a1a0e), hex(0xa8441c), hex(0xe8783a), hex(0xffb070)};
                                         },
                                         draw,
                                         options};
    return d;
}
}  // namespace eve::stylize::pixelfx
