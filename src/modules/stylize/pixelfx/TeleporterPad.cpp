// eve-hot-path
// Native adaptation of pixelfx scifi/teleporter-pad.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScifiGhosts.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4, RX = 31, RY = 7, PY = -3, INNER = .78, HIGH = -84;
struct TeleporterPad final : AnalyticKernel {
    scifi::Ghosts ghosts{D, INFINITY, std::array<double, 4>{-46, -90, 46, 10}};
    explicit TeleporterPad(const PixelFxSettings& o) {
        Rng         rng(o.seed);
        const auto& C = o.palette;
        for (int i = 0; i < 40; ++i) {
            const double                  a = rng.range(0, TAU), r = std::sqrt(rng.next()) * RX * INNER * .9;
            const bool                    star = i % 3 == 0;
            const double                  born = rng.range(0, D), vx = rng.range(-4, 4), vy = -rng.range(30, 75),
                                          life = rng.range(.7, 1.3);
            const std::array<uint32_t, 3> colors{WHITE, C[6], C[5]};
            const auto                    color = rng.pick<uint32_t>(colors);
            const double                  size  = star ? rng.range(1.8, 3.4) : rng.range(.6, 1.2);
            ghosts.add(born, star ? ParticleKind::Star : ParticleKind::Mote, std::cos(a) * r,
                       PY + std::sin(a) * r * (RY / RX), vx, vy, life, color, size);
        }
    }
    void pad(Paint& p, double t, std::span<const uint32_t> C, std::span<const uint32_t> H) const {
        const double phase = t / D;
        p.shade(-RX - 1, PY - RY - 1, RX + 1, 2, [&](double x, double y, int sx, int sy) {
            const double u = x / RX, v = (y - PY) / RY, d = std::sqrt(u * u + v * v);
            if (d > 1) {
                if (std::abs(x) <= RX && y > PY && y <= 1 && std::sqrt(u * u + std::pow((y - 3 - PY) / RY, 2)) <= 1)
                    p.put(sx, sy,
                          y == 1 || std::abs(x) > RX - 1 ? H[0]
                          : x < -RX * .4                 ? H[3]
                          : x > RX * .4                  ? H[1]
                                                         : H[2]);
                return;
            }
            if (d > .97) {
                p.put(sx, sy, H[0]);
                return;
            }
            if (d > INNER) {
                p.put(sx, sy,
                      std::abs(d - (INNER + .1)) < .03 ? H[1]
                      : d < INNER + .03                ? C[3]
                      : v < -.3                        ? H[5]
                      : v < .2                         ? H[4]
                                                       : H[3]);
                return;
            }
            const double r = d / INNER, heat = std::pow(1 - r, 1.5);
            const bool   ring = wrap(r * 3 - phase * 3, 1) < .16;
            p.put(sx, sy,
                  r > .94      ? C[3]
                  : ring       ? (heat > .3 ? WHITE : C[6])
                  : heat > .55 ? C[6]
                  : heat > .25 ? C[5]
                  : r > .8     ? C[3]
                               : C[4]);
        });
        for (int i = 0; i < 10; ++i) {
            const double a = (double(i) / 10) * TAU + .15, x = std::cos(a) * RX * .885,
                         y = PY + std::sin(a) * RY * .885, k = .5 + .5 * std::cos(TAU * (2 * phase - double(i) / 10));
            p.px(x, y, k > .7 ? WHITE : k > .4 ? C[5] : C[2]);
            if (k > .5) p.glow(x, y, 4, C[4], .35 * k);
        }
    }
    void lift(Paint& p, double t, std::span<const uint32_t> C, bool near) const {
        for (int i = 0; i < 4; ++i) {
            const double k = scifi::since(t, (double(i) / 4) * D, D) / 1.25;
            if (k >= 1) continue;
            const double y = lerp(PY, HIGH + 12, std::pow(k, .85)), rx = RX * INNER * (1 + .08 * k),
                         ry = rx * (RY / RX), glint = std::fmod(i * .37 + k * 1.5, 1) * TAU;
            const auto   color = k < .12 ? WHITE : k < .4 ? C[6] : k < .65 ? C[5] : k < .85 ? C[4] : C[3];
            const double n     = std::ceil(rx * 7);
            for (int j = 0; j < n; ++j) {
                const double g = (j / n) * TAU, sg = std::sin(g);
                if ((sg >= 0) != near) continue;
                const double x = std::cos(g) * rx, yy = y + sg * ry;
                const bool   hot = std::abs(std::fmod(g - glint + PI * 3, TAU) - PI) < .25;
                if (k > .85 && (j & 1)) continue;
                p.px(x, yy, hot && k < .8 ? WHITE : near ? color : k < .4 ? C[5] : C[3]);
                if (k < .3) p.px(x, yy + 1, near ? C[5] : C[3]);
            }
            if (near) p.glow(0, y + ry * .5, rx + 8, C[4], .22 * (1 - k));
        }
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        pad(p, t, C, o.scifi.metal);
        const double W = RX * INNER;
        for (int y = -84; y < -3; ++y) {
            const double v = (y - PY) / (HIGH - PY), fade = std::pow(1 - v, 1.6);
            for (double x = std::floor(-W); x <= std::ceil(W); ++x) {
                const double q = std::abs(x + .5) / W;
                if (q > 1) continue;
                p.blend(x + .5, y + .5, q > .9 ? C[6] : C[5],
                        fade * (.07 + .26 * std::pow(q, 4) + .12 * std::pow(1 - v, 4)));
            }
        }
        cycle(t, D, 30, o.seed + 3, .6, 1.1, [&](double age, double life, Rng& r, int) {
            const double k = age / life, x = r.range(-.9, .9) * W, y = lerp(PY - 2, HIGH + 10, k), L = 3 + 7 * k;
            p.lineFn(x, y, x, y + L, [&](double u) { return u < .3 ? C[6] : C[4]; }, (1 - k) * 1.2);
        });
        lift(p, t, C, false);
        for (int y = -9; y > -64; y -= 12) p.glow(0, y, W + 8, C[4], .16 * (1 - (y - PY) / (HIGH - PY)));
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        lift(p, t, C, true);
        ghosts.evaluate(t).draw(p);
        const double k = .85 + .15 * std::sin(TAU * 2 * (t / D));
        for (double x : {-RX * .6, 0., RX * .6}) p.glow(x, PY, 20, C[4], .25 * k);
        p.glow(0, PY, 10, WHITE, .3 * k);
    }
};
}  // namespace
const Descriptor& teleporterPadDescriptor() {
    static const std::array<Option, 1> options{{{"metal", 1, 64, false, 6, &PixelFxScifiSettings::metal}}};
    static const Descriptor            d{{PixelFxKind::TeleporterPad, "scifi/teleporter-pad", 96, 104, 48, 92, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed        = 26;
                                  s.palette     = {hex(0x120a26), hex(0x26145a), hex(0x3c2296), hex(0x5a3ad2),
                                                   hex(0x7f66f0), hex(0xb4a2ff), hex(0xe6deff), WHITE};
                                  s.scifi.metal = {hex(0x0e0d14), hex(0x1e1c27), hex(0x312e3e),
                                                   hex(0x4b4759), hex(0x6f6a82), hex(0x9d98b0)};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<TeleporterPad>(s);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
