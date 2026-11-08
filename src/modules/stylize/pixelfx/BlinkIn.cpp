// eve-hot-path
// Native adaptation of pixelfx arcane/blink-in.js, MIT.
// Sprite Fusion blink, Copyright (c) 2026 Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Arcane.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double LINE = .26, GROW = .1, OPEN = .12, ARRIVE = LINE + GROW + OPEN;
struct Bit {
    double   tx, ty, ox, oy, t0, t1, spin;
    uint32_t color;
};
struct Sample {
    double x, y, k;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct BlinkIn final : Kernel {
    const std::vector<uint32_t>   C;
    Rng                           R;
    const PixelFxActor            actor;
    const std::vector<uint8_t>    sh;
    const double                  mx, my;
    const std::array<uint32_t, 5> cols;
    std::array<Bit, 230>          bits{};
    Particles                     ps;
    Lights                        lights;
    double                        t       = 0;
    bool                          arrived = false;
    explicit BlinkIn(const PixelFxSettings& o)
        : C(o.palette),
          R(o.seed),
          actor(o.actor),
          sh(arcane::shades(actor)),
          mx(actor.x + actor.width / 2.),
          my(actor.y + actor.height * .55),
          cols{WHITE, C[6], C[6], C[5], C[4]} {
        ps.sparks = {WHITE, C[6], C[5], C[4]};
        for (auto& b : bits) {
            const double a = R.range(0, TAU), d = R.range(16, 46), ty = actor.y + R.range(3, actor.height - 2);
            double       oy = std::sin(a) * d * .8 - 6;
            if (ty + oy > -2) oy = -std::abs(oy) - 2;
            const double tx = mx + R.range(-1, 1), t0 = R.range(0, .1), t1 = LINE + R.range(-.02, GROW),
                         spinMagnitude = R.range(.8, 1.8), spin = spinMagnitude * R.sign();
            const auto   color = R.pick<uint32_t>(cols);
            b                  = {tx, ty, std::cos(a) * d, oy, t0, t1, spin, color};
        }
    }
    bool inside(int x, int y) const {
        return x >= 0 && y >= 0 && x < actor.width && y < actor.height &&
               actor.mask[size_t(y) * size_t(actor.width) + size_t(x)];
    }
    void          emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    static Sample at(const Bit& b, double tt) {
        const double k = clamp((tt - b.t0) / (b.t1 - b.t0)), e = ease::outCubic(k), r = 1 - e, sp = b.spin * r * 1.2,
                     ox = b.ox * std::cos(sp) - b.oy * std::sin(sp), oy = b.ox * std::sin(sp) + b.oy * std::cos(sp);
        return {b.tx + ox * r, std::min(-1., b.ty + oy * r), k};
    }
    PixelFxActorVisibility actorVisibility(double time) const override {
        return time < ARRIVE ? PixelFxActorVisibility::Hidden : PixelFxActorVisibility::Visible;
    }
    void update(double dt) override {
        t += dt;
        if (t < LINE && R.isChance(.6)) {
            const double a = R.range(0, TAU), distance = R.range(22, 44), life = R.range(.12, .18),
                         v = (distance * 3) / (std::exp(3 * life) - 1), x = mx + std::cos(a) * distance,
                         y = std::min(-2., my + std::sin(a) * distance), raw = jsHypot2(x - mx, y - my),
                         len = raw == 0 ? 1 : raw;
            emit({ParticleKind::Streak, x, y, ((mx - x) / len) * v, ((my - y) / len) * v, life,
                  R.isChance(.5) ? C[6] : C[5]});
        }
        if (!arrived && t >= ARRIVE) {
            arrived = true;
            lights.add(mx, my - 6, 56, C[5], .9, .35);
            lights.add(mx, my, 24, WHITE, .8, .1);
            for (int i = 0; i < 30; ++i) {
                const double a = R.range(0, TAU), sp = R.range(35, 110);
                emit({R.isChance(.6) ? ParticleKind::Star : ParticleKind::Mote, mx, my, std::cos(a) * sp,
                      std::sin(a) * sp * .7 - 30, R.range(.4, .85), R.pick<uint32_t>(cols), R.range(.6, 1.6)});
            }
            for (int i = 0; i < 12; ++i) {
                const double s = R.sign();
                emit({ParticleKind::Spark, mx + s * R.range(2, 8), -1, s * R.range(60, 180), -R.range(20, 110),
                      R.range(.2, .45)});
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void ring(Paint& p, bool near) const {
        const double k = (t - ARRIVE + .03) / .5;
        if (k >= 0 && k < 1) {
            const std::array<uint32_t, 4> ramp{C[3], C[5], C[6], WHITE};
            band(
                p, 0, 0, 4 + 30 * ease::outCubic(k), .2, std::max(1., 3 * (1 - k)), 1.15 - k, ramp,
                [&](double g) { return (std::sin(g) >= 0) == near; }, k < .25);
        }
    }
    void drawBack(Paint& p) const override {
        ring(p, false);
        p.glow(0, -1, 26, C[5], .3 * (t < ARRIVE ? t / ARRIVE : 1 - clamp((t - ARRIVE) / .5)));
    }
    void draw(Paint& p) const override {
        for (const auto& b : bits) {
            if (t < b.t0 || t > b.t1 + .02) continue;
            const auto [x, y, k] = at(b, t);
            p.dot(x, y, k > .75 ? WHITE : b.color, std::min(1., .35 + k * 2));
            if (k > .55) {
                const auto previous = at(b, t - .015);
                p.dot((x + previous.x) / 2, (y + previous.y) / 2, C[5], .8);
            }
            if (k > .8) p.glow(x, y, 3, C[6], .3);
        }
        if (t >= LINE - .04 && t < LINE) {
            const double q = (t - LINE + .04) / .04;
            p.sparkle(mx, my, 2 + 3 * q, C[6], C[5], .6);
        } else if (t >= LINE && t < LINE + GROW) {
            const double q = ease::outCubic((t - LINE) / GROW), hh = (actor.height / 2. + 3) * q;
            for (double y = -hh - 2; y <= hh + 2; ++y)
                p.dot(mx, my + y, std::abs(y) < hh ? WHITE : C[6], std::abs(y) < hh ? 1 : .7);
            for (double y = -hh * .7; y <= hh * .7; ++y) {
                p.dot(mx - 1, my + y, C[5], .6);
                p.dot(mx + 1, my + y, C[5], .6);
            }
            p.glow(mx, my, 30, C[5], .9);
            p.glow(mx, my, 8, WHITE, .5);
        } else if (t >= LINE + GROW && t < ARRIVE) {
            const double q = ease::outCubic((t - LINE - GROW) / OPEN);
            arcane::squeezed(p, actor, sh, q, WHITE, C[6]);
            p.glow(mx, my, 36, C[5], .8);
        } else if (t >= ARRIVE && t < ARRIVE + .42) {
            const double   k     = 1 - (t - ARRIVE) / .42;
            const uint32_t frame = jsUint(std::floor(t * 30));
            if (k > .86) arcane::squeezed(p, actor, sh, 1, WHITE, C[6]);
            for (int y = 0; y < actor.height; ++y)
                for (int x = 0; x < actor.width; ++x) {
                    if (inside(x, y))
                        p.light(actor.x + x, actor.y + y, C[6],
                                k * k * (sh[size_t(y) * size_t(actor.width) + size_t(x)] == 1 ? .3 : .8));
                    else if ((inside(x - 1, y) || inside(x + 1, y) || inside(x, y - 1) || inside(x, y + 1)) &&
                             rand({uint32_t(x), uint32_t(y), frame}) < .85 * k)
                        p.px(actor.x + x, actor.y + y,
                             k > .5 && rand({uint32_t(x), uint32_t(y), 7u}) < .5 ? WHITE : C[6]);
                }
            p.glow(mx, my, 34, C[5], .6 * k);
        }
        if (t >= ARRIVE && t < ARRIVE + .2) {
            const double q = (t - ARRIVE) / .2;
            flare(p, mx, my - 4, 18 * std::pow(1 - q, 1.5) + 2, 1 - q, C);
        }
        ring(p, true);
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& blinkInDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::BlinkIn, "arcane/blink-in", 136, 96, 68, 80, 1.4, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 12;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                              hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<BlinkIn>(s); },
        nullptr,
        nullptr,
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
