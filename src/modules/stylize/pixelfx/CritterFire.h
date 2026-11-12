// Native adaptation of pixelfx critters/_lib.js flame and ballistic helpers, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/CritterMotion.h"
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::critter {
/** @brief Sample source period-safe noise from injected time; tag truncation occurs only at the hash boundary. */
inline double loopNoise(double t, double duration, int frequency, double offset = 0, uint32_t seed = 0) {
    return tileNoise((t / duration) * frequency + offset, .5, frequency, 0, seed);
}
/** @brief Draw one or all source nested flame layers with synchronous painter/heat-ramp borrows. */
inline void flameTongue(Paint& p, double x, double y, double height, double width, double sway, double t,
                        double duration, double tag, std::span<const uint32_t> heat, int only = -1) {
    if (height < 1) return;
    const int f1 = std::max(1, int(jsRound(9 * duration))), f2 = std::max(1, int(jsRound(4 * duration)));
    constexpr std::array<std::array<double, 3>, 4> layers{
        {{{1, 1, 6}}, {{.76, .86, 4}}, {{.54, .68, 2}}, {{.3, .42, 0}}}};
    for (size_t li = 0; li < layers.size(); ++li) {
        if (only >= 0 && int(li) != only) continue;
        const auto   q = layers[li];
        const double h = height * q[1];
        const auto   c = heat[size_t(q[2])];
        for (int row = 0; row <= h; ++row) {
            const double u    = double(row) / std::max(1., h),
                         half = width * q[0] * std::pow(1 - u, .85) * std::min(1., (row + 1.5) / 3.5) +
                                loopNoise(t, duration, f1, row * .45, jsUint(tag)) * .5,
                         cx = x + sway * u * u * q[1] + loopNoise(t, duration, f2, row * .2, jsUint(tag + 7)) * u * 1.1;
            if (half < .3) continue;
            for (int px = int(jsRound(cx - half)); px <= jsRound(cx + half); ++px) p.px(px, jsRound(y - row), c);
        }
    }
}
/** @brief Draw a source cooling flame puff; caller supplies lifetime fraction, shape seed and borrowed heat ramp. */
inline void coolingFirePuff(Paint& p, double x, double y, double k, double size, double seed,
                            std::span<const uint32_t> heat) {
    const double age = 1 - k, hot = std::min(.999, std::pow(age, .62) + std::fmod(seed, 1) * .1);
    const size_t index  = std::min(heat.size() - 2, size_t(std::floor(hot * double(heat.size() - 1))));
    const double radius = size * (.55 + .45 * std::min(1., age * 7)) * (1 - age * .55);
    const auto   c      = heat[index];
    if (index >= heat.size() - 2) {
        p.disc(x, y, radius, c, std::min(1., k * 3) * .85);
        return;
    }
    p.circle(x, y, radius, heat[std::min(heat.size() - 1, index + 2)]);
    p.circle(x - radius * .16, y - radius * .22, radius * .78, c);
    if (radius > 1.6) p.circle(x, y - radius * .9, radius * .42, c);
    if (index <= 3 && radius > 1.8)
        p.circle(x - radius * .28, y - radius * .34, radius * .36, heat[index == 0 ? 0 : index - 1]);
    if (index < 6) p.glow(x, y, radius * 2.4 + 2, c, .18 * k);
}
/** @brief Owning source ballistic position, velocity and floor-contact sample. */
struct BallisticSample {
    double x, y, vx, vy;
    int    hops;
    bool   rest;
};
/** @brief Sample source bounded ballistic floor bounces from injected age, with no simulation or retained state. */
inline BallisticSample groundBounce(double x, double y, double vx, double vy, double age, double gravity,
                                    double floor = 0, double restitution = .45) {
    int hops = 0;
    for (; hops < 6; ++hops) {
        const auto f = tossSample(x, y, vx, vy, age, gravity, floor);
        if (!f.landed) return {f.x, f.y, f.vx, f.vy, hops, false};
        const double speed = vy + gravity * f.landTime;
        age -= f.landTime;
        x = f.landX;
        y = floor - .01;
        vx *= .7;
        vy = -speed * restitution;
        if (std::abs(vy) < 25) break;
    }
    return {x, floor - .25, 0, 0, hops, true};
}
}  // namespace eve::stylize::pixelfx::critter
