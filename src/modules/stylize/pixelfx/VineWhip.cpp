// eve-hot-path
// Native adaptation of pixelfx nature/vine-whip.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/NatureGeometry.h"
#include "stylize/pixelfx/NatureLeafGeometry.h"
#include "stylize/pixelfx/NatureSimulation.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr size_t N  = 24;
constexpr double LS = 3.3, LASH = .36, LAG = .0052, TAU_S = .055, OMEGA = TAU / .21, D = 1.4;
const double     CRACK = LASH + LAG * N + (PI - std::atan(OMEGA * TAU_S)) / OMEGA;
using VinePath         = std::array<Point, N + 1>;
double spring(double u) {
    return u <= 0 ? 0 : 1 - std::exp(-u / TAU_S) * (std::cos(OMEGA * u) + std::sin(OMEGA * u) / (OMEGA * TAU_S));
}
VinePath pose(double t) {
    const double grow = ease::outCubic(clamp(t / .14)) * (1 - ease::inCubic(clamp((t - 1.) / .3))),
                 rear = smooth(.1, LASH, t), relax = smooth(.82, 1, t);
    VinePath     pts;
    pts[0] = {0, (1 - grow) * (N * LS + 6)};
    for (size_t i = 0; i < N; ++i) {
        const double s = double(i) / N, stand = -PI / 2 + .25 * std::sin(s * 3 + t * 7) * s,
                     back = -PI / 2 - .3 - 1.65 * std::pow(s, 1.3), out = -1. + .9 * std::pow(s, .6);
        double       a = lerp(stand, back, rear);
        a              = lerp(a, out, spring(t - LASH - LAG * double(i)));
        a              = lerp(a, stand, relax);
        pts[i + 1]     = {pts[i].x + std::cos(a) * LS, pts[i].y + std::sin(a) * LS};
    }
    return pts;
}
void haze(Paint& p, std::span<const Point> pts, uint32_t c, double a) {
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (const auto v : pts) {
        x0 = std::min(x0, v.x);
        x1 = std::max(x1, v.x);
        y0 = std::min(y0, v.y);
        y1 = std::max(y1, v.y);
    }
    for (int y = int(std::floor(y0)); y <= std::ceil(y1); ++y)
        for (int x = int(std::floor(x0)); x <= std::ceil(x1); ++x) {
            bool inside = false;
            for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
                const auto A = pts[i], B = pts[j];
                if ((A.y > y + .5) != (B.y > y + .5) && x + .5 < ((B.x - A.x) * (y + .5 - A.y)) / (B.y - A.y) + A.x)
                    inside = !inside;
            }
            if (inside) p.blend(x, y, c, a);
        }
}
double width(double s) { return 1.2 + 4.8 * std::pow(1 - s, .7); }
void   vine(Paint& p, const VinePath& pts, std::span<const uint32_t> C) {
    for (size_t i = 0; i < N; ++i) {
        const auto   A = pts[i], B = pts[i + 1];
        const double dx = B.x - A.x, dy = B.y - A.y, raw = jsHypot2(dx, dy), L = raw == 0 ? 1 : raw, nx = -dy / L,
                     ny = dx / L;
        const double w0 = width(double(i) / N) / 2, w1 = width(double(i + 1) / N) / 2,
                     up = nx * -.6 + ny * -.8 > 0 ? 1. : -1.;
        for (int y = int(std::floor(std::min(A.y, B.y) - 4)); y <= std::ceil(std::max(A.y, B.y) + 4); ++y)
            for (int x = int(std::floor(std::min(A.x, B.x) - 4)); x <= std::ceil(std::max(A.x, B.x) + 4); ++x) {
                if (y > 0) continue;
                const double px = x + .5 - A.x, py = y + .5 - A.y, u = (px * dx + py * dy) / (L * L);
                if (u < -.15 || u > 1.15) continue;
                const double d = px * nx + py * ny, w = lerp(w0, w1, clamp(u)) + .35;
                if (std::abs(d) > w) continue;
                const double side = ((d > 0) - (d < 0)) * up, q = std::abs(d) / w, arc = (double(i) + u) * LS;
                uint32_t     c = q > .8     ? (side > 0 ? C[5] : C[1])
                                 : side > 0 ? (q > .35 ? C[4] : C[3])
                                 : q > .4   ? C[2]
                                            : C[3];
                if (w > 1.6 && std::abs(std::fmod(arc + d * .8, 5.) - 2.5) < .45 && q < .8) c = C[2];
                p.px(x, y, c);
            }
    }
}
void thorns(Paint& p, const VinePath& pts, std::span<const uint32_t> T, std::span<const uint32_t> C, double t) {
    for (size_t i = 2; i < N - 1; i += 2) {
        const auto   A = pts[i], B = pts[i + 1];
        const double dx = B.x - A.x, dy = B.y - A.y, raw = jsHypot2(dx, dy), L = raw == 0 ? 1 : raw,
                     side = (i / 2) % 2 ? 1. : -1.;
        const double nx = (-dy / L) * side, ny = (dx / L) * side, w = width(double(i) / N) / 2,
                     len = 1.8 + 2.4 * (1 - double(i) / N), tx0 = dx / L, ty0 = dy / L;
        const double x0 = A.x + nx * (w - .5), y0 = A.y + ny * (w - .5), tx = x0 + nx * len - tx0 * len * .7,
                     ty = y0 + ny * len - ty0 * len * .7;
        if (y0 > 0) continue;
        p.tri(x0 - tx0 * 1.6, y0 - ty0 * 1.6, x0 + tx0 * 1.4, y0 + ty0 * 1.4, tx, ty, T[1]);
        p.line(x0 + tx0 * .8, y0 + ty0 * .8, tx, ty, T[2]);
        p.px(tx, ty, T[3]);
    }
    for (size_t k : {size_t(7), size_t(13), size_t(18)}) {
        const auto   A = pts[k], B = pts[k + 1];
        const double a = std::atan2(B.y - A.y, B.x - A.x), side = k % 2 ? 1. : -1.;
        if (A.y > -2) continue;
        const double la = a + side * 1.1 + std::sin(t * 9 + double(k)) * .2, lx = A.x + std::cos(la) * 4,
                     ly = A.y + std::sin(la) * 4;
        const std::array<uint32_t, 4> pal{{C[1], C[3], C[5], C[6]}};
        nature::drawLeaf(p, lx, ly, 7, 2.2, la, .3 + std::sin(t * 5 + double(k)) * .4, pal);
    }
}
struct Chip {
    double             x, y, vx, vy, angle, angularVelocity, size;
    nature::StoneShape shape;
    double             life;
};
struct ChipEmission {
    double x, y, vx, vy, angle, angularVelocity, size;
};
struct Torn {
    double x, y, vx, vy, angle, angularVelocity, flip, flipVelocity, life, max;
};
struct Spot {
    double x, t0, width;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
struct VineWhip final : Kernel {
    const std::vector<uint32_t> C, T, S;
    Rng                         R;
    Particles                   ps;
    nature::Drops               drops;
    Lights                      lights;
    std::vector<Chip>           chips;
    std::vector<Torn>           torn;
    std::vector<Spot>           spots;
    double                      t     = 0;
    bool                        burst = false, cracked = false, sunk = false;
    explicit VineWhip(const PixelFxSettings& o) : C(o.palette), T(o.nature.thorn), S(o.nature.soil), R(o.seed) {
        ps.sparks = {C[7], C[6], C[5], C[4]};
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    void appendChip(const ChipEmission& e) {
        const auto   shape = nature::randomStoneShape(R);
        const double life  = std::min(R.range(.6, .9), D - .05 - t);
        chips.push_back({e.x, e.y, e.vx, e.vy, e.angle, e.angularVelocity, e.size, shape, life});
    }
    void dirt(int n, double sp) {
        for (int i = 0; i < n; ++i) {
            const double a = -PI / 2 + R.range(-1.2, 1.2), v = R.range(60, 200) * sp;
            appendChip({R.range(-3, 3), -1, std::cos(a) * v, std::sin(a) * v, R.range(0, TAU), R.range(-15, 15),
                        R.range(1.4, 2.6)});
        }
        for (int i = 0; i < n * 3; ++i) {
            const double a = -PI / 2 + R.range(-1, 1), v = R.range(50, 200) * sp;
            emit({ParticleKind::Grit, R.range(-3, 3), -1, std::cos(a) * v, std::sin(a) * v,
                  std::min(R.range(.5, .9), D - .05 - t), R.pick<uint32_t>(std::array{S[5], S[4], S[3]}),
                  R.isChance(.3) ? 2. : 1.});
        }
        for (int i = 0; i < n; ++i)
            emit({ParticleKind::Dust, R.range(-4, 4), -R.range(0, 3), R.signedRange(15, 60), -R.range(4, 20),
                  std::min(R.range(.4, .8), D - .05 - t), S[5], R.range(2, 4)});
    }
    void update(double dt) override {
        t += dt;
        if (!burst) {
            burst = true;
            dirt(6, 1);
        }
        if (!cracked && t >= CRACK) {
            cracked        = true;
            const auto   a = pose(t), b = pose(t - .01);
            const auto   tip = a[N];
            const double vx = (tip.x - b[N].x) / .01, vy = (tip.y - b[N].y) / .01, raw = jsHypot2(vx, vy),
                         sp = raw == 0 ? 1 : raw;
            lights.add(tip.x, tip.y, 22, WHITE, .5, .08);
            lights.add(tip.x, tip.y, 50, C[5], .35, .4);
            for (int i = 0; i < 18; ++i) {
                const double angle = std::atan2(vy, vx) + R.range(-.7, .7), v = R.range(80, 260);
                drops.add({tip.x, tip.y, std::cos(angle) * v, std::sin(angle) * v - R.range(0, 60),
                           R.isChance(.3) ? 2. : 1.});
            }
            for (int i = 0; i < 4; ++i) {
                const double forward = R.range(40, 120), noise = R.range(-30, 30);
                torn.push_back({tip.x, tip.y, (vx / sp) * forward + noise, R.range(-90, -20), R.range(0, TAU),
                                R.range(-12, 12), R.range(0, TAU), R.range(8, 16), R.range(.65, .8), 0});
            }
            for (auto& l : torn) l.max = l.life;
            for (int i = 0; i < 8; ++i)
                emit({ParticleKind::Star, tip.x + R.range(-6, 6), tip.y + R.range(-6, 6), R.range(-20, 20),
                      R.range(-20, 20), R.range(.15, .35), C[7], R.range(.6, 1.6)});
        }
        if (!sunk && t >= 1.08) {
            sunk = true;
            dirt(3, .5);
        }
        drops.update(
            dt, [](double) { return 0.; },
            [&](const nature::Drop& d, double) { spots.push_back({d.x, t, d.size * .7 + R.range(.2, 1)}); });
        for (size_t i = chips.size(); i > 0; --i) {
            auto&      c       = chips[i - 1];
            const bool expired = (c.life -= dt) <= 0;
            // The source steps its removed local chip; no callbacks/RNG observe it.
            c.vy += 950 * dt;
            c.x += c.vx * dt;
            c.y += c.vy * dt;
            c.angle += c.angularVelocity * dt;
            if (c.y > 0) {
                c.y = 0;
                c.vy *= -.3;
                c.vx *= .6;
            }
            if (expired) chips.erase(chips.begin() + std::ptrdiff_t(i - 1));
        }
        for (size_t i = torn.size(); i > 0; --i) {
            auto&      l       = torn[i - 1];
            const bool expired = (l.life -= dt) <= 0;
            // Same isolated post-removal source step, then erase without a dangling reference.
            l.vy += 160 * dt;
            l.vx -= l.vx * 2.5 * dt;
            l.vy -= l.vy * 2 * dt;
            l.x += l.vx * dt + std::sin(l.flip) * 20 * dt;
            l.y += l.vy * dt;
            l.angle += l.angularVelocity * dt;
            l.flip += l.flipVelocity * dt;
            if (expired) torn.erase(torn.begin() + std::ptrdiff_t(i - 1));
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double k = 1 - smooth(1.15, 1.35, t);
        for (int x = -5; x <= 5; ++x) p.dot(x, 0, std::abs(x) < 3 ? S[0] : S[1], k * (1 - std::abs(x) / 7.));
        for (const auto& s : spots) {
            const double a = (1 - smooth(.25, .6, t - s.t0)) * (1 - smooth(1.2, 1.35, t));
            for (int x = int(std::floor(s.x - s.width)); x <= std::ceil(s.x + s.width); ++x)
                p.dot(x, 0, t - s.t0 < .1 ? C[6] : C[4], a);
        }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const auto pts = pose(t);
        if (t > LASH + .04 && t < CRACK + .1) {
            const double             age = clamp((t - CRACK + .02) / .12);
            std::array<VinePath, 12> hist;
            for (size_t k = 0; k < hist.size(); ++k) hist[k] = pose(t - double(k) * .0045);
            for (size_t k = 0; k < 6; ++k) {
                const std::array<Point, 4> quad{{hist[k][N], hist[k + 1][N], hist[k + 1][N - 12], hist[k][N - 12]}};
                haze(p, quad, k < 2 ? C[6] : C[5], .4 * (1 - double(k) / 6) * (1 - age));
            }
            for (size_t i = N - 12; i <= N; i += 2) {
                const double   q   = double(N - i) / 12;
                const uint32_t col = q < .1 ? WHITE : q < .3 ? C[7] : q < .55 ? C[6] : q < .8 ? C[5] : C[4];
                const size_t   len = size_t(jsRound(11 * (1 - q * .6)));
                for (size_t k = 0; k < len; ++k)
                    p.line(hist[k][i].x, hist[k][i].y, hist[k + 1][i].x, hist[k + 1][i].y,
                           k < 2 && q < .1 ? WHITE : col,
                           (1 - age) * (double(k) < double(len) * .5 ? 1 : 2 * (1 - double(k) / double(len))));
            }
            p.glowLine(hist[0][N].x, hist[0][N].y, hist[6][N].x, hist[6][N].y, 14, C[5], .35 * (1 - age));
        }
        vine(p, pts, C);
        thorns(p, pts, T, C, t);
        const double lk = smooth(LASH, CRACK, t) * (1 - smooth(CRACK, CRACK + .35, t));
        if (lk > 0)
            for (size_t i = 6; i <= N; i += 6) p.glow(pts[i].x, pts[i].y, 12, C[5], .18 * lk);
        const double e = t - CRACK;
        if (e >= 0 && e < .12) {
            const auto   tip = pts[N];
            const double k   = 1 - e / .12;
            for (int j = 0; j < 7; ++j) {
                const double a = (double(j) / 7) * TAU + .3, r0 = 4 + 10 * (1 - k), r1 = r0 + 5 * k;
                p.line(tip.x + std::cos(a) * r0, tip.y + std::sin(a) * r0, tip.x + std::cos(a) * r1,
                       tip.y + std::sin(a) * r1, k > .5 ? WHITE : C[6]);
            }
            p.ring(tip.x, tip.y, 3 + 8 * (1 - k), 1, C[7], k);
            p.sparkle(tip.x, tip.y, 7 * k, C[7], C[5], .4);
        }
        for (const auto& c : chips)
            nature::drawRock(p, c.x, c.y, c.size, c.angle, c.shape, S, std::min(1., c.life / .25));
        const std::array<uint32_t, 4> pal{{C[1], C[3], C[5], C[6]}};
        for (const auto& l : torn) nature::drawLeaf(p, l.x, l.y, 6, 2, l.angle, l.flip, pal, std::min(1., l.life / .3));
        drops.draw(p, C);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& vineWhipDescriptor() {
    static const std::array<Option, 2> options{{{"thorn", 1, 64, false, 4, &PixelFxNatureSettings::thorn},
                                                {"soil", 1, 64, false, 8, &PixelFxNatureSettings::soil}}};
    static const Descriptor            d{
        {PixelFxKind::VineWhip, "nature/vine-whip", 216, 128, 60, 114, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 3;
            s.palette      = {hex(0x0c1a10), hex(0x173a1a), hex(0x24582a), hex(0x357a34),
                              hex(0x52a03e), hex(0x86c84e), hex(0xc4ec7a), hex(0xf4ffd0)};
            s.nature.thorn = {hex(0x2a1410), hex(0x5e2a1a), hex(0xa04c2e), hex(0xe89a6a)};
            s.nature.soil  = {hex(0x120c0c), hex(0x251917), hex(0x3b2a24), hex(0x553d31),
                              hex(0x715440), hex(0x8f6e52), hex(0xb8966e), hex(0xe6d0a8)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<VineWhip>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
