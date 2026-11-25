// eve-hot-path
// Native adaptation of pixelfx melee/hammer-smash.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/MeleeSwing.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double              DROP = .15, HW = 13;
const uint32_t                SOOT = hex(0x0d0a12);
const std::array<uint32_t, 4> HAMMER_HOT{hex(0xfff3a6), hex(0xffc23d), hex(0xff8a2a), hex(0xc9281f)};
double                        head(double t) {
    const double x = clamp((t + .03) / (DROP + .03));
    return .28 * x + .72 * x * x;
}
struct HammerRay {
    double angle, length;
};
struct HammerSmash final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         R;
    const melee::Stone          st;
    const melee::Swing          A{5, -25, 32, 1, 0, -2.25, 1};
    const double                Rm = A.R - HW / 2;
    double                      U1 = 0, X = 0;
    std::vector<melee::Crack>   lines;
    std::array<HammerRay, 7>    rays;
    Particles                   ps;
    Bits                        bits, chips;
    Lights                      lights;
    double                      t   = 0;
    bool                        hit = false;
    explicit HammerSmash(const PixelFxSettings& o) : C(o.palette), R(o.seed), st(melee::stone(o.melee.stone)) {
        while (melee::onSwing(A, A.R, U1).y < -.5 && U1 < 4) U1 += .005;
        X     = melee::onSwing(A, A.R - HW / 2, U1).x;
        lines = melee::cracks(R, X, 5, 24, 6, 0);
        const std::array<double, 7> deltas{-.95, -.6, -.3, -.05, .22, .5, .85};
        for (size_t i = 0; i < rays.size(); ++i) {
            const double a = -PI / 2 + deltas[i] + R.range(-.06, .06), L = (i == 3             ? 34.
                                                                            : i == 2 || i == 4 ? 26.
                                                                            : i == 1 || i == 5 ? 18.
                                                                                               : 12.) *
                                                                           R.range(.9, 1.1);
            rays[i] = {a, L};
        }
    }
    void wedge(Paint& p) const {
        const double h = head(t), U = U1 * h, after = smooth(DROP, DROP + .07, t),
                     lagU = std::max(0., U - (.3 + 1.05 * h)), T = lagU + (U - lagU) * after * .8;
        if (U - T < .03 || after >= 1) return;
        const double len = (U - T) * Rm;
        melee::smear(p, A, T, U, HW + 1, [&](double u, double r, ShadeOutput& out, double) {
            const double lead = (U - u) * Rm, f = lead / len,
                         w   = HW * (lead < 3 ? 1 : std::pow(1 - (lead - 3) / std::max(1., len - 3), .85)) *
                               (1 - after * .5),
                         off = (r - Rm) / (w / 2 + .01);
            if (w < .6 || std::abs(off) > 1) return;
            const double ci =
                lead < 2 ? 0 : std::floor(1 + f * 3.4 + std::abs(off) * 1.4 - (off > .55 ? 1 : 0) + after * 3 + .5);
            if (ci > 5) return;
            out.c = C[size_t(7 - std::max(0., ci))];
        });
        if (after < .6)
            for (const auto& v : std::array<std::array<double, 3>, 6>{{{-HW / 2 - 2, 3, 26},
                                                                       {-HW / 2 - 5, 9, 16},
                                                                       {HW / 2 + 2, 2, 30},
                                                                       {HW / 2 + 5, 8, 18},
                                                                       {-1, len + 2, 14},
                                                                       {3, len + 4, 9}}}) {
                const double r = Rm + v[0];
                for (double d = v[1]; d < v[1] + v[2] * (1 - after); d += .5) {
                    const double u = U - d / r, k = (d - v[1]) / v[2];
                    if (u < 0) break;
                    const auto q = melee::onSwing(A, r, u);
                    p.dot(q.x, q.y, k < .25 ? C[6] : k < .6 ? C[5] : C[4], (1 - k) * (1 - after));
                }
            }
        const auto q = melee::onSwing(A, Rm, U);
        p.glow(q.x, q.y, 34, C[4], .5 * (1 - smooth(DROP - .03, DROP, t)));
    }
    void update(double dt) override {
        t += dt;
        if (!hit && t >= DROP) {
            hit = true;
            lights.add(X, -5, 16, WHITE, .35, .08);
            lights.add(X, -10, 64, C[4], .42, .6);
            const std::array<int, 5> sizes{1, 1, 2, 2, 3};
            for (int i = 0; i < 15; ++i) {
                const int side = R.sign();
                Debris    b;
                b.kind            = DebrisKind::Chunk;
                b.x               = X + side * R.range(0, 5);
                b.y               = -2;
                b.vx              = side * R.range(20, 105);
                b.vy              = -R.range(110, 300);
                b.gravity         = 1;
                b.drag            = .35;
                b.bounce          = .38;
                b.life            = R.range(1.25, 1.65);
                b.angle           = R.range(0, TAU);
                b.angularVelocity = R.range(-16, 16);
                b.size            = R.pick<int>(sizes);
                bits.add(std::move(b));
            }
            for (int i = 0; i < 11; ++i) {
                const double a = -PI / 2 + R.range(-1.2, 1.2), speed = R.range(90, 230);
                Debris       b;
                b.kind    = DebrisKind::Shard;
                b.x       = X + R.range(-4, 4);
                b.y       = -2;
                b.vx      = std::cos(a) * speed;
                b.vy      = std::sin(a) * speed;
                b.gravity = .9;
                b.drag    = .5;
                b.bounce  = .35;
                b.life    = R.range(.9, 1.5);
                b.size    = 1;
                chips.add(std::move(b));
            }
            const std::array<uint32_t, 4> stones{st.light, st.body, st.body, st.shade};
            for (int i = 0; i < 40; ++i) {
                const double a = -PI / 2 + R.range(-1.1, 1.1), speed = R.range(50, 210), x = X + R.range(-4, 4),
                             life  = R.range(.9, 1.5);
                const auto   color = R.pick<uint32_t>(stones);
                const double size  = R.isChance(.4) ? 2 : 1;
                ps.spawn(R, ParticleKind::Grit, x, -1, std::cos(a) * speed, std::sin(a) * speed, life, color, size);
            }
            for (int i = 0; i < 14; ++i) {
                const double side = i % 2 ? -1. : 1., speed = R.range(120, 260), y = -R.range(1, 4),
                             vy = -R.range(0, 30), life = R.range(.18, .34);
                const auto   color = R.isChance(.5) ? C[6] : C[5];
                const double delay = R.range(.02, .05);
                ps.spawn(R, ParticleKind::Mote, X + side * 6, y, side * speed, vy, life, color, .8, delay);
            }
            for (int i = 0; i < 24; ++i) {
                const double a = -PI / 2 + R.range(-1.4, 1.4), speed = R.range(150, 380), life = R.range(.12, .42),
                             delay = R.range(.035, .08);
                ps.spawn(R, ParticleKind::Spark, X + std::cos(a) * 6, -2 + std::sin(a) * 4, std::cos(a) * speed,
                         std::sin(a) * speed - 30, life, 0, 1, delay);
            }
            for (int i = 0; i < 20; ++i) {
                const double side = i % 2 ? -1. : 1., x = X + side * R.range(2, 10), y = -R.range(2, 7),
                             vx = side * R.range(40, 125), vy = -R.range(2, 14), life = R.range(.95, 1.5),
                             size = R.range(6, 9), delay = R.range(.02, .08);
                ps.spawn(R, ParticleKind::Smoke, x, y, vx, vy, life, st.dust, size, delay, ParticleFlag::Big);
            }
            for (int i = 0; i < 7; ++i) {
                const double x = X + R.range(-6, 6), y = -R.range(3, 9), vx = R.range(-20, 20), vy = -R.range(30, 80),
                             life = R.range(1, 1.4), size = R.range(5, 7.5), delay = R.range(.04, .14);
                ps.spawn(R, ParticleKind::Smoke, x, y, vx, vy, life, st.dust, size, delay, ParticleFlag::Big);
            }
            for (int i = 0; i < 8; ++i) {
                const double x = X + R.range(-6, 6), y = -R.range(1, 4), vx = R.range(-70, 70), vy = -R.range(8, 30),
                             life = R.range(.35, .7), size = R.range(2, 3.5);
                ps.spawn(R, ParticleKind::Dust, x, y, vx, vy, life, st.dust, size);
            }
        }
        bits.update(dt, [](double) { return 0.; });
        chips.update(dt, [](double) { return 0.; });
        if (hit && t > DROP + .4 && t < DROP + 1.2 && R.isChance(.08)) {
            const int index = R.integer(0, int(bits.list.size()) - 1);
            if (!bits.list.empty()) {
                const auto& b = bits.list[size_t(index)];
                if (b.rest) {
                    const double x = b.x + R.range(-1, 1), life = R.range(.25, .45);
                    const auto   color = R.isChance(.5) ? HAMMER_HOT[0] : WHITE;
                    const double size  = R.range(.6, 1.6);
                    ps.spawn(R, ParticleKind::Star, x, b.y - 1.5 - b.size, 0, -2, life, color, size);
                }
            }
        }
        for (auto& b : bits.list)
            if (b.rest && !b.dusted) {
                b.dusted = true;
                for (int i = 0; i < 3; ++i) {
                    const double x = b.x + R.range(-1.5, 1.5), vx = R.signedRange(8, 35), vy = -R.range(2, 14),
                                 life = R.range(.3, .6), size = R.range(1.3, 2.6);
                    ps.spawn(R, ParticleKind::Dust, x, -.5, vx, vy, life, st.dust, size);
                }
            }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void shock(Paint& p, double q, bool back) const {
        const double k = q / .5;
        if (k >= 1) return;
        const std::array<uint32_t, 4> ramp{st.body, C[3], C[5], C[7]};
        band(p, X, .5, 6 + 50 * ease::outCubic(k), .16, std::max(1., 2 * (1 - k)), 1.15 - k, ramp,
             [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; });
    }
    void drawBack(Paint& p) const override {
        const double q = t - DROP;
        if (q >= 0) {
            const double fade = 1 - smooth(1.3, 1.7, q);
            for (int x = -11; x <= 11; ++x) {
                const double u = double(x) / 10,
                             d = std::abs(x) > 10
                                     ? 0
                                     : std::floor(3.2 * (1 - u * u) + (rand({jsUint(x), 4}) - .5) * .8 + .5);
                for (int y = 0; y < d; ++y) p.dot(X + x, y, y == 0 ? SOOT : st.dark, fade);
                if (d > 0) p.dot(X + x, d, std::abs(x) < 7 ? st.light : st.body, fade);
                if (std::abs(x) >= 8) p.dot(X + x, -1, std::abs(x) >= 10 ? st.body : st.light, fade);
                if (std::abs(x) == 9 && rand({jsUint(x), 7}) < .7) p.dot(X + x, -2, st.body, fade);
            }
            shock(p, q, true);
        }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const double q = t - DROP;
        wedge(p);
        if (q >= 0) {
            const double fade = 1 - smooth(1.3, 1.7, q);
            for (const auto& c : lines) {
                const double reach = 30 * ease::outCubic(clamp((q - c.at * .06) / .08)) * (c.fork ? .3 : 1);
                if (reach > 0)
                    melee::along(c.points, reach, [&](double x0, double y0, double x1, double y1, double) {
                        if (q >= .12) p.line(x0, y0 + 3, x1, y1 + 3, st.shade, fade);
                        p.line(x0, y0 + 2, x1, y1 + 2, q < .05 ? WHITE : q < .12 ? C[5] : SOOT, fade);
                    });
            }
            shock(p, q, false);
            const double d = (q - .045) / .4;
            if (d >= 0 && d < 1) {
                const double r = 12 + 30 * ease::outCubic(d), th = std::max(1., 3.6 * std::pow(1 - d, .8)),
                             alpha = d < .6 ? 1 : (1 - d) / .4;
                p.shader(X - r - 1, -r - 1, X + r + 1, 0, [&](double x, double y, ShadeOutput& out) {
                    const double e = r - jsHypot2(x - X, y);
                    if (e < 0 || e > th || y > -.5) return;
                    const double v = e / th;
                    out.c          = d < .1    ? (v < .6 ? WHITE : C[6])
                                     : v < .34 ? (d < .45 ? WHITE : C[6])
                                     : v < .67 ? C[5]
                                               : C[4];
                    out.a          = alpha;
                });
                for (int i = 0; i < 7; ++i) {
                    const double g = -PI * (.08 + (double(i) / 6) * .84);
                    p.glow(X + std::cos(g) * r, std::sin(g) * r, 14, C[4], .36 * (1 - d));
                }
            }
            if (q < .2)
                for (const auto& ray : rays) {
                    const double d1 = ray.length * ease::outCubic(std::min(1., q / .05)),
                                 d0 = ray.length * smooth(.04, .2, q), ca = std::cos(ray.angle),
                                 sa = std::sin(ray.angle);
                    if (d1 - d0 < 1) continue;
                    p.lineFn(
                        X + ca * (2 + d0), -1 + sa * (2 + d0), X + ca * d1, -1 + sa * d1,
                        [&](double u) { return u < .5   ? WHITE
                                               : u < .8 ? C[6]
                                                        : C[5]; }, 1.);
                    p.glowLine(X + ca * d0, -1 + sa * d0, X + ca * d1, -1 + sa * d1, 4, C[5], .18 * (1 - q / .2));
                }
            if (q < .07) {
                const bool   hot = q < .035;
                const double rx = hot ? 16 : 12, ry = hot ? 3 : 2;
                p.ellipse(X, 0, rx + 2, ry + 1, hot ? C[6] : C[5]);
                p.ellipse(X, 0, rx, ry, hot ? WHITE : C[7]);
            }
            if (q < .2) p.glow(X, -5, 26, C[5], .3 * std::exp(-q / .05));
            p.glow(X, -1, 16, HAMMER_HOT[2], .28 * std::exp(-q / .35));
        }
        for (const auto& b : bits.list) melee::rock(p, b, st);
        for (const auto& b : chips.list) {
            const double k     = b.life / b.max;
            const auto   color = k > .7    ? HAMMER_HOT[0]
                                 : k > .45 ? HAMMER_HOT[1]
                                 : k > .25 ? HAMMER_HOT[2]
                                           : HAMMER_HOT[3];
            p.dot(b.x, b.y, color, std::min(1., k * 4));
            if (!b.rest) p.dot(b.x - b.vx * .012, b.y - b.vy * .012, HAMMER_HOT[3], std::min(1., k * 4));
            p.glow(b.x, b.y, 4, color, .35 * k);
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& hammerSmashDescriptor() {
    static const std::array<Option, 2> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
         {"stone", 0, 4294967295., false, 0, &PixelFxMeleeSettings::stone, true}}};
    static const Descriptor d{
        {PixelFxKind::HammerSmash, "melee/hammer-smash", 208, 112, 80, 92, 2., false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 9;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x141834), hex(0x242c5c), hex(0x38479a), hex(0x5a72d0),
                              hex(0x8fa8f0), hex(0xc4d4ff), hex(0xeef3ff), WHITE};
            s.melee.stone  = hex(0x4a3f69);
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<HammerSmash>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
