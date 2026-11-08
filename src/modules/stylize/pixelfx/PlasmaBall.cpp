// eve-hot-path
// Native adaptation of pixelfx lightning/plasma-ball.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double GY = -55, RG = 24;
struct Tendril {
    double a, ex, ey, w1, w2;
    bool   front, rim;
};
Tendril tendril(int i, int n, double ph, uint32_t seed) {
    const uint32_t j = uint32_t(i) * 7;
    const double   a = ((i + .5) / n) * TAU + .9 * loopNoise(ph, .9, seed + j), z = loopNoise(ph, .7, seed + j + 3),
                   rho = RG * (.72 + .28 * std::sqrt(1 - std::min(1., z * z * 1.6)));
    return {a,
            std::cos(a) * rho,
            GY + std::sin(a) * rho,
            loopNoise(ph, 1.1, seed + j + 5),
            loopNoise(ph, 1.3, seed + j + 6),
            z > -.15,
            rho > RG * .93};
}
void draw(Paint& p, double t, const PixelFxSettings& o) {
    const auto&          C  = o.palette;
    const auto           f  = jsUint(wrap(std::floor(t * 30 + 1e-6), 90));
    const double         ph = f / 90.;
    const int            n  = o.lightning.tendrils;
    Rng                  R(hash({o.seed, 3, f}));
    const double         pulse = .85 + .15 * std::sin(ph * TAU * 6) + .1 * (R.next() - .5);
    std::vector<Tendril> ts;
    ts.reserve(size_t(n));
    for (int i = 0; i < n; ++i) ts.push_back(tendril(i, n, ph, o.seed));
    const std::array<uint32_t, 6> BASE{
        {hex(0x0d0b12), hex(0x17131f), hex(0x231d2d), hex(0x342b43), hex(0x4b3f60), hex(0x6b5c87)}};
    p.glow(0, -6, 52, C[3], .22 * pulse);
    p.shader(-18, -32, 18, 0, [&](double x, double y, ShadeOutput& out) {
        const double hw = y < -27 ? 7 : y < -2 ? 9 + ((y + 27) / 25) * 6 : 16;
        if (std::abs(x) > hw) return;
        const double u = (x / hw + 1) / 2;
        if (y < -27) {
            out.c = y < -30 ? (u < .6 ? C[4] : BASE[5]) : u < .3 ? BASE[5] : u < .75 ? BASE[3] : BASE[1];
            return;
        }
        if (y >= -2) {
            out.c = y < -1 ? (u < .55 ? BASE[4] : BASE[2]) : BASE[1];
            return;
        }
        const int i = u < .1 ? 2 : u < .2 ? 3 : u < .3 ? 5 : u < .42 ? 4 : u < .6 ? 3 : u < .8 ? 2 : u < .92 ? 1 : 0;
        out.c       = y < -24 && u < .5 ? mix(BASE[size_t(i)], C[4], .3 * pulse)
                      : y > -5          ? BASE[size_t(std::max(0, i - 1))]
                                        : BASE[size_t(i)];
    });
    p.px(9, -9, C[5]);
    p.glow(9, -9, 3, C[5], .5);
    p.shader(-RG, GY - RG, RG, GY + RG, [&](double x, double y, ShadeOutput&) {
        const double d = jsHypot2(x, y - GY);
        if (d < RG - .5) p.blend(x, y, d > RG - 4 ? C[1] : C[0], .22 + .4 * std::pow(d / RG, 4));
    });
    p.glow(0, GY, RG + 14, C[3], .32 * pulse);
    p.glowRing(0, GY, RG - 1, 5, C[3], .22 * pulse);
    const auto drawTendril = [&](const Tendril& q, uint32_t i, bool bright) {
        const double       sx = std::cos(q.a) * 4.5, sy = GY + std::sin(q.a) * 4.5, dx = q.ex - sx, dy = q.ey - sy,
                           L = jsHypot2(dx, dy), nx = -dy / L, ny = dx / L;
        Rng                r(hash({o.seed, 17, i, f}));
        std::vector<Point> pts;
        const int          m = std::max(4, int(jsRound(L / 2.2)));
        for (int j = 0; j <= m; ++j) {
            const double u = double(j) / m, w = 3.2 * q.w1 * std::sin(PI * u) + 1.6 * q.w2 * std::sin(TAU * u + i),
                         jit = j && j < m ? r.range(-.7, .7) : 0;
            pts.push_back({sx + dx * u + nx * (w + jit), sy + dy * u + ny * (w + jit)});
        }
        struct Path {
            std::vector<Point> pts;
            double             w;
        };
        std::vector<Path> paths{{pts, 1}};
        for (int k = 0; k < 3; ++k) {
            const auto   b = pts[size_t(m - 2)];
            const double a = q.a + (k - 1) * r.range(.25, .45), l = r.range(2.5, 5),
                         mx = b.x + std::cos(a) * l * .5 + r.range(-.6, .6),
                         my = b.y + std::sin(a) * l * .5 + r.range(-.6, .6);
            paths.push_back({{{b.x, b.y}, {mx, my}, {b.x + std::cos(a) * l, b.y + std::sin(a) * l}}, .5});
        }
        const bool hot = r.next() < .2;
        for (const auto& q2 : paths)
            for (size_t j = 0; j + 1 < q2.pts.size(); ++j) {
                const auto   a = q2.pts[j], b = q2.pts[j + 1];
                const double u = double(j) / double(q2.pts.size() - 1);
                if (bright && q2.w >= 1) p.line(a.x + 1, a.y + 1, b.x + 1, b.y + 1, C[3]);
                const auto c = !bright          ? (q2.w >= 1 ? C[3] : C[2])
                               : q2.w < 1       ? C[6]
                               : hot || u > .75 ? C[6]
                               : u < .2         ? C[6]
                                                : C[5];
                p.line(a.x, a.y, b.x, b.y, c);
                if (q2.w >= 1 && j % 3 == 0) p.glow(a.x, a.y, bright ? 6 : 4, C[4], bright ? (hot ? .4 : .24) : .12);
            }
        if (bright) {
            p.circle(q.ex, q.ey, 1.6, C[6]);
            p.px(q.ex, q.ey, WHITE);
            p.glow(q.ex, q.ey, 9, C[5], .55);
            if (q.rim)
                for (int s : {-1, 1})
                    p.px(std::cos(q.a + s * .09) * (RG - .5), GY + std::sin(q.a + s * .09) * (RG - .5), C[6]);
        } else
            p.glow(q.ex, q.ey, 6, C[4], .25);
    };
    for (size_t i = 0; i < ts.size(); ++i)
        if (!ts[i].front) drawTendril(ts[i], uint32_t(i), false);
    const double cr = 3.6 + .5 * pulse;
    p.circle(0, GY, cr + 1.3, C[3]);
    p.circle(0, GY, cr, C[5]);
    p.circle(-.6, GY - .6, cr * .55, C[6]);
    p.circle(-.8, GY - .8, cr * .25, WHITE);
    p.glow(0, GY, 16, C[5], .65 * pulse);
    for (size_t i = 0; i < ts.size(); ++i)
        if (ts[i].front) drawTendril(ts[i], uint32_t(i), true);
    p.ring(0, GY, RG + 1.6, 1, C[1]);
    p.ring(0, GY, RG + .6, 1.1, [&](double, double a) {
        return a > 2.2 && a < 3.3 ? C[5] : a > .3 && a < 1.3 ? C[4] : C[3];
    });
    p.arc(0, GY, RG - 1.6, 1.3, 3.55, 4.55, [&](double u) { return u > .25 && u < .7 ? WHITE : C[6]; });
    for (double a = .35; a < 1.15; a += .045) p.dot(std::cos(a) * (RG - 2.6), GY + std::sin(a) * (RG - 2.6), C[4], .85);
    p.px(-RG * .42, GY - RG * .62, WHITE);
}
}  // namespace
const Descriptor& plasmaBallDescriptor() {
    static const std::array<Option, 1> options{{{"tendrils", 0, 64, false, 0, &PixelFxLightningSettings::tendrils}}};
    static const Descriptor            d{{PixelFxKind::PlasmaBall, "lightning/plasma-ball", 112, 104, 56, 96, 3, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed               = 6;
                                  s.lightning.tendrils = 9;
                                  s.palette            = {hex(0x1a0828), hex(0x3b0f5e), hex(0x6a1d9c), hex(0xa03ad6),
                                                          hex(0xd46cf2), hex(0xf2a8ff), hex(0xffe2ff), WHITE};
                                         },
                                         draw,
                                         options};
    return d;
}
}  // namespace eve::stylize::pixelfx
