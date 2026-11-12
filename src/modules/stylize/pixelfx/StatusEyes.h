// eve-hot-path
// Native adaptation of pixelfx status/_lib.js eyes, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#pragma once
#include <algorithm>
#include <array>
#include <utility>
#include <vector>
#include "stylize/pixelfx/StatusShape.h"
namespace eve::stylize::pixelfx::status {
/** @brief Owning source eye component; its skin color and pixel order remain fixed for the prepared actor. */
struct Eye {
    int                             x0, x1, y0, y1;
    std::vector<std::array<int, 2>> pixels;
    uint32_t                        skin;
};
/** @brief Prepare declared eyes, or the original stock Hero connected-component detector.
 * @note Custom v1 snapshots carry an explicit eyes array: empty means none, unlike stock Hero's omitted field.
 * Caller-thread affine; no actor pointers or mutable global caches are retained.
 * @cost Linear bounded silhouette traversal and small per-component color counting once per configuration. */
[[nodiscard]] inline std::vector<Eye> eyes(const PixelFxActor& a, const Shape& shape) {
    std::vector<Eye> result;
    if (a.source != PixelFxActorSource::Hero) {
        for (const auto& q : a.eyes)
            result.push_back({q[0], q[0], q[1], q[1] + 1, {{q[0], q[1]}, {q[0], q[1] + 1}}, colorAt(a, q[0], q[1])});
        return result;
    }
    const auto col  = [&](int x, int y) { return shape.isOccupied(x, y) ? colorAt(a, x, y) : 0u; };
    const auto dark = [&](int x, int y) {
        return shape.isOccupied(x, y) && y < shape.height * .4 && luma(col(x, y)) < .15 && shape.isOccupied(x - 1, y) &&
               shape.isOccupied(x + 1, y) && shape.isOccupied(x, y - 1) && shape.isOccupied(x, y + 1);
    };
    std::vector<uint8_t> seen(size_t(shape.width) * size_t(shape.height), 0);
    for (const auto& start : shape.body) {
        if (seen[size_t(start[1] * shape.width + start[0])] || !dark(start[0], start[1])) continue;
        std::vector<std::array<int, 2>> pixels, stack{start};
        seen[size_t(start[1] * shape.width + start[0])] = 1;
        while (!stack.empty()) {
            const auto q = stack.back();
            stack.pop_back();
            pixels.push_back(q);
            for (const auto delta : std::array<std::array<int, 2>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                const int nx = q[0] + delta[0], ny = q[1] + delta[1];
                if (!shape.isOccupied(nx, ny)) continue;
                const auto index = size_t(ny * shape.width + nx);
                if (seen[index] || !(dark(nx, ny) || luma(col(nx, ny)) > .95)) continue;
                seen[index] = 1;
                stack.push_back({nx, ny});
            }
        }
        if (pixels.size() < 2 || pixels.size() > 14) continue;
        Eye                                   eye{pixels[0][0], pixels[0][0], pixels[0][1], pixels[0][1], pixels, 0};
        std::vector<std::pair<uint32_t, int>> counts;
        for (const auto& q : pixels) {
            eye.x0 = std::min(eye.x0, q[0]);
            eye.x1 = std::max(eye.x1, q[0]);
            eye.y0 = std::min(eye.y0, q[1]);
            eye.y1 = std::max(eye.y1, q[1]);
            for (const auto delta :
                 std::array<std::array<int, 2>, 6>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {2, 0}, {-2, 0}}}) {
                const std::array<int, 2> at{q[0] + delta[0], q[1] + delta[1]};
                const uint32_t           c     = col(at[0], at[1]);
                const double             level = luma(c);
                if (!c || level <= .5 || level >= .95 || std::find(pixels.begin(), pixels.end(), at) != pixels.end())
                    continue;
                auto it = std::find_if(counts.begin(), counts.end(), [&](auto q2) { return q2.first == c; });
                if (it == counts.end())
                    counts.push_back({c, 1});
                else
                    ++it->second;
            }
        }
        std::stable_sort(counts.begin(), counts.end(),
                         [](auto left, auto right) { return left.second > right.second; });
        if (!counts.empty()) eye.skin = counts[0].first;
        if (eye.skin && eye.x1 - eye.x0 <= 4 && eye.y1 - eye.y0 <= 5) result.push_back(std::move(eye));
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const auto& left, const auto& right) { return left.x0 < right.x0; });
    if (result.size() > 2) result.clear();
    return result;
}
}  // namespace eve::stylize::pixelfx::status
