// eve-hot-path
// Native adaptation of pixelfx lightning/chain-lightning.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Lightning.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr Point                HAND{10, -16};
constexpr std::array<Point, 3> HOPS{{{58, -22}, {104, -30}, {150, -19}}};
constexpr double               WIND = .12, LEAP = .05, GAP = .075, HOLD = .78, BREAK = .16;
double                         on(size_t i) { return WIND + double(i) * GAP + LEAP; }
double                         off(size_t i) { return HOLD + double(i) * .05; }
double                         level(size_t i, double t) {
    const double a = t - on(i);
    if (a < 0) return 0;
    const double frame = std::floor(a * 30),
                 hum   = a < .1 ? (frame >= 0 && frame < 3 ? std::array<double, 3>{{1, .55, 1}}[size_t(frame)] : 1)
                                : .78 + .22 * std::sin(t * 61 + double(i)) * std::sin(t * 23 + double(i) * 2),
                 b     = t - off(i);
    return b < 0 ? hum : b < BREAK ? hum * (1 - b / BREAK) * (std::fmod(std::floor(b * 60), 3) == 1 ? .5 : 1) : 0;
}
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
struct ChainLightning final : Kernel {
    std::vector<uint32_t> C;
    uint32_t              seed;
    Rng                   R;
    Particles             ps, hot;
    Lights                lights;
    std::array<Point, 4>  pts{{HAND, HOPS[0], HOPS[1], HOPS[2]}};
    std::array<bool, 3>   hit{}, popped{};
    std::array<double, 3> ground{};
    double                t = 0;
    explicit ChainLightning(const PixelFxSettings& o) : C(o.palette), seed(o.seed), R(o.seed) {
        ps.sparks = o.lightning.sparks;
        for (size_t i = 0; i < ground.size(); ++i) ground[i] = HOPS[i].x + R.range(-5, 5);
    }
    void emit(Particles& target, const Emission& e) {
        target.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay);
    }
    /** @brief Build one owning bowed link from its source frame stream.
     * @cost Two branch trees per link draw, released after synchronous inking. */
    std::vector<lightning::Path> shape(size_t i, uint32_t f) const {
        Rng          r(hash({seed, 31, uint32_t(i), f}));
        const auto   a = pts[i], b = pts[i + 1];
        const double L = jsHypot2(b.x - a.x, b.y - a.y), bow = -L * r.range(.05, .15);
        return lightning::bowed(r, a.x, a.y, b.x, b.y, bow, .2, .08);
    }
    void update(double dt) override {
        t += dt;
        if (t < WIND && R.isChance(70 * dt)) {
            const double a = R.range(0, TAU), d = R.range(10, 24), T = R.range(.06, .1),
                         v = (d * 3) / (std::exp(3 * T) - 1);
            emit(ps, {ParticleKind::Streak, HAND.x + std::cos(a) * d, HAND.y + std::sin(a) * d, -std::cos(a) * v,
                      -std::sin(a) * v, T, R.isChance(.5) ? C[6] : C[5]});
        }
        for (size_t i = 0; i < HOPS.size(); ++i) {
            const auto v = HOPS[i];
            if (!hit[i] && t >= on(i)) {
                hit[i]            = true;
                const auto   prev = pts[i];
                const double dir  = std::atan2(v.y - prev.y, v.x - prev.x);
                lights.add(v.x, v.y, 56, C[4], .85, .22);
                lights.add(v.x, v.y, 16, WHITE, .7, .08);
                if (i == 0) lights.add(HAND.x, HAND.y, 40, C[4], .6, .15);
                for (int j = 0; j < 18; ++j) {
                    const double a = R.isChance(.6) ? dir + R.range(-1.1, 1.1) : R.range(0, TAU), sp = R.range(70, 230);
                    emit(ps,
                         {ParticleKind::Spark, v.x, v.y, std::cos(a) * sp, std::sin(a) * sp - 40, R.range(.15, .4)});
                }
                for (int j = 0; j < 5; ++j)
                    emit(hot, {ParticleKind::Spark, v.x + R.range(-2, 2), v.y + 2, R.range(-50, 50), R.range(-60, 20),
                               R.range(.5, .9)});
                emit(ps, {ParticleKind::Star, v.x + R.range(-4, 4), v.y - R.range(3, 7), 0, -6, R.range(.25, .4), C[6],
                          R.range(1.5, 2.5)});
                for (int j = 0; j < 2; ++j)
                    emit(ps, {ParticleKind::Smoke, v.x + R.range(-4, 4), v.y + R.range(-2, 3), R.range(-6, 6),
                              -R.range(8, 16), R.range(.7, 1), mix(hex(0x5b5566), C[2], .35), R.range(2, 3.5),
                              R.range(.1, .25)});
            }
            if (hit[i] && t < off(i)) {
                if (R.isChance(9 * dt)) {
                    const double u = R.next();
                    const auto   a = pts[i];
                    emit(ps, {ParticleKind::Spark, lerp(a.x, v.x, u), lerp(a.y, v.y, u) - 2, R.range(-60, 60),
                              R.range(-90, 20), R.range(.1, .25)});
                }
                if (R.isChance(7 * dt))
                    emit(ps,
                         {ParticleKind::Spark, ground[i], -1, R.range(-70, 70), -R.range(40, 140), R.range(.12, .3)});
                if (R.isChance(2 * dt))
                    emit(hot, {ParticleKind::Spark, v.x + R.range(-3, 3), v.y + 3, R.range(-30, 30), R.range(-30, 10),
                               R.range(.5, .8)});
            }
            if (!popped[i] && t >= off(i) + BREAK) {
                popped[i] = true;
                for (int j = 0; j < 7; ++j) {
                    const double a = R.range(0, TAU), sp = R.range(40, 140);
                    emit(ps,
                         {ParticleKind::Spark, v.x, v.y, std::cos(a) * sp, std::sin(a) * sp - 30, R.range(.12, .3)});
                }
                emit(ps, {ParticleKind::Smoke, v.x, v.y, R.range(-4, 4), -R.range(10, 16), R.range(.55, .7),
                          mix(hex(0x5b5566), C[2], .35), R.range(2.5, 3.5)});
                lights.add(v.x, v.y, 30, C[4], .5, .12);
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        hot.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        for (size_t i = 0; i < HOPS.size(); ++i) {
            const double a = t - on(i);
            if (a < 0) continue;
            const double T0 = std::min(1., a / .2) * std::pow(clamp(1 - (t - off(i)) / .7), 1.3) * .9;
            lightning::scorch(p, ground[i], 0, 7, T0, 1 - smooth(1.35, 1.75, t));
        }
        ps.draw(p, Paint::Layer::Back);
        hot.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const uint32_t f = jsUint(std::floor(t * 30 + 1e-4)), f2 = f >> 1;
        const double   kh = t < WIND ? t / WIND : level(0, t);
        if (kh > .02) {
            const double r = (1.2 + 1.6 * kh + std::sin(t * 53) * .4) * std::min(1., kh * 1.5);
            p.circle(HAND.x, HAND.y, r + 1.2, C[4]);
            p.circle(HAND.x, HAND.y, r, C[6]);
            p.circle(HAND.x, HAND.y, r * .5, WHITE);
            p.glow(HAND.x, HAND.y, 18, C[4], .6 * kh);
            if (t > WIND * .5) {
                Rng          r2(hash({seed, 5, f}));
                const double a = r2.range(0, TAU), d = r2.range(4, 8);
                const auto   arc =
                    lightning::zigzag(r2, HAND.x, HAND.y, HAND.x + std::cos(a) * d, HAND.y + std::sin(a) * d, .4, .3);
                lightning::crackle(p, arc, .6 * kh, C, .3);
            }
        }
        for (size_t i = 0; i < HOPS.size(); ++i) {
            const auto   v  = HOPS[i];
            const double t0 = WIND + double(i) * GAP;
            if (t >= t0 && t < on(i)) {
                const double      u = ease::outQuad((t - t0) / LEAP);
                auto              b = shape(i, 0xffffffffu);
                lightning::Stroke options;
                options.upTo = u;
                lightning::stroke(p, {b[0]}, 1, C, .75, options);
                const auto& tp = b[0].pts;
                const auto  h  = tp[std::min(tp.size() - 1, size_t(std::floor(u * double(tp.size() - 1))))];
                p.circle(h.x, h.y, 2, WHITE);
                p.glow(h.x, h.y, 16, C[5], .7);
            }
            const double k = level(i, t);
            if (k > 0)
                lightning::stroke(p, shape(i, f2), k, C, .75, {});
            else if (t >= off(i) + BREAK && t < off(i) + BREAK + .35) {
                const double g = 1 - (t - off(i) - BREAK) / .35;
                const auto   b = shape(i, jsUint(std::floor((off(i) + BREAK) * 15)));
                p.polyline(b[0].pts, g > .5 ? C[4] : C[2], g * .9);
            }
            const double a = t - on(i);
            if (a >= 0 && a < .09) {
                const double q = a / .09;
                p.circle(v.x, v.y, 6 * (1 - q) + 1.5, C[5]);
                p.circle(v.x, v.y, 5 * (1 - q), WHITE);
                p.sparkle(v.x, v.y, jsRound(10 * (1 - q)), C[6], C[4], .6);
            }
            if (k > .05) {
                const double r = (2.6 + std::sin(t * 47 + double(i)) * .7) * k;
                p.circle(v.x, v.y, r + 1.5, C[2]);
                p.circle(v.x, v.y, r + .7, C[5]);
                p.circle(v.x, v.y, r * .6, WHITE);
                p.glow(v.x, v.y, 26, C[4], .6 * k);
                p.glow(v.x, v.y, 8, C[6], .5 * k);
                Rng r2(hash({seed, 11, uint32_t(i), f}));
                for (int j = 0; j < (t - on(i) < .12 ? 2 : 1); ++j) {
                    const double a0 = r2.range(0, TAU), a1 = a0 + r2.signedRange(.5, 1), d0 = r2.range(4, 6),
                                 d1       = r2.range(5, 8);
                    const auto   arc      = lightning::zigzag(r2, v.x + std::cos(a0) * d0, v.y + std::sin(a0) * d0,
                                                              v.x + std::cos(a1) * d1, v.y + std::sin(a1) * d1, .35, 0);
                    const double strength = r2.range(.45, .7) * k;
                    lightning::crackle(p, arc, strength, C, .25);
                }
                if (r2.next() < .3 * k) {
                    const double gx = ground[i] + r2.range(-3, 3), x0 = v.x + r2.range(-2, 2);
                    const auto   arc = lightning::zigzag(r2, x0, v.y + 3, gx, -.5, .3, .35);
                    lightning::crackle(p, arc, .75 * k, C, .35);
                    p.glow(gx, -1, 12, C[5], .5 * k);
                    p.px(gx, -1, WHITE);
                }
            }
        }
        ps.draw(p, Paint::Layer::Front);
        hot.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& chainLightningDescriptor() {
    static const std::array<Option, 2> options{{{"sparks", 1, 64, false, 4, &PixelFxLightningSettings::sparks},
                                                {"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::ChainLightning, "lightning/chain-lightning", 248, 104, 40, 82, 1.8, false},
        8,
        [](PixelFxSettings& s) {
            s.seed = 3;
            s.palette.assign(lightning::VOLT.begin(), lightning::VOLT.end());
            s.lightning.sparks.assign(lightning::PLASMA.begin(), lightning::PLASMA.end());
            s.actor.source = PixelFxActorSource::Dummy;
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<ChainLightning>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
