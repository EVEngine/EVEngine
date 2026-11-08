// eve-hot-path
// Native adaptation of pixelfx scifi/emp-blast.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScifiArcs.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double POP = .42, GROW = .32, STROBE = 1.25, BREAK = 1.58;
struct Arc {
    double             time;
    std::vector<Point> points;
    double             life;
};
struct Dead {
    double   x, y, vx, vy, life;
    int      size;
    uint32_t color;
    double   phase;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
struct EmpBlast final : Kernel {
    const std::vector<uint32_t>     C;
    const uint32_t                  fringe, seed;
    const double                    maximumRadius;
    Rng                             R;
    Particles                       ps;
    Lights                          lights;
    std::vector<Arc>                arcs;
    std::vector<Dead>               dead;
    std::vector<std::vector<Point>> star;
    double                          t = 0, nextArc = 0;
    double                          starAt = -1;
    bool                            popped = false, broke = false;
    explicit EmpBlast(const PixelFxSettings& o)
        : C(o.palette), fringe(o.scifi.fringe), seed(o.seed), maximumRadius(o.scifi.radius), R(o.seed) {
        ps.sparks = {WHITE, C[6], C[5], C[4]};
        ps.haze   = C[5];
    }
    // Aggregate argument initialization preserves source left-to-right RNG consumption.
    void   emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    double radius() const {
        return t < POP ? 0
                       : maximumRadius * (.12 + .88 * ease::outExpo(clamp((t - POP) / GROW))) *
                             (1 + .04 * clamp((t - POP - GROW) / .8));
    }
    bool off() const {
        return t > STROBE && t < BREAK &&
               rand({seed, jsUint(std::floor(t * 30))}) < .2 + .5 * ((t - STROBE) / (BREAK - STROBE));
    }
    void charge(Paint& p) const {
        if (t >= POP) return;
        const double k = t / POP, r = 1 + 3.2 * k * k;
        const bool   tick = std::sin(TAU * (2 * t + 10 * t * t)) > 0;
        p.circle(0, -7, r + 1, C[4]);
        p.circle(0, -7, r, tick ? WHITE : C[6]);
        Rng rr(seed * 97u + jsUint(std::floor(t * 30)));
        for (int i = 0; i < 1 + std::floor(k * 3); ++i) {
            const double a = rr.range(0, TAU), l = r + 2 + rr.range(2, 6) * k;
            const auto   points =
                bolt(rr, std::cos(a) * r, -7 + std::sin(a) * r, std::cos(a) * l, -7 + std::sin(a) * l, .4, 2);
            scifi::strokeArc(p, points, {C[2], C[5], C[4]}, .7, .5);
        }
        p.glow(0, -7, 8 + 16 * k, C[4], .4 + .5 * k * (tick ? 1 : .6));
    }
    void bubble(Paint& p, bool near) const {
        const double r = radius();
        if (r <= 1 || t >= BREAK || off()) return;
        const double age = t - POP, roll = age * 40, ry = r * .16;
        const auto   slot = jsUint(std::floor(t * 30));
        // At most six source Map entries; repeated row keys take the last authored shift.
        std::array<std::pair<int, int>, 6> tears{};
        const int                          count = 2 + (t > STROBE ? 4 : 0);
        for (int i = 0; i < count; ++i)
            tears[size_t(i)] = {int(std::floor(-rand({seed, slot, uint32_t(i)}) * r)),
                                (rand({seed, slot, uint32_t(i), 1}) < .5 ? -1 : 1) *
                                    (2 + int(std::floor(rand({seed, slot, uint32_t(i), 2}) * 4)))};
        for (int y = int(std::floor(-r)); y <= std::ceil(ry); ++y) {
            int shift = 0;
            for (int i = 0; i < count; ++i)
                if (tears[size_t(i)].first == y) shift = tears[size_t(i)].second;
            for (int x = int(std::floor(-r)); x <= std::ceil(r); ++x) {
                const int    sx = x - shift;
                const double d  = jsHypot2(sx + .5, y < 0 ? y + .5 : 0) / r;
                if (d > 1) {
                    if (near && y < 0 && d < 1 + 1.4 / r && (x + y) % 2 == 0) p.px(x, y, fringe);
                    continue;
                }
                if (y >= 0) {
                    const double e = jsHypot2((sx + .5) / r, (y + .5) / ry);
                    if (e > 1 || e < .92 || (y > 0) != near) continue;
                    p.px(x, y, e > .97 ? C[6] : C[4]);
                    continue;
                }
                if (!near) continue;
                const double rim  = std::pow(d, 6);
                const bool   line = wrap(y - roll, 4) < 1, stripe = wrap(y - roll * .5, 22) < 4;
                if (d > .965)
                    p.px(x, y, shift ? WHITE : -y > r * .55 && sx < 0 ? WHITE : C[6]);
                else if (d > .93)
                    p.px(x, y, C[4]);
                else if (shift)
                    p.px(x, y, std::abs(sx) > r * .8 ? fringe : (x & 1) ? C[6] : C[5]);
                else
                    p.blend(x + .5, y + .5,
                            line && stripe ? C[6]
                            : line         ? C[5]
                            : stripe       ? C[4]
                                           : C[3],
                            .06 + .42 * rim + (line ? .14 + .24 * rim : 0) + (stripe ? .16 : 0));
            }
            if (shift && near) p.glowLine(-r, y, r, y, 4, C[5], .3);
        }
        if (near) {
            for (double a = .15; a < PI - .1; a += .35) p.glow(std::cos(a) * r, -std::sin(a) * r, 8, C[4], .12);
            for (uint32_t i = 0; i < 3; ++i) {
                const double k = rand({seed, slot, i, 7});
                if (k > .5) continue;
                const double a = rand({seed, slot, i, 8}) * PI, d = rand({seed, slot, i, 9}) * r * .85;
                p.rect(jsRound(std::cos(a) * d), jsRound(-std::sin(a) * d), 2 + std::floor(k * 8),
                       1 + std::floor(k * 4), k < .2 ? WHITE : C[5]);
            }
        }
    }
    void update(double dt) override {
        t += dt;
        if (t < POP - .05 && R.isChance(.6)) {
            const double a = R.range(-PI, 0), D = R.range(16, 40), T = R.range(.14, .22),
                         v = (D * 3) / (std::exp(3 * T) - 1);
            emit({ParticleKind::Streak, std::cos(a) * D, -5 + std::sin(a) * D * .7, -std::cos(a) * v,
                  -std::sin(a) * v * .7, T, R.isChance(.5) ? C[6] : C[5]});
        }
        const double sa = t - POP;
        const double sf = std::floor(t * 30);
        if (sa >= 0 && sa < .26 && sf != starAt) {
            starAt           = sf;
            const double len = sa < .05 ? ease::outBack(sa / .05, 2) : 1 - (sa - .05) / .26;
            star.clear();
            for (uint32_t i = 0; i < 7; ++i) {
                const double a = -PI + (double(i) / 6) * PI + rand({seed, i, 3}) * .3 - .15 + R.range(-.1, .1),
                             L = (20 + 16 * rand({seed, i, 5})) * std::max(.15, len);
                star.push_back(bolt(R, 0, -7, std::cos(a) * L, std::min(-1., -7 + std::sin(a) * L), .28, 4));
            }
        }
        if (sa >= .26) star.clear();
        if (t >= POP && !popped) {
            popped = true;
            for (int i = 0; i < 16; ++i) {
                const double                  a = R.range(-PI, 0), sp = R.range(50, 150);
                const double                  life = R.range(.3, .6);
                const std::array<uint32_t, 3> colors{WHITE, C[6], C[5]};
                emit({ParticleKind::Mote, 0, -7, std::cos(a) * sp, std::sin(a) * sp, life, R.pick<uint32_t>(colors),
                      .8});
            }
            lights.add(0, -20, 72, C[4], .9, .5);
            lights.add(0, -7, 30, WHITE, .9, .12);
            for (int i = 0; i < 60; ++i) {
                const double a = R.range(-PI, 0), sp = R.range(90, 220);
                emit({ParticleKind::Spark, 0, -7, std::cos(a) * sp, std::sin(a) * sp, R.range(.2, .6)});
            }
            for (int i = 0; i < 6; ++i)
                emit({ParticleKind::Smoke, R.range(-3, 3), -4, R.range(-12, 12), -R.range(10, 24), R.range(1, 1.6),
                      hex(0x3c3a52), R.range(2.5, 4), R.range(.05, .3)});
        }
        if (popped && !broke && t >= nextArc) {
            nextArc        = t + (t > STROBE ? R.range(.02, .05) : R.range(.05, .11));
            const double r = radius(), a0 = R.range(.1, PI - .1);
            const int    sign = R.sign();
            const double a1 = a0 + sign * R.range(.25, .6), rr = r * R.range(.92, 1);
            auto         points = bolt(R, std::cos(a0) * rr, -std::sin(a0) * rr, std::cos(a1) * rr,
                                       -std::max(.05, std::sin(a1)) * rr, .3, 4);
            arcs.push_back({t, std::move(points), R.range(.05, .1)});
            if (R.isChance(.35))
                emit({ParticleKind::Spark, std::cos(a1) * rr, -std::max(.05, std::sin(a1)) * rr, R.range(-80, 80),
                      R.range(-90, 10), R.range(.15, .3)});
        }
        if (t >= BREAK && !broke) {
            broke          = true;
            const double r = radius();
            lights.add(0, -r * .4, r * 1.3, C[5], .8, .3);
            const std::array<uint32_t, 4> colors{WHITE, C[6], C[5], C[4]};
            for (int i = 0; i < 70; ++i) {
                const double a = R.range(.05, PI - .05), d = r * R.range(.6, 1);
                dead.push_back({std::cos(a) * d, -std::sin(a) * d, std::cos(a) * R.range(5, 25), -R.range(0, 20),
                                R.range(.4, 1), R.isChance(.3) ? 2 : 1, R.pick<uint32_t>(colors), R.range(0, 9)});
            }
            for (int i = 0; i < 6; ++i) {
                const double a0 = R.range(.1, PI - .1), a1 = a0 + R.range(-.5, .5);
                arcs.push_back({t,
                                bolt(R, std::cos(a0) * r, -std::sin(a0) * r, std::cos(a1) * r * .6,
                                     -std::max(.05, std::sin(a1)) * r * .6, .35, 4),
                                .1});
            }
            for (int i = 0; i < 8; ++i)
                emit({ParticleKind::Smoke, R.range(-3, 3), -4, R.range(-6, 6), -R.range(8, 18), R.range(.8, 1.2),
                      hex(0x2f2c3c), R.range(2, 3.4), R.range(0, .35)});
        }
        if (broke && t < 3.2 - .8 && R.isChance(.05))
            emit({ParticleKind::Spark, R.range(-3, 3), -3, R.range(-60, 60), -R.range(40, 120), R.range(.15, .3)});
        std::erase_if(arcs, [&](const Arc& arc) { return t - arc.time > arc.life; });
        for (size_t i = dead.size(); i > 0; --i) {
            auto& d = dead[i - 1];
            d.life -= dt;
            if (d.life <= 0) {
                dead.erase(dead.begin() + std::ptrdiff_t(i - 1));
                continue;
            }
            d.vy += 60 * dt;
            d.x += d.vx * dt;
            d.y = std::min(-1., d.y + d.vy * dt);
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        bubble(p, false);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        charge(p);
        bubble(p, true);
        const double a = t - POP;
        if (a >= 0 && a < .06) {
            p.circle(0, -7, 12 * (1 - a / .06) + 3, C[6]);
            p.circle(0, -7, 9 * (1 - a / .06) + 1, WHITE);
        }
        if (a >= 0 && a < .4) {
            const double                  k = a / .4, rr = 6 + (maximumRadius + 14) * ease::outCubic(k);
            const std::array<uint32_t, 4> ramp{C[3], C[5], C[6], WHITE};
            band(p, 0, 0, rr, .16, std::max(1., 3 * (1 - k)), 1.2 - k, ramp, [](double) { return true; }, k < .3);
        }
        if (a >= 0 && a < .16) p.sparkle(0, -7, 10 * (1 - a / .16), C[6], C[5], .6);
        for (const auto& b : star) scifi::strokeArc(p, b, {C[3], C[6], C[5]}, 1 - (t - POP) / .3, 1.3);
        for (const auto& c : arcs)
            scifi::strokeArc(p, c.points, {C[2], C[5], C[4]}, 1 - (t - c.time) / (c.life + .03), .8);
        for (const auto& d : dead) {
            if (std::sin(t * 40 + d.phase) < -.3 && d.life < .6) continue;
            p.rect(d.x, d.y, d.size, d.size, d.life > .5 ? d.color : C[3]);
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& empBlastDescriptor() {
    static const std::array<Option, 2> options{
        {{"fringe", 4278190080., 4294967295., false, 0, &PixelFxScifiSettings::fringe, true},
         {"radius", 0, 256, false, 0, &PixelFxScifiSettings::radius}}};
    static const Descriptor d{
        {PixelFxKind::EmpBlast, "scifi/emp-blast", 152, 116, 76, 84, 3.2, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 33;
            s.scifi.fringe = hex(0xff5ad8);
            s.scifi.radius = 56;
            s.palette      = {hex(0x0d0820), hex(0x1c1658), hex(0x2d2aa0), hex(0x3f55e8),
                              hex(0x5a8eff), hex(0x9cc4ff), hex(0xdceaff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<EmpBlast>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
