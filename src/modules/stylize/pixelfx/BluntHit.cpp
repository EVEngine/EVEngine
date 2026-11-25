// eve-hot-path
// Native adaptation of pixelfx impacts/blunt-hit.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Impact.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct Wedge {
    double angle, length, width, radius, start, out, back;
};
struct Puff {
    double x, y, vx, vy, radius, life, maximum, start, lift = 6;
};
struct BluntHit final : Kernel {
    const std::vector<uint32_t> C;
    const uint32_t              warm, dust;
    const impact::Stone         stone;
    const double                Y;
    Rng                         rng;
    double                      phase = 0, t = 0;
    std::array<Wedge, 9>        wedges;
    std::vector<Puff>           puffs, kicks;
    BasicBits<impact::Rock>     bits;
    Lights                      lights;
    explicit BluntHit(const PixelFxSettings& o)
        : C(o.palette),
          warm(o.impact.warm),
          dust(o.impact.dust),
          stone(o.impact.clod),
          Y(-o.impact.height),
          rng(o.seed) {
        phase = rng.range(0, TAU);
        lights.add(0, Y, 48, warm, .6, .4);
        lights.add(0, Y, 16, WHITE, .6, .07);
        lights.add(0, -2, 36, warm, .25, .4);
        for (size_t i = 0; i < 9; ++i)
            wedges[i] = {(double(i) / 9) * TAU + rng.range(-.2, .2),
                         rng.range(6, 15),
                         rng.range(3.2, 5),
                         rng.range(13, 16),
                         rng.range(.015, .04),
                         rng.range(.04, .06),
                         rng.range(.09, .14)};
        puffs.reserve(35);
        for (int i = 0; i < 30; ++i) {
            const double a = (double(i) / 30) * TAU + rng.range(-.12, .12), side = std::abs(std::cos(a)),
                         v = rng.range(40, 140) * (.6 + .5 * side), r = rng.range(2.4, 6.2) * (.75 + .35 * side),
                         life = rng.range(.35, .85), start = rng.range(.03, .1), lift = rng.range(4, 16);
            puffs.push_back({std::cos(a) * 9, Y + std::sin(a) * 6, std::cos(a) * v, std::sin(a) * v * .5, r, life, life,
                             start, lift});
        }
        for (int i = 0; i < 5; ++i) {
            const double x = rng.range(-6, 6), y = Y - rng.range(3, 6), vx = rng.range(-30, 30),
                         vy = -rng.range(20, 45), r = rng.range(3.5, 5.5), life = rng.range(.6, .85),
                         start = rng.range(.05, .1), lift = rng.range(14, 24);
            puffs.push_back({x, y, vx, vy, r, life, life, start, lift});
        }
        for (int i = 0; i < 5; ++i) {
            const int    s = rng.sign();
            const double a = -PI / 2 + s * rng.range(.5, 1.3), v = rng.range(70, 140);
            impact::Rock b;
            b.kind            = DebrisKind::Chunk;
            b.x               = s * rng.range(4, 8);
            b.y               = Y + rng.range(-3, 3);
            b.vx              = std::cos(a) * v;
            b.vy              = std::sin(a) * v;
            b.gravity         = 1;
            b.drag            = .6;
            b.bounce          = .28;
            b.life            = rng.range(.65, .85);
            b.angle           = rng.range(0, TAU);
            b.angularVelocity = s * rng.range(8, 16);
            b.size            = rng.isChance(.4) ? 2 : 1;
            b.points          = impact::rockShape(rng);
            bits.add(std::move(b));
        }
    }
    void update(double dt) override {
        t += dt;
        for (auto& q : puffs) {
            if (t < q.start) continue;
            q.life -= dt;
            q.vx -= q.vx * 4.5 * dt;
            q.vy -= q.vy * 4.5 * dt + q.lift * dt;
            q.x += q.vx * dt;
            q.y += q.vy * dt;
        }
        for (auto& b : bits.list) b.previousVy = b.vy;
        bits.update(dt, [](double) { return 0.; });
        for (auto& b : bits.list)
            if (b.previousVy > 60 && b.vy <= 0) {
                for (int i = 0; i < 2; ++i) {
                    const int    side = rng.sign();
                    const double vx = side * rng.range(8, 22), vy = -rng.range(3, 10), r = rng.range(1.2, 2);
                    kicks.push_back({b.x, -1, vx, vy, r, .4, .4, 0});
                }
                b.previousVy = 0;
            }
        for (auto& q : kicks) {
            q.life -= dt;
            q.vx -= q.vx * 4 * dt;
            q.vy -= q.vy * 4 * dt;
            q.x += q.vx * dt;
            q.y += q.vy * dt;
        }
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void puff(Paint& p, const Puff& q) const {
        const double k = q.life / q.maximum;
        if (t < q.start || k <= 0) return;
        const double r = q.radius * (.7 + (1 - k) * .85), a = std::pow(k, 1.25) * std::min(1., (t - q.start) / .03);
        const auto   c = mix(dust, warm, clamp(1 - (t - .03) / .25) * .55);
        p.disc(q.x, q.y, r, c, [&](double d) { return .58 * a * (1 - d * d * .75); });
        p.disc(q.x - r * .22, q.y - r * .32, r * .58, mix(c, WHITE, .55),
               [&](double d) { return .48 * a * (1 - d * d); });
    }
    void draw(Paint& p) const override {
        for (const auto& q : puffs) puff(p, q);
        for (const auto& q : kicks) puff(p, q);
        for (const auto& b : bits.list) impact::rock(p, b, stone);
        if (t < .058) {
            const int    f = t < .025 ? 0 : 1;
            const double R = f ? 13.5 : 11.5;
            p.shade(-R * 1.4, Y - R * 1.4, R * 1.4, Y + R * 1.4, [&](double x, double y, int sx, int sy) {
                const double dx = x, dy = (y - Y) * (f ? 1.25 : 1.1), d = jsHypot2(dx, dy), ang = std::atan2(dy, dx),
                             ph = phase + f * .6,
                             ro = R * (1 + .15 * std::sin(5 * ang + ph) + .08 * std::sin(9 * ang + ph * 2.3));
                if (d > ro) return;
                const double q = d / ro;
                const auto   c = f == 0    ? (q < .55   ? WHITE
                                              : q < .86 ? C[5]
                                                        : C[4])
                                 : q < .3  ? WHITE
                                 : q < .72 ? C[5]
                                 : q < .9  ? C[4]
                                           : C[3];
                p.put(sx, sy, c);
            });
        }
        for (const auto& w : wedges) {
            const double u = t - w.start;
            if (u < 0 || u > w.out + w.back) continue;
            const double s = u < w.out ? ease::outCubic(u / w.out) : 1 - ease::inQuad((u - w.out) / w.back),
                         L = w.length * s;
            if (L < 1.5) continue;
            const double ca = std::cos(w.angle), sa = std::sin(w.angle), r0 = w.radius + 2 * s,
                         hw = (w.width * (.6 + .4 * s)) / 2;
            const Point  tip{ca * r0, Y + sa * r0}, end{ca * (r0 + L), Y + sa * (r0 + L)}, n{-sa * hw, ca * hw};
            const auto   wedge = [&](double g) {
                return std::array<Point, 3>{
                    {{tip.x - ca * g, tip.y - sa * g},
                     {end.x + n.x * (1 + g * .45) + ca * g, end.y + n.y * (1 + g * .45) + sa * g},
                     {end.x - n.x * (1 + g * .45) + ca * g, end.y - n.y * (1 + g * .45) + sa * g}}};
            };
            p.poly(wedge(1), C[1]);
            p.poly(wedge(0), u < w.out + .02 ? WHITE : C[5]);
        }
        const double pop = std::exp(-t / .06);
        if (pop > .02) p.glow(0, Y, 28, warm, .5 * pop);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& bluntHitDescriptor() {
    static const std::array<Option, 4> options{
        {{"warm", 4278190080., 4294967295., false, 0, &PixelFxImpactSettings::warm, true},
         {"dust", 4278190080., 4294967295., false, 0, &PixelFxImpactSettings::dust, true},
         {"clod", 4278190080., 4294967295., false, 0, &PixelFxImpactSettings::clod, true},
         {"height", 0, 256, false, 0, &PixelFxImpactSettings::height}}};
    static const Descriptor d{
        {PixelFxKind::BluntHit, "impacts/blunt-hit", 128, 88, 64, 72, .9, false},
        7,
        [](PixelFxSettings& s) {
            s.seed          = 9;
            s.impact.warm   = hex(0xffc98a);
            s.impact.dust   = hex(0xb7a8a6);
            s.impact.clod   = hex(0x6e5a5a);
            s.impact.height = 22;
            s.palette       = {hex(0x1d1620), hex(0x4a3a3c), hex(0x7d6a66), hex(0xb09c90),
                               hex(0xdccab4), hex(0xf6ead2), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<BluntHit>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
