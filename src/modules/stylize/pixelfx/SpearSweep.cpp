// eve-hot-path
// Native adaptation of pixelfx melee/spear-sweep.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/MeleeSwing.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct SpearSweep final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         R;
    const melee::Stone          stone;
    const melee::Slash          slash{{2, -12, 50, .2, -.04, PI + .55, -1, .19, PI + .35}, 19, 0, .13, .05, .3};
    const melee::Travel         end = melee::slashEnd(slash);
    Particles                   ps;
    Lights                      lights;
    double                      t = 0, lastU = 0;
    bool                        emitted = false;
    explicit SpearSweep(const PixelFxSettings& o) : C(o.palette), R(o.seed), stone(melee::stone(o.melee.stone)) {
        ps.sparks = o.melee.sparks;
    }
    void update(double dt) override {
        t += dt;
        const auto&  A = slash.swing;
        const double U = A.span * ease::outExpo(clamp(t / slash.sweep));
        for (double u = lastU; u < U; u += .09) {
            const auto   tr   = melee::travel(A, u);
            const bool   near = std::sin(tr.a) >= 0;
            const double fy = near ? -1. : -3., speed = R.range(30, 90), y = fy - R.range(0, 2), vy = -R.range(4, 16),
                         life = R.range(.7, 1.15), size = R.range(3, 5.5), delay = R.range(0, .05);
            ps.spawn(R, ParticleKind::Smoke, tr.x, y, tr.dx * speed, vy, life, stone.dust, size, delay);
            if (R.isChance(.6)) {
                const double gritSpeed = R.range(40, 120), jitter = R.range(-20, 20);
                const double vx = tr.dx * gritSpeed + jitter, vy0 = -R.range(40, 130), life0 = R.range(.6, 1);
                const std::array<uint32_t, 3> colors{stone.light, stone.body, stone.shade};
                const auto                    color = colors[size_t(R.integer(0, 2))];
                const double                  size0 = R.isChance(.3) ? 2. : 1.;
                ps.spawn(R, ParticleKind::Grit, tr.x, fy, vx, vy0, life0, color, size0);
            }
            if (R.isChance(.35)) {
                const double vx = tr.dx * R.range(40, 100), vy0 = tr.dy * 40 - R.range(5, 20), life0 = R.range(.2, .4);
                const auto   color = R.isChance(.5) ? C[4] : C[5];
                ps.spawn(R, ParticleKind::Mote, tr.x, tr.y, vx, vy0, life0, color, .6, 0,
                         near ? ParticleFlag::None : ParticleFlag::Back);
            }
        }
        lastU = U;
        if (!emitted && t >= slash.sweep) {
            emitted = true;
            lights.add(end.x, end.y - 4, 50, C[3], .5, .35);
            for (int i = 0; i < 8; ++i) {
                const double angle = std::atan2(end.dy, end.dx) + R.range(-.5, .3), speed = R.range(120, 260),
                             life = R.range(.12, .3);
                ps.spawn(R, ParticleKind::Spark, end.x, end.y, std::cos(angle) * speed, std::sin(angle) * speed - 40,
                         life);
            }
            for (int i = 0; i < 6; ++i) {
                const double x = end.x + R.range(-6, 4), y = -R.range(1, 4), vx = R.range(30, 90), vy = -R.range(4, 14),
                             life = R.range(.9, 1.25), size = R.range(4.5, 7), delay = R.range(0, .08);
                ps.spawn(R, ParticleKind::Smoke, x, y, vx, vy, life, stone.dust, size, delay, ParticleFlag::Big);
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        melee::slash(p, slash, t, C, -1);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        melee::slash(p, slash, t, C, 1);
        const double g = t - slash.sweep * .6;
        if (g > 0 && g < .24)
            p.sparkle(end.x, end.y, 9 * ease::outBack(std::min(1., g / .05)) * (1 - g / .24), C[5], C[3], .55);
        if (t < slash.sweep + .12) {
            const auto q = melee::onSwing(slash.swing, slash.swing.R - 5,
                                          slash.swing.span * ease::outExpo(clamp(t / slash.sweep)));
            p.glow(q.x, q.y, 36, C[3], .45 * (1 - t / (slash.sweep + .12)));
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& spearSweepDescriptor() {
    static const std::array<Option, 3> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
         {"sparks", 1, 64, false, 4, &PixelFxMeleeSettings::sparks},
         {"stone", 0, 4294967295., false, 0, &PixelFxMeleeSettings::stone, true}}};
    static const Descriptor d{
        {PixelFxKind::SpearSweep, "melee/spear-sweep", 216, 84, 104, 70, 1.5, false},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 10;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x0f2a2a), hex(0x18504a), hex(0x22806e), hex(0x3fb894),
                              hex(0x8fe6c0), hex(0xdcfff0), WHITE};
            s.melee.sparks = {hex(0xfff3a6), hex(0xffe26b), hex(0xff8a2a), hex(0xf2561d)};
            s.melee.stone  = hex(0x4a3f69);
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<SpearSweep>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
