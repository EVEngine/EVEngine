// eve-hot-path
// Native adaptation of pixelfx status/charmed.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/StatusEyes.h"
#include "stylize/pixelfx/StatusVisual.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                          D = 2.4, BEAT = D / 2;
constexpr std::array<std::string_view, 5> HEART5{".#.#.", "#####", "#####", ".###.", "..#.."};
constexpr std::array<std::string_view, 6> HEART7{".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};
double                                    thump(double phase) {
    return std::exp(-std::pow((phase - .04) / .07, 2)) + .6 * std::exp(-std::pow((phase - .26) / .07, 2));
}
void hearts(Paint& p, const PixelFxSettings& o, double t, bool back) {
    const auto& C = o.palette;
    const auto& a = o.actor;
    cycle(t, D, 12, o.seed + 4u, 1.3, 1.9, [&](double age, double life, Rng& r, int i) {
        if ((i % 3 == 0) != back) return;
        const double u = age / life, side = r.next() < .5 ? -1 : 1, phase = r.range(0, TAU);
        const bool   big    = r.next() < .3;
        const double spread = r.range(4, 15), x = side * spread + std::sin(age * 4 + phase) * 2.2, y0 = r.range(8, 30),
                     speed = r.range(14, 22), y = a.y + y0 - age * speed, k = std::min(1., u * 6);
        if (u > .88) {
            const double q = (u - .88) / .12;
            p.sparkle(x, y, jsRound(2 * (1 - q)), C[5], C[4], .4 * (1 - q));
            return;
        }
        const std::array<uint32_t, 4> ramp =
            back ? std::array<uint32_t, 4>{C[0], C[1], C[2], C[3]} : std::array<uint32_t, 4>{C[0], C[2], C[3], C[4]};
        status::icon(
            p, x, y, 4,
            [&](int dx, int dy) { return big ? status::bitmap(HEART7, dx, dy) : status::bitmap(HEART5, dx, dy); }, ramp,
            k);
        if (!back) p.px(x - (big ? 2 : 1), y - 1, C[6]);
        p.glow(x, y, 6, C[3], (back ? .12 : .22) * k);
    });
}
struct Charmed final : AnalyticKernel {
    const std::vector<status::Eye> eyeList;
    explicit Charmed(const PixelFxSettings& o) : eyeList(status::eyes(o.actor, status::Shape(o.actor))) {}
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const double th = thump(wrap(t, BEAT));
        p.glow(0, o.actor.y - 8, 26 + 8 * th, o.palette[3], .16 + .22 * th);
        hearts(p, o, t, true);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C     = o.palette;
        const auto&  a     = o.actor;
        const double phase = wrap(t, BEAT), th = thump(phase);
        for (const auto& e : eyeList) {
            for (const auto& q : e.pixels) p.px(a.x + q[0], a.y + q[1], q[1] == e.y0 ? C[3] : C[2]);
            p.px(a.x + e.x0, a.y + e.y0 + 1, C[6]);
            const uint32_t blush = mix(e.skin, C[3], .55);
            const int      out = e.x0 < double(a.width) / 2 ? -1 : 1, bx = out < 0 ? e.x0 - 1 : e.x1;
            p.px(a.x + bx, a.y + e.y1 + 1, blush);
            p.px(a.x + bx + 1, a.y + e.y1 + 1, blush);
        }
        hearts(p, o, t, false);
        const double hx = 0, hy = a.y - 10, size = 5.6 * (1 + .24 * th);
        for (const auto pulse : std::array<std::array<double, 2>, 2>{{{.04, 1}, {.26, .6}}}) {
            const double u = (phase - pulse[0]) / .5;
            if (u < 0 || u > 1) continue;
            const double S      = 5.6 * (1.25 + 1.3 * u);
            const auto   outer  = status::shaped(status::isInsideHeart, S),
                         inner  = status::shaped(status::isInsideHeart, S - 1.3);
            const int    extent = int(std::ceil(S * 1.2)) + 1;
            for (int dy = -extent; dy <= extent; ++dy)
                for (int dx = -extent; dx <= extent; ++dx)
                    if (outer(dx, dy) && !inner(dx, dy))
                        p.dot(hx + dx, hy + dy, u < .3 ? C[5] : C[4], (1 - u) * pulse[1] * 1.3);
        }
        status::icon(p, hx, hy, 9, status::shaped(status::isInsideHeart, size), {C[0], C[2], C[3], C[4]});
        p.px(hx - jsRound(size * .55), hy - jsRound(size * .45), C[6]);
        p.px(hx - jsRound(size * .55) + 1, hy - jsRound(size * .45), C[5]);
        p.px(hx - jsRound(size * .55), hy - jsRound(size * .45) + 1, C[5]);
        p.glow(hx, hy, 12 + 6 * th, C[4], .3 + .4 * th);
        if (th > .5) p.sparkle(hx + size * .9, hy - size * .8, jsRound(3 * th), C[5], C[4], .4 * th);
        cycle(t, D, 8, o.seed + 2u, .3, .5, [&](double age, double life, Rng& r, int) {
            const double angle = r.range(0, TAU), d = r.range(8, 15), u = age / life;
            p.sparkle(hx + std::cos(angle) * d, hy + std::sin(angle) * d * .8, jsRound(1.6 * std::sin(PI * u)), C[5],
                      C[4], .3 * std::sin(PI * u));
        });
    }
};
}  // namespace
const Descriptor& charmedDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::Charmed, "status/charmed", 60, 76, 30, 68, D, true},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 8;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x3a0a1e), hex(0x7a1238), hex(0xc4245a), hex(0xf04e86),
                              hex(0xff8ab0), hex(0xffc8da), WHITE};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& o) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Charmed>(o); },
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
