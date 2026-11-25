// eve-hot-path
// Native adaptation of pixelfx melee/uppercut.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/MeleeSwing.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct Burst {
    double angle, radius, length;
};
struct Uppercut final : Kernel {
    const std::vector<uint32_t>  C;
    const uint32_t               ink;
    Rng                          R;
    const melee::Slash           A{{8, -25, 27, 1, 0, 1.08, -1, 0, 2.5}, 13, .02, .07, .05, .22};
    const melee::Travel          top = melee::slashEnd(A);
    const double                 sx = top.x + 3, sy = top.y - 5, hitTime = A.t0 + A.sweep * .8;
    std::array<melee::Spike, 10> spikes;
    std::array<Burst, 8>         bursts;
    Particles                    ps;
    Lights                       lights;
    double                       t   = 0;
    bool                         hit = false, kicked = false;
    explicit Uppercut(const PixelFxSettings& o) : C(o.palette), ink(o.melee.outline), R(o.seed) {
        ps.sparks = o.melee.sparks;
        for (size_t i = 0; i < spikes.size(); ++i) {
            const double a   = (double(i) / 10) * TAU + R.range(-.12, .12) - PI / 2,
                         len = (i % 2 ? 7. : 13.) + R.range(0, 4);
            spikes[i]        = {a, len, i % 2 ? 2.2 : 3};
        }
        for (size_t i = 0; i < bursts.size(); ++i) {
            const double a = (double(i) / 8) * TAU + .2 + R.range(-.15, .15), r = R.range(15, 18), L = R.range(5, 9);
            bursts[i] = {a, r, L};
        }
    }
    void update(double dt) override {
        t += dt;
        if (!kicked) {
            kicked = true;
            for (int i = 0; i < 4; ++i) {
                const double x = 18 + R.range(-4, 4), vx = R.range(-30, 30), vy = -R.range(5, 20),
                             life = R.range(.3, .55), size = R.range(1.5, 2.6);
                ps.spawn(R, ParticleKind::Dust, x, -1, vx, vy, life, hex(0x6a6080), size);
            }
        }
        if (!hit && t >= hitTime) {
            hit = true;
            lights.add(sx, sy, 56, C[4], .6, .4);
            lights.add(sx, sy, 18, WHITE, .7, .08);
            for (int i = 0; i < 16; ++i) {
                const double a = -PI / 2 + R.range(-1.6, 1.6), speed = R.range(120, 320), life = R.range(.15, .45);
                ps.spawn(R, ParticleKind::Spark, sx, sy, std::cos(a) * speed, std::sin(a) * speed, life);
            }
            for (int i = 0; i < 6; ++i) {
                const double a = R.range(0, TAU), d = R.range(14, 24), life = R.range(.25, .5);
                const auto   color = R.isChance(.5) ? C[5] : WHITE;
                const double size = R.range(1.4, 2.6), delay = R.range(.04, .18);
                ps.spawn(R, ParticleKind::Star, sx + std::cos(a) * d, sy + std::sin(a) * d, std::cos(a) * 20,
                         std::sin(a) * 20 - 10, life, color, size, delay);
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        melee::slash(p, A, t, C);
        const double u = t - A.t0;
        if (u > 0 && u < .16)
            for (const auto& v : std::array<std::array<double, 3>, 3>{{{A.swing.cx + A.swing.R + 7, -16, 18},
                                                                       {A.swing.cx + A.swing.R + 11, -30, 12},
                                                                       {A.swing.cx - 4, -40, 10}}}) {
                const double k = u / .16, y = v[1] - 30 * k;
                for (double d = 0; d < v[2]; d += 1)
                    p.dot(v[0], y + d, d < 3 ? WHITE : d < v[2] * .5 ? C[5] : C[3], (1 - d / v[2]) * (1 - k));
            }
        if (u > 0 && u < A.sweep + .06) {
            const auto v = melee::onSwing(A.swing, A.swing.R - 5, A.swing.span * ease::outExpo(clamp(u / A.sweep)));
            p.glow(v.x, v.y, 34, C[3], .45 * (1 - u / (A.sweep + .06)));
        }
        const double q = t - hitTime;
        if (q >= 0 && q < .36) {
            const double pop = ease::outBack(clamp(q / .05), 2.6), out = 1 - smooth(.16, .36, q),
                         sc = pop * (.35 + .65 * out), turn = q * 1.2;
            auto         rays = spikes;
            for (auto& ray : rays) ray.angle += turn;
            if (sc > .08) {
                melee::star(p, sx, sy, rays, sc * 1.12, 1, ink, ink, ink, ink);
                melee::star(p, sx, sy, rays, sc, 1, q < .05 ? WHITE : C[6], C[4], C[3], C[2]);
            }
            p.glow(sx, sy, 30, C[4], .45 * out);
        }
        if (q >= .02 && q < .24) {
            const double k = (q - .02) / .22, r = 7 + 17 * ease::outCubic(k), th = std::max(1., 2.2 * (1 - k));
            const auto   color = k < .25 ? WHITE : k < .5 ? C[5] : k < .75 ? C[4] : C[3];
            for (double a = 0; a < TAU; a += .6 / r)
                for (double off = 0; off < th; off += .5)
                    p.dot(sx + std::cos(a) * (r - off), sy + std::sin(a) * (r - off), color, 1.15 - k);
        }
        if (q >= .02 && q < .2) {
            const double k = (q - .02) / .18;
            for (const auto& b : bursts) {
                const double r = b.radius + 10 * ease::outCubic(k), ca = std::cos(b.angle), sa = std::sin(b.angle);
                p.line(sx + ca * r, sy + sa * r, sx + ca * (r + b.length * (1 - k)), sy + sa * (r + b.length * (1 - k)),
                       k < .4 ? WHITE : C[5], 1.1 - k);
            }
        }
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& uppercutDescriptor() {
    static const std::array<Option, 3> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
         {"sparks", 1, 64, false, 4, &PixelFxMeleeSettings::sparks},
         {"outline", 0, 4294967295., false, 0, &PixelFxMeleeSettings::outline, true}}};
    static const Descriptor d{
        {PixelFxKind::Uppercut, "melee/uppercut", 132, 128, 48, 110, .85, false},
        7,
        [](PixelFxSettings& s) {
            s.seed          = 11;
            s.actor.source  = PixelFxActorSource::Dummy;
            s.palette       = {hex(0x3a1408), hex(0x7a2a10), hex(0xc4501a), hex(0xf08a2a),
                               hex(0xffc94a), hex(0xfff2a8), WHITE};
            s.melee.sparks  = {HOT.begin(), HOT.end()};
            s.melee.outline = hex(0x2a0a08);
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Uppercut>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
