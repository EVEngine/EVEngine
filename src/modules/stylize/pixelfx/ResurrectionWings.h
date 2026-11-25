// Native adaptation of pixelfx holy/resurrection.js wing geometry, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/HolyFeathers.h"
namespace eve::stylize::pixelfx::holy {
constexpr double               WING_OPEN = 1.1, BEAT0 = 1.95, BEAT1 = 2.25, BEAT2 = 2.37, BEAT3 = 2.8, SCATTER = 2.95;
constexpr std::array<Point, 4> ARM_OPEN{{{0, 0}, {7, -9}, {15, -19}, {20, -27}}},
    ARM_FOLD{{{0, 0}, {2, -2}, {3, -3}, {4, -3}}};
/** @brief Sample a four-point source arm synchronously, retaining no input reference. */
inline Point along(const std::array<Point, 4>& points, double s) {
    std::array<double, 3> lengths{};
    double                L = 0;
    for (size_t i = 1; i < 4; ++i) {
        lengths[i - 1] = jsHypot2(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
        L += lengths[i - 1];
    }
    double d = s * L;
    for (size_t i = 0; i < 3; ++i) {
        if (d <= lengths[i] || i == 2) {
            const double f = lengths[i] ? clamp(d / lengths[i]) : 0;
            return {lerp(points[i].x, points[i + 1].x, f), lerp(points[i].y, points[i + 1].y, f)};
        }
        d -= lengths[i];
    }
    return points.back();
}
/** @brief Sample the source raise/downstroke/recovery curve at injected time. */
inline double wingBeat(double t) {
    if (t < BEAT0) return 0;
    if (t < BEAT1) return -.34 * ease::inOutSine(seg(t, BEAT0, BEAT1));
    if (t < BEAT2) return lerp(-.34, .66, ease::inQuad(seg(t, BEAT1, BEAT2)));
    return .66 * (1 - ease::outBack(seg(t, BEAT2, BEAT3), 1.3));
}
struct Quill {
    double                s, angle, length, width;
    int                   kind;
    double                fold = 0, open = 0;
    std::array<double, 2> off{};
    std::array<bool, 2>   gone{};
};
struct WingFeather {
    size_t index;
    double x, y, angle, length, width, alpha;
    bool   hot;
};
struct WingPose {
    std::array<Point, 4>        arm;
    std::array<WingFeather, 18> feathers;
    size_t                      count = 0;
    /** @brief Return an owning sampled value, or no value after its quill has scattered. */
    std::optional<WingFeather> find(size_t index) const {
        for (size_t i = 0; i < count; ++i)
            if (feathers[i].index == index) return feathers[i];
        return std::nullopt;
    }
};
/** @brief Instance-owned quill recipes and scatter flags; caller-thread affine, no retained RNG/painter borrows. */
struct ResurrectionWings {
    std::array<Quill, 18>  quills{{{.14, 1.35, 11, 2.6, 1},
                                   {.26, 1.12, 14, 2.8, 1},
                                   {.38, .88, 17, 3, 1},
                                   {.5, .62, 19, 3.1, 1},
                                   {.62, .35, 21, 3.2, 1},
                                   {.72, .08, 23, 3.3, 1},
                                   {.8, -.2, 26, 3.4, 0},
                                   {.85, -.45, 29, 3.5, 0},
                                   {.9, -.72, 31, 3.6, 0},
                                   {.95, -1, 30, 3.6, 0},
                                   {1, -1.3, 26, 3.4, 0},
                                   {.22, 1.2, 7, 2.2, 2},
                                   {.34, .98, 8, 2.3, 2},
                                   {.46, .75, 9, 2.4, 2},
                                   {.58, .5, 10, 2.5, 2},
                                   {.7, .22, 10, 2.5, 2},
                                   {.81, -.1, 11, 2.6, 2},
                                   {.92, -.5, 11, 2.6, 2}}};
    std::array<size_t, 18> order{};
    /** @brief Prepare scatter times from the injected source stream once after the effect's ray/mote draws.
     * @cost Bounded18-quill preparation per configuration/seek, reused by every wing sample. */
    explicit ResurrectionWings(Rng& rng) {
        std::array<double, 18> reach{};
        double                 far = 0;
        for (size_t i = 0; i < 18; ++i) {
            const auto& q = quills[i];
            const auto  b = along(ARM_OPEN, q.s);
            reach[i]      = jsHypot2(b.x + std::cos(q.angle) * q.length, b.y + std::sin(q.angle) * q.length);
            far           = std::max(far, reach[i]);
        }
        for (size_t i = 0; i < 18; ++i) {
            auto& q = quills[i];
            q.fold  = 1.45 + double(i % 6) * .04;
            q.open  = q.kind == 0 ? .06 + (q.s - .8) * .45 : q.kind == 1 ? q.s * .06 : .03 + q.s * .05;
            for (auto& off : q.off) off = SCATTER + (1 - reach[i] / far) * .2 + rng.range(0, .04);
            order[i] = i;
        }
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t a, size_t b) { return quills[a].angle > quills[b].angle; });
    }
    /** @brief Sample owned wing geometry at explicit time/side with no allocation or mutable scratch. */
    WingPose sample(double tt, int side) const {
        const double         oa = ease::outBack(seg(tt, WING_OPEN, WING_OPEN + .4), 1.6), rot = wingBeat(tt),
                             om = (wingBeat(tt) - wingBeat(tt - .03)) / .03;
        std::array<Point, 4> arm{};
        for (size_t i = 0; i < 4; ++i)
            arm[i] = {lerp(ARM_FOLD[i].x, ARM_OPEN[i].x, oa), lerp(ARM_FOLD[i].y, ARM_OPEN[i].y, oa)};
        const double cr = std::cos(rot), sr = std::sin(rot), alpha = smooth(WING_OPEN, WING_OPEN + .1, tt);
        const auto   place = [&](Point p) -> Point {
            return {side * (4 + p.x * cr - p.y * sr), -20 + p.x * sr + p.y * cr};
        };
        constexpr std::array<double, 3> flex{1, .5, .3};
        WingPose                        pose{};
        for (size_t i = 0; i < 4; ++i) pose.arm[i] = place(arm[i]);
        for (size_t i = 0; i < 18; ++i) {
            const auto&  q   = quills[i];
            const double off = q.off[side > 0 ? 0 : 1];
            if (tt >= off + .05) continue;
            const double of = ease::outBack(seg(tt, WING_OPEN + q.open, WING_OPEN + q.open + .42), 2);
            const auto   b  = place(along(arm, q.s));
            const double a  = lerp(q.fold, q.angle, of) + rot - clamp(om * .045 * flex[size_t(q.kind)], -.5, .5);
            pose.feathers[pose.count++] = {i,
                                           b.x,
                                           b.y,
                                           side > 0 ? a : PI - a,
                                           q.length * lerp(.3, 1, of),
                                           q.width,
                                           alpha,
                                           tt < WING_OPEN + q.open + .07 || tt >= off};
        }
        return pose;
    }
    /** @brief Draw sampled feathers, leading edge and downstroke trails through synchronous painter/ramp borrows. */
    void draw(Paint& p, double t, int side, std::span<const uint32_t> C) const {
        const auto pose = sample(t, side);
        if (!pose.count) return;
        const std::array<uint32_t, 6> ramp{C[2], C[3], C[4], C[5], C[6], WHITE};
        for (bool coverts : {false, true})
            for (auto i : order) {
                const auto f = pose.find(i);
                if (f && (quills[i].kind == 2) == coverts)
                    feather(p, f->x, f->y, f->angle, f->length, f->width, ramp, f->alpha, side, f->hot ? 1. : 0.);
            }
        const double k = double(pose.count) / 18, a = pose.feathers[0].alpha;
        if (k > .5) {
            auto lower = pose.arm;
            for (auto& q : lower) q.y += 1;
            p.polyline(lower, C[5], a);
            p.polyline(pose.arm, WHITE, a);
        }
        p.glow(side * 28, -36, 38, C[4], .26 * a * k);
        const double g = std::fmod((t - WING_OPEN - .45) * 1.3, 1.);
        if (k > .9 && t > WING_OPEN + .45 && g < .8) {
            const auto q = along(pose.arm, g / .8);
            p.sparkle(q.x, q.y, 2 + std::sin((g / .8) * PI) * 2, WHITE, C[6], .4);
        }
        p.glowLine(pose.arm[0].x, pose.arm[0].y, pose.arm[3].x, pose.arm[3].y, 7, C[5], .25 * a * k);
        if (t > BEAT1 + .03 && t < BEAT2 + .14)
            for (size_t i : {6u, 8u, 10u}) {
                std::array<Point, 10> points{};
                size_t                n = 0;
                for (int j = 0; j < 10; ++j) {
                    const auto w = sample(std::max(BEAT1 + .01, t - j * .012), side);
                    if (const auto f = w.find(i))
                        points[n++] = {f->x + std::cos(f->angle) * (f->length - 1),
                                       f->y + std::sin(f->angle) * (f->length - 1)};
                }
                const double fade = 1 - smooth(BEAT2, BEAT2 + .14, t);
                for (size_t j = 1; j < n; ++j)
                    p.line(points[j - 1].x, points[j - 1].y, points[j].x, points[j].y,
                           j < 3   ? WHITE
                           : j < 6 ? C[6]
                                   : C[4],
                           fade * (1.1 - double(j) / 10));
            }
    }
};
}  // namespace eve::stylize::pixelfx::holy
