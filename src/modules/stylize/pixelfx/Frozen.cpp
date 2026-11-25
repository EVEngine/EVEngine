// eve-hot-path
// Native adaptation of pixelfx status/frozen.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/FrozenOrder.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/StatusEyes.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2;
struct Fern {
    int    x, y;
    double growth;
};
struct Icicle {
    int    x, y;
    double length, phase, period;
};
struct Frozen final : AnalyticKernel {
    const status::Shape             shape;
    std::vector<Fern>               ferns;
    std::vector<Icicle>             icicles;
    std::vector<std::array<int, 3>> snow;
    Point                           mouth;
    explicit Frozen(const PixelFxSettings& o) : shape(o.actor) {
        const int                       H = shape.height, W = shape.width;
        Rng                             r(o.seed * 11u + 1u);
        std::vector<std::array<int, 3>> candidates;
        for (const auto& q : shape.side)
            if (q[1] > H * .2 && q[1] < H * .85) candidates.push_back(q);
        const auto order = status::frozenRootOrder(int(candidates.size()), r);
        for (size_t i = 0; i < std::min(size_t(6), order.indices.size()); ++i) {
            const auto root = candidates[size_t(order.indices[i])];
            double     x = root[0], y = root[1], angle = root[2] < 0 ? -.5 : PI + .5;
            angle += r.range(-.4, .4);
            for (int s = 0; s < 9; ++s) {
                x += std::cos(angle) * 1.1;
                y += std::sin(angle) * 1.1;
                const int px = int(jsRound(x)), py = int(jsRound(y));
                if (!shape.isOccupied(px, py)) break;
                ferns.push_back({px, py, double(s) / 9});
                if (s % 2 == 1)
                    for (int side : {-1, 1}) {
                        const double branch = angle + side * 1.05,
                                     length = 1 + jsRound(r.range(0, 2) * (1 - double(s) / 9));
                        for (int k = 1; k <= length; ++k) {
                            const int bx = int(jsRound(x + std::cos(branch) * k)),
                                      by = int(jsRound(y + std::sin(branch) * k));
                            if (shape.isOccupied(bx, by)) ferns.push_back({bx, by, (s + k * .6) / 9});
                        }
                    }
                angle += r.range(-.25, .25);
            }
        }
        std::vector<std::array<int, 3>> hangs;
        for (const auto& q : shape.body) {
            const int x = q[0], y = q[1];
            if (shape.isOccupied(x, y + 1) || !shape.isOccupied(x, y - 1) || y >= H * .72 || y <= 4) continue;
            int clear = 0;
            while (clear < 7 && y + 1 + clear < H && !shape.isOccupied(x, y + 1 + clear)) ++clear;
            if (clear >= 4) hangs.push_back({x, y, clear});
        }
        for (int category = 0; category < 6; ++category) {
            std::optional<std::array<int, 3>> best;
            for (const auto& q : hangs) {
                const int  x = q[0], y = q[1];
                const bool matches =
                    category < 2 ? y < H * .36
                    : category < 4
                        ? y >= H * .36 && y < H * .5 && (category == 2 ? x < double(W) / 2 : x >= double(W) / 2)
                        : y >= H * .5 && (category == 4 ? x < double(W) / 2 : x >= double(W) / 2);
                const bool smaller = category == 0 || category == 3 || category == 4;if(matches&&(!best||(smaller?x<(*best)[0]:x>(*best)[0])))best=q;
            }
            if (best) {
                const double length = std::min(double((*best)[2] - 1), r.range(3, 5.5)), phase = r.next(),
                             period = icicles.size() % 2 ? 1. : 2.;
                icicles.push_back({(*best)[0], (*best)[1] + 1, length, phase, period});
            }
        }
        for (const auto& q : shape.up)
            if (q[1] < H * .6) snow.push_back({q[0], q[1], 1 + (rand({uint32_t(q[0]), o.seed, 3u}) < .45 ? 1 : 0)});
        const auto eyes = status::eyes(o.actor, shape);
        mouth = eyes.size() == 2
                    ? Point{double(eyes[0].x1 + eyes[1].x0 + 1) / 2, double(std::max(eyes[0].y1, eyes[1].y1) + 2)}
                    : Point{double(W) / 2, H * .38};
    }
    PixelFxActorVisibility actorVisibility(double) const override { return PixelFxActorVisibility::Hidden; }
    void                   drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C     = o.palette;
        const double reach = 13 + 3.5 * std::sin((t / D) * TAU);
        p.shader(-28, -3, 28, 6, [&](double x, double y, ShadeOutput& out) {
            const double d = jsHypot2(x / 24, (y - .5) / 4);
            if (d < 1) {
                out.l = C[4];
                out.k = .24 * (1 - d) * (1 - d);
            }
        });
        for (int x = -int(std::ceil(reach)) - 1; x <= reach + 1; ++x) {
            const double q = std::abs(x + .5) / reach, h = rand({uint32_t(x), 17u}), k = 1 - q * q;
            if (q > 1 + h * .15) continue;
            const bool twinkle = std::sin((t / D) * TAU * 2 + rand({uint32_t(x), 3u}) * TAU) > .94;
            p.px(x, 0, twinkle || h < .3 ? C[7] : q > .85 ? C[5] : C[6]);
            if (h < .3 * k) p.px(x, -1, C[6]);
            for (int j = 1; j <= 1 + jsRound(2 * k); ++j)
                p.dot(x, j, j == 1 ? C[5] : C[4], (1.05 - double(j) / 4) * .85);
        }
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C = o.palette;
        const auto&  a = o.actor;
        const double H = shape.height, phase = wrap(t, 1);
        const int    dx = phase < .22 ? (int(std::floor(t * 50)) % 2 ? 1 : -1) : 0;
        const auto   X  = [&](double x) { return a.x + x + dx; };
        const auto   Y  = [&](double y) { return a.y + y; };
        for (const auto& q : shape.body) {
            const int      x = q[0], y = q[1];
            const uint32_t color = status::colorAt(a, x, y),
                           ice   = C[size_t(std::clamp(1 + int(jsRound(luma(color) * 5.6)), 1, 7))];
            const bool rim = !shape.isOccupied(x - 1, y) || !shape.isOccupied(x, y - 1) || !shape.isOccupied(x + 1, y);
            p.px(X(x), Y(y),
                 rim && luma(color) < .3 ? (y < H * .5 || !shape.isOccupied(x - 1, y) ? C[5] : C[3])
                                         : mix(color, ice, .38));
        }
        const double grow = .2 + .65 * smooth(.05, .95, phase);
        for (const auto& q : ferns)
            if (q.growth < grow)
                p.px(X(q.x), Y(q.y),
                     q.growth > grow - .1                            ? WHITE
                     : rand({uint32_t(q.x), uint32_t(q.y), 5u}) < .5 ? C[6]
                                                                     : C[5]);
        for (const auto& q : snow) {
            p.px(X(q[0]), Y(q[1]) - 1, C[7]);
            if (q[2] > 1) p.px(X(q[0]), Y(q[1]) - 2, rand({uint32_t(q[0]), uint32_t(q[1]), 9u}) < .5 ? C[7] : C[6]);
        }
        for (const auto& ic : icicles) {
            const double u = wrap(t / ic.period + ic.phase, 1), length = ic.length * (.55 + .45 * smooth(0, .7, u)),
                         x = X(ic.x), y0 = Y(ic.y);
            for (int j = 0; j < length; ++j) {
                const double v = j / length;
                p.px(x, y0 + j, v > .75 ? WHITE : v < .3 ? C[7] : C[6]);
                if (v < .5) p.px(x + 1, y0 + j, C[4]);
            }
            if (u > .7 && u < .86)
                p.px(x, y0 + length, C[6]);
            else if (u >= .86) {
                const double f = (u - .86) * ic.period, y = y0 + length + .5 * 760 * f * f;
                if (y < -1) {
                    p.px(x, y, C[6]);
                    p.px(x, y - 1, C[5]);
                }
            }
            p.glow(x, y0 + length * .6, 4, C[5], .18);
        }
        for (int i = 0; i < 4; ++i) {
            const double age = phase - .35 - i * .05;
            if (age < 0 || age > .65) continue;
            const double u = age / .65, x = X(mouth.x) + 1 + status::travel(16, 0, 2.2, age) + i,
                         y = Y(mouth.y) - status::travel(6, 3, 1, age) - i * .5, radius = .8 + 2.4 * std::sqrt(u);
            p.disc(x, y, radius, C[7], .5 * (1 - u) * std::min(1., u * 8));
            p.disc(x - radius * .2, y - radius * .3, radius * .5, WHITE, .4 * (1 - u));
        }
        cycle(t, D, 9, o.seed + 2u, 1.2, 1.8, [&](double age, double, Rng& r, int) {
            const double x = r.range(-14, 14) + std::sin(age * 3 + r.next() * TAU) * 1.5, y = a.y - 16 + age * 14;
            if (y > a.y + 2) return;
            p.dot(x, y, r.next() < .5 ? WHITE : C[6], std::min(1., age * 4));
        });
        if (phase < .5)
            for (int i = 0; i < 8; ++i) {
                const int    side = i % 2 ? 1 : -1;
                const double vx = side * (20 + 30 * rand({uint32_t(i), 4u})), vy = -(20 + 30 * rand({uint32_t(i), 5u})),
                             x = side * (6 + 6 * rand({uint32_t(i), 6u})) + vx * phase,
                             y = a.y + 3 * rand({uint32_t(i), 7u}) + vy * phase + .5 * 300 * phase * phase;
                p.dot(x, y, i % 3 ? C[6] : WHITE, 1.2 - phase * 2);
            }
        if (phase < .25) p.sparkle(X(jsRound(shape.width * .3)), Y(4), jsRound(3 * (1 - phase / .25)), C[6], C[5], .4);
        p.glow(0, a.y + H * .45, 30, C[4], .2 + .12 * std::exp(-phase / .2));
        p.glow(0, a.y + 2, 16, C[5], .1);
    }
};
}  // namespace
const Descriptor& frozenDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::Frozen, "status/frozen", 72, 64, 36, 54, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 4;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x151a3d), hex(0x212f6b), hex(0x2c58aa), hex(0x3a8bd8),
                              hex(0x62c8f4), hex(0xa6ecff), hex(0xdcfbff), WHITE};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& o) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Frozen>(o); },
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
