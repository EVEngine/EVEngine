// eve-hot-path
// Native adaptation of pixelfx shadow/hex-sigil.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ShadowSimulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double CY = -50, RO = 28, RI = 22, RC = 6, GLYPH = 1.62, FLARE = 2.6, BREAK = 2.74;
struct InkPoint {
    double x, y, time;
};
struct Node {
    int x, y;
};
struct Segment {
    Node a, b;
};
struct Glyph {
    double               x, y, time;
    std::vector<Segment> segments;
    bool                 popped = false;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct PuffEmission {
    double x, y, vx, vy, life, size, delay = 0;
};
struct Chip {
    double   x, y, vx, vy, gravity, life, angle, va, size;
    uint32_t color;
};
struct HexSigil final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         R;
    std::vector<InkPoint>       points;
    std::array<Glyph, 7>        glyphs{};
    Particles                   ps, embers;
    shadow::Puffs               puffs;
    Bits                        bits;
    Lights                      lights;
    double                      t = 0, end = .12;
    size_t                      cursor = 0;
    bool                        flared = false, broke = false;
    explicit HexSigil(const PixelFxSettings& o) : C(o.palette), R(o.seed) {
        ps.sparks         = {WHITE, C[7], C[6], C[5]};
        embers.sparks     = {C[7], C[6], C[5], C[4]};
        puffs.flame       = {WHITE, C[7], C[6], C[5], C[4], C[3], C[2], C[1], C[0]};
        puffs.soot        = {mix(C[3], C[2], .5), mix(C[2], C[1], .5)};
        puffs.cap         = mix(C[4], C[2], .5);
        puffs.rim         = {C[6], C[5], C[4]};
        const auto circle = [](double radius, double a0) {
            const int          n = int(std::ceil(radius * TAU));
            std::vector<Point> path;
            path.reserve(size_t(n + 1));
            for (int i = 0; i <= n; ++i) {
                const double a = a0 + (double(i) / n) * TAU;
                path.push_back({std::cos(a) * radius, CY + std::sin(a) * radius});
            }
            return path;
        };
        std::array<std::vector<Point>, 5> paths;
        paths[0] = circle(RO, -PI / 2);
        for (int i = 0; i <= 7; ++i) {
            const double a = -PI / 2 + ((i * 3) % 7) * (TAU / 7);
            paths[1].push_back({std::cos(a) * RI, CY + std::sin(a) * RI});
        }
        paths[2] = circle(RI, -PI / 2);
        paths[3] = circle(RC, PI / 2);
        paths[4] = {{0, CY - 3.5}, {2, CY}, {0, CY + 3.5}, {-2, CY}, {0, CY - 3.5}};
        constexpr std::array<double, 5> speeds{420, 700, 550, 380, 300};
        for (size_t k = 0; k < paths.size(); ++k) {
            const auto& path = paths[k];
            for (size_t i = 1; i < path.size(); ++i) {
                const auto   a = path[i - 1], b = path[i];
                const double L = jsHypot2(b.x - a.x, b.y - a.y);
                for (double d = 0; d < L; d += .5)
                    points.push_back({a.x + ((b.x - a.x) * d) / L, a.y + ((b.y - a.y) * d) / L, end + d / speeds[k]});
                end += L / speeds[k];
            }
            end += .04;
        }
        for (size_t i = 0; i < glyphs.size(); ++i) {
            Rng                  g(hash({o.seed, uint32_t(i), 31u}));
            std::vector<Segment> segments{{{1, 0}, {1, 2}}};
            // The source consumes a fresh random sample in every loop-condition evaluation.
            for (int k = 0; k < 2 + (g.next() < .5 ? 1 : 0); ++k) {
                const Node a{g.integer(0, 2), g.integer(0, 2)};
                const Node b{std::clamp(a.x + g.integer(-1, 1), 0, 2), std::clamp(a.y + g.integer(-1, 1), 0, 2)};
                if (a.x != b.x || a.y != b.y) segments.push_back({a, b});
            }
            const double angle = -PI / 2 + (double(i) * TAU) / 7 + PI / 7, rr = (RO + RI) / 2;
            glyphs[i] = {std::cos(angle) * rr, CY + std::sin(angle) * rr, GLYPH + double(i) * .07, std::move(segments),
                         false};
        }
    }
    const InkPoint& pen() const { return points[std::min(points.size() - 1, cursor)]; }
    void            emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void            ember(const Emission& e) { embers.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void            puff(const PuffEmission& e) {
        puffs.spawn(R, shadow::PuffKind::Soot, e.x, e.y, e.vx, e.vy, e.life, e.size, e.delay);
    }
    void chip(const Chip& e) {
        Debris b;
        b.kind            = DebrisKind::Shard;
        b.x               = e.x;
        b.y               = e.y;
        b.vx              = e.vx;
        b.vy              = e.vy;
        b.gravity         = e.gravity;
        b.drag            = .6;
        b.bounce          = .25;
        b.life            = e.life;
        b.angle           = e.angle;
        b.angularVelocity = e.va;
        b.size            = e.size;
        b.color           = e.color;
        bits.add(std::move(b));
    }
    void update(double dt) override {
        t += dt;
        while (cursor + 1 < points.size() && points[cursor + 1].time <= t) ++cursor;
        if (t > .12 && t < end) {
            const auto& q = pen();
            if (R.isChance(dt * 70))
                emit({ParticleKind::Spark, q.x, q.y, R.range(-70, 70), R.range(-100, 20), R.range(.12, .35)});
            if (R.isChance(dt * 10))
                ember({ParticleKind::Ember, q.x, q.y, R.range(-12, 12), R.range(5, 25), R.range(.5, .9)});
        }
        if (t > .2 && t < FLARE - .05 && R.isChance(dt * (8 + 14 * smooth(.2, FLARE, t)))) {
            const double a = R.range(0, TAU), radius = R.range(40, 60), life = R.range(.22, .32),
                         v = (radius * 3) / (std::exp(3 * life) - 1);
            emit({ParticleKind::Streak, std::cos(a) * radius, CY + std::sin(a) * radius, -std::cos(a) * v,
                  -std::sin(a) * v, life, R.isChance(.5) ? C[7] : C[6]});
        }
        for (auto& g : glyphs)
            if (!g.popped && t >= g.time) {
                g.popped = true;
                lights.add(g.x, g.y, 14, C[6], .7, .2);
                for (int i = 0; i < 4; ++i)
                    emit({ParticleKind::Spark, g.x, g.y, R.range(-60, 60), R.range(-70, 20), R.range(.1, .25)});
            }
        if (t >= FLARE && !flared) {
            flared = true;
            lights.add(0, CY, 96, C[5], 1.4, .45);
            lights.add(0, CY, 44, C[6], 1, .14);
            for (int i = 0; i < 24; ++i) {
                const double a = R.range(0, TAU), sp = R.range(60, 130);
                emit({ParticleKind::Spark, std::cos(a) * 13, CY + std::sin(a) * 13, std::cos(a) * sp,
                      std::sin(a) * sp - 30, R.range(.2, .45)});
            }
        }
        if (t >= BREAK && !broke) {
            broke = true;
            for (size_t i = 0; i + 1 < points.size(); i += 7) {
                const auto   q = points[i], n = points[i + 1];
                const double a = std::atan2(n.y - q.y, n.x - q.x);
                chip({q.x, q.y, R.range(-8, 8), R.range(-12, 6), R.range(.22, .45), R.range(.8, 1.2), a, R.range(-3, 3),
                      R.range(2.5, 3.5), R.pick<uint32_t>(std::array<uint32_t, 4>{C[7], C[6], C[6], C[5]})});
                if (i % 27 == 0)
                    ember({ParticleKind::Ember, q.x, q.y, R.range(-10, 10), -R.range(10, 40), R.range(.6, 1.1)});
            }
            for (const auto& g : glyphs)
                puff({g.x, g.y, R.range(-6, 6), -R.range(8, 18), R.range(.7, 1.1), R.range(3, 5)});
            for (int i = 0; i < 8; ++i)
                puff({R.range(-20, 20), CY + R.range(-20, 20), R.range(-8, 8), -R.range(6, 16), R.range(.8, 1.2),
                      R.range(3, 6), R.range(0, .15)});
        }
        lights.update(dt);
        const auto floor = [](double) { return 0.; };
        bits.update(dt, floor);
        ps.update(dt, R, floor, [](double, double) {});
        embers.update(dt, R, floor, [](double, double) {});
        puffs.update(dt, R, floor);
    }
    void drawBack(Paint& p) const override {
        const shadow::PuffLight light{0, CY, broke ? .3 : 0, 50};
        puffs.draw(p, shadow::PuffKind::Soot, &light);
        const double k = t < BREAK ? smooth(.1, .6, t) : 1 - smooth(BREAK, BREAK + .6, t);
        if (k > 0) p.glow(0, CY, 52, C[4], .3 * k);
    }
    void draw(Paint& p) const override {
        if (!broke) {
            const double hold  = t > end ? .5 + .5 * std::sin((t - end) * 9) : 0;
            const bool   white = t >= FLARE;
            for (const auto& q : points) {
                if (q.time > t) break;
                const double age = t - q.time;
                p.px(q.x, q.y,
                     white        ? WHITE
                     : age < .05  ? WHITE
                     : age < .15  ? C[7]
                     : age < .35  ? C[6]
                     : hold > .75 ? C[6]
                                  : C[5]);
            }
            if (white)
                for (const auto& q : points)
                    if (int32_t(q.x * 2) % 3 == 0) p.light(q.x, q.y, C[6], .5);
            for (const auto& g : glyphs) {
                if (t < g.time) continue;
                const double age   = t - g.time;
                const auto   color = white || age < .06 ? WHITE : age < .2 ? C[7] : hold > .75 ? C[7] : C[6];
                for (const auto& seg : g.segments)
                    p.line(g.x - 2 + seg.a.x * 2, g.y - 2 + seg.a.y * 2, g.x - 2 + seg.b.x * 2, g.y - 2 + seg.b.y * 2,
                           color);
                if (age < .18) shadow::ringFast(p, g.x, g.y, 2 + 4 * (age / .18), 1, age < .09 ? C[7] : C[5]);
                p.glow(g.x, g.y, 6, C[5], .25);
            }
            if (t > .12 && t < end + .05) {
                const auto& q = pen();
                p.sparkle(q.x, q.y, 3 + std::sin(t * 60), C[7], C[6], .6);
                p.glow(q.x, q.y, 16, C[6], .55);
            }
        }
        const double fu = (t - FLARE) / .35, bu = (t - FLARE) / .22;
        if (bu > 0 && bu < 1) p.sparkle(0, CY, 18 * (1 - bu), C[7], C[5], .9 * (1 - bu));
        if (fu > 0 && fu < 1)
            shadow::ringFast(p, 0, CY, RO + 3 + 12 * ease::outCubic(fu), std::max(1., 3 * (1 - fu)),
                             fu < .4   ? WHITE
                             : fu < .7 ? C[6]
                                       : C[4]);
        bits.draw(p);
        puffs.draw(p, shadow::PuffKind::Flame);
        ps.draw(p);
        embers.draw(p);
        for (const auto& l : lights.list) {
            const double q = l.life / l.max;
            p.glow(l.x, l.y, l.radius * (.6 + .4 * q), l.strength > 1 ? C[5] : C[6], l.strength * q * q);
        }
    }
};
}  // namespace
const Descriptor& hexSigilDescriptor() {
    static const Descriptor d{
        {PixelFxKind::HexSigil, "shadow/hex-sigil", 168, 128, 84, 112, 4.2, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 7;
            s.palette = {hex(0x06040b), hex(0x150c26), hex(0x2a1150), hex(0x461a84),
                         hex(0x6f2cc0), hex(0x9d5cec), hex(0xcfa6ff), hex(0xf6eaff)};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<HexSigil>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
