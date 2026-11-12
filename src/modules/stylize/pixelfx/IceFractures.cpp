// eve-hot-path
// Native adaptation of pixelfx ice/_lib.js drawFractures, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/IceFractures.h"
namespace eve::stylize::pixelfx::ice {
void drawFractures(Paint& p, std::span<const Crack> cracks, double reach, std::span<const uint32_t> C,
                   const FracturePaint& o) {
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& pts : cracks)
            for (size_t i = 1; i < pts.size(); ++i) {
                const auto a = pts[i - 1], b = pts[i];
                if (a.d >= reach) break;
                const double u = std::min(1., (reach - a.d) / std::max(.01, b.d - a.d)), x0 = a.x + o.x, y0 = a.y + o.y,
                             x1 = a.x + o.x + (b.x - a.x) * u, y1 = a.y + o.y + (b.y - a.y) * u;
                if (pass == 0)
                    p.line(x0, y0 + 1, x1, y1 + 1, C[std::min(size_t(2), C.size() - 1)], o.alpha);
                else {
                    const bool tip = o.hot && reach - b.d < 3;
                    p.line(x0, y0, x1, y1, tip ? WHITE : C[C.size() - 2], o.alpha);
                    if (tip)
                        p.glow(x1, y1, 5, C[C.size() - 3], .5 * o.alpha);
                    else if (i % 2)
                        p.glow(x1, y1, 3, C[C.size() - 3], .18 * o.alpha);
                }
            }
}
}  // namespace eve::stylize::pixelfx::ice
