// eve-hot-path
// Native adaptation of pixelfx status/bleeding.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/StatusShape.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4, BEAT = D / 2, G = 900;
struct Wound {
    double x, y0, yb, phase;
    int    mx, drops, side;
};
void drop(Paint& p, std::span<const uint32_t> C, double x, double y, double v, int size) {
    const double tail = std::min(5., v * .012);
    if (tail >= 1) p.line(x, y - tail, x, y - 1, size >= 2 ? C[3] : C[2]);
    if (size >= 2) {
        p.px(x, y, C[5]);
        p.px(x + 1, y, C[4]);
        p.px(x, y + 1, C[3]);
        p.px(x + 1, y + 1, C[2]);
    } else
        p.px(x, y, C[4]);
}
void splat(Paint& p, std::span<const uint32_t> C, double x, double ground, double age, bool big, int seed) {
    const double life = D * .85;
    if (age < 0 || age > life) return;
    const double spread = smooth(0, .1, age), hw = (big ? 2.2 : 1.4) * (.5 + .5 * spread),
                 fade = 1 - smooth(life - .5, life, age);
    const size_t dry  = age < .35 ? 4u : age < 1 ? 3u : 2u;
    for (int xx = int(std::floor(x - hw)); xx <= std::ceil(x + hw); ++xx)
        p.dot(xx, ground - 1, std::abs(xx + .5 - x) > hw * .75 ? C[dry - 1] : C[dry], fade);
    if (age < .3) p.px(x - .5, ground - 1, C[5]);
    if (age < .4)
        for (int j = 0; j < (big ? 3 : 2); ++j) {
            const double vx = (j % 2 ? 1 : -1) * (18 + 24 * rand({uint32_t(seed), uint32_t(j), 1u})),
                         vy = -(40 + 40 * rand({uint32_t(seed), uint32_t(j), 2u})), tt = age, dx = vx * tt,
                         dy = vy * tt + .5 * G * tt * tt;
            if (dy <= 0) p.px(x + dx, ground - 1 + dy, j ? C[3] : C[4]);
        }
}
struct Bleeding final : AnalyticKernel {
    status::Shape        shape;
    std::array<Wound, 3> wounds;
    explicit Bleeding(const PixelFxSettings& o) : shape(o.actor) {
        const auto& a    = o.actor;
        const auto  edge = [&](double q, int side) {
            const int  y   = int(jsRound(shape.height * q));
            const auto row = shape.rows[size_t(y)];
            return std::array<int, 2>{side < 0 ? row[0] + 1 : row[1] - 1, y};
        };
        const auto make = [&](std::array<int, 2> point, int drops, double phase) {
            const int mx = point[0], my = point[1];
            int       run = 1;
            while (run < 3 && shape.isOccupied(mx, my + run + 1)) ++run;
            return Wound{
                a.x + mx + .5, a.y + my, a.y + my + run + 1, phase, mx, drops, mx < double(shape.width) / 2 ? -1 : 1};
        };
        auto front = edge(.55, 1);
        for (const auto& q : shape.body)
            if (q[1] > shape.height * .5 && q[1] < shape.height * .62 && q[0] > shape.width * .55 &&
                q[0] < shape.width * .75 && shape.isOccupied(q[0], q[1] + 3)) {
                front = q;
                break;
            }
        wounds = {make(edge(.57, -1), 3, 0), make(front, 2, .35), make(edge(.6, 1), 2, .7)};
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C  = o.palette;
        const double px = (wounds[0].x + wounds[2].x) / 2 - 1, rx = 6.5, beat = std::exp(-(wrap(t, BEAT) / .25));
        for (int x = int(std::floor(px - rx - 1)); x <= px + rx + 1; ++x) {
            const double q = std::abs(x + .5 - px) / rx;
            if (q > 1.1) continue;
            p.px(x, -1, q > .85 ? C[2] : C[1]);
            if (q < .8) p.px(x, 0, q < .5 ? C[1] : C[0]);
            if (q < .45) p.px(x, 1, C[0]);
        }
        p.px(px - 2, -1, C[4]);
        p.px(px - 1, -1, C[3]);
        p.px(px + 3, -1, C[3]);
        p.glow(px, -1, 8, C[3], .06 + .04 * beat);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C  = o.palette;
        const auto&  a  = o.actor;
        const double ph = wrap(t, BEAT), hurt = std::max(0., 1 - ph / .12);
        if (hurt > 0)
            for (const auto& q : shape.body) p.blend(a.x + q[0], a.y + q[1], C[4], .26 * hurt);
        for (const auto& w : wounds) {
            p.px(w.x, w.y0 - 1, C[1]);
            p.px(w.x - w.side, w.y0 - 1, C[0]);
            p.px(w.x - w.side, w.y0, C[2]);
            for (double y = w.y0; y < w.yb; ++y) p.px(w.x, y, y == w.y0 ? C[4] : y >= w.yb - 1 ? C[3] : C[2]);
            const double period = D / w.drops;
            for (int k = 0; k < w.drops; ++k) {
                const double born = w.phase * period + k * period, age = wrap(t - born, D), swell = period * .62;
                const bool   big   = (k + w.mx) % 2 == 0;
                const double fallH = std::max(1., -1 - w.yb), fall = std::sqrt((2 * fallH) / G), ground = 0;
                if (age < swell) {
                    const double u      = age / swell;
                    const int    length = u < .35 ? 1 : u < .75 ? 2 : 3;
                    for (int j = 0; j < length; ++j) p.px(w.x, w.yb + j, j == length - 1 ? C[2] : C[3]);
                    if (length == 3) p.px(w.x - 1, w.yb + 2, C[1]);
                    p.px(w.x, w.yb + length - 2, u > .5 ? C[5] : C[4]);
                } else if (age < swell + fall) {
                    const double f = age - swell, y = w.yb + 2 + .5 * G * f * f;
                    drop(p, C, w.x, y, G * f, big ? 2 : 1);
                } else
                    splat(p, C, w.x, ground, age - swell - fall, big, w.mx * 31 + k);
            }
        }
        const auto& w = wounds[0];
        if (ph < .5)
            for (int j = 0; j < 7; ++j) {
                const double vx = w.side * (18 + 42 * rand({uint32_t(j), 7u})),
                             vy = -(15 + 50 * rand({uint32_t(j), 8u})), tt = ph - j * .012;
                if (tt <= 0) continue;
                const double x = w.x + status::travel(vx, 0, 1.2, tt), y = w.y0 + status::travel(vy, G * .8, .6, tt),
                             x2 = w.x + status::travel(vx, 0, 1.2, tt - .02),
                             y2 = w.y0 + status::travel(vy, G * .8, .6, tt - .02);
                if (y >= -1) continue;
                p.line(x2, y2, x, y, j % 3 ? C[3] : C[2]);
                p.px(x, y, j % 2 ? C[5] : C[4]);
            }
        p.glow(w.x, w.y0, 6, C[4], .25 * hurt);
    }
};
}  // namespace
const Descriptor& bleedingDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::Bleeding, "status/bleeding", 60, 56, 30, 46, D, true},
        6,
        [](PixelFxSettings& s) {
            s.seed         = 5;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x350711), hex(0x5e0c1b), hex(0x931327), hex(0xc71e33), hex(0xf2384a), hex(0xff8a7a)};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& o) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Bleeding>(o); },
        true,
        0,
        2};
    return d;
}
}  // namespace eve::stylize::pixelfx
