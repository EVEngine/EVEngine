// eve-hot-path
// Adapted from patch-gg/pixelfx impacts/impact-frame.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include <array>
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"

namespace eve::stylize::pixelfx {
namespace {
struct Wedge {
    double a, inner, w;
};
}  // namespace
void impactFrame(Paint& p, double t, const PixelFxSettings& o) {
    const int f = int(std::floor(t * 30 + 1e-6));
    if (f >= 8) return;
    Rng                                   R(o.seed);
    std::array<std::array<Wedge, 256>, 2> sets{};
    for (auto& set : sets)
        for (int i = 0; i < o.lines; ++i) {
            const bool   thick = R.next() < 0.18;
            const double a     = (i + R.range(0.1, 0.9)) / o.lines * TAU;
            const double inner = thick ? R.range(30, 48) : R.range(18, 70),
                         w     = thick ? R.range(5, 8) : R.range(1.2, 3.6);
            set[size_t(i)]     = {a, inner, w};
        }
    std::array<std::array<double, 2>, 2> jit{};
    for (auto& j : jit) {
        j[0] = std::floor(R.range(-2, 2) + 0.5);
        j[1] = std::floor(R.range(-1, 1) + 0.5);
    }
    std::array<double, 4> ph{};
    for (auto& v : ph) v = R.range(0, TAU);
    const auto&    set = sets[size_t(f % 2)];
    const double   jx = f < 4 ? jit[size_t(f % 2)][0] : 0, jy = f < 4 ? jit[size_t(f % 2)][1] : 0;
    const double   reach = f < 4 ? 0 : std::pow((f - 3) / 4.5, 1.5), thin = f < 4 ? 1 : 1 - (f - 3) / 5.5;
    const bool     inv = f == 2 || f == 3;
    const uint32_t ink = inv ? o.palette[7] : o.palette[0], paper = inv ? o.palette[0] : o.palette[7];
    p.shade(-160, -90, 160, 90, [&](double x, double y, int sx, int sy) {
        // Source masks are indexed by local cells even under a transformed painter.
        // Evaluate that cell procedurally rather than interpolating the mask recipe in continuous space.
        const int i = int(std::floor(x + 160)), j = int(std::floor(y + 90));
        if (i < 0 || j < 0 || i >= 320 || j >= 180) return;
        const double X = -160 + i + .5 - jx, Y = -90 + j + .5 - jy, rho = jsHypot2(X, Y * 1.35),
                     ang  = std::atan2(Y * 1.35, X);
        const int    slot = int(std::floor(wrap(ang, TAU) / (TAU / o.lines)));
        bool         line = false;
        for (int k = -1; k <= 1 && !line; ++k) {
            const auto& l = set[size_t((slot + k + o.lines) % o.lines)];
            double      d = wrap(ang - l.a, TAU);
            if (d > PI) d = TAU - d;
            const double inner = l.inner + reach * 150, hw = l.w * clamp((rho - inner) / 40) * thin;
            line = rho > inner && d * rho < std::max(0.5, hw);
        }
        if (f < 4) {
            const double a = std::atan2(Y * 1.1, X), phase = ph[size_t(f)];
            const double spikes = 10 + 6 * std::abs(std::sin(a * 5 + phase)) +
                                  4 * std::abs(std::sin(a * 11 + phase * 2.7)) + 2 * std::sin(a * 23 + phase);
            const double r2 = jsHypot2(X, Y * 1.1), core = spikes * (f == 0 ? 0.85 : 1 + f * 0.08);
            p.put(sx, sy, r2 < core ? (r2 < 4 + f ? paper : ink) : line ? ink : paper);
        } else if (line)
            p.put(sx, sy, o.palette[7]);
    });
}
}  // namespace eve::stylize::pixelfx

namespace eve::stylize::pixelfx {

const Descriptor& impactFrameDescriptor() {
    static const std::array<Option, 1> options{{{"lines", 1, 256, false, 0, &PixelFxSettings::lines}}};
    static const Descriptor            descriptor{
        {PixelFxKind::ImpactFrame, "impacts/impact-frame", 320, 180, 160, 90, 0.3, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 19;
            s.palette = {hex(0x000000), hex(0xffffff), hex(0xffffff), hex(0xffffff),
                         hex(0xffffff), hex(0xffffff), hex(0xffffff), hex(0xffffff)};
        },
        &impactFrame,
        options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
