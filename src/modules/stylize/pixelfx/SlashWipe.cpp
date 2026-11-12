// eve-hot-path
// Native adaptation of pixelfx transitions/slash-wipe.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScreenGeometry.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double SL = .55, HALO = 18, T0 = .06, SWEEP = .62, E0 = -(screen::H / 2.) * SL - 4,
                 E1  = screen::W + (screen::H / 2.) * SL + 24;
constexpr double CUT = .84, CSW = .07, CHOLD = .05, CTRAIL = .12, SLIP = CUT + .12, SPLIT = CUT + CSW + CHOLD + CTRAIL;
const double     CS = 1 / jsHypot2(1, SL);
double           edgeAt(double t) { return E0 + (E1 - E0) * ease::outCubic(clamp((t - T0) / SWEEP)); }
double           speedAt(double t) {
    return t < T0 || t > T0 + SWEEP ? 0 : ((E1 - E0) * 3 * std::pow(1 - (t - T0) / SWEEP, 2)) / SWEEP;
}
double xAt(double e, double y) { return e + (screen::H / 2. - y) * SL; }
struct Half {
    int                side;
    std::vector<Point> pts;
    double             mx, my, spin;
};
struct Placement {
    double dx, dy, rot, sep;
};
struct SlashWipe final : Kernel {
    std::vector<uint32_t> C;
    uint32_t              ink;
    Rng                   rng;
    Particles             ps;
    Lights                lights;
    double                ux, uy, nx, ny, cx, cy, LC, t = 0, acc = 0;
    Point                 A, B;
    std::array<Half, 2>   halves;
    bool                  entered = false, tipped = false, split = false;
    explicit SlashWipe(const PixelFxSettings& o) : C(o.palette), ink(o.transition.color), rng(o.seed) {
        ps.sparks       = o.transition.sparks;
        const double th = rng.range(.42, .56);
        ux              = std::cos(th);
        uy              = std::sin(th);
        nx              = uy;
        ny              = -ux;
        cx              = screen::W / 2. + rng.range(-24, 24);
        cy              = screen::H / 2. + rng.range(-10, 10);
        const std::array<Point, 4> area{
            {{-24, -24}, {screen::W + 24, -24}, {screen::W + 24, screen::H + 24}, {-24, screen::H + 24}}};
        std::array<Point, 2> ends{};
        for (size_t i = 0; i < 2; ++i) {
            const int s  = i == 0 ? -1 : 1;
            double    lo = 0, hi = 400;
            for (int k = 0; k < 30; ++k) {
                const double m = (lo + hi) / 2, x = cx + s * ux * m, y = cy + s * uy * m;
                if (x >= 0 && x <= screen::W && y >= 0 && y <= screen::H)
                    lo = m;
                else
                    hi = m;
            }
            ends[i] = {cx + s * ux * lo, cy + s * uy * lo};
        }
        A  = ends[0];
        B  = ends[1];
        LC = jsHypot2(B.x - A.x, B.y - A.y);
        for (size_t i = 0; i < 2; ++i) {
            auto& h = halves[i];
            h.side  = i == 0 ? 1 : -1;
            h.pts   = screen::halfPlane(area, cx, cy, h.side * nx, h.side * ny);
            h.mx    = 0;
            h.my    = 0;
            for (const auto& p : h.pts) {
                h.mx += p.x / double(h.pts.size());
                h.my += p.y / double(h.pts.size());
            }
            h.spin = h.side * rng.range(.5, .9);
        }
    }
    Placement placeOf(const Half& h) const {
        const double slip = 5 * ease::outCubic(clamp((t - SLIP) / .12)) * (h.side > 0 ? 1 : -.4),
                     u    = std::max(0., t - SPLIT);
        const double sep = 30 * u + .5 * 1500 * u * u, rot = h.spin * u * u;
        return {ux * slip + h.side * nx * sep, uy * slip + h.side * ny * sep, rot, sep};
    }
    static Point moved(const Half& h, Placement p, Point q) {
        const double c = std::cos(p.rot), s = std::sin(p.rot), lx = q.x - h.mx, ly = q.y - h.my;
        return {h.mx + lx * c - ly * s + p.dx, h.my + lx * s + ly * c + p.dy};
    }
    void update(double dt) override {
        t += dt;
        const double sp = speedAt(t), e = edgeAt(t);
        if (!entered && t >= T0) {
            entered = true;
            lights.add(4, 4, 60, C[5], .6, .25);
        }
        for (acc += sp * dt * .035; acc > 1; --acc) {
            const double y = rng.range(4, screen::H - 4), x = xAt(e, y) - 1, a0 = PI + rng.range(-.1, .6);
            const int    n = rng.integer(5, 10);
            if (x < 0 || x >= screen::W) continue;
            for (int i = 0; i < n; ++i) {
                const double a = a0 + rng.range(-.75, .75), v = rng.range(.2, 1) * (140 + .22 * sp),
                             yy = y + rng.range(-2, 2), life = rng.range(.15, .4);
                ps.spawn(rng, ParticleKind::Spark, x, yy, std::cos(a) * v, std::sin(a) * v, life);
                if (rng.isChance(.4)) {
                    const double l = rng.range(.2, .5);
                    const auto   c = rng.isChance(.5) ? C[6] : C[5];
                    ps.spawn(rng, ParticleKind::Mote, x, y, std::cos(a) * v * .45, std::sin(a) * v * .45 - 20, l, c,
                             .7);
                }
            }
            const double radius = rng.range(30, 46);
            lights.add(x, y, radius, C[5], .35, .14);
        }
        if (!tipped && t >= CUT + CSW) {
            tipped = true;
            for (int i = 0; i < 12; ++i) {
                const double a = std::atan2(uy, ux) + rng.range(-.5, .5), v = rng.range(150, 400),
                             life = rng.range(.12, .35);
                ps.spawn(rng, ParticleKind::Spark, B.x - ux * 2, B.y - uy * 2, std::cos(a) * v, std::sin(a) * v, life);
            }
            for (int i = 0; i < 18; ++i) {
                const double k = rng.range(.1, 1), x = A.x + (B.x - A.x) * k, y = A.y + (B.y - A.y) * k;
                const int    s = rng.sign();
                const double v = rng.range(30, 100), life = rng.range(.25, .55);
                const auto   c = rng.isChance(.5) ? C[5] : C[4];
                ps.spawn(rng, ParticleKind::Mote, x, y, s * nx * v + ux * 20, s * ny * v - 10, life, c, .6);
            }
            for (int i = 0; i < 5; ++i)
                lights.add(A.x + (B.x - A.x) * (i + .5) / 5, A.y + (B.y - A.y) * (i + .5) / 5, 46, C[5], .45, .3);
        }
        if (!split && t >= SPLIT) {
            split = true;
            for (int i = 0; i < 46; ++i) {
                const double k = rng.range(0, 1), x = A.x + (B.x - A.x) * k, y = A.y + (B.y - A.y) * k;
                const int    s = rng.sign();
                const double v = rng.range(80, 320), a = std::atan2(s * ny, s * nx) + rng.range(-.7, .7),
                             life = std::min(rng.range(.15, .45), 1.7 - .05 - t);
                ps.spawn(rng, ParticleKind::Spark, x, y, std::cos(a) * v, std::sin(a) * v, life);
            }
            for (int i = 0; i < 6; ++i)
                lights.add(A.x + (B.x - A.x) * (i + .5) / 6, A.y + (B.y - A.y) * (i + .5) / 6, 56, C[5], .5, .35);
        }
        ps.update(dt, rng, [](double) { return INFINITY; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p0) const override {
        auto p = screen::clip(p0);
        if (t < CUT + CSW + CHOLD + .02 || t < SLIP) {
            const double e = edgeAt(t), sp = speedAt(t), Ls = clamp(sp * .0065, 1.5, 8), echo = clamp(sp / 900) * .6;
            for (int y = 0; y < screen::H; ++y) {
                const double xe = xAt(e, y + .5);
                const int    ia = int(clamp(std::floor(xe - (Ls + 10) / CS), 0, screen::W)),
                             ib = int(clamp(std::ceil(xe + HALO / CS), 0, screen::W));
                if (ia > 0) screen::rect(p0, 0, y, ia, 1, ink);
                for (int x = ia; x < ib; ++x) {
                    const double d = (xe - (x + .5)) * CS;
                    if (d < 0) {
                        const double u = 1 + d / HALO;
                        p0.light(x, y, C[5], .32 * u * u);
                        if (d > -4) p0.light(x, y, WHITE, .3 * std::pow(1 + d / 4, 2));
                        continue;
                    }
                    if (d < 1)
                        p0.px(x, y, WHITE);
                    else if (d < 2)
                        p0.px(x, y, C[6]);
                    else {
                        const double q = (d - 2) / Ls;
                        p0.px(x, y, q < .3 ? C[5] : q < .55 ? C[4] : q < .8 ? C[3] : q < 1 ? C[2] : ink);
                        if (echo > 0 && std::abs(d - Ls - 7) < .5 && (x + y) % 3 == 0) p0.dot(x, y, C[4], echo);
                    }
                    if (d < Ls + 2) p0.light(x, y, C[5], .3 * (1 - d / (Ls + 2)));
                }
            }
        }
        if (t >= SLIP) {
            const double heat = 1 - clamp((t - CUT - .05) / .5);
            for (const auto& h : halves) {
                const auto           pl = placeOf(h);
                std::array<Point, 6> pts{};
                size_t               count = 0;
                for (const auto& q : h.pts) pts[count++] = moved(h, pl, q);
                const auto   a = moved(h, pl, A), b = moved(h, pl, B);
                const double c = std::cos(pl.rot), sn = std::sin(pl.rot), mx = h.side * (nx * c - ny * sn),
                             my = h.side * (nx * sn + ny * c);
                const std::array<uint32_t, 4> rim{heat > .55   ? WHITE
                                                  : heat > .25 ? C[6]
                                                               : C[5],
                                                  heat > .4 ? C[6] : C[4], heat > .3 ? C[5] : C[3],
                                                  heat > .3 ? C[4] : ink};
                const double                  deep = clamp(1 + pl.sep * .5, 1, 4);
                screen::fillConvex(p0, std::span<const Point>(pts.data(), count), [&](double x, double y) {
                    const double d = (x - a.x) * mx + (y - a.y) * my;
                    if (d < deep) {
                        if (d < 2) p0.light(x, y, C[5], .4 * heat * (1 - d / 2));
                        return rim[size_t(std::max(0., std::floor(d)))];
                    }
                    return ink;
                });
                if (heat > 0)
                    for (int i = 0; i < 8; ++i) {
                        const double k = (i + .5) / 8;
                        p.glow(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, 12 + pl.sep * .15, C[5], .3 * heat);
                    }
            }
        }
        const double u = t - CUT;
        if (u >= 0 && u < CSW + CHOLD + CTRAIL + .25) {
            const double head = LC * ease::outExpo(clamp(u / CSW)),
                         tail = LC * smooth(CSW + CHOLD, CSW + CHOLD + CTRAIL, u),
                         age  = clamp(u / (CSW + CHOLD + CTRAIL));
            const double Wc = 3.6 * (1 - (tail / LC) * .55), span = head - tail, sn = std::abs(nx);
            if (span > 1 && tail < LC - 1 && t < SPLIT)
                for (int y = 0; y < screen::H; ++y) {
                    const double x0 = A.x + ((y + .5 - A.y) * ux) / uy, hw = (Wc + 1) / sn;
                    for (int x = std::max(0, int(std::floor(x0 - hw)));
                         x < std::min(screen::W, int(std::ceil(x0 + hw))); ++x) {
                        const double px = x + .5 - A.x, py = y + .5 - A.y, s = px * ux + py * uy, v = (s - tail) / span;
                        if (v < 0 || v > 1) continue;
                        const double w = Wc * std::pow(std::sin(PI * std::pow(v, 2.2)), .6);
                        if (w < .35) continue;
                        const double q = std::abs(px * nx + py * ny) / w;
                        if (q > 1) continue;
                        const double ci = q < .34 && age < .5
                                              ? 0
                                              : jsRound(q * (double(C.size()) - 1) * .75 + age * 2.4 - v * .8 + .4);
                        p0.dot(x, y, C[C.size() - 1 - size_t(clamp(ci, 0, double(C.size()) - 1))],
                               std::min(1., v / .4));
                        if (q < .5) p0.light(x, y, C[5], .4 * (1 - age));
                    }
                }
            if (tail > 2 && t < SPLIT)
                for (double s = 0; s < tail; s += 1.6)
                    p.dot(A.x + ux * s, A.y + uy * s, C[5], .7 * (.4 + (.6 * s) / LC));
            if (u < .3) {
                const double hx = A.x + ux * head, hy = A.y + uy * head;
                p.glow(hx, hy, 46, C[4], .55 * (1 - u / .3));
            }
            const double g = u - CSW * .6;
            if (g > 0 && g < .24)
                p.sparkle(B.x - ux * 3, B.y - uy * 3, 11 * ease::outBack(std::min(1., g / .05)) * (1 - g / .24), C[6],
                          C[4], .7);
        }
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& slashWipeDescriptor() {
    static const std::array<Option, 2> options{
        {{"color", 0, 4294967295., false, 0, &PixelFxTransitionSettings::color, true},
         {"sparks", 1, 64, false, 4, &PixelFxTransitionSettings::sparks}}};
    static const Descriptor d{
        {PixelFxKind::SlashWipe, "transitions/slash-wipe", 320, 180, 0, 0, 1.7, false},
        8,
        [](PixelFxSettings& s) {
            s.seed              = 2;
            s.transition.color  = hex(0x0b0a10);
            s.transition.sparks = {hex(0xfff3a6), hex(0xffe26b), hex(0xff8a2a), hex(0xf2561d)};
            s.palette           = {hex(0x151a3d), hex(0x212f6b), hex(0x2c58aa), hex(0x3a8bd8),
                                   hex(0x62c8f4), hex(0xb8c9e8), hex(0xe8f1ff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<SlashWipe>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
