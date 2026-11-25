// Native adaptation of pixelfx projectiles/_lib.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::projectile {
struct Motion {
    double x, y, vx, vy;
};
inline Motion wake(double x, double y, double vx, double vy, double age, double V, double k, double g = 0) {
    const double e = std::exp(-k * age), f = (1 - e) / k, vt = g / k;
    return {x + vx * f - V * age, y + vt * age + (vy - vt) * f, vx * e - V, vt + (vy - vt) * e};
}
inline void spark(Paint& p, Motion m, double k, const std::array<uint32_t, 4>& S, double glow = .45,
                  double stretch = .022, double max = 9) {
    double sp = jsHypot2(m.vx, m.vy);
    if (sp == 0) sp = 1;
    const double   ux = m.vx / sp, uy = m.vy / sp, len = std::min(max, 1 + sp * stretch);
    const uint32_t head = k > .6 ? S[0] : k > .3 ? S[1] : S[2];
    p.line(m.x - ux * len, m.y - uy * len, m.x - ux * len * .4, m.y - uy * len * .4, k > .5 ? head : S[3]);
    p.line(m.x - ux * len * .45, m.y - uy * len * .45, m.x, m.y, head);
    p.glow(m.x, m.y, 3, head, glow * k);
}
struct Puff {
    double x, y, r, a, b, w;
};
/** @brief Composite trusted native puffs onto a synchronously borrowed painter.
 * @param list Positive-radius authored puffs; ramp/warm are equal nonempty prepared color ramps.
 * @note Caller-thread affine; no callbacks or retained references. Scratch grids are call-owned.
 * @cost Allocates density grids and rasterizes the puff bounding area per explicit draw. */
void cloud(Paint& p, std::span<const Puff> list, std::span<const uint32_t> ramp, std::span<const uint32_t> warm,
           double density, double maximum, double lighting = 1);

struct SmokeParticle {
    double x = 0, y = 0, vx = 0, vy = 0, r0 = 1, r1 = 4, life = 1, drag = 2.5, rise = 12, grow = 2, curl = 0, a = .6,
           b = .85, warm = 0, delay = 0, age = 0, seed = 0;
};
struct Smoke {
    std::vector<SmokeParticle> list;
    double                     time = 0;
    /** @brief Append an owning authored puff and consume one value from the synchronously borrowed RNG.
     * @note Owner-thread only; no RNG or caller storage survives this call.
     * @cost Amortized constant append per puff; owned storage may grow at spawn time. */
    void add(Rng& rng, SmokeParticle q);
    /** @brief Advance owned particles using the injected fixed positive tick.
     * @note Owner-thread only; no callbacks or external state. */
    void update(double dt);
    /** @brief Return an independent owning raster snapshot; does not advance the simulation.
     * @return Visible puff records, retaining no references to the owner.
     * @cost Allocates and copies one bounded record per live puff per explicit draw.
     * @note Owner-thread only; no callbacks. */
    [[nodiscard]] std::vector<Puff> puffs() const;
};
struct Lobe {
    double off, angle, length, width, core;
};
struct FlashOptions {
    double                size = 1;
    std::array<double, 2> spike{12, 20}, diagonal{5, 10}, cross{3, 6}, back{2, 4.5};
    double                core = .7;
};
/** @brief Build owning native flash lobes for a trusted authored frame and options.
 * @param rng Synchronously borrowed named RNG, advanced in source order and never retained.
 * @return Independent geometry; no references to input options survive.
 * @cost Allocates at most eight lobe records once per requested flash frame.
 * @note Caller-thread only; no callbacks or global mutable state. */
[[nodiscard]] std::vector<Lobe> flashShape(Rng& rng, int frame, const FlashOptions& options);
/** @brief Rasterize trusted lobe geometry; all spans and the painter are synchronous borrows.
 * @cost Work scales with bounded lobe count times local raster area per flash draw.
 * @note Caller-thread only; no retained references or callbacks. */
void flash(Paint& p, double x, double y, double angle, std::span<const Lobe> lobes,
           const std::array<uint32_t, 4>& colors, uint32_t seed);
struct Casing {
    double x, y, angle;
    bool   rest;
    double hit;
};
/** @brief Evaluate a casing at injected age with at most six analytic floor collisions.
 * @return Owning position, spin, rest state and time since impact; no mutable state is retained. */
[[nodiscard]] Casing casing(double x, double y, double vx, double vy, double spin, double age, double gravity,
                            double floor, double bounce, double angle);
/** @brief Draw trusted casing data with a prepared four-color brass ramp.
 * @note Painter and ramp are synchronous borrows on their owner thread; no callbacks. */
void drawCasing(Paint& p, Casing casing, std::span<const uint32_t> brass, double fade);
}  // namespace eve::stylize::pixelfx::projectile
