// eve-hot-path
// Native adaptation of pixelfx lightning/thunderclap.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Lightning.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                CLAP = .12, CY = -22;
constexpr std::array<double, 6> BK{{1, .7, .95, .5, .8, .3}};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
    ParticleFlag flags = ParticleFlag::None;
};
struct Bolt {
    bool   down;
    double x, y, t0;
};
struct Thunderclap final : Kernel {
    std::vector<uint32_t> C;
    PixelFxActor          actor;
    uint32_t              seed;
    double                reach, t = 0;
    bool                  clapped = false;
    Rng                   R;
    Particles             ps, hot;
    Lights                lights;
    std::array<Bolt, 8>   bolts{};
    std::array<bool, 8>   landed{};
    explicit Thunderclap(const PixelFxSettings& o)
        : C(o.palette), actor(o.actor), seed(o.seed), reach(o.lightning.reach), R(o.seed) {
        ps.sparks = o.lightning.sparks;
        for (size_t i = 0; i < bolts.size(); ++i) {
            const bool   down = i % 2 == 0;
            const int    side = i % 4 < 2 ? -1 : 1;
            const double a    = down       ? (side > 0 ? R.range(.35, .75) : PI - R.range(.35, .75))
                                : side > 0 ? -R.range(.15, 1.2)
                                           : PI + R.range(.15, 1.2),
                         L = down ? -CY / std::sin(a) : R.range(40, 62), x = std::cos(a) * L,
                         y = down ? 0 : CY + std::sin(a) * L, t0 = CLAP + R.range(0, .06);
            bolts[i] = {down, x, y, t0};
        }
    }
    void emit(Particles& target, const Emission& e) {
        target.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay, e.flags);
    }
    double front() const { return t < CLAP ? 0 : 8 + (reach - 8) * ease::outCubic(clamp((t - CLAP) / .5)); }
    void   update(double dt) override {
        t += dt;
        if (t < CLAP && R.isChance(60 * dt)) {
            const double a = R.range(0, TAU), d = R.range(16, 30), T = R.range(.07, .1),
                         v = (d * 3) / (std::exp(3 * T) - 1);
            emit(ps, {ParticleKind::Streak, std::cos(a) * d, CY + std::sin(a) * d * .8, -std::cos(a) * v,
                      -std::sin(a) * v * .8, T, R.isChance(.5) ? C[6] : C[5]});
        }
        if (t >= CLAP && !clapped) {
            clapped = true;
            lights.add(0, CY - 10, 100, C[4], .65, .45);
            lights.add(0, CY, 26, WHITE, .5, .07);
            for (int i = 0; i < 64; ++i) {
                const double a = R.range(0, TAU), sp = R.range(170, 380) * (std::sin(a) > .4 ? .5 : 1);
                emit(ps, {ParticleKind::Spark, std::cos(a) * 6, CY + std::sin(a) * 6, std::cos(a) * sp,
                          std::sin(a) * sp - 40, R.range(.2, .55)});
            }
            for (int i = 0; i < 8; ++i)
                emit(ps, {ParticleKind::Smoke, R.range(-8, 8), CY + R.range(-6, 6), R.range(-25, 25), -R.range(10, 30),
                          R.range(.8, 1.3), mix(hex(0x5b5566), C[4], .3), R.range(4, 7), R.range(.05, .2)});
        }
        for (size_t i = 0; i < bolts.size(); ++i) {
            const auto& b = bolts[i];
            if (b.down && !landed[i] && t >= b.t0) {
                landed[i] = true;
                lights.add(b.x, -4, 46, C[4], .7, .25);
                for (int j = 0; j < 12; ++j) {
                    const double a = -PI / 2 + R.range(-1.2, 1.2), sp = R.range(70, 240);
                    emit(ps, {ParticleKind::Spark, b.x, -1, std::cos(a) * sp, std::sin(a) * sp, R.range(.2, .5)});
                }
                for (int j = 0; j < 5; ++j)
                    emit(hot, {ParticleKind::Spark, b.x + R.range(-2, 2), -1, R.range(-80, 80), -R.range(60, 180),
                               R.range(.4, .8)});
            }
        }
        const double                  fr = front();
        const std::array<uint32_t, 3> stone{{hex(0x655b7a), hex(0x4a4263), hex(0x342d45)}};
        if (t > CLAP && t < CLAP + .5)
            for (int s : {-1, 1}) {
                if (R.isChance(70 * dt))
                    emit(ps, {ParticleKind::Smoke, s * fr + R.range(-3, 3), -R.range(1, 4), s * R.range(10, 40),
                              -R.range(5, 20), R.range(.6, 1.1), hex(0x6e6382), R.range(3, 6) * (1 - .4 * (fr / reach)),
                              0, ParticleFlag::Big});
                if (R.isChance(50 * dt))
                    emit(ps, {ParticleKind::Grit, s * fr, -1, s * R.range(10, 60), -R.range(60, 180), R.range(.8, 1.3),
                              stone[size_t(std::floor(R.next() * 3))], R.isChance(.3) ? 2. : 1.});
            }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        hot.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        for (const auto& b : bolts)
            if (b.down && t >= b.t0)
                lightning::scorch(p, b.x, 0, 9, std::pow(clamp(1 - (t - b.t0) / 1.1), 1.3), 1 - smooth(1.4, 1.85, t));
        const double u = (t - CLAP) / .5;
        if (u > 0 && u < 1) {
            const std::array<uint32_t, 4> ramp{{C[1], C[2], C[4], C[6]}};
            band(
                p, 0, .5, front(), .18, std::max(2., 16 * (1 - u)), 1.25 - u, ramp,
                [](double g) { return std::sin(g) < 0; }, u < .15);
        }
        ps.draw(p, Paint::Layer::Back);
        hot.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const auto   f = jsUint(std::floor(t * 30 + 1e-4));
        const double u = (t - CLAP) / .5, a = t - CLAP;
        if (a >= 0 && a < .07)
            p.mask({actor.width, actor.height, actor.mask}, actor.x, actor.y, a < .035 ? WHITE : C[6]);
        if (a >= 0 && a < .2) {
            const double q = a / .2;
            flare(p, 0, CY, 44 * std::pow(1 - q, 1.4), 1 - q, C);
            if (q < .35) p.circle(0, CY, 10 * (1 - q * 2) + 2, WHITE);
            p.glow(0, CY, 44, C[5], .32 * (1 - q));
        }
        for (size_t i = 0; i < bolts.size(); ++i) {
            const auto&  b  = bolts[i];
            const double ba = std::floor((t - b.t0) * 30 + 1e-4);
            if (t < b.t0 || ba >= double(BK.size())) continue;
            const auto        j = size_t(ba);
            Rng               r(hash({seed, 3, uint32_t(i), uint32_t(j) >> 1}));
            const auto        paths = lightning::zigzag(r, 0, CY, b.x, b.y, .25, b.down ? .5 : .8);
            lightning::Stroke options;
            options.hero = ba == 0;
            lightning::stroke(p, paths, BK[j], C, ba == 0 ? 1 : .8, options);
            if (b.down) {
                p.circle(b.x, -1, 3 * BK[j] + 1, C[5]);
                p.px(b.x, -1, WHITE);
                p.glow(b.x, -3, 20, C[5], .5 * BK[j]);
            } else if (ba < 2)
                p.sparkle(b.x, b.y, 4, C[6], C[4], .4);
        }
        if (u > 0 && u < 1) {
            const std::array<uint32_t, 4> ramp{{C[1], C[2], C[4], C[6]}};
            band(
                p, 0, .5, front(), .18, std::max(2., 16 * (1 - u)), 1.25 - u, ramp,
                [](double g) { return std::sin(g) >= 0; }, u < .15);
            for (int s : {-1, 1}) p.glow(s * front(), -3, 28, C[4], .45 * (1 - u));
            const double ua = (t - CLAP) / .26, ra = 8 + 60 * ease::outCubic(ua);
            if (ua < 1)
                p.ring(0, CY, ra, std::max(1., 2 * (1 - ua)),
                       ua < .25  ? C[5]
                       : ua < .6 ? C[4]
                                 : C[3],
                       [&](double, double g) { return CY + std::sin(g) * ra > -1 ? 0 : .85 - .85 * ua; });
        }
        if (a > .2 && a < .9) {
            Rng          r(hash({seed, 9, f}));
            const double k = 1 - (a - .2) / .7;
            if (r.next() < .8) {
                const double x0 = r.range(-14, 14), y0 = r.range(-34, -4), b = r.range(0, TAU), l = r.range(6, 13);
                const auto   paths = lightning::zigzag(r, x0, y0, x0 + std::cos(b) * l, y0 + std::sin(b) * l, .4, .3);
                lightning::crackle(p, paths, .85 * k + .1, C, .35);
            }
        }
        ps.draw(p, Paint::Layer::Front);
        hot.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& thunderclapDescriptor() {
    static const std::array<Option, 3> options{{{"sparks", 1, 64, false, 4, &PixelFxLightningSettings::sparks},
                                                {"reach", 8, 512, false, 0, &PixelFxLightningSettings::reach},
                                                {"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::Thunderclap, "lightning/thunderclap", 288, 156, 144, 124, 1.9, false},
        8,
        [](PixelFxSettings& s) {
            s.seed = 6;
            s.palette.assign(lightning::VOLT.begin(), lightning::VOLT.end());
            s.lightning.sparks.assign(lightning::PLASMA.begin(), lightning::PLASMA.end());
            s.lightning.reach = 104;
            s.actor.source    = PixelFxActorSource::Dummy;
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Thunderclap>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
