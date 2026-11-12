// Native adaptation of pixelfx weather/_lib.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "common/Assert.h"
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::weather {
/** @brief Derive immutable local source bounds from effect-owned metadata. */
constexpr Paint::ClipRect box(const PixelFxInfo& source) {
    return {-double(source.originX), -double(source.originY), double(source.width - source.originX),
            double(source.height - source.originY)};
}
/** @brief Copy a synchronous borrowed painter with source-canvas write bounds; no ownership escapes. */
inline Paint clip(const Paint& p, const PixelFxInfo& source) {
    auto q     = p;
    q.clipRect = box(source);
    return q;
}
struct Output {
    uint32_t c     = 0;
    double   a     = 1;
    bool     blend = false;
    uint32_t l     = 0;
    double   k     = 0;
};
/** @brief Borrow sampler/target only for this canvas-local raster pass.
 * @cost Linear in visited source rows and columns per draw. */
template <class F>
void pass(Paint& p, const PixelFxInfo& source, double y0, double y1, F fn) {
    const auto SKY = box(source);
    y0             = std::max(SKY.y0, std::floor(y0));
    y1             = std::min(SKY.y1 - 1, std::ceil(y1));
    auto q         = clip(p, source);
    for (double y = y0; y <= y1; ++y)
        for (double x = SKY.x0; x < SKY.x1; ++x) {
            Output o;
            fn(x + .5, y + .5, o);
            if (o.c) {
                if (o.blend)
                    q.blend(x, y, o.c, o.a);
                else
                    q.dot(x, y, o.c, o.a);
            }
            if (o.k > 0) q.light(x, y, o.l ? o.l : o.c, o.k);
        }
}
/** @brief Sample whole-number falls on an injected loop clock/seed; the callable is borrowed synchronously. */
template <class F>
void trips(double t, double D, int n, uint32_t seed, int m0, int m1, F fn) {
    for (int i = 0; i < n; ++i) {
        const int    m = m0 + int(std::floor(rand({seed, uint32_t(i), 72}) * (m1 - m0 + 1)));
        const double s = (t / D) * m + rand({seed, uint32_t(i), 71}), k = std::floor(s);
        fn(s - k, k, m, uint32_t(i));
    }
}
struct RowPixel {
    uint32_t c = 0, l = 0;
    float    a = 1, k = 0;
    bool     blend = false;
};
/** @brief Sample source rows using Float32 alpha/light storage, borrowing the sampler and target synchronously.
 * @cost Linear source-canvas traversal plus one row allocation per call. */
template <class F>
void rows(Paint& p, const PixelFxInfo& source, double y0, double y1, F fn) {
    const auto            b = box(source);
    auto                  q = clip(p, source);
    std::vector<RowPixel> row(size_t(source.width));
    y0 = std::max(b.y0, std::floor(y0));
    y1 = std::min(b.y1 - 1, std::ceil(y1));
    for (double y = y0; y <= y1; ++y) {
        for (auto& v : row) {
            v.c     = 0;
            v.k     = 0;
            v.a     = 1;
            v.blend = false;
        }
        fn(y + .5, std::span<RowPixel>(row));
        for (size_t i = 0; i < row.size(); ++i) {
            const auto&  v = row[i];
            const double x = b.x0 + double(i);
            if (v.c) {
                if (v.blend)
                    q.blend(x, y, v.c, v.a);
                else
                    q.dot(x, y, v.c, v.a);
            }
            if (v.k > 0) q.light(x, y, v.l ? v.l : v.c, v.k);
        }
    }
}
/** @brief Immutable tiling Float32 noise, built at configuration preparation; no global cache or RNG retained. */
struct Field {
    int                w = 256, h = 128;
    std::vector<float> d;
    /** @brief Build a private immutable noise map from compiled positive octave counts.
     * @cost Linear in baked cells times octaves once per prepared program, reused across frames and seeks. */
    explicit Field(uint32_t seed, std::span<const int> cells) : d(size_t(w) * size_t(h), 0) {
        EV_ASSERT(!cells.empty(), "Compiled weather noise requires octaves");
        double amp = .5, total = 0;
        for (int n : cells) {
            EV_ASSERT(n > 0, "Compiled weather octaves must be positive");
            const double cs   = double(w) / n;
            const int    rows = std::max(1, int(jsRound(h / cs)));
            const auto   at   = [&](double a, double b) {
                return rand({jsUint(wrap(a, n)), jsUint(wrap(b, rows)), seed, uint32_t(n)});
            };
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const double gx = x / cs, gy = (double(y) / h) * rows, ix = std::floor(gx), iy = std::floor(gy),
                                 u = gx - ix, v = gy - iy, su = u * u * (3 - 2 * u), sv = v * v * (3 - 2 * v),
                                 top   = at(ix, iy) + (at(ix + 1, iy) - at(ix, iy)) * su,
                                 bot   = at(ix, iy + 1) + (at(ix + 1, iy + 1) - at(ix, iy + 1)) * su;
                    auto&        value = d[size_t(y) * size_t(w) + size_t(x)];
                    value              = float(double(value) + (top + (bot - top) * sv) * amp);
                }
            total += amp;
            amp *= .5;
        }
        for (auto& v : d) v = float(double(v) / total);
    }
    /** @brief Read a bilinearly sampled wrapped cell without mutable state or allocation. */
    double sample(double u, double v) const {
        const int    x = int(std::floor(u)), y = int(std::floor(v));
        const double fx = u - x, fy = v - y;
        const auto   x0 = uint32_t(x) & uint32_t(w - 1), x1 = uint32_t(x + 1) & uint32_t(w - 1),
                     y0 = (uint32_t(y) & uint32_t(h - 1)) * uint32_t(w),
                     y1 = (uint32_t(y + 1) & uint32_t(h - 1)) * uint32_t(w);
        const double a = d[y0 + x0], b = d[y0 + x1], c = d[y1 + x0], e = d[y1 + x1];
        return a + (b - a) * fx + (c - a + (a - b - c + e) * fx) * fy;
    }
};
constexpr std::array<int, 4> OCTAVES{{4, 8, 16, 32}};
constexpr std::array<int, 3> LOW_OCTAVES{{4, 8, 16}};
/** @brief Ordered dither at local integer cell coordinates. */
inline double bayer(double x, double y) {
    constexpr int B[]{0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return (B[((int(std::floor(y)) & 3) << 2) | (int(std::floor(x)) & 3)] + .5) / 16;
}
/** @brief Packed opaque material colors for one synchronously borrowed crown draw. */
struct CrownColors {
    uint32_t hot, cool, white;
};
/** @brief Rendering lifetime state of one source crown. */
enum class CrownState { Expired, Visible };
/** @brief Draw one timed floor crown using synchronously borrowed target/colors and a stable drop hash.
 * @return Visible before the crown lifetime ends, Expired afterwards; no callback or target is retained. */
inline CrownState crown(Paint& q, double x, double z, double age, double size, CrownColors c, double h) {
    const double L = .22 + .14 * size, u = age / L, X = jsRound(x), Z = jsRound(z);
    if (u >= 1) return CrownState::Expired;
    if (age < .1) {
        const int up = age < .033 ? 1 : age < .067 ? 2 : 2 + (size > .5 ? 1 : 0);
        q.px(X, Z - 1, age < .05 ? c.white : c.hot);
        for (int d = 1; d <= std::min(up, size > .5 ? 3 : 1); ++d) {
            q.px(X - d, Z - 1 - (d > 1 ? d - 1 : 0), d == 1 ? c.hot : c.cool);
            q.px(X + d, Z - 1 - (d > 1 ? d - 1 : 0), d == 1 ? c.hot : c.cool);
        }
    }
    if (u > .15) {
        const int w = int(jsRound(1 + (1.5 + 3.5 * size) * std::sqrt(u))), gap = u > .55 ? std::max(0, w - 2) : -1;
        for (int d = -w; d <= w; ++d)
            if (std::abs(d) > gap) q.px(X + d, Z, std::abs(d) >= w - 1 && u < .6 ? c.hot : c.cool);
    }
    const int      nd   = size > .5 ? 2 : 1;
    const uint32_t hash = jsUint(std::floor(h * 1e6));
    for (int d = 0; d < nd; ++d) {
        const double vx = (rand({hash, uint32_t(d), 1}) - .5) * 2 * (20 + 30 * size),
                     vy = -(50 + rand({hash, uint32_t(d), 2}) * 60 * (.4 + size)),
                     y  = Z - 1 + vy * age + 475 * age * age;
        if (y < Z - .5 && age < .3) q.px(x + vx * age, y, age < .1 ? c.white : c.hot);
    }
    return CrownState::Visible;
}
/** @brief Owning seed/material-derived local pixels, preserving source overlap order without a global cache. */
struct RecordedRaster {
    struct Pixel {
        double   x, y;
        uint32_t color;
        double   alpha;
    };
    std::vector<Pixel> pixels;
    /** @brief Borrow a source shader only during cold raster preparation.
     * @cost Visits the authored shape bounds and appends colored samples once per prepared program. */
    template <class F>
    void shader(double x0, double y0, double x1, double y1, F fn) {
        for (int y = int(std::floor(y0)); y <= std::ceil(y1); ++y)
            for (int x = int(std::floor(x0)); x <= std::ceil(x1); ++x) {
                ShadeOutput o;
                fn(x + .5, y + .5, o);
                if (o.c) pixels.push_back({x + .5, y + .5, o.c, o.a});
            }
    }
    /** @brief Replay ordered local samples through a synchronously borrowed clipped painter.
     * @cost Linear in prepared colored samples per draw, without allocations. */
    void draw(Paint& p) const {
        for (const auto& v : pixels) p.dot(v.x, v.y, v.color, v.alpha);
    }
};
}  // namespace eve::stylize::pixelfx::weather
