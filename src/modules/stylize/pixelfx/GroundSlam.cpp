// eve-hot-path
// Native adaptation of pixelfx impacts/ground-slam.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/GroundImpact.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double WAVE = .6, D = 3.2;
struct Ember {
    double x, y, distance, life, phase;
};
struct GroundSlam final : Kernel {
    const std::vector<uint32_t>  C;
    const impact::Stone          stone;
    const double                 reach;
    Rng                          rng;
    Particles                    ps;
    BasicBits<impact::Rock>      bits;
    Lights                       lights;
    std::vector<impact::Crack>   cracks;
    std::array<impact::Spike, 7> burst;
    std::vector<Ember>           embers;
    uint32_t                     dustLit;
    std::array<double, 2>        accumulator;
    double                       t = 0;
    double        front(double time) const { return 7 + (reach - 7) * ease::outCubic(clamp((time - .015) / WAVE)); }
    static double heat(double time) { return std::pow(clamp(1 - time / .8), 1.4); }
    static Point  crest(double u) { return {6 + 28 * std::pow(1 - u, .75), 1.5 + 5 * std::pow(1 - u, .9)}; }
    explicit GroundSlam(const PixelFxSettings& o)
        : C(o.palette), stone(o.impact.stone), reach(o.impact.reach), rng(o.seed) {
        ps.sparks = {C[6], C[5], C[4], C[3]};
        dustLit   = mix(stone.dust, WHITE, .2);
        impact::CrackRecipe recipe;
        recipe.minLength = 22;
        recipe.maxLength = 58;
        recipe.dive      = .55;
        recipe.maxY      = 22;
        recipe.forks     = .7;
        recipe.minX      = 2;
        recipe.maxX      = 9;
        cracks           = impact::makeCracks(rng, 10, recipe);
        burst[0]         = {-.07, rng.range(34, 40), 1.9};
        burst[1]         = {PI + .07, rng.range(34, 40), 1.9};
        burst[2]         = {-.42 + rng.range(-.08, .08), rng.range(15, 20), 1.5};
        burst[3]         = {-PI + .42 + rng.range(-.08, .08), rng.range(15, 20), 1.5};
        burst[4]         = {-PI / 2 + rng.range(-.15, .15), rng.range(18, 24), 1.7};
        burst[5]         = {-1.05 + rng.range(-.1, .1), rng.range(10, 14), 1.2};
        burst[6]         = {-PI + 1.05 + rng.range(-.1, .1), rng.range(10, 14), 1.2};
        for (const auto& c : cracks)
            for (const auto& p : c.points)
                if (p.distance > 3 && p.distance < 32 && rng.isChance(.25)) {
                    const double life = rng.range(.8, 2.2), phase = rng.range(0, TAU);
                    embers.push_back({p.x, p.y, p.distance, life, phase});
                }
        lights.add(0, -12, 96, C[4], .6, .6);
        lights.add(0, -4, 50, WHITE, .9, .14);
        for (int i = 0; i < 56; ++i) {
            const bool   low  = i < 14;
            const int    side = rng.sign();
            const double a    = low ? (side > 0 ? -rng.range(.04, .6) : PI + rng.range(.04, .6))
                                    : -PI / 2 + rng.range(-1.15, 1.15),
                         v = low ? rng.range(150, 380) : rng.range(200, 500), x = rng.range(-4, 4),
                         life = low ? rng.range(.2, .5) : rng.range(.25, .65), delay = low ? 0 : rng.range(0, .05);
            ps.spawn(rng, ParticleKind::Spark, x, -1, std::cos(a) * v, std::sin(a) * v, life, WHITE, 1, delay);
        }
        for (int i = 0; i < 20; ++i) {
            const int    size = i < 3 ? 4 : rng.integer(1, 3);
            const double a    = -PI / 2 + rng.range(-.5, .5),
                         v    = size > 3 ? rng.range(110, 190) : rng.range(170, 380) * (size > 2 ? .8 : 1);
            impact::Rock b;
            b.kind            = DebrisKind::Chunk;
            b.x               = rng.range(-7, 7);
            b.y               = -2;
            b.vx              = std::cos(a) * v;
            b.vy              = std::sin(a) * v;
            b.gravity         = 1;
            b.drag            = .4;
            b.bounce          = .38;
            b.life            = rng.range(1.9, 2.7);
            b.angle           = rng.range(0, TAU);
            b.angularVelocity = rng.range(-14, 14);
            b.size            = size;
            b.points          = impact::rockShape(rng);
            bits.add(std::move(b));
        }
        const std::array<uint32_t, 6> grit{stone.light, stone.body, stone.body, stone.shade, stone.shade, stone.lip};
        for (int i = 0; i < 110; ++i) {
            const double u = rng.range(-1, 1), a = -PI / 2 + u * .65, v = rng.range(110, 380) * (1 - .35 * std::abs(u)),
                         life  = rng.range(1, 2.2);
            const auto   color = rng.pick<uint32_t>(grit);
            const double size  = rng.isChance(.35) ? 2 : 1;
            ps.spawn(rng, ParticleKind::Grit, u * 6, -1, std::cos(a) * v, std::sin(a) * v, life, color, size);
        }
        for (int i = 0; i < 28; ++i) {
            const bool   core = i < 14;
            const int    side = rng.sign();
            const double x = core ? rng.range(-5, 5) : side * rng.range(4, 12), y = -rng.range(2, 10),
                         vx = core ? rng.range(-12, 12) : side * rng.range(14, 38), up = -rng.range(50, 130),
                         vy = up * (core ? 1 : rng.range(.3, .6)), life = rng.range(1.9, 2.7);
            const auto   color = rng.isChance(.35) ? dustLit : stone.dust;
            const double size = core ? rng.range(4.5, 9) : rng.range(4, 7.5), delay = rng.range(.02, .22);
            ps.spawn(rng, ParticleKind::Smoke, x, y, vx, vy, life, color, size, delay, ParticleFlag::Big);
        }
        for (int i = 0; i < 10; ++i) {
            const int    side = rng.sign();
            const double x = side * rng.range(2, 14), y = -rng.range(2, 6), vx = side * rng.range(20, 70),
                         vy = -rng.range(4, 20), life = rng.range(1.4, 2.3), size = rng.range(6, 9),
                         delay = rng.range(0, .08);
            ps.spawn(rng, ParticleKind::Smoke, x, y, vx, vy, life, stone.dust, size, delay, ParticleFlag::Big);
        }
        for (int i = 0; i < 26; ++i) {
            const double x = rng.range(-22, 22), y = -rng.range(14, 80), vx = rng.range(-12, 12),
                         vy = rng.range(-10, 4), life = rng.range(1.2, 2);
            const auto   color = rng.isChance(.5) ? stone.lip : dustLit;
            const double size = rng.range(.5, 1.2), delay = rng.range(.25, 1);
            ps.spawn(rng, ParticleKind::Flake, x, y, vx, vy, life, color, size, delay);
        }
        accumulator = {rng.next(), rng.next()};
    }
    void ring(Paint& p, bool back) const {
        const double u = clamp((t - .015) / (WAVE + .1));
        if (t < .015 || u >= .9) return;
        const std::array<uint32_t, 4> ramp = u < .35 ? std::array<uint32_t, 4>{C[3], C[4], C[5], C[6]}
                                                     : std::array<uint32_t, 4>{stone.body, stone.light, C[4], C[5]};
        band(
            p, 0, .5, front(t) + 1, .16, std::max(1., 4 * std::pow(1 - u, .8)), u < .55 ? 1.1 : (.9 - u) / .35, ramp,
            [&](double g) { return (std::sin(g) < 0) == back; }, t < .09);
    }
    void fronts(Paint& p) const {
        const double u = clamp((t - .03) / (WAVE + .16));
        if (t < .03 || u >= 1) return;
        const double                  fr = front(t), k = u < .7 ? 1 : (1 - u) / .3;
        const auto                    shape = crest(u);
        const std::array<uint32_t, 3> cols  = u < .14   ? std::array<uint32_t, 3>{WHITE, C[6], C[5]}
                                              : u < .34 ? std::array<uint32_t, 3>{C[6], C[5], C[4]}
                                              : u < .55 ? std::array<uint32_t, 3>{C[5], C[4], stone.lip}
                                                        : std::array<uint32_t, 3>{stone.lip, stone.light, stone.dust};
        for (int side : {-1, 1}) {
            impact::frontCrescent(p, side * fr, side, shape.x, shape.y, cols, k);
            p.glow(side * fr, -shape.x * .3, 8 + shape.x * .7, C[4], .5 * (1 - u) * k);
        }
    }
    void update(double dt) override {
        t += dt;
        const double u = clamp((t - .015) / WAVE);
        if (u > 0 && u < 1) {
            const double                  fr = front(t), h = crest(u).x;
            const std::array<uint32_t, 3> grit{stone.light, stone.body, stone.lip};
            for (size_t k = 0; k < 2; ++k) {
                const int side = k ? 1 : -1;
                for (accumulator[k] += dt * 120 * (1 - u * .7); accumulator[k] > 1; accumulator[k]--) {
                    const double x = side * (fr - rng.range(1, 7)), y = -rng.range(.5, 3),
                                 vx = side * rng.range(15, 70) * (1 - u), vy = -rng.range(2, 12),
                                 life  = rng.range(1, 2.1);
                    const auto   color = rng.isChance(.3) ? dustLit : stone.dust;
                    const double size  = rng.range(2.5, 6.5) * (1 - .4 * u);
                    ps.spawn(rng, ParticleKind::Smoke, x, y, vx, vy, life, color, size, 0, ParticleFlag::Big);
                    if (rng.isChance(.5)) {
                        const double xx = side * (fr - rng.range(0, 3)), yy = -rng.range(.3, .9) * h,
                                     vxx = side * rng.range(-15, 25), vyy = -rng.range(8, 35), lt = rng.range(.3, .6);
                        const auto   cc = rng.isChance(.5) ? dustLit : stone.dust;
                        const double sz = rng.range(1.2, 2.4);
                        ps.spawn(rng, ParticleKind::Dust, xx, yy, vxx, vyy, lt, cc, sz);
                    }
                    if (rng.isChance(.4)) {
                        const double vxx = side * rng.range(10, 60), vyy = -rng.range(40, 140) * (1 - u),
                                     lt = rng.range(.5, 1);
                        const auto   cc = rng.pick<uint32_t>(grit);
                        ps.spawn(rng, ParticleKind::Grit, side * fr, -1, vxx, vyy, lt, cc, 1);
                    }
                    if (u < .45 && rng.isChance(.2)) {
                        const double vxx = side * rng.range(60, 200), vyy = -rng.range(30, 160),
                                     lt = rng.range(.12, .3);
                        ps.spawn(rng, ParticleKind::Spark, side * fr, -1, vxx, vyy, lt);
                    }
                }
            }
        }
        for (auto& b : bits.list) b.previousVy = b.vy;
        bits.update(dt, [](double) { return 0.; });
        for (auto& b : bits.list)
            if (b.previousVy > 70 && b.vy <= 0) {
                const int n = b.previousVy > 200 ? 3 : 2;
                for (int i = 0; i < n; ++i) {
                    const double x    = b.x + rng.range(-1.5, 1.5);
                    const int    side = rng.sign();
                    const double vx = side * rng.range(8, 35), vy = -rng.range(2, 16), life = rng.range(.3, .65),
                                 size = rng.range(1.3, 2.4) * (.7 + b.size * .25);
                    ps.spawn(rng, ParticleKind::Dust, x, -.5, vx, vy, life, stone.dust, size);
                }
                b.previousVy = 0;
            }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double fade = 1 - smooth(D - .9, D - .15, t), H = heat(t);
        if (t > .02)
            for (int x = -12; x <= 11; ++x) {
                const double q = std::abs(x + .5) / 12, depth = jsRound(2.6 * (1 - q * q));
                if (q < .84) {
                    for (int y = 0; y < depth; ++y) p.dot(x, y, impact::SOOT, fade);
                    p.dot(x, depth, H > .5 ? C[5] : H > .2 ? C[4] : stone.lip, fade);
                } else {
                    p.dot(x, -1, q > .93 ? stone.light : stone.lip, fade);
                    if (q < .9) p.dot(x, -2, stone.light, fade * .8);
                }
            }
        impact::drawCracks(
            p, cracks, [&](const impact::Crack& c) { return c.length * ease::outCubic(clamp((t - c.delay) / .1)); },
            [&](double d) { return H * clamp(1.2 - d / 40); }, {WHITE, C[5], C[4], C[3]}, impact::SOOT, stone.light,
            fade);
        for (const auto& e : embers) {
            const double k = 1 - (t - .1) / e.life;
            if (t < .1 || k <= 0) continue;
            const double f = .6 + .4 * std::sin(t * 23 + e.phase);
            p.px(e.x, e.y, k * f > .55 ? C[5] : k * f > .25 ? C[4] : C[3]);
            p.glow(e.x, e.y, 4, C[4], .35 * k * f);
        }
        ring(p, true);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        ring(p, false);
        fronts(p);
        for (const auto& b : bits.list) impact::rock(p, b, stone);
        ps.draw(p, Paint::Layer::Front);
        if (t < .035) {
            p.ellipse(0, 0, 30, 4.5, [&](double d) { return d > .8 ? C[4] : d > .5 ? C[6] : WHITE; });
            impact::star(p, 0, -1, burst, 1, 1, {C[4], C[6], WHITE, WHITE});
        } else if (t < .07)
            impact::star(p, 0, -1, burst, 1.25, .5, {C[3], C[5], C[6], WHITE});
        if (t < .14) p.glowLine(-80, 0, 80, 0, 10, C[5], .55 * (1 - t / .14));
        const double H = heat(t);
        if (H > 0) p.glow(0, 1, 46, C[4], .35 * H);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& groundSlamDescriptor() {
    static const std::array<Option, 2> options{
        {{"stone", 4278190080., 4294967295., false, 0, &PixelFxImpactSettings::stone, true},
         {"reach", 0, 512, false, 0, &PixelFxImpactSettings::reach}}};
    static const Descriptor d{
        {PixelFxKind::GroundSlam, "impacts/ground-slam", 288, 208, 144, 164, D, false},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 5;
            s.impact.stone = hex(0x4a3f69);
            s.impact.reach = 104;
            s.palette      = {hex(0x2a1712), hex(0x5e2c1c), hex(0xa4502a), hex(0xe8893a),
                              hex(0xffc764), hex(0xfff0b4), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<GroundSlam>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
