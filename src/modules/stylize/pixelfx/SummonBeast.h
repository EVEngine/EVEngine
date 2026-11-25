// eve-hot-path
// Native adaptation of pixelfx spawn/summon-rise.js beast geometry, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#pragma once
#include "stylize/pixelfx/Math.h"
namespace eve::stylize::pixelfx::summon {
/** @brief Immutable procedural brute geometry; materials and playback state belong to each kernel.
 * @note No actor/script/GPU pointers. Fields retain the source Float32 rounding after every blur pass.
 * @cost More expensive than a frame lookup; bounded mask construction is reused across instances. */
struct Beast {
    static constexpr int       WIDTH = 46, HEIGHT = 46, CELLS = WIDTH * HEIGHT;
    std::array<uint8_t, CELLS> part{}, crease{};
    std::array<float, CELLS>   ambient{}, down{}, rim{};
    std::vector<Point>         outline;
    std::array<Point, 4>       eyes{}, tusks{};
    std::array<Point, 2>       glints{};
    Beast() {
        constexpr double center = 22.5;
        using Mask              = std::array<uint8_t, CELLS>;
        std::array<Mask, 8> parts{};
        size_t              current = 0;
        const auto          put     = [&](double x, double y) {
            const int ix = int(jsRound(x)), iy = int(jsRound(y));
            if (ix >= 0 && iy >= 0 && ix < WIDTH && iy < HEIGHT) parts[current][size_t(iy * WIDTH + ix)] = 1;
        };
        const auto ellipse = [&](double cx, double cy, double rx, double ry) {
            for (int y = int(std::floor(cy - ry)); y <= std::ceil(cy + ry); ++y)
                for (int x = int(std::floor(cx - rx)); x <= std::ceil(cx + rx); ++x) {
                    const double dx = (x + .5 - cx) / rx, dy = (y + .5 - cy) / ry;
                    if (dx * dx + dy * dy <= 1) put(x, y);
                }
        };
        const auto stroke = [&](std::initializer_list<Point> points, double r0, double r1) {
            const std::vector<Point> pts(points);
            std::vector<double>      lengths;
            double                   total = 0;
            for (size_t i = 1; i < pts.size(); ++i) {
                lengths.push_back(jsHypot2(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y));
                total += lengths.back();
            }
            double acc = 0;
            for (size_t i = 1; i < pts.size(); ++i) {
                const int n = int(std::ceil(lengths[i - 1] * 3)) + 1;
                for (int j = 0; j <= n; ++j) {
                    const double u = double(j) / n, r = r0 + (r1 - r0) * ((acc + lengths[i - 1] * u) / total);
                    ellipse(pts[i - 1].x + (pts[i].x - pts[i - 1].x) * u, pts[i - 1].y + (pts[i].y - pts[i - 1].y) * u,
                            r, r);
                }
                acc += lengths[i - 1];
            }
        };
        for (int side : {-1, 1}) {
            const auto X = [&](double d) { return center + side * d; };
            current      = 1;
            stroke({{X(4), 11}, {X(8), 8}, {X(12), 6.5}, {X(15), 4}, {X(16), 1.5}, {X(15.2), 0}}, 2.3, .45);
            current = 3;
            ellipse(X(10), 16, 7, 5);
            ellipse(X(8.5), 19.5, 5.5, 4.2);
            current = 2;
            stroke({{X(14.5), 16.5}, {X(17.5), 22}, {X(18.5), 28}}, 4, 3.2);
            stroke({{X(18.5), 28}, {X(19), 33}, {X(19.2), 36.5}}, 3.4, 2.8);
            ellipse(X(19.2), 37.8, 3, 2.3);
            stroke({{X(17.2), 39}, {X(16.6), 41.8}}, .9, .4);
            stroke({{X(19.2), 39.5}, {X(19.3), 42.6}}, .9, .4);
            stroke({{X(21.2), 38.8}, {X(22), 41.4}}, .9, .4);
            current = 6;
            stroke({{X(5.5), 31}, {X(8), 37}, {X(7.5), 42}}, 4, 3);
            ellipse(X(8.5), 44.3, 4.2, 1.7);
        }
        current = 7;
        stroke({{center, 30},
                {center + .5, 36},
                {center + 2.5, 41.5},
                {center + 7, 44.6},
                {center + 12.5, 44.2},
                {center + 14.5, 42.5}},
               2.3, .6);
        current = 4;
        ellipse(center, 22.5, 8.2, 7.5);
        current = 5;
        ellipse(center, 29.5, 6.2, 4.5);
        current = 0;
        ellipse(center, 12.5, 5.2, 4.6);
        ellipse(center, 16, 4.2, 2.8);
        for (size_t i = 0; i < part.size(); ++i)
            for (size_t k = 0; k < parts.size(); ++k)
                if (parts[k][i]) {
                    part[i] = uint8_t(k + 1);
                    break;
                }
        const auto blur = [](const Mask& src) {
            std::array<float, CELLS> a{}, b{};
            for (size_t i = 0; i < a.size(); ++i) a[i] = float(src[i]);
            for (int pass = 0; pass < 2; ++pass) {
                for (int y = 0; y < HEIGHT; ++y)
                    for (int x = 0; x < WIDTH; ++x) {
                        double sum = 0;
                        for (int d = -2; d <= 2; ++d)
                            sum += x + d >= 0 && x + d < WIDTH ? double(a[size_t(y * WIDTH + x + d)]) : 0;
                        b[size_t(y * WIDTH + x)] = float(sum / 5);
                    }
                for (int y = 0; y < HEIGHT; ++y)
                    for (int x = 0; x < WIDTH; ++x) {
                        double sum = 0;
                        for (int d = -2; d <= 2; ++d)
                            sum += y + d >= 0 && y + d < HEIGHT ? double(b[size_t((y + d) * WIDTH + x)]) : 0;
                        a[size_t(y * WIDTH + x)] = float(sum / 5);
                    }
            }
            return a;
        };
        for (size_t k = 0; k < parts.size(); ++k) {
            const auto B  = blur(parts[k]);
            const auto at = [&](int x, int y) -> double {
                return x >= 0 && y >= 0 && x < WIDTH && y < HEIGHT ? double(B[size_t(y * WIDTH + x)]) : 0.;
            };
            for (int y = 0; y < HEIGHT; ++y)
                for (int x = 0; x < WIDTH; ++x) {
                    const size_t i = size_t(y * WIDTH + x);
                    if (part[i] != k + 1) continue;
                    const double gx = at(x + 1, y) - at(x - 1, y), gy = at(x, y + 1) - at(x, y - 1),
                                 length = jsHypot2(gx, gy), g = length == 0 ? 1e-6 : length, ox = -gx / g, oy = -gy / g,
                                 e = clamp(g / .28);
                    ambient[i]     = float(.42 + e * (.4 * std::max(0., -oy) + .14 * std::max(0., -ox) -
                                                      .3 * std::max(0., oy) - .08 * std::max(0., ox)));
                    down[i]        = float(e * std::max(0., oy) * (std::abs(x + .5 - center) < 7 && y > 27 ? .3 : 1));
                    for (const Point d : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                        const int nx = x + int(d.x), ny = y + int(d.y);
                        if (isOccupied(nx, ny) && part[size_t(ny * WIDTH + nx)] > k + 1) crease[i] = 1;
                    }
                }
        }
        for (int y = 0; y < HEIGHT; ++y)
            for (int x = 0; x < WIDTH; ++x) {
                if (!isOccupied(x, y)) continue;
                const double k             = !isOccupied(x, y + 1) ? 1 : !isOccupied(x, y + 2) ? .5 : 0;
                rim[size_t(y * WIDTH + x)] = float(std::abs(x + .5 - center) < 7 && y > 27 ? k * .3 : k);
            }
        for (int y = -1; y <= HEIGHT; ++y)
            for (int x = -1; x <= WIDTH; ++x)
                if (!isOccupied(x, y) &&
                    (isOccupied(x - 1, y) || isOccupied(x + 1, y) || isOccupied(x, y - 1) || isOccupied(x, y + 1)))
                    outline.push_back({double(x), double(y)});
        size_t i = 0;
        for (int side : {-1, 1}) {
            eyes[i]    = {jsRound(center + side * 2.2 - .5), 12};
            tusks[i++] = {jsRound(center + side * 3.4 - .5), 16};
            eyes[i]    = {jsRound(center + side * 3.2 - .5), 12};
            tusks[i++] = {jsRound(center + side * 3.6 - .5), 15};
        }
        glints = {{{jsRound(center - 2.7 - .5) - double(WIDTH) / 2, 12 - HEIGHT},
                   {jsRound(center + 2.7 - .5) - double(WIDTH) / 2, 12 - HEIGHT}}};
    }
    /** @brief Read complete silhouette occupancy without retaining a cell pointer. */
    [[nodiscard]] bool isOccupied(int x, int y) const {
        return x >= 0 && y >= 0 && x < WIDTH && y < HEIGHT && part[size_t(y * WIDTH + x)] > 0;
    }
};
/** @brief Borrow immutable process-lifetime geometry; no material/playback state is shared.
 * @cost More expensive on the first cold preparation; bounded geometry is cached across instances/frames. */
[[nodiscard]] inline const Beast& beast() {
    static const Beast geometry;
    return geometry;
}
}  // namespace eve::stylize::pixelfx::summon
