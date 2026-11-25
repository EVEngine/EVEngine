// eve-hot-path
// Native adaptation of pixelfx shadow/life-steal.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ShadowSimulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double TX = 104, TY = -24, HX = 1, HY = -20;
struct Orb {
    double            x, y, vx, vy, home, size;
    std::deque<Point> trail;
    bool              done = false;
    double            at   = 0;
};
struct Stain {
    double x, t0;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct LifeSteal final : Kernel {
    const std::vector<uint32_t> C;
    const PixelFxActor          actor;
    Rng                         R;
    Particles                   ps;
    shadow::Drops               drops;
    std::vector<Stain>          stains;
    Lights                      lights;
    std::array<Orb, 7>          orbs{};
    double                      t = 0, flush = 0;
    bool                        hit = false;
    explicit LifeSteal(const PixelFxSettings& o) : C(o.palette), actor(o.actor), R(o.seed) {
        ps.sparks = {WHITE, C[7], C[6], C[5]};
        for (size_t i = 0; i < orbs.size(); ++i) {
            const double a    = -PI / 2 + (double(i) - double(orbs.size() - 1) / 2) * .4 + R.range(-.12, .12),
                         sp   = R.range(100, 170);
            const double home = .36 + double(i) * .05 + R.range(0, .04), size = R.range(1.6, 2.4);
            orbs[i] = {TX, TY, std::cos(a) * sp + 10, std::sin(a) * sp, home, size, {}, false, 0};
        }
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void update(double dt) override {
        t += dt;
        if (!hit) {
            hit = true;
            lights.add(TX, TY, 54, C[5], 1, .35);
            lights.add(TX, TY, 18, WHITE, 1, .08);
            for (int i = 0; i < 14; ++i)
                drops.add({TX + R.range(-3, 3), TY + R.range(-3, 3), R.range(-40, 90), R.range(-140, -20),
                           R.isChance(.4) ? 2. : 1.});
            for (int i = 0; i < 12; ++i) {
                const double a = R.range(0, TAU), sp = R.range(80, 170);
                emit({ParticleKind::Spark, TX, TY, std::cos(a) * sp, std::sin(a) * sp, R.range(.15, .35)});
            }
        }
        for (auto& b : orbs) {
            if (b.done) continue;
            if (t < b.home) {
                b.vx -= b.vx * 5 * dt;
                b.vy -= b.vy * 5 * dt;
            } else {
                const double dx = HX - b.x, dy = HY - b.y, root = jsHypot2(dx, dy), d = root ? root : 1,
                             pull = 2600 * std::min(1., (t - b.home) * 3);
                b.vx += (dx / d) * pull * dt;
                b.vy += (dy / d) * pull * dt;
                b.vx -= b.vx * 2.2 * dt;
                b.vy -= b.vy * 2.2 * dt;
                if (d < 6) {
                    b.done = true;
                    b.at   = t;
                    flush  = std::min(1.5, flush + .45);
                    lights.add(HX, HY, 30, C[5], .8, .25);
                    for (int i = 0; i < 6; ++i) {
                        const double a = R.range(0, TAU), sp = R.range(40, 100);
                        emit({ParticleKind::Spark, HX, HY, std::cos(a) * sp, std::sin(a) * sp - 20, R.range(.12, .3)});
                    }
                    for (int i = 0; i < 3; ++i)
                        emit({ParticleKind::Ember, HX + R.range(-8, 8), HY + R.range(-6, 14), R.range(-10, 10),
                              -R.range(15, 35), R.range(.6, 1.1), C[6]});
                }
            }
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            b.trail.push_front({b.x, b.y});
            if (b.trail.size() > 26) b.trail.pop_back();
        }
        flush -= flush * 2.6 * dt;
        lights.update(dt);
        std::erase_if(stains, [&](const auto& s) { return t - s.t0 > .8; });
        const auto floor = [](double) { return 0.; };
        drops.update(dt, floor, [&](double x, double, double, double) { stains.push_back({x, t}); });
        ps.update(dt, R, floor, [](double, double) {});
    }
    void drawBack(Paint& p) const override {
        const double k = 1 - smooth(.1, .9, t);
        if (k > 0) {
            p.disc(TX, TY, 6 + t * 10, C[2], .5 * k);
            p.glow(TX, TY, 30, C[4], .35 * k);
        }
        for (const auto& s : stains) p.dot(s.x, -.5, C[3], 1 - (t - s.t0) / .8);
    }
    void draw(Paint& p) const override {
        const double fk = std::min(1., flush) * (1 - smooth(1.5, 2.05, t));
        if (actor.source != PixelFxActorSource::None && fk > .03) {
            for (int my = 0; my < actor.height; ++my)
                for (int mx = 0; mx < actor.width; ++mx) {
                    const auto c0 = actor.pixels[size_t(my * actor.width + mx)];
                    if (!c0) continue;
                    const double l = luma(c0);
                    p.blend(actor.x + mx, actor.y + my, l > .6 ? C[6] : l > .4 ? C[5] : l > .2 ? C[4] : C[3], .45 * fk);
                }
            if (fk > .12)
                p.outline({actor.width, actor.height, actor.mask}, actor.x, actor.y,
                          fk > .6   ? C[6]
                          : fk > .3 ? C[5]
                                    : C[3]);
            p.glow(HX, HY, 28, C[5], .45 * fk);
        }
        if (t < .12) p.sparkle(TX, TY, 10 * (1 - t / .12), C[7], C[5], .6);
        const double ru = t / .28;
        if (ru < 1)
            shadow::ringFast(p, TX, TY, 4 + 16 * ease::outCubic(ru), std::max(1., 2 * (1 - ru)), ru < .4 ? C[7] : C[5]);
        drops.draw(p, C[3], C[6]);
        for (const auto& b : orbs) {
            if (b.done && t - b.at > .2) continue;
            const int n =
                b.done ? int(std::floor(double(b.trail.size()) * (1 - (t - b.at) / .2))) : int(b.trail.size());
            for (int i = n - 1; i >= 1; --i) {
                const double u = double(i) / 25;
                const auto   a = b.trail[size_t(i - 1)], z = b.trail[size_t(i)];
                p.line(z.x, z.y, a.x, a.y, u < .15 ? C[7] : u < .35 ? C[6] : u < .6 ? C[5] : u < .8 ? C[4] : C[2]);
                if (i < 7) p.disc(z.x, z.y, (b.size + .4) * (1 - double(i) / 7), u < .12 ? C[6] : C[5], .8);
                if (i % 6 == 3) p.glow(z.x, z.y, 6, C[5], .2 * (1 - u));
            }
            if (b.done) continue;
            const double pulse = t < b.home ? .8 + .2 * std::sin(t * 30 + b.size * 9) : 1;
            p.circle(b.x, b.y, b.size + .9, C[4]);
            p.circle(b.x, b.y, b.size * pulse, C[6]);
            p.px(b.x - .5, b.y - .5, WHITE);
            p.glow(b.x, b.y, 9, C[5], .5);
        }
        ps.draw(p);
        for (const auto& l : lights.list) {
            const double q = l.life / l.max;
            p.glow(l.x, l.y, l.radius * (.6 + .4 * q), l.color, l.strength * q * q);
        }
    }
};
}  // namespace
const Descriptor& lifeStealDescriptor() {
    static const std::array<Option, 2> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
         {"dark", 4278190080., 4294967295., false, 0, &PixelFxShadowSettings::dark, true}}};
    static const Descriptor d{
        {PixelFxKind::LifeSteal, "shadow/life-steal", 184, 100, 36, 92, 2.2, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 11;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x14040a), hex(0x3a0a14), hex(0x6a101c), hex(0xa01824),
                              hex(0xd42a30), hex(0xff5a50), hex(0xff9a8a), hex(0xffe6de)};
            s.shadow.dark  = hex(0x2a1040);
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<LifeSteal>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
