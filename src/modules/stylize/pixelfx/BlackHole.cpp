// eve-hot-path
// Native adaptation of pixelfx shadow/black-hole.js, MIT.
// Sprite Fusion black-hole, Copyright (c) 2026 Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Shadow.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double Y = -70, RS = 13.5, E = 17.5, SI = .25, SI_ARCH = .46, TILT = -.14, D = 4, ROUT = 60;
const double     CT = std::cos(TILT), ST = std::sin(TILT);
struct Band {
    double r0, r1, turns, hot, phase;
};
constexpr std::array<Band, 4> BANDS{
    {{18, 24, 5, 1.35, .3}, {26, 34, 3, .92, 1.9}, {35.5, 46, 2, .64, 3.7}, {47.5, 60, 1, .42, 5.2}}};
struct Disk {
    double intensity, doppler;
};
std::optional<Disk> disk(double x, double z, double t) {
    const double rho = std::sqrt(x * x + z * z);
    for (const auto& b : BANDS) {
        if (rho < b.r0 || rho > b.r1) continue;
        const double w = (rho - b.r0) / (b.r1 - b.r0), ph = std::atan2(z, x) - (TAU * b.turns * t) / D,
                     streak = .62 + .22 * std::cos(7 * ph + rho * .8 + b.phase) +
                              .16 * std::cos(13 * ph - rho * 1.6 + b.phase * 2),
                     lane   = .72 + .28 * std::cos(rho * 2.3 + b.phase),
                     edge   = smooth(0, .18, w) * (1 - smooth(.78, 1, w));
        return Disk{b.hot * streak * lane * (.35 + .65 * edge), -x / rho};
    }
    return std::nullopt;
}
void hole(Paint& p, const std::vector<uint32_t>& C, double t, double pulse) {
    const std::array<uint32_t, 8> ramp{C[1], C[2], C[3], C[4], C[5], C[6], C[7], WHITE};
    constexpr std::array<int, 16> bayer{0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    p.shader(-ROUT - 2, Y - 42, ROUT + 2, Y + 42, [&](double x, double y, ShadeOutput& out) {
        const double dx = x, dy = y - Y, u = dx * CT + dy * ST, v = -dx * ST + dy * CT, r = std::sqrt(u * u + v * v);
        std::optional<Disk> sample;
        if (v > 0) sample = disk(u, -v / SI, t);
        if (!sample && r > RS) {
            const double k = 1 - (E * E) / (r * r), bu = u * k, bv = v * k, si = k > 0 ? SI_ARCH : SI;
            if (-bv / si > 0) sample = disk(bu, -bv / si, t);
        }
        if (!sample || sample->intensity < .06) {
            if (r < RS - .4) {
                out.c = C[0];
                return;
            }
            if (r < RS + .7) {
                const double a = std::atan2(v, u), f = .5 + .5 * std::cos(3 * a + (TAU * 2 * t) / D) + pulse * .4;
                out.k = .3;
                out.c = f > .55 ? WHITE : C[7];
            }
            return;
        }
        const double intensity = sample->intensity * 1.3 * (1 + .55 * sample->doppler) * (1 + .25 * pulse),
                     f         = clamp(intensity) * 7;
        size_t       i         = size_t(std::floor(f));
        const double threshold = (bayer[size_t(((int(std::floor(y)) & 3) << 2) | (int(std::floor(x)) & 3))] + .5) / 16;
        if (f - double(i) > threshold) i = std::min(size_t(7), i + 1);
        if (i == 0 && intensity < .12) return;
        uint32_t c = ramp[i];
        if (sample->doppler > .35 && i >= 5) c = mix(c, hex(0xe4f7ff), std::min(1., (sample->doppler - .35) * 2));
        if (sample->doppler < -.4 && i >= 2 && i < 6) c = mix(c, C[3], .35);
        if (i >= 4 && ((int(std::floor(x)) + int(std::floor(y))) & 1) == 0) out.k = .3 * std::min(1., intensity - .35);
        out.c = c;
    });
}
struct RockPosition {
    double x, y, z, rho;
};
struct BlackHole final : AnalyticKernel {
    void drawBack(Paint& p, double, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        p.glow(0, -1, 44, C[4], .12);
        p.glow(0, Y, 80, C[4], .32);
        p.glow(0, Y, 46, mix(C[6], C[5], .5), .45);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&    C     = o.palette;
        const auto&    K     = o.shadow.rock;
        const uint32_t seed  = o.seed;
        const double   pulse = std::pow(std::max(0., std::cos((TAU * 2 * t) / D)), 12);
        cycle(t, D, 10, seed + 3u, 1.6, 2.6, [&](double age, double life, Rng& r, int) {
            const double x0 = r.range(-60, 60), u = std::pow(age / life, 1.5), tx = (x0 < 0 ? -1 : 1) * r.range(40, 56),
                         ty = Y + r.range(4, 9), x = x0 + (tx - x0) * u, y = -1 + (ty + 1) * u - std::sin(PI * u) * 10;
            p.px(x, y, u > .6 ? K[3] : K[2]);
            if (age < .25) p.disc(x0, -1.5, 2 + age * 8, K[2], .4 * (1 - age / .25));
        });
        hole(p, C, t, pulse);
        cycle(t, D, 40, seed + 7u, .5, .9, [&](double age, double life, Rng& r, int) {
            const double a0 = r.range(0, TAU), d0 = r.range(48, 76), q = std::pow(age / life, 1.6),
                         d = RS + 2 + (d0 - RS - 2) * (1 - q), a = a0 + .9 * q, x = std::cos(a) * d * 1.15,
                         y = Y + std::sin(a) * d * .75, q2 = std::pow(std::max(0., age - .09) / life, 1.6),
                         d2 = RS + 2 + (d0 - RS - 2) * (1 - q2), a2 = a0 + .9 * q2;
            if (d < RS + 3 || y > -3 || y < -120 || Y + std::sin(a2) * d2 * .75 < -120 || std::abs(x) > 90) return;
            const auto c = r.pick<uint32_t>(std::array<uint32_t, 4>{WHITE, C[7], C[6], C[5]});
            p.lineFn(
                std::cos(a2) * d2 * 1.15, Y + std::sin(a2) * d2 * .75, x, y,
                [&](double u) { return u > .6 ? c : C[4]; }, std::min(1., age * 6));
            p.glow(x, y, 3, c, .3);
        });
        cycle(t, D, 22, seed, 1.4, 2.4, [&](double age, double life, Rng& r, int) {
            const double ph0 = r.range(0, TAU), rho0 = r.range(58, 80), height = r.range(-8, 8),
                         size = r.range(1.6, 2.8);
            const auto   at   = [&](double aa) {
                const double q = clamp(aa / life), rho = RS + 1 + (rho0 - RS - 1) * (1 - std::pow(q, 1.5)),
                             ph = ph0 + 2.4 * std::pow(q, 1.6) * (1 + 1.5 * q), x = std::cos(ph) * rho,
                             z = std::sin(ph) * rho, sy = -z * SI + height * (1 - q);
                return RockPosition{x * CT - sy * ST, Y + x * ST + sy * CT, z, rho};
            };
            const auto a = at(age), b = at(age - .04);
            const bool hidden = a.z > 0 && jsHypot2(a.x, a.y - Y) < RS + 1;
            if (hidden) return;
            if (a.rho < RS + 3) {
                p.px(a.x, a.y, WHITE);
                p.glow(a.x, a.y, 6, C[6], .6);
                return;
            }
            const double heat = 1 - smooth(16, 34, a.rho), len = 1 + heat * 7;
            if (heat > .05) {
                const double dx = a.x - b.x, dy = a.y - b.y, raw = jsHypot2(dx, dy), length = raw == 0 ? 1 : raw;
                p.lineFn(
                    a.x - (dx / length) * len, a.y - (dy / length) * len, a.x, a.y,
                    [&](double u) { return u > .7    ? WHITE
                                           : u > .35 ? C[6]
                                                     : C[4]; }, 1.);
                p.glow(a.x, a.y, 4, C[6], .4 * heat);
            } else {
                const double raw = jsHypot2(a.x, a.y - Y), length = raw == 0 ? 1 : raw, lx = -a.x / length,
                             ly = -(a.y - Y) / length;
                const auto   c = at(age - .09), d = at(age - .15);
                p.px(b.x, b.y, C[3]);
                p.dot(c.x, c.y, C[3], .7);
                p.dot(d.x, d.y, C[2], .5);
                const bool large = size > 2.2;
                p.px(a.x, a.y, K[2]);
                p.px(a.x + 1, a.y, K[1]);
                p.px(a.x, a.y + 1, K[1]);
                if (large) {
                    p.px(a.x + 1, a.y + 1, K[0]);
                    p.px(a.x - 1, a.y, K[1]);
                    p.px(a.x, a.y - 1, K[2]);
                }
                p.px(a.x + jsRound(lx * 1.4), a.y + jsRound(ly * 1.4), large ? C[6] : C[5]);
                p.glow(a.x, a.y, 4, C[5], .15);
            }
        });
        shadow::halo(p, 0, Y, RS - .4, RS + 4, C[6], .35 + .3 * pulse);
        shadow::unlight(p, -RS, Y - RS, RS, Y + RS, [&](double x, double y) {
            const double dx = x, dy = y - Y, v = -dx * ST + dy * CT, u = dx * CT + dy * ST;
            return u * u + v * v < std::pow(RS - .6, 2) && !(v > 0 && disk(u, -v / SI, t).has_value());
        });
    }
};
}  // namespace
const Descriptor& blackHoleDescriptor() {
    static const std::array<Option, 1> options{{{"rock", 1, 64, false, 4, &PixelFxShadowSettings::rock}}};
    static const Descriptor            d{
        {PixelFxKind::BlackHole, "shadow/black-hole", 184, 132, 92, 124, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed        = 4;
            s.palette     = {hex(0x06040b), hex(0x150c26), hex(0x2a1150), hex(0x461a84),
                             hex(0x6f2cc0), hex(0x9d5cec), hex(0xcfa6ff), hex(0xf6eaff)};
            s.shadow.rock = {hex(0x120d1c), hex(0x2e2440), hex(0x544468), hex(0x857399)};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<BlackHole>(); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
