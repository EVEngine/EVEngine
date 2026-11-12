// eve-hot-path
// Native adaptation of pixelfx nature/root-snare.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/NatureLeafGeometry.h"
#include "stylize/pixelfx/NatureSpikeGeometry.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double SY = .34, CINCH = .62, BREAK = 1.75, D = 2.7;
constexpr size_t NP = 48;
struct Root {
    double                angle;
    int                   direction;
    double                turns, radius, leg, height, at;
    std::array<double, 2> buds;
    bool                  burst = false;
};
struct RootPoint {
    double x, y, z, s;
};
using RootPath = std::array<RootPoint, NP + 1>;
RootPath path(const Root& r, double cinch) {
    RootPath pts;
    for (size_t i = 0; i <= NP; ++i) {
        const double s = double(i) / NP, th = r.angle + r.direction * TAU * r.turns * s,
                     rad = lerp(r.radius, r.leg - cinch * 1.6, ease::outCubic(std::min(1., s * 3.5)));
        pts[i] = {std::cos(th) * rad, -r.height * std::pow(s, 1.05) + std::sin(th) * rad * SY, std::sin(th), s};
    }
    return pts;
}
void root(Paint& p, const RootPath& pts, double g, std::span<const uint32_t> C, bool back, double alpha) {
    const size_t n = size_t(std::floor(NP * g));
    for (size_t i = 0; i < n; ++i) {
        const auto A = pts[i], B = pts[i + 1];
        const bool front = (A.z + B.z) / 2 >= 0;
        if (front == back) continue;
        const double dx = B.x - A.x, dy = B.y - A.y, raw = jsHypot2(dx, dy), L = raw == 0 ? 1 : raw, nx = -dy / L,
                     ny = dx / L, up = nx * -.6 + ny * -.8 > 0 ? 1. : -1.,
                     w   = (1.4 + 3.6 * std::pow(1 - A.s, .8)) / 2 + .35;
        const int    dim = back ? 1 : 0;
        for (int y = int(std::floor(std::min(A.y, B.y) - w - 1)); y <= std::ceil(std::max(A.y, B.y) + w + 1); ++y)
            for (int x = int(std::floor(std::min(A.x, B.x) - w - 1)); x <= std::ceil(std::max(A.x, B.x) + w + 1); ++x) {
                if (y > 0) continue;
                const double px = x + .5 - A.x, py = y + .5 - A.y, u = (px * dx + py * dy) / (L * L);
                if (u < -.2 || u > 1.2) continue;
                const double d = px * nx + py * ny;
                if (std::abs(d) > w) continue;
                const double side = (d > 0 ? 1 : d < 0 ? -1 : 0) * up, q = std::abs(d) / w;
                int          i2 = q > .72 ? (side > 0 ? 7 : 1) : side > 0 ? (q > .3 ? 6 : 5) : q > .35 ? 2 : 3;
                if (q < .25 && rand({jsUint(jsRound(A.s * 40)), 3u}) < .4) i2 = 4;
                p.dot(x, y, C[size_t(std::max(1, i2 - dim))], alpha);
            }
        if (i % 7 == 3 && w > 1.2) p.dot(A.x - nx * up * (w + 1), A.y - ny * up * (w + 1), C[size_t(2 - dim)], alpha);
    }
}
struct Chip {
    double             x, y, vx, vy, angle, angularVelocity, size;
    nature::StoneShape shape;
    double             life;
};
struct ChipEmission {
    double x, y, vx, vy, angle, angularVelocity, size;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
struct RootSnare final : Kernel {
    const std::vector<uint32_t>       C, G;
    Rng                               R;
    Particles                         ps;
    Lights                            lights;
    std::vector<Chip>                 chips;
    std::array<Root, 5>               roots;
    std::array<std::vector<Point>, 2> cracks;
    double                            t       = 0;
    bool                              cinched = false, broken = false;
    explicit RootSnare(const PixelFxSettings& o) : C(o.palette), G(o.nature.glow), R(o.seed) {
        ps.sparks = {G[3], G[2], G[1], G[0]};
        for (size_t i = 0; i < roots.size(); ++i)
            roots[i] = {(double(i) / 5) * TAU + R.range(-.3, .3),
                        i % 2 ? 1 : -1,
                        R.range(1.1, 1.6),
                        R.range(14, 19),
                        R.range(8, 10),
                        R.range(13, 21),
                        .16 + double(i) * .07 + R.range(0, .04),
                        {{R.range(.35, .55), R.range(.6, .85)}}};
        cracks = {nature::floorCrack(R, -1, R.range(18, 26)), nature::floorCrack(R, 1, R.range(18, 26))};
    }
    double grow(const Root& r) const { return ease::outCubic(clamp((t - r.at) / .32)); }
    double squeeze() const {
        return smooth(CINCH - .03, CINCH + .05, t) * (1 - .4 * smooth(CINCH + .05, CINCH + .25, t));
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    void chip(const ChipEmission& e, double lo, double hi, bool capped) {
        auto         shape   = nature::randomStoneShape(R);
        const double sampled = R.range(lo, hi), life = capped ? std::min(sampled, D - .05 - t) : sampled;
        chips.push_back({e.x, e.y, e.vx, e.vy, e.angle, e.angularVelocity, e.size, shape, life});
    }
    void update(double dt) override {
        t += dt;
        const double f = std::min(1., dt * 120);
        if (t < .4 && R.isChance(.3 * f)) {
            const int sd = R.sign();
            emit({ParticleKind::Grit, sd * R.range(4, 22), -1, R.range(-20, 20), -R.range(30, 90), R.range(.5, .9),
                  R.pick<uint32_t>(std::array<uint32_t, 2>{{C[5], C[4]}}), 1});
        }
        for (auto& r : roots)
            if (!r.burst && t >= r.at) {
                r.burst        = true;
                const double x = std::cos(r.angle) * r.radius;
                for (int i = 0; i < 4; ++i)
                    chip({x, -1, R.range(-60, 60), -R.range(60, 160), R.range(0, TAU), R.range(-15, 15),
                          R.range(1.2, 2)},
                         .6, .9, false);
                for (int i = 0; i < 3; ++i)
                    emit({ParticleKind::Dust, x, -1, R.range(-25, 25), -R.range(4, 18), R.range(.4, .8), C[5],
                          R.range(2, 3.5)});
                lights.add(x, -4, 20, G[2], .35, .3);
            }
        if (!cinched && t >= CINCH) {
            cinched = true;
            lights.add(0, -10, 40, G[3], .5, .3);
            lights.add(0, -10, 16, WHITE, .4, .06);
            for (int i = 0; i < 8; ++i)
                emit({ParticleKind::Dust, R.range(-12, 12), -R.range(0, 4), R.signedRange(20, 60), -R.range(4, 14),
                      R.range(.5, .9), C[5], R.range(2, 3.5)});
            for (int i = 0; i < 6; ++i)
                emit({ParticleKind::Star, R.range(-9, 9), -R.range(4, 20), 0, -4, R.range(.25, .5), G[3],
                      R.range(.8, 1.8), R.range(0, .1)});
        }
        if (t > CINCH && t < BREAK && R.isChance(.06 * f))
            emit({ParticleKind::Mote, R.range(-9, 9), -R.range(3, 18), R.range(-6, 6), -R.range(14, 30),
                  R.range(.5, .9), G[3], .6});
        if (!broken && t >= BREAK) {
            broken = true;
            lights.add(0, -10, 36, G[2], .4, .25);
            for (const auto& r : roots) {
                const auto pts = path(r, 1);
                for (size_t i = 2; i < NP; i += 4) {
                    const auto   q = pts[i];
                    const double w = 1.4 + 3.6 * std::pow(1 - q.s, .8);
                    chip({q.x, std::min(q.y, -1.), (q.x < 0 ? -1 : 1) * R.range(25, 85) + q.x * 3, -R.range(50, 150),
                          R.range(0, TAU), R.range(-10, 10), clamp(w * .75, 1.5, 3)},
                         .7, .95, true);
                    if (i % 10 == 2)
                        emit({ParticleKind::Grit, q.x, std::min(q.y, -1.), R.range(-40, 40), -R.range(20, 80),
                              std::min(R.range(.6, .9), D - .05 - t),
                              R.pick<uint32_t>(std::array<uint32_t, 3>{{C[4], C[3], C[2]}}), R.isChance(.3) ? 2. : 1.});
                }
            }
            for (int i = 0; i < 10; ++i)
                emit({ParticleKind::Smoke, R.range(-12, 12), -R.range(2, 16), R.range(-25, 25), -R.range(5, 20),
                      std::min(R.range(.6, .9), D - .05 - t), C[5], R.range(3, 5), 0});
        }
        for (size_t i = chips.size(); i > 0; --i) {
            auto& c = chips[i - 1];
            c.life -= dt;
            const bool expired = c.life <= 0;
            c.vy += 950 * dt;
            c.x += c.vx * dt;
            c.y += c.vy * dt;
            c.angle += c.angularVelocity * dt;
            if (c.y > 0 && c.vy > 0) {
                if (c.vy > 110) {
                    c.y = 0;
                    c.vy *= -.3;
                    c.vx *= .5;
                } else {
                    c.y  = 0;
                    c.vy = c.vx = c.angularVelocity = 0;
                }
            }
            // Source also steps the removed local object; erase after this isolated step to keep the reference valid.
            if (expired) chips.erase(chips.begin() + std::ptrdiff_t(i - 1));
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double open = ease::outCubic(clamp(t / .25)), hot = smooth(0, .3, t) * (1 - smooth(.5, 1.4, t)),
                     fade = 1 - smooth(D - .6, D - .15, t);
        for (const auto& c : cracks) {
            const size_t m = size_t(std::max(2., std::ceil(double(c.size()) * open)));
            for (size_t k = 1; k < m; ++k) {
                const auto A = c[k - 1], B = c[k];
                p.line(A.x, A.y + 1, B.x, B.y + 1, C[0], fade);
                p.line(A.x, A.y, B.x, B.y, hot > .5 ? G[3] : hot > .2 ? G[2] : C[1], fade);
                if (k % 2 == 0) p.glow(B.x, B.y, 6, G[2], .3 * hot);
            }
        }
        const double hold = smooth(.3, CINCH, t) * (1 - smooth(BREAK, BREAK + .3, t)) * (.8 + .2 * std::sin(t * 8));
        if (hold > 0) {
            p.glow(0, -12, 30, G[2], .3 * hold);
            p.glow(0, -1, 22, G[3], .16 * hold);
        }
        if (t < BREAK)
            for (const auto& r : roots)
                if (t > r.at) root(p, path(r, squeeze()), grow(r), C, true, 1);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        if (t < BREAK) {
            for (const auto& r : roots) {
                if (t <= r.at) continue;
                const auto   pts = path(r, squeeze());
                const double g   = grow(r);
                root(p, pts, g, C, false, 1);
                for (const double b : r.buds) {
                    const auto   q  = pts[size_t(std::floor(b * NP))];
                    const double lk = clamp((t - CINCH - .1 - b * .2) / .25);
                    if (b > g || lk <= 0 || q.z < 0) continue;
                    nature::drawLeaf(p, q.x + 2, q.y - 2, 5 * lk, 1.6, -.6 + std::sin(t * 5 + b * 9) * .2, .3, G);
                }
            }
            const double e = t - CINCH;
            if (e > 0 && e < .12)
                for (const auto q : std::array<Point, 3>{{{-8, -6}, {8, -10}, {-7, -15}}})
                    p.sparkle(q.x, q.y, 5 * (1 - e / .12), C[7], G[3], .4);
            const double w = BREAK - t;
            if (w > 0 && w < .05)
                for (const auto& r : roots)
                    for (const auto q : path(r, 1))
                        if (q.z >= 0) p.px(q.x, q.y, WHITE);
        }
        const std::array<uint32_t, 7> bark{{C[0], C[1], C[2], C[2], C[3], C[4], C[5]}};
        for (const auto& c : chips)
            nature::drawRock(p, c.x, c.y, c.size, c.angle, c.shape, bark, std::min(1., c.life / .25));
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& rootSnareDescriptor() {
    static const std::array<Option, 2> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor}, {"glow", 1, 64, false, 4, &PixelFxNatureSettings::glow}}};
    static const Descriptor d{
        {PixelFxKind::RootSnare, "nature/root-snare", 128, 96, 64, 78, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 7;
            s.actor.source = PixelFxActorSource::Dummy;
            s.nature.glow  = {hex(0x123818), hex(0x2f7a2a), hex(0x6cb83a), hex(0xc8f070)};
            s.palette      = {hex(0x140c08), hex(0x2a1a10), hex(0x46301e), hex(0x64462a),
                              hex(0x86623a), hex(0xa8824e), hex(0xccaa70), hex(0xecd6a0)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<RootSnare>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
