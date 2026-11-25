// eve-hot-path
// Native adaptation of pixelfx shadow/dark-explosion.js, MIT.
// Sprite Fusion black-hole/explosion, Copyright (c) 2026 Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ShadowGeometry.h"
#include "stylize/pixelfx/ShadowSimulation.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double Y = -54, RS = 16, REL = .52, BURN = 1.62, POP = 1.84;
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct PuffEmission {
    shadow::PuffKind kind;
    double           x, y, vx, vy, life, size, delay = 0;
    bool             smokes = false;
};
struct Chip {
    double x, y, vx, vy, drag, life, angle, va, size;
};
struct VoidShard : Debris {
    bool flat = false, wisp = false;
};
struct Ring {
    double r0, r1, life, max, th;
    bool   hot;
    double delay;
};
struct DarkExplosion final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         R;
    Particles                   ps, embers;
    shadow::Puffs               puffs;
    BasicBits<VoidShard>        bits;
    Lights                      lights;
    std::vector<Ring>           rings;
    double                      t = 0, acc = 0, dustAcc = 0, flick = 1;
    bool                        released = false, collapsing = false, popped = false;
    explicit DarkExplosion(const PixelFxSettings& o) : C(o.palette), R(o.seed) {
        ps.sparks        = {WHITE, C[7], C[6], C[5]};
        embers.sparks    = {C[7], C[6], C[5], C[4]};
        const auto smoke = hex(0x5d5468);
        puffs.flame      = {WHITE, C[7], C[6], C[5], C[5], C[4], C[4], C[3], C[2]};
        puffs.soot       = {mix(mix(C[3], C[1], .3), smoke, .45), mix(C[1], smoke, .3)};
        puffs.cap        = mix(mix(C[4], C[2], .5), smoke, .4);
        puffs.rim        = {C[6], C[5], C[4]};
    }
    static double coreR(double tt) { return tt < REL - .07 ? 1.5 + 5 * ease::inQuad(tt / REL) : 1; }
    static double sphereR(double tt) {
        if (tt < REL) return 0;
        if (tt < BURN)
            return RS * ease::outBack(clamp((tt - REL) / .14), 2.4) *
                   (1 + .06 * smooth(REL + .3, BURN, tt) + .02 * std::sin(tt * 11));
        if (tt < POP) return RS * 1.08 * (1 - ease::inQuad((tt - BURN) / (POP - BURN)));
        return 0;
    }
    static double starL(double tt) {
        return tt < REL || tt > REL + .26 ? 0 : 60 * (1 - ease::inQuad(clamp((tt - REL - .05) / .21)));
    }
    static double burning(double tt) {
        return tt < REL ? clamp(tt / REL) * .7 : tt < BURN ? 1 : tt < POP ? 1.2 : std::max(0., 1 - (tt - POP) / .35);
    }
    static Point rim(double a, double r) { return {std::cos(a) * r, Y + std::sin(a) * r}; }
    void         emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void         ember(const Emission& e) { embers.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void         puff(const PuffEmission& e) {
        puffs.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.size, e.delay, e.smokes);
    }
    void chip(const Chip& e) {
        VoidShard b;
        b.kind            = DebrisKind::Shard;
        b.x               = e.x;
        b.y               = e.y;
        b.vx              = e.vx;
        b.vy              = e.vy;
        b.gravity         = .75;
        b.drag            = e.drag;
        b.bounce          = .35;
        b.life            = e.life;
        b.angle           = e.angle;
        b.angularVelocity = e.va;
        b.size            = e.size;
        bits.add(std::move(b));
    }
    void release() {
        lights.add(0, Y, 100, C[4], 1.25, .85);
        lights.add(0, Y, 86, mix(C[6], WHITE, .5), 1.3, .22);
        rings.push_back({RS, 104, .46, .46, 3.6, true, 0});
        rings.push_back({RS * .8, 62, .3, .3, 1.6, true, .02});
        rings.push_back({RS, 124, .62, .62, 1.2, false, .07});
        for (int i = 0; i < 34; ++i) {
            const bool   big = i < 20;
            const double a   = R.range(0, TAU);
            const auto   pt  = rim(a, RS * .7);
            const double sp  = big ? R.range(80, 170) : R.range(130, 220);
            chip({pt.x, pt.y, std::cos(a) * sp, std::sin(a) * sp - R.range(40, 110), big ? 1.3 : 1.7, R.range(1.6, 2.6),
                  R.range(0, TAU), R.range(-16, 16), big ? R.range(6, 11) : R.range(3, 4.5)});
        }
        for (int i = 0; i < 84; ++i) {
            const double a  = R.range(0, TAU);
            const auto   pt = rim(a, RS * .8);
            const double sp = R.range(120, 235);
            emit({ParticleKind::Spark, pt.x, pt.y, std::cos(a) * sp, std::sin(a) * sp - 50, R.range(.2, .7)});
        }
        for (int i = 0; i < 28; ++i) {
            const double a  = R.range(0, TAU);
            const auto   pt = rim(a, RS * .9);
            const double sp = R.range(30, 130);
            ember({ParticleKind::Ember, pt.x, pt.y, std::cos(a) * sp, std::sin(a) * sp - 40, R.range(1.3, 2.5)});
        }
        for (int i = 0; i < 24; ++i) {
            const double a  = R.range(0, TAU);
            const auto   pt = rim(a, RS * .9);
            const double sp = R.range(15, 70);
            puff({shadow::PuffKind::Soot, pt.x, pt.y, std::cos(a) * sp, std::sin(a) * sp * .8 - 14, R.range(2, 2.8),
                  R.range(7, 12), R.range(.02, .3)});
        }
        for (int i = 0; i < 12; ++i) {
            const double side = i % 2 ? 1 : -1;
            puff({shadow::PuffKind::Soot, side * R.range(10, 40), -R.range(2, 5), side * R.range(50, 120),
                  -R.range(0, 10), R.range(1.6, 2.4), R.range(5, 9), R.range(.08, .2)});
        }
        for (int i = 0; i < 10; ++i) {
            const double a  = -PI / 2 + R.range(-1.6, 1.6);
            const auto   pt = rim(a, RS);
            const double sp = R.range(30, 80);
            puff({shadow::PuffKind::Flame, pt.x, pt.y, std::cos(a) * sp, std::sin(a) * sp - 30, R.range(.3, .6),
                  R.range(2, 3.6), 0, R.isChance(.5)});
        }
        ps.list.clear();
        std::erase_if(puffs.list, [](const auto& q) { return q.delay <= 0; });
    }
    template <class List>
    static void shove(List& list) {
        for (auto& q : list) {
            const double dx = q.x, dy = q.y - Y, root = jsHypot2(dx, dy), d = root ? root : 1,
                         f = 90 * std::max(0., 1 - d / 70);
            q.vx += (dx / d) * f;
            q.vy += (dy / d) * f - f * .3;
        }
    }
    void pop() {
        lights.add(0, Y, 56, WHITE, .9, .18);
        lights.add(0, Y, 120, C[5], .8, .5);
        rings.push_back({2, 40, .28, .28, 2, true, 0});
        for (int i = 0; i < 26; ++i) {
            const double a = R.range(0, TAU), sp = R.range(120, 250);
            emit({ParticleKind::Spark, 0, Y, std::cos(a) * sp, std::sin(a) * sp - 40, R.range(.2, .6)});
        }
        for (int i = 0; i < 14; ++i) {
            const double a = R.range(0, TAU), sp = R.range(20, 90);
            ember({ParticleKind::Ember, 0, Y, std::cos(a) * sp, std::sin(a) * sp - 30, R.range(.9, 1.6)});
        }
        for (int i = 0; i < 10; ++i) {
            const double a = R.range(0, TAU), sp = R.range(20, 70);
            puff({shadow::PuffKind::Soot, std::cos(a) * 3, Y + std::sin(a) * 3, std::cos(a) * sp, std::sin(a) * sp - 15,
                  R.range(1.1, 1.7), R.range(3, 6), R.range(0, .08)});
        }
        shove(embers.list);
        shove(puffs.list);
        for (int i = 0; i < 12; ++i)
            puff({shadow::PuffKind::Soot, R.range(-6, 6), Y + R.range(-4, 6), R.range(-10, 10), -R.range(20, 55),
                  R.range(1.3, 1.8), R.range(7, 12), R.range(.02, .2)});
    }
    void update(double dt) override {
        t += dt;
        flick = .88 + .12 * std::sin(t * 37) * std::sin(t * 23);
        if (t < REL - .06) {
            const double k = t / REL;
            for (acc += dt * (90 + 150 * k); acc > 1; --acc) {
                const double a = R.range(0, TAU), D = R.range(36, 84), x = std::cos(a) * D,
                             y = std::min(-3., Y + std::sin(a) * D), vt = R.range(50, 110), vi = R.range(10, 40);
                emit({ParticleKind::Mote, x, y, -std::sin(a) * vt - std::cos(a) * vi,
                      std::cos(a) * vt - std::sin(a) * vi, .9,
                      R.pick<uint32_t>(std::array<uint32_t, 4>{WHITE, C[6], C[6], C[5]}), R.range(.7, 1.2)});
            }
            if (R.isChance(.75)) {
                const double a = R.range(0, TAU), D = R.range(28, 70), T = R.range(.18, .28),
                             v = (D * 3) / (std::exp(3 * T) - 1);
                emit({ParticleKind::Streak, std::cos(a) * D, Y + std::sin(a) * D, -std::cos(a) * v, -std::sin(a) * v, T,
                      R.isChance(.5) ? C[7] : C[6]});
            }
            if (t < REL - .2 && R.isChance(.16)) {
                const double a = R.range(0, TAU), D = R.range(34, 60);
                puff({shadow::PuffKind::Soot, std::cos(a) * D, Y + std::sin(a) * D, -std::sin(a) * 40, std::cos(a) * 40,
                      R.range(.6, .9), R.range(3, 6)});
            }
            (void)shadow::attract(
                ps.list, 0, Y, 2.2e6 * (.3 + k), 110, .55, 2.5, dt,
                [](const auto& q) { return q.kind == ParticleKind::Mote ? 1. : 0.; }, 420);
            (void)shadow::attract(
                puffs.list, 0, Y, 1.2e6 * (.3 + k), 90, .7, 4, dt, [](const auto&) { return 1.; }, 160);
        }
        if (t >= REL && !released) {
            released = true;
            release();
        }
        if (released && t < BURN) {
            const double sr = sphereR(t);
            for (acc += dt * 34; acc > 1; --acc) {
                const double a  = -PI / 2 + R.gauss() * 1.1;
                const auto   pt = rim(a, sr + 1);
                const double sp = R.range(10, 35);
                puff({shadow::PuffKind::Flame, pt.x, pt.y, std::cos(a) * sp, std::sin(a) * sp - R.range(10, 30),
                      R.range(.25, .45), R.range(1.2, 2.2), 0, R.isChance(.2)});
            }
            if (R.isChance(dt * 10)) {
                const double a  = -PI / 2 + R.range(-1.4, 1.4);
                const auto   pt = rim(a, sr * .9);
                puff({shadow::PuffKind::Soot, pt.x, pt.y, std::cos(a) * 20, -R.range(15, 35), R.range(1.2, 1.7),
                      R.range(5, 9)});
            }
            if (R.isChance(dt * 8)) {
                const double a  = R.range(0, TAU);
                const auto   pt = rim(a, sr);
                const double sp = R.range(110, 200);
                emit({ParticleKind::Spark, pt.x, pt.y, std::cos(a) * sp, std::sin(a) * sp - 40, R.range(.2, .5)});
            }
            if (R.isChance(dt * 7)) {
                const double a  = -PI / 2 + R.range(-1.2, 1.2);
                const auto   pt = rim(a, sr);
                ember({ParticleKind::Ember, pt.x, pt.y, std::cos(a) * 30, -R.range(20, 50), R.range(.9, 1.6)});
            }
        }
        if (t >= BURN && !collapsing) {
            collapsing = true;
            for (int i = 0; i < 14; ++i) {
                const double a = R.range(0, TAU), D = R.range(26, 48), T = R.range(.14, .2),
                             v = (D * 3) / (std::exp(3 * T) - 1);
                emit({ParticleKind::Streak, std::cos(a) * D, Y + std::sin(a) * D, -std::cos(a) * v, -std::sin(a) * v, T,
                      R.isChance(.5) ? WHITE : C[6]});
            }
        }
        if (collapsing && !popped)
            (void)shadow::attract(
                puffs.list, 0, Y, 1.4e6, 64, .6, 3, dt,
                [](const auto& q) { return q.kind == shadow::PuffKind::Flame ? 1. : .35; }, 200);
        if (t >= POP && !popped) {
            popped = true;
            pop();
        }
        for (const auto& r : rings) {
            if (r.delay > 0 || !r.hot || r.th < 3) continue;
            const double rr = r.r0 + (r.r1 - r.r0) * (1 - std::pow(r.life / r.max, 3)),
                         rf = rr > -Y ? std::sqrt(rr * rr - Y * Y) : 0;
            if (rf > 0)
                for (dustAcc += dt * 60; dustAcc > 1; --dustAcc) {
                    const double side = R.sign();
                    emit({ParticleKind::Dust, side * rf + R.range(-2, 2), -R.range(1, 3), side * R.range(20, 50),
                          -R.range(4, 14), R.range(.4, .8), mix(C[2], C[3], R.next()), R.range(1.5, 3)});
                }
        }
        lights.update(dt);
        for (size_t i = rings.size(); i-- > 0;) {
            auto& q = rings[i];
            if (q.delay > 0)
                q.delay -= dt;
            else if ((q.life -= dt) <= 0)
                rings.erase(rings.begin() + std::ptrdiff_t(i));
        }
        const auto floor = [](double) { return 0.; };
        bits.update(dt, floor);
        for (auto& b : bits.list) {
            if (b.rest && !b.flat) {
                b.flat  = true;
                b.angle = jsRound(b.angle / PI) * PI + R.range(-.2, .2);
                b.y     = -1;
            }
            if (b.life < .45 && !b.wisp) {
                b.wisp = true;
                puff({shadow::PuffKind::Soot, b.x, b.y - 1, R.range(-4, 4), -R.range(8, 16), R.range(.6, .9),
                      R.range(1.4, 2.4)});
            }
        }
        puffs.update(dt, R, floor);
        ps.update(dt, R, floor, [](double, double) {});
        embers.update(dt, R, floor, [](double, double) {});
    }
    void sphere(Paint& p, double r, double heat) const {
        if (r < .6) return;
        const double E = r + 12 * heat + 2;
        p.shader(-E, Y - E, E, Y + E, [&](double x, double y, ShadeOutput& out) {
            const double dx = x, dy = y - Y, d = std::sqrt(dx * dx + dy * dy);
            if (d > E) return;
            if (d < r - .5) {
                const double e = r - d, ang = std::atan2(dy, dx);
                if (e < 1.4 && std::cos(ang - .8) > .55) {
                    out.c = C[2];
                    return;
                }
                if (r > 8 && std::pow(dx + r * .45, 2) + std::pow(dy + r * .45, 2) < 1.6) {
                    out.c = C[3];
                    return;
                }
                out.c = C[0];
                return;
            }
            const double e = d - (r - .5);
            if (e < 1) {
                out.c = std::sin(std::atan2(dy, dx) * 7 + t * 9) > -.6 ? WHITE : C[7];
                out.k = .4;
                return;
            }
            const double ang = std::atan2(dy + e * .6, dx), top = std::max(0., -std::sin(ang)), a1 = ang - t * .9,
                         a2 = ang + t * 1.7;
            const double i1 = jsRound((a1 * 9) / TAU), i2 = jsRound((a2 * 15) / TAU);
            const double T1 = std::pow(.5 + .5 * std::cos(a1 * 9), 3) * (.55 + .45 * noise(t * 7 + i1 * 3.7, 1));
            const double T2 = std::pow(.5 + .5 * std::cos(a2 * 15), 4) * (.5 + .5 * noise(t * 11 + i2 * 5.1, 2));
            const double h  = heat * (.3 + .95 * top) * (1.5 + 12 * std::max(0., T1) + 6 * std::max(0., T2));
            if (e > h + 1) return;
            const double u = (e - 1) / h;
            out.c          = u < .18 ? WHITE : u < .4 ? C[7] : u < .62 ? C[6] : u < .84 ? C[5] : C[4];
            out.a          = u < .8 ? 1 : (1 - u) * 5;
            if (u < .6 && ((int(std::floor(x)) + int(std::floor(y))) & 1) == 0) out.k = .25 * (1 - u);
        });
    }
    void floorRing(Paint& p, const Ring& r, bool back) const {
        if (r.delay > 0 || !r.hot || r.th < 3) return;
        const double q = r.life / r.max, rr = r.r0 + (r.r1 - r.r0) * (1 - q * q * q);
        if (rr <= -Y) return;
        const double                  rf = std::sqrt(rr * rr - Y * Y);
        const std::array<uint32_t, 4> ramp{C[3], C[4], C[5], C[6]};
        band(p, 0, .5, rf, .16, std::max(1., 3 * q), std::min(1., q * 1.5), ramp,
             [&](double g) { return (std::sin(g) < 0) == back; });
        if (!back)
            for (double side : {-1., 1.}) p.glow(side * rf, -2, 16, C[5], .4 * q);
    }
    void drawBack(Paint& p) const override {
        const double k = burning(t);
        for (const auto& r : rings) floorRing(p, r, true);
        const shadow::PuffLight light{0, Y, k * flick, 84};
        puffs.draw(p, shadow::PuffKind::Soot, &light);
        ps.draw(p, Paint::Layer::Back);
        if (k > 0) p.glow(0, -3, 74, C[4], .24 * std::min(1., k) * flick);
    }
    void draw(Paint& p) const override {
        const double sr = sphereR(t), k = burning(t), L = starL(t);
        for (const auto& r : rings) floorRing(p, r, false);
        double cr = 0;
        if (t < REL) {
            const double q = t / REL, ring = 3 + 74 * (1 - ease::inQuad(q));
            const double beat = 1 + .3 * std::exp(-std::pow(t - REL * .5, 2) / .0015) +
                                .4 * std::exp(-std::pow(t - REL * .8, 2) / .0015);
            cr                = coreR(t) * beat;
            shadow::ringFast(p, 0, Y, ring, 1, q > .7 ? C[6] : C[5], .2 + .7 * q);
            if (t > REL - .07)
                p.circle(0, Y, 1.6, WHITE);
            else
                sphere(p, cr, .3 + .3 * q);
            shadow::halo(p, 0, Y, cr, cr + 14 + 30 * q, C[5], (.45 + .7 * q) * beat);
            p.glow(0, Y, 60 + 30 * q, C[4], .12 + .3 * q);
        }
        const double fu = (t - REL) / .12;
        if (fu >= 0 && fu < 1) {
            const double r = 36 * (1 - .3 * fu);
            p.circle(0, Y, r + 1.5, C[5], 1.2 - fu);
            p.circle(0, Y, r, fu < .35 ? WHITE : C[6], 1.1 - fu);
            if (fu < .6) p.circle(0, Y, r * .62, WHITE);
        }
        for (const auto& r : rings) {
            if (r.delay > 0) continue;
            const double q = r.life / r.max, rr = r.r0 + (r.r1 - r.r0) * (1 - q * q * q),
                         th = std::max(1., r.th * q + .4);
            shadow::ringFast(
                p, 0, Y, rr, th,
                [&](double v, double) {
                    return r.hot ? (v > .6 ? WHITE : v > .3 ? C[6] : C[5]) : v > .5 ? C[5] : C[4];
                },
                [&](double, double g) { return Y + std::sin(g) * rr > .5 ? 0 : std::min(1., q * 1.6); });
            if (r.hot) shadow::ringLight(p, 0, Y, rr, 4, C[5], .4 * q, [](double, double y) { return y < .5; });
        }
        sphere(p, sr, t < BURN ? 1 : 1 + .5 * clamp((t - BURN) / (POP - BURN)));
        std::optional<shadow::BlackStar> star;
        if (L > 0) {
            star.emplace(Y, L, .32 + (t - REL) * 2.2, .5, .4);
            const double age = t - REL, fr = age < .12 ? 36 * (1 - .3 * (age / .12)) + 1.5 : 0, cut = std::max(fr, sr);
            star->draw(p, C[0], age < .1 ? C[5] : C[4],
                       [&](double x, double y) { return x * x + (y - Y) * (y - Y) < cut * cut; });
        }
        puffs.draw(p, shadow::PuffKind::Flame);
        for (const auto& b : bits.list) shadow::shard(p, b, C, 0, Y, std::min(1., k) * .9);
        ps.draw(p, Paint::Layer::Front);
        embers.draw(p, Paint::Layer::Front);
        const double pu = (t - POP) / .2;
        if (pu >= 0 && pu < 1) p.sparkle(0, Y, 12 * (1 - pu), C[6], C[5], .7 * (1 - pu));
        lights.draw(p);
        if (sr > 1) {
            shadow::halo(p, 0, Y, sr - .5, sr + 16, C[6], .5 * flick * std::min(1., k));
            p.glow(0, Y, sr + 54, C[4], .5 * flick * std::min(1., k));
        }
        const double B0 = std::max(sr, cr) + 1;
        if (B0 > 2)
            shadow::unlight(p, -B0, Y - B0, B0, Y + B0, [&](double x, double y) {
                return x * x + (y - Y) * (y - Y) < std::pow(std::max(sr, cr) - .5, 2);
            });
        if (star) star->unlight(p);
    }
};
}  // namespace
const Descriptor& darkExplosionDescriptor() {
    static const Descriptor d{
        {PixelFxKind::DarkExplosion, "shadow/dark-explosion", 280, 248, 140, 206, 3.8, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 6;
            s.palette = {hex(0x06040b), hex(0x150c26), hex(0x2a1150), hex(0x461a84),
                         hex(0x6f2cc0), hex(0x9d5cec), hex(0xcfa6ff), hex(0xf6eaff)};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<DarkExplosion>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
