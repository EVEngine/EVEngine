// eve-hot-path
// Native adaptation of pixelfx arcane/portal.js, MIT.
// Sprite Fusion black hole, Copyright (c) 2026 Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Arcane.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4;
double           wrapPi(double a) { return a - TAU * std::floor((a + PI) / TAU); }
struct Scene {
    const std::vector<uint32_t>& C;
    double                       rx, ry, ph;
    uint32_t                     seed;
    double                       cy;
    Scene(double t, const PixelFxSettings& o)
        : C(o.palette), rx(o.arcane.portalWidth), ry(o.arcane.portalHeight), ph(t / D), seed(o.seed), cy(-(ry + 5)) {}
    double rim(double th) const {
        return 1 + .05 * tileNoise((th / TAU) * 10 + ph * 10, ph * 4, 10, 4, seed) +
               .018 * std::sin(th * 3 - ph * TAU * 2);
    }
    Point at(double th, double rho) const {
        const double out = std::max(0., rho - 1), k = std::min(1., rho);
        return {std::cos(th) * (k * rx + out * 21), std::min(-1., cy + std::sin(th) * (k * ry + out * 16))};
    }
    std::array<double, 2> spots() const { return {ph * TAU * 2 - .6, ph * TAU * 2 + PI - .6}; }
    double                hot(double th) const {
        const auto v = spots();
        return std::max(std::exp(-std::pow(wrapPi(th - v[0]), 2) / .12),
                        .85 * std::exp(-std::pow(wrapPi(th - v[1]), 2) / .08)) +
               .35 * std::max(0., std::cos(th + 2.3));
    }
};
void rift(Paint& p, const Scene& s) {
    const auto&  C  = s.C;
    const double rx = s.rx, ry = s.ry, ph = s.ph, cy = s.cy;
    p.shader(-rx - 4, cy - ry - 4, rx + 4, cy + ry + 4, [&](double x, double y, ShadeOutput& out) {
        const double u = x / rx, v = (y - cy) / ry, raw = jsHypot2(u, v), r = raw == 0 ? 1e-6 : raw,
                     th = std::atan2(v, u), r0 = s.rim(th), g = jsHypot2(u / rx, v / ry) / r, e = (r - r0) / g,
                     h = s.hot(th);
        if (e > 2.5) return;
        if (e > 1.5) {
            if (h > .6) {
                out.c = C[3];
                out.a = .5;
            }
            return;
        }
        if (e > .5) {
            out.c = h > .5 ? C[4] : C[2];
            return;
        }
        if (e > -.5) {
            out.c = h > .42 ? WHITE : C[6];
            out.k = .25 + .4 * h;
            return;
        }
        const double d = r / r0, z = -std::log(std::max(.02, d)), phi = 3 * th + 6.5 * z - TAU * 3 * ph,
                     c1 = .5 + .5 * std::cos(phi), c2 = .5 + .5 * std::cos(phi + PI + .6 * z), lit = std::pow(d, 1.5),
                     sink  = smooth(.2, .62, d),
                     knot  = std::pow(std::max(0., std::cos(4 * z - phi / 3 + TAU * 5 * ph)), 6);
        double       st    = smooth(.4, .7, d) + .6 * smooth(.8, .95, d);
        const double crest = std::pow(c1, 16) * (.6 + 5.6 * lit) * (1 + .5 * knot) * sink;
        st += crest + std::pow(c1, 5) * 1.1 * lit;
        st += std::pow(c2, 18) * (.3 + 1.8 * lit) * sink;
        st += std::pow(.5 + .5 * std::cos(TAU * (z * 1.5 - ph * 2)), 20) * 1.3 * sink * (1 - .5 * c1);
        st += std::pow(.5 + .5 * std::cos(3 * phi + 2 * z + 1.3), 10) * 1.1 * lit * sink;
        st += 2.2 * std::pow(smooth(.8, 1, d), 2) * (.75 + .45 * h);
        out.c = arcane::tone(C, st / 7, x, y, 0, .65);
        if (crest > 2.2) out.k = .12 + .05 * crest;
    });
}
void well(Paint& p, const Scene& s) {
    cycle(s.ph * D, D, 18, s.seed * 7u + 5u, 1.1, 1.8, [&](double age, double life, Rng& r, int) {
        const double q = age / life, th = r.range(0, TAU) + q * 2.6, rho = .92 * (1 - ease::inQuad(q) * .9),
                     r0 = s.rim(th), x = std::cos(th) * rho * r0 * s.rx, y = s.cy + std::sin(th) * rho * r0 * s.ry;
        const int    freq = r.integer(14, 22);
        const double tw   = std::sin(s.ph * TAU * freq + r.range(0, TAU)),
                     a    = std::min(1., q * 6) * (rho > .25 ? 1 : rho / .25);
        if (tw > -.3) p.dot(x, y, rho > .6 && tw > .6 ? WHITE : rho > .45 ? s.C[6] : s.C[5], a);
    });
}
void orbit(Paint& p, const Scene& s) {
    for (int i = 0; i < 7; ++i) {
        const double k   = std::fmod(s.ph * 2 + double(i) / 7, 1),
                     th  = std::fmod(i * 2.39 + double(s.seed), TAU) + s.ph * TAU * 2 + k * 2.5,
                     rho = 1.2 + .35 * std::fmod(i * .618, 1) - .3 * ease::inQuad(k), x = std::cos(th) * rho * s.rx,
                     y = s.cy + std::sin(th) * rho * s.ry, a = std::min(1., std::sin(k * PI) * 2.2);
        arcane::mote(p, x, y, -std::sin(th) * s.rx * 10, std::cos(th) * s.ry * 10, i % 3 ? s.C[6] : WHITE, .6, a, .3);
    }
}
void infall(Paint& p, const Scene& s) {
    const auto& C = s.C;
    cycle(s.ph * D, D, 60, s.seed * 7u + 1u, .3, .5, [&](double age, double life, Rng& r, int) {
        const double a0 = r.range(0, TAU), d0 = r.range(1.8, 3), spinMagnitude = r.range(.8, 1.8),
                     spin = spinMagnitude * (r.isChance(.8) ? 1 : .35), size = r.range(.8, 1.5), hue = r.next();
        const auto   pos    = [&](double q) { return s.at(a0 + spin * q * q, d0 * (1 - std::pow(q, 1.4))); };
        const double q      = age / life;
        const auto [x, y]   = pos(q);
        const auto [x2, y2] = pos(std::min(1., q + .015));
        const double vx = (x2 - x) / (.015 * life), vy = (y2 - y) / (.015 * life), rho = d0 * (1 - std::pow(q, 1.4)),
                     r0 = s.rim(a0 + spin * q * q), fade = std::min(1., q * 5);
        if (rho > r0 + .06)
            arcane::mote(p, x, y, vx, vy,
                         hue < .25   ? WHITE
                         : hue < .55 ? C[6]
                         : hue < .8  ? C[5]
                                     : C[4],
                         hue < .8 ? size : size * 1.4, fade, hue < .8 ? .4 : .15);
        else if (rho > .3)
            p.dot(x, y, rho > .65 ? C[6] : C[5], rho);
    });
    cycle(s.ph * D, D, 12, s.seed * 7u + 2u, .36, .5, [&](double age, double life, Rng& r, int) {
        const double a0 = r.range(0, TAU), d0 = r.range(2.3, 3), spin = r.range(.2, .5), qc = std::sqrt(1 - 1.05 / d0),
                     q   = age / life;
        const auto   pos = [&](double u) { return s.at(a0 + spin * u * u, d0 * (1 - u * u)); };
        if (q < qc) {
            const auto [x, y]   = pos(q);
            const auto [x2, y2] = pos(q + .015);
            arcane::streak(p, x, y, (x2 - x) / (.015 * life), (y2 - y) / (.015 * life), C[6], std::min(1., q * 5));
        } else {
            const double k    = 1 - (q - qc) / (1 - qc);
            const auto [x, y] = pos(qc);
            if (k > .55) p.px(x, y, WHITE);
            p.glow(x, y, 7, C[6], .6 * k * k);
        }
    });
}
void skaters(Paint& p, const Scene& s) {
    const double foot = s.cy + s.ry;
    cycle(s.ph * D, D, 14, s.seed * 7u + 3u, .7, 1.1, [&](double age, double life, Rng& r, int) {
        const int    side = r.sign();
        const double x0 = side * r.range(22, 50), y0 = -1 - r.range(0, 1.5), q = age / life, e = ease::inCubic(q),
                     x = x0 * (1 - e), y = y0 + (foot + 2) * smooth(.55, 1, q),
                     xb  = x0 * (1 - ease::inCubic(std::max(0., q - .04))),
                     yb  = y0 + (foot + 2) * smooth(.55, 1, q - .04);
        const bool   big = r.isChance(.3);
        arcane::mote(p, x, y, (x - xb) * 25, (y - yb) * 25, big ? s.C[6] : s.C[5], big ? .9 : .6,
                     std::min(1., q * 4) * (1 - smooth(.85, 1, q)), .3);
    });
}
struct Portal final : AnalyticKernel {
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const Scene  s(t, o);
        const double pulse = .85 + .15 * std::sin(s.ph * TAU * 2);
        for (int x = -36; x <= 36; ++x) {
            const double u = std::abs(x + .5) / 36, a = (u < .55 ? 1 : 1 - (u - .55) / .45) * (.8 + .2 * pulse);
            p.dot(x, 0, u < .2 ? WHITE : u < .5 ? s.C[6] : s.C[5], a);
            if (u < .3) p.dot(x, 1, u < .15 ? s.C[5] : s.C[4], u < .15 ? 1 : .5);
        }
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const Scene  s(t, o);
        const double pulse = .85 + .15 * std::sin(s.ph * TAU * 2);
        rift(p, s);
        well(p, s);
        orbit(p, s);
        infall(p, s);
        skaters(p, s);
        p.glow(0, s.cy, s.ry * 5, s.C[4], .17 * pulse);
        p.glow(0, s.cy, s.ry * 2.6, s.C[4], .1 * pulse);
        for (int i = 0; i < 16; ++i) {
            const double th = (double(i) / 16) * TAU, r0 = s.rim(th) * 1.08;
            p.glow(std::cos(th) * r0 * s.rx, s.cy + std::sin(th) * r0 * s.ry, 15, s.C[4], .15 * pulse);
        }
        for (double th : s.spots())
            p.glow(std::cos(th) * s.rim(th) * s.rx, s.cy + std::sin(th) * s.rim(th) * s.ry, 16, s.C[6], .42);
        for (int i = -3; i <= 3; ++i) p.glow(i * 7, -1, 16, s.C[5], .12 * pulse);
    }
};
}  // namespace
const Descriptor& portalDescriptor() {
    static const std::array<Option, 2> options{{{"width", .125, 128, false, 0, &PixelFxArcaneSettings::portalWidth},
                                                {"height", .125, 128, false, 0, &PixelFxArcaneSettings::portalHeight}}};
    static const Descriptor            d{
        {PixelFxKind::Portal, "arcane/portal", 152, 116, 76, 106, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed                = 4;
            s.arcane.portalWidth  = 17;
            s.arcane.portalHeight = 29;
            s.palette             = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                                     hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Portal>(); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
