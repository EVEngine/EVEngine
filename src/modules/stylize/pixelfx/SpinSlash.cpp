// eve-hot-path
// Native adaptation of pixelfx melee/spin-slash.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/MeleeSwing.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double SW = .17, FLARE = .3;
double           turn(double t) { return 1 - std::pow(1 - clamp(t / SW), 1.6); }
double           passedAt(double f) { return SW * (1 - std::pow(1 - clamp(f), 1 / 1.6)); }
struct SpinSlash final : Kernel {
    const std::vector<uint32_t>   C;
    const std::array<uint32_t, 7> dim;
    const melee::Swing            A, E;
    Rng                           R;
    Particles                     ps;
    Lights                        lights;
    double                        t = 0, lastU = 0;
    bool                          closed = false;
    explicit SpinSlash(const PixelFxSettings& o)
        : C(o.palette),
          dim{C[0], C[0], C[1], C[2], C[3], C[4], C[5]},
          A{0, -17, o.melee.radius, .27, -.1, .5, -1, .15},
          E{0, -17, o.melee.radius * .8, .27, -.1, .5, -1, .1},
          R(o.seed) {
        ps.sparks = o.melee.sparks;
    }
    void body(Paint& p, const melee::Swing& s, double width, double lag, int half, bool rim) const {
        const double U    = TAU * turn(t - lag);
        const auto   ramp = half < 0 ? std::span<const uint32_t>(dim) : std::span<const uint32_t>(C);
        if (U < .02 || t - lag > SW + .02) return;
        melee::smear(
            p, s, 0, U, width,
            [&](double u, double r, ShadeOutput& out, double) {
                const double age = t - lag - passedAt(u / TAU), lead = U - u,
                             w = width * std::pow(clamp(lead / .85), .5) * (.3 + .7 * std::exp(-age / .075)) *
                                 std::pow(clamp(u / 1.3), .7);
                if (w < .45) return;
                const double q = (s.R - r) / w;
                if (q < 0 || q > 1) return;
                const double ci =
                    rim && q < .26 && age < .06 ? 0 : std::round(q * 6 * .7 + age * 13 + (rim ? .4 : 1.4));
                out.c = ramp[size_t(6 - clamp(ci, 0., 6.))];
                out.a = std::min(1., u / 1.1 + .2);
            },
            half);
    }
    void ring(Paint& p, int half) const {
        body(p, E, 5, .025, half, false);
        body(p, A, 16, 0, half, true);
        if (t < SW + .04)
            melee::glowAlong(
                p, A, A.R - 5, 0, TAU * turn(t), 14, C[3],
                [&](double u) { return (half < 0 ? .2 : .3) * std::exp(-(t - passedAt(u / TAU)) / .08); }, half);
        if (t < SW) return;
        const double f    = (t - SW) / FLARE;
        const auto   ramp = half < 0 ? std::span<const uint32_t>(dim) : std::span<const uint32_t>(C);
        if (f < 1) {
            const double radius = A.R + 6 * ease::outCubic(f),
                         th     = f < .13 ? 9 : std::max(1., 6 * std::pow(1 - f, 1.5) + .5);
            auto         s      = A;
            s.R                 = radius;
            const size_t step   = f < .13 ? 6 : f < .3 ? 5 : f < .55 ? 4 : 3;
            melee::smear(
                p, s, 0, TAU, th,
                [&](double, double r, ShadeOutput& out, double) {
                    const double q = (radius - r) / th;
                    if (q < 0 || q > 1) return;
                    out.c = f < .13 ? (q < .6 ? WHITE : ramp[5]) : q < .45 ? ramp[step] : ramp[step - 1];
                    out.a = f < .4 ? 1 : 1.3 - f;
                },
                half);
            melee::glowAlong(p, s, radius - 2, 0, TAU, 14, C[4], .32 * (1 - f) * (1 - f), half);
        }
        const double k = 1 - clamp((t - SW - .06) / .4);
        if (k > 0)
            melee::trace(
                p, A, A.R - .5, 0, TAU, ramp[4],
                [&](double u) { return clamp(k * 1.5 - rand({jsUint(std::floor(u * 30)), 5}) * .8) * .95; }, half);
    }
    void update(double dt) override {
        t += dt;
        const double U = TAU * turn(t);
        if (t < SW) {
            for (double u = lastU; u < U; u += .3) {
                const auto   tr = melee::travel(A, u);
                const double ox = tr.x - A.cx, oy = tr.y - A.cy, norm = jsHypot2(ox, oy), l = norm != 0 ? norm : 1,
                             speed = R.range(40, 120), vy = tr.dy * speed * .6 - R.range(5, 30),
                             life  = R.range(.22, .45);
                const auto   color = C[size_t(4 + R.integer(0, 1))];
                ps.spawn(R, ParticleKind::Mote, tr.x, tr.y, tr.dx * speed + (ox / l) * speed * .35, vy, life, color,
                         .55, 0, std::sin(tr.a) < 0 ? ParticleFlag::Back : ParticleFlag::None);
            }
            lastU = U;
        }
        if (!closed && t >= SW) {
            closed = true;
            lights.add(A.cx, A.cy + 4, 90, C[3], .4, .4);
            const auto end = melee::travel(A, TAU);
            for (int i = 0; i < 9; ++i) {
                const double a = std::atan2(end.dy, end.dx) + R.range(-.45, .35), speed = R.range(160, 360),
                             vy = std::sin(a) * speed - R.range(20, 70), life = R.range(.14, .36);
                ps.spawn(R, ParticleKind::Spark, end.x, end.y, std::cos(a) * speed, vy, life);
            }
            for (int i = 0; i < 16; ++i) {
                const double u     = R.range(0, TAU);
                const auto   tr    = melee::travel(A, u);
                const double speed = R.range(110, 260), vy = tr.dy * speed - R.range(20, 80), life = R.range(.12, .32);
                ps.spawn(R, ParticleKind::Spark, tr.x, tr.y, tr.dx * speed, vy, life, 0, 1, 0,
                         std::sin(tr.a) < 0 ? ParticleFlag::Back : ParticleFlag::None);
            }
            for (int i = 0; i < 6; ++i) {
                const double u = R.range(0, TAU), radius = A.R + R.range(-1, 3);
                const auto   v  = melee::onSwing(A, radius, u);
                const double vy = -R.range(2, 8), life = R.range(.25, .5);
                const auto   color = R.isChance(.5) ? C[5] : WHITE;
                const double size = R.range(1.2, 2.6), delay = R.range(.04, .16);
                ps.spawn(R, ParticleKind::Star, v.x, v.y, 0, vy, life, color, size, delay,
                         std::sin(A.a0 + A.dir * u) < 0 ? ParticleFlag::Back : ParticleFlag::None);
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        ring(p, -1);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        ring(p, 1);
        if (t < .06) {
            const auto v = melee::onSwing(A, A.R - 3, 0);
            p.sparkle(v.x, v.y, 7 * (1 - t / .06), C[5], C[3], .5);
        }
        const double g   = t - SW * .85;
        const auto   tip = melee::onSwing(A, A.R - 3, TAU);
        if (g > 0 && g < .24)
            p.sparkle(tip.x, tip.y, 10 * ease::outBack(std::min(1., g / .05)) * (1 - g / .24), C[5], C[3], .6);
        if (t < SW + .05) {
            const double U   = TAU * turn(t);
            const auto   v   = melee::onSwing(A, A.R - 5, U);
            const bool   far = std::sin(A.a0 + A.dir * U) < 0;
            p.glow(v.x, v.y, far ? 26 : 38, C[3], (far ? .3 : .5) * (1 - smooth(SW - .04, SW + .05, t)));
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& spinSlashDescriptor() {
    static const std::array<Option, 3> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"sparks", 1, 64, false, 4, &PixelFxMeleeSettings::sparks},
                                                {"radius", 0, 512, false, 0, &PixelFxMeleeSettings::radius}}};
    static const Descriptor            d{
        {PixelFxKind::SpinSlash, "melee/spin-slash", 208, 104, 104, 84, .8, false},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 3;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x16224a), hex(0x1f4a86), hex(0x2a7fc0), hex(0x48b8ec),
                              hex(0x9fe2ff), hex(0xe2f7ff), WHITE};
            s.melee.sparks = {HOT.begin(), HOT.end()};
            s.melee.radius = 46;
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<SpinSlash>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
