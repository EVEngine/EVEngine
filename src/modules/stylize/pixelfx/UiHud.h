// eve-hot-path
// Native adaptation of pixelfx ui/_lib.js panel/HEART/heartPal, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#pragma once
#include "common/Assert.h"
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::ui {
/** @brief Owned source plate colors, independent of actor or renderer lifetime. */
struct PanelColors {
    uint32_t outline, rim, top, body, low;
};
/** @brief Raster a compiled source rounded HUD plate synchronously.
 * @cost Linear in clipped plate area per explicit draw; no allocation or retained data. */
inline void panel(Paint& p, double x, double y, double w, double h, const PanelColors& colors, double r = 3,
                  double alpha = 1, std::optional<double> shine = std::nullopt) {
    if (w < 2 || h < 2) return;
    const double x1 = x + w, y1 = y + h;
    p.shader(x, y, x1, y1, [&](double px, double py, ShadeOutput& out) {
        const double cx = std::max({x + r - px, 0., px - (x1 - r)}), cy = std::max({y + r - py, 0., py - (y1 - r)}),
                     e = cx > 0 && cy > 0 ? r - jsHypot2(cx, cy) : std::min({px - x, x1 - px, py - y, y1 - py});
        if (e < 0) return;
        out.a = alpha;
        if (e < 1) {
            out.c = colors.outline;
            return;
        }
        if (py - y < 2) {
            out.c = colors.rim;
            return;
        }
        if (y1 - py < 2) {
            out.c = colors.low;
            return;
        }
        if (shine) {
            const double d = std::abs(px - (py - y) * .6 - *shine);
            if (d < 1.3) {
                out.c = WHITE;
                return;
            }
            if (d < 3) {
                out.c = colors.rim;
                return;
            }
        }
        out.c = py - y < h * .45 ? colors.top : colors.body;
    });
}
/** @brief Source HUD heart code rows; no external or baked animation asset is retained. */
inline constexpr std::array<std::string_view, 12> HEART{
    {"..kkk...kkk..", ".khhok.khook.", "khwhookoooomk", "khwoooooooomk", "khoooooooommk", "kooooooooomdk",
     ".kooooooomdk.", "..kooooomdk..", "...kooomdk...", "....komdk....", ".....kdk.....", "......k......"}};
/** @brief Derive an owned source heart palette once from the prepared color ramp. */
[[nodiscard]] inline std::array<uint32_t, 256> heartPalette(std::span<const uint32_t> C) {
    EV_ASSERT(C.size() >= 6);
    std::array<uint32_t, 256> out{};
    out['k'] = C[0];
    out['w'] = WHITE;
    out['h'] = C[5];
    out['o'] = C[4];
    out['m'] = C[3];
    out['d'] = C[2];
    return out;
}
}  // namespace eve::stylize::pixelfx::ui
