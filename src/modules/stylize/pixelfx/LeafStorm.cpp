// eve-hot-path
// Native adaptation of pixelfx nature/leaf-storm.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/NatureLeafGeometry.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4, SY = .3, TOP = 64;
double           ringRadius(double y) { return 15 + 9 * clamp(-y / TOP); }
void             leaves(Paint& p, std::span<const uint32_t> L, double t, uint32_t seed, bool back) {
    cycle(t, D, 28, seed, 1.5, 2.3, [&](double age, double life, Rng& R, int) {
        const double k = age / life, turns = R.range(1.4, 2.2), th = R.range(0, TAU) + turns * TAU * k,
                     sn = std::sin(th);
        if ((sn > 0) == back) return;
        const double y = -lerp(1, TOP, std::pow(k, .9)) - R.range(0, 6), fling = smooth(.75, 1, k),
                     rr = ringRadius(y) * R.range(.85, 1.15) + fling * 22, x = std::cos(th) * rr, yy = y + sn * rr * SY,
                     a = std::min(1., k * 7) * (1 - smooth(.85, 1, k)), spin = R.range(-9, 9) * age + th,
                     fl                      = age * R.range(5, 11);
        const size_t o                       = size_t(R.integer(0, 2) * 4);
        const std::array<uint32_t, 4> pal    = back ? std::array<uint32_t, 4>{{L[o], L[o], L[o + 1], L[o + 2]}}
                                                    : std::array<uint32_t, 4>{{L[o], L[o + 1], L[o + 2], L[o + 3]}};
        const double                  length = R.range(6.5, 9), width = R.range(2.4, 3.3);
        nature::drawLeaf(p, x, yy, length, width, spin, fl, pal, a);
        if (!back && std::abs(std::cos(fl)) > .97) {
            p.px(x, yy, WHITE);
            p.glow(x, yy, 5, L[o + 3], .35 * a);
        }
    });
}
void wind(Paint& p, std::span<const uint32_t> C, double t, uint32_t seed, bool back) {
    for (uint32_t j = 0; j < 16; ++j) {
        Rng          R(hash({seed, j}));
        const double y0 = -R.range(4, TOP - 4), r = ringRadius(y0) * R.range(1., 1.25);
        const int    K    = R.integer(2, 3);
        const double span = R.range(1.1, 2.4), head = R.range(0, TAU) + (TAU * K * t) / D, rise = R.range(-.18, .18);
        for (double s = 0; s <= span; s += .45 / r) {
            const double th = head - s, sn = std::sin(th);
            if ((sn > 0) == back) continue;
            const double   k = s / span, x = std::cos(th) * r, y = y0 + sn * r * SY + (th - head) * rise * 4;
            const uint32_t c = back                      ? (k < .3 ? C[3] : C[2])
                               : k < .06 && R.next() < 2 ? WHITE
                               : k < .15                 ? C[5]
                               : k < .4                  ? C[4]
                                                         : C[3];
            if (k < .45)
                p.dot(x, y, c);
            else
                p.blend(x, y, c, 1.6 * (1 - k));
        }
        if (!back && std::sin(head) > 0) p.glow(std::cos(head) * r, y0 + std::sin(head) * r * SY, 5, C[4], .25);
    }
}
void swirl(Paint& p, std::span<const uint32_t> C, double t, uint32_t seed, bool back) {
    cycle(t, D, 14, seed, .6, 1.1, [&](double age, double life, Rng& R, int) {
        const double k = age / life, phase = R.range(0, TAU), speed = R.range(6, 9), th = phase + speed * age,
                     sn = std::sin(th);
        if ((sn > 0) == back) return;
        const double rr = R.range(10, 22) + k * 8, x = std::cos(th) * rr, y = -1 - k * R.range(2, 8) + sn * rr * .25,
                     s = R.range(2, 3.5) * (.6 + k);
        p.disc(x, y, s, back ? C[2] : C[3], (1 - k) * std::min(1., k * 6) * .3);
    });
    for (uint32_t j = 0; j < 6; ++j) {
        Rng          R(hash({seed + 9u, j}));
        const double rr = R.range(12, 24), th0 = R.range(0, TAU) + (TAU * 3 * t) / D, span = R.range(.5, 1);
        for (double s = 0; s < span; s += .6 / rr) {
            const double th = th0 - s;
            if ((std::sin(th) > 0) == back) continue;
            p.dot(std::cos(th) * rr, .5 + rr * .2 * std::sin(th), s < span * .4 ? C[3] : C[2], 1 - s / span);
        }
    }
}
struct LeafStorm final : AnalyticKernel {
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        const auto& L = o.nature.leaves;
        p.glow(0, -30, 46, C[4], .12);
        p.glow(0, -2, 26, L[2], .12);
        swirl(p, C, t, o.seed * 3u + 1u, true);
        wind(p, C, t, o.seed * 5u + 2u, true);
        leaves(p, L, t, o.seed * 7u + 3u, true);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        swirl(p, o.palette, t, o.seed * 3u + 1u, false);
        wind(p, o.palette, t, o.seed * 5u + 2u, false);
        leaves(p, o.nature.leaves, t, o.seed * 7u + 3u, false);
    }
};
}  // namespace
const Descriptor& leafStormDescriptor() {
    static const std::array<Option, 2> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"leaves", 1, 64, false, 12, &PixelFxNatureSettings::leaves}}};
    static const Descriptor            d{
        {PixelFxKind::LeafStorm, "nature/leaf-storm", 112, 104, 56, 92, D, true},
        6,
        [](PixelFxSettings& s) {
            s.seed         = 5;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x1a2420), hex(0x2e4038), hex(0x4a6656), hex(0x7a9a84), hex(0xb4d0b8), hex(0xeef8e8)};
            s.nature.leaves = {hex(0x123818), hex(0x2f7a2a), hex(0x6cb83a), hex(0xc8f070),
                               hex(0x4a2a08), hex(0xa8701c), hex(0xe8b42e), hex(0xfff09a),
                               hex(0x3a1010), hex(0x9a2e1c), hex(0xe0642a), hex(0xffb070)};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<LeafStorm>(); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
