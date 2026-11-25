// eve-hot-path
// Native adaptation of pixelfx water/fountain.js (MIT), including Sprite Fusion caustic lighting.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Water.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2, SY = .34, G = 600, RIM = 10, RX = 36, IN = 29.5, JET = 54;
constexpr double BY = -RX * SY, LEVEL = RIM - 1.2;
struct Arc {
    double a, vr, vh, tl, ca, sa;
};
std::array<Arc, 12> arcs(uint32_t seed) {
    std::array<Arc, 12> out{};
    for (uint32_t i = 0; i < out.size(); ++i) {
        const double a = ((i + .5) / 12) * TAU + rand({i, seed}) * .25, vr = 38 + 14 * rand({i, seed + 1}),
                     vh = 30 + 26 * rand({i, seed + 2}), tl = (vh + std::sqrt(vh * vh + 2 * G * (JET - LEVEL))) / G;
        out[i] = {a, vr, vh, tl, std::cos(a), std::sin(a)};
    }
    return out;
}
Point along(const Arc& q, double tau) {
    const double r = q.vr * tau, h = JET + q.vh * tau - .5 * G * tau * tau;
    return {q.ca * r, BY + q.sa * r * SY - h};
}
bool inEllipse(double x, double y, double rx, double cy) {
    return std::pow(x / rx, 2) + std::pow((y - cy) / (rx * SY), 2) <= 1;
}
void basin(Paint& p, std::span<const uint32_t> ST, int half) {
    const double cy = BY - RIM, ry = RX * SY;
    p.shader(-RX - 1, cy - ry - 1, RX + 1, 1, [&](double x, double y, ShadeOutput& out) {
        if (inEllipse(x, y, RX, cy)) {
            if (inEllipse(x, y, IN, cy)) return;
            const bool near = y > cy;
            if (near != (half > 0)) return;
            const bool   outer = !inEllipse(x, y - 1, RX, cy) || !inEllipse(x, y + 1, RX, cy) ||
                                 !inEllipse(x - 1, y, RX, cy) || !inEllipse(x + 1, y, RX, cy),
                         inner = inEllipse(x, y - 1, IN, cy) || inEllipse(x, y + 1, IN, cy) ||
                                 inEllipse(x - 1, y, IN, cy) || inEllipse(x + 1, y, IN, cy);
            const double a = std::atan2((y - cy) / ry, x / RX), lit = -std::cos(a) * .65 - std::sin(a) * .45;
            out.c = outer        ? ST[0]
                    : inner      ? (y < cy ? ST[2] : ST[1])
                    : lit > .4   ? ST[5]
                    : lit > -.05 ? ST[4]
                    : lit > -.45 ? ST[3]
                                 : ST[2];
            return;
        }
        if (half < 0 || std::abs(x) >= RX) return;
        const double dx = x / RX, top = cy + std::sqrt(1 - dx * dx) * ry, base = top + RIM;
        if (y < top || y > base) return;
        // Preserve the source's [0,1] clamp before asin, including the left-hand block joints.
        const double u = std::asin(clamp(dx)) * RX * .62, row = y - top, course = row > RIM * .5 ? 1 : 0;
        const bool   joint = wrap((u + 40 + course * 4.5) / 9, 1) < .1;
        out.c              = y > base - 1 || std::abs(x) > RX - 1 || row < 1 ? ST[0]
                             : joint || std::abs(row - RIM * .5) < .5        ? ST[1]
                             : dx < -.4                                      ? ST[4]
                             : dx < .3                                       ? ST[3]
                                                                             : ST[2];
        if (row >= 1 && row < 2 && !joint) out.c = dx < 0 ? ST[2] : ST[1];
    });
}
void pool(Paint& p, std::span<const uint32_t> C, double ph) {
    const double rx = IN, ry = rx * SY, cy = BY - LEVEL, T = ph * TAU;
    for (int Y = int(std::floor(cy - ry)); Y < cy + ry; ++Y)
        for (int X = -int(std::ceil(rx)); X < rx; ++X) {
            const double x = X + .5, y = Y + .5, dx = x / rx, dy = (y - cy) / ry, d = dx * dx + dy * dy;
            if (d > 1) continue;
            p.blend(x, y, dy < -.55 ? C[3] : C[1], dy < -.55 ? .8 : .85);
            const double u = x * .25 + 1.6 * std::sin(y * .6 + T), w = y * .7 + 1.6 * std::sin(x * .14 - T * 2),
                         f = std::max(0., 1 - std::abs(std::sin(u) + std::sin(w)) * 2.6) * (1 - d);
            if (f > .35) p.light(x, y, C[5], .3 * f);
        }
}
void arc(Paint& p, std::span<const uint32_t> C, const Arc& q, double ph, bool back) {
    std::optional<Point> previous;
    for (double tau = 0; tau <= q.tl; tau += .012) {
        const auto   pos = along(q, tau);
        const double u   = tau / q.tl;
        if (previous && u < .66)
            p.line(previous->x, previous->y, pos.x, pos.y,
                   back      ? (u < .25 ? C[5] : C[4])
                   : u < .2  ? WHITE
                   : u < .45 ? C[6]
                             : C[5]);
        previous = pos;
    }
    const std::array<uint32_t, 8> dim{{C[0], C[1], C[2], C[2], C[3], C[4], C[5], C[5]}};
    for (int k = 0; k < 6; ++k) {
        const double u = std::fmod(ph * 2 + double(k) / 6, 1) * .42 + .58, tau = u * q.tl;
        const auto   pos = along(q, tau);
        const double vy  = (q.vh - G * tau) * -1 + q.sa * q.vr * SY;
        water::bead(p, pos.x, pos.y, q.ca * q.vr, vy,
                    back         ? 1
                    : k % 3 == 0 ? 2
                                 : 1,
                    back ? std::span<const uint32_t>(dim) : C);
    }
    const double g   = std::fmod(ph * 4 + q.a, 1) * .6;
    const auto   pos = along(q, g * q.tl);
    p.px(pos.x, pos.y, WHITE);
    if (!back) p.glow(pos.x, pos.y, 3, C[5], .3);
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    const auto   Q  = arcs(o.seed);
    const double ph = t / D;
    basin(p, water::rock(), -1);
    pool(p, o.palette, ph);
    for (const auto& q : Q)
        if (q.sa < 0) arc(p, o.palette, q, ph, true);
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const auto&  ST = water::rock();
    const auto   Q  = arcs(o.seed);
    const double ph = t / D, T = ph * TAU;
    p.shader(-3.5, BY - 16, 3.5, BY - LEVEL + .5, [&](double x, double y, ShadeOutput& out) {
        const double w = y < BY - 14 ? 3.4 : 2.2;
        if (std::abs(x) > w) return;
        out.c = std::abs(x) > w - .8 || y < BY - 15.5 ? ST[0] : x < -.6 ? ST[4] : x < .8 ? ST[3] : ST[2];
    });
    p.shader(-3, BY - JET - 1, 3, BY - 16, [&](double x, double y, ShadeOutput& out) {
        const double v = (BY - 16 - y) / (JET - 16), sway = std::sin(T * 2 + v * 5) * .5 * v,
                     w = 1.3 + .5 * v + .3 * std::sin(T * 3 + y * .4), q = (x - sway) / w;
        if (std::abs(q) > 1) return;
        const double n = tileNoise(x * .9, y * .15 + ph * 16, 0, 16, o.seed);
        out.c          = q < -.4 ? C[6] : q < .35 ? (n > -.3 ? WHITE : C[6]) : C[5];
    });
    p.circle(0, BY - JET - 1, 2.4, C[6]);
    p.circle(-.4, BY - JET - 1.5, 1.4, WHITE);
    p.glow(0, BY - JET, 22, C[5], .46 + .06 * std::sin(T * 4));
    p.glow(0, BY - JET - 1, 8, WHITE, .3);
    cycle(t, D, 10, o.seed + 30, .2, .35, [&](double age, double life, Rng& rng, int) {
        const auto&  q   = Q[size_t(std::floor(rng.next() * 12))];
        const auto   pos = along(q, rng.range(.1, .7) * q.tl);
        const double k   = std::sin((PI * age) / life);
        if (q.sa < -.3) return;
        p.px(pos.x, pos.y, WHITE);
        if (k > .6)
            for (const Point off : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                p.px(pos.x + off.x, pos.y + off.y, C[6]);
        p.glow(pos.x, pos.y, 5, C[5], .4 * k);
    });
    for (int i = 0; i < 4; ++i) p.px(std::sin(T * 3 + i * 1.7) * 2.5, BY - 16 - (i % 2), i % 2 ? C[6] : WHITE);
    for (const auto& q : Q)
        if (q.sa >= 0) arc(p, C, q, ph, false);
    for (uint32_t i = 0; i < Q.size(); ++i) {
        const auto&  q   = Q[i];
        const double f   = std::fmod(ph * 2 + rand({i, o.seed + 9}), 1);
        const auto   pos = along(q, q.tl);
        water::ring(p, pos.x, pos.y, 2 + f * 8, SY, .8 * std::pow(1 - f, 1.3), C, {1, 3, 1, false, 0, i});
        const double g = std::fmod(ph * 6 + rand({i, o.seed + 10}), 1);
        if (g < .15) p.px(pos.x, pos.y - 1, WHITE);
        if (g < .3 && q.sa > -.2)
            p.line(pos.x, pos.y - 1, pos.x + (rand({i, o.seed + 11}) - .5) * 2, pos.y - 1 - 3 * (1 - g / .3), C[6]);
        p.glow(pos.x, pos.y, 6, C[5], .18 * (1 - g));
    }
    basin(p, ST, 1);
    cycle(t, D, 16, o.seed + 20, .8, 1.4, [&](double age, double life, Rng& rng, int) {
        const double k = age / life, a = rng.range(0, TAU), rr = rng.range(4, 20) + k * 6, x = std::cos(a) * rr,
                     y = BY - JET + rng.range(-4, 16) + k * 10;
        if (k > .75 && rng.next() < (k - .75) * 4) return;
        p.px(x, y, rng.next() < .4 ? WHITE : C[6]);
    });
    p.glowLine(0, BY - 16, 0, BY - JET, 10, C[5], .18);
    p.glow(0, BY - LEVEL - 2, 46, C[4], .26 + .03 * std::sin(T * 2));
    p.glow(0, BY - LEVEL - 20, 30, C[5], .12);
}
}  // namespace
const Descriptor& fountainDescriptor() {
    static const Descriptor d{{PixelFxKind::Fountain, "water/fountain", 120, 104, 60, 96, D, true},
                              8,
                              [](PixelFxSettings& s) { water::defaults(s, 3); },
                              front,
                              {},
                              nullptr,
                              back};
    return d;
}
}  // namespace eve::stylize::pixelfx
