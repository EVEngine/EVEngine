// eve-hot-path
// Native adaptation of pixelfx transitions/_lib.js field and pixels, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#pragma once
#include <numeric>
#include "stylize/pixelfx/ScreenTransition.h"
namespace eve::stylize::pixelfx::screen {
/** @brief Owning prepared scalar samples, gradients and stable ascending pixel indices. */
struct Field {
    std::vector<float>    f, g, ny;
    std::vector<uint32_t> order;
    double                lo = INFINITY, hi = -INFINITY;
    /** @brief First ordered value not below a supplied threshold; no allocation or mutation. */
    size_t below(double value) const {
        size_t a = 0, b = order.size();
        while (a < b) {
            const size_t m = (a + b) >> 1;
            if (double(f[order[m]]) < value)
                a = m + 1;
            else
                b = m;
        }
        return a;
    }
};
/** @brief Prepare source Float32 storage, double extrema and promoted gradients with stable ties.
 * @note The trusted compiled sampler is synchronous and not retained; output owns all samples.
 * @cost Samples all57600 pixels then sorts once per configuration; amortized across frames and seeks. */
template <class Sample>
Field field(Sample sample, double gmin = .45, double gmax = 3) {
    Field out;
    out.f.resize(W * H);
    out.g.resize(W * H);
    out.ny.resize(W * H);
    out.order.resize(W * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const double v           = sample(x + .5, y + .5);
            out.f[size_t(y * W + x)] = float(v);
            out.lo                   = std::min(out.lo, v);
            out.hi                   = std::max(out.hi, v);
        }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const int    xa = std::max(0, x - 1), xb = std::min(W - 1, x + 1), ya = std::max(0, y - 1),
                         yb = std::min(H - 1, y + 1);
            const double gx = (double(out.f[size_t(y * W + xb)]) - double(out.f[size_t(y * W + xa)])) / (xb - xa),
                         gy = (double(out.f[size_t(yb * W + x)]) - double(out.f[size_t(ya * W + x)])) / (yb - ya),
                         L  = jsHypot2(gx, gy);
            out.g[size_t(y * W + x)]  = float(1 / clamp(L, gmin, gmax));
            out.ny[size_t(y * W + x)] = float(L > 1e-6 ? gy / L : 0);
        }
    std::iota(out.order.begin(), out.order.end(), 0u);
    std::stable_sort(out.order.begin(), out.order.end(), [&](uint32_t a, uint32_t b) { return out.f[a] < out.f[b]; });
    return out;
}
/** @brief Owner-thread synchronous pixel-index writer borrowing one painter; no storage escapes. */
class Pixels {
    Paint&   p;
    int      sx = 0, sy = 0;
    uint32_t index = 0;
    bool     unrotated() const { return p.sn == 0 && p.cs == 1; }
    double   x() const { return index % W + .5; }
    double   y() const { return index / W + .5; }

public:
    /** @brief Borrow the painter for the lifetime of this local writer. */
    explicit Pixels(Paint& painter) : p(painter) {}
    /** @brief Select an authored valid pixel index without writing. */
    void at(uint32_t i) {
        index = i;
        if (unrotated()) {
            sx = int(std::floor(p.tx + ((i % W) + .5) * p.fx));
            sy = int(std::floor(p.ty + (i / W) + .5));
        }
    }
    /** @brief Write the selected pixel, preserving source opacity dithering. */
    void put(uint32_t color, double alpha = 1) {
        if (unrotated())
            p.put(sx, sy, color, alpha);
        else
            p.dot(x(), y(), color, alpha);
    }
    /** @brief Add source light at the selected pixel. */
    void add(uint32_t color, double strength) {
        if (unrotated())
            p.add(sx, sy, color, strength);
        else
            p.light(x(), y(), color, strength);
    }
    /** @brief Blend source color at the selected pixel. */
    void blend(uint32_t color, double alpha) {
        if (unrotated())
            p.blendPx(sx, sy, color, alpha);
        else
            p.blend(x(), y(), color, alpha);
    }
};
}  // namespace eve::stylize::pixelfx::screen
