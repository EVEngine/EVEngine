// eve-hot-path
// Native adaptation of pixelfx melee/_lib.js swing/crescent/trace recipes, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#pragma once
#include <type_traits>
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx::melee {
/** @brief Owned source swing geometry; fixed per prepared recipe, no retained painter/actor. */
struct Swing {
    double cx = 0, cy = 0, R = 30, sy = 1, rot = 0, a0 = 0, dir = 1, k = 0, span = 0;
};
/** @brief Own a source circle passing through a point at an authored fraction of the span. */
[[nodiscard]] inline Swing through(double x, double y, double angle, double direction, double radius, double span,
                                   double at = .55) {
    const double a = angle - (direction * PI) / 2;
    return {x - std::cos(a) * radius,
            y - std::sin(a) * radius,
            radius,
            1,
            0,
            a - direction * span * at,
            direction,
            0,
            span};
}
/** @brief Sample screen coordinates at a source plane radius and swept angle. */
[[nodiscard]] inline Point onSwing(const Swing& s, double radius, double u) {
    const double a = s.a0 + s.dir * u, lx = std::cos(a) * radius,
                 ly = std::sin(a) * radius * s.sy - s.k * (s.R - radius), c = std::cos(s.rot), n = std::sin(s.rot);
    return {s.cx + lx * c - ly * n, s.cy + lx * n + ly * c};
}
/** @brief Owned edge position, finite-difference unit tangent and in-plane angle. */
struct Travel {
    double x, y, dx, dy, a;
};
/** @brief Evaluate the original finite-difference tangent, including zero-length denominator semantics. */
[[nodiscard]] inline Travel travel(const Swing& s, double u) {
    const auto   p0 = onSwing(s, s.R, u), p1 = onSwing(s, s.R, u + .01);
    const double norm = jsHypot2(p1.x - p0.x, p1.y - p0.y), d = norm != 0 ? norm : 1;
    return {p0.x, p0.y, (p1.x - p0.x) / d, (p1.y - p0.y) / d, s.a0 + s.dir * u};
}
/** @brief Invert the source ellipse/cone at a screen position without reassociating the quadratic. */
[[nodiscard]] inline Point plane(const Swing& s, double x, double y) {
    const double c = std::cos(s.rot), n = std::sin(s.rot), dx = x - s.cx, dy = y - s.cy, lx = dx * c + dy * n,
                 ly = -dx * n + dy * c;
    if (s.k == 0) return {jsHypot2(lx, ly / s.sy), std::atan2(ly / s.sy, lx)};
    const double m = ly + s.k * s.R, A = s.sy * s.sy - s.k * s.k, B = 2 * m * s.k, C = lx * lx * s.sy * s.sy + m * m,
                 r = (-B + std::sqrt(B * B + 4 * A * C)) / (2 * A);
    return {r, std::atan2((m - s.k * r) / s.sy, lx)};
}
/** @brief Raster a source swing band through a synchronous trusted typed shader; no callback retained.
 * @cost Work scales with the clipped swing bounds per explicit draw; no geometry allocation. */
template <class Shader>
void smear(Paint& p, const Swing& s, double u0, double u1, double width, Shader shade, int half = 0, double pad = 1) {
    if (u1 - u0 < .005) return;
    const double c = std::cos(s.rot), n = std::sin(s.rot), A = s.R + pad + 1, B = A * s.sy + s.k * (width + 2),
                 hx = jsHypot2(A * c, B * n) + 1, hy = jsHypot2(A * n, B * c) + 1, inner = s.R - width - 1;
    const double over = std::abs(n) * A + s.k * (width + 2) + 2, y0 = half > 0 ? s.cy - over : s.cy - hy,
                 y1 = half < 0 ? s.cy + over : s.cy + hy;
    p.shader(s.cx - hx, y0, s.cx + hx, y1, [&](double x, double y, ShadeOutput& out) {
        const auto   q = plane(s, x, y);
        const double r = q.x, a = q.y;
        if (r > s.R + pad || r < inner) return;
        if (half && (std::sin(a) < 0 ? -1 : 1) != half) return;
        double u = std::fmod(s.dir * (a - s.a0), TAU);
        if (u < 0) u += TAU;
        while (u < u0) u += TAU;
        if (u > u1) return;
        shade(u, r, out, a);
    });
}
/** @brief Sample a known compiled value or function synchronously, without type-erased dispatch. */
template <class T>
[[nodiscard]] auto sample(const T& value, double u) {
    if constexpr (std::is_invocable_v<T, double>)
        return value(u);
    else
        return value;
}
/** @brief Draw source dotted arc traces; compiled color/gain callables are borrowed synchronously.
 * @cost Linear in traced arc length divided by authored step per draw; no retained callbacks or allocation. */
template <class Color, class Gain>
void trace(Paint& p, const Swing& s, double radius, double u0, double u1, const Color& color, const Gain& gain,
           int half = 0, double step = .6) {
    const double du = step / std::max(4., radius);
    for (double u = u0; u <= u1; u += du) {
        const double a = s.a0 + s.dir * u;
        if (half && (std::sin(a) < 0 ? -1 : 1) != half) continue;
        const auto   point = onSwing(s, radius, u);
        const double k     = sample(gain, u);
        if (k > 0) p.dot(point.x, point.y, sample(color, u), k);
    }
}
/** @brief Draw lights riding a source swing, with a synchronous trusted gain function/value.
 * @cost Linear in swept angle divided by authored step per draw, multiplied by glow footprint. */
template <class Gain>
void glowAlong(Paint& p, const Swing& s, double radius, double u0, double u1, double glowRadius, uint32_t color,
               const Gain& gain, int half = 0, double step = .35) {
    for (double u = u0; u <= u1; u += step) {
        const double a = s.a0 + s.dir * u;
        if (half && (std::sin(a) < 0 ? -1 : 1) != half) continue;
        const auto   point = onSwing(s, radius, u);
        const double k     = sample(gain, u);
        if (k > .004) p.glow(point.x, point.y, glowRadius, color, k);
    }
}
/** @brief Owned source slash geometry/timing; its material ramp is owned by the prepared kernel. */
struct Slash {
    Swing  swing;
    double width = 0, t0 = 0, sweep = 0, hold = 0, trail = 0, hot = 0;
    bool   lens = false;
};
/** @brief Draw the original expanding/closing crescent and dotted trace from trusted prepared geometry.
 * @cost Clipped band raster plus arc lights per explicit draw, reusing geometry and borrowed immutable ramp. */
inline void slash(Paint& p, const Slash& slash, double t, std::span<const uint32_t> ramp, int half = 0) {
    const auto&  s = slash.swing;
    const double u = t - slash.t0;
    if (u < 0) return;
    const int    n    = int(ramp.size());
    const double life = slash.sweep + slash.hold + slash.trail, hot = slash.hot,
                 head = ease::outExpo(clamp(u / slash.sweep)), tail = smooth(slash.sweep + slash.hold, life, u),
                 age = clamp(u / life), U = s.span * head, T = s.span * tail, W = slash.width * (1 - tail * .55);
    if (tail < 1 && U - T > .01) {
        smear(
            p, s, T, U, W,
            [&](double uu, double r, ShadeOutput& out, double) {
                const double v = (uu - T) / (U - T),
                             w = W * std::pow(std::sin(PI * std::pow(v, slash.lens ? 1. : 2.2)), slash.lens ? .75 : .6);
                if (w < .35) return;
                const double q = (s.R - r) / w;
                if (q < 0 || q > 1) return;
                const double ci = (q < .22 && age < .5) || q < hot * .7
                                      ? 0
                                      : jsRound(q * (n - 1) * .75 + age * 2.4 - v * .8 + .4 - hot * 3);
                out.c           = ramp[size_t(n - 1 - std::clamp(int(ci), 0, n - 1))];
                out.a           = std::min(1., v / (slash.lens ? .1 : .4));
            },
            half);
        glowAlong(
            p, s, s.R - W * .4, T, U, 5 + W, ramp[size_t(n - 3)],
            [&](double uu) { return (.22 + hot * .3) * (1 - age) * ((uu - T) / (U - T)); }, half, .3);
    }
    const double th = .7 * (1 - clamp((u - slash.sweep - slash.hold) / (slash.trail + .2)));
    if (tail > .02 && th > 0)
        trace(p, s, s.R - .5, 0, T, ramp[2], [&](double uu) { return th * (.4 + .6 * uu / s.span); }, half);
}
/** @brief Evaluate the slash's final edge tangent using the original inward radius offset. */
[[nodiscard]] inline Travel slashEnd(const Slash& s) {
    auto geometry = s.swing;
    geometry.R -= s.width * .3;
    return travel(geometry, geometry.span);
}
/** @brief Source stone material shades derived from one raw packed body color. */
struct Stone {
    uint32_t light, body, shade, dust, dark;
};
/** @brief Derive the original stone ramp synchronously; preserve raw body alpha bits and zero. */
[[nodiscard]] inline Stone stone(uint32_t c) {
    return {mix(scale(c, 1.35), WHITE, .08), c, scale(c, .64), mix(scale(c, 1.2), rgb(150, 140, 150), .3),
            scale(c, .42)};
}
/** @brief Emit source edge shavings with ordered draws from the caller-owned RNG.
 * @note Borrowed geometry/materials/RNG are used synchronously; particles own all emitted values.
 * @cost Linear in authored emission count per simulation event. */
inline void shavings(Particles& ps, Rng& rng, const Slash& slash, int count, std::span<const uint32_t> colors,
                     double from = .3) {
    const auto& s = slash.swing;
    for (int i = 0; i < count; ++i) {
        const double u  = s.span * rng.range(from, 1);
        const auto   p  = onSwing(s, s.R, u);
        const double ox = p.x - s.cx, oy = p.y - s.cy, norm = jsHypot2(ox, oy), d = norm != 0 ? norm : 1,
                     speed = rng.range(30, 100), life = rng.range(.25, .55);
        const auto   color = colors[size_t(rng.integer(0, int(colors.size()) - 1))];
        ps.spawn(rng, ParticleKind::Mote, p.x, p.y, (ox / d) * speed, std::min(25., (oy / d) * speed) - 10, life, color,
                 .6);
    }
}
/** @brief Draw a source flying arc sliver through the synchronous painter borrow.
 * @cost Linear in arc length and authored ribbon width per explicit draw, with no allocation. */
inline void sliver(Paint& p, double x, double y, double angle, double radius, double arc, uint32_t color, double gain) {
    const int count = int(std::ceil(radius * arc * 2.2));
    for (int j = 0; j <= count; ++j) {
        const double u = double(j) / count - .5, a = angle + u * arc, width = 1.4 * (1 - std::abs(u) * 2);
        for (double off = -width * .5; off <= width * .5 + .01; off += .5)
            p.dot(x + std::cos(a) * (radius + off), y + std::sin(a) * (radius + off), color, gain);
    }
}
/** @brief Owned source comic-star ray geometry, independent of painter lifetime. */
struct Spike {
    double angle, length, width;
};
/** @brief Draw the original three-pass comic impact star synchronously.
 * @cost Linear in authored ray area across three raster passes per explicit draw; no allocation. */
inline void star(Paint& p, double x, double y, std::span<const Spike> spikes, double sc, double th, uint32_t core,
                 uint32_t body, uint32_t inner, uint32_t rim) {
    for (int pass : {0, 1, 2})
        for (const auto& s : spikes) {
            const double c = std::cos(s.angle), sn = std::sin(s.angle), L = s.length * sc;
            for (double d = 0; d <= L; d += .5) {
                const double hw0 = s.width * th * (1 - d / L) * std::min(1., .5 + d / 3), hw = pass == 0   ? hw0 + .8
                                                                                               : pass == 1 ? hw0
                                                                                                           : hw0 * .45;
                if (pass == 2 && d > L * .55) break;
                for (double o = -hw; o <= hw; o += .5)
                    p.px(x + c * d - sn * o, y + sn * d + c * o,
                         pass == 0   ? rim
                         : pass == 1 ? (d < L * .35 ? inner : body)
                                     : core);
            }
        }
    const double r = 2.4 * sc * std::sqrt(th);
    p.circle(x, y, r + .8, inner);
    p.circle(x, y, r, core);
}
/** @brief Owned source crack polyline and authored fork start fraction. */
struct Crack {
    std::vector<Point> points;
    double             at;
    bool               fork;
};
/** @brief Construct source floor cracks once from the caller-owned RNG.
 * @cost Linear in authored crack reach and fork count per program preparation; amortized over its draws. */
[[nodiscard]] inline std::vector<Crack> cracks(Rng& rng, double x0, int count, double reach, double depth = 7,
                                               double bias = .3) {
    std::vector<Crack> out;
    for (int i = 0; i < count; ++i) {
        const double side = rng.isChance(.5 + bias * .5) ? 1. : -1., L = reach * rng.range(.45, 1) * (i < 2 ? 1 : .7),
                     start = x0 + side * rng.range(0, 1.5);
        std::vector<Point> points{{start, 0}};
        double             x = start, y = 0;
        while (std::abs(x - x0) < L && y < depth) {
            x += side * rng.range(1.5, 3.5);
            y = std::min(depth, std::max(0., y + rng.range(-.6, 1.6) * (std::abs(x - x0) > 3 ? 1 : .3)));
            points.push_back({std::floor(x + .5), std::floor(y + .5)});
            if (rng.isChance(.18) && points.size() > 3 && y < depth - 2) {
                std::vector<Point> fork{{std::floor(x + .5), std::floor(y + .5)}};
                double             fx = x, fy = y;
                for (int j = rng.integer(1, 3); j > 0; --j) {
                    fx += side * rng.range(1, 3);
                    fy += rng.range(.8, 2);
                    fork.push_back({std::floor(fx + .5), std::floor(std::min(depth + 1, fy) + .5)});
                }
                out.push_back({std::move(fork), std::abs(x - x0) / reach, true});
            }
        }
        out.push_back({std::move(points), 0, false});
    }
    return out;
}
/** @brief Visit a borrowed source polyline prefix with a trusted compiled synchronous callback.
 * @cost Linear in visited segments per explicit draw; no allocation or retained callback. */
template <class Fn>
void along(std::span<const Point> points, double length, Fn fn) {
    double d = 0;
    for (size_t i = 1; i < points.size() && d < length; ++i) {
        const auto   a = points[i - 1], b = points[i];
        const double l = jsHypot2(b.x - a.x, b.y - a.y), f = std::min(1., (length - d) / l);
        fn(a.x, a.y, a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, d);
        d += l;
    }
}
/** @brief Shade a borrowed source tumbling rock with its owned stone ramp.
 * @cost Linear in clipped rock bounding area per explicit draw; no allocation or retained data. */
inline void rock(Paint& p, const Debris& b, const Stone& st) {
    const double r = .6 + b.size * .75, fade = std::min(1., b.life / .5), lx = std::cos(b.angle) * r * .4,
                 ly = std::sin(b.angle) * r * .4;
    p.shader(b.x - r - 1, b.y - r - 1, b.x + r + 1, b.y + r + 1, [&](double x, double y, ShadeOutput& out) {
        const double dx = x - b.x, dy = y - b.y, d = jsHypot2(dx * 1.15, dy);
        if (d > r + .9) return;
        const double lit = dx * lx + dy * ly;
        out.c            = d > r ? hex(0x1a1422) : lit < -.4 ? st.light : lit > .6 ? st.shade : st.body;
        out.a            = fade;
    });
}
}  // namespace eve::stylize::pixelfx::melee
