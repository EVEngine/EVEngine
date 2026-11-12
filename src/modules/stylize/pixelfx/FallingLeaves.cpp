// eve-hot-path
// Native adaptation of pixelfx weather/falling-leaves.js (MIT), including Sprite Fusion falling leaves.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Weather.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double      D = 10, FLOOR = 1;
constexpr PixelFxInfo SOURCE{PixelFxKind::FallingLeaves, "weather/falling-leaves", 320, 180, 160, 170, D, true};
using Ramps = std::array<std::span<const uint32_t>, 3>;
Ramps  ramps(const PixelFxSettings& o) { return {o.palette, o.weather.reds, o.weather.golds}; }
double gust(double t) { return 16 * std::sin((TAU * t) / D - 1.2) + 5 * std::sin((TAU * 3 * t) / D); }
double Hc(double th) {
    const double k = std::floor((th + PI / 2) / PI);
    return 2 * k + 1 + std::sin(th - k * PI);
}
double shapeAt(int shape, double u, double v) {
    if (shape == 0) return std::abs(u) >= 1 ? 9 : (v * v) / (.36 * (1 - u * u) * (1.15 - .3 * u));
    if (shape == 1) {
        const double r = jsHypot2(u, v * 1.1), a = std::atan2(v, u);
        return r / (.62 + .38 * std::pow(std::abs(std::cos(2.5 * a)), 1.5));
    }
    return std::abs(u) >= 1 ? 9 : (v * v) / (.16 * (1 - u * u));
}
template <class P>
void leaf(P& q, double x, double y, double size, double ang, double flip, int shape, std::span<const uint32_t> col,
          double alpha) {
    const bool   back = flip < -.25;
    const double fs = .35 + .65 * std::abs(flip), c = std::cos(ang), s = std::sin(ang), L = size, Wd = size * .9,
                 ext = L + 1.5;
    const std::array<uint32_t, 3> face =
        back ? std::array<uint32_t, 3>{{mix(col[1], hex(0x9c8a78), .2), mix(col[2], hex(0xc8b49c), .22),
                                        mix(col[3], hex(0xe0d0b8), .22)}}
             : std::array<uint32_t, 3>{{col[2], col[3], col[4]}};
    const auto rim = col[0], vein = back ? mix(col[2], hex(0xe8dcc8), .5) : col[1];
    q.shader(x - ext, y - ext, x + ext, y + ext, [&](double px, double py, ShadeOutput& out) {
        const double dx = px - x, dy = py - y, lu = (dx * c + dy * s) / L, lv = (-dx * s + dy * c) / (Wd * fs),
                     d = shapeAt(shape, lu, lv);
        if (lu < -1.25 || lu > 1.1) return;
        if (lu < -.92 && std::abs(lv * Wd * fs) < .5) {
            out.c = rim;
            out.a = alpha;
            return;
        }
        if (d > 1.32) return;
        out.a = alpha;
        if (d > 1)
            out.c = rim;
        else if (std::abs(lv * Wd * fs) < .45 && lu > -.9 && lu < .75)
            out.c = vein;
        else
            out.c = lv < -.15 ? face[2] : lv < .35 ? face[1] : face[0];
    });
}
struct FallingLeaves final : AnalyticKernel {
    weather::RecordedRaster litter;
    explicit FallingLeaves(const PixelFxSettings& o) {
        const auto R3 = ramps(o);
        for (uint32_t i = 0; i < 64; ++i) {
            const double            x   = -164 + rand({o.seed, i, 91}) * 328,
                                    y   = FLOOR - 2 + std::floor(rand({o.seed, i, 92}) * 4) * 2;
            const auto              col = R3[size_t(std::floor(rand({o.seed, i, 93}) * 3))];
            std::array<uint32_t, 6> tint{};
            for (size_t j = 0; j < 6; ++j) tint[j] = mix(col[j], col[0], .12);
            leaf(litter, x, y, 3.6 + rand({o.seed, i, 94}) * 1.8, (rand({o.seed, i, 95}) - .5) * .7,
                 .45 + rand({o.seed, i, 96}) * .25, int(std::floor(rand({o.seed, i, 97}) * 3)), tint, 1);
        }
    }
    void leaves(Paint& q, double t, const PixelFxSettings& o, bool front) const {
        const auto     R3 = ramps(o);
        const uint32_t sd = o.seed * 19;
        weather::trips(t, D, 62, sd, 2, 3, [&](double u, double k, int m, uint32_t i) {
            const auto j    = jsUint(wrap(k, m));
            const auto R    = [&](uint32_t n) { return rand({sd, i, j, n}); };
            const bool near = R(1) < .35;
            if (near != front) return;
            const double dur = D / m, tf = dur * .72, a = std::min(u * dur, tf), rest = u * dur - tf,
                         w = 1.7 + R(2) * 1.2, ph0 = R(3) * TAU, amp = (near ? 14 : 8) + R(4) * 10,
                         x0 = -180 + R(5) * 340, y0 = -186;
            const auto   path = [&](double aa) { return .45 * aa + (.9 * (Hc(w * aa + ph0) - Hc(ph0))) / w; };
            const double vy = (FLOOR - y0) / path(tf), born = t - u * dur;
            const auto   xAt = [&](double aa) {
                return x0 + 7 * aa + std::sin(w * aa + ph0) * amp + (gust(born + aa) - gust(born)) * (near ? 1 : .7);
            };
            const double size  = (near ? 5.8 : 4.2) + R(6) * 1.6;
            const int    shape = int(std::floor(R(7) * 3));
            const auto   col   = R3[size_t(std::floor(R(8) * 3))];
            if (rest > 0) {
                const double fade = 1 - rest / (dur - tf);
                leaf(q, xAt(tf), FLOOR - 1, size, (R(9) - .5) * .5, .42, shape, col, fade);
                return;
            }
            const double x = xAt(a), y = y0 + vy * path(a), tilt = std::cos(w * a + ph0) * .75 + (near ? 0 : .3),
                         flip = std::cos(R(10) * TAU + a * (R(11) < .3 ? 5 : 1.2));
            leaf(q, x, y, size, tilt, flip, shape, col, 1);
            if (flip > .86) {
                const double g = (flip - .86) / .14, hx = x - std::cos(tilt) * size * .3,
                             hy = y - std::sin(tilt) * size * .3 - 1;
                q.px(hx, hy, col[5]);
                q.glow(hx, hy, 5 + size * .5, col[4], .22 * g);
            }
        });
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        auto        q   = weather::clip(p, SOURCE);
        const auto& C   = o.palette;
        const auto  top = hex(0x0e0a12), low = mix(hex(0x140c14), C[0], .3);
        weather::rows(p, SOURCE, -170, 9, [&](double y, std::span<weather::RowPixel> row) {
            const double k = (y + 170) / 180;
            const auto   c = mix(top, low, k * k);
            for (auto& v : row) {
                v.c     = c;
                v.a     = float(.32 + .1 * k);
                v.blend = true;
            }
        });
        litter.draw(q);
        leaves(q, t, o, false);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        auto q = weather::clip(p, SOURCE);
        leaves(q, t, o, true);
    }
};
}  // namespace
const Descriptor& fallingLeavesDescriptor() {
    static const std::array<Option, 2> options{{{"reds", 1, 64, false, 6, &PixelFxWeatherSettings::reds},
                                                {"golds", 1, 64, false, 6, &PixelFxWeatherSettings::golds}}};
    static const Descriptor            d{
        SOURCE,
        6,
        [](PixelFxSettings& s) {
            s.seed         = 8;
            s.palette      = {hex(0x4a1d0c), hex(0x8a3a14), hex(0xc8621e), hex(0xf0982e), hex(0xffcf72), hex(0xfff0c4)};
            s.weather.reds = {hex(0x40101a), hex(0x7c1c22), hex(0xb8302a), hex(0xe85a3c), hex(0xff9a72), hex(0xffd8c4)};
            s.weather.golds = {hex(0x3e300c), hex(0x7a5c14), hex(0xb8901e),
                               hex(0xe8c23a), hex(0xfff09a), hex(0xfffbe0)};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
            return std::make_unique<FallingLeaves>(s);
        }};
    return d;
}
}  // namespace eve::stylize::pixelfx
