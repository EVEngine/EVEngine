// eve-hot-path
// Native adaptation of pixelfx status/_lib.js shape/colorAt/travel, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#pragma once
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/Math.h"

namespace eve::stylize::pixelfx::status {
/** @brief Owning mask-space geometry prepared once for a validated actor snapshot.
 * @note Caller-thread affine; no input pointers, callbacks or process-global mutable caches.
 * @cost Linear bounded-mask scan and allocation once per configuration, amortized across draws. */
struct Shape {
    int                                width, height;
    std::vector<uint8_t>               mask;
    std::vector<std::array<int, 2>>    body, up, rows;
    std::vector<std::array<int, 3>>    side;
    std::vector<std::array<double, 4>> outline;
    std::vector<int>                   top;
    /** @brief Query the immutable source silhouette; coordinates outside the mask are empty. */
    [[nodiscard]] bool isOccupied(int x, int y) const {
        return x >= 0 && y >= 0 && x < width && y < height && mask[size_t(y) * size_t(width) + size_t(x)] != 0;
    }
    /** @brief Copy validated actor occupancy and derive source-ordered lists without retaining the actor.
     * @cost Linear bounded-mask preparation and allocation, amortized over all instance draws and seeks. */
    explicit Shape(const PixelFxActor& a) : width(a.width), height(a.height), mask(a.mask) {
        for (int x = 0; x < width; ++x) {
            int y = 0;
            while (y < height && !isOccupied(x, y)) ++y;
            top.push_back(y < height ? y : -1);
        }
        for (int y = 0; y < height; ++y) {
            int left = -1, right = -1;
            for (int x = 0; x < width; ++x) {
                if (!isOccupied(x, y)) continue;
                if (left < 0) left = x;
                right = x;
                body.push_back({x, y});
                if (!isOccupied(x, y - 1)) up.push_back({x, y});
                if (!isOccupied(x - 1, y)) side.push_back({x, y, -1});
                if (!isOccupied(x + 1, y)) side.push_back({x, y, 1});
            }
            rows.push_back({left, right});
        }
        for (int y = -1; y <= height; ++y)
            for (int x = -1; x <= width; ++x) {
                if (isOccupied(x, y)) continue;
                double nx = 0, ny = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (isOccupied(x + dx, y + dy)) {
                            nx -= dx;
                            ny -= dy;
                        }
                if (isOccupied(x - 1, y) || isOccupied(x + 1, y) || isOccupied(x, y - 1) || isOccupied(x, y + 1)) {
                    const double length      = jsHypot2(nx, ny);
                    const double denominator = length != 0 ? length : 1;
                    outline.push_back({double(x), double(y), nx / denominator, ny / denominator});
                }
            }
    }
};
/** @brief Borrow a validated actor pixel synchronously; raw zero/alpha bits remain source data. */
[[nodiscard]] inline uint32_t colorAt(const PixelFxActor& actor, int x, int y) {
    return actor.pixels[size_t(y) * size_t(actor.width) + size_t(x)];
}
/** @brief Evaluate the source closed-form displacement with injected age and linear drag. */
[[nodiscard]] inline double travel(double v, double g, double drag, double age) {
    return drag > 0 ? (g / drag) * age + ((v - g / drag) * (1 - std::exp(-drag * age))) / drag
                    : v * age + .5 * g * age * age;
}
}  // namespace eve::stylize::pixelfx::status
