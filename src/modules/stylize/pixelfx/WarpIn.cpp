// eve-hot-path
// Native adaptation of pixelfx scifi/warp-in.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include <map>
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double SLAM = .34, OPEN = .52, SCAN0 = .62, SCAN1 = 1.4, COOL = .42, DONE = SCAN1 + COOL, CLOSE = 1.9,
                 SNAP = 2.16, TOP = -128;
double scanY(double t, double y0) { return lerp(y0 - 2, 1, ease::inOutSine(clamp((t - SCAN0) / (SCAN1 - SCAN0)))); }
double scanT(double y, double y0) {
    return SCAN0 + ((SCAN1 - SCAN0) * std::acos(1 - 2 * clamp((y + .5 - (y0 - 2)) / (3 - y0)))) / PI;
}
struct BodyCell {
    double   x, y;
    uint32_t color;
    bool     edge;
    double   ts, j;
};
struct WarpIn final : Kernel {
    const std::vector<uint32_t>             C;
    const double                            W;
    const PixelFxActor                      actor;
    const uint32_t                          seed;
    Rng                                     rng;
    Particles                               ps;
    Lights                                  lights;
    std::vector<BodyCell>                   body;
    std::map<double, std::array<double, 2>> rows;
    std::vector<double>                     rings;
    double                                  t = 0, nextRing = SLAM + .08, lastRow;
    bool                                    slammed = false, snapped = false;
    explicit WarpIn(const PixelFxSettings& o)
        : C(o.palette), W(o.scifi.width), actor(o.actor), seed(o.seed), rng(o.seed), lastRow(actor.y - 3) {
        ps.sparks      = {WHITE, C[6], C[5], C[3]};
        ps.haze        = C[5];
        const auto& A  = actor;
        const auto  on = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < A.width && y < A.height &&
                   A.mask[size_t(y) * size_t(A.width) + size_t(x)] > 0;
        };
        for (int my = 0; my < A.height; ++my)
            for (int mx = 0; mx < A.width; ++mx) {
                if (!on(mx, my)) continue;
                const double x = A.x + mx, y = A.y + my;
                const bool   edge = !on(mx - 1, my) || !on(mx + 1, my) || !on(mx, my - 1) || !on(mx, my + 1);
                const auto   c    = A.pixels[size_t(my) * size_t(A.width) + size_t(mx)];
                body.push_back({x, y, c ? c : C[2], edge, scanT(y, A.y), rand({o.seed, uint32_t(mx), uint32_t(my)})});
                auto [it, inserted] = rows.try_emplace(y, std::array<double, 2>{x, x});
                if (!inserted) {
                    it->second[0] = std::min(it->second[0], x);
                    it->second[1] = std::max(it->second[1], x);
                }
            }
    }
    PixelFxActorVisibility actorVisibility(double time) const override {
        return time >= DONE ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden;
    }
    double width() const {
        if (t < SLAM) return 0;
        if (t < CLOSE) return W * (1 + .45 * std::exp(-(t - SLAM) * 14) + .03 * std::sin(t * 31));
        return lerp(W, .7, ease::outCubic(clamp((t - CLOSE) / (SNAP - CLOSE))));
    }
    void column(Paint& p) const {
        const double w = width();
        if (w <= .05 || t > SNAP + .14) return;
        const double bottom = t - SLAM < .06 ? lerp(TOP, 0, ease::inCubic(clamp((t - SLAM) / .06) * .5 + .5))
                              : t > SNAP     ? lerp(0, TOP, ease::outExpo((t - SNAP) / .14))
                                             : 0;
        const bool   hero   = t - SLAM < .07;
        const double hollow = .78 * smooth(OPEN - .16, OPEN, t), thin = 1 - clamp((w - 1.5) / (W - 1.5));
        for (int y = int(std::floor(TOP)); y < bottom; ++y) {
            const double pulse = wrap((y - TOP) * .028 - t * 2.6, 1) < .09 ? 1 : 0;
            for (int x = int(std::floor(-w * 1.35)); x <= std::ceil(w * 1.35); ++x) {
                const double q = std::abs(x + .5) / w;
                if (q > 1.35) continue;
                if (hero) {
                    p.dot(x, y, q < .55 ? WHITE : q < .8 ? C[6] : q < 1 ? C[5] : C[4], q < 1 ? 1 : (1.35 - q) / .35);
                    continue;
                }
                if (w < 2.4) {
                    p.dot(x, y, q < .7 ? WHITE : C[6], q < 1 ? 1 : (1.35 - q) / .35);
                    continue;
                }
                double strand = 0;
                for (uint32_t s = 0; s < 5; ++s)
                    if (std::abs((x + .5) / w - (rand({seed, s}) - .5) * 1.3 - .06 * std::sin(t * (1.5 + s) + s * 2)) <
                        .05)
                        strand = 1;
                if (q < hollow)
                    p.blend(x + .5, y + .5, strand != 0 || pulse != 0 ? C[6] : C[5],
                            clamp(.12 + .3 * q * q + .3 * strand + .25 * pulse + .7 * thin));
                else if (q < .78) {
                    const int lvl = int(std::max(0., (q < .22 ? 0 : q < .45 ? 1 : 2) - pulse - strand));
                    p.dot(x, y, lvl == 0 ? WHITE : C[size_t(7 - lvl)]);
                } else if (q < .96)
                    p.dot(x, y, q > .86 && q < .93 ? WHITE : C[6]);
                else
                    p.dot(x, y, q < 1.1 ? C[5] : C[4], (1.35 - q) / .39);
            }
            if ((y & 7) == 0) {
                const double r = std::min(w + 8, y - TOP + 2);
                if (hollow < .4)
                    p.glow(0, y, std::min(w * 2.6 + 6, y - TOP + 2), C[4], hero ? .55 : .32);
                else
                    for (double s : {-1., 1.}) p.glow(s * (w + 3), y, r, C[4], .24);
            }
        }
    }
    // Functional strand noise uses the immutable seed; particle RNG remains separate.
    void ring(Paint& p, bool near) const {
        const double w = width();
        for (double t0 : rings) {
            const double k = (t - t0) / .5;
            if (k < 0 || k >= 1) continue;
            const double                  y = lerp(TOP + 6, -1, ease::inQuad(k)), rx = w * 1.18 + 1;
            const uint32_t                c = k < .25 ? WHITE : C[6];
            const std::array<uint32_t, 4> ramp{C[3], C[5], c, WHITE};
            band(p, 0, y, rx, 2.6 / rx, 1.5, 1, ramp, [&](double g) { return (std::sin(g) >= 0) == near; });
            if (near) p.glow(0, y, rx + 6, C[5], .25);
        }
    }
    void floor(Paint& p, bool near) const {
        const double on = t < SLAM ? smooth(0, SLAM, t) * .5 : 1 - smooth(SNAP, SNAP + .5, t);
        if (!near && on > .01) {
            const double rx = W * 1.9 + (t < SLAM ? 0 : 2 * std::sin(t * 6)), ry = 4;
            for (int y = -4; y <= 4; ++y)
                for (int x = int(std::floor(-rx)); x <= std::ceil(rx); ++x) {
                    const double d = jsHypot2((x + .5) / rx, (y + .5) / ry);
                    if (d > 1) continue;
                    p.dot(x, y, d < .35 ? C[7] : d < .6 ? C[6] : d < .85 ? C[5] : C[4], on * clamp((1.05 - d) * 2.4));
                }
            for (double x : {-rx * .6, 0., rx * .6}) p.glow(x, -1, rx * 1.3, C[4], .3 * on);
        }
        const auto keep = [&](double g) { return (std::sin(g) >= 0) == near; };
        for (double t0 : rings) {
            const double k = (t - t0 - .5) / .35;
            if (k >= 0 && k < 1) {
                const std::array<uint32_t, 4> ramp{C[3], C[4], C[5], C[6]};
                band(p, 0, 0, W * (1.2 + 1.3 * ease::outCubic(k)), .16, 1.5, 1 - k, ramp, keep);
            }
        }
        for (const auto& pair : std::array<std::array<double, 2>, 2>{{{0, 1}, {.08, .65}}}) {
            const double k = (t - SLAM - pair[0]) / .55;
            if (k >= 0 && k < 1) {
                const std::array<uint32_t, 4> ramp{C[3], C[5], C[6], WHITE};
                band(p, 0, .5, 92 * pair[1] * ease::outCubic(k), .16, std::max(1., 5 * (1 - k)), 1.15 - k, ramp, keep,
                     k < .25);
            }
        }
        const double k = (t - SNAP) / .45;
        if (k >= 0 && k < 1) {
            const std::array<uint32_t, 4> ramp{C[4], C[5], C[6], WHITE};
            band(p, 0, 0, 10 + 26 * ease::outCubic(k), .16, 2 * (1 - k) + .5, 1.1 - k, ramp, keep, k < .3);
        }
    }
    void hero(Paint& p) const {
        if (t >= DONE || t < SCAN0 - .2) return;
        const uint32_t blink = jsUint(std::floor(t * 24));
        for (const auto& b : body) {
            const double a = t - b.ts;
            if (a < 0) {
                if (b.edge && rand({jsUint(b.x), jsUint(b.y), blink}) < .6)
                    p.dot(b.x, b.y, C[1], .8 * smooth(SCAN0 - .25, SCAN0 - .05, t));
                continue;
            }
            const double k = a / COOL, dx = a < .06 && b.j < .3 ? (b.j < .15 ? -1 : 1) : 0;
            p.dot(b.x + dx, b.y,
                  k < .18   ? WHITE
                  : k < .45 ? mix(b.color, C[6], .72)
                  : k < .75 ? mix(b.color, C[5], .38)
                            : b.color);
            if (k < .5) p.light(b.x + dx, b.y, C[5], .22 * (1 - k / .5));
        }
    }
    void scanline(Paint& p) const {
        if (t < SCAN0 || t > SCAN1) return;
        const double y = std::floor(scanY(t, actor.y)), w = width() * 1.5;
        const auto   r = rows.find(y);
        for (int x = int(std::floor(-w)); x <= std::ceil(w); ++x) {
            const bool inside = r != rows.end() && x >= r->second[0] && x <= r->second[1];
            p.dot(x, y, inside ? WHITE : C[6], inside ? 1 : 1 - std::abs(x + .5) / (w + 1));
        }
        p.glowLine(-w, y, w, y, 5, C[5], .5);
        if (r != rows.end())
            for (double x : r->second) p.sparkle(x + .5, y, 2, C[6], C[5], .3);
    }
    void update(double dt) override {
        t += dt;
        if (t < SLAM - .04) {
            if (rng.isChance(.8)) {
                const double   a = rng.range(-PI * .95, -PI * .05), D = rng.range(30, 64), T = rng.range(.18, .28),
                               v = (D * 3) / (std::exp(3 * T) - 1), x = std::cos(a) * D, y = std::sin(a) * D * .8 - 2,
                               raw = jsHypot2(x, y + 2), l = raw == 0 ? 1 : raw;
                const uint32_t c = rng.isChance(.5) ? C[6] : C[5];
                ps.spawn(rng, ParticleKind::Streak, x, y, (-x / l) * v, (-(y + 2) / l) * v, T, c);
            }
            if (rng.isChance(.12)) {
                const int    s = rng.sign();
                const double x = s * rng.range(30, 56), y = -rng.range(0, 2), vx = -s * rng.range(40, 70),
                             vy = -rng.range(0, 4), life = rng.range(.3, .45), size = rng.range(2, 3);
                ps.spawn(rng, ParticleKind::Vapour, x, y, vx, vy, life, C[5], size);
            }
        }
        if (t >= SLAM && !slammed) {
            slammed = true;
            lights.add(0, -40, 120, C[4], 1, .55);
            lights.add(0, -10, 44, WHITE, .9, .14);
            for (int i = 0; i < 34; ++i) {
                const int    s = rng.sign();
                const double a = -PI / 2 + s * rng.range(.35, 1.45), sp = rng.range(110, 270),
                             x = rng.range(-W, W) * .6, life = rng.range(.25, .75);
                ps.spawn(rng, ParticleKind::Spark, x, -1, std::cos(a) * sp, std::sin(a) * sp, life);
            }
            for (int i = 0; i < 18; ++i) {
                const int      s = rng.sign();
                const double   x = s * rng.range(4, 18), y = -rng.range(0, 4), vx = s * rng.range(40, 150),
                               vy = -rng.range(2, 18), life = rng.range(.5, .95);
                const uint32_t c    = rng.isChance(.6) ? C[6] : C[5];
                const double   size = rng.range(3, 6);
                ps.spawn(rng, ParticleKind::Vapour, x, y, vx, vy, life, c, size);
            }
            for (int i = 0; i < 10; ++i) {
                const double   x = rng.range(-W, W) * 1.3, y = -rng.range(2, 40), vx = rng.range(-20, 20),
                               vy = -rng.range(10, 40), life = rng.range(.4, .8);
                const uint32_t c    = rng.isChance(.5) ? WHITE : C[6];
                const double   size = rng.range(1, 2.2);
                ps.spawn(rng, ParticleKind::Star, x, y, vx, vy, life, c, size);
            }
        }
        if (slammed && t < CLOSE) {
            if (t >= nextRing) {
                rings.push_back(nextRing);
                nextRing += .17;
            }
            const double w = width();
            if (rng.isChance(.5)) {
                const double   x = rng.range(-.8, .8) * w, y = TOP + rng.range(9, 36), vy = rng.range(60, 110),
                               life = rng.range(.3, .45);
                const uint32_t c    = rng.isChance(.5) ? WHITE : C[6];
                ps.spawn(rng, ParticleKind::Streak, x, y, 0, vy, life, c);
            }
            if (rng.isChance(.18)) {
                const int    s = rng.sign();
                const double x = s * w * rng.range(.8, 1.3), y = -rng.range(4, 70), vy = -rng.range(5, 20),
                             life = rng.range(.3, .6), size = rng.range(.6, 1.8);
                ps.spawn(rng, ParticleKind::Star, x, y, 0, vy, life, C[6], size);
            }
            if (rng.isChance(.1)) {
                const int    s = rng.sign();
                const double x = s * rng.range(8, 16), y = -rng.range(0, 3), vx = s * rng.range(20, 50),
                             vy = -rng.range(2, 8), life = rng.range(.5, .8), size = rng.range(2.5, 4);
                ps.spawn(rng, ParticleKind::Vapour, x, y, vx, vy, life, C[5], size);
            }
        }
        if (t > SCAN0 && t < SCAN1) {
            const double y = std::floor(scanY(t, actor.y));
            if (y != lastRow) {
                lastRow      = y;
                const auto r = rows.find(y);
                if (r != rows.end() && rng.isChance(.7))
                    for (int side = 0; side < 2; ++side)
                        if (rng.isChance(.6)) {
                            const int    s = side == 0 ? -1 : 1;
                            const double x = r->second[size_t(side)] + (side == 0 ? 0 : 1), vx = s * rng.range(40, 90),
                                         vy = rng.range(-20, 10), life = rng.range(.15, .3);
                            const uint32_t c = rng.isChance(.5) ? WHITE : C[6];
                            ps.spawn(rng, ParticleKind::Mote, x, y, vx, vy, life, c, .6);
                        }
            }
        }
        if (t >= DONE && t - dt < DONE)
            for (int i = 0; i < 6; ++i) {
                const auto&    b  = body[size_t(std::floor(rng.next() * double(body.size())))];
                const double   vx = rng.range(-6, 6), vy = -rng.range(6, 20), life = rng.range(.4, .8);
                const uint32_t c    = rng.isChance(.5) ? WHITE : C[6];
                const double   size = rng.range(1, 2);
                ps.spawn(rng, ParticleKind::Star, b.x + .5, b.y + .5, vx, vy, life, c, size);
            }
        if (t >= SNAP && !snapped) {
            snapped = true;
            lights.add(0, -1, 32, C[4], .5, .25);
            lights.add(0, -1, 10, WHITE, .5, .08);
            for (int i = 0; i < 22; ++i) {
                const double a = -PI / 2 + rng.range(-1.4, 1.4), sp = rng.range(90, 260), x = rng.range(-2, 2),
                             life = rng.range(.2, .6);
                ps.spawn(rng, ParticleKind::Spark, x, -1, std::cos(a) * sp, std::sin(a) * sp, life);
            }
            for (int i = 0; i < 12; ++i) {
                const double   x = rng.range(-16, 16), y = -rng.range(0, 46), vx = rng.range(-10, 10),
                               vy = -rng.range(8, 26), life = rng.range(.5, .9);
                const uint32_t c    = rng.isChance(.5) ? WHITE : C[6];
                const double   size = rng.range(.6, 1.8);
                ps.spawn(rng, ParticleKind::Star, x, y, vx, vy, life, c, size);
            }
            for (int i = 0; i < 18; ++i) {
                const auto     kind = rng.isChance(.6) ? ParticleKind::Star : ParticleKind::Mote;
                const double   x = rng.range(-6, 6), y = -rng.range(10, 100), vx = rng.range(-6, 6),
                               vy = -rng.range(14, 40), life = rng.range(.5, .75);
                const uint32_t c    = rng.isChance(.4) ? WHITE : rng.isChance(.5) ? C[6] : C[5];
                const double   size = rng.range(.5, 1.4), delay = rng.range(0, .25);
                ps.spawn(rng, kind, x, y, vx, vy, life, c, size, delay);
            }
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        floor(p, false);
        ring(p, false);
        column(p);
        if (t < SLAM) {
            const double a = smooth(0, SLAM * .8, t) * .8, head = lerp(TOP, 0, ease::outCubic(clamp(t / (SLAM * .6))));
            for (int y = int(TOP); y < head; ++y)
                p.dot(0, y, (y & 1) == 0 ? C[6] : C[5], a * (.35 + .65 * ((y - TOP) / -TOP)));
            p.glow(0, head, 10, C[5], .5 * a);
            const double rx = lerp(34, 12, ease::outCubic(t / SLAM)), k = smooth(0, .15, t);
            for (int i = 0; i < 48; ++i) {
                const double g = (double(i) / 48) * TAU + t * 3;
                if ((i / 4) % 2) continue;
                p.dot(std::cos(g) * rx, std::sin(g) * rx * .16, i % 4 == 0 ? WHITE : C[5], k);
            }
        }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        hero(p);
        scanline(p);
        ring(p, true);
        floor(p, true);
        if (t >= DONE && t < DONE + .3)
            p.outline({actor.width, actor.height, actor.mask}, actor.x, actor.y, C[6], 1 - (t - DONE) / .3);
        if (t > SLAM && t < SLAM + .16) p.sparkle(0, -2, 12 * (1 - (t - SLAM) / .16), C[6], C[5], .6);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& warpInDescriptor() {
    static const std::array<Option, 2> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"width", 0, 128, false, 0, &PixelFxScifiSettings::width}}};
    static const Descriptor            d{
        {PixelFxKind::WarpIn, "scifi/warp-in", 192, 168, 96, 128, 3, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 3;
            s.actor.source = PixelFxActorSource::Dummy;
            s.scifi.width  = 15;
            s.palette      = {hex(0x0b1030), hex(0x152a63), hex(0x1d4f9e), hex(0x2582d2),
                              hex(0x36b9ee), hex(0x8ae8ff), hex(0xd4fbff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<WarpIn>(s); },
        nullptr,
        nullptr,
        true,
        1};
    return d;
}
}  // namespace eve::stylize::pixelfx
