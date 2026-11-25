// eve-hot-path
// Native adaptation of pixelfx scifi/hologram.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 4, OY = -50, RAD = 16, LENS = -6;
struct Vec3 {
    double x, y, z;
};
struct Tear {
    double y, height, dx;
};
struct Hologram final : AnalyticKernel {
    std::array<Vec3, 8>                   vertices;
    std::array<std::array<size_t, 3>, 12> faces;
    std::array<std::array<size_t, 2>, 18> edges;
    explicit Hologram(const PixelFxSettings&) {
        vertices[0] = {0, 1.45, 0};
        vertices[1] = {0, -1.45, 0};
        for (size_t i = 0; i < 6; ++i) {
            const double angle = (double(i) / 6) * TAU;
            vertices[i + 2]    = {std::cos(angle) * .9, .12, std::sin(angle) * .9};
            faces[i * 2]       = {0, 2 + i, 2 + (i + 1) % 6};
            faces[i * 2 + 1]   = {1, 2 + (i + 1) % 6, 2 + i};
            edges[i * 3]       = {0, 2 + i};
            edges[i * 3 + 1]   = {1, 2 + i};
            edges[i * 3 + 2]   = {2 + i, 2 + (i + 1) % 6};
        }
    }
    void drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&    C     = o.palette;
        const auto&    H     = o.scifi.metal;
        const uint32_t split = o.scifi.split, S = o.seed, frame = jsUint(std::floor(t * 30));
        const double   phase  = t / D;
        double         glitch = 0;
        for (const auto& g : std::array<std::array<double, 3>, 3>{{{.92, .12, 1}, {2.55, .2, 1.4}, {3.42, .06, .7}}})
            if (t >= g[0] && t < g[0] + g[1]) glitch = std::max(glitch, g[2]);
        const bool          flick = glitch > 0 && rand({S, frame}) < .5;
        const double        cx    = glitch ? (rand({S, frame, 2}) < .5 ? -2 : 2) * glitch : 0,
                            cy    = OY + 1.5 * std::sin(TAU * phase * 2);
        std::array<Tear, 2> tears{};
        size_t              tearCount = 0;
        if (glitch != 0)
            for (uint32_t i = 0; i < 2; ++i)
                tears[tearCount++] = {std::floor(cy - RAD + rand({S, frame, i}) * RAD * 2),
                                      2 + std::floor(rand({S, frame, i, 1}) * 4),
                                      jsRound((rand({S, frame, i, 2}) - .5) * 10 * glitch)};
        const auto put = [&](double x, double y, uint32_t color, double alpha = 1) {
            const int yy = int(std::floor(y));
            if (flick && (yy & 1)) return;
            double dx = 0;
            for (size_t i = 0; i < tearCount; ++i)
                if (yy >= tears[i].y && yy < tears[i].y + tears[i].height) {
                    dx = tears[i].dx;
                    break;
                }
            if (dx != 0) {
                p.dot(x + dx, y, split, alpha * .8);
                p.dot(x + dx + (dx > 0 ? 1 : -1), y, color, alpha);
                return;
            }
            p.dot(x, y, color, alpha);
        };
        p.shade(-16, -8, 16, 2, [&](double x, double y, int sx, int sy) {
            const double q   = std::pow(x / 15, 2) + std::pow((y + 5) / 3, 2);
            const bool   top = q <= 1, side = std::abs(x) <= 15 && y > -5 && y <= 0;
            if (!top && !side) return;
            p.put(sx, sy,
                  top                          ? (q > .8   ? H[0]
                                                  : y < -6 ? H[5]
                                                           : H[3])
                  : std::abs(x) > 14 || y == 0 ? H[0]
                  : y == -4                    ? H[4]
                  : x < -6                     ? H[3]
                  : x > 6                      ? H[1]
                                               : H[2]);
        });
        p.ellipse(0, LENS + .5, 5.5, 1.6, [&](double d) { return d < .4 ? WHITE : d < .8 ? C[6] : C[4]; });
        for (int i = 0; i < 4; ++i) {
            const double k = .5 + .5 * std::cos(TAU * (2 * phase - double(i) / 4));
            p.px(-9 + i * 6, -2, k > .7 ? C[6] : C[3]);
        }
        const double top = cy + RAD * 1.3;
        for (double y = std::ceil(top); y < LENS; ++y) {
            const double v = (y - LENS) / (top - LENS), w = lerp(4, RAD + 4, v);
            const bool   lineOn = wrap(y + t * 24, 5) < 1;
            for (double x = std::floor(-w); x <= std::ceil(w); ++x) {
                const double q = std::abs(x + .5) / w;
                if (q > 1) continue;
                if (q > 1 - 1 / w)
                    put(x, y, v < .5 ? C[5] : C[4]);
                else
                    p.blend(x + .5, y + .5, lineOn ? C[5] : C[4],
                            (.05 + .12 * (1 - v)) * (1 - .6 * q * q) + (lineOn ? .06 : 0));
            }
        }
        p.glow(0, LENS, 12, C[5], .6);
        for (int i = 0; i < 56; ++i) {
            const double a = (double(i) / 56) * TAU - phase * TAU, x = std::cos(a) * (RAD + 4),
                         y = top + std::sin(a) * 3;
            if (i % 7 < 5) put(x, y, std::sin(a) > 0 ? C[6] : C[4], std::sin(a) > 0 ? 1 : .7);
        }
        p.glowLine(-RAD - 4, top, RAD + 4, top, 4, C[4], .2);
        cycle(t, D, 18, S + 1, .8, 1.4, [&](double age, double life, Rng& r, int) {
            const double k = age / life, y = lerp(LENS - 2, top, k), w = lerp(4, RAD + 2, k), x = r.range(-.8, .8) * w;
            const auto   color = r.isChance(.3) ? WHITE : C[5];
            put(x, y, color, std::min(1., (1 - k) * 2));
        });
        std::array<Vec3, 8> projected{};
        const double        spin = TAU * phase, tilt = .32;
        for (size_t i = 0; i < vertices.size(); ++i) {
            const auto&  v  = vertices[i];
            const double x1 = v.x * std::cos(spin) + v.z * std::sin(spin),
                         z1 = -v.x * std::sin(spin) + v.z * std::cos(spin),
                         y2 = v.y * std::cos(tilt) - z1 * std::sin(tilt),
                         z2 = v.y * std::sin(tilt) + z1 * std::cos(tilt);
            projected[i]    = {cx + x1 * RAD, cy - y2 * RAD, z2};
        }
        const double scanY = lerp(cy + RAD * 1.5, cy - RAD * 1.5, std::fmod(phase * 2, 1) / .8);
        for (const auto& face : faces) {
            const auto&  a  = projected[face[0]];
            const auto&  b  = projected[face[1]];
            const auto&  c  = projected[face[2]];
            const double nz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (nz >= 0) continue;
            const double               shade = clamp(.3 + .7 * ((a.z + b.z + c.z) / 3 + .5));
            const std::array<Point, 3> tri{{{a.x, a.y}, {b.x, b.y}, {c.x, c.y}}};
            p.poly(
                tri, [&](double, double y) { return std::abs(y - scanY) < 2 ? C[6]
                                                    : shade > .6            ? C[4]
                                                                            : C[3]; },
                [&](double, double y) {
                    const int yy = int(std::floor(y));
                    return ((yy % 3 == 0 && !(flick && (yy & 1))) || std::abs(y - scanY) < 1.5) ? 1. : 0.;
                });
        }
        struct Edge {
            Vec3   a, b;
            double depth;
        };
        std::array<Edge, 18> ordered{};
        for (size_t i = 0; i < edges.size(); ++i) {
            const auto& a = projected[edges[i][0]];
            const auto& b = projected[edges[i][1]];
            ordered[i]    = {a, b, (a.z + b.z) / 2};
        }
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const Edge& a, const Edge& b) { return a.depth < b.depth; });
        for (const auto& e : ordered) {
            const double L = jsHypot2(e.b.x - e.a.x, e.b.y - e.a.y);
            for (double d = 0; d <= L; d += .5) {
                const double x = lerp(e.a.x, e.b.x, d / L), y = lerp(e.a.y, e.b.y, d / L);
                const bool   hot = std::abs(y - scanY) < 1.5;
                if (e.depth < -.15)
                    put(x, y, hot ? C[5] : C[3], .7);
                else
                    put(x, y, hot ? WHITE : e.depth > .4 ? C[6] : C[5]);
            }
        }
        for (const auto& v : projected) {
            if (v.z < -.15) continue;
            put(v.x, v.y, WHITE);
            p.glow(v.x, v.y, 4, C[5], .3);
        }
        for (int i = 0; i < 64; ++i) {
            const double a = (double(i) / 64) * TAU + phase * TAU * 2, x = cx + std::cos(a) * (RAD + 9),
                         y = cy + 3 + std::sin(a) * 4;
            if ((i / 3) % 3 == 2) continue;
            put(x, y, std::sin(a) > 0 ? (i % 9 == 0 ? WHITE : C[5]) : C[3], std::sin(a) > 0 ? 1 : .6);
        }
        const double light = flick ? .5 : 1;
        p.glow(cx, cy, RAD * 2.2, C[4], .36 * light);
        p.glow(cx, cy - RAD * .6, RAD, C[5], .15 * light);
        if (std::abs(scanY - cy) < RAD * 1.4) p.glowLine(cx - RAD, scanY, cx + RAD, scanY, 5, C[5], .3 * light);
        p.glow(0, -2, 22, C[4], .2);
    }
};
}  // namespace
const Descriptor& hologramDescriptor() {
    static const std::array<Option, 2> options{
        {{"metal", 1, 64, false, 6, &PixelFxScifiSettings::metal},
         {"split", 4278190080., 4294967295., false, 0, &PixelFxScifiSettings::split, true}}};
    static const Descriptor d{{PixelFxKind::Hologram, "scifi/hologram", 112, 104, 56, 92, D, true},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed        = 21;
                                  s.palette     = {hex(0x050c1e), hex(0x0a1f48), hex(0x123b80), hex(0x1d64b8),
                                                   hex(0x2e98e8), hex(0x6cd0ff), hex(0xc8f0ff), WHITE};
                                  s.scifi.metal = {hex(0x0e0d14), hex(0x1e1c27), hex(0x312e3e),
                                                   hex(0x4b4759), hex(0x6f6a82), hex(0x9d98b0)};
                                  s.scifi.split = hex(0xff4fd8);
                              },
                              nullptr,
                              options,
                              nullptr,
                              nullptr,
                              [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<Hologram>(s);
                              }};
    return d;
}
}  // namespace eve::stylize::pixelfx
