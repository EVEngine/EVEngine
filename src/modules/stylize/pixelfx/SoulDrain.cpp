// eve-hot-path
// Native adaptation of pixelfx shadow/soul-drain.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Shadow.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double VY = -50, D = 3.2, TRAVEL = 1.45;
constexpr size_t LINES = 11, SOULS = 4;
struct Streamline {
    double sx, sy, c1x, c1y, c2x, c2y;
};
struct Soul {
    double born;
    size_t line;
};
Point along(const Streamline& L, double u) {
    const double v = 1 - u, a = v * v * v, b = 3 * v * v * u, c = 3 * v * u * u, d = u * u * u;
    return {a * L.sx + b * L.c1x + c * L.c2x, a * L.sy + b * L.c1y + c * L.c2y + d * (VY - L.sy) + d * L.sy};
}
double vessel(Paint& p, std::span<const uint32_t> C, double t, double f) {
    const double     y0 = VY + std::sin((TAU * t) / D) * 1.5;
    constexpr double W = 9, UP = 14, DN = 12;
    p.shader(-W - 1, y0 - UP - 1, W + 1, y0 + DN + 1, [&](double x, double y, ShadeOutput& out) {
        const double dy = y - y0, half = dy < 0 ? W * (1 + dy / UP) : W * (1 - dy / DN);
        if (half <= 0 || std::abs(x) > half + .6) return;
        if (std::abs(x) > half - .4) {
            out.c = f > .5 ? C[6] : C[1];
            return;
        }
        const bool ridge = std::abs(x + dy * .12) < .6, heart = std::abs(x) + std::abs(dy - 1) < 2.6 + f * 1.5;
        if (heart)
            out.c = f > .4 ? WHITE : C[6];
        else if (ridge)
            out.c = f > .3 ? C[7] : C[5];
        else if (x < 0)
            out.c = dy < 0 ? (f > .2 ? C[5] : C[4]) : C[3];
        else
            out.c = dy < 0 ? C[2] : C[1];
    });
    p.glow(0, y0, 30 + 14 * f, C[5], .5 + .5 * f);
    p.glow(0, y0 + 1, 8, WHITE, .25 + .6 * f);
    return y0;
}
struct SoulDrain final : AnalyticKernel {
    std::array<Streamline, LINES> lines{};
    std::array<Soul, SOULS>       souls{};
    SoulDrain() {
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto   n  = uint32_t(i);
            const double sx = 96 + rand({n, 1u}) * 64, sy = -88 + rand({n, 2u}) * 72, bend = (rand({n, 3u}) - .5) * 50;
            lines[i] = {sx, sy, sx * .55, sy + bend, 22 + rand({n, 4u}) * 14, VY + (sy - VY) * .25};
        }
        for (size_t j = 0; j < souls.size(); ++j) {
            const auto n = uint32_t(j);
            souls[j]     = {(double(j) + .15 + rand({n, 7u}) * .5) * (D / double(SOULS)),
                            size_t(std::floor(rand({n, 8u}) * double(LINES)))};
        }
    }
    double landings(double t) const {
        double f = 0;
        for (const auto& s : souls) {
            const double since = wrap(t - s.born - TRAVEL, D);
            f += std::exp(-since / .14) * (since < 1 ? 1 : 0);
        }
        return std::min(1.4, f);
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto&          L  = lines[i];
            const double         ph = wrap(t + double(i) * .37, D) / D;
            std::optional<Point> previous;
            for (double u = 0; u <= 1.0001; u += .01) {
                const auto   q    = along(L, u);
                const double dash = wrap(u * 5 - ph * 10 + double(i) * .3, 1);
                if (previous) p.line(previous->x, previous->y, q.x, q.y, u > .6 ? C[3] : C[2], .4 + .6 * u);
                if (dash < .18) p.px(q.x, q.y, u > .7 ? C[7] : u > .35 ? C[6] : C[5]);
                previous = q;
            }
        }
        p.glow(10, VY, 40, C[4], .22);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C = o.palette;
        const auto&  S = o.shadow.soul;
        const double f = landings(t);
        cycle(t, D, 10, o.seed, .8, 1.3, [&](double age, double Lf, Rng& r, int) {
            const double x = r.range(100, 150), y = r.range(-80, -20) - age * 18, u = age / Lf;
            p.dot(x, y, S[2], (1 - u) * .8);
            p.glow(x, y, 4, S[1], .2 * (1 - u));
        });
        cycle(t, D, 22, o.seed + 1, .4, .7, [&](double age, double Lf, Rng& r, int) {
            const auto&  L  = lines[size_t(std::floor(r.next() * double(LINES)))];
            const double u0 = r.range(.15, .5), q = u0 + (1 - u0) * std::pow(age / Lf, 2.2),
                         q2   = u0 + (1 - u0) * std::pow(std::max(0., age - .06) / Lf, 2.2);
            const auto   head = along(L, q), tail = along(L, q2);
            p.lineFn(
                tail.x, tail.y, head.x, head.y, [&](double v) { return v > .7    ? WHITE
                                                                       : v > .35 ? C[6]
                                                                                 : C[4]; }, 1.);
            p.glow(head.x, head.y, 4, C[6], .35);
        });
        const double y0 = vessel(p, C, t, f);
        for (int k = 0; k < 6; ++k) {
            const double a = (TAU * (2 * t)) / D + (k * TAU) / 6, ex = std::cos(a) * 15, ey = std::sin(a) * 5 - 2,
                         z = std::sin(a);
            p.px(ex, y0 + ey, z > 0 ? C[7] : C[4]);
            if (z > 0) p.glow(ex, y0 + ey, 4, C[6], .35);
        }
        for (size_t j = 0; j < souls.size(); ++j) {
            const auto   s   = souls[j];
            const double age = wrap(t - s.born, D);
            if (age > TRAVEL) continue;
            const auto&  L    = lines[s.line];
            const double u    = std::pow(age / TRAVEL, 1.8);
            const auto   head = along(L, u);
            if (age < .18) {
                const double k = 1 - age / .18;
                shadow::ringFast(p, L.sx, L.sy, 3 + 9 * (1 - k), 1, k > .5 ? S[2] : S[1]);
                p.glow(L.sx, L.sy, 12, S[1], .5 * k);
            }
            for (int k = 14; k >= 1; --k) {
                const double uu   = std::pow(std::max(0., age - k * .03) / TRAVEL, 1.8);
                const auto   tail = along(L, uu);
                const double w = std::sin(t * 12 + k * .8 + double(j)) * k * .16, r = std::max(.5, 2.6 - k * .17);
                p.disc(tail.x, tail.y + w, r, k < 4 ? S[2] : k < 9 ? S[1] : C[5], .75 * (1 - double(k) / 15));
            }
            const double near = smooth(.82, 1, u), hr = 3.4 - near * 1.6;
            p.circle(head.x, head.y, hr + .8, S[1]);
            p.circle(head.x, head.y, hr, S[2]);
            p.px(head.x - 1, head.y - 2, S[3]);
            p.px(head.x - 2, head.y - 1, S[3]);
            if (near < .5) {
                p.px(head.x - 1.5, head.y, S[0]);
                p.px(head.x + 1.5, head.y, S[0]);
                p.px(head.x, head.y + 1.6, S[0]);
                p.px(head.x, head.y + 2.4, S[0]);
            }
            p.glow(head.x, head.y, 10, S[1], .5);
        }
        for (const auto& s : souls) {
            const double since = wrap(t - s.born - TRAVEL, D);
            if (since < .3) {
                const double k = since / .3;
                shadow::ringFast(p, 0, y0, 6 + 18 * ease::outCubic(k), std::max(1., 2 * (1 - k)),
                                 k < .4   ? C[7]
                                 : k < .7 ? C[5]
                                          : C[3]);
            }
        }
        shadow::halo(p, 0, y0, 4, 22, C[6], .25 * f);
    }
};
}  // namespace
const Descriptor& soulDrainDescriptor() {
    static const std::array<Option, 1> options{{{"soul", 1, 64, false, 4, &PixelFxShadowSettings::soul}}};
    static const Descriptor            d{
        {PixelFxKind::SoulDrain, "shadow/soul-drain", 208, 120, 40, 110, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed        = 8;
            s.palette     = {hex(0x06040b), hex(0x150c26), hex(0x2a1150), hex(0x461a84),
                             hex(0x6f2cc0), hex(0x9d5cec), hex(0xcfa6ff), hex(0xf6eaff)};
            s.shadow.soul = {hex(0x2c3f66), hex(0x7d9fd0), hex(0xcfe6ff), hex(0xffffff)};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<SoulDrain>(); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
