// eve-hot-path
// Native adaptation of pixelfx status/slowed.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/StatusShape.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4, STEP = D / 4, R = 18;
struct Hand {
    double angle, flash, phase;
};
Hand hand(double t) {
    const double k = std::floor(t / STEP), ph = (t - k * STEP) / STEP, base = -PI / 2 + (k * TAU) / 4, q = TAU / 4;
    if (ph < .62) return {base, 0, ph};
    if (ph < .72) return {base - .1 * std::sin(((ph - .62) / .1) * PI * .5), 0, ph};
    const double s = (ph - .72) / .28, snap = ease::outBack(clamp(s / .35), 2.2),
                 wob = std::exp(-s * 6) * std::sin(s * 22) * .05;
    return {base - .1 * (1 - clamp(s / .2)) + q * snap + wob, std::exp(-s * 5), ph};
}
double afterglow(double t) {
    const double s = wrap(t - STEP * .72, STEP);
    return std::exp(-s * 4);
}
void dial(Paint& p, std::span<const uint32_t> C, double cx, double cy, double t) {
    const auto   h  = hand(t);
    const double fl = std::max(h.flash, afterglow(t));
    p.disc(cx, cy, R - 1, C[2], .2);
    p.disc(cx - R * .25, cy - R * .3, R * .5, C[4], .08);
    p.ring(cx, cy, R + 1, 1, C[0]);
    p.ring(cx, cy, R, 2, [&](double v, double angle) {
        return std::sin(angle + .8) < -.3 ? (fl > .4 ? C[7] : C[6]) : v > .5 ? C[5] : C[4];
    });
    p.ring(cx, cy, R - 3.5, 1, C[3], .6);
    for (int i = 0; i < 60; ++i) {
        if (i % 5 == 0) continue;
        const double a = (double(i) / 60) * TAU;
        p.dot(cx + std::cos(a) * (R - 2.6), cy + std::sin(a) * (R - 2.6), C[4], .8);
    }
    for (int i = 0; i < 12; ++i) {
        const double a = (double(i) / 12) * TAU, r0 = R - 2.5, r1 = R - (i % 3 == 0 ? 6 : 4.5), c = std::cos(a),
                     s = std::sin(a);
        p.line(cx + c * r0, cy + s * r0, cx + c * r1, cy + s * r1, i % 3 == 0 ? C[6] : C[5]);
    }
    const double shud = h.flash * std::sin(t * 60) * .06, ha = -PI / 2 + 2.2 + shud;
    p.beam(cx, cy, cx + std::cos(ha) * R * .5, cy + std::sin(ha) * R * .5, 2.2, C[5]);
    const double tx = cx + std::cos(h.angle) * (R - 4), ty = cy + std::sin(h.angle) * (R - 4);
    p.beam(cx, cy, tx, ty, 2.4, [&](double u) { return u > .8 ? C[7] : C[6]; });
    p.circle(cx, cy, 2, C[6]);
    p.px(cx, cy, C[1]);
    p.glow(cx, cy, R + 8, C[4], .14 + .2 * fl);
    p.glowRing(cx, cy, R, 3, C[5], .12 + .35 * fl);
    p.glow(tx, ty, 5, C[6], .3 + .3 * h.flash);
    if (h.flash > .02) {
        const double s = 1 - h.flash;
        p.ring(cx, cy, R + 2 + 10 * s, 1, C[5], h.flash);
        p.glowRing(cx, cy, R + 2 + 10 * s, 3, C[4], .3 * h.flash);
    }
}
struct Slowed final : AnalyticKernel {
    status::Shape shape;
    explicit Slowed(const PixelFxSettings& o) : shape(o.actor) {}
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        const auto& a = o.actor;
        dial(p, C, 0, a.y + 3, t);
        const double lag = afterglow(t);
        if (lag > .05) p.outline({a.width, a.height, a.mask}, a.x - 1 - jsRound(2 * lag), a.y, C[5], .55 * lag);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C  = o.palette;
        const auto&  a  = o.actor;
        const double fl = std::max(hand(t).flash, afterglow(t));
        for (const auto& q : shape.body) p.blend(a.x + q[0], a.y + q[1], C[5], .08 + .12 * fl);
        cycle(t, D, 6, o.seed + 3u, 1.6, 2.2, [&](double age, double life, Rng& r, int) {
            const double side = r.next() < .5 ? -1 : 1, x = side * r.range(10, 17), y0 = a.y + r.range(6, 22),
                         v0 = r.range(18, 30), k = 1.1;
            const auto   yAt = [&](double g) { return y0 + (v0 * (1 - std::exp(-k * g))) / k + 4 * g; };
            const double y = yAt(age), u = age / life;
            if (y < -1) {
                for (const auto q : std::array<std::array<double, 2>, 2>{{{.22, .35}, {.11, .6}}})
                    if (age > q[0]) p.dot(x, yAt(age - q[0]), C[4], q[1]);
                p.px(x, y - 1, C[5]);
                p.px(x, y, C[6]);
                p.px(x, y + 1, C[5]);
                p.px(x - 1, y + 1, C[4]);
                p.px(x - 1, y, C[7]);
                p.glow(x, y, 4, C[5], .3 * (1 - u));
            } else {
                double g = age;
                while (g > 0 && yAt(g) >= -1) g -= .02;
                const double s = clamp((age - g) / .9);
                if (s < 1) {
                    const double   rx = 1 + 7 * std::sqrt(s);
                    const uint32_t c  = s < .3 ? C[6] : C[5];
                    for (const int sd : {-1, 1}) p.line(x + sd * rx, -.5, x + sd * (rx - 1.5), -.5, c, (1 - s) * 1.2);
                    for (double dx = -rx + 2; dx <= rx - 2; dx += 1)
                        p.dot(x + dx, -1.5, C[4], (1 - s) * .5 * (1 - std::abs(dx) / rx));
                    p.glow(x, -1, 4 + rx, C[4], .12 * (1 - s));
                }
            }
        });
    }
};
}  // namespace
const Descriptor& slowedDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::Slowed, "status/slowed", 64, 76, 32, 68, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 7;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x081a24), hex(0x0e3446), hex(0x165a6e), hex(0x2a8a9a),
                              hex(0x52bcc4), hex(0x9ee6e0), hex(0xe2fff8), WHITE};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& o) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Slowed>(o); },
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
