// eve-hot-path
// Native adaptation of pixelfx lightning/storm-cloud.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Lightning.h"
#include "stylize/pixelfx/PixelFxInternal.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double BASE = -100, TOP = -156, TH = .35;
const double     VIS = std::sqrt(1 - std::sqrt(TH));
constexpr int    W0 = -84, H0 = -162, GW = 168, GH = 70;
struct Def {
    double x, y, v, sq = 1;
};
constexpr std::array<Def, 17> DEFS{{{-6, -132, 17},
                                    {18, -127, 14},
                                    {-28, -122, 12},
                                    {38, -118, 10},
                                    {0, -142, 12},
                                    {14, -149, 19, .38},
                                    {40, -148, 14, .34},
                                    {-48, -113, 10},
                                    {55, -110, 9},
                                    {-17, -115, 13},
                                    {22, -113, 12},
                                    {-42, -104, 11},
                                    {-10, -104, 12},
                                    {20, -104, 11},
                                    {48, -103, 9},
                                    {-63, -104, 7},
                                    {66, -103, 6}}};
struct Flash {
    uint32_t frame;
    size_t   billow;
    double   k;
};
constexpr std::array<Flash, 8> FLASHES{
    {{7, 2, .8}, {8, 9, .55}, {29, 1, .75}, {31, 10, 1}, {33, 3, .5}, {70, 0, .8}, {71, 4, .55}, {72, 5, .35}}};
constexpr std::array<double, 7> BK{{1, .7, .3, .95, .5, .75, .2}};
struct Billow {
    double sq, x, y, r;
};
struct Cell {
    float field = 0, gx = 0, gy = 0, lit = 0;
};
std::array<Billow, 17> billows(uint32_t seed, double ph) {
    std::array<Billow, 17> out{};
    for (uint32_t i = 0; i < DEFS.size(); ++i) {
        const auto&  b    = DEFS[i];
        const bool   top  = b.y < -112 && b.sq == 1;
        const double boil = .82 + .3 * std::pow(std::sin(PI * std::fmod(ph + i * .37, 1)), 2);
        out[i] = {b.sq, b.x + 3.5 * loopNoise(ph, .7, seed + i) + (b.sq < 1 ? 4 * loopNoise(ph, .5, seed + i + 90) : 0),
                  b.y + 2 * loopNoise(ph, .7, seed + i + 30) - (top ? 4 * (boil - .97) : 0),
                  (b.v * (top ? boil : 1 + .08 * loopNoise(ph, .6, seed + i + 60))) / VIS};
    }
    return out;
}
void cloud(Paint& p, const std::array<Billow, 17>& B, double ph, uint32_t seed, std::span<const uint32_t> K,
           std::span<const uint32_t> C, const std::array<double, 17>& flashes) {
    std::vector<Cell> grid(size_t(GW * GH));
    for (size_t i = 0; i < B.size(); ++i) {
        const auto&  b = B[i];
        const double k = flashes[i], r2 = b.r * b.r, ry = b.r * b.sq;
        for (int y = std::max(0, int(std::floor(b.y - ry - H0))); y < std::min(GH, int(std::ceil(b.y + ry - H0))); ++y)
            for (int x = std::max(0, int(std::floor(b.x - b.r - W0))); x < std::min(GW, int(std::ceil(b.x + b.r - W0)));
                 ++x) {
                const double dx = x + W0 + .5 - b.x, dy = (y + H0 + .5 - b.y) / b.sq, d2 = (dx * dx + dy * dy) / r2;
                if (d2 >= 1) continue;
                auto&        v = grid[size_t(y * GW + x)];
                const double u = 1 - d2, c = u * u;
                v.field = float(double(v.field) + c);
                v.gx    = float(double(v.gx) + (-4 * u * dx) / r2);
                v.gy    = float(double(v.gy) + (-4 * u * dy) / (r2 * b.sq));
                if (k != 0) v.lit = float(double(v.lit) + k * (.25 + .75 * c) * (d2 < 1 ? 1 : 0));
            }
    }
    std::vector<uint32_t> ramp(K.begin(), K.end());
    ramp.insert(ramp.end(), {C[3], C[4], C[5], C[6]});
    for (int y = 0; y < GH; ++y) {
        const double cy = y + H0 + .5;
        for (int x = 0; x < GW; ++x) {
            const auto& v = grid[size_t(y * GW + x)];
            if (v.field < TH - .06) continue;
            const double cx = x + W0 + .5, base = BASE + 1.5 * tileNoise(cx * .12 + 9 * ph, 3, 9, 0, seed + 5),
                         F = double(v.field) * (1 - smooth(base - 3, base, cy)) +
                             .05 * tileNoise(cx * .21 + 18 * ph, cy * .21, 18, 0, seed);
            if (F < TH) continue;
            const double raw = jsHypot2(double(v.gx), double(v.gy)), gl = raw == 0 ? 1 : raw,
                         lam  = -(double(v.gx) * .42 + double(v.gy) * .9) / gl,
                         edge = std::min(1., gl * 5) * (F < TH + .08 ? 1 : .55),
                         h    = 1 - 2 * clamp((cy - TOP) / (BASE - TOP));
            double i = 2.7 + 2.2 * h + 1.3 * lam * edge + .4 * tileNoise(cx * .16 + 12 * ph, cy * .16, 12, 0, seed + 9);
            if (F < TH + .05 && lam > .3 && h > -.5) i += 1;
            if (cy > base - 6) i -= 1;
            i              = clamp(jsRound(i), 0, double(K.size() - 1));
            const double L = double(v.lit) * (.7 + .6 * tileNoise(cx * .2 - 14 * ph, cy * .2, 14, 0, seed + 3));
            if (L > .12) i = std::min(double(ramp.size() - 1), i + jsRound(L * 5.5));
            p.px(cx, cy, ramp[size_t(i)]);
        }
    }
}
void scud(Paint& p, double ph, uint32_t seed, std::span<const uint32_t> K) {
    Rng r(hash({seed, 13}));
    for (uint32_t w = 0; w < 6; ++w) {
        const double x0 = r.range(-60, 60), y0 = BASE + r.range(3, 9), rx = r.range(10, 20), ry = r.range(1.3, 2.2),
                     off = r.next(), u = std::fmod(ph + off, 1), a = std::pow(std::sin(PI * u), .8),
                     xc = x0 + 16 * (u - .5);
        for (double y = std::floor(y0 - ry - 1); y <= y0 + ry + 1; ++y)
            for (double x = std::floor(xc - rx - 1); x <= xc + rx + 1; ++x) {
                const double dx = (x + .5 - xc) / rx, dy = (y + .5 - y0) / ry,
                             nz = tileNoise((x + .5) * .35 + 10 * ph + w * 7, (y + .5) * .5, 10, 0, seed + 20 + w),
                             v  = (1 - dx * dx - dy * dy) * a + .6 * nz - .25;
                if (v > 0) p.blend(x + .5, y + .5, v > .4 ? K[2] : K[3], std::min(.6, .2 + v * .7));
            }
    }
}
void draw(Paint& p, double t, const PixelFxSettings& o) {
    const auto&            C  = o.palette;
    const auto&            K  = o.lightning.cloud;
    const auto&            RN = o.lightning.rain;
    const auto&            S  = o.lightning.sparks;
    const uint32_t         f  = jsUint(wrap(std::floor(t * 30 + 1e-6), 90));
    const double           ph = f / 90.;
    const auto             B  = billows(o.seed, ph);
    Rng                    br(hash({o.seed, 5, 52}));
    const int              ba  = int(f) - 52;
    const double           bx0 = DEFS[12].x + br.range(-4, 4), bx1 = br.range(-24, 24);
    std::array<double, 17> flashes{};
    for (const auto& a : FLASHES)
        if (a.frame == f) flashes[a.billow] = a.k;
    if (ba >= 0 && ba < int(BK.size())) flashes[12] = std::max(flashes[12], BK[size_t(ba)]);
    double flash = 0;
    for (double k : flashes) flash = std::max(flash, k);
    cycle(t, 3, 150, o.seed + 11, .5, .62, [&](double age, double, Rng& r, int) {
        const double x0 = r.range(-62, 62), vy = r.range(190, 240), vx = -vy * .18, tf = (0 - (BASE + 4)) / vy;
        if (age < tf) {
            const double x = x0 + vx * age, y = BASE + 4 + vy * age, l = r.range(4, 6.5);
            p.lineFn(
                x - vx * .018 * (l / 4), y - l, x, y,
                [&](double u) {
                    return u > .7 ? (flash > .5 ? C[5] : RN[3]) : flash > .5 ? C[3] : RN[u > .35 ? 2 : 1];
                },
                1);
        } else if (age < tf + .12) {
            const double x = x0 + vx * tf, a = (age - tf) / .12;
            for (int s : {-1, 1}) p.px(x + s * (1 + a * 2.5), -1 - std::sin(a * PI) * 2.5, a < .5 ? RN[3] : RN[2]);
            if (a < .4) p.px(x, 0, RN[3]);
        }
    });
    scud(p, ph, o.seed, K);
    cloud(p, B, ph, o.seed, K, C, flashes);
    for (size_t i = 0; i < B.size(); ++i)
        if (flashes[i] != 0) p.glow(B[i].x, B[i].y, B[i].r * VIS * (B[i].sq < 1 ? 1.6 : 2.2), C[4], .4 * flashes[i]);
    p.glow(0, -122, 80, C[2], .05 + .22 * flash);
    for (const auto& a : FLASHES)
        if (a.frame == f && a.k > .6) {
            Rng          r(hash({o.seed, 7, a.frame}));
            const double x = B[a.billow].x, x0 = x - r.range(12, 22), y0 = BASE - r.range(2, 6),
                         x1 = x + r.range(12, 22), y1 = BASE - r.range(1, 5);
            const auto   b = lightning::zigzag(r, x0, y0, x1, y1, .25, .5);
            lightning::crackle(p, b, .6, C, .4);
        }
    if (ba >= 0 && ba < int(BK.size())) {
        const double      k = BK[size_t(ba)];
        Rng               r(hash({o.seed, 9, 52}));
        const auto        b = lightning::zigzag(r, bx0, BASE - 3, bx1, 0, .22, .7);
        lightning::Stroke options;
        options.hero = ba == 0;
        lightning::stroke(p, b, k, C, ba == 0 ? 1.15 : 1, options);
        p.circle(bx1, -1, 3 * k + 1.5, C[2]);
        p.circle(bx1, -1, 3 * k + .5, C[5]);
        p.circle(bx1, -1, 2 * k, WHITE);
        p.glow(bx1, -22, 78, C[3], .5 * k);
        p.glow(bx1, -3, 18, C[5], .45 * k);
        p.glow(bx0, BASE, 84, C[4], .3 * k);
    }
    const double sa = wrap(t - 52. / 30, 3);
    for (uint32_t j = 0; j < 16; ++j) {
        Rng          q(hash({o.seed, 31, j}));
        const double life = q.range(.25, .6);
        if (sa >= life) continue;
        const double a = -PI / 2 + q.range(-1.3, 1.3), sp = q.range(60, 200);
        lightning::spark(p, lightning::flight(bx1, -1, std::cos(a) * sp, std::sin(a) * sp, sa, 0), 1 - sa / life, S);
    }
}
}  // namespace
const Descriptor& stormCloudDescriptor() {
    static const std::array<Option, 3> options{{{"sparks", 1, 64, false, 4, &PixelFxLightningSettings::sparks},
                                                {"cloud", 1, 64, false, 7, &PixelFxLightningSettings::cloud},
                                                {"rain", 1, 64, false, 4, &PixelFxLightningSettings::rain}}};
    static const Descriptor            d{{PixelFxKind::StormCloud, "lightning/storm-cloud", 168, 176, 84, 164, 3, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed = 8;
                                  s.palette.assign(lightning::VOLT.begin(), lightning::VOLT.end());
                                  s.lightning.sparks.assign(lightning::PLASMA.begin(), lightning::PLASMA.end());
                                  s.lightning.cloud = {hex(0x09070f), hex(0x120f1e), hex(0x1b182e), hex(0x252640),
                                                       hex(0x323758), hex(0x464d78), hex(0x646b9c)};
                                  s.lightning.rain  = {hex(0x2b3250), hex(0x46527a), hex(0x6d7cab), hex(0xa5b5e0)};
                                         },
                                         draw,
                                         options};
    return d;
}
}  // namespace eve::stylize::pixelfx
