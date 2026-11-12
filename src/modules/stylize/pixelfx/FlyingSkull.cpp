// eve-hot-path
// Native adaptation of pixelfx critters/flying-skull.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Critter.h"
#include "stylize/pixelfx/CritterFire.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4;
struct Motion {
    double x, y, vx, vy;
};
Motion at(double t, double hover) {
    const double a = (t / D) * TAU;
    return {3 * std::sin(a), -hover + 3 * std::sin(2 * a + .5), (3 * TAU * std::cos(a)) / D,
            (6 * TAU * std::cos(2 * a + .5)) / D};
}
double burst(double t, double a, double b) {
    return t <= a || t >= b ? 0 : std::pow(std::sin(PI * ((t - a) / (b - a))), .5);
}
double jaw(double t) {
    const double chatter = std::max(0., std::sin((t / D) * TAU * 22)), b1 = burst(t, .15, .75),
                 b2 = burst(t, 1.3, 1.95), gape = burst(t, 1.95, 2.3);
    return jsRound(std::max(chatter * 2.4 * std::max(b1, b2), gape * 3.4));
}
double flicker(double t) {
    return .74 + .16 * critter::loopNoise(t, D, 18, 0, 1) + .1 * critter::loopNoise(t, D, 46, 0, 2);
}
bool   inCranium(double x, double y) { return std::pow(x / 11, 2) + std::pow((y + 6) / 10.5, 2) <= 1; }
bool   inFace(double x, double y) { return y >= -3 && y <= 6.5 && std::abs(x) <= lerp(10.2, 7.2, (y + 3) / 9.5); }
bool   inSkull(double x, double y) { return inCranium(x, y) || inFace(x, y); }
double brow(double x) { return -6.6 + (8.5 - std::abs(x)) * .3; }
bool   inSocket(double x, double y) {
    return std::pow((std::abs(x) - 5.1) / 3.3, 2) + std::pow((y + 2.9) / 3.3, 2) <= 1 && y > brow(x);
}
double top(double x) { return -6 - 10.5 * std::sqrt(std::max(0., 1 - std::pow(x / 11, 2))); }
bool   inJaw(double x, double y, double base) {
    const double v = y - base, half = 7.4 - v * .45 - (v > 4 ? (v - 4) * 1.2 : 0);
    return v >= 0 && v <= 5.5 && std::abs(x) <= half;
}
void skull(critter::Grid<28, 40>& g, std::span<const uint32_t> B, std::span<const uint32_t> F, double open) {
    const auto warm = mix(B[6], F[6], .45), rim = mix(B[5], F[5], .3), socket = hex(0x120a0c);
    const auto lit = [](double x, double y, double k) { return inSkull(-4 + (x + 4) / k, -15 + (y + 15) / k); };
    g.clear();
    g.shade(-12, -17, 12, 8, [&](double x, double y) -> uint32_t {
        if (!inSkull(x, y)) return 0;
        auto c = lit(x, y, .45) ? B[5] : lit(x, y, .74) ? B[4] : B[3];
        if (std::abs(x) > 8.2 && y > -7 && y < -1) c = x < 0 ? B[3] : B[2];
        if (!inSkull(x + 2, y + 2.2) || (x > 0 && !inSkull(x + 2.6, y))) c = B[2];
        if (!inSkull(x + 1, y + 1.2) && y > 2) c = B[1];
        if (!inSkull(x, y - 1.5))
            c = std::abs(x) < 7 ? warm : rim;
        else if (!inSkull(x, y - 2.5) && std::abs(x) < 6)
            c = rim;
        if (y > brow(x) - 1.2 && y < brow(x) + .4 && std::abs(x) > 1.6 && std::abs(x) < 8.6) c = x < 0 ? B[4] : B[3];
        if (std::abs(x) > 6.2 && std::abs(x) < 9.2 && y > 0 && y < 2) c = x < 0 ? B[5] : B[3];
        if (y > 6) {
            if (std::abs(x) > 6.4) return 0;
            const double cx  = std::floor(x);
            const bool   gap = cx == -6 || cx == -3 || cx == 2 || cx == 5;
            c                = gap ? B[1] : y < 7 ? (x < 0 ? B[5] : B[4]) : cx == -1 || cx == 0 ? B[3] : B[4];
        }
        return c;
    });
    g.shade(-9, -7, 9, 1, [&](double x, double y) -> uint32_t {
        if (!inSocket(x, y)) return 0;
        if (!inSocket(x, y + 1.3)) return mix(B[2], F[4], .55);
        return socket;
    });
    constexpr std::array<Point, 8> NOSE{{{-1, 1}, {0, 1}, {-2, 2}, {-1, 2}, {0, 2}, {1, 2}, {-2, 3}, {1, 3}}};
    constexpr std::array<Point, 8> CRACK{
        {{4, -16}, {4, -15}, {5, -14}, {5, -13}, {4, -12}, {5, -11}, {6, -10}, {6, -9}}};
    for (auto q : NOSE) g.set(q.x, q.y, q.y == 3 ? B[1] : socket);
    g.set(-1, 3, B[2]);
    g.set(0, 3, B[2]);
    for (auto q : CRACK) {
        g.set(q.x, q.y, B[1]);
        g.set(q.x - 1, q.y, B[5]);
    }
    g.set(7, -7, B[1]);
    g.set(8, -6, B[1]);
    const double base = 8 + open;
    g.shade(-8, 4, 8, base + 6, [&](double x, double y) -> uint32_t {
        const double ax = std::abs(x);
        if (ax > 6.6 && ax < 8.1 && y > 4 && y < base + 2) return x < 0 ? B[3] : B[2];
        if (y < base || y > base + 5.5) return 0;
        const double v = y - base, half = 7.4 - v * .45 - (v > 4 ? (v - 4) * 1.2 : 0);
        if (ax > half) return 0;
        if (v < 1.5) {
            if (ax > 6) return x < 0 ? B[4] : B[3];
            const double cx  = std::floor(x);
            const bool   gap = cx == -5 || cx == -2 || cx == 1 || cx == 4;
            return gap ? B[1] : v < 1 ? B[5] : B[4];
        }
        return !inJaw(x + 1.6, y + 1.6, base) ? B[1] : v < 2.5 ? (x < 0 ? B[4] : B[3]) : B[3];
    });
    if (open > 0)
        for (int y = 8; y < base; ++y)
            for (int x = -6; x <= 5; ++x) g.set(x, y, y == base - 1 ? mix(socket, F[3], .5) : socket);
    g.outline([&](double, double, int, int ny) { return ny < 0 ? mix(B[0], F[1], .75) : B[0]; });
}
struct Tongue {
    double x, y, height, width, tag;
    bool   front;
};
std::array<Tongue, 18> prepareCrown(uint32_t seed) {
    std::array<Tongue, 18> result;
    size_t                 i = 0;
    for (int x = -10; x <= 10; x += 2)
        result[i++] = {double(x),
                       top(x) + 3,
                       13 + 13 * (1 - std::pow(double(x) / 11, 2)),
                       2.3 + rand({uint32_t(x + 20), seed, 3}) * .8,
                       37 + x * 3 + double(seed) * 11,
                       false};
    for (int s : {-1, 1}) result[i++] = {double(s * 11), -5, 10, 1.8, 61 + s * 13 + double(seed) * 5, false};
    for (double x = -5; x <= 5; x += 2.5)
        result[i++] = {
            x, top(x) + 1.5, 5 + 3 * rand({jsUint(x * 4 + 20), seed, 5}), 1.6, 91 + x * 5 + double(seed) * 7, true};
    std::stable_sort(result.begin(), result.end(),
                     [](const Tongue& a, const Tongue& b) { return std::abs(a.x) > std::abs(b.x); });
    return result;
}
double height(const Tongue& f, double t, double k) {
    const double leap = std::max(0., critter::loopNoise(t, D, 6, f.tag * 3.1, 70) - .55) * 1.6;
    return (f.height + .25 * f.height * critter::loopNoise(t, D, 8, f.tag * 1.7, 5) + f.height * leap) * (.9 + .1 * k);
}
struct Sample {
    ParticleKind kind;
    double       x, y, vx, vy, age, life;
    uint32_t     color = WHITE;
    double       size  = 1;
    ParticleFlag flags = ParticleFlag::None;
    double       seed  = 0;
};
void addSample(Particles& ps, const Sample& s) {
    if (s.age < 0 || s.age >= s.life) return;
    ps.list.push_back({s.kind, s.x, s.y, s.vx, s.vy, s.life - s.age, s.life, s.size, s.seed, 0, s.color, s.flags});
}
void particles(Paint& p, double t, const PixelFxSettings& o, std::span<const uint32_t> heat,
               std::span<const uint32_t> F, bool back) {
    Particles ps;
    ps.time   = t;
    ps.sparks = {F[7], F[6], F[4], F[2]};
    if (back) {
        cycle(t, D, 15, o.seed * 7u + 1u, 1.7, 2.5, [&](double age, double life, Rng& r, int) {
            const auto   s  = at(wrap(t - age, D), o.critter.hover);
            const double x0 = s.x + r.range(-6, 6), y0 = s.y - 22 - r.range(0, 8), vx = r.range(-5, 5),
                         vy = -r.range(16, 26);
            const auto   q  = fly(x0, y0, vx, vy, age, -12, 1.2);
            const double x = q.x + std::sin(age * 1.3 + r.next() * 6) * 3 * age, size = r.range(2.8, 4.4);
            addSample(ps, {ParticleKind::Smoke, x, q.y, 0, 0, age, life, mix(hex(0x5b5566), F[2], .3), size,
                           ParticleFlag::Big});
        });
        ps.draw(p);
        return;
    }
    cycle(t, D, 40, o.seed * 7u + 2u, .18, .36, [&](double age, double life, Rng& r, int) {
        const auto   s    = at(wrap(t - age, D), o.critter.hover);
        const bool   side = r.isChance(.3);
        const double a = side ? r.signedRange(.9, 1.45) : r.range(-.8, .8), x0 = s.x + std::sin(a) * 10.5,
                     y0 = s.y - 6 - std::cos(a) * 10 - (side ? 0 : r.range(2, 10)),
                     vx = r.range(-6, 6) + std::sin(a) * 6, vy = -r.range(18, 34);
        const auto   q = fly(x0, y0, vx, vy, age, -95, 3.2);
        const double x = q.x + std::sin(age * 17 + r.next() * 6) * .8, size = r.range(1, 2.2), seed = r.next() * 100;
        critter::coolingFirePuff(p, x, q.y, 1 - age / life, size, seed, heat);
    });
    cycle(t, D, 18, o.seed * 7u + 3u, .26, [&](double age, double life, Rng& r, int) {
        const auto   s  = at(wrap(t - age, D), o.critter.hover);
        const double fx = r.range(-10, 10), x0 = s.x + fx,
                     y0 = s.y + top(fx) - r.range(4, 16) * (1 - std::abs(fx) / 14), k = 1 - age / life,
                     x = x0 + r.range(-3, 3) * age, y = y0 - r.range(12, 20) * age,
                     rr = r.range(.9, 1.7) * (.4 + .6 * k);
        p.circle(x, y, rr + .6, k > .5 ? heat[5] : heat[6]);
        if (k > .35) p.circle(x, y - .3, rr * .6, k > .7 ? heat[2] : heat[4]);
    });
    cycle(t, D, 24, o.seed * 7u + 4u, 1, 2.4, [&](double age, double life, Rng& r, int) {
        const auto   s  = at(wrap(t - age, D), o.critter.hover);
        const double x0 = s.x + r.range(-11, 11), y0 = s.y - r.range(6, 24), seed = r.next() * 100,
                     vx = r.range(-22, 22), vy = -r.range(25, 70);
        const auto   q    = fly(x0, y0, vx, vy, age, -20, 1.6);
        const double size = r.isChance(.3) ? 2 : 1;
        addSample(ps, {ParticleKind::Ember, q.x + std::sin(age * 2.3 + seed) * 4 * std::min(1., age), q.y, 0, 0, age,
                       life, F[4], size, ParticleFlag::None, seed});
    });
    if (o.critter.floor)
        cycle(t, D, 5, o.seed * 7u + 5u, .9, 1.4, [&](double age, double life, Rng& r, int) {
            const auto   s = at(wrap(t - age, D), o.critter.hover);
            const double x = s.x + r.range(-6, 6), y = s.y - r.range(10, 18), vx = r.range(-28, 28),
                         vy = -r.range(50, 110);
            const auto   b  = critter::groundBounce(x, y, vx, vy, age, 430, -.5, .42);
            addSample(ps, {ParticleKind::Spark, b.x, b.y, b.rest ? .1 : b.vx, b.rest ? 0 : b.vy, age, life, 0, 1,
                           ParticleFlag::None, r.next() * 100});
        });
    ps.draw(p);
}
struct FlyingSkull final : AnalyticKernel {
    const std::array<Tongue, 18> tongues;
    explicit FlyingSkull(uint32_t seed) : tongues(prepareCrown(seed)) {}
    void drawBack(Paint& p, double time, const PixelFxSettings& o) const override {
        if (!o.critter.floor) return;
        const double t = critter::shift(time, D, o.seed), k = flicker(t);
        const auto   s = at(t, o.critter.hover);
        const auto&  F = o.palette;
        critter::floorShadow(p, s.x, 0, 9, 1.6, hex(0x0d0a12), .6);
        for (int x = -30; x <= 30; x += 6)
            p.glow(s.x + x, -1, 20, mix(F[4], F[5], .5), .15 * k * (1 - std::abs(double(x)) / 40));
    }
    void draw(Paint& p, double time, const PixelFxSettings& o) const override {
        const double            t = critter::shift(time, D, o.seed), k = flicker(t);
        const auto&             F = o.palette;
        const auto&             B = o.critter.bone;
        std::array<uint32_t, 9> heat;
        std::reverse_copy(F.begin(), F.end(), heat.begin());
        const auto   s    = at(t, o.critter.hover);
        const double lean = clamp(-s.vx / 60, -.8, .8), stretch = clamp(s.vy / 90, -.3, .45), X = jsRound(s.x),
                     Y    = jsRound(s.y);
        const auto   lick = [&](const Tongue& f) {
            return critter::loopNoise(t, D, 5, f.tag, 40) * 2 + lean * height(f, t, k) * .9 - clamp(f.x * .25, -2, 2);
        };
        for (int layer = 0; layer < 4; ++layer)
            for (const auto& f : tongues)
                if (!f.front)
                    critter::flameTongue(p, X + f.x, Y + f.y, height(f, t, k) * (1 + stretch),
                                         f.width * (1 - .25 * std::abs(lean)), lick(f), t, D, f.tag, heat, layer);
        particles(p, t, o, heat, F, true);
        critter::Grid<28, 40> g(-14, -20);
        skull(g, B, F, jaw(t));
        g.draw(p, 1, X, Y);
        for (int side : {-1, 1}) {
            const double ex = X + side * 5.1 - .5, ey = Y - 2.4,
                         fl = .75 + .25 * critter::loopNoise(t, D, 30, side * 9, 12);
            p.px(ex, ey, heat[0]);
            p.px(ex + 1, ey, heat[1]);
            p.px(ex, ey + 1, heat[2]);
            p.px(ex + 1, ey + 1, heat[3]);
            if (fl > .8) {
                p.px(ex, ey - 1, heat[3]);
                p.px(ex + 1, ey - 1, heat[5]);
            }
            p.glow(ex + 1, ey + 1, 6, F[5], .45 * fl);
            p.glow(ex + 1, ey + .5, 3, F[7], .5 * fl);
        }
        const double open = jaw(t);
        if (open > 0) p.glow(X, Y + 8 + open * .5, 4 + open * 2, F[5], .25 * open);
        for (uint32_t i = 0; i < 9; ++i) {
            Rng          r(hash({o.seed, i, 21}));
            const double born = 1.98 + i * .03, age = wrap(t - born, D), life = r.range(.25, .45);
            if (age >= life) continue;
            const auto   b = at(born, o.critter.hover);
            const double x = b.x + r.range(-3, 3), vx = r.range(-22, 22), vy = r.range(40, 75);
            const auto   q    = fly(x, b.y + 11, vx, vy, age, -70, 3.5);
            const double size = r.range(1.1, 2), seed = r.next() * 100;
            critter::coolingFirePuff(p, q.x, q.y, 1 - age / life, size, seed, heat);
        }
        for (int layer = 0; layer < 4; ++layer)
            for (const auto& f : tongues)
                if (f.front)
                    critter::flameTongue(p, X + f.x, Y + f.y, height(f, t, k) * (1 + stretch), f.width, lick(f) * .6, t,
                                         D, f.tag, heat, layer);
        particles(p, t, o, heat, F, false);
        const double brightness = .92 + .08 * std::sin((t / D) * TAU * 2);
        p.glow(X + lean * 4, Y - 20, 74 * brightness * (.9 + .1 * k), mix(F[4], F[5], .5), .48 * k);
        p.glow(X, Y - 21, 24, F[6], .3 * k);
        p.glow(X, Y - 15, 9, F[8], .16 * k);
    }
};
}  // namespace
const Descriptor& flyingSkullDescriptor() {
    static const std::array<Option, 3> options{{{"bone", 1, 64, false, 7, &PixelFxCritterSettings::bone},
                                                {"hover", 0, 256, false, 0, &PixelFxCritterSettings::hover},
                                                {"floor", 0, 0, false, 0, &PixelFxCritterSettings::floor}}};
    static const Descriptor            d{{PixelFxKind::FlyingSkull, "critters/flying-skull", 96, 132, 48, 120, D, true},
                                         9,
                                         [](PixelFxSettings& s) {
                                  s.seed          = 1;
                                  s.critter.hover = 40;
                                  s.critter.floor = true;
                                  s.palette       = {hex(0x4a1712), hex(0x8a1f17), hex(0xc9281f),
                                                     hex(0xf2561d), hex(0xff8a2a), hex(0xffc23d),
                                                     hex(0xffe26b), hex(0xfff3a6), WHITE};
                                  s.critter.bone  = {hex(0x1a1114), hex(0x3e2b2a), hex(0x6b5144), hex(0x9e8467),
                                                     hex(0xcdb892), hex(0xefe2bf), hex(0xfffaea)};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<FlyingSkull>(s.seed);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
