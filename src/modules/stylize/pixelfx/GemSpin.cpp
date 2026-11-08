// eve-hot-path
// Native adaptation of pixelfx pickups/gem-spin.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/PickupGem.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4, HOVER = 19;
struct GemSpin final : AnalyticKernel {
    const pickup::GemSolid mesh = pickup::brilliant(7.5, 3.2, .5, 9.5, .9);
    void                   drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C  = o.palette;
        const double ph = t / D, y = -HOVER - 1.8 * std::sin(ph * TAU), spin = ph * PI, lift = (-y - HOVER + 2) / 4;
        pickup::blobShadow(p, 0, 8 - lift * 1.5, .75 - lift * .2);
        for (int k = 0; k < 3; ++k) {
            const double a = spin * 4 + k * TAU / 3, x = std::sin(a) * 9, k2 = .5 + .5 * std::cos(a);
            if (k2 < .15) continue;
            p.dot(x, 0, k2 > .7 ? C[6] : C[5], k2 * 1.2);
            p.dot(x + 1, 0, C[4], k2 * .8);
            p.dot(x - 1, 0, C[4], k2 * .6);
            p.glow(x + .5, 0, 6, C[5], .3 * k2);
        }
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C  = o.palette;
        const double ph = t / D, y = -HOVER - 1.8 * std::sin(ph * TAU), spin = ph * PI;
        p.glowRing(0, y - 1, 5, 15, C[4], .34);
        p.glow(0, y, 26, C[3], .14);
        pickup::Layer<32, 32>       L;
        [[maybe_unused]] const auto hi = pickup::drawGem(L, p, mesh, 0, y, spin, C, {.22});
        const double                f = std::fmod(spin * 4 / TAU, 1), fk = std::max(0.0, 1 - std::abs(f - .5) / .08);
        if (fk > 0) pickup::glint(p, -3, y - 3, 3.4 * ease::outQuad(fk), C[6], .45 * fk);
        for (uint32_t i = 0; i < 8; ++i) {
            const double life = .4, age = wrap(t - double(i) / 8 * D - rand({i, o.seed, 3}) * .1, D);
            if (age > life) continue;
            const double a = rand({i, o.seed, 1}) * TAU + i * 2.4, d = 10 + rand({i, o.seed, 2}) * 7,
                         k = std::sin(age / life * PI);
            pickup::glint(p, std::cos(a) * d, y + std::sin(a) * d * .8 - age * 6,
                          jsRound(2.4 * k * (.6 + .4 * rand({i, 9}))), i % 3 ? C[6] : WHITE, .4 * k);
        }
    }
};
}  // namespace
const Descriptor& gemSpinDescriptor() {
    static const Descriptor d{
        {PixelFxKind::GemSpin, "pickups/gem-spin", 64, 64, 32, 56, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 6;
            s.palette = {hex(0x16040e), hex(0x3e0a1e), hex(0x760e2e), hex(0xb8163c),
                         hex(0xec3250), hex(0xff7482), hex(0xffc4c8), WHITE};
        },
        nullptr,
        {},
        nullptr,
        nullptr,
        [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<GemSpin>(); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
