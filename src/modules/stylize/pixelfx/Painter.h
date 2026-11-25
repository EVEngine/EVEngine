// Native adaptation of pixelfx core/painter.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include <functional>
#include <optional>
#include <tuple>
#include <type_traits>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/Font.h"
#include "stylize/pixelfx/Math.h"

namespace eve::stylize::pixelfx {
struct ShadeOutput {
    uint32_t c = 0;
    double   a = 1;
    uint32_t l = 0;
    double   k = 0;
};
template <size_t N, class F, class Tuple, size_t... I>
constexpr bool isPrefixInvocable(std::index_sequence<I...>) {
    return std::is_invocable_v<F, decltype(std::get<I>(std::declval<Tuple>()))...>;
}
template <size_t N, class F, class Tuple, size_t... I>
auto invokePrefix(const F& fn, const Tuple& args, std::index_sequence<I...>) {
    return std::invoke(fn, std::get<I>(args)...);
}
template <size_t N, class F, class Tuple>
auto samplePrefix(const F& fn, const Tuple& args) {
    if constexpr (isPrefixInvocable<N, F, Tuple>(std::make_index_sequence<N>{}))
        return invokePrefix<N>(fn, args, std::make_index_sequence<N>{});
    else if constexpr (N > 0)
        return samplePrefix<N - 1>(fn, args);
    else
        return fn;
}
template <class T, class... A>
auto sample(const T& value, A... args) {
    return samplePrefix<sizeof...(A)>(value, std::tuple<A...>(args...));
}
struct SpriteOptions {
    double                  scale = 1, alpha = 1;
    bool                    flip = false, flipY = false;
    std::optional<uint32_t> tint;
};
struct TextOptions {
    PixelFont font                                 = PixelFont::Mini;
    enum class Align { Left, Center, Right } align = Align::Left;
    double                  scale = 1, alpha = 1;
    std::optional<uint32_t> outline, shadow;
};
struct MaskView {
    int                      width = 0, height = 0;
    std::span<const uint8_t> data;
};
struct Paint {
    const PixelFxInfo&  info;
    std::span<uint32_t> colors, lights;
    double              gain;
    double              tx = 0, ty = 0, fx = 1, cs = 1, sn = 0, fade = 1;
    enum class Layer { Back, Front } layer = Layer::Front;
    /** @brief Optional local half-open write boundary, owned by this synchronous painter view. */
    struct ClipRect {
        double x0, y0, x1, y1;
        /** @brief Source canvas rules differ between rounded weather edges and transition pixel centers. */
        enum class Rule { RoundedEdges, PixelCenters } rule = Rule::RoundedEdges;
    };
    std::optional<ClipRect> clipRect;
    /** @brief Test screen-cell centers against the copied local boundary; no callbacks or pointers retained. */
    bool isInsideClip(int x, int y) const {
        if (!clipRect) return true;
        const auto b = *clipRect;
        if (sn == 0 && b.rule == ClipRect::Rule::RoundedEdges) {
            const double a = toX(b.x0, 0), c = toX(b.x1, 0);
            return x >= jsRound(std::min(a, c)) && x < jsRound(std::max(a, c)) && y >= jsRound(toY(0, b.y0)) &&
                   y < jsRound(toY(0, b.y1));
        }
        const double lx = fromX(x + .5, y + .5), ly = fromY(x + .5, y + .5);
        return lx >= b.x0 && lx < b.x1 && ly >= b.y0 && ly < b.y1;
    }
    void place(double x, double y, bool flip = false, double angle = 0) {
        tx = x;
        ty = y;
        fx = flip ? -1 : 1;
        cs = std::cos(angle);
        sn = std::sin(angle);
    }
    double toX(double x, double y) const { return tx + x * fx * cs - y * sn; }
    double toY(double x, double y) const { return ty + x * fx * sn + y * cs; }
    double fromX(double x, double y) const { return ((x - tx) * cs + (y - ty) * sn) * fx; }
    double fromY(double x, double y) const { return -(x - tx) * sn + (y - ty) * cs; }
    int    cellX(double x, double y) const { return int(std::floor(toX(std::floor(x) + 0.5, std::floor(y) + 0.5))); }
    int    cellY(double x, double y) const { return int(std::floor(toY(std::floor(x) + 0.5, std::floor(y) + 0.5))); }
    bool   contains(int x, int y) const {
        return x >= -info.originX && y >= -info.originY && x < info.width - info.originX &&
               y < info.height - info.originY;
    }
    size_t index(int x, int y) const {
        return size_t(y + info.originY) * size_t(info.width) + size_t(x + info.originX);
    }
    void put(int x, int y, uint32_t c, double a = 1) {
        constexpr int bayer[] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
        a *= fade;
        if (!c || (a < 1 && a <= (bayer[((y & 3) << 2) | (x & 3)] + 0.5) / 16) || !contains(x, y) ||
            !isInsideClip(x, y))
            return;
        colors[index(x, y)] = c;
    }
    void add(int x, int y, uint32_t c, double k = 1) {
        k *= fade * gain;
        if (k <= 0.004 || !contains(x, y) || !isInsideClip(x, y)) return;
        auto&    d = lights[index(x, y)];
        uint32_t v = 0xff000000u;
        for (int s : {0, 8, 16}) {
            const double value = double((d >> s) & 255u) + double((c >> s) & 255u) * k;
            // Source Math.min propagates NaN; its following bitwise conversion produces zero.
            if (!std::isnan(value)) v |= uint32_t(std::min(255.0, value)) << s;
        }
        d = v;
    }
    void blendPx(int x, int y, uint32_t c, double a) {
        a *= fade;
        if (!c || a <= 0.004 || !contains(x, y) || !isInsideClip(x, y)) return;
        auto&        d  = colors[index(x, y)];
        const double da = double(d >> 24) / 255, oa = a + da * (1 - a), k = a / oa;
        uint32_t     v = (jsUint(oa * 255) & 255u) << 24;
        for (int s : {0, 8, 16})
            v |= (jsUint(double((c >> s) & 255u) * k + double((d >> s) & 255u) * (1 - k)) & 255u) << s;
        d = v;
    }
    void px(double x, double y, uint32_t c, double a = 1) { put(cellX(x, y), cellY(x, y), c, a); }
    void dot(double x, double y, uint32_t c, double a = 1) {
        if (a > 0) px(x, y, c, a);
    }
    void     light(double x, double y, uint32_t c, double k = 1) { add(cellX(x, y), cellY(x, y), c, k); }
    void     blend(double x, double y, uint32_t c, double a) { blendPx(cellX(x, y), cellY(x, y), c, a); }
    uint32_t get(double x, double y) const {
        const int sx = cellX(x, y), sy = cellY(x, y);
        return contains(sx, sy) ? colors[index(sx, sy)] : 0;
    }
    template <class F>
    void screen(double x0, double y0, double x1, double y1, F fn) {
        const int xa = std::max(-info.originX, int(std::floor(x0))),
                  xb = std::min(info.width - info.originX - 1, int(std::ceil(x1)));
        const int ya = std::max(-info.originY, int(std::floor(y0))),
                  yb = std::min(info.height - info.originY - 1, int(std::ceil(y1)));
        for (int y = ya; y <= yb; ++y)
            for (int x = xa; x <= xb; ++x) fn(x + 0.5, y + 0.5, x, y);
    }
    template <class F>
    void shade(double x0, double y0, double x1, double y1, F fn) {
        double ax = INFINITY, bx = -INFINITY, ay = INFINITY, by = -INFINITY;
        for (int k = 0; k < 4; ++k) {
            const double x = k & 1 ? x1 : x0, y = k & 2 ? y1 : y0, sx = toX(x, y), sy = toY(x, y);
            ax = std::min(ax, sx);
            bx = std::max(bx, sx);
            ay = std::min(ay, sy);
            by = std::max(by, sy);
        }
        screen(ax, ay, bx, by, [&](double x, double y, int sx, int sy) { fn(fromX(x, y), fromY(x, y), sx, sy); });
    }
    template <class F>
    void shader(double x0, double y0, double x1, double y1, F fn) {
        shade(x0, y0, x1, y1, [&](double x, double y, int sx, int sy) {
            ShadeOutput out;
            fn(x, y, out);
            if (out.c) put(sx, sy, out.c, out.a);
            if (out.k > 0) add(sx, sy, out.l ? out.l : out.c, out.k);
        });
    }
    template <class C, class A>
    void lineFn(double x0, double y0, double x1, double y1, C c, A a) {
        int       sx = cellX(x0, y0), sy = cellY(x0, y0);
        const int ex = cellX(x1, y1), ey = cellY(x1, y1);
        const int dx = std::abs(ex - sx), dy = -std::abs(ey - sy), stepx = sx < ex ? 1 : -1, stepy = sy < ey ? 1 : -1,
                  n   = std::max(1, std::max(dx, -dy));
        int       err = dx + dy, i = 0;
        for (;;) {
            const double u = double(i++) / n;
            put(sx, sy, sample(c, u), sample(a, u));
            if (sx == ex && sy == ey) break;
            const int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                sx += stepx;
            }
            if (e2 <= dx) {
                err += dx;
                sy += stepy;
            }
        }
    }
    void line(double x0, double y0, double x1, double y1, uint32_t c, double a = 1) { lineFn(x0, y0, x1, y1, c, a); }
    template <class C, class A = double>
    void polyline(std::span<const Point> pts, C c, A a = 1) {
        for (size_t i = 1; i < pts.size(); ++i) lineFn(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, c, a);
    }
    template <class C, class A = double>
    void beam(double x0, double y0, double x1, double y1, double width, C c, A a = 1) {
        const double r = width / 2, dx = x1 - x0, dy = y1 - y0, L2 = dx * dx + dy * dy == 0 ? 1e-9 : dx * dx + dy * dy;
        shader(std::min(x0, x1) - r, std::min(y0, y1) - r, std::max(x0, x1) + r, std::max(y0, y1) + r,
               [&](double x, double y, ShadeOutput& o) {
                   const double u = clamp(((x - x0) * dx + (y - y0) * dy) / L2), px = x0 + dx * u - x,
                                py = y0 + dy * u - y, d2 = px * px + py * py;
                   if (d2 > r * r) return;
                   const double v = std::sqrt(d2) / r;
                   o.c            = sample(c, u, v);
                   o.a            = sample(a, u, v);
               });
    }
    template <class C, class A = double>
    void circle(double cx, double cy, double r, C c, A a = 1) {
        if (r < 0.6) {
            dot(cx, cy, sample(c, 0.0, 0.0), sample(a, 0.0, 0.0));
            return;
        }
        shader(cx - r, cy - r, cx + r, cy + r, [&](double x, double y, ShadeOutput& o) {
            const double dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (d2 > r * r) return;
            const double d = std::sqrt(d2) / r, ang = std::atan2(dy, dx);
            o.c = sample(c, d, ang);
            o.a = sample(a, d, ang);
        });
    }
    template <class C, class A>
    void disc(double cx, double cy, double r, C c, A a) {
        if (r < 0.5) {
            blend(cx, cy, sample(c, 0.0, 0.0), sample(a, 0.0, 0.0));
            return;
        }
        shade(cx - r, cy - r, cx + r, cy + r, [&](double x, double y, int sx, int sy) {
            const double dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (d2 > r * r) return;
            const double d = std::sqrt(d2) / r, ang = std::atan2(dy, dx);
            blendPx(sx, sy, sample(c, d, ang), sample(a, d, ang));
        });
    }
    template <class C, class A = double>
    void ellipse(double cx, double cy, double rx, double ry, C c, A a = 1) {
        if (rx <= 0 || ry <= 0) return;
        shader(cx - rx, cy - ry, cx + rx, cy + ry, [&](double x, double y, ShadeOutput& o) {
            const double dx = (x - cx) / rx, dy = (y - cy) / ry, d2 = dx * dx + dy * dy;
            if (d2 > 1) return;
            const double d = std::sqrt(d2), ang = std::atan2(dy, dx);
            o.c = sample(c, d, ang);
            o.a = sample(a, d, ang);
        });
    }
    template <class C, class A = double>
    void ring(double cx, double cy, double r, double w, C c, A a = 1) {
        const double r0 = std::max(0.0, r - w);
        shader(cx - r, cy - r, cx + r, cy + r, [&](double x, double y, ShadeOutput& o) {
            const double dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (d2 > r * r || d2 < r0 * r0) return;
            const double v = w > 0 ? (std::sqrt(d2) - r0) / (r == r0 ? 1 : r - r0) : 1, ang = std::atan2(dy, dx);
            o.c = sample(c, v, ang);
            o.a = sample(a, v, ang);
        });
    }
    template <class W, class C, class A = double>
    void arc(double cx, double cy, double r, W w, double a0, double a1, C c, A a = 1) {
        const double span = a1 - a0;
        shader(cx - r, cy - r, cx + r, cy + r, [&](double x, double y, ShadeOutput& o) {
            const double dx = x - cx, dy = y - cy, d = std::sqrt(dx * dx + dy * dy);
            if (d > r) return;
            double ang = std::atan2(dy, dx) - a0;
            ang -= std::floor(ang / TAU) * TAU;
            if (ang > span) return;
            const double u = ang / span, ww = sample(w, u);
            if (ww <= 0 || d < r - ww) return;
            const double v = (d - (r - ww)) / ww;
            o.c            = sample(c, u, v);
            o.a            = sample(a, u, v);
        });
    }
    void rect(double x, double y, double w, double h, uint32_t c, double a = 1) {
        if (sn == 0) {
            const int x0 = int(jsRound(x)), y0 = int(jsRound(y)), x1 = int(jsRound(x + w)), y1 = int(jsRound(y + h));
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) put(cellX(xx, yy), cellY(xx, yy), c, a);
        } else
            shader(x, y, x + w, y + h, [&](double lx, double ly, ShadeOutput& o) {
                if (lx < x || ly < y || lx >= x + w || ly >= y + h) return;
                o.c = c;
                o.a = a;
            });
    }
    template <class C, class A = double>
    void poly(std::span<const Point> pts, C c, A a = 1) {
        if (pts.empty()) return;
        double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
        for (const auto& p : pts) {
            x0 = std::min(x0, p.x);
            x1 = std::max(x1, p.x);
            y0 = std::min(y0, p.y);
            y1 = std::max(y1, p.y);
        }
        shader(x0, y0, x1, y1, [&](double x, double y, ShadeOutput& o) {
            bool inside = false;
            for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
                const auto [xi, yi] = pts[i];
                const auto [xj, yj] = pts[j];
                if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) inside = !inside;
            }
            if (inside) {
                o.c = sample(c, x, y);
                o.a = sample(a, x, y);
            }
        });
    }
    template <class C, class A = double>
    void tri(double x0, double y0, double x1, double y1, double x2, double y2, C c, A a = 1) {
        const std::array<Point, 3> pts{{{x0, y0}, {x1, y1}, {x2, y2}}};
        poly(pts, c, a);
    }
    void glow(double x, double y, double r, uint32_t c, double k = 0.5) {
        if (r < 1 || k <= 0.004) return;
        if (clipRect && clipRect->rule == ClipRect::Rule::RoundedEdges) {
            const auto   b  = *clipRect;
            const double x0 = std::max(b.x0, std::floor(x - r)), x1 = std::min(b.x1 - 1, std::ceil(x + r)),
                         y0 = std::max(b.y0, std::floor(y - r)), y1 = std::min(b.y1 - 1, std::ceil(y + r));
            for (double py = y0; py <= y1; ++py) {
                const double dy = (py + .5 - y) / r;
                for (double px = x0; px <= x1; ++px) {
                    const double dx = (px + .5 - x) / r, d = dx * dx + dy * dy;
                    if (d >= 1) continue;
                    const double f = 1 - std::sqrt(d);
                    light(px, py, c, f * f * k);
                }
            }
            return;
        }
        const double cx = toX(x, y), cy = toY(x, y);
        screen(cx - r, cy - r, cx + r, cy + r, [&](double xx, double yy, int sx, int sy) {
            const double dx = (xx - cx) / r, dy = (yy - cy) / r, d = dx * dx + dy * dy;
            if (d >= 1) return;
            const double f = 1 - std::sqrt(d);
            add(sx, sy, c, f * f * k);
        });
    }
    void glowLine(double x0, double y0, double x1, double y1, double r, uint32_t c, double k = 0.5) {
        if (r < 1 || k <= 0.004) return;
        const double ax = toX(x0, y0), ay = toY(x0, y0), bx = toX(x1, y1), by = toY(x1, y1), dx = bx - ax, dy = by - ay,
                     L2 = dx * dx + dy * dy == 0 ? 1e-9 : dx * dx + dy * dy;
        screen(std::min(ax, bx) - r, std::min(ay, by) - r, std::max(ax, bx) + r, std::max(ay, by) + r,
               [&](double px, double py, int sx, int sy) {
                   const double u = clamp(((px - ax) * dx + (py - ay) * dy) / L2), ex = ax + dx * u - px,
                                ey = ay + dy * u - py, d = std::sqrt(ex * ex + ey * ey) / r;
                   if (d < 1) {
                       const double f = 1 - d;
                       add(sx, sy, c, f * f * k);
                   }
               });
    }
    void glowRing(double x, double y, double R, double r, uint32_t c, double k = 0.5) {
        if (r < 1 || k <= 0.004) return;
        const double cx = toX(x, y), cy = toY(x, y), E = R + r;
        screen(cx - E, cy - E, cx + E, cy + E, [&](double xx, double yy, int sx, int sy) {
            const double d = std::abs(jsHypot2(xx - cx, yy - cy) - R) / r;
            if (d >= 1) return;
            const double f = 1 - d;
            add(sx, sy, c, f * f * k);
        });
    }
    void sparkle(double x, double y, double arm, uint32_t c, uint32_t tip, double k) {
        const int n = int(jsRound(arm));
        if (n < 1) return;
        const double X = std::floor(x), Y = std::floor(y);
        for (int d = 1; d <= n; ++d)
            for (int sign : {-1, 1}) {
                const uint32_t cc = d > n * 0.6 ? tip : c;
                px(X + sign * d, Y, cc);
                px(X, Y + sign * d, cc);
            }
        if (n >= 3)
            for (int a : {-1, 1})
                for (int b : {-1, 1}) px(X + a, Y + b, c);
        if (n >= 6)
            for (int a : {-2, 2})
                for (int b : {-2, 2}) px(X + a, Y + b, tip);
        px(X, Y, WHITE);
        glow(x, y, 4 + n * 2, c, k);
    }
    void sprite(std::span<const std::string_view> rows, const std::array<uint32_t, 256>& palette, double x, double y,
                const SpriteOptions& opts = {}) {
        const size_t width = rows.empty() ? 0 : rows.front().size();
        for (size_t r = 0; r < rows.size(); ++r)
            for (size_t q = 0; q < rows[r].size(); ++q) {
                const auto ch = uint8_t(rows[r][q]);
                if (ch == '.' || ch == ' ') continue;
                const uint32_t c = opts.tint.value_or(palette[ch]);
                if (!c) continue;
                const double col = double(opts.flip ? width - 1 - q : q),
                             row = double(opts.flipY ? rows.size() - 1 - r : r);
                if (opts.scale == 1)
                    dot(x + col, y + row, c, opts.alpha);
                else
                    rect(x + col * opts.scale, y + row * opts.scale, opts.scale, opts.scale, c, opts.alpha);
            }
    }
    template <class C>
    void text(std::string_view str, double x, double y, C c, const TextOptions& opts = {}) {
        const double k = opts.scale, w = textWidth(str, opts.font) * k;
        const double left = jsRound(opts.align == TextOptions::Align::Center  ? x - w / 2
                                    : opts.align == TextOptions::Align::Right ? x - w
                                                                              : x),
                     top  = jsRound(y);
        if (opts.outline)
            rasterText(str, opts.font, [&](int gx, int gy) {
                rect(left + gx * k - 1, top + gy * k - 1, k + 2, k + 2, *opts.outline, opts.alpha);
            });
        if (opts.shadow)
            rasterText(str, opts.font, [&](int gx, int gy) {
                rect(left + gx * k + 1, top + gy * k + 1, k, k, *opts.shadow, opts.alpha);
            });
        rasterText(str, opts.font,
                   [&](int gx, int gy) { rect(left + gx * k, top + gy * k, k, k, sample(c, gx, gy), opts.alpha); });
    }
    Point measure(std::string_view str, const TextOptions& opts = {}) const {
        return {textWidth(str, opts.font) * opts.scale, fontHeight(opts.font) * opts.scale};
    }
    template <class C, class A = double>
    void mask(MaskView m, double x, double y, C c, A a = 1) {
        for (int my = 0; my < m.height; ++my)
            for (int mx = 0; mx < m.width; ++mx)
                if (m.data[size_t(my) * size_t(m.width) + size_t(mx)])
                    dot(x + mx, y + my, sample(c, mx, my), sample(a, mx, my));
    }
    template <class C, class A = double>
    void outline(MaskView m, double x, double y, C c, A a = 1) {
        const auto inside = [&](int mx, int my) {
            return mx >= 0 && my >= 0 && mx < m.width && my < m.height &&
                   m.data[size_t(my) * size_t(m.width) + size_t(mx)] != 0;
        };
        for (int my = -1; my <= m.height; ++my)
            for (int mx = -1; mx <= m.width; ++mx)
                if (!inside(mx, my) &&
                    (inside(mx - 1, my) || inside(mx + 1, my) || inside(mx, my - 1) || inside(mx, my + 1)))
                    dot(x + mx, y + my, sample(c, mx, my), sample(a, mx, my));
    }
};
}  // namespace eve::stylize::pixelfx
