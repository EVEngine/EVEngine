// eve-hot-path
// Native adaptation of pixelfx water/waterfall.js (MIT), including Sprite Fusion bubbles caustics.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "common/Assert.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Water.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2, SY = .2, G = 760;
double           halfW(double W, double d, double H) { return W + 3.2 * std::pow(clamp(d / H), 1.6); }
struct RockMap {
    std::array<std::array<double, 4>, 6> lumps;
    double                               x0, y0, width, height;
    std::vector<int8_t>                  ids;
    std::vector<float>                   lit;
    // Exact integer-indexed typed-array semantics, including fractional sheet widths/heights.
    // nullopt is a source undefined cell, while an out-of-region ID is explicitly -1.
    std::optional<size_t> offset(double i, double j) const {
        const double index = j * width + i;
        if (index < 0 || std::floor(index) != index || index >= double(ids.size())) return std::nullopt;
        return size_t(index);
    }
    std::optional<int> at(double i, double j) const {
        if (i < 0 || j < 0 || i >= width || j >= height) return -1;
        const auto index = offset(i, j);
        return index ? std::optional<int>(ids[*index]) : std::nullopt;
    }
    std::optional<float> light(double i, double j) const {
        const auto index = offset(i, j);
        return index ? std::optional<float>(lit[*index]) : std::nullopt;
    }
    RockMap(double H, double W, uint32_t seed)
        : lumps{{{-W - 11, -H - 4, 13, 10},
                 {W + 8, -H - 2, 9, 7.5},
                 {0, -H + 3.5, W + 3, 4.2},
                 {-W - 21, -H + 4, 7, 5.5},
                 {W + 15, -H + 4.5, 5, 4},
                 {-2, -H - 5, W + 7, 6.5}}},
          x0(-W - 29),
          y0(-H - 15),
          width((W + 21) - x0 + 1),
          height((-H + 10) - y0 + 1),
          ids(size_t(std::floor(width * height)), int8_t(-1)),
          lit(ids.size(), 0.f) {
        for (int j = 0; j < height; ++j)
            for (int i = 0; i < width; ++i) {
                const double x = x0 + i + .5, y = y0 + j + .5;
                int          best = -1;
                double       z = -1e9, l = 0;
                for (size_t k = 0; k < lumps.size(); ++k) {
                    const auto&  q  = lumps[k];
                    const double nx = (x - q[0]) / q[2], ny = (y - q[1]) / q[3], r2 = nx * nx + ny * ny;
                    if (r2 >= 1) continue;
                    const double nz = std::sqrt(1 - r2), depth = nz * q[3] + (k == 5   ? -6
                                                                              : k == 2 ? -3
                                                                              : k > 2  ? -1
                                                                                       : 0);
                    if (depth > z) {
                        z    = depth;
                        best = int(k);
                        l    = nx * -.55 + ny * -.72 + nz * .42;
                    }
                }
                const auto index = offset(i, j);
                if (index) {
                    ids[*index] = int8_t(best);
                    lit[*index] = float(l + .2 * tileNoise(x * .32, y * .32, 0, 0, seed + 70));
                }
            }
    }
};
bool negative(std::optional<int> v) { return v && *v < 0; }
bool nonnegative(std::optional<int> v) { return v && *v >= 0; }
bool above(std::optional<int> v, int x) { return v && *v > x; }
void outcrop(Paint& p, std::span<const uint32_t> C, double ph, double H, double W, uint32_t S, const RockMap& map) {
    const auto& ST = water::rock();
    p.shade(-W - 26, -H, W + 18, -H + 20, [&](double x, double y, int sx, int sy) {
        const double k = 1 - jsHypot2((x + 4) / (W + 22), (y + H) / 20);
        if (k > 0) p.blendPx(sx, sy, ST[0], .4 * std::min(1., k * 3));
    });
    for (int j = 0; j < map.height; ++j)
        for (int i = 0; i < map.width; ++i) {
            const auto   k = map.at(i, j);
            const double x = map.x0 + i, y = map.y0 + j;
            if ((k == 2 || k == 5 || negative(k)) && y >= -H - 5 && y < -H + 1 && std::abs(x + .5) < W + 1.5) continue;
            if (negative(k)) {
                if (nonnegative(map.at(i - 1, j)) || nonnegative(map.at(i + 1, j)) || nonnegative(map.at(i, j - 1)) ||
                    nonnegative(map.at(i, j + 1)))
                    p.px(x, y, ST[0]);
                continue;
            }
            const auto r = map.at(i + 1, j), b = map.at(i, j + 1);
            if ((nonnegative(r) && r != k && k && *r < *k) || (nonnegative(b) && b != k)) {
                p.px(x, y, ST[1]);
                continue;
            }
            const auto l      = map.light(i, j);
            const auto bright = [&](double threshold) { return l && double(*l) > threshold; };
            const bool wet    = std::abs(x + .5) < W + 6 && y > -H - 9;
            auto       c = bright(.9) ? ST[5] : bright(.64) ? ST[4] : bright(.3) ? ST[3] : bright(-.05) ? ST[2] : ST[1];
            if (k == 5) c = bright(.7) ? ST[3] : bright(.2) ? ST[2] : ST[1];
            if (negative(map.at(i, j - 1)) && bright(.25)) c = ST[5];
            if (wet) {
                c = bright(.9) ? ST[4] : bright(.64) ? ST[3] : bright(.3) ? ST[2] : ST[1];
                if (bright(.3) && tileNoise(x * .9, y * .6 - ph * 8, 0, 8, S + 71) > .42) c = bright(.6) ? C[6] : C[4];
            }
            p.px(x, y, c);
        }
    for (uint32_t q = 0; q < 3; ++q) {
        const auto&  L  = map.lumps[q % 2];
        const double cx = std::floor(L[0] + (q == 2 ? -.5 : .15) * L[2]),
                     cy = std::floor(L[1] + L[3] * (q == 2 ? .1 : -.25)), len = 3 + std::floor(rand({q, S + 74}) * 4),
                     dx = rand({q, S + 75}) < .5 ? 1 : -1;
        for (int n = 0; n <= len; ++n) {
            const double x = cx + jsRound(n * .6) * dx, y = cy + n, i = x - map.x0, j = y - map.y0;
            if (negative(map.at(i, j)) || above(map.at(i, j), 1) || negative(map.at(i, j + 1))) break;
            p.px(x, y, n == len ? ST[1] : ST[0]);
            if (n == 0) p.px(x - dx, y - 1, ST[4]);
        }
    }
    for (const double sx : {-W - 3, W + 3})
        for (double y = -H - 2; y < -H + 7; ++y) {
            const double g = wrap(y * .35 - ph * 6, 1), i = std::floor(sx) - map.x0, j = std::floor(y) - map.y0;
            if (nonnegative(map.at(i, j))) p.px(sx, y, g < .3 ? C[6] : C[3]);
        }
}
void river(Paint& p, std::span<const uint32_t> C, double ph, double H, double W, uint32_t S) {
    const double top = -H - 6;
    p.shader(-W - 3, top, W + 3, -H, [&](double x, double y, ShadeOutput& out) {
        const double back = top + 1.5 + tileNoise(x * .3, 4, 0, 0, S + 62) * 1.5, v = (y - back) / (-H - back),
                     hw = W + 2 - (1 - v) * 3;
        if (y < back || std::abs(x) > hw) return;
        const double n = tileNoise(x * .32, (y - top) * .7 - ph * 8, 0, 8, S + 3);
        out.c          = n > .66 ? C[6] : n > .4 ? C[5] : v < .5 ? C[3] : C[4];
    });
}
void sheet(Paint& p, std::span<const uint32_t> C, double ph, double H, double W, uint32_t S) {
    for (double y = -H + 2; y <= -1; ++y) {
        const double d = y + .5 + H, v = d / H, s = std::sqrt(d + 3), hw0 = halfW(W, d, H), split = smooth(.56, .95, v),
                     glass = 1 - smooth(4, 24, d), wl = tileNoise(2, s * 1.4 - ph * 12, 0, 12, S + 2) * (.4 + 1.8 * v),
                     wr  = tileNoise(11, s * 1.4 - ph * 12, 0, 12, S + 2) * (.4 + 1.8 * v),
                     fan = split > 0 ? tileNoise(3, s * .8 - ph * 4, 0, 4, S + 61) * 1.6 : 0, sd = s * 1.5 - ph * 20,
                     sb = s * .45 - ph * 6;
        for (double X = -W - 8; X <= W + 8; ++X) {
            const double x = X + .5, hw = hw0 + (x < 0 ? wl : wr), ax = std::abs(x);
            if (ax > hw) continue;
            const double e    = ax / hw;
            bool         edge = false;
            if (split > 0) {
                const double f = wrap((x / (1 + .22 * split) + fan + 64) / 5.4, 1);
                if (f > 1 - .46 * split) continue;
                edge = f < .2;
            }
            const double dash = tileNoise(X * 1.618 + .31, sd, 0, 20, S);
            if (rand({jsUint(X), S + 60}) < .48 && e < .86 && !edge) {
                p.blend(x, y + .5, dash > .45 ? C[4] : glass > .5 ? C[3] : C[2], dash > .45 ? .7 : .42 + .15 * v);
                continue;
            }
            const double broad = tileNoise(std::floor((x + 40) / 3) * 2.7, sb, 0, 6, S + 1);
            double       b     = -.34 + .46 * v + .3 * broad - glass * .5 + .35 * smooth(.7, 1, e) * (x < 0 ? 1 : .3) -
                                 (x > 0 ? .12 * e : 0) + (edge ? .3 : 0);
            if (dash > .2 - .22 * v) b += .36 + .7 * (dash - .2);
            if (b > -.2)
                p.px(X, y, b > .62 ? WHITE : b > .34 ? C[6] : b > .08 ? C[5] : C[4]);
            else
                p.blend(x, y + .5, C[3], .8);
        }
    }
    cycle(ph * D, D, 16, S + 30, .4, .4, [&](double age, double life, Rng& rng, int) {
        const double side = rng.sign(), d0 = rng.range(14, H * .7), d = d0 + 50 * age + 260 * age * age, k = age / life;
        if (d > H - 6) return;
        const double x = side * (halfW(W, d, H) + 1.5 + age * rng.range(6, 16)), y = -H + d, len = 2 + 6 * k;
        p.lineFn(x, y - len, x, y, [&](double u) { return u > .7 ? C[6] : C[4]; }, 1 - k * .5);
    });
}
void curl(Paint& p, std::span<const uint32_t> C, double ph, double H, double W, uint32_t S) {
    for (double x = -W; x <= W; ++x) {
        const double g = tileNoise(x * .45, ph * 4, 0, 4, S + 4);
        p.px(x, -H - 1, C[4]);
        p.px(x, -H, WHITE);
        p.px(x, -H + 1, g > .2 ? C[6] : C[5]);
    }
    p.glowLine(-W, -H, W, -H, 6, C[6], .18);
    p.glow(0, -H, 34, C[5], .2);
    cycle(ph * D, D, 4, S + 5, .36, .36, [&](double age, double life, Rng& rng, int) {
        const double k = std::sin((PI * age) / life), x = rng.range(-W, W);
        if (k > .5)
            for (const auto off : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                p.px(x + off.x, -H + off.y, C[6]);
        p.glow(x, -H, 6, C[5], .4 * k);
    });
}
void pool(Paint& p, std::span<const uint32_t> C, double ph, double W, uint32_t S) {
    const double                           RX = 56, RY = 11, cy = 2, T = ph * TAU;
    std::array<std::array<double, 2>, 112> cols{};
    for (int X = -56; X < 56; ++X) {
        const double x       = X + .5;
        cols[size_t(X + 56)] = {std::sin(x * .11 - T * 2), std::sin(T + x * .05) * 1.4 + x * .13};
    }
    for (int Y = int(cy - RY); Y < cy + RY; ++Y) {
        const double y = Y + .5, dy = (y - cy) / RY, sy = std::sin(y * .5 + T);
        for (int X = -56; X < 56; ++X) {
            const double x = X + .5, dx = x / RX, d = dx * dx + dy * dy;
            if (d > 1) continue;
            p.blend(x, y, d < .12 ? C[2] : C[1], .62 * std::min(1., (1 - d) * 4));
            const auto [cx1, cx2] = cols[size_t(X + 56)];
            const double u = x * .2 + 1.8 * sy, w = y * .55 + 1.8 * cx1,
                         f = (std::max(0., 1 - std::abs(std::sin(u) + std::sin(w)) * 2.6) +
                              .6 * std::max(0., 1 - std::abs(std::sin(cx2 + y * .3)) * 3)) *
                             (1 - d);
            if (f > .5) p.light(x, y, C[5], .18 * f);
        }
    }
    for (int i = 0; i < 7; ++i) {
        const double a = PI * (1.12 + .76 * ((i + .5) / 7)), x = std::cos(a) * RX * .96,
                     y = cy + std::sin(a) * RY * .96, g = std::sin(ph * TAU * 2 + i * 2.4);
        if (g > .2) p.px(x, y, g > .8 ? C[6] : C[4]);
    }
    p.shader(-W - 3, 3, W + 3, 10, [&](double x, double y, ShadeOutput& out) {
        const double n = tileNoise(x * .9, (y - 3) * .35 + ph * 6, 0, 6, S + 6), k = 1 - (y - 3) / 7;
        if (std::abs(x) > W + 2 - (y - 3) * .4 || n < .25 + (1 - k) * .4) return;
        out.c = n > .6 && k > .6 ? C[6] : C[5];
    });
}
void foam(Paint& p, std::span<const uint32_t> C, double t, double ph, double W, uint32_t S) {
    const double R = W + 10;
    p.shader(-R, -13, R, 3, [&](double x, double y, ShadeOutput& out) {
        const double ax   = std::abs(x),
                     lump = tileNoise(x * .22, ph * 6, 0, 6, S + 7) * 3 +
                            tileNoise(x * .55 + 9, ph * 12, 0, 12, S + 8) * 1.5,
                     top  = -8 + lump + std::pow(ax / R, 2) * 7;
        if (ax > R - std::max(0., -y - 1) * .6 || y < top) return;
        const double n    = tileNoise(x * .5, (y - top) * .5 - ph * 10, 0, 10, S + 9),
                     boil = tileNoise(x * .42 + 3, y * .42 - ph * 8, 0, 8, S + 12), u = y - top;
        if (y > 1)
            out.c = n > 0 ? C[5] : C[4];
        else if (u < 1.2)
            out.c = WHITE;
        else if (boil < -.3)
            out.c = u > 4 ? C[4] : C[5];
        else if (boil < -.05)
            out.c = C[6];
        else
            out.c = n > -.2 ? WHITE : C[6];
    });
    cycle(t, D, 30, S + 10, .7, .7, [&](double age, double life, Rng& rng, int) {
        const double k = age / life, side = rng.sign(), x0 = rng.range(-W, W) * .9, sp = rng.range(2, 8),
                     up = rng.range(2, 8), x = x0 + side * ease::outCubic(k) * sp,
                     y   = -rng.range(2, 5) - ease::outCubic(k) * up,
                     rad = rng.range(1.4, 3) * std::pow(std::sin(PI * k), .7);
        if (rad > .6) water::billow(p, x, y, rad, C);
    });
    cycle(t, D, 18, S + 11, 1, 1, [&](double age, double, Rng& rng, int) {
        const double side = rng.sign(), x0 = side * rng.range(2, W + 4), vx = side * rng.range(12, 60),
                     vy = -rng.range(70, 150), z = rng.range(-3, 9), size = rng.next() < .3 ? 2 : 1, land = z * SY,
                     tl = (-vy + std::sqrt(vy * vy + 2 * G * (land + 5))) / G;
        if (age < tl)
            water::bead(p, x0 + vx * age, -5 + vy * age + .5 * G * age * age, vx, vy + G * age, size, C);
        else if (age < tl + .35)
            water::ring(p, x0 + vx * tl, land, 1 + (age - tl) * 12, SY, 1 - (age - tl) / .35, C, {1});
    });
}
void mist(Paint& p, std::span<const uint32_t> C, double t, uint32_t S, int n, uint32_t seed) {
    cycle(t, D, n, S + seed, 1.8, 2.8, [&](double age, double life, Rng& rng, int) {
        const double k = age / life, side = rng.sign(), drift = ease::outCubic(k), x0 = rng.range(3, 12),
                     x = side * (x0 + drift * rng.range(24, 48)), y0 = -rng.range(2, 7), y = y0 - k * rng.range(3, 12),
                     r0 = rng.range(4, 6), rad = r0 + k * rng.range(5, 9),
                     a = rng.range(.2, .3) * std::min(1., k / .12) * std::pow(1 - k, 1.7);
        p.disc(x, y, rad, C[6], [&](double d) { return a * (1 - d * d); });
        if (a > .05) p.glow(x, y, rad * 1.2, C[4], .35 * a);
    });
}
struct Waterfall final : AnalyticKernel {
    std::optional<RockMap> geometry;
    explicit Waterfall(const PixelFxSettings& o) {
        if (o.water.ledge) geometry.emplace(o.water.height, o.water.width, o.seed);
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C  = o.palette;
        const double ph = t / D, W = o.water.width;
        pool(p, C, ph, W, o.seed);
        for (uint32_t i = 0; i < 3; ++i) {
            const double f = std::fmod(ph + double(i) / 3, 1);
            water::ring(p, 0, 1, W + 6 + f * 38, SY, .95 * std::pow(1 - f, 1.5), C, {1, 3, -1, false, 0, i + 1});
        }
        mist(p, C, t, o.seed, 7, 20);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C  = o.palette;
        const double ph = t / D, H = o.water.height, W = o.water.width, T = ph * TAU;
        const auto   S = o.seed;
        for (uint32_t i = 0; i < 3; ++i) {
            const double f = std::fmod(ph + double(i) / 3, 1);
            water::ring(p, 0, 1, W + 6 + f * 38, SY, .95 * std::pow(1 - f, 1.5), C,
                        {1, 3, 1, false, f < .4 ? 1 - f * 2.5 : 0, i + 1});
        }
        river(p, C, ph, H, W, S);
        if (o.water.ledge) {
            EV_ASSERT(geometry.has_value(), "Prepared waterfall owns its outcrop");
            outcrop(p, C, ph, H, W, S, *geometry);
        }
        sheet(p, C, ph, H, W, S);
        curl(p, C, ph, H, W, S);
        foam(p, C, t, ph, W, S);
        mist(p, C, t, S, 5, 21);
        cycle(t, D, 12, S + 50, .2, .4, [&](double age, double life, Rng& rng, int) {
            const double k   = std::sin((PI * age) / life);
            const bool   low = rng.next() < .45;
            const double d   = low ? H - rng.range(4, 16) : rng.range(12, H - 16),
                         x   = low ? rng.range(-W - 10, W + 10) : rng.range(-.8, .8) * halfW(W, d, H),
                         y   = -H + d - (age / life) * (low ? 6 : -10);
            p.px(x, y, WHITE);
            if (k > .55)
                for (const auto off : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                    p.px(x + off.x, y + off.y, C[6]);
            if (k > .85 && rng.next() < .5)
                for (const auto off : std::array<Point, 4>{{{2, 0}, {-2, 0}, {0, 2}, {0, -2}}})
                    p.px(x + off.x, y + off.y, C[5]);
            p.glow(x, y, 5, C[5], .35 * k);
        });
        cycle(t, D, 34, S + 51, .9, 1.6, [&](double age, double life, Rng& rng, int) {
            const double k = age / life, side = rng.sign(), x0 = rng.range(0, W + 6), speed = rng.range(6, 22),
                         angle = rng.range(0, TAU), x = side * (x0 + k * speed) + std::sin(age * 5 + angle) * 1.2,
                         y0 = -rng.range(3, 10), y = y0 - k * rng.range(10, 34);
            if (k > .7 && rng.next() < (k - .7) * 3.3) return;
            p.px(x, y, k < .4 && rng.next() < .5 ? WHITE : C[6]);
        });
        p.glowLine(0, -H + 6, 0, -8, 26, C[4], .12);
        p.glow(0, -5, 40, C[5], .32 + .05 * std::sin(T * 2));
        p.glow(0, 4, 50, C[3], .14);
    }
};
}  // namespace
const Descriptor& waterfallDescriptor() {
    static const std::array<Option, 3> options{{{"height", 32, 256, false, 0, &PixelFxWaterSettings::height},
                                                {"width", 1, 40, false, 0, &PixelFxWaterSettings::width},
                                                {"ledge", 0, 1, false, 0, &PixelFxWaterSettings::ledge}}};
    static const Descriptor            d{{PixelFxKind::Waterfall, "water/waterfall", 144, 136, 72, 118, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  water::defaults(s, 5);
                                  s.water.height = 92;
                                  s.water.width  = 13;
                                  s.water.ledge  = true;
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<Waterfall>(s);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
