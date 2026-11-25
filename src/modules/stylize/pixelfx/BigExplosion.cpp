// eve-hot-path
// Adapted from pixelfx explosions/big-explosion.js (MIT).
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
struct Flash {
    double x, y, r, life, max;
};
struct Ring {
    double   x, y, r0, r1, life, max, thickness;
    uint32_t color;
};
struct MoltenRock : Debris {
    double                  heat = 0, pvy = 0;
    std::vector<TrailPoint> points;
};
struct Blast {
    double t, x, y, radius, power;
    bool   main = false;
};
constexpr std::array<Blast, 3> BLASTS{{{0, 0, -9, 30, 1.45, true}, {.13, -17, -30, 17, .9}, {.25, 15, -46, 15, .8}}};
const std::array<uint32_t, 5>  SM{{hex(0x83777f), hex(0x6e6470), hex(0x575060), hex(0x3f3947), hex(0x29242f)}};
PlumeOptions                   plumeOptions() {
    PlumeOptions o;
    o.y       = -34;
    o.rise    = 118;
    o.tau     = 1.25;
    o.tube    = {9, 21};
    o.ring    = {2, 19};
    o.grow    = 1;
    o.n       = 42;
    o.size    = {6, 11};
    o.swell   = .9;
    o.spin    = 2.6;
    o.spinTau = 1.7;
    o.heat    = 1.2;
    o.cool    = .42;
    o.tilt    = .22;
    o.squash  = .72;
    o.fade    = {4.4, 6.4};
    o.spread  = 22;
    return o;
}
struct BigExplosion final : Kernel {
    std::vector<uint32_t>    F;
    std::array<uint32_t, 9>  FR;
    std::array<uint32_t, 10> HR;
    Stone                    stone;
    Rng                      rng;
    Particles                ps;
    Blazes                   blazes;
    BasicBits<MoltenRock>    rocks;
    Lights                   lights;
    std::vector<Flash>       flashes;
    std::vector<Ring>        rings;
    Plume                    plume;
    Column                   column;
    double                   t    = 0;
    size_t                   next = 0;
    explicit BigExplosion(const PixelFxSettings& o)
        : F(o.palette),
          FR(fireRamp(F)),
          HR(heatRamp(FR)),
          stone(o.explosion.stone),
          rng(o.seed),
          plume(rng, plumeOptions()),
          column(ColumnOptions{5, 18, 85, {7, 11}, 28, 2.5, 0}) {
        ps.sparks = {F[6], F[5], F[3], F[2]};
    }
    void   emit(Emission q) { ps.spawn(rng, q.kind, q.x, q.y, q.vx, q.vy, q.life, q.color, q.size, q.delay, q.flags); }
    double top() const {
        const auto S = plume.shape();
        return S.y + S.tube * .45;
    }
    void blast(const Blast& event) {
        const double x = event.x, y = event.y, rad = event.radius, power = event.power;
        const bool   main = event.main;
        if (!main) {
            flashes.push_back({x, y, rad * 1.05, .06, .06});
            rings.push_back({x, y, rad * .45, rad * 2.7, .36, .36, 3, F[5]});
            rings.push_back({x, y, rad * .3, rad * 1.6, .24, .24, 1.8, F[3]});
        }
        lights.add(x, main ? y - 24 : y, main ? 72 : rad * 3.4, mix(F[3], F[6], .45), .6 * power, .8);
        lights.add(x, main ? y - 12 : y, main ? 44 : rad * 1.6, WHITE, .6, .1);
        const double down = main ? .25 : PI;
        for (int i = 0; i < 5; ++i)
            emit({ParticleKind::Fire, x + rng.range(-2.5, 2.5), y + rng.range(-2.5, 2.5), rng.range(-20, 20),
                  rng.range(-40, -10), rng.range(.18, .3), 0, rng.range(rad * .4, rad * .55)});
        for (int i = 0; i < int(jsRound(26 * power)); ++i) {
            const double a = main ? rng.range(-PI - down, down) : rng.range(0, TAU), d = rng.range(0, rad * .55),
                         sp = rng.range(40, 200) * power;
            emit({ParticleKind::Fire, x + std::cos(a) * d, y + std::sin(a) * d, std::cos(a) * sp,
                  std::sin(a) * sp * .8 - 40, rng.range(.55, 1.15), 0, rng.range(rad * .22, rad * .46), 0,
                  rng.isChance(.45) ? ParticleFlag::Smokes | ParticleFlag::Big : ParticleFlag::None});
        }
        for (int i = 0; i < 5; ++i) {
            const double a = rng.range(-PI, 0), d = rng.range(rad * .1, rad * .5);
            emit({ParticleKind::Smoke, x + std::cos(a) * d, y + std::sin(a) * d, std::cos(a) * 30,
                  std::sin(a) * 20 - 30, rng.range(.9, 1.5), hex(0x221d26), rng.range(rad * .15, rad * .26),
                  rng.range(.06, .2), ParticleFlag::Big});
        }
        for (int i = 0; i < int(jsRound(8 * power)); ++i) {
            const double a = rng.range(-PI, 0), d = rng.range(0, rad * .7), sp = rng.range(15, 90);
            emit({ParticleKind::Smoke, x + std::cos(a) * d, y + std::sin(a) * d, std::cos(a) * sp,
                  std::sin(a) * sp * .6 - 35, rng.range(1.8, 2.8), 0, rng.range(rad * .26, rad * .4),
                  rng.range(.12, .45), ParticleFlag::Big});
        }
        for (int i = 0; i < int(jsRound(80 * power)); ++i) {
            const double a = rng.range(0, TAU), sp = rng.range(170, main ? 470 : 380);
            emit({ParticleKind::Spark, x, y, std::cos(a) * sp, std::sin(a) * sp - 70, rng.range(.2, .65)});
        }
        for (int i = 0; i < int(jsRound(30 * power)); ++i) {
            const double a = rng.range(0, TAU), sp = rng.range(30, 150);
            emit({ParticleKind::Ember, x + std::cos(a) * rad * .4, y + std::sin(a) * rad * .4, std::cos(a) * sp,
                  std::sin(a) * sp - 75, rng.range(1.4, 3), F[3]});
        }
        if (!main) return;
        for (int i = 0; i < 14; ++i) {
            const double a = -PI / 2 + rng.range(-1.3, 1.3), sp = rng.range(160, 340);
            emit({ParticleKind::Brand, x, y, std::cos(a) * sp, std::sin(a) * sp, rng.range(.8, 1.5)});
        }
        for (int i = 0; i < 18; ++i) {
            const int side = rng.sign();
            emit({ParticleKind::Smoke, side * rng.range(0, 30 * .6), -rng.range(1, 6), side * rng.range(3, 9) * 30,
                  -rng.range(2, 15), rng.range(1.3, 2.2), stone.dust, rng.range(8, 12), rng.range(0, .06),
                  ParticleFlag::Big});
        }
        for (int i = 0; i < int(jsRound(1.6 * 12)); ++i)
            emit({ParticleKind::Smoke, rng.range(-.3, .3) * 30, -rng.range(2, 30 * .6), rng.range(-15, 15),
                  -rng.range(60, 190) * 1.6 * .6, rng.range(1.6, 2.6), stone.dust, rng.range(8, 12), rng.range(0, .15),
                  ParticleFlag::Big});
        const std::array<uint32_t, 4> stones{stone.light, stone.body, stone.body, stone.shade};
        for (int i = 0; i < int(jsRound(1.6 * 30 * 4)); ++i) {
            const double u = rng.range(-1, 1), a = -PI / 2 + u * .55,
                         sp = rng.range(5, 15) * 30 * (1 - .45 * std::abs(u));
            emit({ParticleKind::Grit, u * .25 * 30, -1, std::cos(a) * sp, std::sin(a) * sp, rng.range(1.4, 2.4),
                  rng.pick<uint32_t>(stones), rng.isChance(.45) ? 2. : 1.});
        }
        for (int i = 0; i < 26; ++i) {
            const bool big = i < 10, hot = rng.isChance(.45);
            const int  side = rng.sign();
            MoltenRock b;
            b.kind            = DebrisKind::Chunk;
            b.x               = rng.range(-.4, .4) * 30;
            b.y               = -4;
            b.vx              = side * rng.range(30, big ? 120 : 150);
            b.vy              = -rng.range(big ? 280 : 320, big ? 480 : 560);
            b.gravity         = 1;
            b.drag            = .3;
            b.bounce          = .3;
            b.life            = rng.range(3.6, 5);
            b.angle           = rng.range(0, TAU);
            b.angularVelocity = rng.range(-14, 14);
            b.size            = big ? rng.range(2.6, 3.8) : rng.range(1.2, 2.4);
            b.heat            = hot ? rng.range(.6, 1) : 0;
            rocks.add(std::move(b));
        }
    }
    void update(double dt) override {
        t += dt;
        while (next < BLASTS.size() && t >= BLASTS[next].t) blast(BLASTS[next++]);
        if (t > .08) plume.update(dt);
        const auto   S = plume.shape();
        const double k = 1 - smooth(2.2, 3.1, t);
        if (t > .2 && t < 3.1) {
            if (rng.isChance(8 * k * dt))
                emit({ParticleKind::Smoke, rng.range(-10, 10), -rng.range(1, 6), rng.range(-8, 8), -rng.range(40, 80),
                      rng.range(1.5, 2.3), stone.dust, rng.range(5, 8), 0, ParticleFlag::Big});
            if (t < 1.5 && rng.isChance(16 * dt))
                emit({ParticleKind::Fire, rng.range(-6, 6), -rng.range(4, 14), rng.range(-10, 10), -rng.range(90, 150),
                      rng.range(.5, .8), 0, rng.range(4, 7.5), 0,
                      rng.isChance(.5) ? ParticleFlag::Smokes | ParticleFlag::Big : ParticleFlag::None});
            if (t > .45 && rng.isChance(12 * k * dt)) {
                const int side = rng.sign();
                emit({ParticleKind::Smoke, side * rng.range(40, 120), -rng.range(1, 4), -side * rng.range(10, 30),
                      -rng.range(0, 5), rng.range(1.3, 1.9), stone.dust, rng.range(3.5, 5.5), 0, ParticleFlag::Big});
            }
        }
        if (t > .3 && t < 3 && rng.isChance(10 * plume.hot() * dt)) {
            const double a = rng.range(0, TAU);
            emit({ParticleKind::Ember, S.x + std::cos(a) * (S.ring + S.tube), S.y + std::sin(a) * S.tube * .5,
                  std::cos(a) * rng.range(40, 90), -rng.range(10, 60), rng.range(1.2, 2.4), F[3]});
        }
        const std::array<uint32_t, 3> ash{SM[1], SM[2], F[2]};
        if (t > 1.4 && t < 4.6 && rng.isChance(7 * dt))
            emit({ParticleKind::Flake, S.x + rng.range(-1, 1) * (S.ring + S.tube), S.y + S.tube * .6, rng.range(-6, 6),
                  rng.range(0, 10), rng.range(2, 3), rng.pick<uint32_t>(ash), rng.range(.6, 1.2)});
        column.update(dt, top(), t > .22 && t < 2.5 ? 1 : 0, rng);
        draft(ps, dt, {0, S.y + S.tube * .4, 9 + 7 * std::min(1., t), 170, 4, 130, 12, 130, 1 - smooth(2.5, 3.8, t)});
        for (auto& f : flashes) f.life -= dt;
        std::erase_if(flashes, [](const auto& f) { return f.life <= 0; });
        for (auto& r : rings) r.life -= dt;
        std::erase_if(rings, [](const auto& r) { return r.life <= 0; });
        for (auto& b : rocks.list) b.pvy = b.vy;
        rocks.update(dt, [](double) { return 0.; });
        for (size_t i = rocks.list.size(); i-- > 0;) {
            auto& b = rocks.list[i];
            if (b.x < -173 || b.x > 173) {
                rocks.list.erase(rocks.list.begin() + i);
                continue;
            }
            if (b.heat > 0) {
                b.heat = std::max(0., b.heat - dt * .3);
                if (!b.rest && b.heat > .25) {
                    b.points.push_back({b.x, b.y, t});
                    if (rng.isChance(14 * dt))
                        emit({ParticleKind::Fire, b.x, b.y, b.vx * .1, b.vy * .1 - 8, rng.range(.16, .3), 0,
                              rng.range(1.2, 2.2)});
                } else if (b.rest && rng.isChance(3 * b.heat * dt))
                    emit({ParticleKind::Smoke, b.x, b.y - 1, rng.range(-3, 3), -rng.range(5, 12), rng.range(.8, 1.4), 0,
                          rng.range(1.6, 2.6)});
            }
            const bool landed = (b.pvy > 0 && b.vy <= 0) || (b.rest && !b.dusted);
            if (landed && b.pvy > 60) {
                for (int j = 0; j < 2 + int(jsRound(b.size)); ++j)
                    emit({ParticleKind::Dust, b.x + rng.range(-1.5, 1.5), -.5, rng.signedRange(10, 50),
                          -rng.range(2, 20), rng.range(.3, .7), stone.dust, rng.range(1.3, 1 + b.size)});
                if (b.heat > .5 && !b.dusted) {
                    const double life = rng.range(1.2, 2.2);
                    blazes.add(rng, b.x, -.25, life, .45);
                }
            }
            if (b.rest) b.dusted = true;
            while (!b.points.empty() && t - b.points.front().t > .45) b.points.erase(b.points.begin());
        }
        blazes.update(dt, ps, rng);
        ps.update(
            dt, rng, [](double) { return 0.; },
            [&](double x, double y) {
                const double life = rng.range(3, 4.4), power = rng.range(.7, 1.25);
                blazes.add(rng, x, y - .25, life, power);
            });
        cull(ps, -177, -259, 177, 35);
        lights.update(dt);
    }
    void groundRing(Paint& p, bool back) const {
        const double k = t / .6;
        if (k >= 1) return;
        const std::array<uint32_t, 4> ramp{stone.body, stone.light, F[3], F[6]};
        band(p, 0, .5, 132 * ease::outCubic(k), .16, std::max(1., 6 * (1 - k)), 1.15 - k, ramp,
             [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; });
    }
    void drawBack(Paint& p) const override {
        scorch(p, HR, t, 52, 3.4, 1 - smooth(7.2 - 1.6, 7.2 - .3, t), 3);
        groundRing(p, true);
        drawPs(p, ps, Paint::Layer::Back, FR);
        column.draw(p, top(), FR, SM, 1 - t / 1.4, plume.hot());
        plume.draw(p, FR, SM);
        blazes.drawBack(p);
    }
    void draw(Paint& p) const override {
        groundRing(p, false);
        if (t < .04) {
            dome(p, F, 0, 44, 1);
            p.glow(0, -30, 68, WHITE, .6);
        } else if (t < .11)
            dome(p, F, 0, 38 - (t - .04) * 120, 1 - (t - .04) / .07);
        for (const auto& b : rocks.list) {
            if (!b.points.empty()) trail(p, b.points, t, FR, SM, .45, .6 + b.size * .3, b.heat);
            rock(p, b, stone, FR, b.heat);
        }
        drawPs(p, ps, Paint::Layer::Front, FR);
        if (t < .24) flare(p, 0, -14, 70 * std::pow(1 - t / .24, 1.4), 1 - t / .24, F);
        if (t < .5) {
            const double u = t / .5, r = 34 + 80 * ease::outCubic(u);
            p.ring(0, 0, r, std::max(1., 3 * (1 - u) + .5),
                   u < .3   ? F[6]
                   : u < .6 ? F[5]
                            : F[3],
                   [&](double, double a) { return std::sin(a) > 0 ? 0 : std::min(1., (1 - u) * 1.6); });
            glowDome(p, 0, 0, r, 6, F[4], .35 * (1 - u));
        }
        for (const auto& f : flashes) {
            const double k = f.life / f.max, r = f.r * (.8 + .2 * k);
            p.circle(f.x, f.y, r + 1.2, F[2]);
            p.circle(f.x, f.y, r, k > .45 ? F[6] : F[5]);
            p.circle(f.x, f.y, r * .6, k > .45 ? WHITE : F[6]);
        }
        for (const auto& r : rings) {
            const double k = r.life / r.max, e = 1 - std::pow(k, 3);
            p.ring(r.x, r.y, lerp(r.r0, r.r1, e), std::max(1., r.thickness * k + .4), r.color, std::min(1., k * 1.6));
        }
        const double flash = std::max(0., 1 - t / .9);
        pool(p, 0, 1, 150, 22, F[3], .24 * plume.hot() + .5 * flash * flash);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& bigExplosionDescriptor() {
    static const std::array<Option, 1> options{
        {{"stone", 4278190080., 4294967295., false, 0, &PixelFxExplosionSettings::stone, true}}};
    static const Descriptor descriptor{
        {PixelFxKind::BigExplosion, "explosions/big-explosion", 360, 300, 180, 262, 7.2, false},
        8,
        [](PixelFxSettings& s) {
            s.seed            = 11;
            s.explosion.stone = hex(0x4a3f69);
            s.palette         = {hex(0x4a1712), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                 hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<BigExplosion>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
