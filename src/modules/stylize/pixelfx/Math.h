// Native adaptation of pixelfx core/math.js and color.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <span>
#include <vector>
#include "common/Export.h"

namespace eve::stylize::pixelfx {
constexpr double   PI = std::numbers::pi, TAU = 2 * PI;
constexpr uint32_t WHITE = 0xffffffffu, BLACK = 0xff000000u, INK = 0xff100a0bu;
inline double      clamp(double x, double lo = 0, double hi = 1) { return std::clamp(x, lo, hi); }
inline double      lerp(double a, double b, double t) { return a + (b - a) * t; }
inline double      invLerp(double a, double b, double x) { return b == a ? 0 : (x - a) / (b - a); }
inline double remap(double x, double a0, double a1, double b0, double b1) { return lerp(b0, b1, invLerp(a0, a1, x)); }
inline double fract(double x) { return x - std::floor(x); }
inline double wrap(double x, double m) { return std::fmod(std::fmod(x, m) + m, m); }
inline double seg(double t, double t0, double t1) { return clamp((t - t0) / (t1 - t0)); }
inline double smooth(double e0, double e1, double x) {
    const double t = seg(x, e0, e1);
    return t * t * (3 - 2 * t);
}
inline double bump(double t, double t0, double t1) { return t <= t0 || t >= t1 ? 0 : std::sin(PI * seg(t, t0, t1)); }
inline double pingpong(double x) { return 1 - std::abs(wrap(x, 2) - 1); }
/** @brief Match source two-argument Math.hypot normalization and rescaling.
 * @param x First coordinate. @param y Second coordinate. @return Source-compatible length.
 * @note Value-only, reentrant and allocation-free; no retained references or callbacks.
 * Caller supplies FE_TONEAREST. Infinity wins over NaN; signed zero pairs yield positive zero.
 * The implementation owns strict operation ordering independently of caller compilation flags. */
EVENGINE_API_WORLD double jsHypot2(double x, double y) noexcept;
/** @brief Match source three-argument Math.hypot with compensated normalized squares.
 * @param x First coordinate. @param y Second coordinate. @param z Third coordinate.
 * @return Source-compatible length, including IEEE Infinity/NaN and positive zero.
 * @note Value-only, reentrant and allocation-free; no callbacks or retained storage.
 * Caller supplies FE_TONEAREST. The compiled implementation preserves source operation ordering. */
EVENGINE_API_WORLD double jsHypot3(double x, double y, double z) noexcept;
inline double   jsRound(double x) { return std::floor(x + 0.5); }
inline uint32_t jsUint(double n) {
    if (!std::isfinite(n) || n == 0) return 0;
    return uint32_t(wrap(std::trunc(n), 4294967296.0));
}
inline uint32_t hash(std::initializer_list<uint32_t> ns) {
    uint32_t h = 0x9e3779b9u;
    for (auto n : ns) {
        h = (h ^ n) * 0x85ebca6bu;
        h ^= h >> 13;
        h *= 0xc2b2ae35u;
        h ^= h >> 16;
    }
    return h;
}
inline double rand(std::initializer_list<uint32_t> ns) { return double(hash(ns)) / 4294967296.0; }
struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed ? seed : 1) {}
    double next() {
        uint32_t t = (state += 0x6d2b79f5u);
        t          = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return double(t ^ (t >> 14)) / 4294967296.0;
    }
    double range(double a, double b) { return a + (b - a) * next(); }
    int    integer(int a, int b) { return int(std::floor(range(a, b + 1))); }
    bool   isChance(double p) { return next() < p; }
    int    sign() { return next() < 0.5 ? -1 : 1; }
    double signedRange(double a, double b) {
        const int side = sign();
        return side * range(a, b);
    }
    template <class T>
    T pick(std::span<const T> list) {
        return list[size_t(std::floor(next() * double(list.size())))];
    }
    double gauss() {
        const double a = next(), b = next(), c = next(), d = next();
        return (a + b + c + d - 2) * 1.732;
    }
};
namespace ease {
inline double linear(double t) { return t; }
inline double inQuad(double t) { return t * t; }
inline double outQuad(double t) { return 1 - (1 - t) * (1 - t); }
inline double inOutQuad(double t) { return t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2; }
inline double inCubic(double t) { return t * t * t; }
inline double outCubic(double t) { return 1 - std::pow(1 - t, 3); }
inline double inOutCubic(double t) { return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; }
inline double inQuart(double t) { return t * t * t * t; }
inline double outQuart(double t) { return 1 - std::pow(1 - t, 4); }
inline double inExpo(double t) { return t <= 0 ? 0 : std::pow(2, 10 * t - 10); }
inline double outExpo(double t) { return t >= 1 ? 1 : 1 - std::pow(2, -10 * t); }
inline double inSine(double t) { return 1 - std::cos(t * PI / 2); }
inline double outSine(double t) { return std::sin(t * PI / 2); }
inline double inOutSine(double t) { return -(std::cos(PI * t) - 1) / 2; }
inline double inBack(double t, double s = 1.70158) { return t <= 0 ? 0 : t >= 1 ? 1 : (s + 1) * t * t * t - s * t * t; }
inline double outBack(double t, double s = 1.70158) {
    return t <= 0 ? 0 : t >= 1 ? 1 : 1 + (s + 1) * std::pow(t - 1, 3) + s * std::pow(t - 1, 2);
}
inline double outElastic(double t) {
    return t <= 0 ? 0 : t >= 1 ? 1 : std::pow(2, -10 * t) * std::sin((t * 10 - 0.75) * (2 * PI / 3)) + 1;
}
inline double outBounce(double t) {
    constexpr double n = 7.5625, d = 2.75;
    if (t < 1 / d) return n * t * t;
    if (t < 2 / d) {
        t -= 1.5 / d;
        return n * t * t + 0.75;
    }
    if (t < 2.5 / d) {
        t -= 2.25 / d;
        return n * t * t + 0.9375;
    }
    t -= 2.625 / d;
    return n * t * t + 0.984375;
}
}  // namespace ease
inline double noise(double x, uint32_t seed = 0) {
    const double i = std::floor(x), f = x - i, u = f * f * (3 - 2 * f);
    return lerp(rand({jsUint(i), seed}), rand({jsUint(i + 1), seed}), u) * 2 - 1;
}
inline double tileNoise(double x, double y, int px = 0, int py = 0, uint32_t seed = 0) {
    const double   ix = std::floor(x), iy = std::floor(y), fx = x - ix, fy = y - iy;
    const double   ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    const uint32_t x0 = jsUint(px ? wrap(ix, px) : ix), x1 = jsUint(px ? wrap(ix + 1, px) : ix + 1);
    const uint32_t y0 = jsUint(py ? wrap(iy, py) : iy), y1 = jsUint(py ? wrap(iy + 1, py) : iy + 1);
    const double a = rand({x0, y0, seed}), b = rand({x1, y0, seed}), c = rand({x0, y1, seed}), d = rand({x1, y1, seed});
    return lerp(lerp(a, b, ux), lerp(c, d, ux), uy) * 2 - 1;
}
inline double noise2(double x, double y, uint32_t seed = 0) { return tileNoise(x, y, 0, 0, seed); }
inline double fbm(double x, double y, uint32_t seed = 0, int octaves = 3) {
    double s = 0, a = 0.5, n = 0;
    for (int o = 0; o < octaves; ++o) {
        s += noise2(x, y, seed + uint32_t(o) * 101u) * a;
        n += a;
        x *= 2.03;
        y *= 2.03;
        a *= 0.5;
    }
    return s / n;
}
inline double tileFbm(double x, double y, int px = 0, int py = 0, uint32_t seed = 0, int octaves = 3) {
    double s = 0, a = 0.5, n = 0;
    int    k = 1;
    for (int o = 0; o < octaves; ++o) {
        s += tileNoise(x * k, y * k, px * k, py * k, seed + uint32_t(o) * 101u) * a;
        n += a;
        a *= 0.5;
        k *= 2;
    }
    return s / n;
}
inline double loopNoise(double phase, double radius = 1, uint32_t seed = 0) {
    const double a = phase * TAU;
    return noise2(std::cos(a) * radius + 50, std::sin(a) * radius + 50, seed);
}
inline uint32_t rgb(double r, double g, double b) {
    return 0xff000000u | ((jsUint(b) & 255u) << 16) | ((jsUint(g) & 255u) << 8) | (jsUint(r) & 255u);
}
inline uint32_t hex(uint32_t v) { return rgb((v >> 16) & 255u, (v >> 8) & 255u, v & 255u); }
inline double   red(uint32_t c) { return c & 255u; }
inline double   green(uint32_t c) { return (c >> 8) & 255u; }
inline double   blue(uint32_t c) { return (c >> 16) & 255u; }
inline uint32_t mix(uint32_t a, uint32_t b, double t) {
    t = clamp(t);
    return rgb(lerp(red(a), red(b), t), lerp(green(a), green(b), t), lerp(blue(a), blue(b), t));
}
inline uint32_t scale(uint32_t c, double k) {
    return rgb(std::min(255.0, red(c) * k), std::min(255.0, green(c) * k), std::min(255.0, blue(c) * k));
}
inline double luma(uint32_t c) { return (0.299 * red(c) + 0.587 * green(c) + 0.114 * blue(c)) / 255; }
/** @brief Build owned preparation data. @cost Allocates proportional to output elements per call; retain the result
 * across frames. */
inline std::vector<uint32_t> fit(std::span<const uint32_t> r, int n) {
    std::vector<uint32_t> out(size_t(n), 0u);
    if (r.empty()) return out;
    for (int i = 0; i < n; ++i)
        out[size_t(i)] = r[size_t(jsRound(double(i) * double(r.size() - 1) / std::max(1, n - 1)))];
    return out;
}
inline uint32_t at(std::span<const uint32_t> r, double t) {
    return r[size_t(clamp(std::floor(t * double(r.size())), 0, double(r.size() - 1)))];
}
inline uint32_t blend(std::span<const uint32_t> r, double t) {
    if (r.size() == 1) return r[0];
    const double x = clamp(t) * double(r.size() - 1);
    const size_t i = std::min(r.size() - 2, size_t(std::floor(x)));
    return mix(r[i], r[i + 1], x - double(i));
}
struct Point {
    double x = 0, y = 0;
};
inline Point fly(double x, double y, double vx, double vy, double t, double g = 0, double drag = 0) {
    if (drag <= 0) return {x + vx * t, y + vy * t + 0.5 * g * t * t};
    const double f = (1 - std::exp(-drag * t)) / drag, vt = g / drag;
    return {x + vx * f, y + vt * t + (vy - vt) * f};
}
inline Point flyV(double vx, double vy, double t, double g = 0, double drag = 0) {
    if (drag <= 0) return {vx, vy + g * t};
    const double e = std::exp(-drag * t), vt = g / drag;
    return {vx * e, vt + (vy - vt) * e};
}
/** @brief Build owned preparation data. @cost Allocates proportional to output elements per call; retain the result
 * across frames. */
inline std::vector<Point> star(double cx, double cy, double r0, double r1, int n = 5, double rot = -PI / 2) {
    std::vector<Point> out;
    out.reserve(size_t(n) * 2);
    for (int i = 0; i < n * 2; ++i) {
        const double a = rot + i * PI / n, r = i % 2 ? r0 : r1;
        out.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
    return out;
}
/** @brief Build owned preparation data. @cost Allocates proportional to output elements per call; retain the result
 * across frames. */
inline std::vector<Point> ngon(double cx, double cy, double r, int n = 6, double rot = 0) {
    std::vector<Point> out;
    out.reserve(size_t(n));
    for (int i = 0; i < n; ++i) {
        const double a = rot + i * TAU / n;
        out.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
    return out;
}
/** @brief Build owned preparation data. @cost Allocates proportional to output elements per call; retain the result
 * across frames. */
inline std::vector<Point> bolt(Rng& r, double x0, double y0, double x1, double y1, double jag = 0.22, int depth = 5) {
    std::vector<Point> pts{{x0, y0}, {x1, y1}};
    for (int d = 0; d < depth; ++d) {
        std::vector<Point> next{pts.front()};
        next.reserve(pts.size() * 2 - 1);
        for (size_t i = 1; i < pts.size(); ++i) {
            const auto [ax, ay] = pts[i - 1];
            const auto [bx, by] = pts[i];
            const double dx = bx - ax, dy = by - ay, L = jsHypot2(dx, dy), off = (r.next() * 2 - 1) * jag * L;
            next.push_back({(ax + bx) / 2 - dy / (L == 0 ? 1 : L) * off, (ay + by) / 2 + dx / (L == 0 ? 1 : L) * off});
            next.push_back(pts[i]);
        }
        pts = std::move(next);
    }
    return pts;
}
/** @brief Build owned preparation data. @cost Allocates proportional to output elements per call; retain the result
 * across frames. */
inline std::vector<std::vector<Point>> forks(Rng& r, std::span<const Point> path, int count = 3, double len = 20,
                                             double jag = 0.3, double spread = 0.9) {
    std::vector<std::vector<Point>> out;
    out.reserve(size_t(count));
    for (int k = 0; k < count; ++k) {
        const size_t i      = 1 + size_t(std::floor(r.next() * double(path.size() - 2)));
        const auto [x, y]   = path[i];
        const auto [px, py] = path[i - 1];
        const double sign   = r.next() < 0.5 ? -1 : 1,
                     a      = std::atan2(y - py, x - px) + sign * spread * (0.4 + 0.6 * r.next());
        const double l      = len * (0.4 + 0.6 * r.next());
        out.push_back(bolt(r, x, y, x + std::cos(a) * l, y + std::sin(a) * l, jag, 3));
    }
    return out;
}
}  // namespace eve::stylize::pixelfx
