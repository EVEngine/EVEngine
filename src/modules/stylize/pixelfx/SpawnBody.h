// eve-hot-path
// Native adaptation of pixelfx spawn/_lib.js bodyOf/rimOf, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#pragma once
#include <optional>
#include "common/Assert.h"
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/Math.h"
namespace eve::stylize::pixelfx::spawn {
/** @brief One source body pixel with owning local position, mask coordinates and source/fallback color. */
struct BodyPixel {
    double   x, y;
    int      mx, my;
    uint32_t color;
    bool     edge;
};
/** @brief One four-neighbor outside cell, in source row-major order. */
struct RimPixel {
    double x, y;
    int    mx, my;
};
/** @brief Owning prepared body geometry; per-effect mutable peel/burn state belongs to its kernel.
 * @note Caller-thread affine; no input references or pointers retained. Geometry/list order stays fixed
 * after preparation, so returned indices remain valid for the owner's lifetime.
 * @cost Storage linear in bounded mask cells; preparation is amortized over instance playback. */
struct Body {
    int                    width = 0, height = 0;
    double                 x = 0, y = 0;
    std::vector<BodyPixel> list;
    std::vector<int>       cells;
    /** @brief Query occupied mask coordinates; gone/peel state does not change occupancy. */
    [[nodiscard]] bool isOccupied(int mx, int my) const {
        return mx >= 0 && my >= 0 && mx < width && my < height && cells[size_t(my) * size_t(width) + size_t(mx)] >= 0;
    }
    /** @brief Query the stable list index of an occupied cell, or explicit absence outside the silhouette. */
    [[nodiscard]] std::optional<size_t> cellIndex(int mx, int my) const {
        if (!isOccupied(mx, my)) return std::nullopt;
        return size_t(cells[size_t(my) * size_t(width) + size_t(mx)]);
    }
};
/** @brief Prepare source bodyOf from a validated owning actor snapshot and normalized color ramp.
 * @note Borrows inputs synchronously; occupied zero-color cells use the source height-based fallback.
 * @cost Linear mask scan and owning geometry allocation once per instance/replay construction. */
[[nodiscard]] inline Body bodyOf(const PixelFxActor& actor, std::span<const uint32_t> C) {
    EV_ASSERT((actor.width > 0 && actor.height > 0 && C.size() >= 3));
    Body       body{actor.width, actor.height, actor.x,
                    actor.y,     {},           std::vector<int>(size_t(actor.width) * size_t(actor.height), -1)};
    const auto on = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < actor.width && y < actor.height &&
               actor.mask[size_t(y) * size_t(actor.width) + size_t(x)] != 0;
    };
    for (int my = 0; my < actor.height; ++my)
        for (int mx = 0; mx < actor.width; ++mx) {
            if (!on(mx, my)) continue;
            const size_t   at  = size_t(my) * size_t(actor.width) + size_t(mx);
            const uint32_t own = actor.pixels[at];
            const auto     shade =
                size_t(std::max(1., std::min(double(C.size() - 2), jsRound(double(C.size() - 2) *
                                                                           (.85 - (double(my) / actor.height) * .5)))));
            const bool edge = !on(mx - 1, my) || !on(mx + 1, my) || !on(mx, my - 1) || !on(mx, my + 1);
            body.cells[at]  = int(body.list.size());
            body.list.push_back({actor.x + mx, actor.y + my, mx, my, own ? own : C[shade], edge});
        }
    return body;
}
/** @brief Prepare source rimOf cells immediately outside the four-neighbor silhouette.
 * @note Body is a synchronous borrow; returned positions retain no body/actor references.
 * @cost Linear expanded-mask scan once per kernel construction, reused by all draws. */
[[nodiscard]] inline std::vector<RimPixel> rimOf(const Body& body) {
    std::vector<RimPixel> rim;
    for (int my = -1; my <= body.height; ++my)
        for (int mx = -1; mx <= body.width; ++mx)
            if (!body.isOccupied(mx, my) && (body.isOccupied(mx - 1, my) || body.isOccupied(mx + 1, my) ||
                                             body.isOccupied(mx, my - 1) || body.isOccupied(mx, my + 1)))
                rim.push_back({body.x + mx, body.y + my, mx, my});
    return rim;
}
}  // namespace eve::stylize::pixelfx::spawn
