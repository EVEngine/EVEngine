// eve-hot-path
// Native adaptation of pixelfx pickups/collect-burst.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Pickup.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double CY = -30;
struct Star {
    double             x, y, angle, speed, turn, life, age, size;
    std::vector<Point> trail;
    uint32_t           color;
    double             phase;
};
struct Mote {
    double x, y, vy, life, delay, phase;
};
struct CollectBurst final : Kernel {
    std::vector<uint32_t> C;
    std::vector<Star>     stars;
    std::vector<Mote>     motes;
    Lights                lights;
    double                t = 0;
    explicit CollectBurst(const PixelFxSettings& o) : C(o.palette) {
        Rng r(o.seed);
        lights.add(0, CY, 36, C[5], .85, .32);
        lights.add(0, CY, 14, WHITE, .75, .08);
        stars.reserve(14);
        for (int i = 0; i < 14; ++i) {
            const double angle = (double(i) / 14) * TAU + r.range(-.2, .2),
                         speed = r.range(145, 225) * (i % 3 == 0 ? 1.12 : .85),
                         turn = (i % 2 ? 1 : -1) * r.range(2.5, 5), life = r.range(.75, 1.15),
                         size = i % 3 == 0 ? 2.6 : r.range(1.2, 2), phase = r.range(0, TAU);
            stars.push_back({0,
                             CY,
                             angle,
                             speed,
                             turn,
                             life,
                             0,
                             size,
                             {},
                             i % 4 == 0 ? WHITE
                             : i % 2    ? C[6]
                                        : C[5],
                             phase});
            stars.back().trail.reserve(5);
        }
        motes.reserve(7);
        for (int i = 0; i < 7; ++i) {
            const double x = r.range(-8, 8), y = CY + r.range(-4, 6), vy = -r.range(12, 30), life = r.range(.5, .9),
                         delay = r.range(.05, .25), phase = r.range(0, TAU);
            motes.push_back({x, y, vy, life, delay, phase});
        }
    }
    void update(double dt) override {
        t += dt;
        for (auto& s : stars) {
            if (s.age > s.life) continue;
            s.age += dt;
            s.trail.insert(s.trail.begin(), {s.x, s.y});
            if (s.trail.size() > 4) s.trail.pop_back();
            s.angle += s.turn * dt * std::max(0., 1 - s.age * 1.5);
            s.speed -= s.speed * 4.3 * dt;
            s.x += std::cos(s.angle) * s.speed * dt;
            s.y += std::sin(s.angle) * s.speed * dt + 6 * s.age * dt;
            if (s.y > -2) {
                s.y     = -2;
                s.angle = -std::abs(s.angle);
                s.speed *= .5;
            }
        }
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        if (t < .07) p.circle(0, CY, 5.5 * (1 - t / .07) + 1, WHITE);
        if (t < .16) {
            const double k = 1 - t / .16, length = 26 * ease::outCubic(std::min(1., t / .03 + .4)) * k;
            constexpr std::array<std::array<double, 3>, 4> arms{
                {{{1, 0, 1}}, {{-1, 0, 1}}, {{0, 1, .6}}, {{0, -1, .6}}}};
            for (auto arm : arms)
                p.lineFn(
                    0, CY, arm[0] * length * arm[2], CY + arm[1] * length * arm[2],
                    [&](double u) { return u < .3    ? WHITE
                                           : u < .65 ? C[6]
                                                     : C[5]; },
                    [&](double u) { return (u < .5 ? 1 : (1 - u) * 2) * (.5 + k); });
            p.glow(0, CY, 20 * k + 4, C[5], .6 * k);
        }
        const double u1 = t / .32;
        if (u1 < 1) {
            const double radius = 3 + 23 * ease::outExpo(u1);
            p.ring(0, CY, radius, 1, u1 < .3 ? WHITE : C[6], 1.3 - u1 * 1.2);
            p.glowRing(0, CY, radius, 4, C[5], .4 * (1 - u1));
        }
        const double u2 = (t - .04) / .5;
        if (u2 > 0 && u2 < 1) p.ring(0, CY, 2 + 16 * ease::outCubic(u2), std::max(1., 2.6 * (1 - u2)), C[5], 1 - u2);
        for (const auto& s : stars) {
            if (s.age > s.life) continue;
            const double k = 1 - s.age / s.life, tw = .65 + .35 * std::sin(s.age * 26 + s.phase);
            for (size_t i = 1; i < s.trail.size(); ++i)
                p.dot(s.trail[i].x, s.trail[i].y, i < 2 ? C[5] : C[4], (1 - double(i) / 4) * k * 1.4);
            if (k < .3 && std::sin(s.age * 40 + s.phase) < 0) continue;
            pickup::glint(p, s.x, s.y, jsRound(s.size * tw * std::min(1., k * 2.5)), s.color, .45 * k);
        }
        for (const auto& m : motes) {
            const double age = t - m.delay;
            if (age < 0 || age > m.life) continue;
            const double k = 1 - age / m.life, x = m.x + std::sin(age * 6 + m.phase) * 1.5, y = m.y + m.vy * age;
            p.dot(x, y, k > .5 ? C[6] : C[5], k * 1.5);
            p.glow(x + .5, y + .5, 3, C[5], .3 * k);
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& collectBurstDescriptor() {
    static const Descriptor d{
        {PixelFxKind::CollectBurst, "pickups/collect-burst", 112, 108, 56, 94, 1.35, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 3;
            s.palette = {hex(0x1a0516), hex(0x3d0a33), hex(0x701560), hex(0xb0228f),
                         hex(0xf53cd1), hex(0xff86e6), hex(0xffd2f6), WHITE};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<CollectBurst>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
