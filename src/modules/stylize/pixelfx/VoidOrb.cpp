// eve-hot-path
// Native adaptation of pixelfx shadow/void-orb.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Shadow.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double Y = -62, RADIUS = 12, VR = 48, GAP = RADIUS + 9, E = RADIUS + 4.5, D = 3.2, ARMS = 3, K = 7,
                 WIND = (TAU * 2) / D;
constexpr int    N    = int(VR * 2);
constexpr std::array<double, 2> GULPS{.2, .68};
struct Source {
    double cx, cy, rho, n, phase, size, sy;
};
constexpr std::array<Source, 7> SOURCES{{{0, 0, 6, 1, 0, 2.6, .75},
                                         {1, -1, 8, -1, 2.2, 2, .6},
                                         {18, -6, 24, 1, .4, 2.4, .7},
                                         {-26, 10, 30, 1, 2.6, 2, .55},
                                         {8, 22, 20, 2, 4.1, 1.6, .8},
                                         {-12, -26, 34, 1, 5.3, 1.8, .6},
                                         {34, 18, 16, 2, 1.2, 1.4, .9}}};
double                          gulp(double t) {
    double sum = 0;
    for (double g : GULPS) sum += std::exp(-std::pow(((fract(t / D - g + .5) - .5) * D) / .07, 2));
    return sum;
}
struct Cell {
    float radius, angle, logRadius, noise;
};
void lensing(Paint& p, std::span<const uint32_t> C, double t, double g) {
    for (double a = 0, step = .7 / E; a < TAU; a += step) {
        const double f = .5 + .5 * std::cos(5 * a - (TAU * 2 * t) / D) + .5 * g;
        p.px(std::cos(a) * E, Y + std::sin(a) * E, f > .85 ? C[5] : f > .45 ? C[4] : C[3]);
    }
    for (const auto s : SOURCES) {
        const double q = (TAU * s.n * t) / D + s.phase, sx = s.cx + s.rho * std::cos(q),
                     sy = s.cy + s.rho * std::sin(q) * s.sy, b = std::max(.3, jsHypot2(sx, sy)),
                     al = std::atan2(sy, sx), root = std::sqrt(b * b + 4 * E * E);
        const std::array<std::array<double, 3>, 2> images{{{(b + root) / 2, al, 1}, {(root - b) / 2, al + PI, .55}}};
        for (const auto image : images) {
            const double r = image[0], a0 = image[1], w = image[2];
            if (r < RADIUS + 1.5) continue;
            const double mag = (r / b) * w, hw = std::min(PI, (s.size * 1.6) / (2 * b)), step = .6 / r;
            for (double a = a0 - hw; a <= a0 + hw + 1e-6; a += step) {
                const double u = hw > 0 ? std::abs(a - a0) / hw : 0, x = std::cos(a) * r, y = Y + std::sin(a) * r;
                p.dot(x, y, u < .3 && mag > .8 ? WHITE : u < .65 ? C[7] : C[5], std::min(1., mag * 1.4) * (1.15 - u));
                if (mag > 1.2 && u < .6) p.dot(x + std::cos(a), y + std::sin(a), C[6], .8);
            }
            p.glow(std::cos(a0) * r, Y + std::sin(a0) * r, 4 + std::min(10., hw * r), C[6], std::min(.5, .18 * mag));
        }
    }
}
void orb(Paint& p, std::span<const uint32_t> C, double t, double g) {
    p.shader(-RADIUS - 1, Y - RADIUS - 1, RADIUS + 1, Y + RADIUS + 1, [&](double x, double y, ShadeOutput& out) {
        const double dx = x, dy = y - Y, d = std::sqrt(dx * dx + dy * dy);
        if (d > RADIUS + .5) return;
        const double a = std::atan2(dy, dx);
        if (d > RADIUS - .6) {
            const double f = .5 * std::cos(3 * a - (TAU * 2 * t) / D) + .3 * std::cos(7 * a + (TAU * 3 * t) / D + 1) +
                             .25 * std::cos(13 * a - (TAU * 5 * t) / D + 2) + g;
            out.c          = f > .5 ? WHITE : f > .05 ? C[6] : f > -.4 ? C[5] : C[4];
            return;
        }
        if (RADIUS - d < 1.4 && std::cos(a - .8) > .6)
            out.c = C[2];
        else if (std::pow(dx + RADIUS * .42, 2) + std::pow(dy + RADIUS * .45, 2) < 1.6)
            out.c = C[3];
        else
            out.c = C[0];
    });
}
struct VoidOrb final : AnalyticKernel {
    std::array<Cell, N * N> cells{};
    VoidOrb() {
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i) {
                const double x = i - VR + .5, y = j - VR + .5, a = std::atan2(y, x), d = jsHypot2(x, y),
                             l           = std::log(std::max(1., d));
                cells[size_t(j * N + i)] = {
                    float(d), float(a), float(l),
                    float(fbm(std::cos(a) * 2.5 + l * 2.4, std::sin(a) * 2.5 + l * 2.4, 17, 3))};
            }
    }
    void vortex(Paint& p, std::span<const uint32_t> C, double t, double g) const {
        const double wf = (TAU * 12) / D;
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i) {
                const auto cell = cells[size_t(j * N + i)];
                // JS Float32Array stores once, then promotes every lookup back to Number.
                const double r = cell.radius, th = cell.angle, ln = cell.logRadius, nz = cell.noise;
                if (r < GAP || r > VR) continue;
                const double env = smooth(VR, VR * .42, r) * smooth(GAP, GAP + 4, r),
                             ph = ARMS * th + K * ln + WIND * t + nz * 2.6, cs = std::cos(ph),
                             A = (.5 + .5 * cs) * env * (1 + .4 * g);
                if (A < .16) continue;
                const double x = i - VR + .5, y = Y + j - VR + .5, hot = 1 - smooth(GAP, GAP + 16, r),
                             fil  = std::cos(12 * th + 28 * ln + wf * t + nz * 1.6),
                             dash = std::cos(9 * ln * 2.2 + 2.2 * th + wf * .75 * t);
                if (fil > .86 && A > .3 && dash > -.55) {
                    const uint32_t c = hot > .6 ? C[7] : (hot > .25 || fil > .97) && dash > 0 ? C[6] : C[5];
                    p.px(x, y, c);
                    p.light(x, y, c, .1 + .2 * hot);
                    continue;
                }
                if (A > .36 && A < .46 && std::sin(ph) > 0) {
                    p.px(x, y, hot > .4 ? C[6] : C[4]);
                    continue;
                }
                p.blend(x, y, A > .6 ? C[3] : C[2], std::min(.75, (A - .16) * 1.4));
            }
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C = o.palette;
        const double g = gulp(t);
        p.glow(0, -1, 52, C[4], .22 + .1 * g);
        vortex(p, C, t, g);
        lensing(p, C, t, g);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C = o.palette;
        const double g = gulp(t);
        cycle(t, D, 14, o.seed + 2, 1.6, 2.4, [&](double age, double L, Rng& r, int) {
            const double x0 = r.range(-44, 44), u = std::pow(age / L, 1.7), side = x0 < 0 ? -1 : 1,
                         ea = PI / 2 + side * r.range(.3, 1), ex = std::cos(ea) * RADIUS,
                         ey = Y + std::sin(ea) * RADIUS, mx = x0 * .2, my = -18 - r.range(0, 12),
                         x = (1 - u) * (1 - u) * x0 + 2 * (1 - u) * u * mx + u * u * ex,
                         y = (1 - u) * (1 - u) * -1 + 2 * (1 - u) * u * my + u * u * ey;
            if (age < .5) p.disc(x0, -1.5, 2 + age * 6, C[3], .4 * (1 - age / .5));
            p.dot(x, y, u > .6 ? C[7] : u > .25 ? C[6] : C[5], std::min(1., age * 4));
            if (u > .3) p.glow(x, y, 3, C[5], .25);
        });
        cycle(t, D, 30, o.seed, 1.1, 1.8, [&](double age, double L, Rng& r, int) {
            double th0 = r.range(0, TAU);
            if (std::sin(th0) > .75) th0 = -th0;
            const double r0 = r.range(30, 64), sw = r.range(1.4, 2.6);
            const auto   col = r.pick<uint32_t>(std::array<uint32_t, 5>{WHITE, C[7], C[6], C[6], C[5]});
            const auto   pos = [&](double aa) {
                const double u = clamp(aa / L), k = std::pow(u, 1.8), rr = RADIUS + 1 + (r0 - RADIUS - 1) * (1 - k),
                             th = th0 - sw * (k + .5 * u);
                return std::array<double, 3>{std::cos(th) * rr, Y + std::sin(th) * rr, rr};
            };
            const auto   head = pos(age), tail = pos(age - .05);
            const double a = std::min(1., age * 5);
            if (head[2] < RADIUS + 3) {
                p.px(head[0], head[1], WHITE);
                p.glow(head[0], head[1], 5, C[6], .5);
                return;
            }
            p.line(tail[0], tail[1], (tail[0] + head[0]) / 2, (tail[1] + head[1]) / 2, C[4], a * .8);
            p.line((tail[0] + head[0]) / 2, (tail[1] + head[1]) / 2, head[0], head[1], col, a);
            p.glow(head[0], head[1], 3, col, .3 * a);
        });
        cycle(t, D, 9, o.seed + 1, .42, [&](double age, double L, Rng& r, int) {
            double th = r.range(0, TAU);
            if (std::sin(th) > .45) th = -th;
            const double r0 = r.range(56, 74), u = age / L, k = (std::exp(3 * u) - 1) / (std::exp(3.) - 1),
                         rr = r0 - (r0 - RADIUS - 1) * k, len = 2 + 10 * k, ca = std::cos(th), sa = std::sin(th);
            for (double d = 0; d <= len; d += .5)
                p.dot(ca * (rr + d), Y + sa * (rr + d),
                      d < 1          ? WHITE
                      : d < len * .5 ? C[6]
                                     : C[4],
                      d < len * .5 ? 1 : 1.2 - d / len);
            p.glow(ca * rr, Y + sa * rr, 4, C[6], .4);
        });
        for (double gp : GULPS) {
            const double u = fract(t / D - gp) / (.4 / D);
            if (u >= 1) continue;
            const double rr = RADIUS + 2 + (VR - RADIUS) * (1 - ease::inQuad(u));
            shadow::ringFast(p, 0, Y, rr, 1, C[4], .35 * (1 - u));
            shadow::ringLight(p, 0, Y, rr, 4, C[5], .3 * (1 - u));
        }
        orb(p, C, t, g);
        shadow::halo(p, 0, Y, RADIUS - .5, RADIUS + 7, C[6], .6 + .3 * g);
        shadow::halo(p, 0, Y, RADIUS - .5, RADIUS + 46, C[4], .38 + .15 * g);
        shadow::unlight(p, -RADIUS, Y - RADIUS, RADIUS, Y + RADIUS,
                        [](double x, double y) { return x * x + std::pow(y - Y, 2) < std::pow(RADIUS - .6, 2); });
    }
};
}  // namespace
const Descriptor& voidOrbDescriptor() {
    static const Descriptor d{
        {PixelFxKind::VoidOrb, "shadow/void-orb", 168, 160, 84, 150, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 5;
            s.palette = {hex(0x06040b), hex(0x150c26), hex(0x2a1150), hex(0x461a84),
                         hex(0x6f2cc0), hex(0x9d5cec), hex(0xcfa6ff), hex(0xf6eaff)};
        },
        nullptr,
        {},
        nullptr,
        nullptr,
        [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<VoidOrb>(); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
