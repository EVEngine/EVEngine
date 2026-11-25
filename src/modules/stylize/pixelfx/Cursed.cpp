// eve-hot-path
// Native adaptation of pixelfx status/cursed.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/StatusEyes.h"
#include "stylize/pixelfx/StatusVisual.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                           D = 2.4, APPEAR = .5, HOLD = .75, EXIT = 1.85, GONE = 2.1;
constexpr std::array<std::string_view, 11> SKULL{"...#####...", ".#########.", "###########", "##eee#eee##",
                                                 "##eEe#eEe##", "##eee#eee##", "#####.#####", ".####.####.",
                                                 "..#######..", "..#.#.#.#..", "...#####..."};
double                                     tide(double t) { return .3 + .12 * std::sin((t / D) * TAU); }
double reach(int x, double t, uint32_t seed) { return tide(t) + .13 * tileFbm(x * .35, (t / D) * 4, 0, 4, seed, 2); }
void   trails(Paint& p, std::span<const uint32_t> C, const PixelFxActor& a, const status::Shape& shape, double t,
              bool front) {
    constexpr std::array<std::array<double, 2>, 4> spots{{{.12, -1}, {.1, 1}, {.5, -1}, {.48, 1}}};
    for (size_t j = 0; j < spots.size(); ++j) {
        if ((j % 2 == 0) != front) continue;
        const double side = spots[j][1];
        const int    my   = int(jsRound(shape.height * spots[j][0]));
        // The source explicitly substitutes [0,0] when a rounded sample row is absent.
        const auto   row = my >= 0 && my < shape.height ? shape.rows[size_t(my)] : std::array<int, 2>{0, 0};
        const double x0 = a.x + (side < 0 ? row[0] : row[1] + 1), y0 = a.y + my, life = 1.6;
        for (int i = 17; i >= 0; --i) {
            const double age = wrap(t - (double(i) / 18) * D - double(j) * .37, D), born = t - age, u = age / life;
            if (u >= 1) continue;
            const double sway = std::sin((born / D) * TAU * 2 + double(j) * 1.7) * 4 * std::min(1., u * 1.6),
                         x    = x0 + side * u * 7 + sway + (rand({uint32_t(i), uint32_t(j), 71u}) - .5) * 2.5 * u,
                         y    = y0 - age * 19 - 5 * u * u,
                         radius =
                             (1.3 + 3.6 * (1 - std::pow(1 - u, 3))) * (.7 + .6 * rand({uint32_t(i), uint32_t(j), 72u})),
                         k = smooth(0, .12, u) * (1 - smooth(.45, 1, u)) * (front ? .5 : .62);
            const uint32_t c =
                mix(rand({uint32_t(i), uint32_t(j), 70u}) < .5 ? C[4] : mix(C[4], C[3], .4), C[1], u * .85);
            p.disc(x, y, radius * 1.08, c, k * .4);
            p.disc(x + radius * .05, y + radius * .08, radius * .76, c, k * .55);
            p.disc(x - radius * .22, y - radius * .3, radius * .48, mix(c, C[6], .25), k * .38 * (1 - u * .6));
            if (i % 3 == 0) p.glow(x, y, radius * 1.6, C[4], .08 * k);
        }
    }
}
struct Cursed final : AnalyticKernel {
    const status::Shape            shape;
    const std::vector<status::Eye> eyeList;
    explicit Cursed(const PixelFxSettings& o) : shape(o.actor), eyeList(status::eyes(o.actor, shape)) {}
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C   = o.palette;
        const auto&  a   = o.actor;
        const auto   row = shape.rows[size_t(shape.height - 1)];
        const double cx  = a.x + double(row[0] + row[1] + 1) / 2,
                     rx  = double(row[1] - row[0]) / 2 + 7 + 2 * std::sin((t / D) * TAU);
        for (int x = int(std::floor(cx - rx - 1)); x <= cx + rx + 1; ++x) {
            const double q = std::abs(x + .5 - cx) / rx, n = rand({uint32_t(x), 31u});
            if (q > 1 + n * .12) continue;
            p.px(x, 0, q > .8 ? C[4] : n < .2 ? C[3] : C[2]);
            if (q < .85) p.px(x, 1, C[1]);
            if (q < .55 && n < .7) p.px(x, 2, C[1]);
        }
        p.shader(cx - rx - 6, -4, cx + rx + 6, 5, [&](double x, double y, ShadeOutput& out) {
            const double d = jsHypot2((x - cx) / (rx + 6), y / 4);
            if (d < 1) {
                out.l = C[4];
                out.k = .2 * (1 - d) * (1 - d);
            }
        });
        trails(p, C, a, shape, t, false);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        const auto& a = o.actor;
        const int   H = shape.height;
        for (const auto& q : shape.body) {
            const int      mx = q[0], my = q[1];
            const uint32_t c  = status::colorAt(a, mx, my);
            const double   up = 1 - (my + .5) / H, level = reach(mx, t, o.seed);
            if (up > level + .035) continue;
            const double x = a.x + mx, y = a.y + my;
            if (up > level - .02)
                p.px(x, y, C[6]);
            else if (up > level - .06)
                p.px(x, y, C[4]);
            else
                p.px(x, y, mix(C[1], C[3], clamp(luma(c) * 1.6)));
        }
        for (int mx = 0; mx < shape.width; ++mx) {
            const double level = reach(mx, t, o.seed);
            const int    my    = int(std::floor((1 - level) * H));
            if (shape.isOccupied(mx, my)) p.glow(a.x + mx, a.y + my, 4, C[5], .14);
        }
        for (const auto& e : eyeList) {
            for (const auto& q : e.pixels) p.px(a.x + q[0], a.y + q[1], q[1] == e.y0 ? C[7] : C[5]);
            p.glow(a.x + double(e.x0 + e.x1 + 1) / 2, a.y + double(e.y0 + e.y1 + 1) / 2, 5, C[5], .4);
        }
        trails(p, C, a, shape, t, true);
        if (t >= APPEAR && t < GONE) {
            const double   glitch = t < HOLD   ? 1 - (t - APPEAR) / (HOLD - APPEAR)
                                    : t > EXIT ? (t - EXIT) / (GONE - EXIT)
                                               : 0;
            const uint32_t f      = uint32_t(std::floor(t * 30));
            const bool     on     = glitch < .15 || rand({f, 3u}) > glitch * .6;
            const double   x = 0, y = a.y - 12 + std::sin((t / D) * TAU * 2) * 1.2;
            if (on) {
                const auto tear = [&](int dx, int dy) {
                    const int    row   = dy + 6;
                    const double shift = glitch > 0 && rand({f, uint32_t(row >> 1), 7u}) < glitch * .8
                                             ? jsRound((rand({f, uint32_t(row), 9u}) - .5) * 6 * glitch)
                                             : 0;
                    return status::bitmap(SKULL, dx - int(shift), dy);
                };
                if (glitch > 0) {
                    status::icon(p, x - 2, y, 7, tear, {0, C[2], C[2], C[3]}, .7 * glitch);
                    status::icon(p, x + 2, y, 7, tear, {0, C[6], C[6], C[6]}, .5 * glitch);
                }
                status::icon(p, x, y, 8, tear, {C[0], C[4], C[6], C[7]}, 1, [&](char ch) -> std::optional<uint32_t> {
                    if (ch == 'e') return C[0];
                    if (ch == 'E') return C[7];
                    return std::nullopt;
                });
                p.glow(x - 3, y - 1, 5, C[5], .6);
                p.glow(x + 3, y - 1, 5, C[5], .6);
                p.glow(x, y, 16, C[4], .24 + .3 * glitch);
            }
            if (glitch > .1)
                for (int k = 0; k < 3; ++k) {
                    const double ly     = y - 5 + std::floor(rand({f, uint32_t(k), 11u}) * 11),
                                 lx     = jsRound((rand({f, uint32_t(k), 12u}) - .5) * 10),
                                 length = 3 + std::floor(rand({f, uint32_t(k), 13u}) * 8);
                    p.line(lx - length / 2, ly, lx + length / 2, ly, k % 2 ? C[6] : C[4], glitch);
                }
        }
        p.glow(0, a.y + H * (1 - tide(t)), 24, C[4], .2);
        p.glow(0, a.y + H * .45, 30, C[3], .12);
    }
};
}  // namespace
const Descriptor& cursedDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::Cursed, "status/cursed", 64, 84, 32, 76, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 9;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x0c0612), hex(0x1f0f2e), hex(0x3a1a55), hex(0x5e2a85),
                              hex(0x8a3fc0), hex(0xb86ef0), hex(0xe0b0ff), WHITE};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& o) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Cursed>(o); },
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
