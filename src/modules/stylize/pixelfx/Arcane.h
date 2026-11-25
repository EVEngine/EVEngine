// eve-hot-path
// Native adaptation of pixelfx arcane/_lib.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::arcane {
/** @brief Return the source ordered-dither threshold at the cell containing the supplied coordinates. */
inline double bayer(double x, double y) {
    constexpr std::array<int, 16> values{0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return (values[size_t(((int(std::floor(y)) & 3) << 2) | (int(std::floor(x)) & 3))] + .5) / 16;
}

/** @brief Sample the source ordered-dither ramp; palette and scalar inputs are synchronous borrows. */
inline uint32_t tone(std::span<const uint32_t> ramp, double intensity, double x, double y, double lo = .06,
                     double sharp = 0) {
    const double b = bayer(x, y);
    if (intensity < lo && b > intensity / lo) return 0;
    const size_t n = ramp.size() - 1;
    const double f = clamp(intensity) * double(n);
    size_t       i = size_t(std::floor(f));
    const double q = sharp > 0 ? clamp((f - double(i) - .5) / (1 - sharp) + .5) : f - double(i);
    if (q > b) i = std::min(n, i + 1);
    return ramp[i];
}
/** @brief Prepare owning source luminance bands from a resolved actor snapshot; retain no input references.
 * @cost Allocates one byte per actor cell once per configuration/kernel construction. */
inline std::vector<uint8_t> shades(const PixelFxActor& actor) {
    std::vector<uint8_t> result(actor.mask.size());
    for (size_t i = 0; i < result.size(); ++i)
        if (actor.mask[i])
            result[i] = actor.pixels.empty()          ? 2
                        : luma(actor.pixels[i]) < .24 ? 1
                        : luma(actor.pixels[i]) < .62 ? 2
                                                      : 3;
    return result;
}
/** @brief Paint a source squeezed silhouette from resolved actor and matching luminance bands.
 * @note Painter, actor and bands are borrowed synchronously on the caller thread; no callbacks or retained references.
 * @cost Visits actor height times squeezed width cells per explicit draw, with no allocation. */
inline void squeezed(Paint& p, const PixelFxActor& actor, std::span<const uint8_t> bands, double sx, uint32_t body,
                     uint32_t edge, double alpha = 1, double dx = 0) {
    if (sx <= .02) return;
    const double cx = actor.x + actor.width / 2., half = actor.width / 2. + 1;
    for (int y = 0; y < actor.height; ++y)
        for (double x = std::floor(cx - half * sx); x <= std::ceil(cx + half * sx); ++x) {
            const double  i     = jsRound(cx + (x + .5 - cx) / sx - .5) - actor.x;
            const uint8_t value = i < 0 || i >= actor.width || i != std::floor(i)
                                      ? 0
                                      : bands[size_t(y) * size_t(actor.width) + size_t(i)];
            if (value) p.dot(x + dx, actor.y + y, value == 1 ? edge : body, alpha);
        }
}
/** @brief Paint a one-cell-thick perspective ring; callbacks and painter are borrowed synchronously.
 * @cost Visits the clipped ring bounding rectangle once per draw, without retained geometry. */
template <class C, class A = double, class Keep = std::nullptr_t>
void floorRing(Paint& p, double cx, double cy, double r, double sy, C color, A alpha = 1, Keep keep = nullptr) {
    if (r < .5) return;
    const double ry = r * sy;
    p.shader(cx - r - 1, cy - ry - 1, cx + r + 1, cy + ry + 1, [&](double x, double y, ShadeOutput& out) {
        const double nx = (x - cx) / r, ny = (y - cy) / std::max(.3, ry), d = jsHypot2(nx, ny);
        if (d < 1e-6) return;
        const double g = jsHypot2(nx / r, ny / std::max(.3, ry)) / d;
        if (std::abs(d - 1) / g > .5) return;
        const double angle = std::atan2(ny, nx);
        if constexpr (!std::is_same_v<Keep, std::nullptr_t>)
            if (!keep(angle)) return;
        out.c = sample(color, angle);
        out.a = sample(alpha, angle);
    });
}
inline constexpr std::array<std::array<std::string_view, 5>, 8> GLYPHS{{{{".#.", "#.#", ".#.", ".#.", "#.#"}},
                                                                        {{"#.#", "#.#", ".#.", "#.#", "#.#"}},
                                                                        {{"##.", "#.#", "##.", "#..", "#.."}},
                                                                        {{"#..", "#.#", "###", "#.#", "..#"}},
                                                                        {{".##", "#..", ".#.", "..#", "##."}},
                                                                        {{"###", ".#.", "###", ".#.", ".#."}},
                                                                        {{"#.#", "###", "#.#", "..#", "..#"}},
                                                                        {{".#.", ".##", ".#.", "##.", ".#."}}}};
/** @brief Draw a source rune anchored by its bottom row; all inputs are synchronous values/borrows. */
inline void glyph(Paint& p, int index, double x, double y, uint32_t color, double alpha = 1, uint32_t under = 0) {
    const auto&                                rows = GLYPHS[size_t((index % 8 + 8) % 8)];
    const double                               X = jsRound(x) - 1, Y = jsRound(y) - 4;
    static constexpr std::array<uint32_t, 256> empty{};
    SpriteOptions                              options;
    options.alpha = alpha;
    if (under) {
        options.tint = under;
        p.sprite(rows, empty, X, Y + 1, options);
    }
    options.tint = color;
    p.sprite(rows, empty, X, Y, options);
}
/** @brief Paint a closed-form source mote using borrowed painter and scalar velocity/intensity. */
inline void mote(Paint& p, double x, double y, double vx, double vy, uint32_t c, double size = 1, double k = 1,
                 double glow = .4) {
    const double raw = jsHypot2(vx, vy), sp = raw == 0 ? 1 : raw, ux = vx / sp, uy = vy / sp,
                 len = std::min(16., .5 + sp * .026) * size, a = k < .35 ? k / .35 : 1;
    if (len > 1.2) p.line(x - ux * len, y - uy * len, x - ux * len * .35, y - uy * len * .35, scale(c, .55), a);
    p.line(x - ux * len * .4, y - uy * len * .4, x, y, c, a);
    if (glow > 0) p.glow(x, y, 2.5 + size, c, glow * k);
}
inline constexpr std::array<std::array<std::string_view, 7>, 8> SIGILS{
    {{{"..#..", ".#.#.", "#.#.#", "..#..", ".###.", "..#..", ".#.#."}},
     {{"#...#", ".#.#.", "..#..", ".###.", "..#..", ".#.#.", "#...#"}},
     {{"#....", "##...", "#.#..", "#..#.", "#.#..", "##...", "#...."}},
     {{"..#..", "#.#.#", ".###.", "..#..", "..#..", ".#.#.", "#...#"}},
     {{"#.#.#", "#.#.#", "#####", "..#..", "..#..", "..#..", ".###."}},
     {{"#...#", "##.##", "#.#.#", "#...#", "#.#.#", "##.##", "#...#"}},
     {{"..#..", ".#.#.", "#...#", ".#.#.", "..#..", "..#..", "..#.."}},
     {{"#..#.", "#.#..", "##...", "#....", "##...", "#.#..", "#..#."}}}};
/** @brief Draw an original source sigil with a one-cell edge; painter and glyph data are synchronous borrows. */
inline void sigil(Paint& p, int index, double x, double y, uint32_t color, double alpha = 1, uint32_t edge = 0) {
    const auto&                                rows = SIGILS[size_t((index % 8 + 8) % 8)];
    const double                               X = jsRound(x) - 2, Y = jsRound(y) - 3;
    static constexpr std::array<uint32_t, 256> empty{};
    SpriteOptions                              options;
    options.alpha = alpha;
    if (edge) {
        options.tint = edge;
        for (const auto& d : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
            p.sprite(rows, empty, X + d.x, Y + d.y, options);
    }
    options.tint = color;
    p.sprite(rows, empty, X, Y, options);
}
/** @brief Paint a source white-tip streak; painter and scalar inputs are borrowed synchronously. */
inline void streak(Paint& p, double x, double y, double vx, double vy, uint32_t color, double alpha = 1,
                   double maxLength = 7, double glow = .4) {
    const double raw = jsHypot2(vx, vy), sp = raw == 0 ? 1 : raw, ux = vx / sp, uy = vy / sp,
                 L = clamp(sp / 30, 1, maxLength);
    for (double d = L; d >= 0; d -= .5)
        p.dot(x - ux * d, y - uy * d,
              d < 1        ? WHITE
              : d < L * .5 ? color
                           : scale(color, .6),
              (d < L * .5 ? 1 : 1.2 - d / L) * alpha);
    if (glow > 0) p.glow(x, y, 4, color, glow * alpha);
}
struct ArcPath {
    std::vector<Point> points;
    double             width;
};
/** @brief Build owning source arc paths from the borrowed RNG; retain no input points or callbacks.
 * @cost Allocates main/fork midpoint vectors per crackle sample, reused across all three ink passes. */
inline std::vector<ArcPath> arc(Rng& r, double x0, double y0, double x1, double y1, double jag = .3, int forks = 1,
                                double forkLength = .5) {
    const double         L     = jsHypot2(x1 - x0, y1 - y0);
    const int            depth = int(clamp(jsRound(std::log2(L / 1.5)), 1, 5));
    const auto           main  = bolt(r, x0, y0, x1, y1, jag, depth);
    std::vector<ArcPath> out{{main, 1}};
    for (int k = 0; k < forks && main.size() > 3; ++k) {
        if (r.next() > .7) continue;
        const size_t i      = 1 + size_t(std::floor(r.next() * double(main.size() - 2)));
        const auto [x, y]   = main[i];
        const auto [px, py] = main[i - 1];
        const double turn   = r.next() < .5 ? -1 : 1;
        const double a      = std::atan2(y - py, x - px) + turn * (.4 + r.next() * .6),
                     len    = L * forkLength * (.4 + .6 * r.next()) * (1 - double(i) / double(main.size()));
        if (len > 1.5)
            out.push_back(
                {bolt(r, x, y, x + std::cos(a) * len, y + std::sin(a) * len, jag * 1.1, std::max(1, depth - 2)), .55});
    }
    return out;
}
struct ArcColors {
    uint32_t core, body, halo, glow;
};
/** @brief Ink borrowed arc paths synchronously; no path/painter/callback retained.
 * @cost Three half-cell traversals per path segment, including sparse clipped glow stamps per draw. */
inline void drawArc(Paint& p, std::span<const ArcPath> paths, const ArcColors& colors, double k = 1, double glow = .5) {
    const auto steps = [&](auto fn) {
        for (const auto& path : paths)
            for (size_t i = 0; i + 1 < path.points.size(); ++i) {
                const auto [ax, ay] = path.points[i];
                const auto [bx, by] = path.points[i + 1];
                const double l      = std::max(1., jsHypot2(bx - ax, by - ay));
                for (double d = 0; d <= l; d += .5)
                    fn(ax + ((bx - ax) * d) / l, ay + ((by - ay) * d) / l, path, i, d == 0);
            }
    };
    if (k > .45)
        steps([&](double x, double y, const ArcPath& path, size_t, bool) {
            if (path.width >= 1)
                for (const auto& d : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                    p.dot(x + d.x * 1.5, y + d.y * 1.5, colors.halo, (k - .45) * 1.6);
        });
    if (k > .25)
        steps([&](double x, double y, const ArcPath& path, size_t, bool) {
            if (path.width > .4) {
                p.dot(x + 1, y, colors.body);
                p.dot(x, y + 1, colors.body);
            }
        });
    steps([&](double x, double y, const ArcPath& path, size_t i, bool first) {
        p.px(x, y, path.width >= 1 && k > .3 ? colors.core : colors.body);
        if (first && i % 3 == 0)
            p.glow(x, y, (path.width >= 1 ? 9 : 5) * (.5 + k * .5), colors.glow, glow * k * path.width);
    });
}
}  // namespace eve::stylize::pixelfx::arcane
