// eve-hot-path
// Native adaptation of pixelfx status/_lib.js periodic fire recipes, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#pragma once
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx::status {
/** @brief Evaluate source one-dimensional noise with a wrapped lattice and injected seed. */
[[nodiscard]] inline double wnoise(double x, double period, uint32_t seed = 0) {
    const double i = std::floor(x), f = x - i, u = f * f * (3 - 2 * f);
    const double a = rand({jsUint(wrap(i, period)), seed}), b = rand({jsUint(wrap(i + 1, period)), seed});
    return (a + (b - a) * u) * 2 - 1;
}
/** @brief Evaluate source periodic time noise without accumulating time or retaining state. */
[[nodiscard]] inline double tnoise(double time, double duration, double frequency, uint32_t seed = 0,
                                   double offset = 0) {
    const double period = std::max(2., jsRound(frequency * duration));
    return wnoise((time / duration) * period + offset, period, seed);
}
/** @brief Draw the source four-layer flame tongue using supplied time, geometry and immutable ramp.
 * @note Borrows target/ramp synchronously; no callbacks, allocations or retained references.
 * @cost Raster work linear in authored flame height times row width per logical-pixel draw. */
inline void tongue(Paint& p, double x, double y, double height, double width, double sweep,
                   std::span<const uint32_t> ramp, double time, double duration, uint32_t seed) {
    if (height < 1) return;
    constexpr std::array<std::array<double, 3>, 4> layers{{{1, 1, 6}, {.76, .86, 4}, {.54, .68, 2}, {.3, .42, 0}}};
    for (const auto& layer : layers) {
        if (layer[2] == 0 && height < 4) continue;
        const double   H     = height * layer[1];
        const uint32_t color = ramp[size_t(layer[2])];
        for (int row = 0; row <= H; ++row) {
            const double u    = row / std::max(1., H),
                         half = width * layer[0] * std::pow(1 - u, .85) * std::min(1., (row + 1.5) / 3.5) +
                                tnoise(time, duration, 9, seed, row * .45) * .5;
            if (half < .3) continue;
            const double cx = x + sweep * u * u * layer[1] + tnoise(time, duration, 4, seed + 7u, row * .2) * u * 1.1;
            for (int px = int(jsRound(cx - half)); px <= jsRound(cx + half); ++px) p.px(px, jsRound(y - row), color);
        }
    }
}
/** @brief Draw the source age-derived smoke puff, including the optional warm underside.
 * @note Input age is authored in0..1; borrows are synchronous and no simulation state is created.
 * @cost Touches the puff's clipped logical disc area per draw, amortized by low-resolution rendering. */
inline void smoke(Paint& p, double x, double y, double size, double age, uint32_t color, double gain = 1,
                  uint32_t warm = 0) {
    const double radius = size * (.65 + std::min(1., age * 2.5) * .45 + age * .4),
                 alpha  = std::min(1., age * 12) * std::min(1., (1 - age) * 2.4) * .58 * gain;
    if (alpha <= .01) return;
    const auto at = [](double u) {
        return SMOKE_RAMP[size_t(
            std::min(double(SMOKE_RAMP.size() - 1), std::max(0., std::floor(u * double(SMOKE_RAMP.size())))))];
    };
    const uint32_t c = color ? mix(color, at(age), age) : at(age * 1.05);
    p.disc(x, y, radius, c, alpha * .45);
    p.disc(x + radius * .05, y + radius * .06, radius * .74, c, alpha * .62);
    if (warm && age < .6)
        p.disc(x + radius * .1, y + radius * .3, radius * .56, mix(c, warm, .5), alpha * .7 * (1 - age / .6));
    if (age < .5)
        p.disc(x - radius * .22, y - radius * .3, radius * .5, mix(c, WHITE, .12), alpha * .4 * (1 - age * 2));
}
/** @brief Draw the source age-derived ember with injected phase and three immutable colors. */
inline void ember(Paint& p, double x, double y, double age, const std::array<uint32_t, 3>& colors, double phase,
                  double gain = 1) {
    const double left = 1 - age;
    if (left < .22 && std::sin(phase * 40) < 0) return;
    const uint32_t c = left > .5    ? (std::sin(phase * 22) > -.2 ? colors[0] : colors[1])
                       : left > .22 ? colors[1]
                                    : colors[2];
    p.px(x, y, c);
    p.glow(x, y, 3, c, .32 * left * gain);
}
}  // namespace eve::stylize::pixelfx::status
