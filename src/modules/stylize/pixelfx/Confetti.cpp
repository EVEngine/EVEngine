// eve-hot-path
// Native adaptation of pixelfx ui/confetti.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
struct Popper {
    double x, y, a, time;
};
constexpr std::array<Popper, 2> POPPERS{{{-74, -1, -PI / 2 + .62, .1}, {74, -1, -PI / 2 - .62, .26}}};
struct Paper {
    double   x, y, vx, vy;
    uint32_t color;
    double   w, h, angle, spin, flip, fs, phase, amp, fw, x0 = 0;
    int      mode = 0;
    double   rest = 0, fall = 0;
};
struct Ribbon {
    std::array<Point, 11> points;
    double                vx, vy;
    uint32_t              color;
    double                phase, amp, fw;
    int                   mode = 0;
    double                x0 = 0, curl = 0, fall = 0;
};
struct Confetti final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         rng;
    Particles                   ps;
    Lights                      lights;
    std::array<Paper, 92>       bits{};
    std::array<Ribbon, 8>       ribbons{};
    size_t                      bitCount = 0, ribbonCount = 0;
    std::array<bool, 2>         fired{};
    double                      t = 0;
    explicit Confetti(const PixelFxSettings& o) : C(o.palette), rng(o.seed) {
        ps.sparks = {WHITE, hex(0xfff3a6), hex(0xffc23d), hex(0xff8a2a)};
    }
    void fire(const Popper& pp) {
        const double ca = std::cos(pp.a), sa = std::sin(pp.a), mx = pp.x + ca * 11, my = pp.y + sa * 11;
        lights.add(mx, my, 34, WHITE, .6, .12);
        lights.add(mx, my, 50, C[2], .4, .4);
        for (int i = 0; i < 6; ++i) {
            const double speedX = rng.range(20, 60), jitter = rng.range(-10, 10), speedY = rng.range(20, 60),
                         life = rng.range(.6, 1), size = rng.range(2.5, 4);
            ps.spawn(rng, ParticleKind::Smoke, mx, my, ca * speedX + jitter, sa * speedY, life, hex(0x5d5660), size);
        }
        for (int i = 0; i < 12; ++i) {
            const double a = pp.a + rng.range(-.5, .5), speed = rng.range(120, 260), life = rng.range(.1, .25);
            ps.spawn(rng, ParticleKind::Spark, mx, my, std::cos(a) * speed, std::sin(a) * speed, life);
        }
        for (int i = 0; i < 46; ++i) {
            const double a = pp.a + rng.range(-.38, .38), speed = rng.range(200, 380);
            Paper        b{};
            b.x              = mx;
            b.y              = my;
            b.vx             = std::cos(a) * speed;
            b.vy             = std::sin(a) * speed;
            b.color          = rng.pick<uint32_t>(C);
            b.w              = rng.range(2, 3);
            b.h              = rng.range(3, 4.6);
            b.angle          = rng.range(0, TAU);
            b.spin           = rng.range(-14, 14);
            b.flip           = rng.range(0, TAU);
            b.fs             = rng.range(5, 13);
            b.phase          = rng.range(0, TAU);
            b.amp            = rng.range(5, 12);
            b.fw             = rng.range(2, 3.4);
            b.fall           = rng.range(26, 40);
            bits[bitCount++] = b;
        }
        for (int i = 0; i < 4; ++i) {
            const double a = pp.a + rng.range(-.25, .25), speed = rng.range(220, 330);
            Ribbon       r{};
            r.points.fill({mx, my});
            r.vx                   = std::cos(a) * speed;
            r.vy                   = std::sin(a) * speed;
            r.color                = rng.pick<uint32_t>(C);
            r.phase                = rng.range(0, TAU);
            r.amp                  = rng.range(6, 12);
            r.fw                   = rng.range(1.5, 2.4);
            r.curl                 = rng.range(.7, 1.1);
            r.fall                 = rng.range(22, 32);
            ribbons[ribbonCount++] = r;
        }
    }
    void update(double dt) override {
        t += dt;
        for (size_t i = 0; i < POPPERS.size(); ++i)
            if (!fired[i] && t >= POPPERS[i].time) {
                fired[i] = true;
                fire(POPPERS[i]);
            }
        for (size_t i = 0; i < bitCount; ++i) {
            auto& b = bits[i];
            if (b.mode == 2) {
                b.rest += dt;
                continue;
            }
            b.flip += b.fs * dt;
            if (b.mode == 0) {
                b.vx -= b.vx * 3.4 * dt;
                b.vy -= b.vy * 3.4 * dt - 140 * dt;
                b.x += b.vx * dt;
                b.y += b.vy * dt;
                b.angle += b.spin * dt;
                if (jsHypot2(b.vx, b.vy) < 40) {
                    b.mode = 1;
                    b.x0   = b.x - std::sin(b.phase) * b.amp;
                }
            } else {
                b.phase += b.fw * dt;
                b.x0 += b.vx * dt;
                b.vx -= b.vx * 2 * dt;
                b.x = b.x0 + std::sin(b.phase) * b.amp;
                b.y += b.fall * (.4 + .9 * std::abs(std::cos(b.phase))) * dt;
                b.angle = std::cos(b.phase) * .9;
                if (b.y >= -1) {
                    b.y     = -.5;
                    b.mode  = 2;
                    b.angle = rng.range(-.3, .3);
                    b.flip  = 0;
                }
            }
        }
        for (size_t j = 0; j < ribbonCount; ++j) {
            auto& r = ribbons[j];
            auto& h = r.points[0];
            if (r.mode == 0) {
                r.vx -= r.vx * 3 * dt;
                r.vy -= r.vy * 3 * dt - 120 * dt;
                h.x += r.vx * dt;
                h.y += r.vy * dt;
                if (jsHypot2(r.vx, r.vy) < 35) {
                    r.mode = 1;
                    r.x0   = h.x - std::sin(r.phase) * r.amp;
                }
            } else if (h.y < -1) {
                r.phase += r.fw * dt;
                r.x0 += r.vx * dt;
                r.vx -= r.vx * 2 * dt;
                h.x = r.x0 + std::sin(r.phase) * r.amp;
                h.y += r.fall * (.5 + .7 * std::abs(std::cos(r.phase))) * dt;
            }
            for (size_t i = 1; i < r.points.size(); ++i) {
                const auto& a   = r.points[i - 1];
                auto&       b   = r.points[i];
                b.y             = std::min(-.5, b.y + 20 * dt);
                const double dx = b.x - a.x, dy = b.y - a.y, d0 = jsHypot2(dx, dy), d = d0 ? d0 : 1;
                if (d > 2.6) {
                    b.x = a.x + (dx / d) * 2.6;
                    b.y = a.y + (dy / d) * 2.6;
                }
            }
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override { ps.draw(p, Paint::Layer::Back); }
    void draw(Paint& p) const override {
        const double fade = 1 - seg(t, 2.9, 3.3);
        for (const auto& pp : POPPERS) {
            const double k = t < pp.time ? 0 : 3 * std::exp(-(t - pp.time) * 14), ca = std::cos(pp.a),
                         sa = std::sin(pp.a), bx = pp.x - ca * k, by = pp.y - sa * k, alpha = 1 - seg(t, 2.5, 2.9);
            p.shader(bx - 12, by - 12, bx + 12, by + 12, [&](double x, double y, ShadeOutput& out) {
                const double dx = x - bx, dy = y - by, u = dx * ca + dy * sa, v = -dx * sa + dy * ca;
                if (u < 0 || u > 11) return;
                const double hw = .8 + u * .32;
                if (std::abs(v) > hw + .6) return;
                out.a = alpha;
                if (std::abs(v) > hw - .4 || u > 10.2 || u < .8) {
                    out.c = hex(0x141019);
                    return;
                }
                const bool stripe = std::fmod(std::floor((u + v * .6) / 2.2), 2) == 0;
                out.c             = stripe ? (v < 0 ? C[2] : scale(C[2], .75)) : v < 0 ? WHITE : hex(0xc8c0d8);
            });
            const double ft = t - pp.time;
            if (ft > 0 && ft < .12) p.sparkle(pp.x + ca * 13, pp.y + sa * 13, 7 * (1 - ft / .12), WHITE, C[2], .7);
        }
        for (size_t j = 0; j < ribbonCount; ++j) {
            const auto& r      = ribbons[j];
            auto        points = r.points;
            for (size_t i = 0; i < points.size(); ++i)
                points[i].x += std::sin(double(i) * r.curl + t * 9 + r.phase) * (i ? 1.6 : 0);
            for (size_t i = 1; i < points.size(); ++i) {
                const auto& a     = points[i - 1];
                const auto& b     = points[i];
                const bool  front = std::sin(double(i) * r.curl + t * 9 + r.phase) > 0;
                p.line(a.x, a.y, b.x, b.y, front ? r.color : scale(r.color, .6), fade);
                p.line(a.x + 1, a.y, b.x + 1, b.y, front ? mix(r.color, WHITE, .35) : scale(r.color, .45), fade);
            }
        }
        for (size_t i = 0; i < bitCount; ++i) {
            const auto& b = bits[i];
            if (b.y > 1) continue;
            const double fw = b.mode == 2 ? 1 : std::cos(b.flip), w = std::max(.5, std::abs(fw) * b.w),
                         ca = std::cos(b.angle), sa = std::sin(b.angle);
            const auto   color = fw >= 0 ? b.color : scale(b.color, .62);
            if (b.mode == 2) {
                p.dot(b.x, b.y, scale(b.color, .8), fade);
                p.dot(b.x + 1, b.y, scale(b.color, .6), fade);
                continue;
            }
            p.shader(b.x - 4, b.y - 4, b.x + 4, b.y + 4, [&](double x, double y, ShadeOutput& out) {
                const double dx = x - b.x, dy = y - b.y, u = dx * ca + dy * sa, v = -dx * sa + dy * ca;
                if (std::abs(u) > w / 2 + .2 || std::abs(v) > b.h / 2) return;
                out.a = fade;
                out.c = std::abs(fw) > .92 && v < -b.h / 2 + 1 ? WHITE : color;
            });
            if (std::abs(fw) > .97) p.glow(b.x, b.y, 5, mix(b.color, WHITE, .5), .35 * fade);
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& confettiDescriptor() {
    static const Descriptor d{
        {PixelFxKind::Confetti, "ui/confetti", 200, 140, 100, 126, 3.4, false},
        7,
        [](PixelFxSettings& s) {
            s.seed    = 11;
            s.palette = {hex(0xff4a5a), hex(0xff9a2a), hex(0xffe03c), hex(0x5ad84a),
                         hex(0x3cc8ff), hex(0x7a6cff), hex(0xf53cd1)};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Confetti>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
