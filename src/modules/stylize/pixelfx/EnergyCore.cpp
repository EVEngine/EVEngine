// eve-hot-path
// Native adaptation of pixelfx scifi/energy-core.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScifiArcs.h"
#include "stylize/pixelfx/ScifiGhosts.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4, CY = -36, R0 = 11, RC = 24, TOPY = -64, BOTY = -9, SLOT = .1;
constexpr int    NS = 24;
struct Beat {
    double time, weight;
    bool   strong;
};
constexpr std::array<Beat, 4> BEATS{{{.05, 1, true}, {.25, .5, false}, {1.25, 1, true}, {1.45, .5, false}}};
struct Bar {
    double x, z;
};
struct Ring {
    double r, tilt;
};
constexpr std::array<Ring, 2> RINGS{{{17, 1.15}, {19, -.9}}};
struct Arc {
    int                             slot;
    std::vector<Point>              path;
    std::vector<std::vector<Point>> branches;
};
struct Vec3 {
    double x, y, z;
};
double pulseAt(double t) {
    double pulse = 0;
    for (const auto& b : BEATS) {
        const double a = scifi::since(t, b.time, D);
        pulse          = std::max(pulse, b.weight * (a < .03 ? a / .03 : std::exp(-(a - .03) / .12)));
    }
    return pulse;
}
Vec3 ringPoint(double r, double tilt, double spin, double a) {
    const double x = r * std::cos(a), z0 = r * std::sin(a), y = -z0 * std::sin(tilt), z = z0 * std::cos(tilt);
    return {x * std::cos(spin) + z * std::sin(spin), CY - y, -x * std::sin(spin) + z * std::cos(spin)};
}
struct EnergyCore final : AnalyticKernel {
    std::array<Bar, 6> bars{};
    std::vector<Arc>   arcs;
    scifi::Ghosts      ghosts{D, 0, std::array<double, 4>{-54, -98, 54, 11}};
    explicit EnergyCore(const PixelFxSettings& o) {
        for (size_t i = 0; i < bars.size(); ++i) {
            const double a = (double(i) / 6) * TAU + PI / 6;
            bars[i]        = {RC * std::sin(a), std::cos(a)};
        }
        const auto& C = o.palette;
        ghosts.sparks = {WHITE, C[6], C[5], C[4]};
        ghosts.haze   = C[5];
        Rng              rn(o.seed);
        std::vector<Bar> eligible;
        for (const auto& b : bars)
            if (b.z > -.2) eligible.push_back(b);
        for (int s = 0; s < NS; ++s) {
            const double ts  = s * SLOT;
            bool         hot = false;
            for (const auto& b : BEATS)
                if (b.strong && scifi::since(ts, b.time, D) < .16) hot = true;
            for (int n = 0; n < (hot ? 2 : 1); ++n) {
                if (!hot && !rn.isChance(.4)) continue;
                const auto   bar = rn.pick<Bar>(eligible);
                const double ty = rn.range(TOPY + 8, BOTY - 6), ang = std::atan2(ty - CY, bar.x),
                             sx = std::cos(ang) * R0, sy = CY + std::sin(ang) * R0;
                auto         path     = bolt(rn, sx, sy, bar.x - (bar.x > 0 ? 1 : bar.x < 0 ? -1 : 0) * 1.5, ty, .3, 4);
                const int    count    = rn.integer(0, 1);
                auto         branches = forks(rn, path, count, 7, .35);
                arcs.push_back({s, std::move(path), std::move(branches)});
                for (int i = 0; i < 5; ++i) {
                    const double a = std::atan2(ty - sy, bar.x - sx) + PI + rn.range(-1.2, 1.2), sp = rn.range(60, 170),
                                 life = rn.range(.2, .5);
                    ghosts.add(ts, ParticleKind::Spark, bar.x, ty, std::cos(a) * sp, std::sin(a) * sp - 40, life);
                }
            }
        }
        for (const auto& b : BEATS)
            if (b.strong)
                for (int i = 0; i < 12; ++i) {
                    const double a = rn.range(0, TAU), sp = rn.range(40, 110), life = rn.range(.25, .5);
                    const std::array<uint32_t, 3> colors{WHITE, C[6], C[5]};
                    const auto                    color = rn.pick<uint32_t>(colors);
                    ghosts.add(b.time + .02, ParticleKind::Mote, std::cos(a) * R0, CY + std::sin(a) * R0,
                               std::cos(a) * sp, std::sin(a) * sp, life, color, .7);
                }
    }
    static void plinth(Paint& p, std::span<const uint32_t> C, std::span<const uint32_t> H, double t, double pulse) {
        p.shade(-30, -10, 30, 2, [&](double x, double y, int sx, int sy) {
            const double q   = std::pow(x / 29, 2) + std::pow((y + 7) / 4, 2);
            const bool   top = q <= 1, side = std::abs(x) <= 29 && y > -7 && y <= 0;
            if (!top && !side) return;
            p.put(sx, sy,
                  top                          ? (q > .82  ? H[0]
                                                  : y < -8 ? H[5]
                                                  : x < 0  ? H[4]
                                                           : H[3])
                  : std::abs(x) > 28 || y == 0 ? H[0]
                  : y == -6                    ? H[4]
                  : x < -14                    ? H[3]
                  : x > 14                     ? H[1]
                                               : H[2]);
        });
        for (int i = 0; i < 7; ++i) {
            const double x = -18 + i * 6, k = .5 + .5 * std::cos(TAU * (2 * (t / D) - double(i) / 7));
            p.dot(x, -3, k > .75 ? WHITE : k > .4 ? C[5] : C[2]);
            if (k > .6) p.glow(x, -3, 4, C[4], .3 * k);
        }
        p.glow(0, -8, 26, C[4], .25 + .3 * pulse);
    }
    static void barDraw(Paint& p, const Bar& b, std::span<const uint32_t> C, std::span<const uint32_t> H, double pulse,
                        bool back) {
        const uint32_t lit    = back ? C[3] : pulse > .5 ? C[6] : C[5];
        const double   inward = b.x > 0 ? -1 : b.x < 0 ? 1 : 0;
        for (int y = int(TOPY) + 2; y < BOTY; ++y)
            for (int i = -1; i <= 1; ++i) {
                const bool face = i == inward;
                p.dot(b.x + i, y, back ? (face ? lit : H[1]) : face ? lit : i == 0 ? H[3] : H[1]);
            }
    }
    static void hoop(Paint& p, double y, double rx, std::span<const uint32_t> H, std::span<const uint32_t> C, bool near,
                     double pulse) {
        const int n = int(std::ceil(rx * 8));
        for (int i = 0; i < n; ++i) {
            const double g = (double(i) / n) * TAU, s = std::sin(g);
            if ((s >= 0) != near) continue;
            const double x = std::cos(g) * rx, yy = y + s * 4;
            p.dot(x, yy, near ? (s > .6 ? H[4] : H[3]) : H[1]);
            p.dot(x, yy + 1, near ? H[1] : H[0]);
            if (near && s < .3) p.dot(x, yy - 1, pulse > .5 ? C[5] : C[3]);
        }
    }
    static void core(Paint& p, double t, std::span<const uint32_t> C, double R, double pulse, uint32_t seed) {
        const double ph = t / D;
        p.shade(-R - 2, CY - R - 2, R + 2, CY + R + 2, [&](double x, double y, int sx, int sy) {
            const double dx = x, dy = y - CY, d = std::sqrt(dx * dx + dy * dy) / R;
            if (d > 1.1) return;
            if (d > 1) {
                p.put(sx, sy, C[2]);
                return;
            }
            if (d > .9) {
                p.put(sx, sy, dy < -R * .3 && dx < R * .3 ? C[6] : C[4]);
                return;
            }
            const double a    = std::atan2(dy, dx),
                         n    = .5 + .5 * tileFbm((a / TAU) * 6 + ph * 6 + d * 1.5, d * 2.2 - ph * 4, 6, 4, seed, 2);
            const bool   band = std::abs(n - .5) < .05 + .05 * pulse;
            const double heat = std::pow(1 - d, 2) + pulse * .45;
            p.put(sx, sy,
                  band         ? (heat > .5   ? WHITE
                                  : heat > .2 ? C[6]
                                              : C[5])
                  : heat > .7  ? C[6]
                  : heat > .42 ? C[5]
                  : heat > .2  ? C[3]
                  : n > .5     ? C[2]
                               : C[1]);
        });
        const auto f = jsUint(std::floor(t * 15));
        Rng        r(hash({seed, f, 5}));
        for (int i = 0; i < 3; ++i) {
            const double a   = (double(i) / 3) * TAU + f * .7 + r.range(-.4, .4);
            const auto   pts = bolt(r, 0, CY, std::cos(a) * R * .88, CY + std::sin(a) * R * .88, .32, 3);
            p.polyline(pts, [&](double u) { return u < .4 ? WHITE : C[6]; });
            const auto [ex, ey] = pts.back();
            p.dot(ex, ey, WHITE);
            p.glow(ex, ey, 4, C[5], .4);
        }
        p.circle(0, CY, 1.8 + pulse * 2, WHITE);
        p.circle(0, CY, 3 + pulse * 2, [&](double d) { return d > .6 ? C[6] : WHITE; }, .0001);
        p.dot(-R * .45, CY - R * .55, WHITE);
        p.dot(-R * .45 + 1, CY - R * .55, C[6]);
    }
    static void ring(Paint& p, double t, const Ring& rg, size_t index, std::span<const uint32_t> C, bool front,
                     double pulse) {
        const double spin = index == 0 ? TAU * (t / D) : -TAU * 2 * (t / D) + 1, bead = (t / D) * TAU * 3;
        const int    n = int(std::ceil(rg.r * 9));
        for (int i = 0; i < n; ++i) {
            const double a       = (double(i) / n) * TAU;
            const auto [x, y, z] = ringPoint(rg.r, rg.tilt, spin, a);
            if ((z >= 0) != front) continue;
            const double u    = wrap((a - bead) / TAU, 1);
            const bool   head = u > .9;
            p.dot(x, y, head ? WHITE : !front ? C[3] : z > .5 * rg.r || pulse > .6 ? C[6] : C[5]);
        }
        if (front) {
            const auto point = ringPoint(rg.r, rg.tilt, spin, bead);
            p.glow(point.x, point.y, 6, C[5], .5);
        }
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C     = o.palette;
        const auto&  H     = o.scifi.metal;
        const double pulse = pulseAt(t);
        hoop(p, BOTY, RC, H, C, false, pulse);
        hoop(p, TOPY, RC, H, C, false, pulse);
        for (const auto& b : bars)
            if (b.z < 0) barDraw(p, b, C, H, pulse, true);
        for (size_t i = 0; i < RINGS.size(); ++i) ring(p, t, RINGS[i], i, C, false, pulse);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C     = o.palette;
        const auto&  H     = o.scifi.metal;
        const double pulse = pulseAt(t), R = R0 * (1 + .14 * pulse);
        plinth(p, C, H, t, pulse);
        core(p, t, C, R, pulse, o.seed);
        for (size_t i = 0; i < RINGS.size(); ++i) ring(p, t, RINGS[i], i, C, true, pulse);
        for (const auto& b : bars)
            if (b.z >= 0) barDraw(p, b, C, H, pulse, false);
        hoop(p, BOTY, RC, H, C, true, pulse);
        hoop(p, TOPY, RC, H, C, true, pulse);
        p.shade(-15, TOPY - 7, 15, TOPY, [&](double x, double y, int sx, int sy) {
            const double d = std::pow(x / 14.5, 2) + std::pow((y - TOPY) / 6.5, 2);
            if (d > 1) return;
            const double l = -x * .05 - (y - TOPY) * .12;
            p.put(sx, sy, d > .8 ? H[0] : l > .55 ? H[5] : l > .3 ? H[4] : H[3]);
        });
        p.dot(0, TOPY - 7, pulse > .4 ? WHITE : C[5]);
        p.glow(0, TOPY - 7, 5, C[4], .4 + .4 * pulse);
        const int    slot = int(std::floor(t / SLOT)) % NS;
        const double k    = 1 - (t - std::floor(t / SLOT) * SLOT) / SLOT;
        for (const auto& a : arcs)
            if (a.slot == slot) {
                scifi::strokeArc(p, a.path, {C[2], C[5], C[4]}, k);
                for (const auto& f : a.branches) scifi::strokeArc(p, f, {C[2], C[5], C[4]}, k, .6);
                const auto [ex, ey] = a.path.back();
                p.glow(ex, ey, 8, C[5], .6 * k);
            }
        for (const auto& b : BEATS) {
            const double a = scifi::since(t, b.time, D);
            if (!b.strong || a > .45) continue;
            const double q = a / .45, r = R0 + 34 * ease::outCubic(q);
            p.ring(0, CY, r, std::max(1., 3 * (1 - q)), q < .25 ? WHITE : q < .5 ? C[6] : C[5], 1.1 - q);
            p.glowRing(0, CY, r, 6, C[4], .35 * (1 - q));
        }
        ghosts.evaluate(t).draw(p);
        p.glow(0, CY, 46, C[4], .2 + .3 * pulse);
        p.glow(0, CY, 9, WHITE, .35 + .4 * pulse);
    }
};
}  // namespace
const Descriptor& energyCoreDescriptor() {
    static const std::array<Option, 1> options{{{"metal", 1, 64, false, 6, &PixelFxScifiSettings::metal}}};
    static const Descriptor            d{{PixelFxKind::EnergyCore, "scifi/energy-core", 112, 100, 56, 88, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed        = 35;
                                  s.palette     = {hex(0x1a0516), hex(0x3d0a33), hex(0x701560), hex(0xb0228f),
                                                   hex(0xf53cd1), hex(0xff86e6), hex(0xffd2f6), WHITE};
                                  s.scifi.metal = {hex(0x0e0d14), hex(0x1e1c27), hex(0x312e3e),
                                                   hex(0x4b4759), hex(0x6f6a82), hex(0x9d98b0)};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<EnergyCore>(s);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
