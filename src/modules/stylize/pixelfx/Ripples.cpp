// eve-hot-path
// Native adaptation of pixelfx water/ripples.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Water.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2, SY = .26, EVERY = .5, LIFE = 2, RMAX = 60, PATCH = 64;
double           radius(double a) { return 2 + (RMAX - 2) * ease::outCubic(a / LIFE); }
double           power(double a) { return std::pow(1 - a / LIFE, 1.5) * std::min(1., a / .08); }
struct Ripple {
    double   age;
    uint32_t index;
};
std::array<Ripple, 4> alive(double t) {
    std::array<Ripple, 4> out{};
    for (int k = 0; k < 4; ++k) {
        const double a = wrap(t - k * EVERY, D);
        out[size_t(k)] = {a, jsUint(jsRound(wrap(t - a, D) / EVERY))};
    }
    return out;
}
void surface(Paint& p, std::span<const uint32_t> C, double t) {
    struct Swell {
        double r, k, w;
    };
    std::array<Swell, 4> rs{};
    const auto           list = alive(t);
    for (size_t i = 0; i < list.size(); ++i) {
        const double a = list[i].age;
        rs[i]          = {radius(a), power(a), 3 + a * 2.2};
    }
    for (int Y = int(std::floor(-PATCH * SY)); Y < PATCH * SY; ++Y)
        for (int X = -int(PATCH); X < PATCH; ++X) {
            const double x = X + .5, y = Y + .5, z = y / SY, r = jsHypot2(x, z), e = r / PATCH;
            if (e > 1) continue;
            const double edge = 1 - smooth(.6, 1, e);
            p.blend(x, y, C[1], .55 * edge);
            double slope = 0;
            for (const auto& q : rs) {
                const double d = r - q.r;
                if (d > 2.2 * q.w || d < -2.2 * q.w) continue;
                slope += q.k * std::exp(-std::pow(d / q.w, 2)) * -std::sin(d * 1.15) * 1.15;
            }
            if (slope == 0) continue;
            const double face = slope * (-(x * -.55 + z * -.84) / std::max(1., r));
            if (face > .18)
                p.blend(x, y, face > .6 ? C[5] : C[4], std::min(.9, (face - .18) * 2.2) * edge);
            else if (face < -.25)
                p.blend(x, y, C[0], std::min(.8, (-face - .25) * 2) * edge);
        }
}
void rings(Paint& p, std::span<const uint32_t> C, double t, int half) {
    for (const auto& q : alive(t)) {
        const double a = q.age, k = power(a);
        water::ring(p, 0, 0, radius(a), SY, .85 * k, C,
                    {2, 3 + a * 1.4, half, a < .14, half > 0 && a > .1 ? k : 0, q.index + 1, .4 + a * 1.1});
    }
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    surface(p, o.palette, t);
    rings(p, o.palette, t, -1);
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto& C = o.palette;
    rings(p, C, t, 1);
    const auto all = alive(t);
    for (const auto& q : all) {
        const double a = q.age;
        if (a < .08) {
            p.px(0, 0, WHITE);
            p.px(-1, 0, C[6]);
            p.px(1, 0, C[6]);
            p.glow(0, 0, 8, C[5], .5 * (1 - a / .08));
        }
        const double b = a - .04, h = 90 * b - .5 * 700 * b * b;
        if (b > 0 && h > 0) {
            p.px(0, -h, WHITE);
            p.px(0, -h + 1, C[5]);
        }
    }
    cycle(t, D, 10, o.seed + 5, .25, .45, [&](double age, double life, Rng& rng, int) {
        const double a   = all[size_t(std::floor(rng.next() * double(all.size())))].age;
        const double ang = rng.range(.3, PI - .3), R = radius(a), k = std::sin((PI * age) / life) * power(a);
        if (k < .15 || R < 6) return;
        const double x = std::cos(ang) * R, y = std::sin(ang) * R * SY;
        p.px(x, y, WHITE);
        if (k > .5)
            for (const Point off : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                p.px(x + off.x, y + off.y, C[6]);
        p.glow(x, y, 5, C[5], .4 * k);
    });
    p.glow(0, 0, 40, C[4], .14);
    p.glow(0, 0, 14, C[5], .12);
}
}  // namespace
const Descriptor& ripplesDescriptor() {
    static const Descriptor d{{PixelFxKind::Ripples, "water/ripples", 136, 52, 68, 30, D, true},
                              8,
                              [](PixelFxSettings& s) { water::defaults(s, 2); },
                              front,
                              {},
                              nullptr,
                              back};
    return d;
}
}  // namespace eve::stylize::pixelfx
