// Native adaptation of pixelfx water/_lib.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Water.h"
namespace eve::stylize::pixelfx::water {
struct Drop {
    double x = 0, z = 0, h = 0, vx = 0, vz = 0, vh = 0, s = 1, life = 4, delay = 0;
    bool   small = false;
    double max   = 4;
};
/** @brief Owning water trajectories; simulation time is supplied by the kernel, with no retained callbacks. */
struct Drops {
    double            sy = .22, g = 820, drag = .35;
    std::vector<Drop> list;
    /** @brief Append an owning trajectory. @cost Amortized vector growth per emitted drop. */
    void add(Drop d) {
        d.max = d.life;
        list.push_back(d);
    }
    /** @brief Update reverse insertion order and borrow the landing callback synchronously.
     * @note The callback receives a local snapshot, may append drops, and must not otherwise mutate/reenter this
     * updater. Appended drops start updating on the next tick. No snapshot/callback is retained.
     * @cost Linear in live drops plus vector erasure shifts per fixed tick. */
    template <class F>
    void update(double dt, F onLand) {
        const double k = drag * dt;
        for (size_t remaining = list.size(); remaining > 0; --remaining) {
            const size_t i = remaining - 1;
            auto         d = list[i];
            if (d.delay > 0) {
                list[i].delay -= dt;
                continue;
            }
            if ((d.life -= dt) <= 0) {
                list.erase(list.begin() + std::ptrdiff_t(i));
                continue;
            }
            d.vh -= g * dt;
            d.vx -= d.vx * k;
            d.vz -= d.vz * k;
            d.vh -= d.vh * k * .5;
            d.x += d.vx * dt;
            d.z += d.vz * dt;
            d.h += d.vh * dt;
            if (d.h <= 0 && d.vh < 0) {
                d.h = 0;
                list.erase(list.begin() + std::ptrdiff_t(i));
                onLand(d);
            } else
                list[i] = d;
        }
    }
    /** @brief Draw synchronously borrowed ramps/target in source insertion order, with no retained references. */
    void draw(Paint& p, std::span<const uint32_t> C, Paint::Layer layer) const {
        for (const auto& d : list) {
            if (d.delay > 0 || (layer == Paint::Layer::Back && d.z >= 0) || (layer == Paint::Layer::Front && d.z < 0))
                continue;
            const double y = d.z * sy - d.h;
            bead(p, d.x, y, d.vx, d.vz * sy - d.vh, d.s, C, std::min(1., d.life / .2));
            p.glow(d.x, y, 2.5 + d.s, C[5], .1 + .05 * d.s);
        }
    }
};
struct RippleOptions {
    int    n   = 2;
    double gap = 3, k = 1, hot = .1, glint = 0, delay = 0;
};
struct Ripple {
    double        x, z, r0, r1, life, age;
    RippleOptions options;
    uint32_t      seed;
};
/** @brief Owning spreading crest trains; sequence and fixed-step ages belong to this kernel-local owner. */
struct Ripples {
    double              sy       = .22;
    uint32_t            sequence = 0;
    std::vector<Ripple> list;
    /** @brief Append an owning train. @cost Amortized vector growth per impact. */
    void add(double x, double z, double r0, double r1, double life, RippleOptions o = {}) {
        list.push_back({x, z, r0, r1, life, -o.delay, o, ++sequence});
    }
    /** @brief Consume only the supplied fixed step; no callbacks or external clocks.
     * @cost Linear live-state traversal plus vector erasure shifts per tick. */
    void update(double dt) {
        for (size_t remaining = list.size(); remaining > 0; --remaining) {
            const size_t i = remaining - 1;
            if ((list[i].age += dt) >= list[i].life) list.erase(list.begin() + std::ptrdiff_t(i));
        }
    }
    /** @brief Borrow painter/ramp only during drawing; no callbacks or references are retained. */
    void draw(Paint& p, std::span<const uint32_t> C, int half = 0) const {
        for (const auto& q : list) {
            if (q.age < 0) continue;
            const double u = q.age / q.life, r = q.r0 + (q.r1 - q.r0) * (1 - std::pow(1 - u, 2.6)),
                         k = q.options.k * std::pow(1 - u, 1.7);
            ring(p, q.x, q.z * sy, r, sy, k, C,
                 {q.options.n, q.options.gap * (.55 + .6 * u), half, q.age < q.options.hot, q.options.glint * k,
                  q.seed});
        }
    }
};
struct MistPuff {
    double   x, y, vx, vy, r0, r1, life;
    uint32_t color;
    double   alpha = .25, delay = 0, max = 1;
};
/** @brief Owning soft puffs with fixed-step age, growth and damping; no clock/RNG is sampled internally. */
struct Mist {
    std::vector<MistPuff> list;
    /** @brief Append an owning puff. @cost Amortized vector growth per emitted puff. */
    void add(MistPuff q) {
        q.max = q.life;
        list.push_back(q);
    }
    /** @brief Consume only the supplied fixed step; no callbacks or external clocks.
     * @cost Linear live-state traversal plus vector erasure shifts per tick. */
    void update(double dt) {
        for (size_t remaining = list.size(); remaining > 0; --remaining) {
            const size_t i = remaining - 1;
            auto&        q = list[i];
            if (q.delay > 0) {
                q.delay -= dt;
                continue;
            }
            if ((q.life -= dt) <= 0) {
                list.erase(list.begin() + std::ptrdiff_t(i));
                continue;
            }
            q.vx -= q.vx * 2.4 * dt;
            q.vy -= q.vy * 2.4 * dt + 7 * dt;
            q.x += q.vx * dt;
            q.y += q.vy * dt;
        }
    }
    /** @brief Borrow the painter synchronously; soft-disc samplers never escape this call. */
    void draw(Paint& p, uint32_t haze) const {
        for (const auto& q : list) {
            if (q.delay > 0) continue;
            const double age = 1 - q.life / q.max, r = q.r0 + (q.r1 - q.r0) * (1 - std::pow(1 - age, 2.2)),
                         a = q.alpha * std::min(1., age / .12) * std::pow(1 - age, 1.4);
            if (a < .01) continue;
            p.disc(q.x, q.y, r, q.color, [&](double d) { return a * (1 - d * d) * (1 - d * .5); });
            p.disc(q.x - r * .2, q.y - r * .32, r * .55, WHITE, [&](double d) { return a * .25 * (1 - d) * (1 - d); });
            if (haze) p.glow(q.x, q.y, r * 1.5, haze, (.16 * a) / q.alpha * std::min(1., q.alpha * 3));
        }
    }
};
/** @brief Draw a borrowed-ramp foam field, with synchronous samplers and no retained pointers. */
inline void lace(Paint& p, double cx, double cy, double rx, double ry, double k, std::span<const uint32_t> C, double t,
                 uint32_t seed = 0) {
    if (k <= .01 || rx < 1) return;
    const double sy = ry / rx;
    p.shader(cx - rx, cy - ry, cx + rx, cy + ry, [&](double x, double y, ShadeOutput& out) {
        const double dx = (x - cx) / rx, dy = (y - cy) / ry, d = dx * dx + dy * dy;
        if (d > 1) return;
        const double u = (x - cx) * .36, v = ((y - cy) / sy) * .36, n = noise2(u + seed * 7.13, v - t * .35, seed),
                     m = noise2(u * 2.2 - t * .3, v * 2.2 + seed * 3.1, seed + 9), net = 1 - std::abs(n) * 4.6 + m * .3,
                     need = 1.02 - k * (1.15 - d * .9);
        if (net >= need) out.c = net > need + .4 ? WHITE : C[6];
    });
}
/** @brief Draw one wet pebble; target/ramp are borrowed synchronously with no retained callbacks. */
inline void pebble(Paint& p, double x, double y, double r, std::span<const uint32_t> ST) {
    p.shader(x - r - 1, y - r - 1, x + r + 1, y + r + 1, [&](double px, double py, ShadeOutput& out) {
        const double dx = px - x, dy = py - y, d = jsHypot2(dx * 1.1, dy);
        if (d > r + .8) return;
        const double lit = (dx + dy) / std::max(.5, r);
        out.c            = d > r ? ST[0] : lit < -.7 ? ST[5] : lit < .2 ? ST[4] : lit < .8 ? ST[3] : ST[2];
    });
}
}  // namespace eve::stylize::pixelfx::water
