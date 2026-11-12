// Native adaptation of pixelfx scifi/_lib.js strokeArc, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::scifi {
/** @brief Stroke borrowed arc points synchronously; retains no frame or path references.
 * @cost Three half-pixel traversals per path segment, including clipped glow stamps. */
inline void strokeArc(Paint& p, std::span<const Point> pts, const std::array<uint32_t, 3>& C, double k = 1,
                      double w = 1) {
    for (int pass = 0; pass < 3; ++pass)
        for (size_t i = 1; i < pts.size(); ++i) {
            const auto [ax, ay] = pts[i - 1];
            const auto [bx, by] = pts[i];
            const double L      = std::max(1., jsHypot2(bx - ax, by - ay));
            for (double d = 0; d <= L; d += .5) {
                const double x = ax + ((bx - ax) * d) / L, y = ay + ((by - ay) * d) / L;
                if (pass == 0)
                    p.circle(x, y, (k > .6 ? 1.6 : 1.1) * w, C[0]);
                else if (pass == 1)
                    p.circle(x, y, (k > .6 ? .9 : .6) * w, C[1]);
                else if (k > .3)
                    p.dot(x, y, WHITE);
            }
            if (pass == 0 && i % 3 == 0) p.glow(ax, ay, 9 * w, C[2], .45 * k);
        }
}
}  // namespace eve::stylize::pixelfx::scifi
