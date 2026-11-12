// eve-hot-path
// Native adaptation of pixelfx transitions/star-iris.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScreenTransition.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double CLOSE = .98, OPEN0 = 1.16, OPEN1 = 2, RMAX = 430, INNER = .46, RW = 4;
struct StarState {
    double r, a;
};
StarState starAt(double t) {
    if (t < CLOSE)
        return {RMAX * std::pow(1 - ease::inOutQuad(t / CLOSE), 1.3),
                -PI / 2 + ease::inOutCubic(t / CLOSE) * 1.6 * TAU};
    if (t < OPEN0) return {0, 0};
    const double u = clamp((t - OPEN0) / (OPEN1 - OPEN0));
    return {RMAX * std::pow(ease::inOutCubic(u), 1.15), -PI / 2 - ease::inOutCubic(u) * 1.6 * TAU};
}
struct StarIris final : Kernel {
    std::vector<uint32_t> C;
    uint32_t              ink;
    Rng                   rng;
    double                fx, fy, t = 0;
    bool                  shut = false, opened = false;
    StarState             prev = starAt(0);
    Particles             ps;
    Lights                lights;
    explicit StarIris(const PixelFxSettings& o)
        : C(o.palette), ink(o.transition.color), rng(o.seed), fx(o.transition.focus[0]), fy(o.transition.focus[1]) {
        ps.sparks = {WHITE, C[6], C[5], C[4]};
    }
    std::array<Point, 10> points(StarState s) const {
        std::array<Point, 10> result{};
        for (int i = 0; i < 10; ++i) {
            const double r = i % 2 ? s.r * INNER : s.r, a = s.a + (i * PI) / 5;
            result[size_t(i)] = {fx + std::cos(a) * r, fy + std::sin(a) * r};
        }
        return result;
    }
    void update(double dt) override {
        t += dt;
        const auto   s    = starAt(t);
        const auto   pts  = points(s);
        const double spin = (s.a - prev.a) / dt;
        if (s.r > 2)
            for (int i = 0; i < 10; i += 2) {
                const auto [x, y] = pts[size_t(i)];
                if (x < 0 || y < 0 || x >= screen::W || y >= screen::H || !rng.isChance(.35)) continue;
                const double an = s.a + (i * PI) / 5, v = spin * s.r * .25;
                const auto   kind = rng.isChance(.6) ? ParticleKind::Star : ParticleKind::Mote;
                const double life = std::min(rng.range(.25, .55), 2.2 - .05 - t);
                const auto   c    = rng.isChance(.5) ? C[6] : C[5];
                const double size = rng.range(.6, 1.8);
                ps.spawn(rng, kind, x, y, -std::sin(an) * v + std::cos(an) * 20, std::cos(an) * v + std::sin(an) * 20,
                         life, c, size);
            }
        if (!shut && t >= CLOSE) {
            shut = true;
            lights.add(fx, fy, 60, C[5], .7, .3);
        }
        if (!opened && t >= OPEN0) {
            opened = true;
            lights.add(fx, fy, 120, C[5], .7, .4);
            lights.add(fx, fy, 40, WHITE, .8, .12);
            for (int i = 0; i < 24; ++i) {
                const double a = rng.range(0, TAU), v = rng.range(60, 200), life = rng.range(.3, .6);
                const auto   c    = rng.isChance(.5) ? WHITE : C[6];
                const double size = rng.range(.6, 2);
                ps.spawn(rng, ParticleKind::Star, fx, fy, std::cos(a) * v, std::sin(a) * v, life, c, size);
            }
        }
        prev = s;
        ps.update(dt, rng, [](double) { return INFINITY; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p0) const override {
        auto       p = screen::clip(p0);
        const auto s = starAt(t);
        if (t >= CLOSE && t < OPEN0)
            screen::rect(p0, 0, 0, screen::W, screen::H, ink);
        else if (s.r < RMAX - 2) {
            const auto pts = points(s);
            for (int y = 0; y < screen::H; ++y) {
                const double           yc = y + .5;
                std::array<double, 10> xs{};
                size_t                 count = 0;
                for (int i = 0, j = 9; i < 10; j = i++) {
                    const auto [xi, yi] = pts[size_t(i)];
                    const auto [xj, yj] = pts[size_t(j)];
                    if ((yi > yc) != (yj > yc)) xs[count++] = xi + ((yc - yi) * (xj - xi)) / (yj - yi);
                }
                std::sort(xs.begin(), xs.begin() + std::ptrdiff_t(count));
                int x = 0;
                for (size_t k = 0; k + 1 < count; k += 2) {
                    const int a = int(clamp(jsRound(xs[k]), 0, screen::W));
                    if (a > x) screen::rect(p0, x, y, a - x, 1, ink);
                    x = std::max(x, int(clamp(jsRound(xs[k + 1]), 0, screen::W)));
                }
                if (x < screen::W) screen::rect(p0, x, y, screen::W - x, 1, ink);
            }
            for (int i = 0, j = 9; i < 10; j = i++) {
                const auto [ax, ay] = pts[size_t(j)];
                const auto [bx, by] = pts[size_t(i)];
                const double L      = jsHypot2(bx - ax, by - ay);
                if (L < .5) continue;
                const double ux = (bx - ax) / L, uy = (by - ay) / L, nx = -uy, ny = ux;
                const bool   lit = nx * -.6 + ny * -.8 > .1;
                for (double sx = 0; sx <= L; sx += .5) {
                    const double x0 = ax + ux * sx, y0 = ay + uy * sx;
                    if (x0 < -RW || y0 < -RW || x0 > screen::W + RW || y0 > screen::H + RW) continue;
                    for (double d = 0; d < RW; d += .5) {
                        const auto c = d < 1 ? (lit ? WHITE : C[6]) : d < 2 ? C[5] : d < 3 ? C[4] : C[3];
                        p.px(x0 + nx * d, y0 + ny * d, c);
                    }
                    if (std::fmod(sx, 2) == 0) p.light(x0, y0, C[5], .5);
                }
            }
            for (int i = 0; i < 10; i += 2) {
                const auto [x, y] = pts[size_t(i)];
                if (x < 2 || y < 2 || x > screen::W - 2 || y > screen::H - 2) continue;
                screen::glint(p, x, y, 5 + 3 * std::sin(t * 13 + i), t * 2 + i, C[6], C[5]);
            }
            if (s.r < 140) p.glow(fx, fy, s.r * .7 + 8, C[5], .35 * (1 - s.r / 140));
        }
        const double u = t - CLOSE;
        if (u > -.05 && u < .2) screen::glint(p, fx, fy, 14 * (1 - std::abs(u) / .2), u * 4, C[6], C[5]);
        const double v = t - OPEN0;
        if (v >= 0 && v < .16) {
            screen::glint(p, fx, fy, 18 * (1 - v / .16), .3 - v * 5, C[6], C[5]);
            p.glow(fx, fy, 50, WHITE, .5 * (1 - v / .16));
        }
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& starIrisDescriptor() {
    static const std::array<Option, 2> options{
        {{"color", 0, 4294967295., false, 0, &PixelFxTransitionSettings::color, true},
         {"focus", -4096, 4096, false, 0, &PixelFxTransitionSettings::focus}}};
    static const Descriptor d{
        {PixelFxKind::StarIris, "transitions/star-iris", 320, 180, 0, 0, 2.2, false},
        8,
        [](PixelFxSettings& s) {
            s.seed             = 1;
            s.transition.color = hex(0x0b0a10);
            s.transition.focus = {160, 96};
            s.palette          = {hex(0x1e1206), hex(0x4a2c08), hex(0x86520c), hex(0xc48412),
                                  hex(0xf0b424), hex(0xffe066), hex(0xfff6c4), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<StarIris>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
