// eve-hot-path
// Native adaptation of pixelfx weather/fog.js (MIT), including Sprite Fusion fog.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Weather.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double      D = 12;
constexpr PixelFxInfo SOURCE{PixelFxKind::Fog, "weather/fog", 320, 180, 160, 170, D, true};
struct Bank {
    double             top, base, bot, sx, sy, alpha;
    std::array<int, 3> tone;
    int                crest;
    double             warp;
    int                crestFrom;
};
constexpr std::array<Bank, 3> BANKS{{{-84, -40, -20, 1.6, 2.2, .24, {2, 3, 4}, 5, 16, 3},
                                     {-62, -24, -2, 1.0667, 2.5, .3, {2, 3, 4}, 5, 22, 2},
                                     {-42, -8, 10, .8, 2.8, .36, {2, 3, 5}, 6, 28, 1}}};
struct Fog final : AnalyticKernel {
    std::array<weather::Field, 3> fields;
    weather::Field                warpField, streakField;
    explicit Fog(uint32_t seed)
        : fields{{weather::Field(seed * 7, weather::OCTAVES), weather::Field(seed * 7 + 1, weather::OCTAVES),
                  weather::Field(seed * 7 + 2, weather::OCTAVES)}},
          warpField(seed * 7 + 11, weather::LOW_OCTAVES),
          streakField(seed * 7 + 21, weather::OCTAVES) {}
    void bank(Paint& p, double t, std::span<const uint32_t> C, size_t n) const {
        const auto             B     = BANKS[n];
        const auto&            F     = fields[n];
        const double           drift = (256 * t) / D, boil = (128 * t) / D;
        const uint32_t         crest = mix(C[size_t(B.crest)], WHITE, .12 + .08 * double(n));
        std::array<float, 320> prev1{}, prev2{}, br{}, warp{};
        for (size_t i = 0; i < 320; ++i)
            br[i] = float(2 * std::sin((TAU * t) / D + (double(i) - 160) * .006 + double(n) * 1.7));
        weather::rows(p, SOURCE, B.top - 2, B.bot, [&](double y, std::span<weather::RowPixel> row) {
            const double prof = smooth(B.top, B.base, y) * (1 - smooth(B.bot - (B.bot < 10 ? 14 : 4), B.bot + 1, y));
            for (size_t i = 0; i < 320; i += 2)
                warp[i] = warp[i + 1] = float(warpField.sample(double(i) * .35 + double(n) * 70, y * .5 + boil) - .5);
            for (size_t i = 0; i < 320; ++i) {
                const double x = double(i) - 160 + .5, w = warp[i],
                             nn = clamp((F.sample(x * B.sx + drift + w * B.warp, (y + br[i]) * B.sy + w * 8) - .5) *
                                            2.6 +
                                        .5) -
                                  .5,
                             f = prof <= .01 ? 0 : (prof * .72 + nn * .95) * (.6 + .5 * prof), above = prev2[i];
                prev2[i] = prev1[i];
                prev1[i] = float(f);
                if (f <= .3) continue;
                const double th   = weather::bayer(double(i), y) * .06;
                const int    band = f > .7 + th ? 2 : f > .5 + th ? 1 : 0, solid = f > .7 ? 2 : f > .5 ? 1 : 0;
                constexpr std::array<double, 3> T{{.3, .5, .7}};
                auto&                           r = row[i];
                if (solid >= B.crestFrom && above < T[size_t(solid)]) {
                    r.c = crest;
                    r.a = float(B.alpha * .95);
                    r.l = C[6];
                    r.k = float(.07);
                } else {
                    r.c = C[size_t(B.tone[size_t(band)])];
                    r.a = float(B.alpha * (.55 + .25 * band));
                    if (band == 2) {
                        r.l = C[5];
                        r.k = float(.03 + .02 * double(n));
                    }
                }
                r.blend = true;
            }
        });
    }
    void streaks(Paint& p, double t, std::span<const uint32_t> C) const {
        const double drift = (256 * t) / D;
        weather::rows(p, SOURCE, -40, 6, [&](double y, std::span<weather::RowPixel> row) {
            const double env = smooth(-40, -30, y) * (1 - smooth(-4, 6, y));
            for (size_t i = 0; i < 320; ++i) {
                const double x = double(i) - 160 + .5,
                             s = (streakField.sample(x * .4 + drift, y * 6.5 + 30) - .64) * 3 * env;
                if (s <= .05) continue;
                auto& r = row[i];
                r.c     = s > .35 ? C[5] : C[4];
                r.a     = float(std::min(.4, s));
                r.blend = true;
                r.l     = C[5];
                r.k     = float(s * .05);
            }
        });
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        bank(p, t, o.palette, 0);
        bank(p, t, o.palette, 1);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        bank(p, t, o.palette, 2);
        streaks(p, t, o.palette);
    }
};
}  // namespace
const Descriptor& fogDescriptor() {
    static const Descriptor d{SOURCE,
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed    = 5;
                                  s.palette = {hex(0x14121e), hex(0x24213a), hex(0x363250), hex(0x4f4a6c),
                                               hex(0x6d688b), hex(0x918cab), hex(0xbdb8d4), hex(0xece8f6)};
                              },
                              nullptr,
                              {},
                              nullptr,
                              nullptr,
                              [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<Fog>(s.seed);
                              }};
    return d;
}
}  // namespace eve::stylize::pixelfx
