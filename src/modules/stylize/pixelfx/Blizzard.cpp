// eve-hot-path
// Native adaptation of pixelfx weather/blizzard.js (MIT), including Sprite Fusion snow.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Weather.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double      D = 8;
constexpr PixelFxInfo SOURCE{PixelFxKind::Blizzard, "weather/blizzard", 320, 180, 160, 170, D, true};
double gustAt(double x, double t) { return .5 + .5 * std::sin(TAU * ((x + 160) / 400 - (2 * t) / D) + .8); }
double bank(double x) { return -2 + 1.2 * std::sin(x * .05 + 2) + .8 * std::sin(x * .15 + 1); }
struct Layer {
    int                n, m0, m1;
    double             len0, len1, drop0, drop1;
    std::array<int, 3> col;
    bool               near;
};
constexpr std::array<Layer, 3> LAYERS{{{240, 3, 4, 2, 4, 10, 40, {3, 4, 5}, false},
                                       {130, 4, 5, 5, 8, 20, 60, {4, 5, 6}, false},
                                       {34, 6, 7, 10, 16, 30, 70, {4, 6, 7}, true}}};
struct Blizzard final : AnalyticKernel {
    weather::Field farField, nearField, groundField;
    explicit Blizzard(uint32_t S)
        : farField(S * 3 + 7, weather::OCTAVES),
          nearField(S * 3 + 8, weather::OCTAVES),
          groundField(S * 5 + 1, weather::OCTAVES) {}
    void streaks(Paint& q, double t, const PixelFxSettings& o, size_t L) const {
        const auto     Ly = LAYERS[L];
        const uint32_t sd = o.seed * 17 + uint32_t(L);
        const auto&    C  = o.palette;
        const auto     c0 = C[size_t(Ly.col[0])], c1 = C[size_t(Ly.col[1])], c2 = C[size_t(Ly.col[2])];
        weather::trips(t, D, Ly.n, sd, Ly.m0, Ly.m1, [&](double u, double k, int m, uint32_t i) {
            const auto   j    = jsUint(wrap(k, m));
            const auto   R    = [&](uint32_t n) { return rand({sd, i, j, n}); };
            const double drop = Ly.drop0 + R(3) * (Ly.drop1 - Ly.drop0), x = -190 + 380 * u,
                         y = -178 + R(1) * 182 + drop * u + 3 * std::sin(u * TAU * (1 + R(5)) + R(6) * 6);
            if (y > bank(x) - 1) return;
            const double g = gustAt(x, t);
            if (!Ly.near && R(7) > .45 + .75 * g) return;
            const double len = (Ly.len0 + R(2) * (Ly.len1 - Ly.len0)) * (.7 + .6 * g), dx = 380 / (D / m),
                         dy = drop / (D / m), l = jsHypot2(dx, dy), ux = dx / l, uy = dy / l;
            q.lineFn(x - ux * len, y - uy * len, x - ux * len * .45, y - uy * len * .45, c0,
                     [](double v) { return .3 + .7 * v; });
            q.line(x - ux * len * .45, y - uy * len * .45, x - ux, y - uy, c1);
            q.px(x, y, g > .6 && Ly.near ? WHITE : c2);
            if (Ly.near) q.glow(x, y, 5, C[5], .12 + .12 * g);
        });
    }
    void sheets(Paint& p, double t, std::span<const uint32_t> C, bool near) const {
        const auto&            F     = near ? nearField : farField;
        const double           shift = (256 * (near ? 3 : 2) * t) / D, y0 = near ? -110 : -170, sy = near ? 4.5 : 3.2,
                               th = near ? .58 : .54;
        const auto             c = near ? C[6] : C[5], haze = mix(C[2], C[3], .5);
        std::array<float, 320> GX{}, WV{};
        for (size_t i = 0; i < 320; ++i) {
            GX[i] = float(gustAt(double(i) - 160 + .5, t));
            WV[i] = float(5 * std::sin((double(i) - 160) * .021 - (TAU * t) / D + (near ? 2 : 0)));
        }
        weather::rows(p, SOURCE, y0, 4, [&](double y, std::span<weather::RowPixel> row) {
            const double fade = near ? smooth(-110, -40, y) : 1;
            for (size_t i = 0; i < 320; ++i) {
                const double g = GX[i], n = F.sample((double(i) - 160) * .16 - shift, (y + WV[i]) * sy + 40),
                             d = (n - th) * 2.2 * (.35 + g) * fade;
                auto&        r = row[i];
                if (near) {
                    if (d <= .02) continue;
                    r.c = c;
                    r.a = float(std::min(.5, d));
                } else {
                    r.c = d > .08 ? c : haze;
                    r.a = float(.3 + .12 * g + std::min(.42, std::max(0., d)));
                }
                r.blend = true;
                if (d > .05) {
                    r.l = C[5];
                    r.k = float(d * .08);
                }
            }
        });
    }
    void ground(Paint& p, Paint& q, double t, const PixelFxSettings& o) const {
        const auto&            C     = o.palette;
        const double           shift = (256 * 4 * t) / D;
        std::array<float, 320> GX{}, WV{};
        for (size_t i = 0; i < 320; ++i) {
            GX[i] = float(gustAt(double(i) - 160 + .5, t));
            WV[i] = float(bank(double(i) - 160 + .5));
        }
        weather::rows(p, SOURCE, -16, 9, [&](double y, std::span<weather::RowPixel> row) {
            for (size_t i = 0; i < 320; ++i) {
                const double x = double(i) - 160 + .5, top = WV[i], d = y - top;
                auto&        r = row[i];
                if (d >= 0) {
                    r.c = d < 1 ? WHITE : d < 2 ? C[6] : d < 4 ? C[5] : C[4];
                    if (d < 1) {
                        r.l = C[6];
                        r.k = float(.1);
                    }
                    continue;
                }
                if (y < -14) continue;
                const double n = groundField.sample(x * .5 - shift, y * 3 + 9),
                             s = (n - .5) * 3 * smooth(-14, -1, y) * (.4 + GX[i]);
                if (s > .02) {
                    r.c     = C[6];
                    r.a     = float(std::min(.55, s));
                    r.blend = true;
                }
            }
        });
        weather::trips(t, D, 40, o.seed * 23, 8, 10, [&](double u, double k, int m, uint32_t i) {
            const auto   j = jsUint(wrap(k, m));
            const double x = -175 + 350 * u,
                         y = bank(x) - 1 - rand({o.seed, i, j, 1}) * 6 * std::abs(std::sin(u * 9 + i));
            q.px(x, y, rand({o.seed, i, j, 2}) < .5 ? WHITE : C[6]);
            q.px(x - 1, y, C[5]);
        });
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        auto q = weather::clip(p, SOURCE);
        sheets(p, t, o.palette, false);
        streaks(q, t, o, 0);
        streaks(q, t, o, 1);
        ground(p, q, t, o);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        auto q = weather::clip(p, SOURCE);
        sheets(p, t, o.palette, true);
        streaks(q, t, o, 2);
    }
};
}  // namespace
const Descriptor& blizzardDescriptor() {
    static const Descriptor d{SOURCE,
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed    = 9;
                                  s.palette = {hex(0x0d111f), hex(0x182035), hex(0x27324d), hex(0x3e4d6e),
                                               hex(0x64779c), hex(0x9cb0d0), hex(0xd8e4f6), WHITE};
                              },
                              nullptr,
                              {},
                              nullptr,
                              nullptr,
                              [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<Blizzard>(s.seed);
                              }};
    return d;
}
}  // namespace eve::stylize::pixelfx
