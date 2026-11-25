// eve-hot-path
// Native adaptation of pixelfx fire/meteor.js (MIT), including Sprite Fusion explosion.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Explosion.h"
#include "stylize/pixelfx/Fire.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double   HIT     = .62;
constexpr uint32_t OUTLINE = 0xff22141au, SOOT = 0xff120a0du, SOOT2 = 0xff1d1217u;
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
    ParticleFlag flags = ParticleFlag::None;
};
struct Meteor final : Kernel {
    std::vector<uint32_t> H, RK;
    explosion::Stone      st;
    Rng                   rng;
    uint32_t              S, GLOW;
    Particles             ps;
    Blazes                blazes;
    BasicBits<>           bits;
    Lights                lights;
    double                t   = 0;
    bool                  hit = false;
    Point                 last{-112, -142};
    const double          len = jsHypot2(112., 142.), ux = 112 / len, uy = 142 / len;
    explicit Meteor(const PixelFxSettings& o)
        : H(o.palette.rbegin(), o.palette.rend()),
          RK(o.fire.rock),
          st(o.fire.stone),
          rng(o.seed),
          S(o.seed * 11),
          GLOW(mix(mix(H[3], H[4], .5), WHITE, .25)) {
        ps.sparks = {H[1], H[2], H[4], H[5]};
    }
    void  emit(Emission q) { ps.spawn(rng, q.kind, q.x, q.y, q.vx, q.vy, q.life, q.color, q.size, q.delay, q.flags); }
    Point pos(double tt) const {
        const double u = clamp(tt / HIT), k = ease::inQuad(u) * .35 + u * .65;
        return {-112 * (1 - k), -142 * (1 - k)};
    }
    void update(double dt) override {
        t += dt;
        if (t < HIT) {
            const auto   q = pos(t);
            const double x = q.x, y = q.y, vx = (x - last.x) / dt, vy = (y - last.y) / dt;
            last = q;
            for (int i = 0; i < 3; ++i) {
                if (!rng.isChance(.8 * dt * 60)) continue;
                const double behindX = rng.range(2, 8), offsetX = rng.range(-3, 3), behindY = rng.range(2, 8),
                             offsetY = rng.range(-3, 3);
                emit({ParticleKind::Fire, x - ux * behindX + offsetX, y - uy * behindY + offsetY,
                      vx * .15 + rng.range(-20, 20), vy * .15 + rng.range(-30, 10), rng.range(.3, .6), 0,
                      rng.range(3, 6), 0,
                      rng.isChance(.65) ? ParticleFlag::Smokes | ParticleFlag::Big : ParticleFlag::None});
            }
            if (rng.isChance(.6 * dt * 60))
                emit({ParticleKind::Spark, x, y, vx * .35 + rng.range(-80, 80), vy * .35 + rng.range(-80, 40),
                      rng.range(.2, .5)});
            if (rng.isChance(.3 * dt * 60))
                emit({ParticleKind::Ember, x, y, vx * .1 + rng.range(-30, 30), vy * .1 + rng.range(-40, 0),
                      rng.range(.8, 1.6), H[3], rng.isChance(.2) ? 2. : 1.});
        } else if (!hit) {
            hit = true;
            lights.add(0, -40, 100, rgb(255, 176, 96), .95, .9);
            lights.add(0, -20, 46, WHITE, .9, .12);
            for (int i = 0; i < 34; ++i) {
                const double a = -PI / 2 + rng.range(-1.3, 1.3) + .25, sp = rng.range(50, 210);
                emit({ParticleKind::Fire, rng.range(-6, 6), -rng.range(2, 10), std::cos(a) * sp,
                      std::sin(a) * sp * .75 - 30, rng.range(.5, 1.1), 0, rng.range(5, 9), 0,
                      rng.isChance(.5) ? ParticleFlag::Smokes | ParticleFlag::Big : ParticleFlag::None});
            }
            for (int i = 0; i < 9; ++i)
                emit({ParticleKind::Fire, rng.range(-6, 6), -6, rng.range(-14, 14), -rng.range(130, 220),
                      rng.range(.8, 1.2), 0, rng.range(6, 8.5), rng.range(0, .12),
                      ParticleFlag::Smokes | ParticleFlag::Big});
            for (int i = 0; i < 20; ++i) {
                const double a = rng.range(-PI, 0), d = rng.range(4, 18);
                emit({ParticleKind::Smoke, std::cos(a) * d, -rng.range(6, 24), std::cos(a) * rng.range(10, 40),
                      -rng.range(30, 110), rng.range(1.8, 2.8), 0, rng.range(8, 12), rng.range(.15, .5),
                      ParticleFlag::Big});
            }
            for (int i = 0; i < 16; ++i) {
                const double s = rng.sign();
                emit({ParticleKind::Smoke, s * rng.range(4, 18), -rng.range(1, 5), s * rng.range(60, 130) + 10,
                      -rng.range(2, 12), rng.range(1.2, 2), st.dust, rng.range(5, 8), rng.range(0, .06),
                      ParticleFlag::Big});
            }
            for (int i = 0; i < 90; ++i) {
                const double a = -PI / 2 + rng.range(-1.3, 1.3) + .15, sp = rng.range(100, 300);
                emit({ParticleKind::Spark, 0, -4, std::cos(a) * sp, std::sin(a) * sp, rng.range(.2, .7)});
            }
            for (int i = 0; i < 36; ++i) {
                const double a = rng.range(0, TAU), sp = rng.range(40, 150);
                emit({ParticleKind::Ember, std::cos(a) * 8, -8 + std::sin(a) * 6, std::cos(a) * sp,
                      std::sin(a) * sp - 70, rng.range(1.2, 2.6), H[3]});
            }
            for (int i = 0; i < 10; ++i) {
                const double a = -PI / 2 + rng.range(-1, 1.05), sp = rng.range(130, 240);
                emit({ParticleKind::Brand, 0, -6, std::cos(a) * sp, std::sin(a) * sp, rng.range(.7, 1.3)});
            }
            for (int i = 0; i < 28; ++i) {
                Debris b;
                b.kind            = DebrisKind::Chunk;
                b.x               = rng.range(-8, 8);
                b.y               = -3;
                b.vx              = rng.range(-80, 110);
                b.vy              = -rng.range(140, 340);
                b.gravity         = 1;
                b.drag            = .4;
                b.bounce          = .3;
                b.life            = rng.range(1.8, 2.8);
                b.angle           = rng.range(0, TAU);
                b.angularVelocity = rng.range(-16, 16);
                b.size            = rng.integer(1, 3);
                b.ramp            = {OUTLINE, st.shade, st.body, st.light, mix(st.light, WHITE, .3)};
                bits.add(std::move(b));
            }
            const std::array<uint32_t, 4> ramp{{st.light, st.body, st.body, st.shade}};
            for (int i = 0; i < 80; ++i) {
                const double u = rng.range(-1, 1), a = -PI / 2 + u * .7 + .15,
                             sp = rng.range(90, 270) * (1 - .4 * std::abs(u));
                emit({ParticleKind::Grit, u * 6, -1, std::cos(a) * sp, std::sin(a) * sp, rng.range(1.2, 2.2),
                      rng.pick<uint32_t>(ramp), rng.isChance(.45) ? 2. : 1.});
            }
        }
        const auto floor = [](double) { return 0.; };
        bits.update(dt, floor);
        for (auto& b : bits.list)
            if (b.rest && !b.dusted) {
                b.dusted = true;
                for (int i = 0; i < 3; ++i)
                    emit({ParticleKind::Dust, b.x + rng.range(-1.5, 1.5), -.5, rng.signedRange(8, 35),
                          -rng.range(2, 18), rng.range(.3, .6), st.dust, rng.range(1.3, 2.6)});
            }
        blazes.update(dt, ps, rng);
        ps.update(dt, rng, floor, [&](double x, double y) {
            const double life = rng.range(.9, 1.6), power = rng.range(.5, .85);
            blazes.add(rng, x, y - .25, life, power);
        });
        lights.update(dt);
    }
    void meteor(Paint& p, double x, double y, double R0, double cool, double alpha = 1) const {
        p.shader(x - R0 - 2, y - R0 - 2, x + R0 + 2, y + R0 + 2, [&](double px, double py, ShadeOutput& out) {
            out.a           = alpha;
            const double dx = px - x, dy = py - y, d = jsHypot2(dx, dy), a = std::atan2(dy, dx),
                         rr = R0 * (1 + .18 * noise(a * 2.2 + 3, S) + .1 * noise(a * 5, S + 1));
            if (d > rr + 1) return;
            if (d > rr) {
                out.c = OUTLINE;
                return;
            }
            const double face = (dx * ux + dy * uy) / std::max(1., d),
                         heat = (1 - cool) * (face * .6 + .5 - (d / rr) * .25);
            if (heat > .62) {
                out.c = heat > .85 ? H[0] : heat > .74 ? H[1] : H[2];
                return;
            }
            if (heat > .42) {
                out.c = H[4];
                return;
            }
            const double h = rand({jsUint(std::floor(px - x)), jsUint(std::floor(py - y)), S + 3});
            if (h < .22 * (1 - cool * .7)) {
                out.c = 1 - cool > .5 ? H[4] : H[6];
                return;
            }
            out.c = dx * -.6 + dy * -.8 > 0 ? RK[2] : d / rr > .6 ? RK[0] : RK[1];
        });
    }
    void crater(Paint& p) const {
        if (t < HIT) return;
        const double a = t - HIT, end = 1 - smooth(4, 4.85, t), rx = 36 * std::min(1., a / .08), ry = 6;
        for (int y = -1; y <= 3; ++y)
            for (int x = int(std::floor(-rx)); x <= std::ceil(rx); ++x) {
                const double dx = (x + .5) / rx, dy = (y + .5) / ry, d = std::sqrt(dx * dx + dy * dy);
                if (d > 1) continue;
                const double k = rand({jsUint(x * 31 + y), S + 5}),
                             T = std::pow(clamp(1 - a / 2.6), 1.3) * (1.25 - d) - k * .35;
                if (T > .06) {
                    const auto c = H[size_t(std::min(8., std::floor((1 - std::min(1., T)) * 8)))];
                    p.dot(x, y, c, end);
                    if ((x & 3) == 0) p.glow(x + .5, y, 7, c, .2 * T * end);
                } else
                    p.dot(x, y, d < .55 ? SOOT : SOOT2, (d < .95 - k * .3 ? 1 : 1 - d) * end);
            }
        for (int x = -40; x <= 40; ++x) {
            const double e = std::abs(x) / 40.;
            const int    hh =
                e < .7 ? 0 : int(jsRound(2.4 * std::sin(((e - .7) / .3) * PI) * (.6 + .4 * rand({jsUint(x), S + 7}))));
            for (int j = 0; j < hh; ++j)
                p.dot(x, -1 - j, j == hh - 1 ? mix(st.light, H[4], .5 * clamp(1 - a / 2)) : st.shade, end);
            if (hh) p.dot(x, 0, OUTLINE, end);
        }
    }
    void drawBack(Paint& p) const override {
        crater(p);
        fire::simDraw(p, ps, H, Paint::Layer::Back);
        blazes.drawBack(p);
        const double                  k = (t - HIT) / .5;
        const std::array<uint32_t, 4> ramp{{st.body, st.light, H[4], H[1]}};
        if (k > 0 && k < 1)
            band(p, 0, .5, 110 * ease::outCubic(k), .16, std::max(1., 5 * (1 - k)), 1.15 - k, ramp,
                 [](double g) { return std::sin(g) < 0; });
    }
    void draw(Paint& p) const override {
        const double                  k = (t - HIT) / .5;
        const std::array<uint32_t, 4> ramp{{st.body, st.light, H[4], H[1]}};
        if (k > 0 && k < 1)
            band(
                p, 0, .5, 110 * ease::outCubic(k), .16, std::max(1., 5 * (1 - k)), 1.15 - k, ramp,
                [](double g) { return std::sin(g) >= 0; }, k < .12);
        for (const auto& b : bits.list) explosion::rock(p, b, st, H);
        if (t < HIT) {
            const auto        q = pos(t);
            const fire::Noise NZ{0, t};
            fire::floorLight(p, 0, .5, 30 + 40 * (t / HIT), 7, GLOW, .5 * std::pow(t / HIT, 2));
            constexpr std::array<std::array<double, 3>, 4> tongues{
                {{{36, 7, 0}}, {{26, 7.5, -2.5}}, {{26, 7.5, 2.5}}, {{16, 8, 0}}}};
            for (uint32_t i = 0; i < tongues.size(); ++i) {
                const auto     a = tongues[i];
                const uint32_t n = (S + i) * 13;
                fire::flameBlade(p, q.x - uy * a[2], q.y + ux * a[2], -ux, -uy, a[0] * (.85 + .25 * NZ(5, n)), a[1],
                                 NZ(3, n + 3) * 3, NZ, n, H, fire::LAYERS, .5, 1.6);
            }
            meteor(p, q.x, q.y, 6.5, 0);
            p.glow(q.x, q.y, 46, GLOW, .55);
            p.glow(q.x, q.y, 12, H[1], .4);
        } else {
            const double a = t - HIT, cool = clamp(a / 2.8), end = 1 - smooth(3.9, 4.8, t);
            if (end > 0) {
                meteor(p, 0, 0, 5.5, cool, end);
                p.glow(0, -2, 22, GLOW, .4 * (1 - cool) * end);
            }
        }
        fire::simDraw(p, ps, H, Paint::Layer::Front);
        const double f = (t - HIT) / .08;
        if (f > 0 && f < 1) {
            p.circle(0, -6, 11 * (1 - f * .3) + 1.5, H[3]);
            p.circle(0, -6, 11 * (1 - f * .3), f < .5 ? H[1] : H[2]);
            p.circle(0, -6, 7 * (1 - f * .3), f < .5 ? WHITE : H[1]);
        }
        const double                  fr = (t - HIT) / .24;
        const std::array<uint32_t, 4> rays{{H[6], H[4], H[2], H[1]}};
        if (fr > 0 && fr < 1) flare(p, 0, -6, 44 * std::pow(1 - fr, 1.4), 1 - fr, rays);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& meteorDescriptor() {
    static const std::array<Option, 2> options{
        {{"stone", 4278190080., 4294967295., false, 0, &PixelFxFireSettings::stone, true},
         {"rock", 1, 64, false, 4, &PixelFxFireSettings::rock}}};
    static const Descriptor d{
        {PixelFxKind::Meteor, "fire/meteor", 272, 232, 150, 208, 5, false},
        9,
        [](PixelFxSettings& s) {
            s.seed       = 9;
            s.palette    = {0xff12174au, 0xff171f8au, 0xff1f28c9u, 0xff1d56f2u, 0xff2a8affu,
                            0xff3dc2ffu, 0xff6be2ffu, 0xffa6f3ffu, WHITE};
            s.fire.stone = 0xff693f4au;
            s.fire.rock  = {0xff160e12u, 0xff30222au, 0xff4a3a46u, 0xff665a6au};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Meteor>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
