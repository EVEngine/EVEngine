// eve-hot-path
// Native adaptation of pixelfx lightning/charge-up.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Lightning.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                CX = 15, CY = -21;
constexpr std::array<double, 6> BK{{1, .7, .95, .5, .8, .35}};
struct Bolt {
    double x, y, t0;
};
struct Arc {
    double   x, y, t;
    uint32_t seed;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
struct ChargeUp final : Kernel {
    std::vector<uint32_t> C;
    PixelFxActor          actor;
    uint32_t              seed;
    double                release, t = 0, phase = 0;
    bool                  released = false;
    Rng                   R;
    Particles             ps;
    Lights                lights;
    std::array<Bolt, 8>   bolts{};
    std::vector<Arc>      arcs;
    std::vector<double>   beats;
    std::vector<Point>    edges;
    explicit ChargeUp(const PixelFxSettings& o)
        : C(o.palette), actor(o.actor), seed(o.seed), release(o.lightning.charge), R(o.seed) {
        ps.sparks = o.lightning.sparks;
        for (size_t i = 0; i < bolts.size(); ++i) {
            const double a = (double(i) / 8) * TAU + R.range(-.25, .25), L = R.range(44, 72), x = CX + std::cos(a) * L,
                         y = std::min(0., CY + std::sin(a) * L), t0 = release + R.range(0, .05);
            bolts[i] = {x, y, t0};
        }
        const auto on = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < actor.width && y < actor.height &&
                   actor.mask[size_t(y) * size_t(actor.width) + size_t(x)] != 0;
        };
        for (int y = 0; y < actor.height; ++y)
            for (int x = 0; x < actor.width; ++x)
                if (on(x, y) && (!on(x - 1, y) || !on(x + 1, y) || !on(x, y - 1) || !on(x, y + 1)))
                    edges.push_back({actor.x + x, actor.y + y});
    }
    void     emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    double   level() const { return clamp(t / release); }
    uint32_t bright() { return std::array<uint32_t, 3>{{C[7], C[6], C[5]}}[size_t(std::floor(R.next() * 3))]; }
    void     update(double dt) override {
        t += dt;
        const double k = level(), e = k * k;
        if (t < release) {
            for (double acc = (60 + 360 * e) * dt; acc > 0; --acc) {
                if (acc < 1 && !R.isChance(acc)) break;
                const double a = R.range(0, TAU), d = R.range(30, 72) * (1 - .35 * k), life = R.range(.16, .26),
                             sp = (d / life) * .95;
                emit({ParticleKind::Mote, CX + std::cos(a) * d, std::min(-2., CY + std::sin(a) * d),
                      -std::cos(a) * sp - std::sin(a) * sp * .4, -std::sin(a) * sp + std::cos(a) * sp * .4, life,
                      bright(), 1});
            }
            if (k > .2 && R.isChance((2 + 16 * e) * dt)) {
                const double   a = R.range(0, TAU), d = R.range(20, 40);
                const uint32_t s = uint32_t(R.integer(0, 1000000));
                arcs.push_back({CX + std::cos(a) * d, std::min(-3., CY + std::sin(a) * d), t, s});
            }
            phase += (1.5 + 11 * e) * dt;
            if (phase >= 1) {
                phase -= 1;
                beats.push_back(t);
            }
            if (R.isChance(4 * e * dt))
                emit({ParticleKind::Star, CX + R.range(-14, 14), CY + R.range(-14, 10), 0, -4, R.range(.2, .35), C[6],
                      R.range(1, 2)});
        } else if (!released) {
            released = true;
            lights.add(CX, CY, 110, C[4], .8, .5);
            lights.add(CX, CY, 28, WHITE, .6, .08);
            for (int i = 0; i < 70; ++i) {
                const double a = R.range(0, TAU), sp = R.range(140, 330) * (std::sin(a) > .4 ? .55 : 1);
                emit({std::sin(a) < -.2 && R.isChance(.35) ? ParticleKind::Mote : ParticleKind::Spark, CX, CY,
                      std::cos(a) * sp, std::sin(a) * sp, R.range(.25, .55), bright(), R.range(.8, 1.4)});
            }
            for (int i = 0; i < 10; ++i) {
                const double a = R.range(0, TAU), sp = R.range(60, 160);
                emit({ParticleKind::Star, CX, CY, std::cos(a) * sp, std::sin(a) * sp, R.range(.4, .8),
                      R.isChance(.5) ? C[6] : C[5], R.range(1.2, 2.4)});
            }
            for (int i = 0; i < 4; ++i)
                emit({ParticleKind::Smoke, CX + R.range(-5, 5), CY + R.range(-4, 4), R.range(-12, 12), -R.range(8, 18),
                      R.range(.6, .85), mix(hex(0x5b5566), C[4], .3), R.range(3, 5), R.range(.05, .15)});
        }
        std::erase_if(beats, [&](double b) { return t - b >= .22; });
        std::erase_if(arcs, [&](const Arc& a) { return t - a.t > .07; });
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double k = level();
        if (t < release) p.glow(CX * .5, -3, 40, C[3], .3 * k * k);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const double   k = level(), e = k * k;
        const uint32_t f = jsUint(std::floor(t * 30 + 1e-4));
        if (t < release) {
            const double jit = k > .85 ? (f % 2 ? 1 : -1) * ((k - .85) / .15) : 0, x = CX + jit, y = CY,
                         pr = .5 + .5 * std::sin(phase * TAU);
            for (const auto v : edges) {
                const double d = jsHypot2(v.x + .5 - x, v.y + .5 - y);
                if (d < 6 + 10 * k && e > .08) p.px(v.x, v.y, d < 4 + 6 * k && e > .4 ? C[6] : C[4]);
            }
            for (double b : beats) {
                const double q = (t - b) / .22;
                p.ring(x, y, 4 + 20 * (1 - ease::inQuad(q)) * (.5 + k), 1, q > .6 ? C[6] : C[4], .4 + .6 * q);
            }
            for (const auto& a : arcs) {
                Rng        r(hash({a.seed, f}));
                const auto b = lightning::zigzag(r, a.x, a.y, x, y, .35, .4);
                lightning::crackle(p, b, .85, C, .4);
            }
            const double r = 1.2 + 6.5 * e + pr * (.5 + 1.2 * k);
            p.circle(x, y, r + 1.3, C[2]);
            p.circle(x, y, r, C[4]);
            p.circle(x - r * .12, y - r * .12, r * .7, C[5]);
            p.circle(x, y, r * .38, WHITE);
            if (r > 3)
                for (double s : {0., PI})
                    for (double a = 0; a < 1.3; a += .12) {
                        const double ang = t * 10 + s + a, rr = r * (.62 + .1 * std::sin(a * 3));
                        p.dot(x + std::cos(ang) * rr, y + std::sin(ang) * rr, a < .5 ? WHITE : C[6]);
                    }
            if (k > .35) {
                Rng          q(hash({seed, 5, f}));
                const double a = q.range(0, TAU), l = q.range(3, 6) * (.5 + k);
                const auto b = lightning::zigzag(q, x + std::cos(a) * r, y + std::sin(a) * r, x + std::cos(a) * (r + l),
                                                 y + std::sin(a) * (r + l), .4, .3);
                lightning::crackle(p, b, .7, C, .3);
            }
            p.glow(x, y, 12 + 30 * k, C[4], .35 + .45 * k);
            p.glow(x, y, 6 + 6 * k, C[6], .25 + .3 * k);
        } else {
            const double a = t - release;
            if (a < .07) p.mask({actor.width, actor.height, actor.mask}, actor.x, actor.y, a < .035 ? WHITE : C[6]);
            if (a < .22) {
                const double q = a / .22;
                flare(p, CX, CY, 46 * std::pow(1 - q, 1.4), 1 - q, C);
                if (q < .3) p.circle(CX, CY, 12 * (1 - q * 2.5) + 1.5, WHITE);
            }
            for (size_t i = 0; i < bolts.size(); ++i) {
                const auto&  b  = bolts[i];
                const double ba = std::floor((t - b.t0) * 30 + 1e-4);
                if (t < b.t0 || ba >= double(BK.size())) continue;
                const auto        j = size_t(ba);
                Rng               r(hash({seed, 3, uint32_t(i), uint32_t(j) >> 1}));
                const auto        path = lightning::zigzag(r, CX, CY, b.x, b.y, .25, .75);
                lightning::Stroke opts;
                opts.hero = ba == 0;
                lightning::stroke(p, path, BK[j], C, ba == 0 ? .95 : .75, opts);
                if (b.y > -1) p.glow(b.x, -3, 18, C[5], .5 * BK[j]);
            }
            const double q = a / .45;
            if (q < 1) {
                const double r = 8 + 80 * ease::outCubic(q);
                p.ring(CX, CY, r, std::max(1., 3 * (1 - q)), q < .25 ? WHITE : C[5],
                       [&](double, double g) { return CY + std::sin(g) * r > -1 ? 0 : 1.1 - q; });
                const double r2 = 6 + 56 * ease::outCubic(std::min(1., q * 1.3));
                p.ring(CX, CY, r2, 1, C[4], [&](double, double g) { return CY + std::sin(g) * r2 > -1 ? 0 : .8 - q; });
                p.glow(CX, CY - 6, std::min(r * .9, 46.), C[3], .42 * (1 - q) * (1 - q));
            }
            if (a > .15 && a < .5) {
                Rng          qq(hash({seed, 8, f}));
                const double ang = qq.range(0, TAU), l = qq.range(5, 10);
                const auto   path =
                    lightning::zigzag(qq, CX, CY, CX + std::cos(ang) * l, CY + std::sin(ang) * l, .45, .3);
                lightning::crackle(p, path, .8 * (1 - (a - .15) / .35) + .15, C, .3);
            }
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& chargeUpDescriptor() {
    static const std::array<Option, 3> options{{{"sparks", 1, 64, false, 4, &PixelFxLightningSettings::sparks},
                                                {"charge", .05, 8, false, 0, &PixelFxLightningSettings::charge},
                                                {"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::ChargeUp, "lightning/charge-up", 272, 184, 124, 148, 2.7, false},
        8,
        [](PixelFxSettings& s) {
            s.seed = 4;
            s.palette.assign(lightning::VOLT.begin(), lightning::VOLT.end());
            s.lightning.sparks.assign(lightning::PLASMA.begin(), lightning::PLASMA.end());
            s.lightning.charge = 1.6;
            s.actor.source     = PixelFxActorSource::Dummy;
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<ChargeUp>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
