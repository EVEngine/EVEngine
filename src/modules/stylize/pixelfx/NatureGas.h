// eve-hot-path
// Native adaptation of pixelfx nature/_lib.js Gas, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#pragma once
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx::nature {
/** @brief Owning gas launch sampled against the injected effect clock. */
struct GasLaunch {
    double x, y, vx, vy, r0, r1, t0, end, rise, seed;
};
/** @brief Immutable gas geometry evaluated for one draw time. */
struct GasSample {
    double x, y, r, a, u;
};
/** @brief Source layered gas puff; painter is borrowed only for this call.
 * @cost Five bounded discs per visible puff. */
inline void gasPuff(Paint& p, double x, double y, double r, double a, uint32_t belly, uint32_t body, uint32_t cap,
                    uint32_t hi = 0) {
    if (a < .02 || r < .6) return;
    p.disc(x + r * .06, y + r * .28, r * .9, belly, a * .55);
    p.disc(x, y, r, body, a * .45);
    p.disc(x + r * .03, y - r * .02, r * .76, body, a * .5);
    p.disc(x - r * .2, y - r * .34, r * .52, cap, a * .45);
    if (hi) p.disc(x - r * .3, y - r * .55, r * .26, hi, a * .32);
}
/** @brief Sole owner of source gas launches; caller-thread affine, no external references.
 * @cost Storage and drawing linear in live launches; append amortizes vector growth. */
struct Gas {
    std::vector<GasLaunch> list;
    uint64_t               n = 0;
    /** @brief Append one launch with deterministic insertion phase. */
    void add(double t, double x, double y, double vx, double vy, double r0, double r1, double end, double rise = 0) {
        list.push_back({x, y, vx, vy, r0, r1, t, end, rise, std::fmod(double(n++) * 37.7, 100.)});
    }
    /** @brief Remove strictly expired launches in source reverse order. */
    void prune(double t) {
        for (size_t i = list.size(); i-- > 0;)
            if (t > list[i].end) list.erase(list.begin() + std::ptrdiff_t(i));
    }
    /** @brief Evaluate a synchronously borrowed launch without mutating its state. */
    [[nodiscard]] static GasSample at(const GasLaunch& q, double t) {
        const double age = t - q.t0, e = 1 - std::exp(-age * 2.2), sw = std::min(1., age);
        const double x = q.x + (q.vx / 2.2) * e + std::sin(t * 1.7 + q.seed) * 2.5 * sw;
        const double y = q.y + (q.vy / 2.2) * e - q.rise * age + std::sin(t * 1.3 + q.seed * 2) * 1.5 * sw;
        const double u = clamp(age / std::max(.01, q.end - q.t0));
        const double r = q.r0 + (q.r1 - q.r0) * (1 - std::pow(1 - std::min(1., age / 1.2), 3));
        return {x, y, r, clamp(age / .15) * (1 - clamp((u - .4) / .6)), u};
    }
    /** @brief Draw insertion-ordered layered puffs against the injected clock; retains no painter. */
    void draw(Paint& p, double t, uint32_t belly, uint32_t body, uint32_t cap, uint32_t hi = 0, double k = 1,
              uint32_t glow = 0) const {
        for (const auto& q : list) {
            const auto g = at(q, t);
            if (g.a <= .02) continue;
            gasPuff(p, g.x, g.y, g.r, g.a * k, belly, body, cap, g.u < .5 ? hi : 0);
            if (glow && g.r > 4) p.glow(g.x, g.y, g.r * 1.6, glow, .07 * g.a * k);
        }
    }
};
}  // namespace eve::stylize::pixelfx::nature
