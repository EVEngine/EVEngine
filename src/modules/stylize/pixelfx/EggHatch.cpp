// eve-hot-path
// Native adaptation of pixelfx spawn/egg-hatch.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include <map>
#include "stylize/pixelfx/EggGeometry.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                               POP = 1.98, D = 3.1, CRUMBLE = D - .55;
constexpr std::array<std::array<double, 2>, 3> WOBBLES{{{.3, .11}, {.85, .19}, {1.4, .28}}};
struct ShellColor {
    uint32_t color;
    bool     edge;
};
struct ShellPiece {
    int                                       id;
    double                                    x, y, vx, vy, angle, angularVelocity, radius;
    std::map<std::pair<int, int>, ShellColor> pixels;
    double                                    life, max;
    bool                                      rest;
    double                                    born;
};
struct EggEmission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct EggDebris {
    double   x, y, vx, vy, gravity, drag, bounce, life, angle, angularVelocity, size;
    uint32_t color;
};
struct EggHatch final : Kernel {
    const std::vector<uint32_t> C, G;
    const int64_t               SPK;
    Rng                         R;
    const egg::Geometry         geometry;
    Particles                   ps;
    Bits                        bits;
    Lights                      lights;
    std::vector<ShellPiece>     pieces;
    double                      t = 0, cupA = 0, cupV = 0;
    bool                        popped = false, crumbled = false;
    std::array<bool, 3>         done{};
    explicit EggHatch(const PixelFxSettings& s)
        : C(s.palette), G(s.spawn.glow), SPK(s.spawn.speckle), R(s.seed), geometry(C, SPK, R) {
        ps.sparks = {WHITE, G[2], G[1], G[0]};
    }
    void emit(const EggEmission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void debris(const EggDebris& e) {
        Debris b;
        b.kind            = DebrisKind::Shard;
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
        b.color           = e.color;
        bits.add(std::move(b));
    }
    int stage() const {
        return t > WOBBLES[2][0] + .05 ? 3 : t > WOBBLES[1][0] + .05 ? 2 : t > WOBBLES[0][0] + .05 ? 1 : 0;
    }
    double angle() const {
        double a = 0;
        for (const auto& w : WOBBLES) {
            const double u = t - w[0];
            if (u > 0 && u < .55) a += w[1] * std::sin(u * 22) * std::exp(-u * 5);
        }
        if (t > POP - .14 && t < POP) a += .05 * std::sin(t * 120);
        return a;
    }
    double inner() const {
        return t < POP ? double(stage()) / 3 + .25 * std::max(0., std::sin(t * 9)) * (double(stage()) / 3)
                       : 1.6 * std::exp(-(t - POP) * 2.6);
    }
    template <class Velocity, class Life>
    void breakUp(bool top, int n, Velocity velocity, Life lifetime) {
        std::vector<Point> seeds;
        for (int i = 0; i < n; ++i)
            seeds.push_back({R.range(-egg::WIDTH / 2 + 1, egg::WIDTH / 2 - 1), R.range(0, egg::HEIGHT)});
        struct Group {
            size_t              seed;
            std::vector<size_t> pixels;
        };
        std::vector<Group> groups;
        for (size_t i = 0; i < geometry.pixels.size(); ++i) {
            const auto& e = geometry.pixels[i];
            if ((e.y >= egg::Geometry::zigAt(e.x)) != top) continue;
            size_t best     = 0;
            double distance = 1e9;
            for (size_t j = 0; j < seeds.size(); ++j) {
                const double d = jsHypot2(e.x - seeds[j].x, e.y - seeds[j].y);
                if (d < distance) {
                    distance = d;
                    best     = j;
                }
            }
            auto group = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.seed == best; });
            if (group == groups.end()) {
                groups.push_back({best, {}});
                group = groups.end() - 1;
            }
            group->pixels.push_back(i);
        }
        for (const auto& group : groups) {
            double                                     cx = 0, cy = 0;
            std::array<bool, egg::WIDTH * egg::HEIGHT> mine{};
            for (auto i : group.pixels) {
                const auto& e = geometry.pixels[i];
                cx += e.x + .5;
                cy += e.y + .5;
                mine[size_t(e.y * egg::WIDTH + e.x + egg::WIDTH / 2)] = true;
            }
            cx /= double(group.pixels.size());
            cy /= double(group.pixels.size());
            std::map<std::pair<int, int>, ShellColor> map;
            double                                    radius = 0;
            for (auto i : group.pixels) {
                const auto& e    = geometry.pixels[i];
                bool        edge = false;
                for (const auto& d : CROSS) {
                    const int x = e.x + int(d.x), y = e.y + int(d.y);
                    if (geometry.isOccupied(x, y) && !mine[size_t(y * egg::WIDTH + x + egg::WIDTH / 2)]) {
                        edge = true;
                        break;
                    }
                }
                const uint32_t c =
                    e.color == egg::Geometry::signedColor(C[1]) || e.color == egg::Geometry::signedColor(C[2])
                        ? C[3]
                        : uint32_t(e.color);
                map[{int(jsRound(e.x + .5 - cx)), int(jsRound(-(e.y + .5 - cy)))}] = {c, edge};
                radius = std::max(radius, jsHypot2(e.x + .5 - cx, e.y + .5 - cy));
            }
            radius += 1;
            const Point  v    = velocity(cx, cy);
            const double life = lifetime();
            pieces.push_back({int(pieces.size()) + 1, cx, -cy, v.x, v.y, 0, R.range(-10, 10), radius, std::move(map),
                              life, life, false, t});
        }
    }
    void pop() {
        popped = true;
        breakUp(
            true, 6,
            [&](double cx, double) {
                const double speed = R.range(8, 13), jitter = R.range(-14, 14), vertical = -R.range(140, 210);
                return Point{cx * speed + jitter, vertical};
            },
            [&] { return R.range(.95, 1.15); });
        for (int i = 0; i < 14; ++i) {
            const double a = -PI / 2 + R.range(-1.2, 1.2), sp = R.range(70, 170);
            debris({R.range(-5, 5), -egg::SEAM - R.range(0, 4), std::cos(a) * sp, std::sin(a) * sp, .9, .6, .3,
                    R.range(.55, .85), R.range(0, TAU), R.range(-20, 20), R.range(1.2, 2.4),
                    R.pick<uint32_t>(std::array{C[5], C[5], C[4], G[2]})});
        }
        for (int i = 0; i < 12; ++i) {
            const double a = -PI / 2 + R.range(-1.1, 1.1), sp = R.range(50, 110);
            emit({ParticleKind::Star, R.range(-3, 3), -egg::SEAM - 2, std::cos(a) * sp, std::sin(a) * sp,
                  R.range(.4, .9), R.pick<uint32_t>(std::array{WHITE, G[2], G[1]}), R.range(1, 2.2)});
        }
        for (int i = 0; i < 10; ++i) {
            const double a = -PI / 2 + R.range(-1, 1), sp = R.range(100, 200);
            emit({ParticleKind::Spark, R.range(-3, 3), -egg::SEAM - 1, std::cos(a) * sp, std::sin(a) * sp,
                  R.range(.2, .45)});
        }
        for (int i = 0; i < 4; ++i) {
            const double side = R.sign();
            emit({ParticleKind::Dust, side * R.range(5, 8), -1, side * R.range(25, 50), -R.range(2, 6),
                  R.range(.15, .25), C[5], R.range(1, 1.8)});
        }
        lights.add(0, -egg::SEAM - 10, 40, G[1], .9, .55);
        lights.add(0, -egg::SEAM - 2, 18, WHITE, .9, .12);
        cupV = R.sign() * 3;
    }
    void update(double dt) override {
        t += dt;
        for (size_t i = 0; i < WOBBLES.size(); ++i)
            if (!done[i] && t >= WOBBLES[i][0] + .05) {
                done[i]        = true;
                const double j = double(i);
                lights.add(i == 1 ? -3 : 2, -14 + j, 8 + j * 2, G[1], .25 + j * .08, .18);
                for (size_t k = 0; k < 2 + i * 2; ++k)
                    emit({ParticleKind::Star, R.range(-5, 5), -R.range(8, 17), R.range(-20, 20), -R.range(10, 30),
                          R.range(.25, .5), R.pick<uint32_t>(std::array{WHITE, G[2]}), R.range(.6, 1.4)});
                for (size_t k = 0; k < i + 1; ++k)
                    debris({R.range(-4, 4), -R.range(10, 16), R.range(-50, 50), -R.range(40, 90), .9, .5, .3,
                            R.range(.5, .8), R.range(0, TAU), R.range(-20, 20), R.range(1, 1.6), C[4]});
                const double x = R.signedRange(5, 7), vx = R.signedRange(8, 18);
                emit({ParticleKind::Dust, x, -.5, vx, -R.range(1, 3), R.range(.15, .25), C[5], R.range(.8, 1.4)});
            }
        if (!popped && t >= POP) pop();
        if (popped && t < POP + .6 && R.isChance(.35))
            emit({ParticleKind::Mote, R.range(-4, 4), -egg::SEAM - 1, R.range(-8, 8), -R.range(40, 80),
                  std::min(R.range(.4, .8), D - .1 - t), R.pick<uint32_t>(std::array{G[2], G[1], WHITE}), .7});
        if (popped && !crumbled && t >= CRUMBLE) {
            crumbled = true;
            breakUp(
                false, 4, [&](double cx, double) { return Point{cx * R.range(4, 8), -R.range(40, 80)}; },
                [&] { return R.range(.38, .48); });
            for (int i = 0; i < 3; ++i) {
                const double x = R.range(-6, 6), vx = R.signedRange(10, 25);
                emit({ParticleKind::Dust, x, -1, vx, -R.range(2, 6), R.range(.15, .25), C[5], R.range(1, 1.6)});
            }
        }
        if (popped) {
            cupV += (-cupA * 90 - cupV * 4) * dt;
            cupA += cupV * dt;
        }
        for (size_t left = pieces.size(); left > 0; --left) {
            const size_t i = left - 1;
            auto&        q = pieces[i];
            q.life -= dt;
            if (q.life <= 0) {
                pieces.erase(pieces.begin() + std::ptrdiff_t(i));
                continue;
            }
            if (q.rest) continue;
            q.vy += 900 * dt;
            q.vx -= q.vx * .6 * dt;
            q.x += q.vx * dt;
            q.y += q.vy * dt;
            q.angle += q.angularVelocity * dt;
            const double floor = -q.radius * .45;
            if (q.y > floor && q.vy > 0) {
                q.y = floor;
                if (q.vy > 80) {
                    q.vy *= -.35;
                    q.vx *= .6;
                    q.angularVelocity *= .5;
                } else {
                    q.rest = true;
                    q.vx = q.vy = 0;
                }
            }
        }
        bits.update(dt, [](double) { return 0.; });
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        if (!crumbled) {
            const double sx = popped ? 0 : -angle() * 8, k = popped ? 1 - smooth(POP, CRUMBLE, t) * .4 : 1;
            for (int y = -1; y <= 1; ++y)
                for (int x = -10; x <= 10; ++x) {
                    const double dx = (x + .5 - sx) / 9.5, dy = (y + .5) / 1.8, d = dx * dx + dy * dy;
                    if (d < 1) p.blend(x, y, C[0], .5 * k * (1 - d));
                }
        }
        const double g = inner();
        if (g > .05) p.glow(0, -3, 22, G[0], .18 * std::min(1.2, g));
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const int    st = stage();
        const double a = popped ? cupA * .03 : angle(), ca = std::cos(a), sa = std::sin(a), g = inner();
        const auto   crackAt = [&](int x, int y) {
            const int i = geometry.index(x, y);
            return i < 0 ? -1 : geometry.pixels[size_t(i)].crack;
        };
        if (!crumbled)
            p.shader(-12, -egg::HEIGHT - 4, 12, 1, [&](double x, double y, ShadeOutput& out) {
                const int  ux = int(std::floor(x * ca + y * sa)), uy = int(std::floor(-(-x * sa + y * ca))),
                           i      = geometry.index(ux, uy);
                const bool inside = i >= 0 && !(popped && uy >= egg::Geometry::zigAt(ux));
                if (!inside) {
                    bool neighbor = false;
                    for (const auto& d : CROSS) {
                        const int nx = ux + int(d.x), ny = uy + int(d.y);
                        if (geometry.isOccupied(nx, ny) && !(popped && ny >= egg::Geometry::zigAt(nx))) {
                            neighbor = true;
                            break;
                        }
                    }
                    if (neighbor && !(popped && uy >= egg::Geometry::zigAt(ux) && uy <= egg::Geometry::zigAt(ux) + 1))
                        out.c = C[0];
                    return;
                }
                const auto& e = geometry.pixels[size_t(i)];
                if (popped) {
                    out.c = uy >= egg::Geometry::zigAt(ux) - 1 ? (g > .9    ? WHITE
                                                                  : g > .4  ? G[2]
                                                                  : g > .12 ? G[1]
                                                                            : C[4])
                                                               : uint32_t(e.color);
                    if (uy >= egg::Geometry::zigAt(ux) - 1) {
                        out.k = .25 * g;
                        out.l = G[1];
                    }
                    return;
                }
                if (e.crack >= 0 && e.crack < st) {
                    const bool fresh = t - WOBBLES[size_t(e.crack)][0] < .17;
                    out.c = fresh                                                                        ? WHITE
                            : rand({jsUint(ux), jsUint(uy), jsUint(std::trunc(t * 20))}) < .25 + .1 * st ? G[1]
                                                                                                         : G[0];
                    out.k = .14 + .06 * st;
                    out.l = G[1];
                    return;
                }
                const int above = crackAt(ux, uy + 1), right = crackAt(ux + 1, uy);
                if ((above >= 0 && above < st) || (right >= 0 && right < st)) {
                    out.c = e.lit > .3 ? C[2] : C[1];
                    return;
                }
                const double   dn = st > 0 ? e.near[size_t(st - 1)] : 9, k = std::min(1., g * 1.5);
                const uint32_t base =
                    st > 0 && e.color != SPK ? mix(uint32_t(e.color), G[0], .05 * st * k) : uint32_t(e.color);
                out.c = dn < 1.6              ? mix(base, G[1], (st >= 2 ? .32 : .2) * k)
                        : dn < 2.7 && st >= 2 ? mix(base, G[1], .14 * k)
                                              : base;
            });
        const double q = t - POP;
        if (popped && q < 1.2 && !crumbled && g > .22) {
            const double  oy = -egg::SEAM + .5;
            constexpr int rx = 7;
            for (int y = -2; y <= 2; ++y)
                for (int x = -rx; x <= rx; ++x) {
                    const double d = jsHypot2((x + .5) / rx, (y + .5) / 2.2);
                    if (d > 1) continue;
                    if (g < .6 && d > .75) continue;
                    p.px(x, oy + y,
                         g > 1     ? (d < .7 ? WHITE : G[2])
                         : g > .6  ? (d < .5 ? G[2] : G[1])
                         : d < .45 ? G[1]
                                   : G[0]);
                }
            p.glow(0, oy, 16, G[1], .6 * g);
            if (q < .65) {
                const double rk = q / .65, L = 36 * ease::outCubic(clamp(q / .1)) * (1 - rk * .6);
                for (int i = 0; i < 7; ++i) {
                    const double an = -PI / 2 + (i - 3) * .24 + .05 * std::sin(t * 6 + i), len = L * (i % 2 ? .65 : 1);
                    const int    w  = i % 2 ? 1 : 2;
                    const double x1 = std::cos(an) * len, y1 = oy + std::sin(an) * len;
                    for (int j = 0; j < w; ++j)
                        p.lineFn(
                            j * .8 - .4, oy - 1, x1 + j * .8 - .4, y1,
                            [&](double u) { return u < .3    ? WHITE
                                                   : u < .65 ? G[2]
                                                             : G[1]; },
                            [&](double u) { return (1 - u * u) * (1.1 - rk); });
                    p.glowLine(0, oy, x1, y1, 5, G[1], .35 * (1 - rk));
                }
            }
            if (q < .1) p.glow(0, oy - 4, 30, WHITE, .6 * (1 - q / .1));
            p.glowLine(0, oy, 0, oy - 26, 8, G[1], .25 * g);
        }
        if (!popped && g > 0) p.glow(0, -egg::HEIGHT * .55, 7 + 2 * st, G[1], .05 * g);
        for (const auto& s : pieces) {
            const double c2 = std::cos(s.angle), s2 = std::sin(s.angle), f = std::min(1., s.life / .4);
            const int    r   = int(std::ceil(s.radius));
            const bool   hot = t - s.born < .35;
            for (int py = -r; py <= r; ++py)
                for (int px = -r; px <= r; ++px) {
                    const int  ux = int(jsRound(px * c2 + py * s2)), uy = int(jsRound(-px * s2 + py * c2));
                    const auto m = s.pixels.find({ux, uy});
                    if (m != s.pixels.end() && rand({jsUint(ux), jsUint(uy), jsUint(s.id)}) < f)
                        p.px(std::floor(s.x) + px, std::floor(s.y) + py,
                             m->second.edge ? (hot ? G[2] : C[4]) : m->second.color);
                }
            if (hot) p.glow(s.x, s.y, 6, G[1], .3);
        }
        bits.draw(p);
        if (popped && q < .9) p.sparkle(-3, -egg::SEAM - 6, jsRound(4 * (1 - q / .9)), WHITE, G[1], .4);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& eggHatchDescriptor() {
    static const std::array<Option, 2> options{
        {{"glow", 1, 64, false, 3, &PixelFxSpawnSettings::glow},
         {"speckle", -2147483648., 4294967295., false, 0, &PixelFxSpawnSettings::speckle, true}}};
    static const Descriptor d{
        {PixelFxKind::EggHatch, "spawn/egg-hatch", 120, 96, 60, 84, D, false},
        6,
        [](PixelFxSettings& s) {
            s.seed       = 31;
            s.palette    = {hex(0x1e1826), hex(0x4a3e52), hex(0x857684), hex(0xc4b4ac), hex(0xece0d2), hex(0xfffaf0)};
            s.spawn.glow = {hex(0xff8a2a), hex(0xffd25a), hex(0xfff3c0)};
            s.spawn.speckle = egg::Geometry::signedColor(hex(0x9a6a52));
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<EggHatch>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
