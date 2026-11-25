// eve-hot-path
// Native adaptation of pixelfx holy/feather-burst.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#include "stylize/pixelfx/HolyFeathers.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
struct Lobe {
    double x, y, r0, r1, life;
};
struct FeatherBurst final : Kernel {
    std::vector<uint32_t> F;
    uint32_t              G;
    Rng                   rng;
    Particles             ps;
    Lights                lights;
    holy::Feathers        loose{1e6};
    std::array<Lobe, 11>  lobes;
    double                t = 0;
    explicit FeatherBurst(const PixelFxSettings& o) : F(o.palette), G(o.holy.glow), rng(o.seed) {
        ps.sparks = {WHITE, F[4], F[3], F[2]};
        for (size_t i = 0; i < lobes.size(); ++i) {
            const double a = (double(i) / 11) * TAU + rng.range(-.25, .25), d = rng.range(5, 14),
                         r0 = rng.range(2.5, 4), r1 = rng.range(6, 9.5), life = rng.range(.45, .8);
            lobes[i] = {std::cos(a) * d, std::sin(a) * d * .8, r0, r1, life};
        }
        for (int i = 0; i < 20; ++i) {
            const double a = (double(i) / 20) * TAU + rng.range(-.15, .15), sp = rng.range(110, 230),
                         up = std::sin(a) < 0 ? .7 : .9, angle = a + rng.range(-.5, .5), length = rng.range(10, 14),
                         width = rng.range(2.6, 3.4), life = rng.range(1.9, 2.6), delay = rng.range(0, .03),
                         sink = rng.range(26, 38), amp = rng.range(8, 13);
            loose.add(rng, {std::cos(a) * 3, std::sin(a) * 3, std::cos(a) * sp, std::sin(a) * sp * up - 20, angle,
                            length, width, life, delay, false, sink, amp});
        }
        for (int i = 0; i < 10; ++i) {
            const double a = rng.range(0, TAU), sp = rng.range(50, 130), angle = rng.range(0, TAU),
                         length = rng.range(6, 8), width = rng.range(1.8, 2.2), life = rng.range(1.6, 2.4),
                         sink = rng.range(18, 28);
            loose.add(rng,
                      {0, 0, std::cos(a) * sp, std::sin(a) * sp - 20, angle, length, width, life, 0, true, sink, {}});
        }
        for (int i = 0; i < 26; ++i) {
            const double a = rng.range(0, TAU), sp = rng.range(30, 140), x = rng.range(-3, 3), y = rng.range(-3, 3),
                         life  = rng.range(1.4, 2.6);
            const auto   color = rng.isChance(.6) ? WHITE : F[4];
            const double size  = rng.range(.6, 1.6);
            ps.spawn(rng, ParticleKind::Flake, x, y, std::cos(a) * sp, std::sin(a) * sp - 15, life, color, size);
        }
        for (int i = 0; i < 10; ++i) {
            const double a = rng.range(0, TAU), sp = rng.range(60, 160), life = rng.range(.3, .6);
            const auto   color = rng.isChance(.5) ? WHITE : G;
            const double size  = rng.range(.8, 2.2);
            ps.spawn(rng, ParticleKind::Star, 0, 0, std::cos(a) * sp, std::sin(a) * sp, life, color, size);
        }
        lights.add(0, 0, 50, G, .75, .45);
        lights.add(0, 0, 20, WHITE, .7, .1);
    }
    void update(double dt) override {
        t += dt;
        if (t > .3 && t < 2.2 && !loose.list.empty() && rng.isChance(.14)) {
            const auto q = loose.list[size_t(std::floor(rng.next() * double(loose.list.size())))];
            if (q.delay <= 0) {
                const double x = q.x + rng.range(-2, 2), y = q.y + rng.range(-1, 1), life = rng.range(.2, .4);
                const auto   color = rng.isChance(.5) ? WHITE : G;
                const double size  = rng.range(1, 2.2);
                ps.spawn(rng, ParticleKind::Star, x, y, 0, 0, life, color, size);
            }
        }
        ps.update(dt, rng, [](double) { return INFINITY; }, [](double, double) {});
        loose.update(dt);
        lights.update(dt);
    }
    void puff(Paint& p) const {
        for (auto l : lobes) {
            const double u = t / l.life;
            if (u >= 1) continue;
            const double e = ease::outCubic(u), x = l.x * (.4 + e * 1.3), y = l.y * (.4 + e * 1.3) - u * 5,
                         r = (l.r0 + (l.r1 - l.r0) * ease::outCubic(std::min(1., u * 3))) * (1 - ease::inQuad(u));
            if (r < .6) continue;
            p.circle(x, y, r + .8, F[2]);
            p.circle(x, y, r, [&](double d, double a) {
                return d > .55 && std::sin(a + .8) > .3 ? F[3] : d < .45 && std::sin(a + .8) < -.2 ? WHITE : F[4];
            });
        }
    }
    void drawBack(Paint& p) const override {
        loose.draw(p, F, Paint::Layer::Back);
        ps.draw(p, Paint::Layer::Back);
        puff(p);
    }
    void draw(Paint& p) const override {
        if (t < .07) p.circle(0, 0, 9 * (1 - t / .14), WHITE);
        if (t < .25) {
            const double u = t / .25;
            p.ring(
                0, 0, 6 + 24 * ease::outCubic(u), std::max(1., 2.5 * (1 - u)),
                [&](double v) { return v > .5 ? WHITE : mix(F[4], G, .4); }, 1.1 - u);
        }
        if (t < .2) p.sparkle(0, 0, 10 * (1 - t / .2), WHITE, G, .6);
        p.glow(0, 4, 46, G, .28 * (1 - smooth(0, 1.3, t)));
        loose.draw(p, F, Paint::Layer::Front);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& featherBurstDescriptor() {
    static const std::array<Option, 1> options{
        {{"glow", 4278190080., 4294967295., false, 0, &PixelFxHolySettings::glow, true}}};
    static const Descriptor d{
        {PixelFxKind::FeatherBurst, "holy/feather-burst", 160, 160, 80, 60, 3, false},
        6,
        [](PixelFxSettings& s) {
            s.seed      = 4;
            s.holy.glow = hex(0xfff0c0);
            s.palette   = {hex(0x4a4560), hex(0x7a7494), hex(0xaaa5c2), hex(0xd6d3e6), hex(0xf4f3fa), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<FeatherBurst>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
