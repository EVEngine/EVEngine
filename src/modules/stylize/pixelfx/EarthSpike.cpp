// eve-hot-path
// Native adaptation of pixelfx nature/earth-spike.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/NatureSpikeGeometry.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double UP = .34, RISE = .09, CRACK = 1.18, SPLIT = 1.5, PART = .1, FALL = .42, SINK = .55, D = 3.4;
struct Spike {
    double                            x, height, width, lean, at, split;
    bool                              back = false, main = false;
    nature::SpikeLook                 look;
    std::array<std::vector<Point>, 3> cracks;
    bool                              up = false, split0 = false;
    double                            cut = 0, top = 1;
};
struct Slab {
    int                side;
    double             x;
    nature::StoneShape shape;
    double             size, angle;
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
struct CrackEmission {
    double u, v;
    int    direction;
};
struct EarthSpike final : Kernel {
    const std::vector<uint32_t>       S, G;
    const double                      H;
    const uint32_t                    dust, dust2;
    Rng                               R;
    Particles                         ps;
    BasicBits<Rock>                   bits;
    Lights                            lights;
    std::array<Spike, 3>              spikes;
    std::array<std::vector<Point>, 4> cracks;
    std::array<Slab, 2>               slabs;
    double                            t = 0;
    std::vector<Point> crack(const CrackEmission& e) { return nature::spikeCrack(R, e.u, e.v, e.direction); }
    explicit EarthSpike(const PixelFxSettings& o)
        : S(o.palette),
          G(o.nature.glow),
          H(o.nature.height),
          dust(mix(S[6], hex(0x8c8494), .45)),
          dust2(mix(S[5], hex(0x6c6474), .4)),
          R(o.seed) {
        ps.sparks = {G[4], G[3], G[2], G[1]};
        spikes    = {{{-H * .17, H * .52, H * .22, -H * R.range(.13, .18), UP + .045, SPLIT - .1, true},
                      {H * .18, H * .4, H * .2, H * R.range(.11, .15), UP + .085, SPLIT + .08},
                      {0, H, H * .36, R.range(-2, 2), UP, SPLIT, false, true}}};
        for (auto& s : spikes) {
            s.look   = nature::spikeLook(R);
            s.cracks = {crack({R.range(.04, .16), R.range(-.6, -.1), 1}),
                        crack({R.range(.22, .4), R.range(.1, .6), -1}),
                        crack({R.range(.42, .58), R.range(-.4, .4), R.sign()})};
            s.cut    = R.range(.24, .32);
        }
        cracks   = {nature::floorCrack(R, -1, R.range(34, 44)), nature::floorCrack(R, 1, R.range(34, 44)),
                    nature::floorCrack(R, -1, R.range(14, 20)), nature::floorCrack(R, 1, R.range(14, 20))};
        size_t k = 0;
        for (int sd : {-1, 1}) {
            const double       x = sd * R.range(13, 16);
            nature::StoneShape shape;
            shape.count     = 4;
            shape.points[0] = {-1.2, -.32};
            shape.points[1] = {1.1, -.42};
            shape.points[2] = {1.2, .36};
            shape.points[3] = {-1.1, .4};
            slabs[k++]      = {sd, x, shape, R.range(5.5, 7), sd * R.range(.45, .65)};
        }
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
    double grown(const Spike& s) const { return t < s.at ? 0 : ease::outBack(clamp((t - s.at) / RISE), 2.2); }
    double open() const { return ease::outCubic(clamp((t - .02) / .24)); }
    double heat() const { return t < UP ? smooth(0, UP, t) : 1 - smooth(UP, UP + .9, t); }
    double gap(const Spike& s) const { return smooth(s.split, s.split + PART, t) * (s.main ? 3 : 2); }
    double tip(const Spike& s) const { return smooth(s.split, s.split + PART + FALL, t) * .12 * s.height; }
    double topAt(const Spike& s) const { return lerp(1, s.cut, ease::inQuad(clamp((t - s.split - .04) / FALL))); }
    void   erupt(const Spike& s) {
        const double k = s.height / H;
        lights.add(s.x, -s.height * .4, 30 * k + 14, WHITE, .5 * k + .1, .1);
        lights.add(s.x, -10, 60 * k + 20, G[2], .34 * k, .5);
        for (int i = 0; i < int(jsRound(16 * k)) + 3; ++i) {
            const double a = -PI / 2 + R.range(-1.1, 1.1), sp = R.range(140, 360) * (.6 + .4 * k);
            rock({s.x + R.range(-s.width / 3, s.width / 3), -R.range(1, 6), std::cos(a) * sp, std::sin(a) * sp, 1, .35,
                  .32, R.range(1.4, 2.4), R.range(0, TAU), R.range(-14, 14), R.range(1.6, 3.8) * (.6 + .4 * k)});
        }
        for (int i = 0; i < int(jsRound(55 * k)) + 10; ++i) {
            const double u = R.range(-1, 1), a = -PI / 2 + u * .9, sp = R.range(90, 330) * (1 - .35 * std::abs(u));
            emit({ParticleKind::Grit, s.x + u * s.width * .4, -1, std::cos(a) * sp, std::sin(a) * sp, R.range(1, 2),
                  R.pick<uint32_t>(std::array<uint32_t, 5>{{S[6], S[5], S[4], S[4], S[3]}}), R.isChance(.4) ? 2. : 1.});
        }
        for (int i = 0; i < int(jsRound(9 * k)) + 2; ++i) {
            const double a = -PI / 2 + R.range(-1.2, 1.2), sp = R.range(160, 380);
            emit({ParticleKind::Spark, s.x, -R.range(2, 10), std::cos(a) * sp, std::sin(a) * sp, R.range(.15, .4)});
        }
        for (int i = 0; i < int(jsRound(10 * k)) + 3; ++i) {
            const int sd = R.sign();
            emit({ParticleKind::Smoke, s.x + sd * R.range(2, 10), -R.range(1, 4), sd * R.range(50, 170) * (.5 + .5 * k),
                  -R.range(3, 16), R.range(.9, 1.6), R.isChance(.5) ? dust : dust2, R.range(3, 5.5) * (.6 + .4 * k),
                  R.range(0, .05), ParticleFlag::Big});
        }
        for (int i = 0; i < int(jsRound(14 * k)) + 4; ++i) {
            const int sd = R.sign();
            emit({ParticleKind::Dust, s.x + sd * R.range(0, 8), -R.range(0, 3), sd * R.range(40, 160), -R.range(2, 20),
                  R.range(.5, 1), R.isChance(.5) ? dust : dust2, R.range(1.5, 3.2)});
        }
        for (int i = 0; i < int(jsRound(3 * k)) + 1; ++i)
            emit({ParticleKind::Smoke, s.x + R.range(-6, 6), -R.range(2, 12), R.range(-20, 20), -R.range(15, 45),
                  R.range(1, 1.6), dust, R.range(4, 6) * k + 2, R.range(.02, .12), ParticleFlag::Big});
        if (s.main) {
            for (int i = 0; i < 3; ++i)
                emit({ParticleKind::Star, s.x + R.range(-5, 5), -R.range(10, s.height * .9), 0, -3, R.range(.25, .5),
                      S[7], R.range(1, 2.2), R.range(.1, .4)});
            for (int i = 0; i < 14; ++i)
                emit({ParticleKind::Grit, R.range(-60, 60), -R.range(110, 125), R.range(-15, 15), R.range(10, 60),
                      R.range(1.2, 1.8), R.pick<uint32_t>(std::array<uint32_t, 3>{{S[5], S[4], S[3]}}),
                      R.isChance(.35) ? 2. : 1., R.range(.25, .9)});
        }
    }
    void crumble(const Spike& s, double u0, double u1) {
        const double g = gap(s), lean = s.lean, tp = tip(s);
        for (int half : {-1, 1}) {
            const double u = (u0 + u1) / 2, hw = (s.width / 2) * std::pow(1 - u, .85),
                         x = s.x + lean * u + half * (g / 2 + hw * .45 + tp * u), y = -u * s.height;
            if (hw < .8 && half > 0) continue;
            rock({x, y, half * R.range(8, 40) + lean * .6, -R.range(0, 50), 1, .3, .3,
                  std::min(R.range(1.1, 1.6), D - .15 - t), R.range(0, TAU), R.range(-8, 8),
                  clamp(hw * .62, 1.6, 4.8)});
            for (int i = 0; i < 3; ++i)
                emit({ParticleKind::Grit, x + R.range(-2, 2), y + R.range(-2, 2), half * R.range(5, 45),
                      -R.range(10, 70), R.range(.9, 1.6),
                      R.pick<uint32_t>(std::array<uint32_t, 4>{{S[6], S[5], S[4], S[3]}}), R.isChance(.35) ? 2. : 1.});
        }
        if (R.isChance(.5))
            emit({ParticleKind::Dust, s.x + lean * u1 + R.range(-4, 4), -u1 * s.height, R.signedRange(15, 45),
                  R.range(20, 60), R.range(.35, .6), dust, R.range(2, 3.5)});
    }
    void drawShard(Paint& p, const Spike& s) const {
        const double g = grown(s);
        if (g <= 0) return;
        nature::SpikeDrawOptions o;
        o.cracks = s.cracks;
        o.glow   = G;
        if (t < s.split) {
            o.white         = t - s.at < .04 || t > s.split - .045;
            o.crack         = smooth(CRACK + (s.main ? 0 : .05), s.split - .03, t);
            o.heat          = smooth(CRACK, s.split, t);
            const double sw = (t - s.at) * .9 - .15;
            o.glint         = sw > 0 && std::fmod(sw, 1.2) < .55 ? std::fmod(sw, 1.2) / .55 : -1;
            nature::drawSpike(p, s.x, 0, s.height * g, s.width, s.lean * std::min(1., g), s.look, S, o);
            return;
        }
        const double top = topAt(s), gp = gap(s), tp = tip(s),
                     sink = smooth(s.split + FALL, s.split + FALL + SINK, t) * s.height * (s.cut + .06);
        if (sink >= s.height * (s.cut + .05)) return;
        o.clip  = top;
        o.floor = 0;
        o.crack = 1;
        o.heat  = 1 - smooth(s.split, s.split + .45, t);
        for (int half : {-1, 1}) {
            o.half = half;
            nature::drawSpike(p, s.x + (half * gp) / 2, sink, s.height, s.width, s.lean + half * tp, s.look, S, o);
        }
    }
    void update(double dt) override {
        t += dt;
        const double f = std::min(1., dt * 120);
        if (t < UP) {
            const double L = open();
            for (const auto& c : cracks)
                if (R.isChance(.35 * f)) {
                    const auto q = c[std::min(c.size() - 1, size_t(std::floor(R.range(0, L) * double(c.size()))))];
                    emit({ParticleKind::Grit, q.x, -1, R.range(-20, 20), -R.range(30, 110), R.range(.5, 1),
                          R.pick<uint32_t>(std::array<uint32_t, 2>{{S[5], S[4]}}), 1});
                    if (R.isChance(.25))
                        emit({ParticleKind::Dust, q.x, -1, R.range(-15, 15), -R.range(4, 14), R.range(.3, .6), dust2,
                              R.range(1.5, 3)});
                }
            if (t > .12 && R.isChance(.2 * f))
                emit({ParticleKind::Dust, R.signedRange(10, 16), -R.range(1, 4), R.signedRange(10, 30), -R.range(4, 12),
                      R.range(.4, .7), dust2, R.range(2, 3.5)});
        }
        for (auto& s : spikes) {
            if (!s.up && t >= s.at) {
                s.up = true;
                erupt(s);
            }
            if (!s.split0 && t >= s.split) {
                s.split0 = true;
                lights.add(s.x, -s.height * .5, 30, WHITE, .3, .08);
                emit({ParticleKind::Star, s.x + s.lean * .5, -s.height * .55, 0, -2, .3, S[7], 2.5});
            }
            if (t >= s.split) {
                const double top = topAt(s);
                while (s.top - top > .045 || (top <= s.cut + 1e-6 && s.top > s.cut + 1e-6)) {
                    const double u1 = s.top, u0 = std::max(top, s.top - .11);
                    crumble(s, u0, u1);
                    s.top = u0;
                    if (u0 <= s.cut + 1e-6) break;
                }
            }
            if (t < s.split && t > CRACK && R.isChance(.12 * f)) {
                const double u = R.range(.1, .7);
                emit({ParticleKind::Grit, s.x + s.lean * u + R.range(-2, 2), -u * s.height, R.range(-15, 15),
                      -R.range(0, 20), R.range(.6, 1.1), R.pick<uint32_t>(std::array<uint32_t, 2>{{S[5], S[4]}}), 1});
                if (R.isChance(.3))
                    emit({ParticleKind::Dust, s.x + s.lean * u, -u * s.height, R.range(-10, 10), -R.range(2, 8),
                          R.range(.4, .7), dust, R.range(1.5, 2.5)});
            }
            if (s.up && t < s.split && t > s.at + .2 && R.isChance(.05 * f)) {
                const double u  = R.range(.2, .8);
                const int    sd = R.sign();
                emit({ParticleKind::Grit, s.x + s.lean * u + sd * (s.width / 2) * (1 - u), -u * s.height,
                      sd * R.range(10, 30), -R.range(0, 15), R.range(.7, 1.2),
                      R.pick<uint32_t>(std::array<uint32_t, 2>{{S[6], S[5]}}), 1});
            }
            if (s.main && s.up && t < s.split && t > s.at + .25 && R.isChance(.03 * f))
                emit({ParticleKind::Star, s.x + R.range(-4, 2), -R.range(8, s.height * .85), 0, -2, R.range(.25, .45),
                      R.isChance(.5) ? S[7] : G[3], R.range(.8, 2)});
            if (t > s.split + FALL && t < s.split + FALL + SINK * .8 && R.isChance(.15 * f))
                emit({ParticleKind::Dust, s.x + R.range(-s.width / 2, s.width / 2), -R.range(0, 2),
                      R.signedRange(20, 60), -R.range(0, 5), R.range(.4, .7), dust2, R.range(1.5, 3)});
        }
        const auto floor = [](double) { return 0.; };
        bits.update(dt, floor);
        for (auto& b : bits.list)
            if (b.rest && !b.dusted) {
                b.dusted = true;
                for (int i = 0; i < 2; ++i)
                    emit({ParticleKind::Dust, b.x + R.range(-1.5, 1.5), -.5, R.signedRange(8, 35), -R.range(2, 14),
                          R.range(.3, .6), dust2, R.range(1.3, 2.6)});
            }
        ps.update(dt, R, floor, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double L = open(), hk = heat(), fade = 1 - smooth(D - 1.1, D - .3, t);
        for (const auto& c : cracks) {
            const size_t m = size_t(std::max(2., std::ceil(double(c.size()) * L)));
            for (size_t k = 1; k < m; ++k) {
                const auto a = c[k - 1], b = c[k];
                p.line(a.x, a.y + 1, b.x, b.y + 1, S[0], fade);
                p.line(a.x, a.y, b.x, b.y,
                       hk > .65   ? G[4]
                       : hk > .35 ? G[3]
                       : hk > .12 ? G[2]
                       : hk > .03 ? G[1]
                                  : S[1],
                       fade);
                if (k % 2 == 0) p.glow(b.x, b.y, 6, G[2], .3 * hk);
            }
        }
        if (hk > 0) p.glow(0, -3, 26, G[2], .35 * hk);
        ps.draw(p, Paint::Layer::Back);
        for (const auto& s : spikes)
            if (s.back) drawShard(p, s);
    }
    void draw(Paint& p) const override {
        if (t < UP + .02) {
            const double k = smooth(.06, UP, t), hm = 8 * k, jx = std::sin(t * 95) * .8 * k;
            if (hm > .6)
                p.shader(-16, -hm - 1, 16, 1, [&](double x, double y, ShadeOutput& out) {
                    const double dx = (x - jx) / 15, dy = y / hm, d = dx * dx + dy * dy;
                    if (y > .5 || d > 1.18) return;
                    out.c = d > 1 ? S[0] : dy < -.72 ? S[6] : dx < -.3 ? S[5] : dx > .35 ? S[3] : S[4];
                });
        }
        if (t >= UP) {
            const double sink = smooth(SPLIT + FALL, SPLIT + FALL + SINK, t) * 7,
                         a    = 1 - smooth(SPLIT + FALL, SPLIT + FALL + SINK, t);
            for (const auto& sl : slabs) {
                const double g = ease::outBack(clamp((t - UP) / .1), 2);
                if (a > 0)
                    nature::drawRock(p, sl.x, -1.5 + sink, sl.size * std::min(1., g), sl.angle * g, sl.shape, S, a,
                                     t - UP < .04);
            }
        }
        for (const auto& s : spikes)
            if (!s.back) drawShard(p, s);
        const auto&  m    = spikes[2];
        const double leak = smooth(CRACK, SPLIT, t) * (1 - smooth(SPLIT, SPLIT + .5, t));
        if (leak > 0) p.glowLine(m.x, -4, m.x + m.lean * .6, -m.height * .6, 20, G[2], .22 * leak);
        for (const auto& b : bits.list)
            nature::drawRock(p, b.x, b.y, b.size, b.angle, b.shape, S, std::min(1., b.life / .4));
        ps.draw(p, Paint::Layer::Front);
        const double e = t - m.at;
        if (e > 0 && e < .2) {
            const double g = ease::outBack(clamp(e / RISE), 2.2);
            p.sparkle(m.x + m.lean, -m.height * g, 10 * (1 - e / .2), S[7], G[3], .5);
            p.glowLine(m.x, 0, m.x + m.lean, -m.height * g, 10, G[3], .3 * (1 - e / .2));
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& earthSpikeDescriptor() {
    static const std::array<Option, 2> options{{{"glow", 1, 64, false, 5, &PixelFxNatureSettings::glow},
                                                {"height", 1, 256, false, 0, &PixelFxNatureSettings::height}}};
    static const Descriptor            d{
        {PixelFxKind::EarthSpike, "nature/earth-spike", 272, 150, 136, 132, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed          = 5;
            s.nature.height = 62;
            s.nature.glow   = {hex(0x4a1a0c), hex(0xa8401a), hex(0xff8a2a), hex(0xffd27a), hex(0xfff4d0)};
            s.palette       = {hex(0x120c0c), hex(0x251917), hex(0x3b2a24), hex(0x553d31),
                               hex(0x715440), hex(0x8f6e52), hex(0xb8966e), hex(0xe6d0a8)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<EarthSpike>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
