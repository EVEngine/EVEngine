// eve-hot-path
// Native adaptation of pixelfx arcane/arcane-orb.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Arcane.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4, RAD = 17, Y0 = -52;
struct Ring {
    double radius, sy, rotation;
    int    beads, turn, color;
};
constexpr std::array<Ring, 2> rings{{{31, .24, .36, 3, 2, 0}, {27, .3, -.55, 2, -3, 1}}};
struct Scene {
    double phase, cx, cy;
};
Scene setup(double t) {
    const double ph = t / D;
    return {ph, 0, Y0 + std::sin(ph * TAU) * 2};
}
void ring(Paint& p, const std::vector<uint32_t>& C, const Scene& s, const Ring& R, bool near) {
    const double rot = R.rotation + .12 * std::sin(s.phase * TAU + R.color), cr = std::cos(rot), sr = std::sin(rot);
    const int    n   = int(std::ceil(R.radius * TAU * 1.5));
    const auto   pos = [&](double g) {
        const double lx = std::cos(g) * R.radius, ly = std::sin(g) * R.radius * R.sy;
        return Point{s.cx + lx * cr - ly * sr, s.cy + lx * sr + ly * cr};
    };
    for (int j = 0; j < n; ++j) {
        const double g = (double(j) / n) * TAU, z = std::sin(g);
        if ((z > 0) != near) continue;
        const auto [x, y] = pos(g);
        p.dot(x, y, near ? C[5] : C[4], near ? .7 : .4);
    }
    for (int b = 0; b < R.beads; ++b) {
        const double g = s.phase * TAU * R.turn + (b * TAU) / R.beads;
        for (int k = 0; k < 7; ++k) {
            const double gg = g - (R.turn > 0 ? 1 : -1) * k * .07, z = std::sin(gg);
            if ((z > 0) != near) continue;
            const auto [x, y] = pos(gg);
            p.dot(x, y, k == 0 ? WHITE : k < 3 ? C[6] : C[5], k < 4 ? 1 : 1 - double(k - 3) / 4);
            if (k == 0) p.glow(x, y, near ? 7 : 5, C[6], near ? .5 : .25);
        }
    }
}
void orb(Paint& p, const std::vector<uint32_t>& C, const Scene& s) {
    const auto [ph, cx, cy] = s;
    p.disc(
        cx, cy, RAD, [&](double d, double) { return d > .82 ? C[2] : C[1]; },
        [](double d, double) { return .32 + .5 * d * d; });
    for (const auto [offset, main] :
         std::array<std::pair<double, bool>, 4>{{{0, true}, {PI, true}, {PI / 2, false}, {(3 * PI) / 2, false}}})
        for (double u = .08; u <= .85; u += .012) {
            const double a = ph * TAU * 2 + offset + u * 3.4, r = u * RAD * .92, x = cx + std::cos(a) * r,
                         y = cy + std::sin(a) * r, fade = u < .6 ? 1 : 1 - (u - .6) / .25;
            if (!main) {
                p.dot(x, y, C[4], fade * .8);
                continue;
            }
            const double b = a - .16, xb = cx + std::cos(b) * r, yb = cy + std::sin(b) * r;
            p.dot(xb, yb, u < .4 ? C[6] : C[5], fade);
            p.dot(x, y, u < .3 ? WHITE : u < .55 ? C[6] : C[5], fade);
        }
    const double pulse = .85 + .15 * std::sin(ph * TAU * 3);
    p.circle(cx, cy, 3.6 * pulse + 1, C[5]);
    p.circle(cx, cy, 2.8 * pulse, C[6]);
    p.circle(cx, cy, 1.7 * pulse, WHITE);
    p.ring(cx, cy, RAD + .4, 1, C[5]);
    p.ring(cx, cy, RAD + 1.3, 1, C[2], .8);
    for (double a = 3.5; a < 4.7; a += .03)
        p.dot(cx + std::cos(a) * (RAD - 2.4), cy + std::sin(a) * (RAD - 2.4), a > 3.85 && a < 4.35 ? WHITE : C[6]);
    p.circle(cx - RAD * .42, cy - RAD * .5, 1.3, WHITE);
    p.dot(cx + RAD * .52, cy + RAD * .55, C[6]);
    p.glow(cx, cy, 20, C[6], .26 * pulse);
    p.glowRing(cx, cy, RAD, 6, C[5], .25);
}
void motes(Paint& p, const std::vector<uint32_t>& C, const Scene& s, uint32_t seed, bool near) {
    for (int i = 0; i < 8; ++i) {
        const double a  = std::fmod(i * 2.4 + double(seed), TAU) + s.phase * TAU * (i % 2 ? 1 : -1) * (1 + i % 3),
                     rr = RAD + 5 + std::fmod(i * .37, 1) * 12, tilt = (std::fmod(i * .71, 1) - .5) * 1.4,
                     x0 = std::cos(a) * rr, z = std::sin(a);
        if ((z > 0) != near) continue;
        const double x = s.cx + x0 * std::cos(tilt), y = s.cy + x0 * std::sin(tilt) + z * rr * .28;
        const int    dir = i % 2 ? 1 : -1;
        p.dot(x, y, near ? WHITE : C[5]);
        p.dot(x - dir, y, near ? C[6] : C[4], near ? 1 : .6);
        if (near) {
            p.dot(x - 2 * dir, y, C[4], .6);
            p.glow(x, y, 5, C[6], .35);
        }
    }
    if (!near) return;
    cycle(s.phase * D, D, 10, seed * 3u + 1u, .6, 1., [&](double age, double life, Rng& r, int) {
        const double a = r.range(0, TAU), q = age / life, d = RAD + 1 + q * r.range(8, 18), x = s.cx + std::cos(a) * d,
                     y = s.cy + std::sin(a) * d - q * 10;
        arcane::mote(p, x, y, std::cos(a) * 25, std::sin(a) * 25 - 12, r.isChance(.5) ? C[6] : C[5], .6, 1 - q, .3);
    });
}
struct ArcaneOrb final : AnalyticKernel {
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C    = o.palette;
        const auto   s    = setup(t);
        const double lift = (s.cy - Y0) / 2;
        p.glow(0, -1, 36, C[4], .26 - .04 * lift);
        for (int x = -20; x <= 20; ++x) {
            const double u = std::abs(x + .5) / 20;
            p.dot(x, 0, u < .3 ? C[6] : C[5], (1.1 - u) * (.9 - .15 * lift));
        }
        p.glow(s.cx, s.cy, 60, C[4], .36);
        for (const auto& R : rings) ring(p, C, s, R, false);
        motes(p, C, s, o.seed, false);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        const auto  s = setup(t);
        orb(p, C, s);
        for (const auto& R : rings) ring(p, C, s, R, true);
        motes(p, C, s, o.seed, true);
        p.sparkle(s.cx - RAD * .42, s.cy - RAD * .5, 1.5 + std::max(0., std::sin(s.phase * TAU * 2)) * 2, C[6], C[5],
                  .3);
    }
};
}  // namespace
const Descriptor& arcaneOrbDescriptor() {
    static const Descriptor d{
        {PixelFxKind::ArcaneOrb, "arcane/arcane-orb", 136, 112, 68, 104, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 6;
            s.palette = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                         hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
        },
        nullptr,
        {},
        nullptr,
        nullptr,
        [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<ArcaneOrb>(); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
