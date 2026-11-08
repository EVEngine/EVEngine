// eve-hot-path
// Native adaptation of pixelfx weather/aurora.js (MIT), including Sprite Fusion holy light.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Weather.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double      D = 12;
constexpr int         W = 320, H = 180;
constexpr PixelFxInfo SOURCE{PixelFxKind::Aurora, "weather/aurora", W, H, 160, 180, D, true};
double                cyc(double t, double c, double ph = 0) { return (TAU * c * t) / D + ph; }
struct Curtain {
    double             y, slope, amp, lam, fold, flam, height, k, s0, s1, rip;
    std::array<int, 3> c;
    double             hue;
};
constexpr std::array<Curtain, 3> CURTAINS{{{-132, -.08, 8, 210, 14, 150, 40, .6, -210, 210, 1.6, {1, 1, 2}, .35},
                                           {-88, .22, 13, 250, 19, 165, 82, 1.5, -195, 180, 2.4, {1, 2, 3}, 0},
                                           {-38, -.05, 5, 150, 10, 110, 20, .45, -210, 210, 1.4, {2, 1, 3}, 0}}};
struct Ray {
    double x, hem, h, k;
};
/** @brief Frame-local floating rasters and strand records; no scratch or target survives this draw. */
struct Frame {
    std::vector<float> I, HV, BL, TMP;
    std::vector<Ray>   rays;
    Frame() : I(W * H, 0), HV(W * H, 0), BL(W * H, 0), TMP(W * H, 0) { rays.reserve(256); }
};
void blur(Frame& f, int r) {
    const double n = 2 * r + 1;
    for (int y = 0; y < H; ++y) {
        double    acc = 0;
        const int o   = y * W;
        for (int x = -r; x < W + r; ++x) {
            if (x + r < W) acc += f.I[size_t(o + std::min(W - 1, x + r))];
            if (x - r - 1 >= 0) acc -= f.I[size_t(o + x - r - 1)];
            if (x >= 0 && x < W) f.TMP[size_t(o + x)] = float(acc / n);
        }
    }
    for (int x = 0; x < W; ++x) {
        double acc = 0;
        for (int y = -r; y < H + r; ++y) {
            if (y + r < H) acc += f.TMP[size_t(std::min(H - 1, y + r) * W + x)];
            if (y - r - 1 >= 0) acc -= f.TMP[size_t((y - r - 1) * W + x)];
            if (y >= 0 && y < H) f.BL[size_t(y * W + x)] = float(acc / n);
        }
    }
}
struct Aurora final : AnalyticKernel {
    weather::Field         field;
    std::array<float, 201> profile{};
    explicit Aurora(uint32_t S) : field(S * 5 + 1, std::array<int, 3>{{8, 16, 32}}) {
        for (size_t i = 0; i < 201; ++i) {
            const double v = double(i) / 200;
            profile[i]     = float(v < .035 ? 2.6 : v < .22 ? 1.15 - v : std::pow(1 - v, 1.5) * .9);
        }
    }
    void curtain(const Curtain& c, int n, double t, double breath, Frame& f) const {
        const double fast = (256 * 2 * t) / D, slow = (256 * t) / D;
        double       p1 = 0, p2 = 0, lastX = -999;
        for (double s = c.s0; s < c.s1; s += .5) {
            const double x = s + c.fold * std::sin((TAU * s) / c.flam + cyc(t, c.c[0], n * 2)) +
                             5 * std::sin((TAU * s) / 64 + cyc(t, c.c[2], n));
            if (x < -162 || x >= 161) continue;
            const double hem   = c.y + c.slope * s + c.amp * std::sin((TAU * s) / c.lam + cyc(t, c.c[1], n * 1.7)) +
                                 c.rip * std::sin((TAU * s) / 23 - cyc(t, 7, n)),
                         env   = smooth(c.s0, c.s0 + 60, s) * (1 - smooth(c.s1 - 70, c.s1, s));
            const double r1    = clamp((field.sample(s * 5 - fast + n * 40, 10 + n * 30) - .38) * 3.2),
                         r2    = clamp((field.sample(s * 4.3 + fast * .7 + n * 90, 50 + n * 13) - .35) * 3),
                         patch = field.sample(s * .7 + slow + n * 17, 80 + n * 9),
                         k     = c.k * env * breath * (.2 + .8 * r1) * (.3 + 1.4 * std::max(0., patch - .3));
            if (k < .01) continue;
            const double h  = c.height * (.45 + .75 * r2) * (.75 + .5 * patch);
            const int    xi = int(std::floor(x + 160));
            const double fr = x + 160 - xi;
            if (p1 > .75 && p1 >= p2 && p1 >= r1 && std::abs(x - lastX) > 2 && k > .25) {
                f.rays.push_back({x, hem - 1, h * .9, std::min(.85, k * .9)});
                lastX = x;
            }
            p2 = p1;
            p1 = r1;
            if (xi < -1 || xi >= W) continue;
            const int    top = std::max(0, int(std::floor(hem - h + 180))),
                         bot = std::min(H - 1, int(std::floor(hem + 180)) + 3);
            const double inv = 1 / h, k0 = k * .5 * (1 - fr), k1 = k * .5 * fr;
            double       v = (hem + 180 - .5 - top) * inv;
            for (int yi = top; yi <= bot; ++yi, v -= inv) {
                const double pr = v >= 0 ? profile[size_t(std::min(200, int(v * 200)))]
                                         : std::max(0., .45 + v * h * .16),
                             hv = clamp(v + c.hue);
                if (pr <= 0) continue;
                const int o = yi * W + xi;
                if (xi >= 0) {
                    f.I[size_t(o)]  = float(double(f.I[size_t(o)]) + k0 * pr);
                    f.HV[size_t(o)] = float(double(f.HV[size_t(o)]) + k0 * pr * hv);
                }
                if (xi + 1 < W) {
                    f.I[size_t(o + 1)]  = float(double(f.I[size_t(o + 1)]) + k1 * pr);
                    f.HV[size_t(o + 1)] = float(double(f.HV[size_t(o + 1)]) + k1 * pr * hv);
                }
            }
        }
    }
    void drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        auto        q = weather::clip(p, SOURCE);
        const auto& G = o.palette;
        const auto& U = o.weather.upper;
        for (uint32_t i = 0; i < 150; ++i) {
            const double   x = -160 + rand({o.seed, i, 1}) * 320, y = -180 + rand({o.seed, i, 2}) * 175,
                           r = rand({o.seed, i, 3}), c = 1 + std::floor(rand({o.seed, i, 4}) * 4),
                           tw  = .5 + .5 * std::sin(cyc(t, c, rand({o.seed, i, 5}) * TAU));
            const uint32_t col = r < .6 ? mix(U[3], WHITE, .2) : r < .9 ? mix(U[5], WHITE, .5) : WHITE;
            if (r < .6)
                q.dot(x, y, col, .35 + .5 * tw);
            else if (r < .95) {
                q.px(x, y, col);
                q.glow(x + .5, y + .5, 3, U[5], .25 * tw);
            } else
                q.sparkle(x, y, 1 + jsRound(tw * 2), mix(U[5], WHITE, .6), U[4], .35 * tw);
        }
        Frame        f;
        const double breath = .75 + .25 * std::sin(cyc(t, 1, -1.2));
        for (int n = 0; n < 3; ++n) curtain(CURTAINS[size_t(n)], n, t, breath, f);
        blur(f, 5);
        std::array<uint32_t, 7> GM{};
        for (size_t i = 0; i < 7; ++i) GM[i] = mix(G[i], U[i], .5);
        weather::rows(p, SOURCE, -180, -12, [&](double y, std::span<weather::RowPixel> row) {
            const size_t base = size_t(int(std::floor(y)) + 180) * W;
            for (size_t xi = 0; xi < W; ++xi) {
                const size_t i   = base + xi;
                const double raw = f.I[i], bl = f.BL[i];
                double       k    = bl > .015 ? std::min(.4, bl * .6) : 0;
                auto&        vout = row[xi];
                if (raw >= .03) {
                    const double e = (raw * 1.5) / (1 + raw * 1.5), v = f.HV[i] / raw,
                                 th = e > .45 ? weather::bayer(double(xi), y) * .06 : 0;
                    int lv = e > .86 + th ? 6 : e > .72 + th ? 5 : e > .56 + th ? 4 : e > .4 + th ? 3 : e > .24 ? 2 : 1;
                    if (v > .35) lv = std::min(lv, 5);
                    const size_t level = size_t(lv);
                    const auto   c     = v < .3 ? G[level] : v < .5 ? GM[level] : U[level];
                    if (lv >= 3) {
                        vout.c     = c;
                        vout.a     = float(clamp((e - .3) * 2.2));
                        vout.blend = lv < 5;
                    }
                    vout.l = lv >= 3 ? (v < .4 ? G[4] : U[4]) : c == G[level] ? G[4] : c == U[level] ? U[4] : GM[4];
                    k += e * (lv >= 3 ? .28 : .72);
                } else
                    vout.l = G[4];
                vout.k = float(k);
            }
        });
        for (const auto r : f.rays) {
            const int n = int(jsRound(r.h));
            for (int j = 1; j < n; ++j) {
                const double u = double(j) / n;
                q.blend(r.x, r.hem - j,
                        u < .35  ? G[5]
                        : u < .6 ? mix(G[5], U[5], .5)
                                 : U[5],
                        r.k * std::pow(1 - u, 1.3));
            }
            q.px(r.x, r.hem, G[6]);
        }
    }
};
}  // namespace
const Descriptor& auroraDescriptor() {
    static const std::array<Option, 1> options{{{"upper", 1, 64, false, 7, &PixelFxWeatherSettings::upper}}};
    static const Descriptor            d{SOURCE,
                                         7,
                                         [](PixelFxSettings& s) {
                                  s.seed          = 3;
                                  s.palette       = {hex(0x081a1e), hex(0x0d3a36), hex(0x13664e), hex(0x1fa06a),
                                                     hex(0x3fd88e), hex(0x9af7bf), hex(0xe6fff0)};
                                  s.weather.upper = {hex(0x120b2a), hex(0x26164f), hex(0x41208a), hex(0x6a34c0),
                                                     hex(0x9c62e8), hex(0xd2b4ff), hex(0xf6eeff)};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<Aurora>(s.seed);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
