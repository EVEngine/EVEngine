// eve-hot-path
// Native adaptation of pixelfx transitions/crt-off.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScreenTransition.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double YC = 90, XC = 160, SQ0 = .06, SQ1 = .34, LN1 = .56, DOT1 = .86, ON0 = 1.06, ON1 = 1.16, OPEN1 = 1.42;
std::pair<double, double> shape(double t) {
    if (t < SQ0) return {screen::H, XC};
    if (t < SQ1) return {std::max(1., screen::H * (1 - ease::inCubic((t - SQ0) / (SQ1 - SQ0)))), XC};
    if (t < LN1) return {1, XC * (1 - ease::inQuad((t - SQ1) / (LN1 - SQ1)))};
    if (t < ON0) return {0, 0};
    if (t < ON1) return {1, XC * ease::outExpo((t - ON0) / (ON1 - ON0))};
    return {std::max(1., screen::H * ease::outCubic(clamp((t - ON1) / (OPEN1 - ON1)))), XC};
}
void crtOff(Paint& p0, double t, const PixelFxSettings& o) {
    const auto& C     = o.palette;
    const auto  ink   = o.transition.color;
    auto        p     = screen::clip(p0);
    const auto [h, w] = shape(t);
    const double fl   = .85 + .15 * rand({jsUint(std::floor(t * 30)), 7, o.seed});
    if (t >= OPEN1 + .02 && t < OPEN1 + .6) {
        const double u = (t - OPEN1) / .6, band = -20 + u * (screen::H + 40);
        for (int y = 0; y < screen::H; ++y) {
            const double k = .22 * std::pow(1 - u, 2) +
                             .3 * std::pow(std::max(0., 1 - std::abs(y - band) / 10), 2) * (1 - u) +
                             (y & 1 ? .05 * (1 - u) : 0);
            if (k > .01)
                for (int x = 0; x < screen::W; ++x) p0.light(x, y, C[5], k);
        }
        return;
    }
    if (h >= screen::H) return;
    const int top = int(jsRound(YC - h / 2)), bot = int(jsRound(YC + h / 2));
    if (top > 0) screen::rect(p0, 0, 0, screen::W, top, ink);
    if (bot < screen::H) screen::rect(p0, 0, bot, screen::W, screen::H - bot, ink);
    if (h > 2) {
        const double sq = 1 - h / screen::H;
        for (int y = top; y < bot; ++y) {
            const int    e = std::min(y - top, bot - 1 - y);
            const double k =
                .55 * sq * sq + (y & 1 ? .08 * sq : 0) + .35 * std::pow(std::max(0., 1 - double(e) / 6), 2);
            for (int x = 0; x < screen::W; ++x) p0.light(x, y, C[5], k * fl);
        }
        for (int y : {top - 1, bot})
            if (y >= 0 && y < screen::H) screen::rect(p0, 0, y, screen::W, 1, sq > .6 ? C[6] : C[5]);
        return;
    }
    const bool dotPhase = t >= LN1 && t < ON0, fresh = t < SQ1 + .08 || (t > ON0 && t < ON1 + .04);
    const int  x0 = int(std::floor(XC - w)), x1 = int(std::ceil(XC + w));
    if (h > 0 && w >= 1) {
        const auto frame = jsUint(std::floor(t * 30));
        for (int x = std::max(0, x0); x < std::min(screen::W, x1); ++x) {
            const double u = std::abs(x + .5 - XC) / w,
                         y = YC + jsRound(noise(x / 23. + t * 9, o.seed) * (.7 + .5 * u));
            p0.px(x, y, u < .85 ? WHITE : C[6]);
            if (u < .92) {
                p0.px(x, y - 1, C[6]);
                p0.px(x, y + 1, u < .6 ? C[6] : C[5]);
            }
            if (fresh && u < .8) {
                p0.dot(x, y - 2, C[4], .5);
                p0.dot(x, y + 2, C[4], .5);
            }
            if (rand({uint32_t(x), frame, o.seed + 3}) < .06)
                p.px(x,
                     y + (rand({uint32_t(x), frame, o.seed + 4}) < .5 ? -1 : 1) *
                             (2 + std::floor(rand({uint32_t(x), frame, o.seed + 5}) * 4)),
                     rand({uint32_t(x), frame, 6}) < .5 ? WHITE : C[5]);
            p0.light(x, y, C[6], .5 * fl * (1 - u * .6));
        }
        for (int x = x0; x <= x1; x += 9) p.glow(x, YC, 10, C[5], .35 * fl);
        p.glow(XC, YC, std::min(70., 16 + w * .4), C[5], .4 * fl);
    }
    if (dotPhase) {
        const double u = (t - LN1) / (DOT1 - LN1);
        if (u < .35) screen::glint(p, XC, YC, 10 * (1 - u / .35) + 3, .2 + u * 2, C[6], C[5]);
        if (u < 1) {
            p.px(XC, YC, u < .5 ? WHITE : u < .75 ? C[6] : C[4]);
            p.px(XC - 1, YC, u < .4 ? C[6] : C[4]);
            const double sm = jsRound(9 * (1 - u));
            for (int k = 2; k <= sm; ++k) {
                p.dot(XC - k, YC, C[4], 1 - k / (sm + 1));
                p.dot(XC + k - 1, YC, C[4], 1 - k / (sm + 1));
            }
            p.glow(XC, YC, 22 * (1 - u * .5), C[5], .55 * std::pow(1 - u, 1.5));
        }
    }
    const double s = t - ON0;
    if (s >= 0 && s < .14) {
        screen::glint(p, XC, YC, 12 * (1 - s / .14) + 2, .4 + s * 6, C[6], C[5]);
        p.glow(XC, YC, 40, WHITE, .5 * (1 - s / .14));
    }
}
}  // namespace
const Descriptor& crtOffDescriptor() {
    static const std::array<Option, 1> options{
        {{"color", 0, 4294967295., false, 0, &PixelFxTransitionSettings::color, true}}};
    static const Descriptor d{{PixelFxKind::CrtOff, "transitions/crt-off", 320, 180, 0, 0, 2, false},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed             = 1;
                                  s.transition.color = hex(0x0b0a10);
                                  s.palette          = {hex(0x060a14), hex(0x0e1a33), hex(0x1a3360), hex(0x2a5a9a),
                                                        hex(0x4a8fd0), hex(0x8cc4f0), hex(0xd4ecff), WHITE};
                              },
                              &crtOff,
                              options};
    return d;
}
}  // namespace eve::stylize::pixelfx
