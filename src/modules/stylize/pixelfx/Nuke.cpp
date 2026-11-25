// eve-hot-path
// Adapted from pixelfx explosions/nuke.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Explosion.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
using namespace explosion;
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
    ParticleFlag flags = ParticleFlag::None;
};
struct Veil {
    double angle, size, w, e, dx, dy;
};
struct Skirt {
    double ph, v, size, w, e, dx, dy;
};
const std::array<uint32_t, 5> CAP{hex(0xa0918c), hex(0x7c6f6e), hex(0x5f5458), hex(0x40373f), hex(0x271f27)};
PlumeOptions                  plumeOptions() {
    PlumeOptions o;
    o.y       = -26;
    o.rise    = 152;
    o.tau     = 1.7;
    o.drift   = 3;
    o.tube    = {14, 26};
    o.ring    = {4, 46};
    o.grow    = 1.6;
    o.n       = 50;
    o.size    = {9.5, 14.5};
    o.swell   = .45;
    o.spin    = 2.4;
    o.spinTau = 2.4;
    o.heat    = 1.25;
    o.cool    = .42;
    o.under   = .6;
    o.tilt    = .16;
    o.squash  = .48;
    o.fade    = {6, 8.3};
    o.spread  = 26;
    o.style   = 1;
    o.inner   = true;
    o.hollow  = true;
    o.fray    = std::array<double, 3>{4, 1.6, 6.4};
    return o;
}
struct Nuke final : Kernel {
    std::vector<uint32_t>   F;
    std::array<uint32_t, 9> FR;
    uint32_t                dust;
    std::array<uint32_t, 5> DUST;
    Rng                     rng;
    Particles               ps;
    Lights                  lights;
    Plume                   plume;
    Column                  column;
    std::array<Veil, 72>    veil;
    std::array<Skirt, 110>  skirt;
    double                  t      = 0;
    bool                    boomed = false;
    explicit Nuke(const PixelFxSettings& o)
        : F(o.palette),
          FR(fireRamp(F)),
          dust(o.explosion.dust),
          DUST{mix(dust, WHITE, .22), dust, mix(dust, hex(0x3a3040), .35), mix(dust, hex(0x2a2330), .6),
               mix(dust, hex(0x1c1720), .8)},
          rng(o.seed),
          plume(rng, plumeOptions()),
          column(ColumnOptions{3.5, 22, 125, {5.5, 7}, 84, 1.4, 1}) {
        ps.sparks = {F[6], F[5], F[3], F[2]};
        for (size_t i = 0; i < veil.size(); ++i)
            veil[i] = {-PI * ((i + rng.range(.2, .8)) / 72),
                       rng.range(6, 10),
                       rng.range(.97, 1.06),
                       rng.range(.6, 1.3),
                       rng.range(-1, 1),
                       rng.range(-1, .2)};
        for (auto& q : skirt)
            q = {rng.range(0, TAU),  rng.range(0, 1),  rng.range(5, 9),  rng.range(.9, 1.06),
                 rng.range(.6, 1.3), rng.range(-1, 1), rng.range(-1, .4)};
    }
    void   emit(Emission q) { ps.spawn(rng, q.kind, q.x, q.y, q.vx, q.vy, q.life, q.color, q.size, q.delay, q.flags); }
    double top() const {
        const auto S = plume.shape();
        return S.y + S.tube * .3;
    }
    void boom() {
        lights.add(0, -60, 150, F[4], .7, 1.6);
        for (int i = 0; i < 26; ++i) {
            const double a = rng.range(-PI, 0), d = rng.range(4, 22), sp = rng.range(40, 160);
            emit({ParticleKind::Fire, std::cos(a) * d, -26 + std::sin(a) * d, std::cos(a) * sp,
                  std::sin(a) * sp * .7 - 30, rng.range(.6, 1.1), 0, rng.range(7, 12), 0,
                  rng.isChance(.5) ? ParticleFlag::Smokes | ParticleFlag::Big : ParticleFlag::None});
        }
        for (int i = 0; i < 90; ++i) {
            const double a = rng.range(-PI - .3, .3), sp = rng.range(200, 420);
            emit({ParticleKind::Spark, std::cos(a) * 18, -26 + std::sin(a) * 18, std::cos(a) * sp,
                  std::sin(a) * sp - 60, rng.range(.3, .8)});
        }
        for (int i = 0; i < 50; ++i) {
            const double a = rng.range(-PI, 0), sp = rng.range(30, 140);
            emit({ParticleKind::Ember, std::cos(a) * 20, -26 + std::sin(a) * 20, std::cos(a) * sp,
                  std::sin(a) * sp - 60, rng.range(1.6, 3.4), F[3]});
        }
        for (int i = 0; i < 120; ++i) {
            const bool   slow = i >= 92;
            const double a = rng.range(0, TAU), c = std::cos(a), s = std::sin(a),
                         sp = slow ? rng.range(110, 200) : rng.range(240, 440);
            emit({ParticleKind::Smoke, c * 14, s * 14 * .16 - rng.range(1, 3), c * sp,
                  s * sp * (s > 0 ? .06 : .14) - rng.range(0, slow ? 6 : 3), rng.range(1.8, 3), dust,
                  slow ? rng.range(8, 11) : rng.range(5, 9), rng.range(.05, .2)});
        }
        for (int i = 0; i < 40; ++i) {
            const double u = rng.range(-1, 1), a = -PI / 2 + u * .7, sp = rng.range(150, 380) * (1 - .4 * std::abs(u));
            emit({ParticleKind::Grit, u * 12, -1, std::cos(a) * sp, std::sin(a) * sp, rng.range(1.2, 2),
                  rng.pick<uint32_t>(std::span<const uint32_t>(DUST).first(3)), rng.isChance(.4) ? 2. : 1.});
        }
    }
    void update(double dt) override {
        t += dt;
        if (!boomed && t >= .07) {
            boomed = true;
            boom();
        }
        if (t >= .07)
            plume.update(dt, rng, [&](double x, double y, double r, double ux, double uy) {
                // Preserve JS left-to-right RNG order inside the vertical-velocity subtraction.
                const double vx = ux * rng.range(10, 26), rise = uy * rng.range(4, 12), vy = rise - rng.range(2, 8),
                             life  = rng.range(1, 1.7);
                const auto   color = CAP[rng.integer(0, 1)];
                const double size  = r * rng.range(.3, .45);
                emit({ParticleKind::Smoke, x, y, vx, vy, life, color, size});
            });
        const auto   S   = plume.shape();
        const double hot = plume.hot();
        column.update(dt, top(), t > .35 && t < 4.9 ? 1 : 0, rng);
        if (t > .9 && t < 4.6 && rng.isChance(9 * dt)) {
            const int side = rng.sign();
            emit({ParticleKind::Vapour, side * rng.range(50, 160), -rng.range(2, 6), -side * rng.range(20, 45),
                  -rng.range(0, 6), rng.range(1.2, 1.8), DUST[1], rng.range(5, 8)});
        }
        if (t > .3 && t < 3.4 && rng.isChance(14 * hot * dt)) {
            const double a = rng.range(0, TAU);
            emit({ParticleKind::Ember, S.x + std::cos(a) * (S.ring + S.tube), S.y + std::sin(a) * S.tube * .5,
                  std::cos(a) * rng.range(30, 80), -rng.range(10, 50), rng.range(1.4, 2.6), F[3]});
        }
        draft(ps, dt, {0, S.y + S.tube * .3, 9, 160, 4, 120, 14, 170, 1 - smooth(4.4, 5.4, t)});
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        cull(ps, -197, -281, 197, 33);
        fadeEdges(ps, -197, 197, 34, 33);
        lights.update(dt);
    }
    void groundRing(Paint& p, bool back) const {
        const double k = (t - .07) / .8;
        if (k <= 0 || k >= 1) return;
        const std::array<uint32_t, 4> ramp{DUST[2], DUST[0], F[5], F[7]};
        band(p, 0, .5, 196 * ease::outCubic(k), .16, std::max(1., 5 * (1 - k)), 1.15 - k, ramp,
             [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; });
    }
    void skirtDraw(Paint& p, const Plume::Shape& S, bool back) const {
        const double u = (t - .55) / (1.75 - .55);
        if (u <= 0 || u >= 1) return;
        const double a = std::min(1., u / .06) * std::pow(1 - u, 1.5) * .6, capR = (S.ring + S.tube) * 1.1,
                     y0 = S.y + S.tube * .3, y1 = S.y * .55;
        if (!back)
            for (const auto& q : veil) {
                const double ev = u * u * q.e, R = capR * q.w * (1 + ev * .2);
                p.disc(std::cos(q.angle) * R + q.dx * ev * 10,
                       S.y + std::sin(q.angle) * (S.tube * 1.15 + 6) * q.w + q.dy * ev * 8, q.size * (1 - .5 * u),
                       WHITE, a * .38);
            }
        for (const auto& q : skirt) {
            const double z = std::sin(q.ph);
            if ((z < 0) != back) continue;
            const double R = lerp(capR * .82, 13, std::pow(q.v, .6)) * q.w, ev = u * u * q.e,
                         x = std::cos(q.ph) * R * (1 + ev * .25) + q.dx * ev * 10,
                         y = lerp(y0, y1, q.v) + z * R * .18 + q.dy * ev * 8;
            p.disc(x, y, q.size * (1 - .55 * u), WHITE, a * (back ? .3 : .55) * (1 - q.v * .3));
        }
        if (u < .7) {
            const double R = 13 * 1.25, k = 1 - u / .7;
            for (int i = 0; i < 64; ++i) {
                const double g = (i / 64.) * TAU, z = std::sin(g);
                if ((z < 0) != back) continue;
                p.dot(std::cos(g) * R * (1 + u * .6), y1 + 2 + z * R * .22, WHITE, k * (back ? .6 : 1));
            }
        }
    }
    void wash(Paint& p, double k) const {
        p.shader(-200, -284, 199, 35, [&](double x, double y, ShadeOutput& out) {
            const double d = jsHypot2(x / 196, (y + 40) / (y < -40 ? 240 : 72));
            out.l          = WHITE;
            out.k          = k * (1 - smooth(0, 1, d));
        });
    }
    void drawBack(Paint& p) const override {
        if (t < .05) return;
        const auto S = plume.shape();

        groundRing(p, true);
        drawPs(p, ps, Paint::Layer::Back, FR);
        skirtDraw(p, S, true);
        plume.draw(p, FR, CAP);
        const double E = plume.ember(), gone = smooth(plume.o.fade[0], plume.o.fade[1] - .6, t - .07);
        if (gone < 1) {
            const double hy = S.y + S.tube * .42, rx = S.ring * .7 + 5, ry = S.tube * .28 + 2;
            haze(
                p, 0, hy, rx, ry,
                [&](double d) { return d < .55 * E ? mix(CAP[4], FR[d < .3 * E ? 5 : 6], .75 * E) : CAP[4]; },
                [&](double d) {
                    return (d < .6 ? .8 : d < .85 ? .5 : .22) * (1 - gone);
                });
        }
        column.draw(p, top(), FR, DUST, clamp(1 - t / 1.8), E * .8);
        skirtDraw(p, S, false);
        pool(p, 0, 1, 140, 16, F[3], .3 * plume.hot());
    }
    void draw(Paint& p) const override {
        if (t < .05) {
            wash(p, 1.6);
            p.circle(0, -40, 64, WHITE);
            return;
        }
        groundRing(p, false);
        const double f = (t - .05) / .13;
        if (f < 1) {
            const double r = 22 + 8 * f;
            p.circle(0, -26, r + 1.5, F[2]);
            p.circle(0, -26, r, f < .5 ? F[6] : F[5]);
            p.circle(0, -26, r * .62, f < .5 ? WHITE : F[6]);
        }
        drawPs(p, ps, Paint::Layer::Front, FR);
        const double u = (t - .05) / .7;
        if (u < 1) {
            const double r = 30 + 250 * ease::outCubic(u), cy = -26;
            p.ring(0, cy, r, std::max(1., 2.5 * (1 - u)), u < .3 ? WHITE : F[6], [&](double, double a) {
                return cy + std::sin(a) * r > 0 || std::abs(std::cos(a) * r) > 197 || cy + std::sin(a) * r < -281
                           ? 0
                           : 1.1 - u;
            });
            glowDome(p, 0, cy, r, 7, F[5], .3 * (1 - u), {-197, -281, 196, 0});
        }
        const double w = std::exp(-(t - .05) / .13);
        if (w > .02) wash(p, .9 * w);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& nukeDescriptor() {
    static const std::array<Option, 1> options{
        {{"dust", 4278190080., 4294967295., false, 0, &PixelFxExplosionSettings::dust, true}}};
    static const Descriptor descriptor{
        {PixelFxKind::Nuke, "explosions/nuke", 400, 320, 200, 284, 9, false},
        8,
        [](PixelFxSettings& s) {
            s.seed           = 5;
            s.explosion.dust = hex(0x8a7466);
            s.palette        = {hex(0x4a1712), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Nuke>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
