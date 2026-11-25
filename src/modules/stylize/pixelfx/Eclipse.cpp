// eve-hot-path
// Native adaptation of pixelfx shadow/eclipse.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Shadow.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double RADIUS = 19, D = 4, BEAD = -.72;
double           breathe(double t, double k, double phase) { return std::sin((TAU * k * t) / D + phase); }
struct Stream {
    double angle, length, curl, k, phase, weight;
};
struct Eclipse final : AnalyticKernel {
    std::array<Stream, 30> streams{};
    Eclipse() {
        for (size_t i = 0; i < streams.size(); ++i) {
            const double side   = i % 2 ? 0 : PI,
                         spread = (rand({uint32_t(i), 1u}) - .5) * (rand({uint32_t(i), 2u}) < .7 ? 1.3 : 3.1);
            const double a = side + spread, eq = std::abs(std::cos(a));
            streams[i] = {a,
                          (10 + rand({uint32_t(i), 3u}) * 28) * (.45 + .75 * eq),
                          (rand({uint32_t(i), 4u}) - .5) * .00018,
                          double(1 + i % 3),
                          rand({uint32_t(i), 5u}) * TAU,
                          rand({uint32_t(i), 6u})};
        }
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        const auto& Q = o.shadow.pearl;
        cycle(t, D, 26, o.seed, 1.2, 2.6, [&](double age, double life, Rng& r, int) {
            const double x = r.range(-78, 78), y = r.range(-70, 70);
            if (jsHypot2(x, y) < RADIUS + 22) return;
            const double k = std::sin((PI * age) / life);
            p.dot(x, y, k > .7 ? WHITE : Q[1], k * 1.2);
            if (k > .85 && r.next() < .4) {
                p.dot(x - 1, y, Q[0], .8);
                p.dot(x + 1, y, Q[0], .8);
            }
        });
        const double b = 1 + .08 * breathe(t, 1, 0);
        p.glow(0, 0, 72 * b, C[4], .38);
        p.glow(0, 0, 46 * b, Q[1], .34);
        for (const auto& s : streams) {
            const double L = s.length * (1 + .18 * breathe(t, s.k, s.phase));
            for (double d = 4; d <= L; d += .5) {
                const double u = d / L, a = s.angle + s.curl * d * d, r = RADIUS + 1 + d, x = std::cos(a) * r,
                             y = std::sin(a) * r;
                p.dot(x, y, u < .3 ? Q[2] : u < .65 ? Q[1] : C[5], u < .75 ? 1 : (1 - u) * 4);
            }
            p.glowLine(std::cos(s.angle) * (RADIUS + 4), std::sin(s.angle) * (RADIUS + 4),
                       std::cos(s.angle) * (RADIUS + L * .6), std::sin(s.angle) * (RADIUS + L * .6), 3, Q[1], .12);
        }
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C = o.palette;
        const auto&  Q = o.shadow.pearl;
        const double b = 1 + .08 * breathe(t, 1, 0), W = RADIUS + 10;
        p.shader(-W, -W, W, W, [&](double x, double y, ShadeOutput& out) {
            const double r = std::sqrt(x * x + y * y);
            if (r < RADIUS - .5 || r > RADIUS + 9 * b) return;
            const double a = std::atan2(y, x), e = (r - RADIUS) / (9 * b);
            const double streak =
                .55 + .3 * std::cos(a * 23 + std::sin(a * 5) * 2) + .15 * std::cos(a * 41 - (TAU * t) / D);
            const double intensity = std::pow(1 - e, 2.2) * streak * (.75 + .4 * std::abs(std::cos(a)));
            if (r < RADIUS + .9) {
                out.c = Q[3];
                return;
            }
            if (intensity < .1) return;
            if (intensity > .45 && ((int(std::floor(x)) + int(std::floor(y))) & 1) == 0) out.k = .2 * intensity;
            out.c = intensity > .72 ? Q[2] : intensity > .42 ? Q[1] : intensity > .22 ? C[5] : C[4];
        });
        p.shader(-RADIUS, -RADIUS, RADIUS, RADIUS, [&](double x, double y, ShadeOutput& out) {
            const double r = std::sqrt(x * x + y * y);
            if (r >= RADIUS - .5) return;
            out.c = r > RADIUS - 2.2 && x < -RADIUS * .45 ? C[1] : C[0];
        });
        const double bx = std::cos(BEAD) * (RADIUS + .4), by = std::sin(BEAD) * (RADIUS + .4),
                     k = .75 + .25 * breathe(t, 2, 1);
        for (int i = 0; i < 8; ++i) {
            const double a = (i * PI) / 4 + .12, len = (i % 2 ? 7 : 22) * k * (i == 1 || i == 5 ? 1.6 : 1);
            for (double d = 1; d <= len; d += .5) {
                const double u = d / len, x = bx + std::cos(a) * d, y = by + std::sin(a) * d;
                if (x * x + y * y < std::pow(RADIUS - .5, 2)) break;
                p.dot(x, y, u < .35 ? WHITE : u < .7 ? Q[2] : Q[1], u < .5 ? 1 : (1 - u) * 2.2);
            }
        }
        p.circle(bx, by, 3 * k, Q[2]);
        p.circle(bx, by, 2.2 * k, Q[3]);
        p.circle(bx, by, 1.4, WHITE);
        p.glow(bx, by, 46 * k, Q[2], .8 * k);
        p.glow(bx, by, 14, WHITE, .8);
        shadow::halo(p, 0, 0, RADIUS - .5, RADIUS + 16, Q[2], .35 * b);
        shadow::unlight(p, -RADIUS, -RADIUS, RADIUS, RADIUS,
                        [](double x, double y) { return x * x + y * y < std::pow(RADIUS - .6, 2); });
    }
};
}  // namespace
const Descriptor& eclipseDescriptor() {
    static const std::array<Option, 1> options{{{"pearl", 1, 64, false, 4, &PixelFxShadowSettings::pearl}}};
    static const Descriptor            d{
        {PixelFxKind::Eclipse, "shadow/eclipse", 160, 144, 80, 72, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 3;
            s.palette      = {hex(0x06040b), hex(0x150c26), hex(0x2a1150), hex(0x461a84),
                              hex(0x6f2cc0), hex(0x9d5cec), hex(0xcfa6ff), hex(0xf6eaff)};
            s.shadow.pearl = {hex(0x5a4f8a), hex(0xa99be0), hex(0xe6defa), hex(0xfffaf2)};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Eclipse>(); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
