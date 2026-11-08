// eve-hot-path
// Native adaptation of pixelfx melee/dagger-flurry.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/MeleeSwing.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double EVERY = .055;
struct DaggerSliver {
    double angle, radius, speed, arc;
};
struct DaggerFlurry final : Kernel {
    const std::vector<uint32_t>  C;
    Rng                          R;
    std::array<melee::Slash, 7>  cuts;
    std::array<melee::Travel, 7> ends;
    std::array<bool, 7>          emitted{};
    std::array<DaggerSliver, 4>  slivers;
    Particles                    ps;
    Lights                       lights;
    double                       t = 0;
    explicit DaggerFlurry(const PixelFxSettings& o) : C(o.palette), R(o.seed) {
        ps.sparks = o.melee.sparks;
        constexpr std::array<std::array<double, 4>, 6> positions{{{21, -27, -.5, 1},
                                                                  {33, -16, .9, -1},
                                                                  {26, -38, 2.4, 1},
                                                                  {37, -30, -1.8, -1},
                                                                  {19, -17, .2, 1},
                                                                  {30, -24, 1.4, -1}}};
        for (size_t i = 0; i < positions.size(); ++i) {
            const auto&  p      = positions[i];
            const double radius = R.range(12, 15), sy = R.range(.55, .75), a0 = R.range(-2.8, -2.2),
                         span = R.range(2.3, 2.8), width = R.range(5.2, 6.4);
            cuts[i] = {{p[0], p[1], radius, sy, p[2], a0, p[3], 0, span}, width, double(i) * EVERY, .035, .03, .13};
        }
        cuts[6] = {{24, -26, 30, .36, .12, 3, -1, .24, 2.85}, 11, 6 * EVERY + .04, .055, .06, .24};
        for (size_t i = 0; i < cuts.size(); ++i) ends[i] = melee::slashEnd(cuts[i]);
        size_t i = 0;
        for (double a : {-2.6, -1.5, -.4, .7}) {
            const double angle = a + R.range(-.2, .2), radius = R.range(4, 7), speed = R.range(70, 110),
                         arc = R.range(.4, .7);
            slivers[i++]     = {angle, radius, speed, arc};
        }
    }
    void update(double dt) override {
        t += dt;
        for (size_t i = 0; i < cuts.size(); ++i) {
            const auto& s = cuts[i];
            if (emitted[i] || t < s.t0 + s.sweep) continue;
            emitted[i]       = true;
            const auto& e    = ends[i];
            const bool  last = i == 6;
            lights.add(e.x, e.y, last ? 56 : 26, C[3], last ? .6 : .4, last ? .4 : .14);
            if (last) lights.add(e.x, e.y, 16, WHITE, .7, .08);
            for (int k = 0; k < (last ? 12 : 3); ++k) {
                const double angle = std::atan2(e.dy, e.dx) + R.range(-.5, .5), speed = R.range(100, last ? 340 : 220),
                             life = R.range(.1, last ? .35 : .22);
                ps.spawn(R, ParticleKind::Spark, e.x, e.y, std::cos(angle) * speed, std::sin(angle) * speed, life);
            }
            const std::array<uint32_t, 2> colors{C[4], C[5]};
            melee::shavings(ps, R, s, last ? 14 : 4, colors);
            if (last)
                for (int k = 0; k < 5; ++k) {
                    const double x = s.swing.cx + R.range(-22, 22), y = s.swing.cy + R.range(-14, 14),
                                 life  = R.range(.2, .45);
                    const auto   color = R.isChance(.5) ? C[5] : WHITE;
                    const double size = R.range(1, 2.2), delay = R.range(.05, .25);
                    ps.spawn(R, ParticleKind::Star, x, y, 0, -4, life, color, size, delay);
                }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        for (const auto& s : cuts) melee::slash(p, s, t, C);
        for (size_t i = 0; i < cuts.size(); ++i) {
            const auto&  s    = cuts[i];
            const auto&  e    = ends[i];
            const bool   last = i == 6;
            const double g = t - s.t0 - s.sweep * .6, life = last ? .24 : .12;
            if (g > 0 && g < life)
                p.sparkle(e.x, e.y, (last ? 10 : 4) * ease::outBack(std::min(1., g / .04)) * (1 - g / life), C[5], C[3],
                          last ? .6 : .35);
            const double u = t - s.t0;
            if (u > 0 && u < s.sweep + .1) {
                auto partial       = s;
                partial.swing.span = s.swing.span * ease::outExpo(clamp(u / s.sweep));
                const auto h       = melee::slashEnd(partial);
                p.glow(h.x, h.y, last ? 40 : 22, C[3], (last ? .5 : .35) * (1 - u / (s.sweep + .1)));
            }
        }
        const double q = t - cuts[6].t0 - cuts[6].sweep;
        if (q > .02 && q < .14) {
            const double k = (q - .02) / .12;
            const auto&  e = ends.back();
            for (const auto& s : slivers)
                melee::sliver(p, e.x, e.y, s.angle + std::atan2(e.dy, e.dx), s.radius + s.speed * (q - .02),
                              s.arc * (1 - .4 * k),
                              k < .4   ? C[6]
                              : k < .7 ? C[5]
                                       : C[4],
                              1 - k);
        }
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& daggerFlurryDescriptor() {
    static const std::array<Option, 2> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"sparks", 1, 64, false, 4, &PixelFxMeleeSettings::sparks}}};
    static const Descriptor            d{
        {PixelFxKind::DaggerFlurry, "melee/dagger-flurry", 148, 104, 36, 86, .95, false},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 12;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x1b1630), hex(0x3a2f63), hex(0x6150a3), hex(0x9480d8),
                              hex(0xc7b8f5), hex(0xefe9ff), WHITE};
            s.melee.sparks = {WHITE, hex(0xefe9ff), hex(0xc7b8f5), hex(0x9480d8)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<DaggerFlurry>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
