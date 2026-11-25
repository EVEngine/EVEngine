// eve-hot-path
// Adapted from pixelfx explosions/smoke-bomb.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Explosion.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct Canister {
    double x = -70, y = -60, vx = 125, vy = -80, va = 7, angle = 0;
    bool   rest = false;
};
struct SmokeLobe {
    int    kind;
    double t0, vx, vh, d, r0, r1, f, wob;
    bool   on = false;
    double x = 0, h = 0, age = 0;
};
struct SmokeBomb final : Kernel {
    std::vector<uint32_t>   C, K, S;
    std::array<uint32_t, 5> SM;
    Rng                     rng;
    Particles               ps;
    Lights                  lights;
    Canister                can;
    std::vector<SmokeLobe>  lobes;
    double                  t      = 0;
    bool                    popped = false;
    template <class Hit>
    static void step(Canister& c, double dt, Hit hit) {
        if (c.rest) return;
        c.vy += 900 * dt;
        c.x += c.vx * dt;
        c.y += c.vy * dt;
        if (c.y >= -4 && c.vy > 0) {
            c.y = -4;
            if (c.vy > 80) {
                hit(c.x, c.vy);
                c.vy = -c.vy * .38;
                c.vx *= .78;
            } else
                c.vy = 0;
        }
        if (c.vy == 0 && c.y >= -4 - .01) {
            c.vx -= c.vx * 3 * dt;
            if (std::abs(c.vx) < 2) {
                c.vx   = 0;
                c.rest = true;
            }
        }
        c.angle += (c.vy == 0 ? c.vx / 4 : c.va) * dt;
    }
    explicit SmokeBomb(const PixelFxSettings& o)
        : C(o.palette), K(o.explosion.canister), S(o.explosion.sparks), SM{C[5], C[4], C[3], C[2], C[1]}, rng(o.seed) {
        ps.sparks    = S;
        Canister sim = can;
        for (int i = 0; i < 2400 && !sim.rest; ++i) step(sim, 1. / 240, [](double, double) {});
        can.x -= sim.x;
        const std::array<int, 6> kinds{0, 0, 1, 1, 2, 3};
        for (int i = 0; i < 52; ++i) {
            const int    kind = kinds[size_t(i) % 6], side = rng.sign();
            const double ang = kind == 0 || kind == 3 ? (side > 0 ? -rng.range(0, .25) : PI + rng.range(0, .25))
                                                      : -PI / 2 + rng.range(-.6, .6);
            // Both source array literals consume all four RNG samples before indexing.
            const std::array<double, 4> speeds{rng.range(70, 170), rng.range(0, 25), rng.range(45, 95),
                                               rng.range(20, 60)},
                radii{rng.range(12, 17), rng.range(16, 22), rng.range(13, 18), rng.range(14, 19)};
            const double sp = speeds[kind], r1 = radii[kind],
                         t0 = kind == 3 ? .95 + rng.range(.45, 1.5)
                                        : .95 + .6 * std::pow(i / 51., 1.6) + rng.range(0, .03);
            lobes.push_back({kind, t0, std::cos(ang) * sp, -std::sin(ang) * sp, rng.range(-1, 1), rng.range(5, 8), r1,
                             rng.range(-.45, .4), rng.range(0, TAU)});
        }
    }
    void   emit(Emission q) { ps.spawn(rng, q.kind, q.x, q.y, q.vx, q.vy, q.life, q.color, q.size); }
    Point  fuse() const { return {can.x + std::sin(can.angle) * 6.5, can.y - std::cos(can.angle) * 6.5}; }
    double thin(const SmokeLobe& b) const { return smooth(2.5 + b.f, 3.9 + b.f * .6, t); }
    double radius(const SmokeLobe& b) const {
        return (b.r0 + (b.r1 - b.r0) * (1 - std::exp(-b.age / .28))) * (1 + .05 * std::sin(t * 3 + b.wob)) *
               (1 + .35 * thin(b));
    }
    void update(double dt) override {
        t += dt;
        const std::array<uint32_t, 3> grit{hex(0x5a4f6b), hex(0x3e3650), hex(0x776c88)};
        step(can, dt, [&](double x, double) {
            for (int i = 0; i < 3; ++i)
                emit({ParticleKind::Spark, x, -1, rng.range(-70, 70), -rng.range(40, 110), rng.range(.06, .14)});
            for (int i = 0; i < 3; ++i)
                emit({ParticleKind::Dust, x + rng.range(-2, 2), -.5, rng.signedRange(10, 40), -rng.range(4, 14),
                      rng.range(.3, .55), grit[2], rng.range(1.4, 2.4)});
        });
        if (t < .95) {
            const auto   f     = fuse();
            const double hurry = smooth(.95 - .4, .95, t);
            if (rng.isChance((26 + 50 * hurry) * dt))
                emit({ParticleKind::Spark, f.x, f.y, rng.range(-50, 50), -rng.range(30, 100), rng.range(.08, .18)});
            if (rng.isChance(8 * dt))
                emit({ParticleKind::Vapour, f.x, f.y - 1, rng.range(-4, 4), -rng.range(10, 18), rng.range(.5, .8), C[5],
                      rng.range(1, 1.8)});
        } else if (!popped) {
            popped = true;
            lights.add(can.x, -9, 64, C[6], .75, .22);
            lights.add(can.x, -5, 22, WHITE, .7, .06);
            for (int i = 0; i < 30; ++i) {
                const double a = rng.range(-PI - .35, .35), sp = rng.range(120, 300);
                emit({ParticleKind::Spark, can.x, -5, std::cos(a) * sp, std::sin(a) * sp - 40, rng.range(.15, .4)});
            }
            for (int i = 0; i < 26; ++i) {
                const double u = rng.range(-1, 1), a = -PI / 2 + u * .9,
                             sp = rng.range(80, 220) * (1 - .35 * std::abs(u));
                emit({ParticleKind::Grit, can.x + u * 4, -1, std::cos(a) * sp, std::sin(a) * sp, rng.range(.9, 1.6),
                      rng.pick<uint32_t>(grit), rng.isChance(.35) ? 2. : 1.});
            }
            can.vy   = -95;
            can.vx   = rng.range(-25, 25);
            can.va   = rng.sign() * 9;
            can.rest = false;
        }
        const double wind = 6 * smooth(1.8, 3.2, t);
        for (auto& b : lobes) {
            if (!b.on) {
                if (t < b.t0) continue;
                b.on = true;
                b.x  = can.x + rng.range(-2, 2);
                b.h  = 1;
            }
            b.age += dt;
            b.vx -= b.vx * 2.4 * dt;
            const std::array<double, 4> rise{14 * smooth(.7, 1.6, b.age), 3 + 9 * smooth(.8, 1.8, b.age),
                                             6 + 12 * smooth(.3, 1.4, b.age), 10 * smooth(.8, 1.6, b.age)};
            b.vh += rise[b.kind] * dt;
            b.vh -= b.vh * 1.6 * dt;
            b.x += (b.vx + wind) * dt;
            b.h            = std::max(0., b.h + b.vh * dt);
            const double k = thin(b);
            if (k > .12 && k < .85 && rng.isChance(1.8 * dt)) {
                const double r = radius(b), life = std::min(rng.range(1, 1.6), 5 - .15 - t);
                if (life > .3)
                    emit({ParticleKind::Vapour, b.x + rng.range(-r, r) * .5, -(b.h + r * 1.4), wind + rng.range(-6, 10),
                          -rng.range(8, 20), life, C[4], r * rng.range(.3, .45)});
            }
        }
        if (t > .95 + .5 && t < 4.1 && rng.isChance(4 * dt))
            emit({ParticleKind::Vapour, can.x, can.y - 3, rng.range(-3, 3), -rng.range(8, 16), rng.range(.6, .9), C[4],
                  rng.range(1.5, 2.5)});
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        explosion::cull(ps, -127, -153, 127, 17);
        explosion::fadeEdges(ps, -127, 127, 20, 17);
        lights.update(dt);
    }
    void clouds(Paint& p, bool back) const {
        double top = 0;
        for (const auto& b : lobes)
            if (b.on) top = std::min(top, -(b.h + radius(b) * 1.92));
        std::vector<const SmokeLobe*> on;
        on.reserve(lobes.size());
        for (const auto& b : lobes)
            if (b.on && (b.d < -.35) == back) on.push_back(&b);
        std::stable_sort(on.begin(), on.end(), [](const auto* a, const auto* b) { return a->d < b->d; });
        for (const auto* ptr : on) {
            const auto&  b = *ptr;
            const double r = radius(b), y = -(b.h + r * .92) + b.d * 2.5, k = thin(b),
                         a = std::min(1., b.age / .05) * std::pow(1 - k, 1.4) * .97 *
                             (1 - smooth(-156 + 34, -156 + 6, y - r));
            if (a <= .01) continue;
            const int shade = (y - top) / std::max(20., -top) > .55 ? 1 : 0;
            explosion::lobe(p, b.x, y, r, SM, SM, 0, 0, a, b.d, std::min(2, shade + (k > .6 ? 1 : 0)), 1);
        }
    }
    void drawCanister(Paint& p, double alpha) const {
        const double                  ca = std::cos(can.angle), sa = std::sin(can.angle);
        const std::array<uint32_t, 3> band{hex(0x3a1418), hex(0x7a2a24), hex(0xb84a34)};
        p.shader(can.x - 6, can.y - 6, can.x + 6, can.y + 6, [&](double px, double py, ShadeOutput& out) {
            const double dx = px - can.x, dy = py - can.y, d = jsHypot2(dx, dy);
            if (d > 5) return;
            out.a = alpha;
            if (d > 4) {
                out.c = hex(0x1a1422);
                return;
            }
            const double v = -dx * sa + dy * ca, lit = -(dx * .6 + dy * .8) / 4;
            if (std::abs(v) < .9) {
                out.c = band[lit > .3 ? 2 : lit > -.4 ? 1 : 0];
                return;
            }
            if (d < 2 && lit > .45) {
                out.c = K[4];
                return;
            }
            out.c = K[lit > .5 ? 3 : lit > 0 ? 2 : lit > -.5 ? 1 : 0];
        });
        const double cx = can.x + sa * 4, cy = can.y - ca * 4;
        if (t < .95)
            p.line(cx, cy, cx + sa * 2.5, cy - ca * 2.5, hex(0x8a6a48), alpha);
        else
            p.dot(cx, cy, K[0], alpha);
    }
    void drawBack(Paint& p) const override {
        ps.draw(p, Paint::Layer::Back);
        clouds(p, true);
    }
    void draw(Paint& p) const override {
        const double fade = 1 - smooth(4.3, 4.8, t);
        if (fade > 0) drawCanister(p, fade);
        if (t < .95) {
            const auto f = fuse();
            p.px(f.x, f.y, std::sin(t * 50) > 0 ? WHITE : S[1]);
            p.glow(f.x, f.y, 6, S[2], .45 + .2 * std::sin(t * 37));
        }
        clouds(p, false);
        ps.draw(p, Paint::Layer::Front);
        const double u = t - .95;
        if (u >= 0 && u < .067) {
            p.circle(can.x, -5, 6.5, C[5]);
            p.circle(can.x, -5, 4.5, WHITE);
        }
        if (u >= 0 && u < .16) {
            const std::array<uint32_t, 4> ramp{C[3], C[4], C[5], C[6]};
            flare(p, can.x, -5, 22 * (1 - u / .16), 1 - u / .16, ramp);
        }
        if (u >= 0 && u < .35) explosion::pool(p, can.x, 1, 50, 8, C[6], .5 * (1 - u / .35));
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& smokeBombDescriptor() {
    static const std::array<Option, 2> options{{{"canister", 1, 64, false, 5, &PixelFxExplosionSettings::canister},
                                                {"sparks", 1, 64, false, 4, &PixelFxExplosionSettings::sparks}}};
    static const Descriptor            descriptor{
        {PixelFxKind::SmokeBomb, "explosions/smoke-bomb", 260, 176, 130, 156, 5, false},
        7,
        [](PixelFxSettings& s) {
            s.seed               = 3;
            s.palette            = {hex(0x0e0b14), hex(0x1e1828), hex(0x30283e), hex(0x463c58),
                                    hex(0x62567a), hex(0x867a9e), hex(0xaea4c4)};
            s.explosion.canister = {hex(0x16121e), hex(0x2a2434), hex(0x443c52), hex(0x686078), hex(0x9c94ac)};
            s.explosion.sparks   = {hex(0xfff3a6), hex(0xffe26b), hex(0xff8a2a), hex(0xf2561d)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<SmokeBomb>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
