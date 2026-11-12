// eve-hot-path
// Native adaptation of pixelfx critters/eyeball.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Critter.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 3.2, RADIUS = 13;
struct Look {
    double time, x, y;
};
constexpr std::array<Look, 6> LOOKS{
    {{0, 0, 0}, {.55, 4.2, -1.2}, {1.15, -4.4, .8}, {1.95, 1.2, 3.4}, {2.5, -2, -3.4}, {3, 0, 0}}};
Point look(double t) {
    size_t i = LOOKS.size() - 1;
    while (i > 0 && t < LOOKS[i].time) --i;
    const auto   a = LOOKS[i], b = LOOKS[(i + LOOKS.size() - 1) % LOOKS.size()];
    const double u = ease::outBack(clamp((t - a.time) / .08), 2.2);
    return {lerp(b.x, a.x, u), lerp(b.y, a.y, u)};
}
double lid(double t) { return t > 1.74 && t < 1.92 ? std::sin(PI * ((t - 1.74) / .18)) : 0; }
Point  at(double t, double hover) {
    const double a = (t / D) * TAU;
    return {1.5 * std::sin(a), -hover - 3 * std::sin(2 * a + .4)};
}
using Veins = std::array<std::array<Point, 5>, 7>;
Veins prepareVeins(uint32_t seed) {
    Veins result;
    Rng   r(hash({seed, 77}));
    for (size_t i = 0; i < result.size(); ++i) {
        double a = (double(i) / 7) * TAU + r.range(-.3, .3), d = RADIUS - .5;
        result[i][0] = {std::cos(a) * d, std::sin(a) * d};
        for (size_t k = 1; k < 5; ++k) {
            a += r.range(-.35, .35);
            d -= r.range(1.2, 2.1);
            result[i][k] = {std::cos(a) * d, std::sin(a) * d};
        }
    }
    return result;
}
struct Eyeball final : AnalyticKernel {
    const Veins veins;
    explicit Eyeball(uint32_t seed) : veins(prepareVeins(seed)) {}
    void eye(critter::Grid<36, 38>& g, std::span<const uint32_t> C, std::span<const uint32_t> Fl,
             std::span<const uint32_t> I, double t) const {
        g.clear();
        const auto   target = look(t);
        const double lx = target.x, ly = target.y, L = lid(t);
        const auto   lit = [](double x, double y, double k) {
            return jsHypot2(-4 + (x + 4) / k, -4.5 + (y + 4.5) / k) <= RADIUS;
        };
        g.shade(-RADIUS, -RADIUS, RADIUS, RADIUS, [&](double x, double y) -> uint32_t {
            if (jsHypot2(x, y) > RADIUS) return 0;
            if (!(jsHypot2(x + 2, y + 2.2) <= RADIUS)) return C[2];
            return lit(x, y, .5) ? C[5] : lit(x, y, .8) ? C[4] : C[3];
        });
        for (const auto& pts : veins) {
            std::array<Point, 5> shifted;
            for (size_t i = 0; i < pts.size(); ++i) {
                auto         q = pts[i];
                const double d = jsHypot2(q.x, q.y) / RADIUS;
                shifted[i]     = {q.x + lx * .35 * (1 - d), q.y + ly * .35 * (1 - d)};
            }
            for (size_t k = 1; k < shifted.size(); ++k)
                g.line(shifted[k - 1].x, shifted[k - 1].y, shifted[k].x, shifted[k].y,
                       k < 2 ? hex(0xc42a3a) : mix(hex(0xc42a3a), C[3], .45));
        }
        const double fx = 1 - .32 * std::pow(std::abs(lx) / RADIUS, 1.4) * 3,
                     fy = 1 - .32 * std::pow(std::abs(ly) / RADIUS, 1.4) * 3, ir = 6.4,
                     pr = 2 + .4 * std::sin((t / D) * TAU * 2);
        g.shade(lx - ir - 1, ly - ir - 1, lx + ir + 1, ly + ir + 1, [&](double x, double y) -> uint32_t {
            const double dx = (x - lx) / (ir * fx), dy = (y - ly) / (ir * fy), d = jsHypot2(dx, dy);
            if (d > 1 || jsHypot2(x, y) > RADIUS - .5) return 0;
            if (jsHypot2((x - lx) / fx, (y - ly) / fy) < pr) return C[0];
            if (d > .86) return I[0];
            const double angle  = std::atan2(dy, dx);
            const bool   streak = std::sin(angle * 9 + d * 4) > .5;
            return d < .5 ? (streak ? I[4] : I[3]) : d < .7 ? (streak ? I[3] : I[2]) : streak ? I[2] : I[1];
        });
        if (L < .5) {
            g.set(-4, -5, C[5]);
            g.set(-3, -5, C[5]);
            g.set(-4, -4, C[5]);
            g.set(1, -2, C[5]);
        }
        const double up = lerp(-RADIUS + 3, RADIUS * .42, L), down = lerp(RADIUS - 2.2, RADIUS * .42, L);
        const auto   fleshLit = [](double x, double y, double k) {
            return jsHypot2(-4 + (x + 4) / k, -5 + (y + 5) / k) <= RADIUS + .6;
        };
        g.shade(-RADIUS - 1, -RADIUS - 1, RADIUS + 1, RADIUS + 1, [&](double x, double y) -> uint32_t {
            if (jsHypot2(x, y) > RADIUS + .6) return 0;
            const double upper = up + 1.8 * std::pow(x / RADIUS, 2), lower = down - 1.4 * std::pow(x / RADIUS, 2);
            const auto   tone = !(jsHypot2(x + 2, y + 2.2) <= RADIUS + .6) ? Fl[1]
                                : fleshLit(x, y, .55)                        ? Fl[4]
                                : fleshLit(x, y, .85)                        ? Fl[3]
                                                                             : Fl[2];
            if (y < upper) return y > upper - 1.2 ? Fl[0] : tone;
            if (y > lower) return y < lower + 1.1 ? Fl[0] : tone == Fl[4] ? Fl[3] : tone;
            return 0;
        });
        g.outline(C[0]);
    }
    void drawBack(Paint& p, double time, const PixelFxSettings& o) const override {
        const double t   = critter::shift(time, D, o.seed);
        const auto   pos = at(t, o.critter.hover);
        const auto&  I   = o.critter.iris;
        critter::floorShadow(p, pos.x, 0, 10, 1.5, hex(0x0d0a12), .5);
        for (int x = -26; x <= 26; x += 6) p.glow(pos.x + x, -1, 18, I[2], .15 * (1 - std::abs(double(x)) / 34));
    }
    void draw(Paint& p, double time, const PixelFxSettings& o) const override {
        const double          t   = critter::shift(time, D, o.seed);
        const auto&           C   = o.palette;
        const auto&           Fl  = o.critter.flesh;
        const auto&           I   = o.critter.iris;
        const auto            pos = at(t, o.critter.hover);
        critter::Grid<40, 64> nerve(-20, -70);
        for (double s = 0; s <= 20; s += .5) {
            const auto   b  = at(t - s * .035, o.critter.hover);
            const double sw = 2.6 * std::pow(s / 20, 1.3) * std::sin((t / D) * TAU * 2 - s * .16),
                         r = lerp(2, .6, s / 20), cx = b.x + 1 + sw, cy = b.y + RADIUS - 2 + s;
            nerve.shade(cx - r - 1, cy - 1, cx + r + 1, cy + 1, [&](double x, double y) -> uint32_t {
                const double v = (x - cx) / std::max(.6, r);
                if (std::abs(v) > 1 || std::abs(y - cy) > .5) return 0;
                return v < -.35 ? Fl[4] : v < .4 ? Fl[3] : Fl[2];
            });
        }
        nerve.outline(Fl[0]);
        nerve.draw(p);
        critter::Grid<36, 38> g(-18, -18);
        eye(g, C, Fl, I, t);
        g.draw(p, 1, pos.x, pos.y);
        const auto target = look(t);
        double     snap   = 0;
        for (auto q : LOOKS)
            snap = std::max(snap, std::max(0., 1 - std::abs(wrap(t - q.time + D / 2, D) - D / 2) / .2));
        const double open = 1 - lid(t);
        p.glow(pos.x + target.x, pos.y + target.y, 15, I[3], (.5 + .3 * snap) * open);
        p.glow(pos.x + target.x, pos.y + target.y, 5, I[4], .4 * open);
        p.glow(pos.x, pos.y, 50, I[1], .3);
        for (int i = 0; i < 8; ++i) {
            const int            speed = (i % 2 ? 1 : -1) * (i % 3 ? 1 : 2);
            const double         a0 = (i * TAU) / 8, rr = 19 + (i % 3) * 2;
            std::optional<Point> previous;
            for (int k = 5; k >= 0; --k) {
                const double a = (t / D) * TAU * speed + a0 - speed * k * .035, z = std::sin(a),
                             x = pos.x + std::cos(a) * rr, y = pos.y + z * 5 - std::cos(a) * 3;
                if (z < 0 && jsHypot2(x - pos.x, y - pos.y) < RADIUS + 1) {
                    previous.reset();
                    continue;
                }
                if (previous && k > 0) p.line(previous->x, previous->y, x, y, I[2], .4 + (5 - k) * .12);
                if (k == 0) {
                    p.px(x, y, z > 0 ? I[4] : I[3]);
                    p.glow(x, y, 5, I[3], .45);
                }
                previous = Point{x, y};
            }
        }
    }
};
}  // namespace
const Descriptor& eyeballDescriptor() {
    static const std::array<Option, 3> options{{{"flesh", 1, 64, false, 5, &PixelFxCritterSettings::flesh},
                                                {"iris", 1, 64, false, 5, &PixelFxCritterSettings::iris},
                                                {"hover", 0, 256, false, 0, &PixelFxCritterSettings::hover}}};
    static const Descriptor            d{
        {PixelFxKind::Eyeball, "critters/eyeball", 96, 108, 48, 100, D, true},
        6,
        [](PixelFxSettings& s) {
            s.seed          = 1;
            s.critter.hover = 48;
            s.palette       = {hex(0x2a1420), hex(0x6a3040), hex(0xb07078), hex(0xdcbcb4), hex(0xf4e9de), WHITE};
            s.critter.flesh = {hex(0x1e0c16), hex(0x4a1a2c), hex(0x7c2c40), hex(0xb04a58), hex(0xe07a80)};
            s.critter.iris  = {hex(0x0c2a14), hex(0x1f6a2a), hex(0x3fae3a), hex(0x9be05a), hex(0xe8ffb0)};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
            return std::make_unique<Eyeball>(s.seed);
        }};
    return d;
}
}  // namespace eve::stylize::pixelfx
