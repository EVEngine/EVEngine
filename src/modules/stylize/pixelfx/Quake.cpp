// eve-hot-path
// Native adaptation of pixelfx nature/quake.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/NatureGeometry.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr std::array<double, 3> JOLTS{{.06, .5, .92}};
constexpr double                CLOSE = 2.2, D = 3.1;
struct Slab {
    double             x, y, vy, angle, angularVelocity;
    bool               rest;
    double             size;
    int                side;
    nature::StoneShape shape;
};
struct Rock : Debris {
    nature::StoneShape shape;
};
struct RockEmission {
    double x, y, vx, vy, gravity, drag, bounce, life, angle, angularVelocity, size;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
    ParticleFlag flags = ParticleFlag::None;
};
struct Quake final : Kernel {
    const std::vector<uint32_t> S, G;
    const double                reach;
    const uint32_t              dust, dust2;
    Rng                         R;
    Particles                   ps;
    BasicBits<Rock>             bits;
    Lights                      lights;
    std::vector<Point>          rift;
    std::vector<Slab>           slabs;
    double                      t = 0;
    size_t                      j = 0;
    explicit Quake(const PixelFxSettings& o)
        : S(o.palette),
          G(o.nature.glow),
          reach(o.nature.reach),
          dust(mix(S[6], hex(0x8c8494), .4)),
          dust2(mix(S[5], hex(0x6c6474), .35)),
          R(o.seed) {
        ps.sparks = {G[4], G[3], G[2], G[1]};
        for (double x = -reach; x <= reach; x += R.range(2.5, 5)) rift.push_back({x, double(R.integer(0, 2))});
        nature::StoneShape shape;
        shape.count     = 4;
        shape.points[0] = {-1, -.38};
        shape.points[1] = {1, -.42};
        shape.points[2] = {1.02, .4};
        shape.points[3] = {-.98, .38};
        for (double x = -reach + 8; x < reach - 6; x += R.range(12, 17))
            slabs.push_back({x + R.range(-2, 2), 0, 0, 0, 0, true, R.range(4.5, 6), R.sign(), shape});
    }
    double open() const {
        return ease::outCubic(clamp((t - JOLTS[0]) / .45)) * reach * (.55 + .45 * smooth(JOLTS[1], JOLTS[1] + .3, t));
    }
    double gap(double x) const {
        return (1 - smooth(CLOSE, CLOSE + .5, t)) * (1 + 2.6 * (1 - std::abs(x) / reach)) *
               (.6 + .4 * smooth(JOLTS[1], JOLTS[2] + .2, t));
    }
    double heat() const { return smooth(0, .3, t) * (1 - smooth(1.4, CLOSE + .3, t)) * (.8 + .2 * std::sin(t * 17)); }
    double shake() const {
        double s = 0;
        for (size_t i = 0; i < JOLTS.size(); ++i)
            if (t > JOLTS[i])
                s += std::exp(-(t - JOLTS[i]) * 9) * std::sin((t - JOLTS[i]) * 80) * (1.6 - double(i) * .3);
        return s;
    }
    void emit(const Emission& e) {
        ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay, e.flags);
    }
    void rock(const RockEmission& e) {
        Rock b;
        b.kind            = DebrisKind::Chunk;
        b.x               = e.x;
        b.y               = e.y;
        b.vx              = e.vx;
        b.vy              = e.vy;
        b.gravity         = e.gravity;
        b.drag            = e.drag;
        b.bounce          = e.bounce;
        b.life            = e.life;
        b.angle           = e.angle;
        b.angularVelocity = e.angularVelocity;
        b.size            = e.size;
        b.shape           = nature::randomStoneShape(R);
        bits.add(std::move(b));
    }
    void update(double dt) override {
        t += dt;
        const double f = std::min(1., dt * 120);
        while (j < JOLTS.size() && t >= JOLTS[j]) {
            const double k = 1 - double(j) * .2, L = open() + 10;
            ++j;
            lights.add(0, -2, 60, G[2], .5 * k, .5);
            lights.add(0, -2, 26, WHITE, .4 * k, .07);
            for (auto& s : slabs) {
                const double d = std::abs(s.x) / reach;
                if (std::abs(s.x) > L || R.isChance(.25 + d * .3)) continue;
                s.rest            = false;
                s.vy              = -R.range(70, 170) * k * (1 - d * .5);
                s.angularVelocity = s.side * R.range(3, 8);
            }
            for (int i = 0; i < 70 * k; ++i) {
                const double x = R.range(-L, L), a = -PI / 2 + R.range(-.6, .6), sp = R.range(40, 200) * k;
                emit({ParticleKind::Grit, x, -1, std::cos(a) * sp, std::sin(a) * sp, R.range(.6, 1.2),
                      R.pick<uint32_t>(std::array<uint32_t, 4>{{S[7], S[6], S[5], S[4]}}), R.isChance(.35) ? 2. : 1.});
            }
            for (int i = 0; i < 8 * k; ++i) {
                const double x = R.range(-L, L) * .8, a = -PI / 2 + R.range(-.9, .9), sp = R.range(100, 230) * k;
                rock({x, -2, std::cos(a) * sp, std::sin(a) * sp, 1, .5, .3, std::min(R.range(1.1, 1.6), D - .1 - t),
                      R.range(0, TAU), R.range(-14, 14), R.range(1.6, 3.2)});
            }
            for (int i = 0; i < 14 * k; ++i) {
                const double x = R.range(-L, L) * .9;
                emit({ParticleKind::Smoke, x, -R.range(1, 4), R.range(-40, 40), -R.range(15, 55) * k, R.range(1, 1.7),
                      R.isChance(.5) ? dust : dust2, R.range(4, 7) * k, R.range(0, .15), ParticleFlag::Big});
            }
            for (int i = 0; i < 10 * k; ++i) {
                const double x = R.range(-L, L) * .7, a = -PI / 2 + R.range(-1, 1), sp = R.range(120, 300);
                emit({ParticleKind::Spark, x, -1, std::cos(a) * sp, std::sin(a) * sp, R.range(.12, .3)});
            }
        }
        if (t > JOLTS[0] && t < CLOSE && R.isChance(.3 * f)) {
            const double x = R.range(-1, 1) * open();
            emit({ParticleKind::Grit, x, -1, R.range(-20, 20), -R.range(20, 70), R.range(.5, .8),
                  R.pick<uint32_t>(std::array<uint32_t, 2>{{S[6], S[5]}}), 1});
            if (R.isChance(.3))
                emit({ParticleKind::Ember, x, -1, R.range(-10, 10), -R.range(20, 45),
                      std::min(R.range(.6, 1.2), D - .1 - t), G[3], 1});
            if (R.isChance(.25))
                emit({ParticleKind::Dust, x, -1, R.range(-15, 15), -R.range(4, 14), R.range(.5, .8), dust2,
                      R.range(2, 3.5)});
        }
        for (auto& s : slabs) {
            if (s.rest) continue;
            s.vy += 950 * dt;
            s.y += s.vy * dt;
            s.angle += s.angularVelocity * dt;
            if (s.y >= 0 && s.vy > 0) {
                s.y     = 0;
                s.angle = clamp(s.angle, -.45, .45);
                if (s.vy > 160) {
                    s.vy *= -.25;
                    s.angularVelocity *= -.4;
                } else {
                    s.rest = true;
                    for (int i = 0; i < 3; ++i)
                        emit({ParticleKind::Dust, s.x + R.range(-5, 5), -.5, R.signedRange(10, 40), -R.range(2, 12),
                              R.range(.4, .7), dust2, R.range(1.5, 3)});
                }
            }
        }
        const auto floor = [](double) { return 0.; };
        bits.update(dt, floor);
        for (auto& b : bits.list)
            if (b.rest && !b.dusted) {
                b.dusted = true;
                emit({ParticleKind::Dust, b.x, -.5, R.signedRange(8, 30), -R.range(2, 10),
                      std::min(R.range(.3, .6), D - .1 - t), dust2, R.range(1.3, 2.4)});
            }
        ps.update(dt, R, floor, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double L = open(), hk = heat(), jx = shake(), fade = 1 - smooth(CLOSE + .3, CLOSE + .7, t);
        if (t > JOLTS[0])
            for (size_t i = 1; i < rift.size(); ++i) {
                const auto a = rift[i - 1], b = rift[i];
                if (std::abs(a.x) > L) continue;
                for (double x = std::ceil(a.x); x < b.x; ++x) {
                    const double u = (x - a.x) / (b.x - a.x), d = jsRound(a.y + (b.y - a.y) * u), w = gap(x),
                                 X = x + jx;
                    if (w < .4) {
                        p.dot(X, d, S[1], fade);
                        continue;
                    }
                    p.dot(X, d - 1, S[6], fade);
                    const double deep = rand({jsUint(std::floor(x / 3)), 41u}) + (w - 1.5) * .35,
                                 hot  = hk * clamp(deep - .35, 0, 1) * 1.6;
                    for (int y = 0; y < w; ++y)
                        p.dot(X, d + y,
                              y > w / 2 - 1 && hot > .25
                                  ? (hot > .9 && rand({jsUint(x), jsUint(std::floor(t * 12))}) < .6 ? G[3] : G[2])
                              : y > w / 2 - 1 && hot > .08 ? G[1]
                                                           : S[0],
                              fade);
                    p.dot(X, d + std::ceil(w), S[2], fade);
                }
                if (i % 3 == 0 && hk > 0)
                    p.glow(b.x + jx, b.y + 1, 9, G[2], .35 * hk * (1 - std::abs(b.x) / (reach * 1.2)));
            }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const double jx = shake(), sink = smooth(CLOSE, CLOSE + .6, t);
        for (const auto& s : slabs)
            if (!(s.rest && s.angle == 0 && s.y == 0) && sink < 1)
                nature::drawRock(p, s.x + jx, s.y - 1.5 + sink * 4, s.size, s.angle, s.shape, S, 1 - sink);
        for (const auto& b : bits.list)
            nature::drawRock(p, b.x, b.y, b.size, b.angle, b.shape, S, std::min(1., b.life / .4));
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& quakeDescriptor() {
    static const std::array<Option, 2> options{{{"glow", 1, 64, false, 5, &PixelFxNatureSettings::glow},
                                                {"reach", 0, 512, false, 0, &PixelFxNatureSettings::reach}}};
    static const Descriptor            d{
        {PixelFxKind::Quake, "nature/quake", 288, 104, 144, 88, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 12;
            s.nature.reach = 118;
            s.nature.glow  = {hex(0x4a1a0c), hex(0xa8401a), hex(0xff8a2a), hex(0xffd27a), hex(0xfff4d0)};
            s.palette      = {hex(0x0e0c14), hex(0x1a1724), hex(0x211d2d), hex(0x2b2639),
                              hex(0x3a3450), hex(0x4a4263), hex(0x6a6088), hex(0x8e86a8)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Quake>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
