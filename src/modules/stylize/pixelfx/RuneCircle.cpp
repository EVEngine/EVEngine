// eve-hot-path
// Native adaptation of pixelfx arcane/rune-circle.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Arcane.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 4, R = 46, RR = 39, RI = 23, RC = 7, SY = .3;
Point            at(double g, double r) { return {std::cos(g) * r, std::sin(g) * r * SY}; }
double           gap(double a, double b) { return std::abs(std::atan2(std::sin(a - b), std::cos(a - b))); }
void             circle(Paint& p, const std::vector<uint32_t>& C, double t, bool far) {
    const double ph = t / D, spin = ph * TAU / 4, chase = ph * TAU * 2;
    const auto   keep = [&](double g) { return (std::sin(g) < 0) == far; };
    const auto   lit  = [&](double g, double w) { return gap(g, chase) < w || gap(g, chase + PI) < w; };
    const auto   tone = [](double g, uint32_t hi, uint32_t lo) { return std::sin(g) < 0 ? lo : hi; };
    arcane::floorRing(
        p, 0, 0, R, SY, [&](double g) { return lit(g, .22)  ? WHITE
                                               : lit(g, .5) ? C[6]
                                                            : tone(g, C[6], C[5]); }, 1.,
        keep);
    arcane::floorRing(p, 0, 0, RI, SY, [&](double g) { return tone(g, C[5], C[4]); }, 1., keep);
    arcane::floorRing(p, 0, 0, RC, SY, [&](double g) { return tone(g, C[6], C[5]); }, 1., keep);
    for (int j = 0; j < 16; ++j) {
        const double g = spin + ((j + .5) * TAU) / 16;
        if ((std::sin(g) < 0) != far) continue;
        const auto [x, y] = at(g, R + 3.5);
        const bool hot    = lit(g, .3);
        p.px(x, y, hot ? WHITE : tone(g, j % 2 ? C[5] : C[6], C[4]));
        if (j % 2 == 0)
            for (double dx : {1., -1.}) p.px(x + dx, y, hot ? C[6] : tone(g, C[4], C[3]));
    }
    std::array<Point, 4> pts{};
    for (int k = 0; k < 4; ++k) pts[size_t(k)] = at(-spin + (k * TAU) / 4 + PI / 4, RI);
    for (size_t k = 0; k < 4; ++k) {
        auto [ax, ay] = pts[k];
        auto [bx, by] = pts[(k + 1) % 4];
        if ((ay < 0) != (by < 0)) {
            const double u = ay / (ay - by), mx = ax + (bx - ax) * u;
            if ((ay < 0) == far) {
                bx = mx;
                by = 0;
            } else {
                ax = mx;
                ay = 0;
            }
        } else if ((ay < 0) != far)
            continue;
        p.line(ax, ay, bx, by, far ? C[4] : C[5]);
    }
    struct Rune {
        int    k;
        double g;
    };
    std::array<Rune, 12> runes{};
    size_t               count = 0;
    for (int k = 0; k < 12; ++k) {
        const double g = spin + ((k + .5) * TAU) / 12;
        if ((std::sin(g) < 0) == far) runes[count++] = {k, g};
    }
    std::stable_sort(runes.begin(), runes.begin() + std::ptrdiff_t(count),
                     [](Rune a, Rune b) { return std::sin(a.g) < std::sin(b.g); });
    for (size_t i = 0; i < count; ++i) {
        const auto [k, g]      = runes[i];
        const auto [x, y]      = at(g, RR);
        const bool    hot      = lit(g, .2);
        const double  bob      = std::sin(ph * TAU * 4 + 4 * g) * .5;
        constexpr int glyphs[] = {0, 3, 5};
        arcane::glyph(p, glyphs[k % 3], x, y - 1 + bob,
                      hot   ? WHITE
                      : far ? C[5]
                            : C[6],
                      1,
                      hot   ? C[5]
                      : far ? C[2]
                            : C[3]);
        p.glow(x, y - 3, hot ? 10 : 6, C[5], hot ? .5 : far ? .12 : .22);
        p.glow(x, y, 5, C[4], .15);
    }
}
void motes(Paint& p, const std::vector<uint32_t>& C, double t, uint32_t seed, bool far) {
    cycle(t, D, 76, seed * 5u + 1u, 1.1, 1.9, [&](double age, double life, Rng& r, int) {
        const double                    g0 = r.range(0, TAU);
        constexpr std::array<double, 5> radii{R, R, RR, RR, RI};
        const double                    r0 = r.pick<double>(radii), q = age / life, g = g0 + q * r.range(.6, 1.4);
        if ((std::sin(g) < 0) != far) return;
        const double   rr = r0 * (1 - .35 * q), h = 2 + ease::outQuad(q) * r.range(26, 52), x = std::cos(g) * rr,
                       y = std::sin(g) * rr * SY - h, a = std::min(1., q * 5) * (1 - smooth(.65, 1, q));
        const bool     big   = r.isChance(.2);
        const uint32_t color = r.isChance(.3) ? WHITE : r.isChance(.6) ? C[6] : C[5];
        const double   size  = big ? 1.1 : r.range(.55, .9);
        arcane::mote(p, x, y, -std::sin(g) * rr * .8, -36 * (1 - q) - 10, color, size, a, big ? .5 : .3);
    });
    cycle(t, D, 14, seed * 5u + 2u, .45, [&](double age, double life, Rng& r, int) {
        const double g = r.range(0, TAU);
        if ((std::sin(g) < 0) != far) return;
        const double rr = r.range(8, R), x = std::cos(g) * rr, y = std::sin(g) * rr * SY - r.range(6, 34),
                     k = std::sin((age / life) * PI);
        p.sparkle(x, y, 1 + 2.4 * k, C[6], C[5], .35 * k);
    });
}
struct RuneCircle final : AnalyticKernel {
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C     = o.palette;
        const double pulse = .85 + .15 * std::sin(t / D * TAU * 4);
        p.glow(0, 0, R * 1.4, C[4], .24 * pulse);
        for (double s : {-1., 1.}) p.glow(s * R * .55, 1, R * .7, C[5], .12 * pulse);
        for (int h = 0; h < 66; h += 6) p.glow(0, -h, 26, C[4], .11 * pulse * (1 - h / 80.));
        circle(p, C, t, true);
        motes(p, C, t, o.seed, true);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C     = o.palette;
        const double chase = t / D * TAU * 2;
        circle(p, C, t, false);
        motes(p, C, t, o.seed, false);
        for (double g : {chase, chase + PI}) {
            const auto [x, y] = at(g, R);
            p.sparkle(x, y, 2.5 + std::sin(t * 30) * .8, C[6], C[5], .5);
            p.glow(x, y, 14, C[5], .4);
        }
    }
};
}  // namespace
const Descriptor& runeCircleDescriptor() {
    static const Descriptor d{
        {PixelFxKind::RuneCircle, "arcane/rune-circle", 136, 100, 68, 80, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 8;
            s.palette = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                         hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
        },
        nullptr,
        {},
        nullptr,
        nullptr,
        [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<RuneCircle>(); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
