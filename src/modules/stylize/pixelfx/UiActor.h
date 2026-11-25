// eve-hot-path
// Native adaptation of pixelfx ui/_lib.js tintActor/lightActor/haloActor, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#pragma once
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::ui {
/** @brief Recolor a synchronously borrowed prepared actor by source luminance thresholds.
 * @note Owner-thread only; no retained actor/painter references or callbacks.
 * @cost Linear in prepared mask cells per explicit draw; no allocation. */
inline void tintActor(Paint& p, const PixelFxActor& actor, double k, std::span<const uint32_t, 3> ramp) {
    if (actor.source == PixelFxActorSource::None || k <= .12) return;
    const double cut = k > .6 ? 0 : k > .3 ? .22 : .5;
    for (int j = 0; j < actor.height; ++j)
        for (int i = 0; i < actor.width; ++i) {
            const size_t q = size_t(j) * size_t(actor.width) + size_t(i);
            if (!actor.mask[q]) continue;
            const double l = actor.pixels.empty() ? .6 : luma(actor.pixels[q]);
            if (l < cut) continue;
            p.px(actor.x + i, actor.y + j, l > .5 ? (k > .8 ? WHITE : ramp[2]) : l > .22 ? ramp[1] : ramp[0]);
        }
}
/** @brief Apply source additive body light and interior rim mixing to a prepared actor.
 * @note Owner-thread only; synchronous borrows, no RNG, allocation or retained state.
 * @cost Linear in prepared mask cells per explicit draw, independent of display scaling. */
inline void lightActor(Paint& p, const PixelFxActor& actor, uint32_t color, double k, double rim = 0, double time = 0) {
    if (actor.source == PixelFxActorSource::None || (k <= .01 && rim <= .01)) return;
    const auto has = [&](int i, int j) {
        return i >= 0 && j >= 0 && i < actor.width && j < actor.height &&
               actor.mask[size_t(j) * size_t(actor.width) + size_t(i)] != 0;
    };
    for (int j = 0; j < actor.height; ++j)
        for (int i = 0; i < actor.width; ++i) {
            if (!has(i, j)) continue;
            if (k > .01) p.light(actor.x + i, actor.y + j, color, k * (1.1 - (.5 * j) / actor.height));
            if (rim <= .01 || !has(i - 1, j) || !has(i + 1, j) || !has(i, j - 1)) continue;
            if (has(i - 2, j) && has(i + 2, j) && has(i, j - 2)) continue;
            const double f = rim * (.75 + .35 * noise(j * .6 - time * 8, uint32_t(i * 3)));
            if (f > .25) {
                const auto pixel =
                    actor.pixels.empty() ? color : actor.pixels[size_t(j) * size_t(actor.width) + size_t(i)];
                p.px(actor.x + i, actor.y + j, mix(pixel ? pixel : color, color, std::min(.85, f)));
            }
        }
}
/** @brief Draw the source flickering one-cell outer aura around a prepared actor.
 * @note Owner-thread only; actor/painter borrows expire on return; no RNG or mutation.
 * @cost Linear in prepared actor bounding cells per explicit draw; no allocation. */
inline void haloActor(Paint& p, const PixelFxActor& actor, uint32_t color, double k, double time = 0) {
    if (actor.source == PixelFxActorSource::None || k <= .02) return;
    p.outline({actor.width, actor.height, actor.mask}, actor.x, actor.y, color,
              [&](int mx, int my) { return k * (.75 + .45 * noise(my * .45 - time * 7, uint32_t(mx * 3))); });
}
}  // namespace eve::stylize::pixelfx::ui
