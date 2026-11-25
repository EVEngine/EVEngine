// eve-hot-path
// Native adaptation of pixelfx nature/_lib.js stoneShape/drawRock, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#pragma once
#include "common/Assert.h"
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::nature {
/** @brief Own the source stone's bounded angular corner sequence; no painter state is retained. */
struct StoneShape {
    std::array<Point, 8> points{};
    size_t               count = 0;
};
/** @brief Prepare source corners with explicit injected RNG and a compiled 5..8-corner count.
 * @cost Constant bounded trigonometry per emitted stone, amortized over its lifetime. */
inline StoneShape stoneShape(Rng& rng, int count) {
    EV_ASSERT((count >= 5 && count <= 8));
    StoneShape shape;
    shape.count     = size_t(count);
    const double a0 = rng.range(0, TAU), squash = rng.range(.7, 1);
    for (int i = 0; i < count; ++i) {
        const double a = a0 + ((i + rng.range(-.28, .28)) * TAU) / count, r = rng.range(.72, 1.12);
        shape.points[size_t(i)] = {std::cos(a) * r, std::sin(a) * r * squash};
    }
    return shape;
}
/** @brief Prepare the source random-corner stone, consuming the corner-count sample first.
 * @cost Same fixed bounded preparation as stoneShape, once per emission. */
inline StoneShape randomStoneShape(Rng& rng) {
    const int count = rng.integer(5, 7);
    return stoneShape(rng, count);
}
/** @brief Query the source polygon interior with its original edge crossing order. */
inline bool isInsideStone(std::span<const Point> vertices, double x, double y) {
    bool inside = false;
    for (size_t i = 0, j = vertices.size() - 1; i < vertices.size(); j = i++) {
        const auto a = vertices[i], b = vertices[j];
        if ((a.y > y) != (b.y > y) && x < ((b.x - a.x) * (y - a.y)) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}
/** @brief Draw source 3..8-corner faceted stone, lit/shaded rim and optional white flash with local scratch ownership.
 * @note Caller-thread painter/ramp borrows only; no callbacks or references retained. Ramp has 7+ stops.
 * @cost Visits at most 62x62 source mask cells times at most eight facets per draw; no geometry allocation. */
inline void drawRock(Paint& p, double x, double y, double size, double angle, const StoneShape& shape,
                     std::span<const uint32_t> ramp, double alpha = 1, bool white = false) {
    EV_ASSERT((shape.count >= 3 && shape.count <= 8 && ramp.size() >= 7));
    const size_t top = ramp.size() - 2;
    const double ca = std::cos(angle), sa = std::sin(angle);
    if (size < 1.4) {
        p.dot(x, y, ramp[top - 1], alpha);
        p.dot(x + 1, y, ramp[top - 3], alpha);
        return;
    }
    std::array<Point, 8>  vertices{};
    std::array<double, 8> angles{}, lit{};
    for (size_t i = 0; i < shape.count; ++i) {
        const auto q = shape.points[i];
        vertices[i]  = {(q.x * ca - q.y * sa) * size, (q.x * sa + q.y * ca) * size};
        angles[i]    = std::atan2(vertices[i].y, vertices[i].x);
    }
    for (size_t i = 0; i < shape.count; ++i) {
        const auto   a = vertices[i], b = vertices[(i + 1) % shape.count];
        const double ex = b.x - a.x, ey = b.y - a.y, raw = jsHypot2(ex, ey), length = raw == 0 ? 1 : raw;
        double       nx = ey / length, ny = -ex / length;
        if (nx * (a.x + b.x) + ny * (a.y + b.y) < 0) {
            nx = -nx;
            ny = -ny;
        }
        lit[i] = nx * -.6 + ny * -.8;
    }
    const int                    x0 = int(std::floor(x)) - int(std::ceil(size * 1.25)) - 1,
                                 y0 = int(std::floor(y)) - int(std::ceil(size * 1.25)) - 1,
                                 n  = std::min(62, 2 * int(std::ceil(size * 1.25)) + 3);
    std::array<uint8_t, 64 * 64> mask{}, face{};
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const double dx = x0 + i + .5 - x, dy = y0 + j + .5 - y;
            const size_t k = size_t(j * 64 + i);
            mask[k]        = isInsideStone(std::span(vertices).first(shape.count), dx, dy) ? 1 : 0;
            if (!mask[k]) continue;
            const double g = std::atan2(dy, dx);
            for (size_t q = 0; q < shape.count; ++q) {
                const double a0 = angles[q], a1 = angles[(q + 1) % shape.count];
                double       d = std::fmod(g - a0, TAU), span = std::fmod(a1 - a0, TAU);
                if (d < 0) d += TAU;
                if (span < 0) span += TAU;
                if (d <= span) {
                    face[k] = uint8_t(q);
                    break;
                }
            }
        }
    const auto at = [&](int i, int j) { return i >= 0 && j >= 0 && i < n && j < n && mask[size_t(j * 64 + i)] == 1; };
    for (int j = -1; j <= n; ++j)
        for (int i = -1; i <= n; ++i) {
            const int X = x0 + i, Y = y0 + j;
            if (!at(i, j)) {
                if (at(i - 1, j) || at(i + 1, j) || at(i, j - 1) || at(i, j + 1)) p.dot(X, Y, ramp[0], alpha);
                continue;
            }
            if (white) {
                p.dot(X, Y, WHITE, alpha);
                continue;
            }
            const double L    = lit[face[size_t(j * 64 + i)]];
            const bool   edge = !at(i - 1, j) || !at(i + 1, j) || !at(i, j - 1) || !at(i, j + 1);
            uint32_t     c    = L > .55 ? ramp[top] : L > .15 ? ramp[top - 1] : L > -.3 ? ramp[top - 2] : ramp[top - 3];
            if (edge) c = L > .3 ? ramp[top + 1] : L < -.2 ? ramp[std::max(size_t(1), top - 4)] : c;
            p.dot(X, Y, c, alpha);
        }
}
}  // namespace eve::stylize::pixelfx::nature
