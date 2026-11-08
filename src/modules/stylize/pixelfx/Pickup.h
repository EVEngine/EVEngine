// Native adaptation of pixelfx pickups/_lib.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include <cstddef>
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::pickup {
/** @brief Call-local owning scratch sprite; no external buffers or painter references survive a draw.
 * @note Caller-thread affine; no callbacks retained. Storage is bounded by compile-time dimensions. */
template <int W, int H>
struct Layer {
    std::array<uint32_t, size_t(W) * H> colors{}, lights{}, tmp{};
    PixelFxInfo                         info{PixelFxKind::CoinSpin, "", W, H, 0, 0, 1, false};
    /** @brief Clear sprite cells and select an integer local origin. */
    void begin(double x, double y) {
        info.originX = -int(std::floor(x));
        info.originY = -int(std::floor(y));
        colors.fill(0);
        lights.fill(0);
    }
    /** @brief Read a local cell; out-of-layer reads are empty. */
    uint32_t get(double x, double y) const {
        const int i = int(std::floor(x)) + info.originX, j = int(std::floor(y)) + info.originY;
        return i < 0 || j < 0 || i >= W || j >= H ? 0 : colors[size_t(j * W + i)];
    }
    /** @brief Write a local cell; out-of-layer writes are clipped. */
    void set(double x, double y, uint32_t c) {
        const int i = int(std::floor(x)) + info.originX, j = int(std::floor(y)) + info.originY;
        if (i >= 0 && j >= 0 && i < W && j < H) colors[size_t(j * W + i)] = c;
    }
    /** @brief Sample a borrowed callback synchronously at source pixel centers. */
    template <class F>
    void fill(double x0, double y0, double x1, double y1, F fn) {
        for (int y = int(std::floor(y0)); y <= y1; ++y)
            for (int x = int(std::floor(x0)); x <= x1; ++x) {
                const auto c = fn(x + .5, y + .5);
                if (c) set(x, y, c);
            }
    }
    /** @brief Add the source four-neighbor one-cell outline without feeding newly written cells back. */
    void outline(uint32_t c) {
        tmp.fill(0);
        for (int j = 0; j < H; ++j)
            for (int i = 0; i < W; ++i) {
                const size_t k = size_t(j * W + i);
                if (colors[k]) continue;
                const bool n = (i > 0 && colors[k - 1]) || (i < W - 1 && colors[k + 1]) || (j > 0 && colors[k - W]) ||
                               (j < H - 1 && colors[k + W]);
                if (n) tmp[k] = c;
            }
        for (size_t k = 0; k < colors.size(); ++k)
            if (tmp[k]) colors[k] = tmp[k];
    }
    /** @brief Stamp through the borrowed target's transform and fade, retaining no target reference. */
    void draw(Paint& p, double dx = 0, double dy = 0, double a = 1) const {
        for (size_t k = 0; k < colors.size(); ++k)
            if (colors[k]) p.dot(double(k % W) - info.originX + dx, double(k / W) - info.originY + dy, colors[k], a);
    }
};
/** @brief Draw the source soft dithered floor shadow through a synchronous target borrow. */
inline void blobShadow(Paint& p, double x, double rx, double k = 1, double y = 0) {
    if (rx < .8 || k <= 0) return;
    for (int i = int(std::floor(x - rx)); i <= int(std::ceil(x + rx)); ++i) {
        const double u = std::abs(i + .5 - x) / rx;
        if (u > 1) continue;
        const double a = k * (1 - u * u);
        p.dot(i, y, hex(0x0b0814), a * 1.1);
        if (u < .6) p.dot(i, y + 1, hex(0x0b0814), a * .6);
    }
}
/** @brief Draw the source four-point glint and light through a synchronous target borrow. */
inline void glint(Paint& p, double x, double y, double arm, uint32_t c, double k = .4) {
    const int n = int(jsRound(arm)), X = int(std::floor(x)), Y = int(std::floor(y));
    if (n < 0) return;
    const auto tip = scale(c, .8);
    for (int d = 1; d <= n; ++d) {
        const auto cc = d == n && n > 1 ? tip : c;
        p.px(X + d, Y, cc);
        p.px(X - d, Y, cc);
        p.px(X, Y + d, cc);
        p.px(X, Y - d, cc);
    }
    p.px(X, Y, n > 0 ? WHITE : c);
    p.glow(X + .5, Y + .5, 3 + n * 2.5, c, k);
}
/** @brief Draw source elliptical additive floor light; target is borrowed synchronously. */
inline void pool(Paint& p, double x, double y, double rx, double ry, uint32_t c, double k) {
    if (k <= .004 || rx < 1) return;
    p.shader(x - rx, y - ry, x + rx, y + ry, [&](double px, double py, ShadeOutput& out) {
        const double dx = (px - x) / rx, dy = (py - y) / ry, d = dx * dx + dy * dy;
        if (d >= 1) return;
        const double f = 1 - std::sqrt(d);
        out.l          = c;
        out.k          = f * f * k;
    });
}
/** @brief Source pillow surface lighting controls copied for one synchronous draw. */
struct PillowOptions {
    double                bevel = 3;
    std::array<double, 3> light{-.5, -.6, .62};
};
/** @brief Fill an owning layer from borrowed SDF/color/clip callbacks; retain none of them.
 * @cost Linear in sampled cells with five SDF evaluations per accepted cell per draw. */
template <int W, int H, class Sdf, class Color, class Clip = std::nullptr_t>
void pillow(Layer<W, H>& L, double x0, double y0, double x1, double y1, Sdf sdf, Color color,
            const PillowOptions& o = {}, Clip clip = nullptr) {
    L.fill(x0, y0, x1, y1, [&](double x, double y) -> uint32_t {
        const double d = sdf(x, y);
        if (d > 0) return 0;
        if constexpr (!std::is_same_v<Clip, std::nullptr_t>) {
            if (clip(x, y)) return 0;
        }
        const double e  = -d;
        double       gx = sdf(x + .5, y) - sdf(x - .5, y), gy = sdf(x, y + .5) - sdf(x, y - .5);
        const double mag = jsHypot2(gx, gy), len = mag == 0 ? 1 : mag;
        gx /= len;
        gy /= len;
        const double k = std::max(0.0, 1 - e / o.bevel) * 1.25, sn = std::sin(k), cs = std::cos(k);
        return sample(color, gx * sn * o.light[0] + gy * sn * o.light[1] + cs * o.light[2], e, x, y);
    });
}
/** @brief Sample the source sextic heart signed distance in logical pixels. */
inline double heartSdf(double x, double y, double cx, double cy, double w, double h) {
    const double S = w / 1.14, X = (x - cx) / S, Y = -(y - cy) / (h / 1.14) + .12, q = X * X + Y * Y - 1,
                 f = q * q * q - X * X * Y * Y * Y, gx = 6 * X * q * q - 2 * X * Y * Y * Y,
                 gy = 6 * Y * q * q - 3 * X * X * Y * Y;
    return f / std::max(jsHypot2(gx, gy), .6) * S;
}
}  // namespace eve::stylize::pixelfx::pickup
