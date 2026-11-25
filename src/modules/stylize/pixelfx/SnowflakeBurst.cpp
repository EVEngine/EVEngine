// eve-hot-path
// Native adaptation of pixelfx ice/snowflake-burst.js and ice/_lib.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Ice.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double POP = .16, CY = -48, D = 3.2;
struct Flake {
    double           x = 0, y = CY, vx, vy, r, rot, w0, w1;
    ice::FlakeDesign design;
    double           life, age = 0, ph, tw, tw0;
    bool             rest = false, popped = false;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color;
    double       size = 1, delay = 0;
};
struct SnowflakeBurst final : Kernel {
    std::vector<uint32_t> C;
    Rng                   R;
    Particles             ps;
    Lights                lights;
    std::vector<Flake>    flakes;
    double                t      = 0;
    bool                  popped = false;
    explicit SnowflakeBurst(const PixelFxSettings& o) : C(o.palette), R(o.seed) {
        ps.sparks = {C[7], C[6], C[5], C[4]};
        ps.haze   = C[5];
        constexpr std::array<int, 9> SIZES{{2, 0, 1, 0, 2, 0, 1, 0, 1}};
        flakes.reserve(size_t(o.ice.count));
        for (int i = 0; i < o.ice.count; ++i) {
            const int    z  = SIZES[size_t(i) % SIZES.size()];
            const double r  = z == 2   ? R.range(9.5, 11.5)
                              : z == 1 ? R.range(6, 7.5)
                                       : R.range(3, 4.5),
                         a  = (double(i) / o.ice.count) * TAU + R.range(-.3, .3) + .4,
                         sp = R.range(80, 120) * (1.4 - r / 14), rot = R.range(0, TAU), w0 = R.signedRange(7, 12),
                         w1     = R.signedRange(.4, 1);
            const auto   design = ice::flakeDesign(R);
            const double life = R.range(2.1, 2.8), ph = R.range(0, TAU), tw = R.range(.5, .85), tw0 = R.range(.15, .6);
            flakes.push_back(
                {0, CY, std::cos(a) * sp, std::sin(a) * sp * .85 - 20, r, rot, w0, w1, design, life, 0, ph, tw, tw0});
        }
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    static double size(const Flake& f) {
        return f.r * std::min(1., ease::outBack(clamp(f.age / .18), 2)) * std::min(1., (f.life - f.age) / .35);
    }
    void update(double dt) override {
        t += dt;
        if (t < POP - .03 && R.isChance(.8)) {
            const double a = R.range(0, TAU), dist = R.range(22, 44), T = R.range(.12, .16),
                         v = (dist * 3) / (std::exp(3 * T) - 1);
            emit({ParticleKind::Streak, std::cos(a) * dist, CY + std::sin(a) * dist, -std::cos(a) * v, -std::sin(a) * v,
                  T, R.isChance(.5) ? C[6] : C[5]});
        }
        if (!popped && t >= POP) {
            popped = true;
            lights.add(0, CY, 44, WHITE, .8, .1);
            lights.add(0, CY, 72, C[4], .5, .6);
            for (int i = 0; i < 22; ++i) {
                const double a = R.range(0, TAU), sp = R.range(80, 150);
                emit({ParticleKind::Mote, 0, CY, std::cos(a) * sp, std::sin(a) * sp, R.range(.25, .5),
                      R.isChance(.5) ? C[6] : C[5], R.range(.6, 1)});
            }
            for (int i = 0; i < 18; ++i) {
                const double a = R.range(-PI, .2), sp = R.range(30, 110);
                emit({ParticleKind::Flake, 0, CY, std::cos(a) * sp, std::sin(a) * sp, R.range(1, 1.6),
                      R.isChance(.6) ? WHITE : C[6], R.range(.5, 1.5)});
            }
            for (int i = 0; i < 12; ++i) {
                const double a = R.range(0, TAU), d = R.range(10, 44);
                emit({ParticleKind::Star, std::cos(a) * d, CY + std::sin(a) * d * .85, 0, R.range(2, 6),
                      R.range(.3, .6), R.isChance(.5) ? WHITE : C[6], R.range(.8, 2.2), R.range(.08, 1.6)});
            }
        }
        if (popped)
            for (auto& f : flakes) {
                f.age += dt;
                if (f.age >= f.life) continue;
                if (!f.rest) {
                    f.vx -= f.vx * 2.8 * dt;
                    f.vy -= f.vy * 2.8 * dt - 26 * dt;
                    f.x += f.vx * dt + std::sin(t * 1.8 + f.ph) * 5 * dt;
                    f.y += f.vy * dt;
                    f.rot += (f.w1 + (f.w0 - f.w1) * std::exp(-f.age * 2.5)) * dt;
                    if (f.y > -size(f) * .3) {
                        f.rest = true;
                        f.y    = -1;
                        f.life = std::min(f.life, f.age + .6);
                    }
                }
                if (!f.popped && f.life - f.age < .1) {
                    f.popped = true;
                    for (int i = 0; i < 2; ++i)
                        emit({ParticleKind::Star, f.x + R.range(-2, 2), f.y + R.range(-2, 2), R.range(-6, 6),
                              -R.range(2, 8), R.range(.2, .4), R.isChance(.5) ? WHITE : C[6], R.range(.8, 1.6)});
                }
            }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
        const double left = std::max(.001, D - .05 - t);
        for (auto& q : ps.list)
            if (q.life > left) q.life = left;
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        if (t < POP) {
            const double k = t / POP;
            ice::snowflake(p, 0, CY, 1 + 3.2 * k * k, t * 20, flakes.front().design, C, k);
            p.glow(0, CY, 8 + 14 * k, C[5], .3 + .4 * k);
        }
        const double q = t - POP;
        if (q >= 0 && q < .4) {
            if (q < .07) p.circle(0, CY, 6 * (1 - q / .07) + 1, WHITE);
            flare(p, 0, CY, 28 * std::pow(std::max(0., 1 - q / .26), 1.5), 1 - q / .26, C);
            const double e = ease::outCubic(q / .4), r = 5 + 34 * e;
            p.ring(0, CY, r, std::max(1., 2.4 * (1 - e)), q < .08 ? WHITE : q < .2 ? C[6] : C[5], 1.15 - e);
            p.glowRing(0, CY, r, 5, C[5], .4 * (1 - e));
        }
        if (popped) p.glow(0, CY, 46, C[4], .3 * (1 - smooth(POP, POP + 1.6, t)));
        for (const auto& f : flakes) {
            if (!popped || f.age >= f.life) continue;
            const double r = size(f), tw = std::pow(std::max(0., std::sin(((f.age - f.tw0) / f.tw) * TAU)), 8) *
                                           (f.age > f.tw0 ? 1 : 0);
            if (f.rest) {
                const int L = std::max(1, int(jsRound(r * 1.2 * ((f.life - f.age) / .6))));
                for (int i = -L; i <= L; ++i) p.px(f.x + i, -1, std::abs(i) == L ? C[5] : i == 0 ? WHITE : C[6]);
                p.glow(f.x, -1, 3 + L, C[4], .3);
                continue;
            }
            ice::snowflake(p, f.x, f.y, r, f.rot, f.design, C, std::max(tw, 1 - clamp(f.age / .12)));
            p.glow(f.x, f.y, 6 + r * 1.4, C[4], .12);
            if (tw > .5) flare(p, f.x, f.y, (3 + r * .7) * tw, tw, C);
        }
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& snowflakeBurstDescriptor() {
    static const std::array<Option, 1> options{{{"count", 1, 128, false, 0, &PixelFxIceSettings::count}}};
    static const Descriptor            d{
        {PixelFxKind::SnowflakeBurst, "ice/snowflake-burst", 140, 120, 70, 106, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed      = 8;
            s.ice.count = 9;
            s.palette   = {hex(0x151a3d), hex(0x212f6b), hex(0x2c58aa), hex(0x3a8bd8),
                           hex(0x62c8f4), hex(0xa6ecff), hex(0xdcfbff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<SnowflakeBurst>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
