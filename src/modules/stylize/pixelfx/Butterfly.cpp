// eve-hot-path
// Native adaptation of pixelfx critters/butterfly.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Critter.h"
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double               D = 3.2;
constexpr std::array<Point, 9> FORE{
    {{0, -1}, {3, -6}, {8, -11}, {13, -12.5}, {16.5, -10.5}, {16.5, -6}, {13, -1.5}, {7, 1}, {2, 1.5}}};
constexpr std::array<Point, 8> HIND{
    {{0, 1}, {6, 1.5}, {11, 4}, {12.5, 8.5}, {10.5, 12.5}, {6.5, 13.5}, {3, 11}, {.5, 6}}};
constexpr std::array<Point, 2> BURSTS{{{0, .95}, {1.5, 2.55}}};
constexpr std::array<Point, 4> SPOTS{{{13.2, -10.2}, {15.2, -8.4}, {14.6, -5.6}, {11.6, -11.4}}};
bool                           flapping(double t) {
    for (auto b : BURSTS)
        if (t >= b.x && t < b.y) return true;
    return false;
}
double fold(double t) {
    t = wrap(t, D);
    for (auto b : BURSTS)
        if (t >= b.x && t < b.y) {
            const double n = jsRound((b.y - b.x) * 6.3), ph = ((t - b.x) / (b.y - b.x)) * n;
            return .14 + .86 * (.5 + .5 * std::cos(ph * TAU));
        }
    return 1;
}
Point at(double t) {
    t                 = wrap(t, D);
    const double a    = (t / D) * TAU;
    double       lift = 0;
    for (auto b : BURSTS) lift += -3 * std::sin(PI * clamp((t - b.x) / (b.y - b.x)));
    return {30 * std::sin(a) + 6 * std::sin(3 * a), 12 * std::sin(2 * a + .4) + lift};
}
bool inside(std::span<const Point> pts, double x, double y) {
    bool in = false;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const auto a = pts[i], b = pts[j];
        if ((a.y > y) != (b.y > y) && x < ((b.x - a.x) * (y - a.y)) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}
double edgeDistance(std::span<const Point> pts, double x, double y) {
    double distance = INFINITY;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const auto   a = pts[j], b = pts[i];
        const double dx = b.x - a.x, dy = b.y - a.y, L2 = dx * dx + dy * dy == 0 ? 1 : dx * dx + dy * dy,
                     u = clamp(((x - a.x) * dx + (y - a.y) * dy) / L2), px = a.x + dx * u - x, py = a.y + dy * u - y;
        distance = std::min(distance, jsHypot2(px, py));
    }
    return distance;
}
uint32_t wingColor(std::span<const uint32_t> C, std::span<const uint32_t> Sp, bool hind, double u, double v) {
    const std::span<const Point> pts = hind ? std::span<const Point>(HIND) : std::span<const Point>(FORE);
    const double                 e = edgeDistance(pts, u, v), r = jsHypot2(u, v), angle = std::atan2(v, u);
    if (e < 1) return C[0];
    if (!hind) {
        if (u + v * -.45 > 14.2 || (r > 12.5 && v < -4)) {
            for (auto spot : SPOTS)
                if (jsHypot2(u - spot.x, v - spot.y) < .9) return C[6];
            return C[0];
        }
    } else {
        if (e < 2 && r > 8) return std::sin(angle * 10) > .5 ? C[5] : C[1];
        const double d = jsHypot2(u - 7.6, v - 9.4);
        if (d < .7) return C[6];
        if (d < 1.4) return C[0];
        if (d < 2.2) return Sp[2];
    }
    const std::array<double, 3> veins =
        hind ? std::array<double, 3>{{.5, 1, 1.45}} : std::array<double, 3>{{-1.2, -.75, -.3}};
    for (double a : veins)
        if (std::abs(angle - a) * r < .45 && r > 3.5) return C[1];
    if (r < 3) return C[2];
    if (r < 6) return C[3];
    if (!hind && e < 2.4 && v < -3) return C[5];
    return C[4];
}
void body(critter::Grid<44, 40>& g, std::span<const uint32_t> C, std::span<const uint32_t> Sp, double w) {
    g.clear();
    for (int side : {-1, 1})
        for (bool hind : {true, false}) {
            const std::span<const Point> pts = hind ? std::span<const Point>(HIND) : std::span<const Point>(FORE);
            g.shade(-20, -15, 20, 16, [&](double x, double y) -> uint32_t {
                if ((x + .001 < 0 ? -1 : 1) != side) return 0;
                const double u = (std::abs(x) - 1) / w, v = y;
                if (u < 0 || !inside(pts, u, v)) return 0;
                auto c = wingColor(C, Sp, hind, u, v);
                if (w < .5 && c != C[0]) c = mix(c, C[1], (.5 - w) * 1.6);
                return c;
            });
        }
    g.ellipse(0, .5, 1.6, 2.6, [&](double nx, double ny) { return nx + ny < -.4 ? C[3] : C[1]; });
    for (int y = 3; y < 10; ++y) {
        g.set(-1, y, y < 6 ? C[2] : C[1]);
        if (y < 8) g.set(0, y, C[1]);
    }
    g.set(-1, -3, C[3]);
    g.set(0, -3, C[2]);
    g.set(-1, -4, C[2]);
    g.set(0, -4, C[1]);
    for (int side : {-1, 1}) {
        const double tip = side * 4 - (side > 0 ? 0 : 1);
        g.line(side > 0 ? 0 : -1, -5, tip, -9.5, C[2]);
        g.set(tip, -9.5, C[6]);
    }
    g.outline(C[0]);
}
void draw(Paint& p, double time, const PixelFxSettings& o) {
    const double t = critter::shift(time, D, o.seed), w = fold(t);
    const auto&  C        = o.palette;
    const auto&  Sp       = o.critter.spots;
    const auto   position = at(t);
    cycle(t, D, 56, o.seed * 7u + 1u, 1, 1.8, [&](double age, double life, Rng& r, int i) {
        const double born = wrap(t - age, D);
        const auto   b    = at(born);
        const double k    = age / life;
        if (!flapping(born) && r.isChance(.45)) return;
        const double x = b.x + r.range(-9, 9) + std::sin(age * 3 + i) * 1.5,
                     y = b.y + r.range(-4, 8) + 7 * age + 3 * age * age, tw = .5 + .5 * std::sin(age * 11 + i * 2.1),
                     a     = std::min(1., k * 8) * (1 - k);
        const auto   color = tw > .7 ? C[6] : r.isChance(.3) ? Sp[2] : C[5];
        p.dot(x, y, color, a * 1.2);
        p.glow(x, y, 6, C[4], .5 * a * tw);
        if (i % 9 == 0 && tw > .85 && k < .5) p.sparkle(x, y, 2, C[6], C[5], .3 * a);
    });
    critter::Grid<44, 40> g(-22, -20);
    body(g, C, Sp, w);
    g.draw(p, 1, position.x, position.y);
    p.glow(position.x, position.y, 38, C[4], .16 + .14 * w);
    p.glow(position.x, position.y, 12, C[5], .12 + .1 * w);
}
}  // namespace
const Descriptor& butterflyDescriptor() {
    static const std::array<Option, 1> options{{{"spots", 1, 64, false, 4, &PixelFxCritterSettings::spots}}};
    static const Descriptor            d{{PixelFxKind::Butterfly, "critters/butterfly", 128, 88, 64, 44, D, true},
                                         7,
                                         [](PixelFxSettings& s) {
                                  s.seed          = 1;
                                  s.palette       = {hex(0x0b0f24), hex(0x16224f), hex(0x1f3f8a), hex(0x2f74c8),
                                                     hex(0x4fb4f0), hex(0xa0e6ff), WHITE};
                                  s.critter.spots = {hex(0x3a1a08), hex(0xc8641a), hex(0xffb43c), hex(0xfff0b0)};
                                         },
                                         draw,
                                         options};
    return d;
}
}  // namespace eve::stylize::pixelfx
