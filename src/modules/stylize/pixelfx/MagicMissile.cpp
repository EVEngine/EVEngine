// eve-hot-path
// Adapted from pixelfx projectiles/magic-missile.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double CHARGE = .2, FLY = .62;
struct TrailPoint {
    double x, y, time;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
struct MagicMissile final : Kernel {
    std::vector<uint32_t>   C;
    double                  height, reach, t = 0;
    bool                    burst = false;
    Rng                     rng;
    Particles               ps;
    Lights                  lights;
    std::vector<TrailPoint> trail;
    explicit MagicMissile(const PixelFxSettings& o)
        : C(o.palette), height(o.projectile.height), reach(o.projectile.reach), rng(o.seed) {
        ps.sparks = {WHITE, C[6], C[5], C[4]};
        trail.reserve(80);
    }
    Point position(double u) const {
        return {reach * ease::inOutSine(u) + std::sin(u * PI) * 6,
                -height + std::sin(u * TAU * 1.5) * 9 * std::sin(u * PI)};
    }
    void emit(Emission e) { ps.spawn(rng, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    void update(double dt) override {
        t += dt;
        const std::array<uint32_t, 3> bright{{C[5], C[6], WHITE}}, trailColors{{C[4], C[5], C[6]}},
            late{{C[6], WHITE, C[5]}};
        if (t < CHARGE) {
            if (rng.isChance(.8)) {
                const double a = rng.range(0, TAU), d = rng.range(12, 26), sp = d / .14;
                emit({ParticleKind::Mote, std::cos(a) * d, -height + std::sin(a) * d,
                      -std::cos(a) * sp - std::sin(a) * sp * .5, -std::sin(a) * sp + std::cos(a) * sp * .5, .14,
                      rng.pick<uint32_t>(bright), .8});
            }
        } else if (t < CHARGE + FLY) {
            const auto p = position((t - CHARGE) / FLY);
            trail.push_back({p.x, p.y, t});
            if (rng.isChance(.6))
                emit({ParticleKind::Mote, p.x, p.y, rng.range(-50, 50), rng.range(-50, 50), rng.range(.2, .4),
                      rng.pick<uint32_t>(trailColors), .7});
            if (rng.isChance(.25))
                emit({ParticleKind::Star, p.x + rng.range(-3, 3), p.y + rng.range(-3, 3), rng.range(-10, 10),
                      rng.range(-10, 10), rng.range(.25, .5), C[6], rng.range(.6, 1.6)});
        } else if (!burst) {
            burst          = true;
            const double y = -height;
            lights.add(reach, y, 52, C[4], .95, .55);
            lights.add(reach, y, 22, WHITE, .8, .12);
            for (int i = 0; i < 26; ++i) {
                const double a = rng.range(0, TAU), sp = rng.range(60, 150) * (1 - .65 * std::max(0.0, std::sin(a)));
                emit({i % 3 ? ParticleKind::Mote : ParticleKind::Star, reach, y, std::cos(a) * sp, std::sin(a) * sp,
                      rng.range(.25, .45), rng.pick<uint32_t>(bright), rng.range(.8, 1.8)});
            }
            for (int i = 0; i < 18; ++i) {
                const double a = rng.range(0, TAU), sp = rng.range(110, 240) * (1 - .5 * std::max(0.0, std::sin(a)));
                emit({ParticleKind::Spark, reach, y, std::cos(a) * sp, std::sin(a) * sp - 30, rng.range(.12, .3)});
            }
            for (int i = 0; i < 14; ++i) {
                const double a = rng.range(0, TAU), d = rng.range(4, 22);
                emit({ParticleKind::Star, reach + std::cos(a) * d, y + std::sin(a) * d * .8, 0, -rng.range(2, 8),
                      rng.range(.5, 1.1), rng.pick<uint32_t>(late), rng.range(.8, 2.6), rng.range(.05, .45)});
            }
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const auto first =
            std::find_if(trail.begin(), trail.end(), [&](const TrailPoint& q) { return t - q.time < .32; });
        const std::span<const TrailPoint> keep(first, trail.end());
        for (size_t i = 1; i < keep.size(); ++i) {
            const auto   a = keep[i - 1], b = keep[i];
            const double dx = b.x - a.x, dy = b.y - a.y, raw = jsHypot2(dx, dy), l = raw == 0 ? 1 : raw, nx = -dy / l,
                         ny = dx / l;
            for (double f = 0; f < 1; f += .5 / l) {
                const double tt = a.time + (b.time - a.time) * f, k = 1 - (t - tt) / .32, tw = std::sin(tt * 26),
                             w = 3.2 * k * std::abs(tw) + .4, x = a.x + dx * f, y = a.y + dy * f;
                const bool   face = tw > 0;
                for (double s = -w; s <= w; s += .5) {
                    const bool     edge = std::abs(s) > w - .8;
                    const uint32_t c    = face ? (edge ? C[6] : k > .6 ? C[5] : C[4]) : edge ? C[4] : C[2];
                    p.dot(x + nx * s, y + ny * s, c, k > .35 ? 1 : k / .35);
                }
            }
            if (i % 3 == 0) p.glow(b.x, b.y, 6, C[4], .25 * (1 - (t - b.time) / .32));
        }
        ps.draw(p);
        if (t < CHARGE + FLY) {
            const double k   = t < CHARGE ? t / CHARGE : 1;
            const auto   pos = t < CHARGE ? Point{0, -height} : position((t - CHARGE) / FLY);
            const double r   = 1 + 3 * ease::outBack(k);
            p.circle(pos.x, pos.y, r + 1.4, C[3]);
            p.circle(pos.x, pos.y, r + .4, C[5]);
            p.circle(pos.x - .4, pos.y - .4, r * .55, WHITE);
            for (int i = 0; i < 3; ++i) {
                const double a = t * 14 + (i * TAU) / 3;
                p.px(pos.x + std::cos(a) * (r + 3), pos.y + std::sin(a) * (r + 3) * .6, i ? C[6] : WHITE);
            }
            p.glow(pos.x, pos.y, 20, C[4], .65 * k);
            p.glow(pos.x, pos.y, 7, C[6], .5 * k);
        }
        const double q = t - CHARGE - FLY;
        if (q >= 0 && q < .5) {
            const double e = q / .5;
            if (e < .5) flare(p, reach, -height, 26 * std::pow(1 - e * 2, 1.2) + 2, 1 - e * 2, C);
            if (e < .12) p.circle(reach, -height, 6 * (1 - e / .12) + 1, WHITE);
            p.ring(reach, -height, 4 + 20 * ease::outCubic(e), std::max(1.0, 2.5 * (1 - e)), e < .3 ? C[6] : C[5],
                   std::min(1.0, (1 - e) * 1.5));
            if (e < .4) p.sparkle(reach, -height, 7 * (1 - e / .4), C[6], C[4], .6 * (1 - e / .4));
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& magicMissileDescriptor() {
    static const std::array<Option, 2> options{{{"height", 1, 95, false, 0, &PixelFxProjectileSettings::height},
                                                {"reach", 1, 170, false, 0, &PixelFxProjectileSettings::reach}}};
    static const Descriptor            descriptor{
        {PixelFxKind::MagicMissile, "projectiles/magic-missile", 208, 112, 24, 104, 1.9, false},
        8,
        [](PixelFxSettings& s) {
            s.seed              = 6;
            s.projectile.height = 26;
            s.projectile.reach  = 112;
            s.palette           = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                                   hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<MagicMissile>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
