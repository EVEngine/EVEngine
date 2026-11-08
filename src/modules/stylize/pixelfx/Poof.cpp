// eve-hot-path
// Native adaptation of pixelfx spawn/poof.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double CY = -17;
struct Ball {
    double x, y, vx, vy, r0, r1, life, delay, max;
};
struct SpeedLine {
    double angle, radius, delay;
};
struct PoofEmission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color;
    double       size, delay = 0;
};
struct Poof final : Kernel {
    const std::vector<uint32_t> C, S;
    Rng                         R;
    Particles                   ps;
    Lights                      lights;
    std::vector<Ball>           balls;
    std::array<SpeedLine, 12>   lines;
    double                      t = 0;
    explicit Poof(const PixelFxSettings& o) : C(o.palette), S(o.spawn.stars), R(o.seed) {
        ps.sparks = {WHITE, S[1], S[0], C[2]};
        for (int i = 0; i < 9; ++i) {
            const double a = (double(i) / 9) * TAU + R.range(-.2, .2), sp = R.range(95, 140), r = R.range(5, 7);
            balls.push_back({std::cos(a) * 3, CY + std::sin(a) * 3, std::cos(a) * sp, std::sin(a) * sp * .8 - 12,
                             r * .5, r, R.range(.7, 1.05), R.range(0, .03), 0});
        }
        for (int i = 0; i < 5; ++i) {
            const double a = R.range(0, TAU), sp = R.range(20, 60);
            balls.push_back({std::cos(a) * 2, CY + std::sin(a) * 2, std::cos(a) * sp, std::sin(a) * sp - 16, 4,
                             R.range(6.5, 8.5), R.range(.85, 1.2), R.range(.02, .06), 0});
        }
        for (int i = 0; i < 7; ++i) {
            const double a = R.range(0, TAU), sp = R.range(170, 240), r = R.range(2.2, 3.6);
            balls.push_back({std::cos(a) * 4, CY + std::sin(a) * 4, std::cos(a) * sp, std::sin(a) * sp * .75 - 10,
                             r * .6, r, R.range(.32, .5), R.range(.01, .04), 0});
        }
        for (auto& b : balls) b.max = b.life;
        for (size_t i = 0; i < lines.size(); ++i) {
            const double a = (double(i) / 12) * TAU + R.range(-.12, .12) + .13, sa = std::sin(a);
            lines[i] = {a, std::min(R.range(24, 34), sa > .1 ? (-2 - CY) / sa : 99), R.range(0, .04)};
        }
        for (int i = 0; i < 7; ++i) {
            const double a = R.range(-PI, .3), sp = R.range(70, 125);
            emit({ParticleKind::Star, std::cos(a) * 6, CY + std::sin(a) * 6, std::cos(a) * sp, std::sin(a) * sp - 30,
                  R.range(.55, .95), R.pick<uint32_t>(std::array{S[2], S[1], S[1], S[0]}), R.range(1.3, 2.4),
                  R.range(.02, .1)});
        }
        for (int i = 0; i < 10; ++i) {
            const double a = R.range(0, TAU), sp = R.range(40, 110);
            emit({ParticleKind::Star, std::cos(a) * 5, CY + std::sin(a) * 5, std::cos(a) * sp, std::sin(a) * sp - 20,
                  R.range(.3, .6), R.pick<uint32_t>(std::array{WHITE, S[1]}), .6, R.range(0, .08)});
        }
        for (int i = 0; i < 6; ++i) {
            const double side = R.sign();
            emit({ParticleKind::Dust, side * R.range(1, 6), -1, side * R.range(25, 70), -R.range(2, 10),
                  R.range(.4, .7), C[2], R.range(1.6, 3), R.range(.02, .08)});
        }
        lights.add(0, CY, 46, C[4], .8, .4);
        lights.add(0, CY, 22, WHITE, .8, .1);
    }
    void   emit(const PoofEmission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    double radius(const Ball& b) const {
        const double age = b.max - b.life, u = age / b.max;
        const double grow    = b.r0 + (b.r1 - b.r0) * ease::outBack(clamp(age / .22), 2),
                     breathe = 1 + .06 * std::sin(age * 14 + b.r1);
        return grow * breathe * (u < .62 ? 1 : 1 - ease::inBack(clamp((u - .62) / .38), 1.4));
    }
    void update(double dt) override {
        t += dt;
        for (size_t left = balls.size(); left > 0; --left) {
            const size_t i = left - 1;
            auto&        b = balls[i];
            if (b.delay > 0) {
                b.delay -= dt;
                continue;
            }
            b.life -= dt;
            if (b.life <= 0) {
                if (b.r1 > 4) emit({ParticleKind::Star, b.x, b.y, 0, -8, .22, WHITE, .6});
                balls.erase(balls.begin() + std::ptrdiff_t(i));
                continue;
            }
            b.vx -= b.vx * 7 * dt;
            b.vy -= b.vy * 7 * dt + 30 * dt;
            b.x += b.vx * dt;
            b.y = std::min(b.y + b.vy * dt, -radius(b) * .7);
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBall(Paint& p, const Ball& b, int layer) const {
        const double age = b.max - b.life, r = radius(b), a = std::min(1., age * 20);
        const bool   lit = age < .12;
        if (r < .6) return;
        const double hy = (b.y - CY + 6) / 14;
        const bool   lo = hy > .4, hi = hy < -.3;
        if (layer == 0)
            p.disc(b.x + r * .16, b.y + r * .2, r, lit ? C[3] : lo ? C[1] : C[2], .8 * a);
        else if (layer == 1)
            p.disc(b.x - r * .06, b.y - r * .08, r * .86, lit ? C[4] : lo ? C[2] : C[3], .88 * a);
        else if (!lo || lit) {
            p.disc(b.x - r * .28, b.y - r * .34, r * .42, lit || hi ? WHITE : C[4], .85 * a);
            if (age < .25) p.disc(b.x - r * .3, b.y - r * .38, r * .2, WHITE, .9 * a * (1 - age / .25));
        }
    }
    void drawBack(Paint& p) const override {
        const double s = 1 - smooth(.1, .9, t);
        if (s > 0) p.glow(0, -1, 26, C[4], .25 * s);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        for (int layer = 0; layer < 3; ++layer)
            for (const auto& b : balls)
                if (b.delay <= 0) drawBall(p, b, layer);
        if (t < .07) {
            const double k = t / .07;
            p.circle(0, CY, 9 + 3 * k, C[4]);
            p.circle(0, CY, 7 + 3 * k, WHITE);
            flare(p, 0, CY, 20 * (1 - k * .5), 1 - k, C);
        }
        const double rk = (t - .03) / .2;
        if (rk > 0 && rk < 1)
            p.ring(0, CY, 8 + 18 * ease::outCubic(rk), std::max(1., 2.5 * (1 - rk)), rk < .3 ? WHITE : C[4], 1.1 - rk);
        for (const auto& l : lines) {
            const double u = (t - l.delay) / .2;
            if (u <= 0 || u >= 1) continue;
            const double head = 8 + (l.radius - 8) * ease::outExpo(clamp(u / .6)),
                         tail = 8 + (l.radius - 8) * ease::inCubic(u), ca = std::cos(l.angle), sa = std::sin(l.angle);
            p.lineFn(
                ca * tail, CY + sa * tail, ca * head, CY + sa * head, [&](double v) { return v > .6 ? WHITE : C[4]; },
                1.);
            p.glowLine(ca * tail, CY + sa * tail, ca * head, CY + sa * head, 3, C[4], .25);
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& poofDescriptor() {
    static const std::array<Option, 1> options{{{"stars", 1, 64, false, 3, &PixelFxSpawnSettings::stars}}};
    static const Descriptor            d{
        {PixelFxKind::Poof, "spawn/poof", 112, 92, 56, 80, 1.6, false},
        6,
        [](PixelFxSettings& s) {
            s.seed        = 21;
            s.palette     = {hex(0x2a2440), hex(0x5e5878), hex(0x9a94b4), hex(0xcdc8e0), hex(0xf2f0fa), WHITE};
            s.spawn.stars = {hex(0xffd84a), hex(0xfff3a6), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Poof>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
