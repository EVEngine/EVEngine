// eve-hot-path
// Adapted from pixelfx explosions/firework-burst.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
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
struct Star {
    double              x, y, vx, vy, life, age;
    bool                core, big;
    std::vector<Sample> pts;
    double              acc, seed;
};
struct Pop {
    double   x, y, t0, life;
    uint32_t color;
};
struct FireworkBurst final : Kernel {
    std::vector<uint32_t> C, K;
    Rng                   rng;
    Particles             ps;
    Lights                lights;
    std::vector<Star>     stars;
    std::vector<Pop>      pops;
    double                t     = 0;
    bool                  burst = false;
    explicit FireworkBurst(const PixelFxSettings& o) : C(o.palette), K(o.explosion.core), rng(o.seed) {
        ps.sparks = {K[6], K[5], K[4], K[3]};
    }
    void emit(Emission q) { ps.spawn(rng, q.kind, q.x, q.y, q.vx, q.vy, q.life, q.color, q.size); }
    void shell(int n, double speed, bool core, double minLife, double maxLife) {
        for (int i = 0; i < n; ++i) {
            const double z = rng.range(-1, 1), a = rng.range(0, TAU), s = std::sqrt(1 - z * z),
                         v = speed * rng.range(.93, 1.05);
            stars.push_back({0,
                             0,
                             std::cos(a) * s * v,
                             std::sin(a) * s * v,
                             rng.range(minLife, maxLife),
                             0,
                             core,
                             rng.isChance(.2),
                             {},
                             rng.next(),
                             rng.range(0, 100)});
        }
    }
    void update(double dt) override {
        t += dt;
        if (t < .42) {
            const double y = 72 * (1 - ease::outCubic(t / .42));
            if (rng.isChance(50 * dt))
                emit({ParticleKind::Spark, rng.range(-.5, .5), y + 2, rng.range(-25, 25), rng.range(20, 70),
                      rng.range(.15, .35)});
            if (rng.isChance(14 * dt))
                emit({ParticleKind::Vapour, rng.range(-1, 1), y + 3, rng.range(-3, 3), rng.range(5, 15),
                      rng.range(.5, .8), hex(0x4a4458), rng.range(1.5, 2.5)});
        }
        if (!burst && t >= .42) {
            burst = true;
            lights.add(0, 0, 110, C[4], .6, 1.5);
            lights.add(0, 0, 30, WHITE, .5, .07);
            shell(66, 236, false, 1.35, 1.9);
            shell(26, 120, true, 1, 1.4);
            for (int i = 0; i < 7; ++i) {
                const double a = rng.range(0, TAU), d = rng.range(0, 8);
                emit({ParticleKind::Vapour, std::cos(a) * d, std::sin(a) * d, std::cos(a) * rng.range(10, 30),
                      std::sin(a) * rng.range(10, 30), rng.range(2.2, 3), hex(0x4a4458), rng.range(6, 11)});
            }
            for (int i = 0; i < 24; ++i) {
                const double a = rng.range(0, TAU), sp = rng.range(40, 140);
                emit({ParticleKind::Spark, 0, 0, std::cos(a) * sp, std::sin(a) * sp, rng.range(.15, .35)});
            }
        }
        for (size_t i = stars.size(); i-- > 0;) {
            auto& s = stars[i];
            s.age += dt;
            const double k = s.age / s.life;
            if (k >= 1) {
                const auto& ramp = s.core ? K : C;
                for (int j = 0; j < 2 + (s.big ? 1 : 0); ++j)
                    pops.push_back({s.x + rng.range(-3, 3), s.y + rng.range(-3, 3), t + rng.range(0, .18),
                                    rng.range(.06, .12), rng.isChance(.6) ? WHITE : ramp[5]});
                stars.erase(stars.begin() + std::ptrdiff_t(i));
                continue;
            }
            s.vx -= s.vx * 2.6 * dt;
            s.vy -= s.vy * 2.6 * dt - 88 * dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            s.pts.push_back({s.x, s.y, t});
            while (s.pts.size() > 1 && t - s.pts.front().t > .34) s.pts.erase(s.pts.begin());
            for (s.acc += dt * (k < .25 ? 4 : 11); s.acc > 1; --s.acc)
                emit({ParticleKind::Star, s.x, s.y, s.vx * .15 + rng.range(-6, 6), s.vy * .15 + rng.range(0, 10),
                      rng.range(.35, .8), rng.isChance(.5) ? K[5] : WHITE, rng.isChance(.15) ? rng.range(1, 1.6) : .5});
        }
        std::erase_if(pops, [&](const auto& q) { return t > q.t0 + q.life; });
        ps.update(dt, rng);
        lights.update(dt);
    }
    void drawBack(Paint& p) const override { ps.draw(p, Paint::Layer::Back); }
    void rays(Paint& p, double L, double ang) const {
        if (L < 1) return;
        for (int i = 0; i < 8; ++i) {
            const bool   diag = i % 2 == 1;
            const double an = ang + i * TAU / 8, len = diag ? L * .42 : L, ca = std::cos(an), sa = std::sin(an);
            for (double d = 0; d <= len; d += .5) {
                const double u = d / len;
                p.dot(ca * d, sa * d,
                      diag      ? (u < .5 ? K[5] : K[4])
                      : u < .3  ? WHITE
                      : u < .65 ? C[5]
                                : C[4],
                      u < .7 ? 1 : 1 - (u - .7) / .35);
            }
        }
        p.glow(0, 0, 4 + L * 1.2, C[4], .3);
        p.glow(0, 0, 2 + L * .3, WHITE, .25);
    }
    void draw(Paint& p) const override {
        if (t < .42) {
            const double u = t / .42, y = 72 * (1 - ease::outCubic(u));
            p.lineFn(0, y, 0, y + 3 + 6 * (1 - u), [&](double q) { return q < .3 ? K[5] : K[3]; }, 1.);
            p.px(0, y, WHITE);
            p.glow(0, y, 7, K[4], .5);
        }
        for (const auto& s : stars) {
            const double k = s.age / s.life;
            for (size_t j = s.pts.size(); j-- > 1;) {
                const auto & q = s.pts[j], &prev = s.pts[j - 1];
                const double u = (t - q.t) / .34;
                if (u >= 1) break;
                const auto& ramp = s.age - (t - q.t) < .22 || s.core ? K : C;
                const auto  c    = u < .3 ? ramp[5] : u < .6 ? ramp[4] : ramp[3];
                p.line(q.x, q.y, prev.x, prev.y, c, std::pow(1 - u, .8) * (1.3 - k * .6));
            }
            if (k > .72 && std::sin(t * 60 + s.seed) < -.2) continue;
            const auto& ramp = s.age < .22 || s.core ? K : C;
            const auto  head = s.age < .04 ? WHITE : k < .55 ? ramp[s.age < .22 ? 5 : 6] : ramp[5];
            p.px(s.x, s.y, head);
            if (s.big && k < .6 && s.age > .06)
                for (const auto& d : CROSS) p.px(s.x + d.x, s.y + d.y, ramp[5]);
            p.glow(s.x, s.y, s.big ? 7 : 5, ramp[4], (s.big ? .7 : .5) * (1 - k * .6) * std::min(1., s.age / .2));
        }
        for (const auto& q : pops) {
            const double u = (t - q.t0) / q.life;
            if (u < 0 || u > 1) continue;
            p.sparkle(q.x, q.y, 2.6 * std::sin(PI * u), q.color, mix(q.color, C[3], .5), .5);
        }
        const double b = t - .42;
        if (b >= 0 && b < .28) {
            const double u = b / .28, L = 32 * std::pow(1 - u, 1.3);
            rays(p, L, u * .6);
            if (b < .07) p.circle(0, 0, 2.6 - b * 20, WHITE);
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& fireworkBurstDescriptor() {
    static const std::array<Option, 1> options{{{"core", 1, 64, false, 7, &PixelFxExplosionSettings::core}}};
    static const Descriptor            descriptor{
        {PixelFxKind::FireworkBurst, "explosions/firework-burst", 240, 248, 120, 100, 3.6, false},
        7,
        [](PixelFxSettings& s) {
            s.seed           = 9;
            s.palette        = {hex(0x3d0a33), hex(0x701560), hex(0xb0228f), hex(0xf53cd1),
                                hex(0xff86e6), hex(0xffd2f6), WHITE};
            s.explosion.core = {hex(0x4a2c08), hex(0x86520c), hex(0xc48412), hex(0xf0b424),
                                hex(0xffe066), hex(0xfff6c4), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<FireworkBurst>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
