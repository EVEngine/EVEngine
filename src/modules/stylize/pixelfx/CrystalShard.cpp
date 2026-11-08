// eve-hot-path
// Native adaptation of pixelfx pickups/crystal-shard.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/PickupGem.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 3.2, HALF = 6, TILT = .18, LEAN = .2, HOVER = 27, RX = 15, RY = 3.8;
using Vec = std::array<double, 3>;
Vec roll(Vec v) {
    return {v[0] * std::cos(LEAN) - v[1] * std::sin(LEAN), v[0] * std::sin(LEAN) + v[1] * std::cos(LEAN), v[2]};
}
Vec unit(double x, double y, double z) {
    const double l = jsHypot3(x, y, z);
    return {x / l, y / l, z / l};
}
pickup::GemSolid makeSolid() {
    pickup::GemSolid g{{}, 8, HALF + 13, HALF + 8};
    g.planes.reserve(22);
    constexpr std::array<double, 6> jit{0, 8, -6, 4, -9, 5}, rr{1, .86, 1.08, .94, .82, 1.04},
        low{6.5, 5.5, 7, 6, 5, 6.8};
    const double deg = PI / 180;
    for (size_t k = 0; k < 6; ++k) {
        const double a = (double(k) * 60 + jit[k]) * deg, r = 4.8 * rr[k], ct = std::atan2(13, r),
                     cb = std::atan2(low[k], r);
        g.planes.push_back({{std::cos(a), 0, std::sin(a)}, r, 6});
        g.planes.push_back({{std::sin(ct) * std::cos(a), std::cos(ct), std::sin(ct) * std::sin(a)},
                            std::sin(ct) * r + std::cos(ct) * HALF,
                            7});
        g.planes.push_back({{std::sin(cb) * std::cos(a), -std::cos(cb), std::sin(cb) * std::sin(a)},
                            std::sin(cb) * r + std::cos(cb) * HALF,
                            8});
    }
    const auto shear = unit(.7, .62, .34);
    g.planes.push_back({shear, shear[1] * 7.2 + shear[0] * -.6, 9});
    g.planes.push_back({unit(-.7, -.25, .65), 4, 10});
    g.planes.push_back({unit(.75, .15, .6), 3.9, 10});
    g.planes.push_back({unit(-.55, .5, .3), 6.8, 10});
    for (auto& q : g.planes) q.n = roll(q.n);
    return g;
}
constexpr std::array<Vec, 6> CRACK0{
    {{.5, -9, .3}, {-.8, -5, .5}, {.6, -1, .2}, {-.4, 3, .6}, {1.4, 6.5, .4}, {2.2, 8.5, .6}}};
constexpr std::array<Vec, 3> CRACK1{{{.6, -1, .2}, {2.4, 1.5, 1}, {3.4, 4.2, 1.2}}};
constexpr std::array<Vec, 4> CRACK2{{{-.4, 3, .6}, {-2, 6.5, .4}, {-2.6, 10.5, .2}, {-2.4, 14.5, .1}}};
constexpr std::array<Vec, 2> CRACK3{{{-.8, -5, .5}, {-2.8, -3, .8}}};
double                       climb(double ph) { return std::fmod(ph * 2, 1) * 1.7 - .3; }
Point                        project(Vec pt, double spin, double cy) {
    const auto   v  = roll(pt);
    const double xr = v[0] * std::cos(spin) - v[2] * std::sin(spin), zr = v[0] * std::sin(spin) + v[2] * std::cos(spin);
    return {xr, cy - (v[1] * std::cos(TILT) - zr * std::sin(TILT))};
}
void cracks(Paint& p, std::span<const uint32_t> C, const pickup::Layer<32, 52>& L, double spin, double cy, double ph) {
    const double s0  = climb(ph);
    const auto   rim = C[0];
    for (const auto crack : std::array<std::span<const Vec>, 4>{CRACK0, CRACK1, CRACK2, CRACK3})
        for (size_t i = 1; i < crack.size(); ++i) {
            const auto a = project(crack[i - 1], spin, cy), b = project(crack[i], spin, cy);
            const int  n = int(std::ceil(jsHypot2(b.x - a.x, b.y - a.y) * 2));
            for (int j = 0; j <= n; ++j) {
                const double u = double(j) / n, x = a.x + (b.x - a.x) * u, y = a.y + (b.y - a.y) * u,
                             h = crack[i - 1][1] + (crack[i][1] - crack[i - 1][1]) * u;
                const auto   c = L.get(x, y);
                if (!c || c == rim) continue;
                const double v = ((h + 9) / 22 - s0) / .14, brightness = .3 + std::exp(-(v * v));
                p.px(x, y, brightness > .9 ? WHITE : brightness > .55 ? C[6] : C[5]);
                if (j % 4 == 0 && brightness > .5) p.glow(x + .5, y + .5, 2 + 3 * brightness, C[5], .16 * brightness);
            }
        }
}
void splinter(Paint& p, std::span<const uint32_t> C, double x, double y, double len, double angle, bool far) {
    const double          ca = std::cos(angle), sa = std::sin(angle), X = jsRound(x), Y = jsRound(y);
    pickup::Layer<16, 16> L;
    L.begin(X - 7, Y - 7);
    L.fill(X - 7, Y - 7, X + 6, Y + 6, [&](double px, double py) -> uint32_t {
        const double dx = px - X, dy = py - Y, u = dx * ca + dy * sa, v = -dx * sa + dy * ca;
        if (std::abs(u) / len + std::abs(v) / (u > 0 ? 1.7 : 1.1) > 1) return 0;
        const double lit = -(ca * .6 + sa * .8) * (v == 0 ? 1 : v < 0 ? -1 : 1);
        return far ? (v < 0 ? C[4] : C[3]) : std::abs(v) < .5 && u < 0 ? WHITE : ((v < 0) == (lit > 0)) ? C[6] : C[4];
    });
    L.outline(C[far ? 1 : 0]);
    L.draw(p);
}
void splinters(Paint& p, std::span<const uint32_t> C, double ph, double cy, bool back) {
    for (int i = 0; i < 3; ++i) {
        const double a = ph * TAU + i * TAU / 3, s = std::sin(a);
        if ((s < 0) != back) continue;
        const double x = std::cos(a) * RX, y = cy + 1 + s * RY - std::cos(a) * 1.5, near = (s + 1) / 2;
        splinter(p, C, x, y, 3 + 1.6 * near, a * 2 + i * 1.7, back);
        p.glow(x, y, 6 + 3 * near, C[5], (back ? .18 : .32) * (.6 + .4 * near));
        if (!back && s > .92) pickup::glint(p, x - 1, y - 1, 2.5 * (s - .92) / .08, WHITE, .4);
    }
}
struct CrystalShard final : AnalyticKernel {
    const pickup::GemSolid mesh = makeSolid();
    void                   drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C  = o.palette;
        const double ph = t / D, y = -HOVER - 1.8 * std::sin(ph * TAU);
        pickup::blobShadow(p, 0, 6, .75);
        pickup::pool(p, 0, .5, 20, 3.5, C[4], .3);
        p.glowRing(0, y, 7, 20, C[4], .22);
        splinters(p, C, ph, y, true);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&           C  = o.palette;
        const double          ph = t / D, y = -HOVER - 1.8 * std::sin(ph * TAU), spin = .3 + .62 * std::sin(ph * TAU),
                              s0 = climb(ph);
        pickup::Layer<32, 52> L;
        [[maybe_unused]] const auto hi = pickup::drawGem(L, p, mesh, 0, y, spin, C, {TILT, false, 1, true, .6});
        cracks(p, C, L, spin, y, ph);
        const double v = (s0 - .45) / .45, inside = std::exp(-(v * v));
        p.glow(0, y, 10, C[5], .2 * inside);
        splinters(p, C, ph, y, false);
        const auto   f   = project({1.8, 8.4, .9}, spin, y);
        const double out = clamp(1 - std::abs(s0 - 1.05) / .18);
        if (out > 0) {
            pickup::glint(p, f.x, f.y, 3.5 * out, WHITE, .55 * out);
            p.glow(f.x, f.y, 12, C[5], .4 * out);
        }
        constexpr double P2 = D / 2, burst = (1.05 + .3) / 1.7 * P2;
        for (uint32_t i = 0; i < 7; ++i) {
            const double born = burst + rand({i, o.seed, 1}) * .12, at = wrap(t - born, P2),
                         life = .6 + rand({i, o.seed, 2}) * .45;
            if (at > life) continue;
            const double k  = 1 - at / life,
                         x  = f.x + (rand({i, o.seed, 3}) - .5) * 4 + std::sin(at * 5 + i) * 2 * (at / life),
                         yy = f.y - at * (14 + rand({i, o.seed, 4}) * 14);
            p.px(x, yy, k > .6 ? WHITE : k > .3 ? C[6] : C[5]);
            p.glow(x + .5, yy + .5, 3, C[5], .35 * k);
        }
    }
};
}  // namespace
const Descriptor& crystalShardDescriptor() {
    static const Descriptor d{{PixelFxKind::CrystalShard, "pickups/crystal-shard", 64, 76, 32, 68, D, true},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed    = 8;
                                  s.palette = {hex(0x1a0a04), hex(0x3e1606), hex(0x702a08), hex(0xa8480c),
                                               hex(0xe07818), hex(0xffa838), hex(0xffd890), WHITE};
                              },
                              nullptr,
                              {},
                              nullptr,
                              nullptr,
                              [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<CrystalShard>();
                              }};
    return d;
}
}  // namespace eve::stylize::pixelfx
