// eve-hot-path
// Adapted from patch-gg/pixelfx projectiles/arrow.js (MIT).
// Copyright (c) 2026 Patch; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"

namespace eve::stylize::pixelfx {
namespace {
constexpr std::array<std::string_view, 5> HEAD{{"..LL....", "..sLLL..", "bbsSSSSW", "..dDDD..", "..DD...."}};
constexpr std::array<std::array<std::string_view, 7>, 3> FLETCH{
    {{{"AAa.........", ".aAAa.......", "..aaAAa.....", "nkkkkkkkkkkk", "..cCCc......", ".cCCc.......", "CCc........."}},
     {{".AAa........", "..aAAa......", "..aaAAAa....", "nkkkkkkkkkkk", "..cCCCc.....", "..cCCc......", ".CCc........"}},
     {{"AAAa........", "aAAa........", ".aaAAa......", "nkkkkkkkkkkk", "..ccCc......", "cCCc........",
       "CCc........."}}}};
void arrow(Paint& p, double t, const PixelFxSettings& o) {
    const auto&    PL   = o.palette;
    const auto&    W    = o.projectile.wood;
    const auto&    ST   = o.projectile.steel;
    const uint32_t wind = o.projectile.wind, S = o.seed;
    const double   D = .6, ph = t / D, left = -90, LEN = 36;
    const uint32_t fr = jsUint(std::floor(t * 30 + 1e-6));
    cycle(
        t, D, 9, S, .14, .26,
        [&](double age, double life, Rng& r, int) {
            const double k    = age / life;
            const int    top  = r.isChance(.5) ? -1 : 1;
            const double from = r.isChance(.6) ? -3 : -LEN + 6;
            const double x0 = from - age * r.range(160, 240), len = r.range(8, 16) * (.5 + k);
            const double base = r.range(3, 5), spread = r.range(1, 2.5);
            const double y = .5 + top * (base + k * spread);
            if (x0 - len < left) return;
            const uint32_t c = k < .4 ? wind : mix(wind, hex(0x2a2540), (k - .4) * 1.2);
            p.lineFn(
                x0, y, x0 - len, y, [&](double u) { return u < .25 && k < .4 ? mix(wind, WHITE, .5) : c; },
                [](double u) { return u < .65 ? 1 : (1 - u) / .35; });
        },
        true);
    cycle(
        t, D, 6, S + 3, .15, .25,
        [&](double age, double life, Rng& r, int) {
            const double k = age / life, cx = -LEN - 2 - age * r.range(100, 150);
            const int    side = r.sign();
            const double cy = .5 + side * r.range(2, 4), a = age * 30 + r.range(0, TAU), rr = 1.2 + k * 1.5;
            if (cx < left + 4) return;
            for (int i = 0; i < 4; ++i)
                p.dot(cx + std::cos(a + i * .5) * rr, cy + std::sin(a + i * .5) * rr, wind, (1 - k) * (1 - i * .2));
        },
        true);
    p.line(-LEN + 11, 0, -7, 0, W[2]);
    for (double x = -LEN + 14; x < -8; x += 6) p.px(x, 0, W[3]);
    std::array<uint32_t, 256> colors{};
    colors['a']       = PL[2];
    colors['A']       = PL[3];
    colors['c']       = PL[0];
    colors['C']       = PL[1];
    colors['k']       = W[1];
    colors['n']       = W[0];
    const size_t pose = size_t(std::floor(rand({fr, S}) * 3));
    p.sprite(FLETCH[pose], colors, -LEN, -3);
    colors      = {};
    colors['L'] = ST[3];
    colors['s'] = ST[2];
    colors['S'] = ST[2];
    colors['d'] = ST[0];
    colors['D'] = ST[1];
    colors['b'] = W[0];
    colors['W'] = WHITE;
    p.sprite(HEAD, colors, -7, -2);
    const double g = std::fmod(ph * 2, 1);
    if (g < .25) p.sparkle(-5 + g * 16, -1, 1 + jsRound(2 * std::sin((g / .25) * PI)), WHITE, ST[3], .25);
}
}  // namespace
const Descriptor& arrowDescriptor() {
    static const std::array<Option, 3> options{
        {{"wood", 1, 64, false, 4, &PixelFxProjectileSettings::wood},
         {"steel", 1, 64, false, 4, &PixelFxProjectileSettings::steel},
         {"wind", 4278190080.0, 4294967295.0, false, 0, &PixelFxProjectileSettings::wind, true}}};
    static const Descriptor descriptor{
        {PixelFxKind::Arrow, "projectiles/arrow", 112, 32, 92, 16, .6, true},
        4,
        [](PixelFxSettings& s) {
            s.seed             = 3;
            s.palette          = {hex(0x7a1f2c), hex(0xb8323c), hex(0xe8e2d6), WHITE};
            s.projectile.wood  = {hex(0x3a2414), hex(0x6e4422), hex(0xa8723c), hex(0xd8a868)};
            s.projectile.steel = {hex(0x2e3648), hex(0x5a6680), hex(0x9aa6ba), hex(0xdfe7f2)};
            s.projectile.wind  = hex(0xb8c8e8);
        },
        &arrow,
        options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
