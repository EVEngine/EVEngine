// eve-hot-path
// Native adaptation of pixelfx nature/_lib.js Drops, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#pragma once
#include <type_traits>
#include <utility>
#include "common/Assert.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx::nature {
/** @brief One owning source liquid trajectory, including delayed activation. */
struct Drop {
    double x, y, vx, vy, size = 1, delay = 0;
};
/** @brief Own source liquid drops with injected floor/time and synchronous landing observation.
 * @note Caller-thread only; callbacks cannot mutate/reenter this collection or retain its borrowed drop.
 * @cost Storage linear in live drops; updates visit drops once with vector-erasure shifts. */
struct Drops {
    std::vector<Drop> list;
    /** @brief Append one owning trajectory. @cost Amortized vector growth per emitted drop. */
    void add(Drop drop) {
        EV_ASSERT((drop.size > 0));
        list.push_back(drop);
    }
    /** @brief Advance in reverse insertion order and observe a floor crossing before erasure.
     * @param dt Finite nonnegative source step. @param floor Synchronously borrowed floor sampler.
     * @param onLand Synchronously borrowed observer; returns void and retains no trajectory reference. */
    template <class Floor, class Land>
    void update(double dt, Floor floor, Land onLand) {
        EV_ASSERT((std::isfinite(dt) && dt >= 0));
        static_assert(std::is_void_v<std::invoke_result_t<Land&, const Drop&, double>>,
                      "Landing observers must return void; critical results must not be discarded");
        for (size_t i = list.size(); i-- > 0;) {
            auto& d = list[i];
            if (d.delay > 0) {
                d.delay -= dt;
                continue;
            }
            const double drag = .6 / d.size, py = d.y;
            d.vy += GRAVITY * dt;
            d.vx -= d.vx * drag * dt;
            d.vy -= d.vy * drag * .5 * dt;
            d.x += d.vx * dt;
            d.y += d.vy * dt;
            const double g = floor(d.x);
            if (d.vy > 0 && py <= g && d.y >= g) {
                onLand(std::as_const(d), g);
                list.erase(list.begin() + std::ptrdiff_t(i));
            }
        }
    }
    /** @brief Draw the source moving bead, direction-aligned tail and glow.
     * @note Painter/ramp are synchronous borrows; the source liquid ramp has at least five colors.
     * @cost Constant bounded primitive work per live trajectory without geometry allocation. */
    void draw(Paint& p, std::span<const uint32_t> C) const {
        EV_ASSERT((C.size() >= 5));
        const size_t n = C.size();
        for (const auto& d : list) {
            if (d.delay > 0) continue;
            const double raw = jsHypot2(d.vx, d.vy), speed = raw == 0 ? 1 : raw, ux = d.vx / speed, uy = d.vy / speed,
                         len = std::min(5., .5 + speed * .012);
            p.line(d.x - ux * len, d.y - uy * len, d.x - ux, d.y - uy, C[n - 4]);
            p.px(d.x, d.y, C[n - 1]);
            if (d.size > 1) {
                p.px(d.x - ux, d.y - uy, C[n - 2]);
                p.px(d.x + 1, d.y, C[n - 3]);
            }
            p.glow(d.x, d.y, 3 + d.size * 1.5, C[n - 2], .3);
        }
    }
};
}  // namespace eve::stylize::pixelfx::nature
