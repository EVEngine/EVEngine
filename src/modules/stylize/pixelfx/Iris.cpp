// eve-hot-path
// Native adaptation of pixelfx transitions/iris.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScreenTransition.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double CLOSE = .62, SNAP = .9, SHUT = .99, POP = 1.2, OPEN = 1.75, RS = 24, RW = 5, GLO = 16;
struct Iris final : Kernel {
    std::vector<uint32_t> C;
    uint32_t              ink;
    Rng                   rng;
    double                fx, fy, R0, t = 0, prevR;
    bool                  shut = false, popped = false;
    Particles             ps;
    Lights                lights;
    explicit Iris(const PixelFxSettings& o)
        : C(o.palette),
          ink(o.transition.color),
          rng(o.seed),
          fx(o.transition.focus[0]),
          fy(o.transition.focus[1]),
          R0(std::max({jsHypot2(-fx, -fy), jsHypot2(screen::W - fx, -fy), jsHypot2(-fx, screen::H - fy),
                       jsHypot2(screen::W - fx, screen::H - fy)}) +
             RW + 2),
          prevR(R0) {
        ps.sparks = {WHITE, C[6], C[5], C[4]};
    }
    double radius(double time) const {
        if (time < CLOSE) return RS + (R0 - RS) * (1 - ease::inOutCubic(time / CLOSE));
        if (time < SNAP) return RS + std::sin((time - CLOSE) * 22) * .8 * (1 - (time - CLOSE) / (SNAP - CLOSE));
        if (time < SHUT) return RS * (1 - ease::inCubic((time - SNAP) / (SHUT - SNAP)));
        if (time < POP) return 0;
        return R0 * ease::outQuart(clamp((time - POP) / (OPEN - POP)));
    }
    void update(double dt) override {
        t += dt;
        const double r = radius(t);
        if (t > CLOSE - .25 && t < SNAP && rng.isChance(.25)) {
            const double a = rng.range(0, TAU), d = std::sqrt(rng.next()) * r * .9;
            const double vx = rng.range(-6, 6), vy = -rng.range(4, 14), life = rng.range(.4, .8);
            ps.spawn(rng, ParticleKind::Ember, fx + std::cos(a) * d, fy + std::sin(a) * d, vx, vy, life, 0, 1);
        }
        if (t > CLOSE - .1 && t < SNAP && rng.isChance(.3)) {
            const double a = rng.range(0, TAU), life = rng.range(.2, .4);
            const auto   c    = rng.isChance(.5) ? WHITE : C[6];
            const double size = rng.range(1, 2.4);
            ps.spawn(rng, ParticleKind::Star, fx + std::cos(a) * (r + 1), fy + std::sin(a) * (r + 1), 0, 0, life, c,
                     size);
        }
        if (!shut && t >= SHUT) {
            shut = true;
            lights.add(fx, fy, 50, C[5], .7, .3);
        }
        if (!popped && t >= POP) {
            popped = true;
            lights.add(fx, fy, 140, C[5], .7, .45);
            lights.add(fx, fy, 50, WHITE, .9, .12);
            for (int i = 0; i < 40; ++i) {
                const double a = rng.range(0, TAU), v = rng.range(150, 420), life = rng.range(.2, .5);
                ps.spawn(rng, ParticleKind::Spark, fx + std::cos(a) * 3, fy + std::sin(a) * 3, std::cos(a) * v,
                         std::sin(a) * v, life);
            }
        }
        if (t > POP && t < OPEN - .1) {
            const double vr = (r - prevR) / dt;
            for (int k = 0; k < 2; ++k) {
                if (!rng.isChance(.6)) continue;
                const double a = rng.range(0, TAU), x = fx + std::cos(a) * r, y = fy + std::sin(a) * r;
                if (x < 0 || y < 0 || x >= screen::W || y >= screen::H) continue;
                const double life = std::min(rng.range(.2, .45), 2.2 - .05 - t);
                const auto   c    = rng.isChance(.5) ? C[6] : C[5];
                ps.spawn(rng, ParticleKind::Mote, x, y, std::cos(a) * vr * .35, std::sin(a) * vr * .35, life, c, .7);
            }
        }
        prevR = r;
        ps.update(dt, rng, [](double) { return INFINITY; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p0) const override {
        auto         p   = screen::clip(p0);
        const double r   = radius(t);
        const double hot = t >= POP   ? 1 - smooth(POP, OPEN, t)
                           : t < SNAP ? .45 + .25 * smooth(CLOSE - .2, CLOSE, t)
                                      : .7;
        const double rw  = RW + (t >= POP ? 3 * hot : 0);
        if (t < OPEN + .05 && !(t >= SHUT && t < POP && r <= 0)) {
            for (int y = 0; y < screen::H; ++y) {
                const double dy = y + .5 - fy, xo = std::sqrt(std::max(0., (r + rw) * (r + rw) - dy * dy));
                if (xo <= 0) {
                    screen::rect(p0, 0, y, screen::W, 1, ink);
                    continue;
                }
                const int a0 = int(clamp(std::floor(fx - xo), 0, screen::W)),
                          b1 = int(clamp(std::ceil(fx + xo), 0, screen::W));
                if (a0 > 0) screen::rect(p0, 0, y, a0, 1, ink);
                if (b1 < screen::W) screen::rect(p0, b1, y, screen::W - b1, 1, ink);
                const double g = r > GLO ? std::sqrt(std::max(0., (r - GLO) * (r - GLO) - dy * dy)) : 0;
                for (int x = a0; x < b1; ++x) {
                    const double dx = x + .5 - fx;
                    if (std::abs(dx) < g) continue;
                    const double d = jsHypot2(dx, dy) - r;
                    if (d >= 0) {
                        const double q = d / rw, lit = .5 - .5 * (dx * .7 + dy * .7) / (r + d + .01),
                                     h = hot * (.55 + .6 * lit);
                        p0.px(x, y,
                              q >= 1    ? ink
                              : q < .2  ? (h > .5   ? WHITE
                                           : h > .3 ? C[6]
                                                    : C[5])
                              : q < .4  ? (h > .38 ? C[6] : C[5])
                              : q < .65 ? (h > .3 ? C[5] : C[4])
                              : q < .85 ? C[3]
                                        : C[2]);
                        if (q < .5) p0.light(x, y, C[5], .45 * h * (1 - q * 2));
                    } else if (d > -GLO)
                        p0.light(x, y, C[5], (.18 + .3 * hot) * std::pow(1 + d / GLO, 2));
                }
            }
        } else if (t >= SHUT && t < POP)
            screen::rect(p0, 0, 0, screen::W, screen::H, ink);
        if (r > 0 && r < 80 && t < SHUT) {
            const double k = 1 - r / 80;
            p.glow(fx, fy, r * 1.15 + 6, C[5], .18 + .32 * k);
            p.glow(fx, fy, r * .6 + 3, C[6], .25 * k);
        }
        if (t > .08 && t < SNAP) {
            const double a = -2.4 + ((t - .08) / (SNAP - .08)) * TAU, gx = fx + std::cos(a) * (r + 1),
                         gy = fy + std::sin(a) * (r + 1);
            if (gx > 2 && gy > 2 && gx < screen::W - 2 && gy < screen::H - 2)
                screen::glint(p, gx, gy, 7 + 3 * std::sin(t * 9), t * 3, C[6], C[5], .9);
        }
        const double s = t - SHUT;
        if (s > -.06 && s < .22) screen::glint(p, fx, fy, 14 * (1 - std::abs(s + .01) / .23), s * 4, C[6], C[5]);
        const double u = t - POP;
        if (u >= 0 && u < .18) {
            screen::glint(p, fx, fy, 20 * (1 - u / .18), .4 - u * 3, C[6], C[5]);
            p.glow(fx, fy, 60, WHITE, .6 * (1 - u / .18));
        }
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& irisDescriptor() {
    static const std::array<Option, 2> options{
        {{"color", 0, 4294967295., false, 0, &PixelFxTransitionSettings::color, true},
         {"focus", -4096, 4096, false, 0, &PixelFxTransitionSettings::focus}}};
    static const Descriptor d{
        {PixelFxKind::Iris, "transitions/iris", 320, 180, 0, 0, 2.2, false},
        8,
        [](PixelFxSettings& s) {
            s.seed             = 1;
            s.transition.color = hex(0x0b0a10);
            s.transition.focus = {160, 100};
            s.palette          = {hex(0x1c140c), hex(0x3b2a18), hex(0x6e5233), hex(0xa8875a),
                                  hex(0xd9bd8a), hex(0xf3e2bd), hex(0xfff7e8), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Iris>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
