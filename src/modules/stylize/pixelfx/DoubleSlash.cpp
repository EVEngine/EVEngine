// eve-hot-path
// Native adaptation of pixelfx melee/double-slash.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/MeleeSwing.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double UP = .14, CLOSE = UP + .012, X = 34, Y = -23;
struct CrossSliver {
    double angle, radius, speed, arc;
};
struct DoubleSlash final : Kernel {
    const std::vector<uint32_t>       C;
    Rng                               R;
    const std::array<melee::Slash, 2> cuts{
        {{melee::through(X, Y, .72, 1, 62, .84, .55), 10, 0, .06, .07, .24, 0, true},
         {melee::through(X, Y, -.66, -1, 62, .84, .55), 11, UP, .06, .06, .26, 0, true}}};
    const std::array<melee::Travel, 2> ends{melee::slashEnd(cuts[0]), melee::slashEnd(cuts[1])};
    std::array<CrossSliver, 4>         slivers;
    Particles                          ps;
    Lights                             lights;
    double                             t    = 0;
    bool                               cut1 = false, cut2 = false, closed = false;
    explicit DoubleSlash(const PixelFxSettings& o) : C(o.palette), R(o.seed) {
        ps.sparks = o.melee.sparks;
        size_t i  = 0;
        for (double a : {-2.4, -1.1, .2, 1.6}) {
            const double angle = a + R.range(-.2, .2), radius = R.range(5, 8), speed = R.range(70, 120),
                         arc = R.range(.4, .7);
            slivers[i++]     = {angle, radius, speed, arc};
        }
    }
    void update(double dt) override {
        t += dt;
        const std::array<uint32_t, 2> colors{C[4], C[5]};
        if (!cut1 && t >= cuts[0].sweep) {
            cut1 = true;
            melee::shavings(ps, R, cuts[0], 10, colors);
            for (int i = 0; i < 6; ++i) {
                const double angle = std::atan2(ends[0].dy, ends[0].dx) + R.range(-.5, .5), speed = R.range(120, 300),
                             life = R.range(.12, .3);
                ps.spawn(R, ParticleKind::Spark, ends[0].x, ends[0].y, std::cos(angle) * speed, std::sin(angle) * speed,
                         life);
            }
        }
        if (!closed && t >= CLOSE) {
            closed = true;
            lights.add(X, Y, 56, C[3], .45, .4);
            lights.add(X, Y, 18, WHITE, .6, .08);
            for (int i = 0; i < 18; ++i) {
                const double angle = R.range(-PI, PI) * .8, speed = R.range(140, 360), life = R.range(.14, .4);
                ps.spawn(R, ParticleKind::Spark, X, Y, std::cos(angle) * speed, std::sin(angle) * speed - 40, life);
            }
            for (int i = 0; i < 5; ++i) {
                const double x = X + R.range(-14, 14), y = Y + R.range(-12, 12), life = R.range(.2, .4);
                const auto   color = R.isChance(.5) ? C[5] : WHITE;
                const double size = R.range(1, 2.2), delay = R.range(.05, .2);
                ps.spawn(R, ParticleKind::Star, x, y, 0, -4, life, color, size, delay);
            }
        }
        if (!cut2 && t >= UP + cuts[1].sweep) {
            cut2 = true;
            melee::shavings(ps, R, cuts[1], 12, colors);
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const double flash = t >= CLOSE && t < CLOSE + .07 ? 1. : 0.;
        for (const auto& geometry : cuts) {
            auto local = geometry;
            local.hot  = flash;
            melee::slash(p, local, t, C);
        }
        for (size_t i = 0; i < cuts.size(); ++i) {
            const auto&  s = cuts[i];
            const auto&  e = ends[i];
            const double g = t - s.t0 - s.sweep * .6;
            if (g > 0 && g < .2)
                p.sparkle(e.x, e.y, 7 * ease::outBack(std::min(1., g / .05)) * (1 - g / .2), C[5], C[3], .5);
        }
        const double q = t - CLOSE;
        if (q >= 0 && q < .26) {
            const double k = q / .26;
            flare(p, X, Y, 22 * std::pow(1 - k, 1.4), 1 - k, C);
            p.glow(X, Y, 34, C[4], .35 * (1 - k));
        }
        if (q >= .03 && q < .15) {
            const double k = (q - .03) / .12;
            for (const auto& s : slivers)
                melee::sliver(p, X, Y, s.angle, s.radius + s.speed * (q - .03), s.arc * (1 - .4 * k),
                              k < .4   ? C[6]
                              : k < .7 ? C[5]
                                       : C[4],
                              1 - k);
        }
        for (const auto& s : cuts) {
            const double u = t - s.t0;
            if (u <= 0 || u >= .25) continue;
            auto partial       = s;
            partial.swing.span = s.swing.span * ease::outExpo(clamp(u / s.sweep));
            const auto e       = melee::slashEnd(partial);
            p.glow(e.x, e.y, 40, C[3], .5 * (1 - u / .25));
        }
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& doubleSlashDescriptor() {
    static const std::array<Option, 2> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"sparks", 1, 64, false, 4, &PixelFxMeleeSettings::sparks}}};
    static const Descriptor            d{
        {PixelFxKind::DoubleSlash, "melee/double-slash", 164, 108, 44, 92, .8, false},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 6;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x2a0a22), hex(0x5e1238), hex(0xa01f4e), hex(0xe0386a),
                              hex(0xff86a6), hex(0xffd2dd), WHITE};
            s.melee.sparks = {hex(0xfff3a6), hex(0xffe26b), hex(0xff8a2a), hex(0xf2561d)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<DoubleSlash>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
