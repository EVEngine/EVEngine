// eve-hot-path
// Native adaptation of pixelfx water/rain-splash.js (MIT), including Sprite Fusion rain streaks.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Water.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4, SY = .3, RX = 92, G = 640, LAMP = 34;
struct Drop {
    double   x, z, t0, H, J, spin;
    uint32_t i;
};
std::array<Drop, 8> drops(uint32_t seed) {
    std::array<Drop, 8> out{};
    for (uint32_t i = 0; i < out.size(); ++i)
        out[i] = {(rand({i, seed}) - .5) * 120,
                  (rand({i, seed + 1}) - .5) * 50,
                  ((i + .25 * rand({i, seed + 2})) / 8) * D,
                  10 + 4 * rand({i, seed + 3}),
                  7 + 4 * rand({i, seed + 4}),
                  rand({i, seed + 5}) * TAU,
                  i};
    return out;
}
double ringR(double a) { return 3 + 34 * std::pow(a / 1.8, .7); }
double ringK(double a) { return a < 1.8 ? 1.3 * std::pow(1 - a / 1.8, 1.3) * std::min(1., a / .06) : 0; }
double crownH(double a, double H) { return H * std::pow(std::sin(PI * clamp(a / .3)), .8) * (a < .3 ? 1 : 0); }
double crownR(double a) { return 1.5 + 4.5 * std::sqrt(clamp(a / .3)); }
double jetH(double a, double J) { return a > .26 && a < .56 ? J * std::sin((PI * (a - .26)) / .3) : 0; }
struct Swell {
    double x, z, a, r, k;
};
double field(double x, double z, std::span<const Swell> list) {
    double h = 0;
    for (const auto& q : list) {
        const double dx = x - q.x;
        if (dx > q.r + 10 || dx < -q.r - 10) continue;
        const double d = jsHypot2(dx, z - q.z) - q.r, w = 3.6 + q.a * 3;
        if (d > 2 * w || d < -3 * w) continue;
        h += q.k * std::cos(d * 1.1) * std::exp(-std::pow(d / w, 2));
    }
    return h;
}
void crown(Paint& p, std::span<const uint32_t> C, const Drop& q, double a, bool back) {
    const double h = crownH(a, q.H);
    if (h < 1) return;
    const double r0 = crownR(a), cy = q.z * SY;
    for (uint32_t f = 0; f < 10; ++f) {
        const double ang = q.spin + (double(f) / 10) * TAU, sn = std::sin(ang);
        if ((sn < 0) != back) continue;
        const double tip = h * (.75 + .25 * rand({q.i, f, 7})), lean = .35 * tip, x0 = q.x + std::cos(ang) * r0,
                     y0 = cy + sn * r0 * SY, x1 = q.x + std::cos(ang) * (r0 + lean),
                     y1 = cy + sn * (r0 + lean) * SY - tip;
        p.lineFn(
            x0, y0, x1, y1,
            [&](double u) { return back      ? (u > .7 ? C[5] : C[3])
                                   : u > .75 ? WHITE
                                   : u > .35 ? C[6]
                                             : C[5]; }, 1.);
        if (a > .08 && a < .17) p.px(x1, y1 - 1, back ? C[5] : WHITE);
    }
    if (!back) p.glow(q.x, cy - h * .6, 6 + h * .6, C[5], .3 * (h / q.H));
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    const auto&          C  = o.palette;
    const double         ry = RX * SY, ph = t / D;
    const auto           dd = drops(o.seed);
    std::array<Swell, 8> list{};
    size_t               count = 0;
    for (const auto& q : dd) {
        const double a = wrap(t - q.t0, D);
        if (a < 1.8) list[count++] = {q.x, q.z, a, ringR(a), ringK(a)};
    }
    for (int Y = int(std::floor(-ry)); Y < ry; ++Y) {
        const double         y = Y + .5, z = y / SY, far = (y + ry) / (2 * ry);
        std::array<Swell, 8> act{};
        size_t               active = 0;
        for (size_t j = 0; j < count; ++j)
            if (std::abs(z - list[j].z) <= list[j].r + 12) act[active++] = list[j];
        const double dashW =
            (1.5 + far * 4) * (.35 + .65 * (.5 + .5 * std::sin(Y * 1.73 + ph * TAU * 3 + std::sin(Y * .61) * 3)));
        double hPrev = 0;
        for (int X = -88; X < 88; ++X) {
            const double x = X + .5, d = std::pow(x / RX, 2) + std::pow(y / ry, 2);
            if (d > 1) continue;
            const double edge = std::min(1., (1 - d) * 6);
            p.blend(x, y, C[0], (.62 + .2 * far) * edge);
            if (far < .45) p.blend(x, y, C[2], .3 * (1 - far / .45) * edge);
            const double h = active ? field(x, z, std::span<const Swell>(act.data(), active)) : 0, hx = h - hPrev;
            hPrev = h;
            if (h > .24)
                p.blend(x, y, h > .75 ? C[5] : C[4], std::min(.95, (h - .24) * 2.2) * edge);
            else if (h < -.4)
                p.blend(x, y, C[0], std::min(.85, (-h - .4) * 1.8) * edge);
            if (x < LAMP - 14 || x > LAMP + 14) continue;
            const double lx = std::abs(x - LAMP - hx * 9 - std::sin(y * .8 + ph * TAU * 2) * 1.4);
            if (lx < dashW) {
                const double k = (1 - lx / dashW) * edge;
                p.px(X, Y, k > .72 ? WHITE : k > .35 ? C[6] : C[5]);
                p.light(x, y, C[5], .2 * k);
            }
        }
    }
    for (int X = -82; X < 82; ++X) {
        const double x = X + .5, y = -std::sqrt(std::max(0., 1 - std::pow(x / RX, 2))) * ry;
        p.px(X, y + .5, std::abs(x - LAMP) < 10 ? C[6] : C[3]);
    }
    for (const auto& q : dd) crown(p, C, q, wrap(t - q.t0, D), true);
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto& C  = o.palette;
    const auto  dd = drops(o.seed);
    for (const auto& q : dd) {
        const double a = wrap(t - q.t0, D), cy = q.z * SY, pre = D - a;
        if (pre < .14 && cy - pre * 520 - 10 > -48) {
            const double y = cy - pre * 520;
            p.line(q.x, y - 10, q.x, y - 4, C[4]);
            p.line(q.x, y - 4, q.x, y, C[6]);
        }
        if (a < .05) {
            p.px(q.x, cy, WHITE);
            p.glow(q.x, cy - 2, 12, C[5], .5 * (1 - a / .05));
        }
        crown(p, C, q, a, false);
        if (a > .16 && a < .8)
            for (uint32_t f = 0; f < 10; f += 2) {
                const double ang = q.spin + (double(f) / 10) * TAU,
                             tip = crownH(.16, q.H) * (.75 + .25 * rand({q.i, f, 7})), r0 = crownR(.16) + tip * .35,
                             v = 22 + 18 * rand({q.i, f, 8}), vh = 40 + 30 * rand({q.i, f, 9}), tt = a - .16,
                             hh = tip + vh * tt - .5 * G * tt * tt, bx = q.x + std::cos(ang) * (r0 + v * tt),
                             bz = q.z + std::sin(ang) * (r0 + v * tt), by = bz * SY;
                if (hh > 0)
                    water::bead(p, bx, by - hh, std::cos(ang) * v, -(vh - G * tt) + std::sin(ang) * v * SY,
                                rand({q.i, f, 10}) < .4 ? 2 : 1, C);
                else {
                    const double land = (vh + std::sqrt(vh * vh + 2 * G * tip)) / G, since = tt - land;
                    if (since < .05) p.px(bx, by, WHITE);
                    if (since < .35)
                        water::ring(p, bx, by, 1 + since * 12, SY, 1 - since / .35, C,
                                    {1, 3, 1, false, 0, q.i * 10 + f});
                }
            }
        const double jh = jetH(a, q.J);
        if (jh > .8) {
            p.lineFn(q.x, cy - 1, q.x, cy - jh, [&](double u) { return u > .6 ? WHITE : C[6]; }, 1.);
            p.px(q.x + 1, cy - 1, C[5]);
            if (a < .41) p.circle(q.x, cy - jh - .5, 1.2, C[6]);
            p.glow(q.x, cy - jh * .6, 6, C[5], .3);
        }
        const double ba = a - .41, bh = q.J + 1 + 24 * ba - .5 * G * ba * ba;
        if (ba > 0 && bh > 0) {
            p.circle(q.x, cy - bh, 1.1, C[6]);
            p.px(q.x, cy - bh - 1, WHITE);
        }
    }
    for (const auto& q : dd) {
        const double a = wrap(t - q.t0, D);
        if (a < 1.8) water::ring(p, q.x, q.z * SY, ringR(a), SY, .75 * ringK(a), C, {1, 3, 1, a < .1, 0, q.i, .5 + a});
    }
    cycle(t, D, 16, o.seed + 20, .5, .5, [&](double age, double, Rng& rng, int) {
        const double x = rng.range(-80, 80), z = rng.range(-26, 26), y = z * SY, s = age - .1;
        if (s < 0) {
            const double yy = y - (.1 - age) * 600;
            if (yy - 6 < -48) return;
            p.lineFn(x, yy - 6, x, yy, [&](double u) { return u > .6 ? C[5] : C[3]; }, 1.);
        } else if (s < .4 && std::pow(x / RX, 2) + std::pow(y / (RX * SY), 2) < .9) {
            if (s < .04) p.px(x, y, C[6]);
            water::ring(p, x, y, 1 + s * 9, SY, .8 * (1 - s / .4), C, {1, 3, 1, false, 0, 40});
        }
    });
    p.glow(LAMP, -6, 30, C[4], .16);
}
}  // namespace
const Descriptor& rainSplashDescriptor() {
    static const Descriptor d{{PixelFxKind::RainSplash, "water/rain-splash", 176, 80, 88, 50, D, true},
                              8,
                              [](PixelFxSettings& s) { water::defaults(s, 9); },
                              front,
                              {},
                              nullptr,
                              back};
    return d;
}
}  // namespace eve::stylize::pixelfx
