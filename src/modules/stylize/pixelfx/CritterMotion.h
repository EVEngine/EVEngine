// Native adaptation of pixelfx critters/_lib.js toss and spring, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Math.h"
namespace eve::stylize::pixelfx::critter {
/** @brief Owning source ballistic sample, including floor landing time/position. */
struct FlightSample {
    double x, y, vx, vy;
    bool   landed;
    double landTime, landX;
};
/** @brief Sample the source ballistic trajectory and hold it at the floor after landing; no state retained. */
inline FlightSample tossSample(double x, double y, double vx, double vy, double age, double gravity, double floor = 0) {
    const double land =
        gravity > 0 ? (-vy + std::sqrt(std::max(0., vy * vy + 2 * gravity * (floor - y)))) / gravity : INFINITY;
    if (age < land)
        return {x + vx * age, y + vy * age + .5 * gravity * age * age, vx, vy + gravity * age, false, land,
                x + vx * land};
    return {x + vx * land, floor, 0, 0, true, land, x + vx * land};
}
/** @brief Sample a source damped spring from injected age, cycles-per-second frequency and decay. */
inline double dampedSpring(double age, double frequency, double damping) {
    return age < 0 ? 0 : std::exp(-age * damping) * std::cos(age * frequency * TAU);
}
/** @brief Sample the source piecewise smooth curve using synchronous node borrows.
 * @pre Nodes are nonempty and ordered by nondecreasing x; Point.y contains the value. */
inline double sampleCurve(double x, std::span<const Point> nodes) {
    if (x <= nodes.front().x) return nodes.front().y;
    for (size_t i = 1; i < nodes.size(); ++i) {
        const auto a = nodes[i - 1], b = nodes[i];
        if (x <= b.x) {
            const double width = b.x - a.x, u = (x - a.x) / (width == 0 ? 1 : width);
            return a.y + (b.y - a.y) * u * u * (3 - 2 * u);
        }
    }
    return nodes.back().y;
}
}  // namespace eve::stylize::pixelfx::critter
