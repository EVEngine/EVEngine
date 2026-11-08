// eve-hot-path
// Native adaptation of pixelfx arcane/blink-out.js, MIT.
// Sprite Fusion blink, Copyright (c) 2026 Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Arcane.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double OUT = .32, SQUEEZE = .12, COLLAPSE = .08, POP = OUT + SQUEEZE + COLLAPSE;
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct BlinkOut final : Kernel {
    const std::vector<uint32_t>   C;
    Rng                           R;
    const PixelFxActor            actor;
    const std::vector<uint8_t>    sh;
    const double                  mx, my;
    const std::array<uint32_t, 5> cols;
    Particles                     ps;
    Lights                        lights;
    double                        t   = 0;
    bool                          out = false, popped = false;
    explicit BlinkOut(const PixelFxSettings& o)
        : C(o.palette),
          R(o.seed),
          actor(o.actor),
          sh(arcane::shades(actor)),
          mx(actor.x + actor.width / 2.),
          my(actor.y + actor.height * .55),
          cols{WHITE, C[6], C[6], C[5], C[4]} {
        ps.sparks = {WHITE, C[6], C[5], C[4]};
    }
    bool inside(int x, int y) const {
        return x >= 0 && y >= 0 && x < actor.width && y < actor.height &&
               actor.mask[size_t(y) * size_t(actor.width) + size_t(x)];
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    PixelFxActorVisibility actorVisibility(double time) const override {
        return time < OUT ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden;
    }
    void update(double dt) override {
        t += dt;
        if (t < OUT - .04 && R.isChance(.7)) {
            const double a = R.range(0, TAU), distance = R.range(22, 44), life = R.range(.12, .18),
                         v = (distance * 3) / (std::exp(3 * life) - 1), x = mx + std::cos(a) * distance,
                         y = std::min(-2., my + std::sin(a) * distance), raw = jsHypot2(x - mx, y - my),
                         len = raw == 0 ? 1 : raw;
            emit({ParticleKind::Streak, x, y, ((mx - x) / len) * v, ((my - y) / len) * v, life,
                  R.isChance(.5) ? C[6] : C[5]});
        }
        if (!out && t >= OUT) {
            out = true;
            lights.add(mx, my - 6, 54, C[5], .9, .3);
            lights.add(mx, my, 26, WHITE, .8, .1);
        }
        if (!popped && t >= POP) {
            popped = true;
            lights.add(mx, my - 6, 58, C[5], .9, .35);
            lights.add(mx, my, 24, WHITE, .9, .08);
            for (int i = 0; i < 110; ++i) {
                const double y = actor.y + R.range(2, actor.height - 2), s = R.sign();
                emit({ParticleKind::Ember, mx + R.range(-1, 1), y, s * R.range(25, 105), R.range(-70, 10),
                      R.range(.5, .95), R.pick<uint32_t>(cols), R.isChance(.3) ? 2. : 1.});
            }
            for (int i = 0; i < 18; ++i) {
                const double a = R.range(0, TAU), sp = R.range(80, 240);
                emit({ParticleKind::Spark, mx, my, std::cos(a) * sp, std::sin(a) * sp - 40, R.range(.2, .5)});
            }
            for (int i = 0; i < 14; ++i) {
                const double a = R.range(0, TAU), sp = R.range(20, 110);
                emit({i % 2 ? ParticleKind::Star : ParticleKind::Mote, mx, my, std::cos(a) * sp,
                      std::sin(a) * sp * .8 - 20, R.range(.4, .9),
                      R.pick<uint32_t>(std::array<uint32_t, 3>{WHITE, C[6], C[5]}), R.range(.8, 1.8)});
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void ring(Paint& p, bool near) const {
        const double k = (t - POP) / .5;
        if (k >= 0 && k < 1) {
            const std::array<uint32_t, 4> ramp{C[3], C[5], C[6], WHITE};
            band(
                p, 0, 0, 4 + 30 * ease::outCubic(k), .2, std::max(1., 3 * (1 - k)), 1.15 - k, ramp,
                [&](double g) { return (std::sin(g) >= 0) == near; }, k < .25);
        }
    }
    void drawBack(Paint& p) const override {
        if (t > POP && t < POP + .6) {
            const double k = .5 * (1 - (t - POP) / .6);
            for (int y = 0; y < actor.height; ++y)
                for (int x = 0; x < actor.width; ++x) {
                    const auto shade = sh[size_t(y) * size_t(actor.width) + size_t(x)];
                    if (shade) p.blend(actor.x + x, actor.y + y, shade == 1 ? C[3] : C[4], k);
                }
        }
        ring(p, false);
        if (t < POP + .3) p.glow(0, -1, 26, C[5], .3 * (t < OUT ? t / OUT : 1 - clamp((t - POP) / .3)));
    }
    void draw(Paint& p) const override {
        const uint32_t frame = jsUint(std::floor(t * 30));
        if (t < OUT) {
            const double k = t / OUT;
            for (int y = 0; y < actor.height; ++y)
                for (int x = 0; x < actor.width; ++x) {
                    if (inside(x, y))
                        p.light(actor.x + x, actor.y + y, C[6],
                                k * k * (sh[size_t(y) * size_t(actor.width) + size_t(x)] == 1 ? .3 : .75));
                    else if ((inside(x - 1, y) || inside(x + 1, y) || inside(x, y - 1) || inside(x, y + 1)) &&
                             rand({uint32_t(x), uint32_t(y), frame}) < .25 + .65 * k)
                        p.px(actor.x + x, actor.y + y,
                             k > .6 && rand({uint32_t(x), uint32_t(y), 7u}) < .5 ? WHITE : C[6]);
                }
            p.glow(mx, my, 30, C[5], .5 * k);
        } else if (t < OUT + .05) {
            arcane::squeezed(p, actor, sh, 1, WHITE, C[6]);
            p.glow(mx, my, 40, C[5], .8);
        } else if (t < OUT + SQUEEZE) {
            const double q = ease::inCubic((t - OUT - .05) / (SQUEEZE - .05));
            arcane::squeezed(p, actor, sh, 1 - q, WHITE, C[6]);
            p.glow(mx, my, 34, C[5], .8);
        } else if (t < POP) {
            const double q = (t - OUT - SQUEEZE) / COLLAPSE, hh = (actor.height / 2. + 2) * (1 - ease::inQuad(q));
            for (double y = -hh - 3; y <= hh + 3; ++y)
                p.dot(mx, my + y, std::abs(y) < hh ? WHITE : C[6], std::abs(y) < hh ? 1 : .7);
            if (hh > 4)
                for (double y = -hh * .7; y <= hh * .7; ++y) {
                    p.dot(mx - 1, my + y, C[5], .6);
                    p.dot(mx + 1, my + y, C[5], .6);
                }
            p.glow(mx, my, 30, C[5], .9);
            p.glow(mx, my, 8, WHITE, .5);
        }
        if (t >= POP && t < POP + .22) {
            const double q = (t - POP) / .22;
            flare(p, mx, my, 22 * std::pow(1 - q, 1.5) + 2, 1 - q, C);
            if (q < .3) p.circle(mx, my, 4 * (1 - q / .3) + 1, WHITE);
        }
        ring(p, true);
        if (t > POP && t < POP + .3) p.sparkle(mx, my - 1, 3, C[6], C[5], .3 * (1 - (t - POP) / .3));
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& blinkOutDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::BlinkOut, "arcane/blink-out", 136, 96, 68, 80, 1.6, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 11;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                              hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<BlinkOut>(s); },
        nullptr,
        nullptr,
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
