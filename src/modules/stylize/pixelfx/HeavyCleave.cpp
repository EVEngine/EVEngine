// eve-hot-path
// Native adaptation of pixelfx melee/heavy-cleave.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/MeleeSwing.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double RAISE = .2, SWING = .135, HIT = RAISE + SWING, HOLD = .05, TRAIL = .22;
double           head(double t) { return ease::inQuad(clamp((t - RAISE) / SWING)); }
double           passedAt(double f) { return RAISE + SWING * std::sqrt(clamp(f)); }
struct CleaveShard {
    double angle, radius, speed, arc;
};
struct HeavyCleave final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         R;
    const melee::Stone          st;
    const melee::Swing          A{3, -22, 37, .88, .32, -2.45, 1}, E{3, -22, 37 * .76, .88, .32, -2.45, 1};
    double                      U1 = 0, X = 0;
    Point                       glint{};
    std::vector<melee::Crack>   lines;
    std::array<melee::Spike, 7> spikes;
    std::array<CleaveShard, 4>  shards;
    Particles                   ps;
    Bits                        bits;
    Lights                      lights;
    double                      t   = 0;
    bool                        hit = false;
    explicit HeavyCleave(const PixelFxSettings& o) : C(o.palette), R(o.seed), st(melee::stone(o.melee.stone)) {
        ps.sparks = {C[6], C[5], C[4], C[3]};
        while (melee::onSwing(A, A.R, U1).y < -.5 && U1 < 4) U1 += .005;
        X     = melee::onSwing(A, A.R - 2, U1).x;
        glint = melee::onSwing(A, A.R - 3, 0);
        lines = melee::cracks(R, X, 4, 40, 7, .4);
        const std::array<double, 7> angles{-PI, -2.7, -2.15, -1.62, -1.05, -.45, 0};
        for (size_t i = 0; i < spikes.size(); ++i) {
            const double a   = angles[i] + R.range(-.08, .08),
                         len = (i == 0 || i == 6 ? 20.
                                : i == 3         ? 15.
                                                 : 9.) +
                               R.range(0, 4),
                         w   = 1.6 + R.range(0, .6);
            spikes[i]        = {a, len, w};
        }
        size_t i = 0;
        for (double a : {-2.5, -1.9, -1.2, -.6}) {
            const double angle = a + R.range(-.15, .15), radius = 8 + R.range(0, 3), v = R.range(80, 130),
                         arc = R.range(.35, .6);
            shards[i++]      = {angle, radius, v, arc};
        }
    }
    void arc(Paint& p, const melee::Swing& s, double width, double lag, bool rim) const {
        const double tt = t - lag, U = U1 * head(tt), T = U1 * smooth(HIT + HOLD, HIT + HOLD + TRAIL, tt);
        if (U < .02 || T >= U - .02) return;
        const double gone = smooth(HIT + HOLD, HIT + HOLD + TRAIL, tt), shrink = 1 - .75 * gone;
        melee::smear(p, s, T, U, width, [&](double u, double r, ShadeOutput& out, double) {
            const double age = std::max(0., tt - passedAt(u / U1)), v = (u - T) / (U - T),
                         w = width * shrink * std::pow(std::sin(PI * std::pow(v, 2.2)), .6);
            if (w < .4) return;
            const double q = (s.R - r) / w;
            if (q < 0 || q > 1) return;
            const double ci =
                rim && q < .22 && age < .12
                    ? 0
                    : std::floor(q * 7 * .62 + age * 7 + std::max(0., tt - HIT) * 10 - v * .8 + (rim ? .7 : 1.7) + .5);
            out.c = C[size_t(7 - clamp(ci, 0., 7.))];
            out.a = std::min(1., v / .35) * (1.2 - gone);
        });
        if (rim)
            melee::glowAlong(p, s, s.R - width * .4, T, U, 7 + width * .5, C[4], [&](double u) {
                return .26 * std::exp(-std::max(0., tt - passedAt(u / U1)) / .2) * ((u - T) / (U - T));
            });
    }
    void update(double dt) override {
        t += dt;
        if (t < RAISE - .04 && R.isChance(.35)) {
            const double a = R.range(0, TAU), D = R.range(14, 26), life = R.range(.14, .2),
                         v     = (D * 3) / (std::exp(3 * life) - 1);
            const auto   color = R.isChance(.5) ? C[5] : C[4];
            ps.spawn(R, ParticleKind::Streak, glint.x + std::cos(a) * D, glint.y + std::sin(a) * D, -std::cos(a) * v,
                     -std::sin(a) * v, life, color);
        }
        if (t > RAISE && t < HIT && R.isChance(.7)) {
            const double u = U1 * head(t) * R.range(.6, 1), radius = A.R - R.range(0, 6);
            const auto   v  = melee::onSwing(A, radius, u);
            const double vx = R.range(-20, 20), vy = R.range(-40, -10), life = R.range(.5, 1.1), size = R.range(1, 2);
            ps.spawn(R, ParticleKind::Ember, v.x, v.y, vx, vy, life, C[4], size);
        }
        if (!hit && t >= HIT) {
            hit = true;
            lights.add(X, -6, 26, WHITE, .9, .1);
            lights.add(X, -12, 64, C[4], .5, .8);
            for (int i = 0; i < 26; ++i) {
                const double a = -PI / 2 + R.range(-1.2, 1.35), speed = R.range(140, 360), life = R.range(.15, .5);
                ps.spawn(R, ParticleKind::Spark, X, -1, std::cos(a) * speed, std::sin(a) * speed - 40, life);
            }
            for (int i = 0; i < 20; ++i) {
                const double side = i % 2 ? -1. : 1., x = X + side * R.range(0, 8), y = -R.range(2, 6),
                             vx = side * R.range(45, 135), vy = -R.range(2, 12), life = R.range(1, 1.45),
                             size = R.range(5, 8.5), delay = R.range(0, .07);
                ps.spawn(R, ParticleKind::Smoke, x, y, vx, vy, life, st.dust, size, delay, ParticleFlag::Big);
            }
            for (int i = 0; i < 4; ++i) {
                const double x = X + R.range(-4, 4), y = -R.range(3, 8), vx = R.range(-15, 15), vy = -R.range(40, 90),
                             life = R.range(1, 1.45), size = R.range(4, 6.5), delay = R.range(.02, .1);
                ps.spawn(R, ParticleKind::Smoke, x, y, vx, vy, life, st.dust, size, delay, ParticleFlag::Big);
            }
            for (int i = 0; i < 8; ++i) {
                const double x = X + R.range(-4, 4), y = -R.range(1, 4), vx = R.range(-60, 60), vy = -R.range(8, 30),
                             life = R.range(.35, .7), size = R.range(2, 3.5);
                ps.spawn(R, ParticleKind::Dust, x, y, vx, vy, life, st.dust, size);
            }
            const std::array<uint32_t, 4> stones{st.light, st.body, st.body, st.shade};
            for (int i = 0; i < 44; ++i) {
                const double a = -PI / 2 + R.range(-.7, 1), speed = R.range(60, 230), x = X + R.range(-3, 3),
                             life  = R.range(1, 1.5);
                const auto   color = R.pick<uint32_t>(stones);
                const double size  = R.isChance(.35) ? 2 : 1;
                ps.spawn(R, ParticleKind::Grit, x, -1, std::cos(a) * speed, std::sin(a) * speed, life, color, size);
            }
            for (int i = 0; i < 6; ++i) {
                Debris b;
                b.kind            = DebrisKind::Chunk;
                b.x               = X + R.range(-3, 3);
                b.y               = -2;
                b.vx              = R.range(-50, 110);
                b.vy              = -R.range(120, 260);
                b.gravity         = 1;
                b.drag            = .4;
                b.bounce          = .3;
                b.life            = R.range(1.15, 1.5);
                b.angle           = R.range(0, TAU);
                b.angularVelocity = R.range(-14, 14);
                b.size            = R.integer(1, 2);
                bits.add(std::move(b));
            }
            for (int i = 0; i < 16; ++i) {
                const double x = X + R.range(-8, 8), y = -R.range(1, 6), vx = R.range(-50, 60), vy = -R.range(30, 110),
                             life = R.range(.7, 1.5), size = R.range(1, 2);
                ps.spawn(R, ParticleKind::Ember, x, y, vx, vy, life, C[4], size);
            }
        }
        if (hit && t < HIT + .6 && R.isChance(.12)) {
            const auto&  c  = lines[size_t(R.integer(0, int(lines.size()) - 1))];
            const auto   v  = c.points[size_t(R.integer(0, int(c.points.size()) - 1))];
            const double vx = R.range(-12, 12), vy = -R.range(20, 50), life = R.range(.5, .9);
            ps.spawn(R, ParticleKind::Ember, v.x, v.y - 1, vx, vy, life, C[4], 1);
        }
        bits.update(dt, [](double) { return 0.; });
        for (auto& b : bits.list)
            if (b.rest && !b.dusted) {
                b.dusted = true;
                for (int i = 0; i < 2; ++i) {
                    const double x = b.x + R.range(-1.5, 1.5), vx = R.signedRange(8, 30), vy = -R.range(2, 12),
                                 life = R.range(.3, .6), size = R.range(1.3, 2.4);
                    ps.spawn(R, ParticleKind::Dust, x, -.5, vx, vy, life, st.dust, size);
                }
            }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void shock(Paint& p, double q, bool back) const {
        const double k = q / .45;
        if (k >= 1) return;
        const std::array<uint32_t, 4> ramp{st.body, st.light, C[4], C[6]};
        band(p, X, .5, 6 + 46 * ease::outCubic(k), .16, std::max(1., 3 * (1 - k)), 1.15 - k, ramp,
             [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; });
    }
    void drawBack(Paint& p) const override {
        const double q = t - HIT;
        if (q >= 0) {
            const double fade = 1 - smooth(1, 1.5, q);
            for (int y = 0; y <= 2; ++y)
                for (int x = -9; x <= 9; ++x) {
                    const double d = jsHypot2((x + .5) / 9, (y + .5) / 3.5);
                    if (d > 1) continue;
                    const double T =
                        std::pow(clamp(1 - q / .8), 1.4) * (1.2 - d) - rand({jsUint(x), jsUint(y), 3}) * .3;
                    if (T > .08)
                        p.dot(X + x, y, C[size_t(clamp(std::floor(T * 9 + .5), 1., 7.))], fade);
                    else
                        p.dot(X + x, y, d < .5 ? st.dark : st.shade,
                              (d < .9 - rand({jsUint(x), jsUint(y), 9}) * .3 ? 1 : 1 - d) * fade);
                }
            shock(p, q, true);
        }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const double q = t - HIT;
        if (t < RAISE + .03) {
            const double g =
                ease::outBack(clamp(t / .1)) * (1 + .15 * std::sin(t * 60)) * (t > RAISE ? 1 - (t - RAISE) / .03 : 1);
            p.sparkle(glint.x, glint.y, 1 + 7 * g, C[6], C[4], .5);
            p.glow(glint.x, glint.y, 22, C[4], .3 * g);
        }
        if (q >= 0) {
            const double heat = std::pow(clamp(1 - q / 1.2), 1.2), fade = 1 - smooth(1.15, 1.5, q);
            for (const auto& c : lines) {
                const double reach = 40 * ease::outCubic(clamp((q - c.at * .1) / .13)) * (c.fork ? .3 : 1);
                if (reach <= 0) continue;
                melee::along(c.points, reach, [&](double x0, double y0, double x1, double y1, double d) {
                    const double h     = heat * (1 - d / 46) - (c.fork ? .12 : 0);
                    const auto   color = h > .72   ? WHITE
                                         : h > .08 ? C[size_t(clamp(std::floor(h * 8 + .5), 1., 6.))]
                                                   : st.dark;
                    if (y1 > .5) p.line(x0 + 1, y0, x1 + 1, y1, st.dark, fade);
                    p.line(x0, y0, x1, y1, color, fade);
                    if (h > .25 && int(d) % 6 < 2) p.glow(x1, y1, 6, C[4], .2 * h * fade);
                });
            }
            if (heat > 0) p.glow(X, -2, 34, C[4], .32 * heat * fade);
            shock(p, q, false);
        }
        arc(p, E, 5, .02, false);
        arc(p, A, 16, 0, true);
        const double tail = U1 * smooth(HIT + HOLD, HIT + HOLD + TRAIL, t),
                     th   = .8 * (1 - clamp((t - HIT - HOLD) / (TRAIL + .25)));
        if (tail > .02 && th > 0)
            melee::trace(p, A, A.R - .5, 0, tail, C[3], [&](double u) { return th * (.4 + .6 * u / U1); });
        if (t > RAISE && t < HIT + .05) {
            const auto v = melee::onSwing(A, A.R - 5, U1 * head(t));
            p.glow(v.x, v.y, 40, C[4], .5);
        }
        if (q >= 0 && q < .035)
            melee::star(p, X, -1, spikes, 1, 1, WHITE, C[6], C[7], C[5]);
        else if (q >= .035 && q < .07)
            melee::star(p, X, -1, spikes, 1.25, .55, C[7], C[6], C[6], C[4]);
        if (q >= .05 && q < .16) {
            const double k = (q - .05) / .11;
            for (const auto& s : shards)
                melee::sliver(p, X, -1, s.angle, s.radius + s.speed * (q - .05), s.arc * (1 - .4 * k),
                              k < .4   ? C[6]
                              : k < .7 ? C[5]
                                       : C[4],
                              1 - k);
        }
        if (q >= 0 && q < .2) {
            const double pop = std::exp(-q / .05);
            p.glow(X, -6, 30, C[5], .8 * pop);
            p.glow(X, -2, 10, WHITE, .7 * pop);
        }
        for (const auto& b : bits.list) melee::rock(p, b, st);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& heavyCleaveDescriptor() {
    static const std::array<Option, 2> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
         {"stone", 0, 4294967295., false, 0, &PixelFxMeleeSettings::stone, true}}};
    static const Descriptor d{
        {PixelFxKind::HeavyCleave, "melee/heavy-cleave", 208, 116, 68, 96, 1.9, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 5;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x2a0c10), hex(0x5c1414), hex(0xa3241a), hex(0xe0481c),
                              hex(0xff8a2a), hex(0xffc23d), hex(0xfff0a0), WHITE};
            s.melee.stone  = hex(0x4a3f69);
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<HeavyCleave>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
