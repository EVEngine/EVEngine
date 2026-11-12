// eve-hot-path
// Adapted from pixelfx explosions/firework-rocket.js (MIT).
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
struct Sample {
    double x, y, t;
};
struct Frond {
    double              x, y, vx, vy, age, life;
    std::vector<Sample> pts;
    double              acc, seed;
};
struct Pop {
    double   x, y, t0, life;
    uint32_t color;
};
struct FireworkRocket final : Kernel {
    std::vector<uint32_t> C, T;
    Rng                   rng;
    Particles             ps;
    Lights                lights;
    std::vector<Frond>    fronds;
    std::vector<Pop>      pops;
    double                t = 0, bx = 0, by = 0, acc = 0;
    bool                  burst = false;
    explicit FireworkRocket(const PixelFxSettings& o) : C(o.palette), T(o.explosion.tips), rng(o.seed) {
        ps.sparks = {C[6], C[5], C[4], C[3]};
        lights.add(0, -14, 24, C[4], .7, .35);
        for (int i = 0; i < 18; ++i) {
            const double a = -PI / 2 + rng.range(-1.2, 1.2), sp = rng.range(60, 180);
            emit({ParticleKind::Spark, 0, -2, std::cos(a) * sp, std::sin(a) * sp * .6, rng.range(.15, .4)});
        }
        for (int i = 0; i < 5; ++i)
            emit({ParticleKind::Vapour, rng.range(-3, 3), -2, rng.range(-20, 20), -rng.range(2, 10), rng.range(.6, 1),
                  hex(0x5a5468), rng.range(3, 5)});
    }
    void  emit(Emission q) { ps.spawn(rng, q.kind, q.x, q.y, q.vx, q.vy, q.life, q.color, q.size); }
    Point at(double time) const {
        return {std::sin(time * 8) * 1.6 * (time / 1.05), -8 - 192 * ease::outQuad(clamp(time / 1.05))};
    }
    void update(double dt) override {
        t += dt;
        if (t < 1.05) {
            const auto   pos = at(t), prev = at(std::max(0., t - .02));
            const double vx = (pos.x - prev.x) / .02, vy = (pos.y - prev.y) / .02;
            for (acc += dt * 90; acc > 1; --acc) {
                const double a = PI / 2 + rng.range(-.45, .45), sp = rng.range(30, 110);
                emit({ParticleKind::Spark, pos.x, pos.y + 4, std::cos(a) * sp + vx * .2, std::sin(a) * sp + vy * .15,
                      rng.range(.15, .4)});
                if (rng.isChance(.25))
                    emit({ParticleKind::Star, pos.x + rng.range(-1.5, 1.5), pos.y + rng.range(2, 6), rng.range(-6, 6),
                          rng.range(5, 20), rng.range(.4, .9), rng.isChance(.5) ? C[5] : WHITE,
                          rng.isChance(.15) ? 1.2 : .5});
                if (rng.isChance(.18))
                    emit({ParticleKind::Vapour, pos.x, pos.y + 5, rng.range(-4, 4), rng.range(4, 12),
                          rng.range(.6, 1.1), hex(0x4a4458), rng.range(1.2, 2.2)});
            }
        } else if (!burst) {
            burst          = true;
            const auto pos = at(1.05);
            bx             = pos.x;
            by             = pos.y;
            lights.add(bx, by, 82, C[4], .8, 1.6);
            lights.add(bx, by, 40, WHITE, .6, .08);
            for (int i = 0; i < 9; ++i) {
                const double a  = -PI / 2 + ((i - 4) / 4.) * 1.4 + rng.range(-.1, .1),
                             sp = rng.range(180, 210) * (1 - .12 * std::abs(i - 4) / 4);
                fronds.push_back({bx,
                                  by,
                                  std::cos(a) * sp,
                                  std::sin(a) * sp,
                                  0,
                                  rng.range(1.8, 2.15),
                                  {},
                                  rng.next(),
                                  rng.range(0, 100)});
            }
            for (int i = 0; i < 6; ++i)
                emit({ParticleKind::Vapour, bx + rng.range(-4, 4), by + rng.range(-4, 4), rng.range(-20, 20),
                      rng.range(-15, 15), rng.range(1.8, 2.6), hex(0x4a4458), rng.range(6, 10)});
            for (int i = 0; i < 30; ++i) {
                const double a = rng.range(0, TAU), sp = rng.range(40, 160);
                emit({ParticleKind::Spark, bx, by, std::cos(a) * sp, std::sin(a) * sp, rng.range(.15, .35)});
            }
        }
        for (size_t i = fronds.size(); i-- > 0;) {
            auto& f = fronds[i];
            f.age += dt;
            const double k = f.age / f.life;
            if (k >= 1) {
                for (int j = 0; j < 4; ++j)
                    pops.push_back({f.x + rng.range(-5, 5), f.y + rng.range(-5, 5), t + rng.range(0, .2),
                                    rng.range(.06, .12), rng.isChance(.5) ? WHITE : T[3]});
                fronds.erase(fronds.begin() + std::ptrdiff_t(i));
                continue;
            }
            f.vx -= f.vx * 1.35 * dt;
            f.vy -= f.vy * 1.35 * dt - 105 * dt;
            f.x += f.vx * dt;
            f.y += f.vy * dt;
            f.pts.push_back({f.x, f.y, t});
            while (f.pts.size() > 1 && t - f.pts.front().t > 1.15) f.pts.erase(f.pts.begin());
            for (f.acc += dt * 34; f.acc > 1; --f.acc)
                emit({ParticleKind::Star, f.x + rng.range(-1, 1), f.y + rng.range(-1, 1), f.vx * .1 + rng.range(-5, 5),
                      rng.range(0, 14), rng.range(.5, 1.1), rng.isChance(.6) ? C[5] : WHITE,
                      rng.isChance(.12) ? rng.range(1, 1.5) : .5});
        }
        std::erase_if(pops, [&](const auto& q) { return t > q.t0 + q.life; });
        ps.update(dt, rng);
        explosion::cull(ps, -148, -286, 148, 10);
        lights.update(dt);
    }
    void drawBack(Paint& p) const override { ps.draw(p, Paint::Layer::Back); }
    void draw(Paint& p) const override {
        if (t < 1.05) {
            const auto pos = at(t);
            p.line(pos.x, pos.y + 4, pos.x, pos.y + 12, hex(0x5a4030));
            p.rect(pos.x - 1, pos.y - 3, 3, 7, hex(0xb8303a));
            p.px(pos.x - 1, pos.y - 3, hex(0xe86070));
            p.px(pos.x, pos.y - 4, hex(0xd8d0c0));
            const double fl = 2 + (std::sin(t * 70) > 0 ? 1 : 0);
            p.lineFn(pos.x, pos.y + 4, pos.x, pos.y + 4 + fl, [&](double u) { return u < .4 ? WHITE : C[5]; }, 1.);
            p.glow(pos.x, pos.y + 5, 9, C[4], .6);
        }
        for (const auto& f : fronds) {
            const double k = f.age / f.life;
            for (size_t j = f.pts.size(); j-- > 1;) {
                const auto & q = f.pts[j], &prev = f.pts[j - 1];
                const double u = (t - q.t) / 1.15;
                if (u >= 1) break;
                const auto   c = u < .08 ? C[6] : u < .25 ? C[5] : u < .5 ? C[4] : u < .75 ? C[3] : C[2];
                const double a = std::pow(1 - u, .6) * (1.25 - k * .45);
                p.line(q.x, q.y, prev.x, prev.y, c, a);
                if (u < .7) p.line(q.x + 1, q.y, prev.x + 1, prev.y, u < .25 ? C[5] : u < .5 ? C[3] : C[2], a * .85);
                if (u < .35) p.line(q.x, q.y + 1, prev.x, prev.y + 1, C[3], a * .6);
            }
            if (k > .8 && std::sin(t * 55 + f.seed) < 0) continue;
            p.rect(f.x - .5, f.y - .5, 2, 2, k < .5 ? WHITE : C[6]);
            if (k < .6)
                for (const auto& d : std::array<Point, 4>{{{-1, 0}, {2, 0}, {0, -1}, {0, 2}}})
                    p.px(f.x + d.x, f.y + d.y, C[5]);
            p.glow(f.x, f.y, 10, C[4], .6 * (1 - k * .5));
        }
        for (const auto& q : pops) {
            const double u = (t - q.t0) / q.life;
            if (u < 0 || u > 1) continue;
            p.sparkle(q.x, q.y, 2.8 * std::sin(PI * u), q.color, T[2], .5);
        }
        const double b = t - 1.05;
        if (b >= 0 && b < .26) {
            const double u = b / .26, L = 30 * std::pow(1 - u, 1.3);
            for (int i = 0; i < 8; ++i) {
                const bool   diag = i % 2 == 1;
                const double an = u * .5 + i * TAU / 8, len = diag ? L * .45 : L, ca = std::cos(an), sa = std::sin(an);
                for (double d = 0; d <= len; d += .5) {
                    const double v = d / len;
                    p.dot(bx + ca * d, by + sa * d,
                          v < .3    ? WHITE
                          : v < .65 ? C[5]
                                    : C[4],
                          v < .7 ? 1 : 1 - (v - .7) / .35);
                }
            }
            if (b < .07) p.circle(bx, by, 2.5, WHITE);
            p.glow(bx, by, 4 + L * 1.2, C[4], .35);
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& fireworkRocketDescriptor() {
    static const std::array<Option, 1> options{{{"tips", 1, 64, false, 6, &PixelFxExplosionSettings::tips}}};
    static const Descriptor            descriptor{
        {PixelFxKind::FireworkRocket, "explosions/firework-rocket", 300, 300, 150, 288, 3.8, false},
        7,
        [](PixelFxSettings& s) {
            s.seed           = 12;
            s.palette        = {hex(0x2a1406), hex(0x6a3a0c), hex(0xb8701a), hex(0xf0a830),
                                hex(0xffd860), hex(0xfff2b0), WHITE};
            s.explosion.tips = {hex(0x0c2a3a), hex(0x1a5a78), hex(0x36a2c8), hex(0x7cd8f0), hex(0xc8f4ff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<FireworkRocket>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
