// Native adaptation of pixelfx impacts/_lib.js stone and rock helpers, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "common/Assert.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx::impact {
struct Spike {
    double angle, length, width;
};
struct RingSegment {
    double angle, gap, drift, width = 1;
};
/** @brief Draw source flying ring arcs with synchronous numeric recipe and painter borrows. */
inline void brokenRing(Paint& p, double x, double y, double r, double thickness, std::span<const RingSegment> segments,
                       double breakup, uint32_t outer, uint32_t inner, double alpha = 1, double squash = 1,
                       double maxY = INFINITY) {
    if (r <= 0 || alpha <= 0) return;
    EV_ASSERT(!segments.empty(), "Prepared impact ring must own segments");
    const double full = TAU / double(segments.size());
    for (const auto& s : segments) {
        const double span = full * (1 - s.gap * breakup) + .02, rr = r * (1 + s.drift * breakup),
                     m = std::max(4., std::ceil(rr * span * 1.8));
        for (int j = 0; j <= m; ++j) {
            const double u = j / m, angle = s.angle + (u - .5) * span,
                         w  = std::max(.5, thickness * s.width * (1 - breakup * std::pow(std::abs(2 * u - 1), 1.6))),
                         ca = std::cos(angle), sa = std::sin(angle);
            for (double o = -w / 2; o <= w / 2 + .01; o += .5)
                if (y + sa * (rr + o) * squash < maxY)
                    p.dot(x + ca * (rr + o), y + sa * (rr + o) * squash, o > -.26 ? outer : inner, alpha);
        }
    }
}
/** @brief Owning source star recipe options consumed only during preparation. */
struct SpikeRecipe {
    double minLength = 7, maxLength = 10, minWidth = 1.5, maxWidth = 2.1, forward = 1.8, back = .6, jag = 1,
           jitter = .18;
};
/** @brief Prepare source directional spikes with an injected RNG; no input references retained.
 * @cost Allocates one bounded spike sequence per configuration preparation, reused across frames. */
inline std::vector<Spike> spikes(Rng& rng, int count, double aim, const SpikeRecipe& o) {
    EV_PARAM_CHECK((count > 0 && count <= 128), "Compiled impact star requires1..128spikes");
    std::vector<Spike> out;
    out.reserve(size_t(count));
    for (int i = 0; i < count; ++i) {
        const double a = aim + (double(i) / count) * TAU + rng.range(-o.jitter, o.jitter), cb = std::cos(a - aim),
                     L = rng.range(o.minLength, o.maxLength) *
                         (1 + o.forward * std::pow(std::max(0., cb), 3) + o.back * std::pow(std::max(0., -cb), 3)) *
                         (i % 2 ? o.jag : 1),
                     w = rng.range(o.minWidth, o.maxWidth) * (i % 2 ? std::sqrt(o.jag) : 1);
        out.push_back({a, L, w});
    }
    return out;
}
/** @brief Draw a source three-pass hit star using synchronous recipe/material/painter borrows. */
inline void star(Paint& p, double x, double y, std::span<const Spike> list, double sc, double th,
                 const std::array<uint32_t, 4>& cols, double alpha = 1, double heart = 2.4) {
    for (int pass = 0; pass < 3; ++pass)
        for (const auto& s : list) {
            const double c = std::cos(s.angle), sn = std::sin(s.angle), L = s.length * sc;
            for (double d = 0; d <= L; d += .5) {
                if (pass == 2 && d > L * .55) break;
                const double hw0 = s.width * th * (1 - d / L) * std::min(1., .5 + d / 3), hw = pass == 0   ? hw0 + .8
                                                                                               : pass == 1 ? hw0
                                                                                                           : hw0 * .45;
                const auto   col = pass == 0 ? cols[0] : pass == 1 ? (d < L * .35 ? cols[2] : cols[1]) : cols[3];
                for (double o = -hw; o <= hw; o += .5) p.dot(x + c * d - sn * o, y + sn * d + c * o, col, alpha);
            }
        }
    const double r = heart * sc * std::sqrt(th);
    p.circle(x, y, r + .8, cols[2], alpha);
    p.circle(x, y, r, cols[3], alpha);
}
/** @brief Immutable source impact stone shades, independently owned by the effect. */
struct Stone {
    uint32_t light, body, shade, dust, lip;
    explicit Stone(uint32_t c)
        : light(mix(scale(c, 1.35), WHITE, .08)),
          body(c),
          shade(scale(c, .64)),
          dust(mix(scale(c, 1.2), rgb(150, 140, 150), .3)),
          lip(mix(scale(c, 1.55), WHITE, .12)) {}
};
/** @brief Owning faceted clod state; Bits owns motion and this snapshot owns its source outline. */
struct Rock : Debris {
    std::vector<double> points;
    double              previousVy = 0;
};
/** @brief Prepare a source outline from the caller's RNG without retaining it.
 * @cost Allocates one bounded radius sequence per prepared rock, reused by every frame. */
inline std::vector<double> rockShape(Rng& rng, int count = 6) {
    EV_PARAM_CHECK((count > 0 && count <= 64), "Compiled impact outline requires1..64vertices");
    std::vector<double> out;
    out.reserve(size_t(count));
    for (int i = 0; i < count; ++i) out.push_back(rng.range(.7, 1.12));
    return out;
}
/** @brief Draw an owning rock snapshot with synchronous painter/material borrows. */
inline void rock(Paint& p, const Rock& b, const Stone& st) {
    EV_ASSERT(!b.points.empty(), "Prepared impact rock must own an outline");
    const double r = .75 + b.size * .8, fade = std::min(1., b.life / .5), ca = std::cos(b.angle),
                 sa = std::sin(b.angle), n = double(b.points.size()), LIGHT = std::atan2(-1., -1.);
    p.shade(b.x - r - 2, b.y - r - 2, b.x + r + 2, b.y + r + 2, [&](double x, double y, int sx, int sy) {
        const double dx = x - b.x, dy = y - b.y, u = dx * ca + dy * sa, v = -dx * sa + dy * ca, d = jsHypot2(u, v);
        double       ang = std::atan2(v, u);
        if (ang < 0) ang += TAU;
        const double f = (ang / TAU) * n, w = f - std::floor(f);
        const size_t i    = size_t(std::floor(f)) % b.points.size();
        const double edge = r * (b.points[i] * (1 - w) + b.points[(i + 1) % b.points.size()] * w);
        if (d > edge + .85) return;
        uint32_t c = hex(0x1a1422);
        if (d <= edge) {
            const double lit = std::cos(((double(i) + .5) / n) * TAU + b.angle - LIGHT);
            c                = d < edge * .42 ? (lit > .55 ? st.light : st.body)
                               : lit > .35    ? st.light
                               : lit < -.4    ? st.shade
                                              : st.body;
        }
        p.put(sx, sy, c, fade);
    });
}
}  // namespace eve::stylize::pixelfx::impact
