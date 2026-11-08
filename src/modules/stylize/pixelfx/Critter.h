// Native adaptation of pixelfx critters/_lib.js Grid, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::critter {
/** @brief Source seed-shifted loop phase; time is injected, with no global RNG or clock. */
inline double shift(double time, double duration, uint32_t seed) {
    return wrap(time + (double(seed) - 1) * .618 * duration, duration);
}
/** @brief Blend the source soft elliptical ground shadow through a synchronous painter borrow. */
inline void floorShadow(Paint& p, double cx, double cy, double rx, double ry, uint32_t c, double alpha) {
    if (rx <= 0 || ry <= 0 || alpha <= 0) return;
    for (int y = int(std::floor(cy - ry)); y <= int(std::ceil(cy + ry)); ++y)
        for (int x = int(std::floor(cx - rx)); x <= int(std::ceil(cx + rx)); ++x) {
            const double d = std::pow((x + .5 - cx) / rx, 2) + std::pow((y + .5 - cy) / ry, 2);
            if (d < 1) p.blend(x, y, c, alpha * std::pow(1 - d, .7));
        }
}
/** @brief Call-local creature grid owning color/Float32 alpha/tags; no external buffers retained.
 * @note Caller-thread affine; all callbacks and painter/material references are synchronous borrows. */
template <int W, int H>
struct Grid {
    std::array<uint32_t, size_t(W) * H> colors{};
    std::array<float, size_t(W) * H>    alpha{};
    std::array<uint8_t, size_t(W) * H>  tags{};
    std::array<uint8_t, size_t(W) * H>  depthValues{};
    int                                 x0, y0;
    /** @brief Construct a bounded grid with integer source origin. */
    Grid(int x, int y) : x0(x), y0(y) {}
    /** @brief Return a local cell index, or negative for a clipped coordinate. */
    int index(double x, double y) const {
        const int ix = int(std::floor(x)) - x0, iy = int(std::floor(y)) - y0;
        return ix < 0 || iy < 0 || ix >= W || iy >= H ? -1 : iy * W + ix;
    }
    /** @brief Paint a nonzero source color; zero leaves existing storage unchanged. */
    void set(double x, double y, uint32_t c, double a = 1, uint8_t tag = 0) {
        const int i = index(x, y);
        if (i >= 0 && c) {
            colors[size_t(i)] = c;
            alpha[size_t(i)]  = float(a);
            tags[size_t(i)]   = tag;
        }
    }
    /** @brief Read a cell, treating out-of-grid coordinates as empty. */
    uint32_t get(double x, double y) const {
        const int i = index(x, y);
        return i < 0 ? 0 : colors[size_t(i)];
    }
    /** @brief Read a source tag; out-of-grid coordinates have tag zero. */
    uint8_t cellTag(double x, double y) const {
        const int i = index(x, y);
        return i < 0 ? 0 : tags[size_t(i)];
    }
    /** @brief Compute owning source four-neighbor inward distances, saturated at maximum.
     * @cost Linear in grid cell count; recompute only after preparing the cell silhouette.
     * @note Depths are a snapshot and persist across subsequent cell writes and clear(), matching source grids. */
    void computeDepths(uint8_t maximum = 4) {
        depthValues.fill(0);
        std::array<size_t, size_t(W) * H> queue{};
        size_t                            count = 0;
        for (size_t i = 0; i < colors.size(); ++i) {
            if (!colors[i]) continue;
            const size_t x = i % W, y = i / W;
            if (x == 0 || y == 0 || x == W - 1 || y == H - 1 || !colors[i - 1] || !colors[i + 1] || !colors[i - W] ||
                !colors[i + W]) {
                depthValues[i] = 1;
                queue[count++] = i;
            }
        }
        for (size_t k = 0; k < count; ++k) {
            const size_t i     = queue[k];
            const auto   value = depthValues[i];
            if (value >= maximum) continue;
            for (int j : {int(i) - 1, int(i) + 1, int(i) - W, int(i) + W}) {
                if (j >= 0 && size_t(j) < colors.size() && colors[size_t(j)] && !depthValues[size_t(j)]) {
                    depthValues[size_t(j)] = uint8_t(value + 1);
                    queue[count++]         = size_t(j);
                }
            }
        }
        for (size_t i = 0; i < colors.size(); ++i)
            if (colors[i] && !depthValues[i]) depthValues[i] = maximum;
    }
    /** @brief Read the last depth snapshot at a floored source coordinate; outside or uncomputed cells return zero. */
    uint8_t cellDepth(double x, double y) const {
        const int i = index(x, y);
        return i < 0 ? 0 : depthValues[size_t(i)];
    }
    /** @brief Erase only the color, preserving source alpha/tag storage until overwritten. */
    void erase(double x, double y) {
        const int i = index(x, y);
        if (i >= 0) colors[size_t(i)] = 0;
    }
    struct Output {
        double  a   = 1;
        uint8_t tag = 0;
    };
    /** @brief Sample a borrowed callback at source cell centers; output state resets for each sample. */
    template <class F>
    void shade(double ax, double ay, double bx, double by, F fn) {
        for (int y = int(std::floor(ay)); y <= int(std::floor(by)); ++y)
            for (int x = int(std::floor(ax)); x <= int(std::floor(bx)); ++x) {
                Output     out;
                const auto c = [&]() {
                    if constexpr (std::is_invocable_v<F, double, double, Output&>)
                        return fn(x + .5, y + .5, out);
                    else
                        return sample(fn, x + .5, y + .5);
                }();
                if (c) set(x, y, c, out.a, out.tag);
            }
    }
    /** @brief Fill a source ellipse; color may be a synchronous normalized-coordinate callback. */
    template <class C>
    void ellipse(double cx, double cy, double rx, double ry, C c, double a = 1) {
        if (rx <= 0 || ry <= 0) return;
        shade(cx - rx, cy - ry, cx + rx, cy + ry, [&](double x, double y, Output& out) -> uint32_t {
            const double nx = (x - cx) / rx, ny = (y - cy) / ry;
            if (nx * nx + ny * ny > 1) return 0;
            out.a = a;
            return sample(c, nx, ny);
        });
    }
    /** @brief Rasterize the source single-cell line; callback color receives normalized segment time. */
    template <class C>
    void line(double ax, double ay, double bx, double by, C c, double a = 1) {
        const int n = std::max(1, int(std::ceil(std::max(std::abs(bx - ax), std::abs(by - ay)))));
        for (int i = 0; i <= n; ++i)
            set(ax + ((bx - ax) * i) / n, ay + ((by - ay) * i) / n, sample(c, double(i) / n), a);
    }
    /** @brief Stamp a source tapering path of discs, borrowing vertices/radius/color callbacks synchronously. */
    template <class R, class C>
    void tube(std::span<const Point> pts, R radius, C color, double alphaValue = 1) {
        for (size_t k = 1; k < pts.size(); ++k) {
            const auto   a = pts[k - 1], b = pts[k];
            const double length = std::max(1., jsHypot2(b.x - a.x, b.y - a.y));
            for (double s = 0; s <= length; s += .5) {
                const double u = (double(k - 1) + s / length) / double(pts.size() - 1), r = sample(radius, u),
                             x = a.x + ((b.x - a.x) * s) / length, y = a.y + ((b.y - a.y) * s) / length;
                if (r < .75) {
                    set(x, y, sample(color, u, 0.), alphaValue);
                    continue;
                }
                shade(x - r, y - r, x + r, y + r, [&](double px, double py, Output& out) -> uint32_t {
                    const double d = jsHypot2(px - x, py - y) / r;
                    out.a          = alphaValue;
                    return d > 1 ? 0 : sample(color, u, d);
                });
            }
        }
    }
    /** @brief Fill a source even-odd polygon, borrowing vertices and callback only for this call. */
    template <class C>
    void poly(std::span<const Point> pts, C c, double a = 1) {
        double ax = INFINITY, ay = INFINITY, bx = -INFINITY, by = -INFINITY;
        for (auto q : pts) {
            ax = std::min(ax, q.x);
            ay = std::min(ay, q.y);
            bx = std::max(bx, q.x);
            by = std::max(by, q.y);
        }
        if (pts.empty()) return;
        shade(ax, ay, bx, by, [&](double x, double y, Output& out) -> uint32_t {
            bool inside = false;
            for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
                const auto a0 = pts[i], b0 = pts[j];
                if ((a0.y > y) != (b0.y > y) && x < ((b0.x - a0.x) * (y - a0.y)) / (b0.y - a0.y) + a0.x)
                    inside = !inside;
            }
            out.a = a;
            return inside ? sample(c, x, y) : 0;
        });
    }
    /** @brief Outline an immutable snapshot; callback receives local cell and summed outward normal. */
    template <class C>
    void outline(C c, double a = 1) {
        const auto src = colors;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t i = size_t(y * W + x);
                if (src[i]) continue;
                const bool l = x > 0 && src[i - 1], r = x < W - 1 && src[i + 1], u = y > 0 && src[i - W],
                           d = y < H - 1 && src[i + W];
                if (!(l || r || u || d)) continue;
                const auto col = sample(c, double(x0 + x), double(y0 + y), int(l) - int(r), int(u) - int(d));
                if (col) {
                    colors[i] = col;
                    alpha[i]  = float(a);
                    tags[i]   = 255;
                }
            }
    }
    /** @brief Rework filled cells through a synchronous borrowed callback; zero erases only color. */
    template <class F>
    void reworkCells(F fn) {
        for (size_t i = 0; i < colors.size(); ++i) {
            if (!colors[i]) continue;
            colors[i] =
                sample(fn, double(x0 + int(i % W)), double(y0 + int(i / W)), colors[i], double(alpha[i]), tags[i]);
        }
    }
    /** @brief Stamp opaque cells with source dither and translucent Float32 cells with source blending. */
    void draw(Paint& p, double fade = 1, double dx = 0, double dy = 0) const {
        const double X = x0 + jsRound(dx), Y = y0 + jsRound(dy);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t i = size_t(y * W + x);
                if (!colors[i]) continue;
                if (alpha[i] >= .999)
                    p.dot(X + x, Y + y, colors[i], fade);
                else
                    p.blend(X + x, Y + y, colors[i], double(alpha[i]) * fade);
            }
    }
    /** @brief Clear colors/tags while preserving source alpha storage. */
    void clear() {
        colors.fill(0);
        tags.fill(0);
    }
};
}  // namespace eve::stylize::pixelfx::critter
