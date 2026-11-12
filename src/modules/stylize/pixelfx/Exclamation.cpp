// eve-hot-path
// Native adaptation of pixelfx ui/exclamation.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double HEAD = -40, REST = -45, POP = 1.1, END = 1.4 - .05;
struct Exclamation final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         R;
    Particles                   ps;
    Lights                      lights;
    double                      t = 0, y = HEAD, vy = -420, squash = 0;
    bool                        landed = false, popped = false, sprung = false;
    explicit Exclamation(const PixelFxSettings& o) : C(o.palette), R(o.seed) { ps.sparks = {C[7], C[6], C[5], C[3]}; }
    void update(double dt) override {
        t += dt;
        if (t < POP) {
            vy += (-(y - REST) * 900 - vy * 14) * dt;
            y += vy * dt;
            if (!landed && vy > 0) {
                landed = true;
                squash = 1;
            }
        }
        squash = std::max(0., squash - dt / .12);
        const std::array<uint32_t, 3> colors{C[7], C[6], C[5]};
        if (!sprung && t >= .04) {
            sprung = true;
            lights.add(0, HEAD - 4, 26, C[5], .6, .25);
            for (int i = 0; i < 8; ++i) {
                const double a = -PI / 2 + R.range(-1.3, 1.3), speed = R.range(40, 90), x = R.range(-4, 4),
                             life  = R.range(.2, .4);
                const auto   color = R.pick<uint32_t>(colors);
                ps.spawn(R, ParticleKind::Star, x, HEAD - 2, std::cos(a) * speed, std::sin(a) * speed, life, color,
                         i % 3 ? .6 : 1.3);
            }
        }
        if (!popped && t >= POP + .05) {
            popped = true;
            lights.add(0, REST - 13, 28, C[5], .5, .2);
            for (int i = 0; i < 7; ++i) {
                const double a = (double(i) / 7) * TAU + R.range(-.2, .2), speed = R.range(30, 60),
                             life  = std::min(R.range(.2, .35), END - t);
                const auto   color = R.pick<uint32_t>(colors);
                ps.spawn(R, ParticleKind::Star, std::cos(a) * 5, REST - 13 + std::sin(a) * 9, std::cos(a) * speed,
                         std::sin(a) * speed, life, color, i % 3 ? .6 : 1.4);
            }
        }
        ps.update(dt, R, [](double) { return INFINITY; }, [](double, double) {});
        lights.update(dt);
    }
    void bang(Paint& p, double sx, double sy, double angle, bool white) const {
        const double ca = std::cos(angle), sa = std::sin(angle);
        const auto   hw     = [](double v) { return 2.3 + (-v - 10) * .09; };
        const auto   inBar  = [&](double u, double v) { return v > -24 && v < -9.5 && std::abs(u) < hw(v); };
        const auto   inDot  = [](double u, double v) { return jsHypot2(u, v + 3.6) < 3.3; };
        const auto   inside = [&](double u, double v) { return inBar(u, v) || inDot(u, v); };
        p.shader(-16, y - 34, 16, y + 6, [&](double px, double py, ShadeOutput& out) {
            const double dx = px, dy = py - y, u = (dx * ca + dy * sa) / sx, v = (-dx * sa + dy * ca) / sy, ex = 1 / sx,
                         ey = 1 / sy;
            if (!inside(u, v)) {
                if (inside(u - ex, v) || inside(u + ex, v) || inside(u, v - ey) || inside(u, v + ey)) out.c = C[0];
                return;
            }
            if (white) {
                out.c = WHITE;
                return;
            }
            if (inDot(u, v)) {
                out.c = jsHypot2(u + 1.2, v + 4.8) < 1.2 ? WHITE
                        : u + v + 3.6 < -.5              ? C[6]
                        : u + v + 3.6 < 1.5              ? C[5]
                                                         : C[4];
                return;
            }
            const double w = hw(v), q = (u + w) / (2 * w);
            out.c = q < .28 ? (v < -20 ? WHITE : C[7]) : q > .74 ? C[4] : v < -18 ? C[6] : C[5];
        });
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        for (const auto& beat : std::array<std::array<double, 2>, 2>{{{.04, 6}, {.48, 4.5}}}) {
            const double u = (t - beat[0]) / .24;
            if (u <= 0 || u >= 1) continue;
            for (double side : {-1., 1.})
                for (int i = 0; i < 3; ++i) {
                    const double angle = -PI / 2 + side * (.55 + i * .42), r0 = 14 + 7 * ease::outCubic(u),
                                 L = beat[1] * std::sin(u * PI) * (i == 1 ? 1 : .75), ca = std::cos(angle),
                                 sa = std::sin(angle);
                    if (L < .8) continue;
                    p.lineFn(
                        ca * r0, REST - 13 + sa * r0, ca * (r0 + L), REST - 13 + sa * (r0 + L),
                        [&](double q) { return q < .5 ? WHITE : C[5]; }, 1.);
                }
        }
        const double pu = (t - POP) / .06;
        if (pu < 1) {
            const double speed = std::min(1., std::abs(vy) / 420), up = vy < 0 ? speed : 0;
            double       sx = 1 - .3 * up + .3 * squash, sy = 1 + .45 * up - .28 * squash;
            if (t < .05) {
                sx *= t / .05;
                sy *= t / .05;
            }
            if (pu > 0) sx = sy = 1 + .3 * pu;
            const double wob   = .28 * std::exp(-t * 4) * std::sin(t * 26);
            const bool   white = t < .06 || pu > 0;
            if (sx > .1) bang(p, sx, sy, wob, white);
            p.glow(0, y - 13, 20, C[5], .35 + (white ? .3 : 0));
            if (t > .6 && t < .85) p.sparkle(-3, y - 22, 3.5 * std::sin(((t - .6) / .25) * PI), C[7], C[6], .4);
        }
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& exclamationDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::Exclamation, "ui/exclamation", 84, 104, 42, 92, 1.4, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 3;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x2a1405), hex(0x6a2e06), hex(0xb85a0a), hex(0xf08c14),
                              hex(0xffc23a), hex(0xffe580), hex(0xfff6c8), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Exclamation>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
