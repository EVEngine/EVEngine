// eve-hot-path
// Native adaptation of pixelfx spawn/egg-hatch.js geometry, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#pragma once
#include "stylize/pixelfx/Math.h"
namespace eve::stylize::pixelfx::egg {
constexpr int WIDTH = 16, HEIGHT = 20, SEAM = 11;
/** @brief One owning shell cell with source shading and prepared crack distances. */
struct Pixel {
    int                   x, y;
    double                u, v, half, lit = 0;
    int64_t               color = 0;
    int                   crack = -1;
    std::array<double, 3> near{9, 9, 9};
};
/** @brief Owning fixed-size egg shell and source crack stages, prepared once per instance.
 * @note Borrowed palette and RNG are used synchronously; no input pointers survive preparation.
 * @cost Bounded 320-cell geometry and three nearest-crack scans, amortized over playback. */
struct Geometry {
    /** @brief Canonical ramp/bitwise color number produced by upstream hex/mix. */
    [[nodiscard]] static int64_t    signedColor(uint32_t c) { return int64_t(c) - 4294967296LL; }
    std::vector<Pixel>              pixels;
    std::array<int, WIDTH * HEIGHT> indices;
    Geometry(std::span<const uint32_t> C, int64_t speckle, Rng& R) {
        indices.fill(-1);
        for (int y = 0; y < HEIGHT; ++y)
            for (int x = -WIDTH / 2; x < WIDTH / 2; ++x) {
                const double v = (y + .5) / HEIGHT, u = (x + .5) / (WIDTH / 2);
                const double half = std::sqrt(std::max(0., 1 - (2 * v - 1) * (2 * v - 1))) * (1.08 - .22 * v);
                if (std::abs(u) <= half) {
                    indices[size_t(y * WIDTH + x + WIDTH / 2)] = int(pixels.size());
                    pixels.push_back({x, y, u, v, half});
                }
            }
        for (auto& e : pixels) {
            const double nx = clamp(e.u / std::max(.05, e.half), -1, 1), ny = (2 * e.v - 1) * .9;
            const double nz   = std::sqrt(std::max(0., 1 - nx * nx - ny * ny));
            const double d    = nx * -.72 + ny * .5 + nz * .48;
            const bool   edge = !isOccupied(e.x - 1, e.y) || !isOccupied(e.x + 1, e.y) || !isOccupied(e.x, e.y - 1) ||
                                !isOccupied(e.x, e.y + 1);
            int64_t      c    = d > .9    ? -1
                                : d > .62 ? signedColor(C[5])
                                : d > .32 ? signedColor(C[4])
                                : d > .02 ? signedColor(C[3])
                                : d > -.3 ? signedColor(C[2])
                                          : signedColor(C[1]);
            if (edge && nx > .25 && ny < .55)
                c = signedColor(C[3]);
            else if (rand({jsUint(e.x), jsUint(e.y), 3u}) < .08 && e.v > .12 && !edge)
                c = d > .2 ? speckle : signedColor(mix(uint32_t(speckle), C[1], .5));
            e.color = c;
            e.lit   = d;
        }
        const auto walk = [&](double x, double y, double a, int len, int stage) {
            double px = jsRound(x), py = jsRound(y);
            for (int i = 0; i < len; ++i) {
                const double side = (i % 2 ? 1 : -1) * R.range(.6, 1.3);
                a += R.range(-.25, .25);
                x += std::cos(a) * 1.8 - std::sin(a) * side * .6;
                y += std::sin(a) * 1.8 + std::cos(a) * side * .6;
                const double nx = jsRound(x), ny = jsRound(y), n = std::max(std::abs(nx - px), std::abs(ny - py));
                for (int j = 1; j <= n; ++j) {
                    const int k = index(int(jsRound(px + ((nx - px) * j) / n)), int(jsRound(py + ((ny - py) * j) / n)));
                    if (k < 0) return;
                    if (pixels[size_t(k)].crack < 0) pixels[size_t(k)].crack = stage;
                }
                px = nx;
                py = ny;
            }
        };
        walk(2, 15, -2.2, 3, 0);
        walk(2, 15, .3, 2, 0);
        walk(-2, 13, -.6, 3, 1);
        walk(-2, 13, 2.7, 3, 1);
        walk(3, 11, 1.9, 2, 1);
        walk(2, 15, 1.5, 2, 1);
        for (int x = -WIDTH / 2; x < WIDTH / 2; ++x) {
            const int i = index(x, zigAt(x));
            if (i >= 0) pixels[size_t(i)].crack = 2;
        }
        for (int stage = 0; stage < 3; ++stage)
            for (auto& e : pixels)
                for (const auto& p : pixels)
                    if (p.crack >= 0 && p.crack <= stage)
                        e.near[size_t(stage)] = std::min(e.near[size_t(stage)], jsHypot2(p.x - e.x, p.y - e.y));
    }
    /** @brief Return a stable shell pixel index, or -1 for explicit geometric absence. */
    [[nodiscard]] int index(int x, int y) const {
        return x >= -WIDTH / 2 && x < WIDTH / 2 && y >= 0 && y < HEIGHT ? indices[size_t(y * WIDTH + x + WIDTH / 2)]
                                                                        : -1;
    }
    /** @brief Test shell occupancy without changing per-instance geometry. */
    [[nodiscard]] bool isOccupied(int x, int y) const { return index(x, y) >= 0; }
    /** @brief Source zigzag seam extends at both ends through clamped column selection. */
    [[nodiscard]] static int zigAt(int x) {
        const int c = std::clamp(x + WIDTH / 2, 0, WIDTH - 1) % 3;
        return SEAM + (c == 0 ? 1 : c == 1 ? 0 : -1);
    }
};
}  // namespace eve::stylize::pixelfx::egg
