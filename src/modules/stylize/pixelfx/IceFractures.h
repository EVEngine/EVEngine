// Native adaptation of pixelfx ice/_lib.js fractures/drawFractures, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "common/Export.h"
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::ice {
/** @brief Independent crack vertex and cumulative source path distance. */
struct CrackPoint {
    double x, y, d;
};
using Crack = std::vector<CrackPoint>;
/** @brief Owning authored crack-walk controls; no pointers or callbacks. */
struct FractureOptions {
    std::array<double, 2> step{{1.6, 2.6}};
    int                   length = 18;
    double                fork   = .16;
    std::optional<double> angle;
    double                spread = TAU;
};
/** @brief Build independent source-ordered paths from a synchronous RNG/predicate borrow.
 * @param inside Trusted compiled membership predicate; never retained or invoked under a lock.
 * @cost Allocates bounded paths proportional to authored count/length with source depth capped at two;
 * build once during configuration and retain the owning result across frames.
 * @note Caller owns RNG mutation. Recursion emits child paths before their parent, as in the source. */
template <class Inside>
[[nodiscard]] std::vector<Crack> fractures(Rng& R, Inside inside, double x, double y, int count,
                                           const FractureOptions& o = {}) {
    std::vector<Crack> out;
    const double       a0   = o.angle ? *o.angle : R.range(0, TAU);
    const auto         walk = [&](auto&& self, double px, double py, double d, double a, int len, int depth) -> void {
        Crack pts{{px, py, d}};
        for (int s = 0; s < len; ++s) {
            a += R.range(-.55, .55);
            const double st = R.range(o.step[0], o.step[1]), nx = px + std::cos(a) * st, ny = py + std::sin(a) * st;
            if (!inside(nx, ny)) break;
            px = nx;
            py = ny;
            d += st;
            pts.push_back({px, py, d});
            if (depth < 2 && pts.size() > 2 && R.isChance(o.fork)) {
                const double turn = R.signedRange(.6, 1.2);
                self(self, px, py, d, a + turn, int(std::ceil(len * .5)), depth + 1);
            }
        }
        if (pts.size() > 1) out.push_back(std::move(pts));
    };
    for (int i = 0; i < count; ++i)
        walk(walk, x, y, 0, a0 + (double(i) / count) * o.spread + R.range(-.3, .3) * std::min(1., o.spread / TAU),
             o.length, 0);
    return out;
}
/** @brief Owning draw controls for already-built cracks; no references. */
struct FracturePaint {
    double x = 0, y = 0;
    bool   hot   = true;
    double alpha = 1;
};
/** @brief Draw growing engraved cracks using synchronous path/ramp/target borrows.
 * @cost Two passes over bounded path segments per draw; no geometry allocation or retained callbacks.
 * @note Caller-thread affine to the target; no RNG mutation. */
EVENGINE_API_WORLD void drawFractures(Paint& p, std::span<const Crack> cracks, double reach,
                                      std::span<const uint32_t> C, const FracturePaint& o = {});
}  // namespace eve::stylize::pixelfx::ice
