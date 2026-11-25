// eve-hot-path
// Native adaptation of pixelfx scifi/laser-grid.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScifiGhosts.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                               D = 3, PX = 48, TOP = -64, IN = PX - 4;
constexpr std::array<std::array<double, 4>, 3> SLIDE{{{-14, 8, 1, 0}, {-31, 12, 2, 1.3}, {-48, 9, 1, 3.6}}};
using Beam = std::array<double, 4>;
double sweepX(double t, double s) { return s * (34 - 68 * (.5 - .5 * std::cos(TAU * (t / D) + (s > 0 ? 0 : 1.9)))); }
double slideY(double t, const std::array<double, 4>& b) { return b[0] + b[1] * std::sin(TAU * b[2] * (t / D) + b[3]); }
std::array<Beam, 5> beams(double t) {
    std::array<Beam, 5> out{};
    for (size_t i = 0; i < 3; ++i) {
        const double y = slideY(t, SLIDE[i]);
        out[i]         = {-IN, y, IN, y};
    }
    size_t i = 3;
    for (double s : {-1., 1.}) out[i++] = {s * IN, TOP + 6, sweepX(t, s), 0};
    return out;
}
std::optional<Point> cross(const Beam& a, const Beam& b) {
    const auto [x1, y1, x2, y2] = a;
    const auto [x3, y3, x4, y4] = b;
    const double d              = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::abs(d) < 1e-6) return std::nullopt;
    const double u = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / d,
                 v = -((x1 - x2) * (y1 - y3) - (y1 - y2) * (x1 - x3)) / d;
    if (u >= 0 && u <= 1 && v >= 0 && v <= 1) return Point{x1 + u * (x2 - x1), y1 + u * (y2 - y1)};
    return std::nullopt;
}
double near(const Beam& b, double x, double y) {
    const auto [x0, y0, x1, y1] = b;
    const double dx = x1 - x0, dy = y1 - y0, u = clamp(((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy)),
                 ex = x0 + dx * u - x, ey = y0 + dy * u - y;
    return std::sqrt(ex * ex + ey * ey);
}
struct LaserGrid final : AnalyticKernel {
    scifi::Ghosts ghosts{D, 0, std::array<double, 4>{-62, -80, 62, 7}};
    explicit LaserGrid(const PixelFxSettings& o) {
        const auto& C = o.palette;
        ghosts.sparks = {WHITE, C[6], C[4], C[3]};
        Rng rn(o.seed);
        for (int i = 0; i < 36; ++i) {
            const double tb = (double(i) / 36) * D + rn.range(0, .05), s = i % 2 ? 1 : -1, x = sweepX(tb, s),
                         change = sweepX(tb + .02, s) - x,
                         dir    = change > 0   ? 1
                                  : change < 0 ? -1
                                               : 1,
                         a      = -PI / 2 - dir * rn.range(.2, 1.1);
            const double vx = std::cos(a) * rn.range(50, 140), vy = std::sin(a) * rn.range(50, 140),
                         life = rn.range(.2, .45);
            ghosts.add(tb, ParticleKind::Spark, x, -1, vx, vy, life);
            if (i % 4 == 0) {
                const double vxSmoke = rn.range(-4, 4), vySmoke = -rn.range(6, 14), lifeSmoke = rn.range(.8, 1.2),
                             size = rn.range(1.6, 2.4);
                ghosts.add(tb, ParticleKind::Smoke, x, -2, vxSmoke, vySmoke, lifeSmoke, hex(0x3a2a30), size);
            }
        }
    }
    static void laser(Paint& p, const Beam& b, std::span<const uint32_t> C, double t, uint32_t seed) {
        const auto [x0, y0, x1, y1] = b;
        const bool   steep          = std::abs(y1 - y0) > std::abs(x1 - x0);
        const double ox = steep ? 1 : 0, oy = steep ? 0 : 1;
        for (double s : {-1., 1.}) p.line(x0 + s * ox, y0 + s * oy, x1 + s * ox, y1 + s * oy, s < 0 ? C[4] : C[3]);
        p.lineFn(x0, y0, x1, y1, [&](double u) { return wrap(u * 5 - t * 3, 1) < .12 ? C[6] : WHITE; }, 1.);
        const double L = jsHypot2(x1 - x0, y1 - y0);
        for (double d = 0; d <= L; d += 5) {
            const double x = lerp(x0, x1, d / L), y = lerp(y0, y1, d / L),
                         h = .5 + .5 * tileFbm(x / 14 + (t / D) * 4, y / 10, 4, 0, seed);
            p.glow(x, y, 6 + 4 * h, C[3], .14 + .3 * h * h);
        }
    }
    static void post(Paint& p, double s, std::span<const uint32_t> H, std::span<const uint32_t> C, double t) {
        const double          x0 = s * PX - 4;
        std::array<double, 3> B{};
        for (size_t i = 0; i < B.size(); ++i) B[i] = slideY(t, SLIDE[i]);
        for (int y = int(TOP) + 4; y < 0; ++y) {
            double glint = 0;
            for (double by : B) glint = std::max(glint, std::exp(-std::pow((y - by) / 2.5, 2)));
            for (int i = 0; i < 8; ++i) {
                const bool inner = s < 0 ? i >= 6 : i <= 1;
                uint32_t   c     = i == 0 || i == 7 ? H[0]
                                   : i == 1         ? H[4]
                                   : i == 2         ? H[3]
                                   : i == 4         ? H[1]
                                   : i == 6         ? H[2]
                                                    : H[3];
                if (inner && i != 0 && i != 7 && glint > .35) c = glint > .75 ? C[5] : C[3];
                p.dot(x0 + i, y, c);
            }
        }
        for (int y = int(TOP) + 12; y < -4; y += 9) p.dot(x0 + 3, y, H[5]);
        for (int y = int(TOP); y < TOP + 10; ++y)
            for (int i = -1; i < 9; ++i)
                p.dot(x0 + i, y,
                      i == -1 || i == 8 || y == TOP || y == TOP + 9 ? H[0]
                      : y == TOP + 1                                ? H[5]
                      : i < 3                                       ? H[4]
                                                                    : H[2]);
        p.dot(s * IN, TOP + 6, WHITE);
        p.dot(s * IN, TOP + 5, C[5]);
        p.dot(s * IN, TOP + 7, C[4]);
        for (int i = -3; i < 11; ++i) {
            p.dot(x0 + i, -2, i == -3 || i == 10 ? H[0] : H[4]);
            p.dot(x0 + i, -1, i == -3 || i == 10 ? H[0] : H[2]);
            p.dot(x0 + i, 0, H[1]);
        }
        for (double by : B) {
            const double y = jsRound(by);
            for (int j = -2; j <= 2; ++j)
                for (int i = 0; i < 4; ++i)
                    p.dot(s < 0 ? x0 + 5 + i : x0 - 1 + i, y + j,
                          std::abs(j) == 2 || i == (s < 0 ? 3 : 0) ? H[0]
                          : j < 0                                  ? H[5]
                                                                   : H[3]);
            p.dot(s * IN, y, WHITE);
            p.glow(s * IN, y, 7, C[4], .55);
        }
        p.glow(s * IN, TOP + 6, 8, C[4], .55);
    }
    void drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&    C = o.palette;
        const auto&    H = o.scifi.metal;
        const auto     B = beams(t);
        const uint32_t S = o.seed;
        for (double s : {-1., 1.})
            for (int k = 0; k < 14; ++k) {
                const double   ago = k * .025, x = sweepX(t - ago, s);
                const uint32_t c = k < 2 ? WHITE : k < 5 ? C[6] : k < 9 ? C[4] : C[3];
                p.dot(x, 0, c, 1 - double(k) / 14);
                p.dot(x, 1, C[3], .6 - double(k) / 24);
            }
        for (int x = -40; x <= 40; x += 20) p.glow(x, -2, 20, C[3], .12);
        const double ph = t / D;
        for (int y = -60; y < 0; ++y)
            for (int x = -int(IN) + 1; x < IN; ++x) {
                double d = 99;
                for (const auto& b : B) d = std::min(d, near(b, x + .5, y + .5));
                if (d > 11) continue;
                const double low = .35 + .65 * clamp((y + 60.) / 50),
                             n = .5 + .5 * tileFbm((x + .5) / 18 + ph * 3, (y + .5) / 7 + .4 * std::sin(TAU * ph), 3, 0,
                                                   S + 7, 2),
                             dens = clamp((n - .42) * 2.2) * low;
                if (dens <= .02) continue;
                const double lit = std::exp(-d / 4.5);
                p.blend(x + .5, y + .5, lit > .5 ? C[4] : lit > .15 ? C[3] : C[1], dens * .8 * lit);
            }
        for (double s : {-1., 1.}) post(p, s, H, C, t);
        for (const auto& b : B) laser(p, b, C, t, S);
        for (uint32_t i = 0; i < 90; ++i) {
            const double x = (rand({S, i}) * 2 - 1) * (IN - 3) + 4 * std::sin(TAU * (t / D) + i),
                         y = -2 - rand({S, i, 1}) * 62 + 3 * std::sin(TAU * 2 * (t / D) + i * 1.7);
            double       d = 9;
            for (const auto& b : B) d = std::min(d, near(b, x, y));
            if (d < 1.6) {
                p.dot(x, y, d < .9 ? WHITE : C[6]);
                p.glow(x, y, 3, C[5], .45);
            }
        }
        for (size_t i = 0; i < B.size(); ++i)
            for (size_t j = i + 1; j < B.size(); ++j)
                if (auto c = cross(B[i], B[j])) {
                    p.glow(c->x, c->y, 9, C[5], .4);
                    p.sparkle(c->x, c->y, 2, C[6], C[5], .3);
                }
        for (double s : {-1., 1.}) {
            const double x = sweepX(t, s);
            p.dot(x, -1, WHITE);
            p.dot(x - 1, 0, C[6]);
            p.dot(x + 1, 0, C[6]);
            p.glow(x, -1, 10, C[4], .55);
            p.glow(x, -1, 4, WHITE, .35);
        }
        ghosts.evaluate(t).draw(p);
    }
};
}  // namespace
const Descriptor& laserGridDescriptor() {
    static const std::array<Option, 1> options{{{"metal", 1, 64, false, 6, &PixelFxScifiSettings::metal}}};
    static const Descriptor            d{{PixelFxKind::LaserGrid, "scifi/laser-grid", 128, 84, 64, 76, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed        = 31;
                                  s.palette     = {hex(0x1a0408), hex(0x4a0a12), hex(0x8a1020), hex(0xd01c2a),
                                                   hex(0xff4a4a), hex(0xff9a90), hex(0xffe4e0), WHITE};
                                  s.scifi.metal = {hex(0x0e0d14), hex(0x1e1c27), hex(0x312e3e),
                                                   hex(0x4b4759), hex(0x6f6a82), hex(0x9d98b0)};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<LaserGrid>(s);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
