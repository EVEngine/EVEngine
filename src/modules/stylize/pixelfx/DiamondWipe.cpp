// eve-hot-path
// Native adaptation of pixelfx transitions/diamond-wipe.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScreenTransition.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double HD = 17, GROW = .24, IN0 = .04, IN1 = .66, OUT0 = 1.05, OUT1 = 1.56;
struct Diamond {
    double cx, cy, tin, tout;
    bool   glint;
    double spin;
};
struct DiamondWipe final : AnalyticKernel {
    const std::vector<Diamond>  lattice;
    static std::vector<Diamond> prepare(uint32_t seed) {
        std::vector<Diamond> out;
        out.reserve(195);
        for (int j = -1; j <= 13; ++j)
            for (int i = -1; i <= 11; ++i) {
                const double cx = i * 32 + ((j & 1) ? 16 : 0), cy = j * 16;
                if (cx + HD < 0 || cy + HD < 0 || cx - HD > screen::W || cy - HD > screen::H) continue;
                const double u  = clamp((cx + .45 * cy) / (screen::W + .45 * screen::H)),
                             jr = rand({uint32_t(i), uint32_t(j), seed});
                out.push_back(
                    {cx, cy, IN0 + (IN1 - IN0) * clamp(u + (jr - .5) * .08),
                     OUT0 + (OUT1 - OUT0) * clamp(u + (rand({uint32_t(i), uint32_t(j), seed + 1}) - .5) * .08), jr < .3,
                     rand({uint32_t(i), uint32_t(j), seed + 2}) * 3});
            }
        return out;
    }
    explicit DiamondWipe(const PixelFxSettings& o) : lattice(prepare(o.seed)) {}
    static double radius(double ki, double ko) {
        return HD * (ko > 0 ? 1 - ease::inBack(ko, 2) : ease::outBack(std::min(1., ki), 2.2));
    }
    void drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void draw(Paint& p0, double t, const PixelFxSettings& o) const override {
        const auto&                                  C   = o.palette;
        const auto                                   ink = o.transition.color;
        auto                                         p   = screen::clip(p0);
        const std::array<std::array<uint32_t, 3>, 4> face{
            {{C[2], C[1], C[0]}, {C[1], C[0], C[0]}, {C[1], C[0], C[0]}, {C[0], C[0], ink}}};
        for (const auto& d : lattice) {
            const double ki = (t - d.tin) / GROW, ko = (t - d.tout) / GROW;
            if (ki <= 0 || ko >= 1) continue;
            const double hd = radius(ki, ko), heat = ko > 0 ? smooth(0, .35, ko) : 1 - smooth(.45, 1.25, ki);
            if (hd < .5) continue;
            const bool settled = heat <= 0;
            const int  y0      = std::max(0, int(std::floor(d.cy - hd))),
                       y1      = std::min(screen::H - 1, int(std::ceil(d.cy + hd)));
            for (int y = y0; y <= y1; ++y) {
                const double dy = y + .5 - d.cy, w = hd - std::abs(dy);
                if (w <= 0) continue;
                const int x0 = std::max(0, int(std::ceil(d.cx - w - .5))),
                          x1 = std::min(screen::W - 1, int(std::floor(d.cx + w - .5)));
                if (x1 < x0) continue;
                if (settled) {
                    screen::rect(p0, x0, y, x1 - x0 + 1, 1, ink);
                    continue;
                }
                const size_t st = heat > .66 ? 0 : heat > .33 ? 1 : 2;
                for (int x = x0; x <= x1; ++x) {
                    const double dx = x + .5 - d.cx, e = hd - std::abs(dx) - std::abs(dy);
                    const size_t q  = (dy < 0 ? 0u : 2u) + (dx < 0 ? 0u : 1u);
                    const bool   vr = dx > -1 && dx <= 0, hr = dy > -1 && dy <= 0;
                    uint32_t     c;
                    if (e < 1) {
                        const std::array<uint32_t, 4> rim{heat > .5 ? WHITE : C[6], heat > .5 ? C[6] : C[5],
                                                          heat > .5 ? C[5] : C[4], heat > .5 ? C[4] : C[3]};
                        c = heat > .15 ? rim[q] : ink;
                        if (heat > .15) p0.light(x, y, C[5], .4 * heat * (q == 0 ? 1 : .6));
                    } else if (e < 2 && q == 0 && heat > .4)
                        c = C[5];
                    else if ((vr || hr) && heat > .3) {
                        const bool tip   = vr && dy < -hd + 4,
                                   sheen = std::abs(std::fmod(std::fmod(x + y - t * 260, 90) + 90, 90) - 45) < 3;
                        c                = tip || (sheen && heat > .5) ? WHITE
                                           : vr && dy < 0              ? (heat > .6 ? C[6] : C[5])
                                           : heat > .6                 ? C[3]
                                                                       : C[2];
                    } else
                        c = heat > .15 ? face[q][st] : ink;
                    p0.px(x, y, c);
                }
            }
            if (!settled) p.glow(d.cx, d.cy, hd + 6, C[4], .22 * heat);
        }
        // The source appends glints while rastering but draws them after all facets.
        // Revisit immutable geometry in identical insertion order without a frame allocation.
        const auto sparkle = [&](const Diamond& d, double u, double hd) {
            if (u <= 0 || u >= 1) return;
            screen::glint(p, d.cx, hd ? d.cy - hd : d.cy, 9 * std::pow(std::sin(PI * u), .7), d.spin + u * 1.5, C[6],
                          C[5]);
        };
        for (const auto& d : lattice) {
            const double ki = (t - d.tin) / GROW, ko = (t - d.tout) / GROW;
            if (d.glint && ko > .7 && ko < 1.5) sparkle(d, (ko - .7) / .8, 0);
            if (ki <= 0 || ko >= 1) continue;
            const double hd = radius(ki, ko);
            if (hd < .5) continue;
            if (ki > .6 && ki < 1.8 && d.glint) sparkle(d, (ki - .6) / 1.2, hd);
        }
    }
};
}  // namespace
const Descriptor& diamondWipeDescriptor() {
    static const std::array<Option, 1> options{
        {{"color", 0, 4294967295., false, 0, &PixelFxTransitionSettings::color, true}}};
    static const Descriptor d{{PixelFxKind::DiamondWipe, "transitions/diamond-wipe", 320, 180, 0, 0, 2, false},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed             = 1;
                                  s.transition.color = hex(0x0b0a10);
                                  s.palette          = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                                                        hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
                              },
                              nullptr,
                              options,
                              nullptr,
                              nullptr,
                              [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<DiamondWipe>(s);
                              }};
    return d;
}
}  // namespace eve::stylize::pixelfx
