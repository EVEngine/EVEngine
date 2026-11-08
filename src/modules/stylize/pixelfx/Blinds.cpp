// eve-hot-path
// Native adaptation of pixelfx transitions/blinds.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScreenTransition.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr int    N  = 12;
constexpr double SH = 15, SHUT0 = .08, OPEN0 = 1.02, STEP = .038, TURN = .42, OSTEP = .03, OTURN = .4, FADE0 = 1.72,
                 FADE1 = 1.94;
double           angle(int i, double t) {
    return t < OPEN0 ? (PI / 2) * ease::outBack(clamp((t - SHUT0 - i * STEP) / TURN), 1.6)
                     : (PI / 2) * (1 - ease::inOutCubic(clamp((t - OPEN0 - i * OSTEP) / OTURN)));
}
struct Blinds final : Kernel {
    const std::vector<uint32_t> C;
    const uint32_t              ink;
    Rng                         rng;
    Particles                   ps;
    double                      t = 0;
    explicit Blinds(const PixelFxSettings& o) : C(o.palette), ink(o.transition.color), rng(o.seed) {
        ps.sparks = {WHITE, C[6], C[5], C[4]};
    }
    void update(double dt) override {
        t += dt;
        if (t < FADE0 && rng.isChance(.8)) {
            const int    i  = rng.integer(0, N - 1);
            const double th = angle(i, t);
            if (th > .2 && th < 1.45) {
                const double x = rng.range(0, screen::W), y = (i + 1) * SH + rng.range(-2, 2), vx = rng.range(-8, 8),
                             vy = -rng.range(2, 8), life = std::min(rng.range(.5, 1), 2. - .05 - t);
                ps.spawn(rng, ParticleKind::Ember, x, y, vx, vy, life, 0, 1);
            }
        }
        ps.update(dt, rng, [](double) { return INFINITY; }, [](double, double) {});
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p0) const override {
        auto                           p = screen::clip(p0);
        std::array<uint8_t, screen::H> open;
        open.fill(1);
        for (int i = 0; i < N; ++i) {
            const double h = std::max(1., (SH + 1) * std::abs(std::sin(angle(i, t)))), yc = (i + .5) * SH;
            for (int y = std::max(0, int(std::ceil(yc - h / 2 - .5)));
                 y <= std::min(screen::H - 1, int(std::floor(yc + h / 2 - .5))); ++y)
                open[size_t(y)] = 0;
        }
        for (const auto& q : ps.list) {
            const int    y = int(std::floor(q.y));
            const double k = q.life / q.max;
            if (q.delay > 0 || y < 0 || y >= screen::H || !open[size_t(y)] || q.x < 0 || q.x >= screen::W) continue;
            const bool tw = std::fmod(q.seed, 1) < .3 && std::sin(t * 14 + q.seed) > 0;
            p0.px(q.x, y, tw || k > .5 ? WHITE : C[5]);
            if (tw)
                for (const auto& point : CROSS) {
                    const int yy = y + int(point.y);
                    if (yy >= 0 && yy < screen::H && open[size_t(yy)]) p.px(q.x + point.x, yy, C[6]);
                }
            p0.light(q.x, y, C[6], (tw ? .7 : .4) * k);
        }
        const double fade = 1 - clamp((t - FADE0) / (FADE1 - FADE0)), appear = clamp(t / SHUT0);
        for (int i = 0; i < N; ++i) {
            const double th = angle(i, t), s = std::sin(th), c = std::cos(th), yc = (i + .5) * SH,
                         h     = std::max(1., (SH + 1) * std::abs(s));
            const int    y0    = std::max(0, int(std::ceil(yc - h / 2 - .5))),
                         y1    = std::min(screen::H - 1, int(std::floor(yc + h / 2 - .5)));
            const bool   under = th > PI / 2;
            if (h < 2.5) {
                const double k = std::min(appear, fade);
                if (k <= 0) continue;
                const int yy = int(jsRound(yc - .5));
                screen::rect(p0, 0, yy, screen::W, 1, k > .66 ? C[6] : k > .33 ? C[4] : C[2]);
                if (h >= 1.5 && yy + 1 < screen::H) screen::rect(p0, 0, yy + 1, screen::W, 1, k > .5 ? C[3] : C[1]);
                for (int x = 0; x < screen::W; x += 2) p0.light(x, yy, C[5], .18 * k);
                continue;
            }
            const double lit = under ? 0 : c;
            for (int y = y0; y <= y1; ++y) {
                const int    r    = y - y0;
                const bool   last = y == y1;
                const double v = double(r) / std::max(1, y1 - y0), f = lit * (1 - v * .8);
                const auto   col = r == 0    ? (lit > .55   ? WHITE
                                                : lit > .3  ? C[6]
                                                : lit > .12 ? C[4]
                                                            : ink)
                                   : r == 1  ? (lit > .45  ? C[5]
                                                : lit > .2 ? C[3]
                                                           : ink)
                                   : last    ? ink
                                   : f > .55 ? C[3]
                                   : f > .35 ? C[2]
                                   : f > .15 ? C[1]
                                             : ink;
                screen::rect(p0, 0, y, screen::W, 1, col);
                if (r == 0 && lit > .12)
                    for (int x = 0; x < screen::W; x += 2) p0.light(x, y, C[5], .35 * lit);
            }
            const double gap = SH - h;
            if (gap > .5 && th > .12 && !under) {
                const double k  = (.2 + .45 * std::pow(1 - gap / SH, 1.5)) * std::min(1., th / .4);
                const int    gy = y1 + 1;
                for (int y = gy; y < std::min(double(screen::H), gy + std::ceil(gap)); ++y) {
                    const double v = (y - gy + .5) / gap, kk = k * (1 - std::abs(v - .5) * 1.2);
                    if (kk > .01)
                        for (int x = 0; x < screen::W; ++x) {
                            p0.light(x, y, C[6], kk);
                            p0.light(x, y, C[4], kk * .5);
                        }
                }
            }
            const double u = t < OPEN0 ? clamp((t - SHUT0 - i * STEP) / TURN) : clamp((t - OPEN0 - i * OSTEP) / OTURN);
            if (lit > .3 && u > .05 && u < .95)
                screen::glint(p, screen::W * (t < OPEN0 ? u : 1 - u), y0, 6 * std::sin(PI * u), u * 3, C[6], C[5]);
        }
    }
};
}  // namespace
const Descriptor& blindsDescriptor() {
    static const std::array<Option, 1> options{
        {{"color", 0, 4294967295., false, 0, &PixelFxTransitionSettings::color, true}}};
    static const Descriptor d{
        {PixelFxKind::Blinds, "transitions/blinds", 320, 180, 0, 0, 2, false},
        8,
        [](PixelFxSettings& s) {
            s.seed             = 1;
            s.transition.color = hex(0x0b0a10);
            s.palette          = {hex(0x1a120c), hex(0x33241a), hex(0x5a4030), hex(0x8a6848),
                                  hex(0xc09a6a), hex(0xe8cc98), hex(0xfff0cc), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Blinds>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
