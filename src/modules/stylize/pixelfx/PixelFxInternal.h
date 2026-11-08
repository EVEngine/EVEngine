// Native pixelfx implementation helpers. MIT; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Painter.h"

namespace eve::stylize::pixelfx {
template <class F>
void cycle(double t, double period, int count, uint32_t seed, double lo, double hi, F fn, bool beat = false) {
    for (int i = 0; i < count; ++i) {
        Rng          g(hash({seed, uint32_t(i), 911u}));
        const double born = beat ? double(i) * period / count - 1e-7 : g.next() * period;
        const double life = g.range(lo, hi);
        for (double age = wrap(t - born, period); age < life; age += period) {
            Rng r(hash({seed, uint32_t(i)}));
            fn(age, life, r, i);
        }
    }
}
/** @brief Visit source constant-lifetime cycle copies with synchronous callback borrows. */
template <class F>
void cycle(double t, double period, int count, uint32_t seed, double life, F fn) {
    for (int i = 0; i < count; ++i) {
        Rng          g(hash({seed, uint32_t(i), 911u}));
        const double born = g.next() * period;
        for (double age = wrap(t - born, period); age < life; age += period) {
            Rng r(hash({seed, uint32_t(i)}));
            fn(age, life, r, i);
        }
    }
}
void fireDefaults(PixelFxSettings& s, PixelFxKind kind);
void fireBreath(Paint& p, double t, const PixelFxSettings& o);
void flamethrower(Paint& p, double t, const PixelFxSettings& o);
void fireTornado(Paint& p, double t, const PixelFxSettings& o);
void fireball(Paint& p, double t, const PixelFxSettings& o);
void spiritFlame(Paint& p, double t, const PixelFxSettings& o);
void brazier(Paint& p, double t, const PixelFxSettings& o);
void torch(Paint& p, double t, const PixelFxSettings& o);
void candle(Paint& p, double t, const PixelFxSettings& o);
void halo(Paint& p, double t, const PixelFxSettings& o);
void sunburst(Paint& p, double t, const PixelFxSettings& o);
void laserBolt(Paint& p, double t, const PixelFxSettings& o);
void impactFrame(Paint& p, double t, const PixelFxSettings& o);
}  // namespace eve::stylize::pixelfx
