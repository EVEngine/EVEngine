// eve-hot-path
// Native adaptation of pixelfx lightning/_lib.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#pragma once
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx::lightning {
inline const std::array<uint32_t, 8> VOLT{
    {hex(0x120a33), hex(0x2a1b78), hex(0x4630c0), hex(0x5a62f0), hex(0x58a6ff), hex(0x9ed8ff), hex(0xe4f6ff), WHITE}};
inline const std::array<uint32_t, 4> PLASMA{{WHITE, hex(0xc8f8ff), hex(0x7ff3ff), hex(0x1ab8e6)}};
struct Path {
    std::vector<Point> pts;
    double             w;
    int                depth;
};
/** @brief Generate owning forked paths from the supplied RNG; no global state or borrowed points retained.
 * @cost Allocates midpoint/branch arrays per arc, proportional to path length and two branch levels. */
inline std::vector<Path> zigzag(Rng& R, double x0, double y0, double x1, double y1, double jag = .32,
                                double forks = .35, double w = 1, int depth = 0) {
    const double       L = jsHypot2(x1 - x0, y1 - y0);
    std::vector<Point> pts{{x0, y0}, {x1, y1}};
    double             off = L * jag;
    while (double(pts.size()) < 2 + L / 3) {
        std::vector<Point> next{pts.front()};
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const auto   a = pts[i], b = pts[i + 1];
            const double raw = jsHypot2(b.x - a.x, b.y - a.y), l = raw == 0 ? 1 : raw, o = R.range(-off, off);
            next.push_back({(a.x + b.x) / 2 + (-(b.y - a.y) / l) * o, (a.y + b.y) / 2 + ((b.x - a.x) / l) * o});
            next.push_back(b);
        }
        pts = std::move(next);
        off *= .52;
    }
    std::vector<Path> out{{pts, w, depth}};
    if (depth < 2)
        for (size_t i = 2; i + 3 < pts.size(); ++i)
            if (R.isChance(forks / (1 + depth) / std::max(4., double(pts.size()) / 6))) {
                const auto   a = pts[i], b = pts[std::min(pts.size() - 1, i + 3)];
                const double angle = std::atan2(b.y - a.y, b.x - a.x) + R.signedRange(.35, .9),
                             len   = (L - (double(i) / double(pts.size())) * L) * R.range(.25, .55);
                auto children = zigzag(R, a.x, a.y, a.x + std::cos(angle) * len, a.y + std::sin(angle) * len, jag * 1.1,
                                       forks, w * .55, depth + 1);
                out.insert(out.end(), std::make_move_iterator(children.begin()),
                           std::make_move_iterator(children.end()));
            }
    return out;
}

/** @brief Own one call's pixel levels and first-hit order; no process-global scratch or target retained.
 * @cost Allocates only the clipped path bounding grid per inking call, released before return. */
struct Ink {
    int                  x0 = 512, y0 = 512, x1 = -512, y1 = -512, w = 0;
    std::vector<uint8_t> levels;
    std::vector<size_t>  hits;
    explicit Ink(const std::vector<Path>& paths, double margin) {
        for (const auto& p : paths)
            for (const auto a : p.pts) {
                x0 = std::min(x0, int(std::floor(clamp(a.x - margin, -512, 511))));
                y0 = std::min(y0, int(std::floor(clamp(a.y - margin, -512, 511))));
                x1 = std::max(x1, int(std::ceil(clamp(a.x + margin, -512, 511))));
                y1 = std::max(y1, int(std::ceil(clamp(a.y + margin, -512, 511))));
            }
        if (x1 >= x0 && y1 >= y0) {
            w = x1 - x0 + 1;
            levels.resize(size_t(w) * size_t(y1 - y0 + 1), 0);
        }
    }
    void mark(double x, double y, uint8_t level) {
        const int X = int(std::floor(x)), Y = int(std::floor(y));
        if (X < x0 || Y < y0 || X > x1 || Y > y1 || X < -512 || Y < -512 || X >= 512 || Y >= 512 || w == 0) return;
        const size_t i = size_t(Y - y0) * size_t(w) + size_t(X - x0);
        if (levels[i] == 0) hits.push_back(i);
        levels[i] = std::max(levels[i], level);
    }
    void stamp(double x, double y, double r, uint8_t level) {
        if (r <= .5) {
            mark(jsRound(x), jsRound(y), level);
            return;
        }
        for (double py = std::floor(y - r); py <= std::ceil(y + r); ++py) {
            const double dy = py + .5 - y;
            for (double px = std::floor(x - r); px <= std::ceil(x + r); ++px) {
                const double dx = px + .5 - x;
                if (dx * dx + dy * dy <= r * r) mark(px, py, level);
            }
        }
    }
    void flush(Paint& p, const std::array<uint32_t, 4>& cols, const std::array<double, 4>& alpha) {
        for (size_t i : hits) {
            const auto   v = size_t(levels[i] - 1);
            const double x = double(i % size_t(w)) + x0, y = double(i / size_t(w)) + y0, a = alpha[v];
            if (a >= 1)
                p.px(x, y, cols[v]);
            else if (a > 0)
                p.dot(x, y, cols[v], a);
        }
    }
};
/** @brief Borrow a point callback only during this synchronous half-pixel path traversal. */
template <class F>
void walk(std::span<const Point> pts, F fn) {
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const auto   a = pts[i], b = pts[i + 1];
        const double l = std::max(1., jsHypot2(b.x - a.x, b.y - a.y));
        for (double d = 0; d <= l; d += .5) fn(a.x + ((b.x - a.x) * d) / l, a.y + ((b.y - a.y) * d) / l);
    }
}
struct Stroke {
    bool                  hero  = false;
    double                light = .5, alpha = 1;
    std::optional<double> upTo, core, minY;
};
/** @brief Ink borrowed paths with local priority pixels; borrowed target/ramp never escape.
 * @cost Linear half-pixel path traversal plus bounded local scratch, per draw. */
inline void stroke(Paint& p, const std::vector<Path>& paths, double k, std::span<const uint32_t> C, double s,
                   const Stroke& o) {
    if (k <= .02) return;
    Ink ink(paths, std::max({3., 2.1 * s, o.core.value_or(0.)}) + 2);
    for (const auto& q : paths) {
        const bool trunk = q.w >= 1;
        if (!trunk && k < .5) continue;
        const double rh = (trunk ? (k > .6 ? 2.1 : 1.6) : 1.1) * s, rb = (trunk ? (k > .6 ? 1.3 : .8) : .5) * s;
        auto         pts = std::span<const Point>(q.pts);
        if (o.upTo && trunk)
            pts = pts.first(std::min(pts.size(), size_t(std::max(2., std::ceil(double(pts.size()) * *o.upTo)))));
        walk(pts, [&](double x, double y) {
            ink.stamp(x, y, rh, 1);
            ink.stamp(x, y, rb, trunk ? 3 : 2);
            if (trunk && k > .3) {
                if (o.core && *o.core != 0)
                    ink.stamp(x, y, *o.core, 4);
                else
                    ink.mark(x, y, 4);
            }
        });
        for (size_t i = 0; i + 1 < pts.size(); i += 3)
            if (!o.minY || pts[i].y > *o.minY)
                p.glow(pts[i].x, pts[i].y, (trunk ? 14 : 7) * s, C[4], o.light * k * q.w);
    }
    ink.flush(
        p,
        o.hero ? std::array<uint32_t, 4>{{C[3], C[6], C[7], C[7]}} : std::array<uint32_t, 4>{{C[2], C[4], C[5], C[7]}},
        {{o.alpha, o.alpha, o.alpha, o.alpha}});
}
/** @brief Ink short borrowed arcs with the source first-hit ordering; no shared mutable scratch. */
inline void crackle(Paint& p, const std::vector<Path>& paths, double k, std::span<const uint32_t> C, double glowK = .5,
                    double a = 1) {
    if (k <= .02) return;
    Ink ink(paths, 4);
    for (const auto& q : paths) {
        const bool thick = q.w >= 1;
        walk(q.pts, [&](double x, double y) {
            if (k > .45 && thick)
                for (auto d : CROSS) ink.mark(x + d.x * 1.5, y + d.y * 1.5, 1);
            if (k > .25 && (thick || q.w > .4)) {
                ink.mark(x + 1, y, 2);
                ink.mark(x, y + 1, 2);
            }
            ink.mark(x, y, thick && k > .3 ? 4 : 3);
        });
        for (size_t i = 0; i + 1 < q.pts.size(); i += 3)
            p.glow(q.pts[i].x, q.pts[i].y, (thick ? 9 : 5) * (.5 + k * .5), C[4], glowK * k * q.w);
    }
    ink.flush(p, {{C[3], C[5], C[5], C[7]}}, {{std::min(1., (k - .45) * 1.6) * a, a, a, a}});
}
/** @brief Generate an owning bowed arc; all RNG consumption belongs to the supplied stream.
 * @cost Builds two owning branch trees and joins their trunks once per frame arc. */
inline std::vector<Path> bowed(Rng& R, double x0, double y0, double x1, double y1, double bow, double jag = .22,
                               double forks = .4) {
    const double raw = jsHypot2(x1 - x0, y1 - y0), L = raw == 0 ? 1 : raw, nx = -(y1 - y0) / L, ny = (x1 - x0) / L,
                 mx = (x0 + x1) / 2 + nx * bow + R.range(-2, 2), my = (y0 + y1) / 2 + ny * bow;
    auto         a = zigzag(R, x0, y0, mx, my, jag, forks), b = zigzag(R, mx, my, x1, y1, jag, forks);
    a[0].pts.insert(a[0].pts.end(), b[0].pts.begin() + 1, b[0].pts.end());
    a.insert(a.end(), std::make_move_iterator(b.begin() + 1), std::make_move_iterator(b.end()));
    return a;
}
struct Flight {
    double x, y, vx, vy;
};
/** @brief Replay a finite age on a private trajectory with the source fractional final step. */
inline Flight flight(double x, double y, double vx, double vy, double age, double floor) {
    constexpr double dt = 1. / 120;
    for (double s = 0; s < age; s += dt) {
        const double h = std::min(dt, age - s);
        vy += GRAVITY * .45 * h;
        vx -= vx * 2.2 * h;
        vy -= vy * 2.2 * h;
        const double ny = y + vy * h;
        x += vx * h;
        if (vy > 0 && y <= floor && ny >= floor) {
            vy = -vy * .45;
            vx *= .7;
            y = floor - .25;
        } else
            y = ny;
    }
    return {x, y, vx, vy};
}
/** @brief Draw a private trajectory sample using a borrowed head-to-tail spark palette. */
inline void spark(Paint& p, const Flight& v, double k, std::span<const uint32_t> S) {
    const double raw = jsHypot2(v.vx, v.vy), sp = raw == 0 ? 1 : raw, ux = v.vx / sp, uy = v.vy / sp,
                 len  = std::min(9., 1 + sp * .022);
    const auto   head = k > .6 ? S[0] : k > .3 ? S[1] : S[2];
    p.line(v.x - ux * len, v.y - uy * len, v.x - ux * len * .4, v.y - uy * len * .4, k > .5 ? head : S.back());
    p.line(v.x - ux * len * .45, v.y - uy * len * .45, v.x, v.y, head);
    p.glow(v.x, v.y, 3, head, .45 * k);
}
/** @brief Read the source fire-ramp heat color, or zero after cooling. */
inline uint32_t heat(double T) {
    return T > .06 ? FIRE_RAMP[size_t(std::min(double(FIRE_RAMP.size() - 1),
                                               std::floor((1 - std::min(1., T)) * double(FIRE_RAMP.size() - 1))))]
                   : 0;
}
/** @brief Draw a finite-age smoke puff without retained particles or target ownership. */
inline void puff(Paint& p, double x, double y, double size, double u, uint32_t c) {
    const double k = 1 - u, r = size * (.65 + std::min(1., u * 2.5) * .45 + u * .4),
                 a = std::min(1., u * 12) * std::min(1., k * 2.4) * .58;
    if (a <= .01) return;
    const auto col = mix(
        c, SMOKE_RAMP[size_t(std::min(double(SMOKE_RAMP.size() - 1), std::floor(u * double(SMOKE_RAMP.size()))))], u);
    p.disc(x, y, r, col, a * .45);
    p.disc(x + r * .05, y + r * .06, r * .74, col, a * .62);
    if (u < .5) p.disc(x - r * .22, y - r * .3, r * .5, mix(col, WHITE, .12), a * .4 * (1 - u * 2));
}
/** @brief Draw source heat and cooling soot in local space; target and ramp are borrowed synchronously. */
inline void scorch(Paint& p, double cx, double cy, double rx, double T0, double fade) {
    if (fade <= 0) return;
    const double ry = std::max(2.5, rx * .12);
    for (double y = std::floor(cy); y <= cy + ry; ++y)
        for (double x = std::floor(cx - rx); x <= std::ceil(cx + rx); ++x) {
            const double dx = (x + .5 - cx) / rx, dy = (y + .5 - cy) / ry, d = std::sqrt(dx * dx + dy * dy);
            if (d > 1) continue;
            const double n = rand({jsUint(x), jsUint(y), 41}), T = T0 * (1.25 - d) - n * .35;
            if (T > .06) {
                const auto c = heat(T);
                p.dot(x, y, c, fade);
                if ((jsUint(x) & 3u) == 0) p.glow(x + .5, y, 6, c, .18 * T * fade);
            } else
                p.dot(x, y, d < .5 ? hex(0x110c16) : hex(0x1c1522), (d < .95 - n * .3 ? 1 : 1 - d) * fade);
        }
}
}  // namespace eve::stylize::pixelfx::lightning
