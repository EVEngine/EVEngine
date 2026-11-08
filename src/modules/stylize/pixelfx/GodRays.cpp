// eve-hot-path
// Native adaptation of pixelfx weather/god-rays.js (MIT), including Sprite Fusion holy light.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Weather.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double      D = 10, SX = -230, SY = -300;
constexpr PixelFxInfo SOURCE{PixelFxKind::GodRays, "weather/god-rays", 320, 180, 160, 170, D, true};
const double          AX = std::cos(.94), AY = std::sin(.94), NX = -AY, NY = AX;
struct Shaft {
    double c, h, k, b, sw;
};
constexpr std::array<Shaft, 6> SHAFTS{{{-.27, .03, .65, 1, .006},
                                       {-.17, .022, .55, 2, .005},
                                       {-.07, .05, 1, 1, .008},
                                       {.03, .018, .6, 3, .004},
                                       {.12, .042, .85, 2, .007},
                                       {.23, .026, .6, 1, .005}}};
struct Frame {
    std::array<float, 512> strands{};
    std::array<Shaft, 6>   shafts{};
    Frame(double t, uint32_t S) {
        for (size_t i = 0; i < 512; ++i) {
            const double a = double(i) / 512;
            strands[i]     = float(.5 + .5 * std::sin(a * 190 + std::sin((TAU * t) / D) * 2 + S) *
                                            std::sin(a * 77 - (TAU * t) / D + double(S) * 2));
        }
        for (size_t i = 0; i < 6; ++i) {
            const auto s = SHAFTS[i];
            shafts[i]    = {s.c + s.sw * std::sin((TAU * t) / D + double(i) * 1.3),
                            s.h * (1 + .15 * std::sin((TAU * s.b * t) / D + double(i))),
                            s.k * (.72 + .28 * std::sin((TAU * s.b * t) / D + double(i) * 2.1)), s.b, s.sw};
        }
    }
    double lightAt(double x, double y) const {
        const double dx = x - SX, dy = y - SY, r = dx * AX + dy * AY, ang = (dx * NX + dy * NY) / r;
        double       e = 0;
        for (const auto s : shafts) {
            const double q = std::abs(ang - s.c) / s.h;
            if (q < 1) e += s.k * std::pow(1 - q * q, 1.5);
        }
        if (e <= 0) return 0;
        const double strand = strands[size_t(std::clamp(int(std::floor((ang + .36) * 700)), 0, 511))],
                     fall   = clamp(1.25 - (r - 230) / 420);
        return e * fall * (.7 + .45 * strand);
    }
};
void motes(Paint& q, double t, uint32_t S, std::span<const uint32_t> C, const Frame& f, bool front) {
    for (uint32_t i = front ? 0 : 50; i < uint32_t(front ? 50 : 160); ++i) {
        const auto   R  = [&](uint32_t k) { return rand({S, i, k}); };
        const double n1 = 1 + std::floor(R(1) * 2), n2 = 1 + std::floor(R(2) * 2),
                     x = -170 + R(3) * 340 + (8 + R(4) * 14) * std::sin((TAU * n1 * t) / D + R(5) * TAU),
                     y = -170 + R(6) * 172 + (6 + R(7) * 10) * std::sin((TAU * n2 * t) / D + R(8) * TAU) +
                         4 * std::sin((TAU * 3 * t) / D + i),
                     e = f.lightAt(x, y);
        if (e < .12) continue;
        const bool   big = front && R(9) < .5;
        const double k   = std::min(1., e);
        q.px(x, y, k > .6 ? WHITE : k > .3 ? C[5] : C[4]);
        if (big && k > .5) {
            q.px(x + 1, y, C[4]);
            q.px(x, y + 1, C[4]);
        }
        q.glow(x, y, big ? 6 : 4, C[4], .4 * k);
    }
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    auto                    q = weather::clip(p, SOURCE);
    const auto&             C = o.palette;
    const Frame             f(t, o.seed);
    std::array<uint32_t, 7> L{};
    for (size_t i = 0; i < 7; ++i) L[i] = mix(C[0], C[i], .75);
    std::array<float, 6> SC{}, SI{}, SK{};
    for (size_t j = 0; j < 6; ++j) {
        SC[j] = float(f.shafts[j].c);
        SI[j] = float(1 / f.shafts[j].h);
        SK[j] = float(f.shafts[j].k);
    }
    weather::rows(p, SOURCE, -170, 9, [&](double y, std::span<weather::RowPixel> row) {
        const bool   floor = y > 0;
        const double dy = y - SY, air = floor ? .8 : .66, boost = floor ? 1.6 : 1;
        double       r = (-160 + .5 - SX) * AX + dy * AY, nn = (-160 + .5 - SX) * NX + dy * NY;
        for (size_t i = 0; i < 320; ++i, r += AX, nn += NX) {
            auto& v          = row[i];
            v.a              = float(air);
            v.blend          = true;
            v.c              = C[0];
            const double ang = nn / r;
            double       e   = 0;
            for (size_t j = 0; j < 6; ++j) {
                const double q2 = (ang - SC[j]) * SI[j];
                if (q2 > -1 && q2 < 1) {
                    const double w = 1 - q2 * q2;
                    e += SK[j] * w * std::sqrt(w);
                }
            }
            if (e <= 0) continue;
            const double fall = 1.35 - (r - 200) / 330;
            const int    si   = int((ang + .36) * 700);
            e *= (fall > 1 ? 1 : fall < 0 ? 0 : fall) * (.7 + .45 * f.strands[size_t(std::clamp(si, 0, 511))]) * boost;
            if (e < .06) continue;
            const size_t lv = e > 1.05 ? 6 : e > .78 ? 5 : e > .52 ? 4 : e > .3 ? 3 : 2;
            v.c             = floor ? C[lv] : L[lv];
            v.a             = float(floor ? .85 : std::min(.78, .12 + e * .5));
            v.l             = C[4];
            v.k             = float(e * (floor ? .32 : .26));
        }
    });
    q.glow(-168, -178, 130, C[3], .3);
    q.glow(-168, -178, 50, C[5], .35);
    motes(q, t, o.seed, C, f, false);
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    auto        q = weather::clip(p, SOURCE);
    const Frame f(t, o.seed);
    motes(q, t, o.seed, o.palette, f, true);
}
}  // namespace
const Descriptor& godRaysDescriptor() {
    static const Descriptor d{SOURCE,
                              7,
                              [](PixelFxSettings& s) {
                                  s.seed    = 3;
                                  s.palette = {hex(0x1a1208), hex(0x3a2810), hex(0x7a5a24), hex(0xc89a44),
                                               hex(0xf2d27a), hex(0xfff2c4), WHITE};
                              },
                              front,
                              {},
                              nullptr,
                              back};
    return d;
}
}  // namespace eve::stylize::pixelfx
